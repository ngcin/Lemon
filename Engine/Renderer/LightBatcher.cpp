#include "Renderer/LightBatcher.h"

#include <cstring>

#include "Core/Log.h"
#include "Core/Math.h"
#include "Renderer/EmbeddedShaders.h"

namespace lemon::renderer {

void LightBatcher::Init(rhi::Device& device, rhi::Format colorFormat, uint32_t ringSlot) {
    // 幂等（SpriteBatcher 评审 D3 同款）：重复 Init 先摘旧 token 防回调表累积 UAF
    if (recreateCbId_ != 0) device.RemoveRecreateCallback(recreateCbId_);
    device_ = &device;
    colorFormat_ = colorFormat;
    ringSlot_ = ringSlot;
    CreateGeometry();
    EnsureCapacity(kInitialCapacity);
    CreatePipelines();

    recreateCbId_ = device.AddRecreateCallback("LightBatcher", [this](rhi::Device& d) {
        device_ = &d;
        cornerVB_ = {};
        indexIB_ = {};
        instanceRing_ = {};
        ringMapped_ = nullptr;
        for (auto& p : pipelines_) p = {};
        CreateGeometry();
        EnsureCapacity(kInitialCapacity);
        CreatePipelines();
    });
}

void LightBatcher::CreateGeometry() {
    cornerVB_ = device_->CreateBuffer({.size = sizeof(float) * 2 * 4,
                                       .usage = (uint32_t)rhi::BufferUsage::Vertex,
                                       .debugName = "lightQuadCorners"});
    const float corners[4][2] = {{-0.5f, -0.5f}, {0.5f, -0.5f}, {0.5f, 0.5f}, {-0.5f, 0.5f}};
    std::memcpy(device_->MapBuffer(cornerVB_), corners, sizeof(corners));
    const uint16_t indices[6] = {0, 1, 2, 0, 2, 3};
    indexIB_ = device_->CreateBuffer({.size = sizeof(indices),
                                      .usage = (uint32_t)rhi::BufferUsage::Index,
                                      .debugName = "lightQuadIndices"});
    std::memcpy(device_->MapBuffer(indexIB_), indices, sizeof(indices));
}

void LightBatcher::CreatePipelines() {
    rhi::Shader vs = device_->CreateShader(rhi::ShaderStage::Vertex, lemon_spv_light_vert,
                                           lemon_spv_light_vert_count);
    rhi::Shader fs = device_->CreateShader(rhi::ShaderStage::Fragment, lemon_spv_light_frag,
                                           lemon_spv_light_frag_count);
    // 桶序即绘序：Additive → Screen → Subtract（暗斑最后扣）
    const rhi::BlendMode blends[kBlendCount] = {rhi::BlendMode::Additive, rhi::BlendMode::Screen,
                                                rhi::BlendMode::Subtract};
    for (uint32_t i = 0; i < kBlendCount; ++i) {
        pipelines_[i] = device_->CreatePipeline(
            {.vs = vs, .fs = fs, .blend = blends[i], .colorFormat = colorFormat_});
    }
}

void LightBatcher::EnsureCapacity(uint32_t neededInstances) {
    if (neededInstances <= capacity_ && instanceRing_.IsValid()) return;
    device_->WaitIdle();
    if (instanceRing_.IsValid()) device_->DestroyBuffer(instanceRing_);
    while (capacity_ < neededInstances) capacity_ *= 2;
    instanceRing_ = device_->CreateBuffer(
        {.size = (uint64_t)capacity_ * kRingFrames * sizeof(LightInstance),
         .usage = (uint32_t)rhi::BufferUsage::Storage,
         .debugName = "lightInstanceRing"});
    ringMapped_ = (LightInstance*)device_->MapBuffer(instanceRing_);
}

void LightBatcher::Bake(const AtlasRegistry* atlas, std::span<const LightItem> items,
                        std::span<const BlobItem> blobs) {
    EnsureCapacity((uint32_t)(items.size() + blobs.size()));
    LightInstance* seg = ringMapped_ + (uint64_t)ringFrame_ * capacity_;

    // 桶计数 → 前缀偏移 → 单遍落位（稳定：桶内保持提取序）。批③ 两路扩展：
    //   * SpriteMask 灯计数前先解析蒙版（无效 → 跳过 + 一次性 WARN，计数与落位
    //     严格一致——Prepare 已滤一遍，此处是防御层）
    //   * blob 全数计入 Subtract 桶尾部（暗斑最后扣，FillBlobInstance 映射）
    uint32_t counts[kBlendCount] = {0, 0, 0};
    for (const LightItem& it : items) {
        uint32_t b = it.blend;
        if (b >= kBlendCount) {
            if (!blendWarned_) {
                blendWarned_ = true;
                LEMON_WARN("light blend out of range (%u) — clamped to Subtract", b);
            }
            b = kBlendCount - 1;
        }
        if (it.type == 3u /* SpriteMask */) {
            float uv[4];
            uint32_t page = 0;
            if (!ResolveLightMask(atlas, it.maskSprite, uv, page)) {
                if (!maskFailWarned_) {
                    maskFailWarned_ = true;
                    LEMON_WARN("SpriteMask light has invalid maskSprite (%u) — light skipped",
                               it.maskSprite);
                }
                continue;
            }
        }
        ++counts[b];
    }
    counts[kBlendCount - 1] += (uint32_t)blobs.size();
    uint32_t off = 0;
    for (uint32_t b = 0; b < kBlendCount; ++b) {
        buckets_[b].instanceOffset = off;
        buckets_[b].instanceCount = counts[b];
        off += counts[b];
    }
    uint32_t cursor[kBlendCount] = {buckets_[0].instanceOffset, buckets_[1].instanceOffset,
                                    buckets_[2].instanceOffset};
    for (const LightItem& it : items) {
        uint32_t b = it.blend >= kBlendCount ? kBlendCount - 1 : it.blend;
        // 四边形边长 = 2×range（SpritePacket.scale 世界像素边长同约定；light.frag 的
        // 归一化距离 dn = 2|corner| 与此对齐——四边形是 range 圆外接方）
        const float edge = 2.0f * it.range;
        float sinR, cosR;
        math::FastSinCos(it.rot, sinR, cosR);
        // SpriteMask 的蒙版 UV/页槽（批③）：解析失败的灯在计数遍已跳过——落位遍
        // 同判 continue（两遍不同步 = 桶计数与实际落位错位，实例串桶腐败）
        uint32_t maskPage = 0;
        float uv[4] = {0, 0, 0, 0};
        if (it.type == 3u && !ResolveLightMask(atlas, it.maskSprite, uv, maskPage)) continue;
        LightInstance& inst = seg[cursor[b]++];
        inst.a = cosR * edge;
        inst.b = sinR * edge;
        inst.c = -sinR * edge;
        inst.d = cosR * edge;
        inst.e = it.posX;
        inst.f = it.posY;
        inst.colorBits = it.color;
        inst.typeBlend =
            PackLightTypeBlend(it.type, it.blend >= kBlendCount ? kBlendCount - 1 : it.blend,
                               maskPage);
        inst.intensity = it.intensity;
        inst.range = it.range;
        inst.angle = it.angle;
        inst.innerRatio = it.innerRatio;
        inst.maskU0 = uv[0];
        inst.maskV0 = uv[1];
        inst.maskU1 = uv[2];
        inst.maskV1 = uv[3];
    }
    for (const BlobItem& blob : blobs)
        FillBlobInstance(seg[cursor[kBlendCount - 1]++], blob);
    lastInstanceCount_ = off;
}

void LightBatcher::Record(rhi::CommandList& cl, const Mat3x2& viewProj) {
    cl.BindQuadGeometry(cornerVB_, indexIB_);
    cl.BindGlobalDescriptors();
    cl.BindStorageBuffer(instanceRing_, ringSlot_);

    LightPushConstants pc{};
    pc.vpR0[0] = viewProj.m[0];
    pc.vpR0[1] = viewProj.m[1];
    pc.vpR0[2] = viewProj.m[2];
    pc.vpR0[3] = viewProj.m[3];
    pc.vpR1[0] = viewProj.m[4];
    pc.vpR1[1] = viewProj.m[5];
    pc.ringIndex = ringSlot_;
    const uint32_t segBase = ringFrame_ * capacity_;
    for (uint32_t b = 0; b < kBlendCount; ++b) {
        const Bucket& bucket = buckets_[b];
        if (bucket.instanceCount == 0) continue;
        cl.BindPipeline(pipelines_[b]);
        pc.baseInstance = segBase + bucket.instanceOffset;
        cl.PushConstants(&pc, sizeof(pc));
        cl.DrawQuadInstances(bucket.instanceCount, 0);
    }
}

void LightBatcher::AdvanceFrame() { ringFrame_ = (ringFrame_ + 1) % kRingFrames; }

} // namespace lemon::renderer
