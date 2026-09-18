#include "Renderer/SpriteBatcher.h"

#include <chrono>
#include <cstring>

#include "Core/Log.h"
#include "Renderer/EmbeddedShaders.h"

namespace lemon::renderer {

void SpriteBatcher::Init(rhi::Device& device, uint32_t samplerLinearSlot,
                         uint32_t samplerPointSlot) {
    device_ = &device;
    samplerSlots_[0] = samplerLinearSlot;
    samplerSlots_[1] = samplerPointSlot;
    CreateGeometry();
    EnsureCapacity(kInitialCapacity);
    CreatePipelines();

    // 设备丢失重建：句柄已随设备销毁全部作废（必须先清，否则 EnsureCapacity
    // 会因旧句柄"看似有效"跳过重建 → 无效 buffer 写描述符，验证层实测抓过）
    device.AddRecreateCallback("SpriteBatcher", [this](rhi::Device& d) {
        device_ = &d;
        cornerVB_ = {};
        indexIB_ = {};
        instanceRing_ = {};
        ringMapped_ = nullptr;
        for (auto& p : pipelines_) p = {}; // capacity_ 保留，按原容量重建
        CreateGeometry();
        EnsureCapacity(kInitialCapacity);
        CreatePipelines();
    });
}

void SpriteBatcher::CreateGeometry() {
    cornerVB_ = device_->CreateBuffer({.size = sizeof(float) * 2 * 4,
                                       .usage = (uint32_t)rhi::BufferUsage::Vertex,
                                       .debugName = "quadCorners"});
    const float corners[4][2] = {{-0.5f, -0.5f}, {0.5f, -0.5f}, {0.5f, 0.5f}, {-0.5f, 0.5f}};
    std::memcpy(device_->MapBuffer(cornerVB_), corners, sizeof(corners));
    const uint16_t indices[6] = {0, 1, 2, 0, 2, 3};
    indexIB_ = device_->CreateBuffer({.size = sizeof(indices),
                                      .usage = (uint32_t)rhi::BufferUsage::Index,
                                      .debugName = "quadIndices"});
    std::memcpy(device_->MapBuffer(indexIB_), indices, sizeof(indices));
}

void SpriteBatcher::CreatePipelines() {
    rhi::Shader vs = device_->CreateShader(rhi::ShaderStage::Vertex, lemon_spv_sprite_vert,
                                      lemon_spv_sprite_vert_count);
    rhi::Shader fs = device_->CreateShader(rhi::ShaderStage::Fragment, lemon_spv_sprite_frag,
                                      lemon_spv_sprite_frag_count);
    const rhi::BlendMode blends[4] = {rhi::BlendMode::Opaque, rhi::BlendMode::Alpha,
                                      rhi::BlendMode::Additive, rhi::BlendMode::Multiply};
    for (int i = 0; i < 4; ++i) {
        pipelines_[i] = device_->CreatePipeline(
            {.vs = vs, .fs = fs, .blend = blends[i], .colorFormat = device_->SwapchainFormat()});
    }
}

void SpriteBatcher::EnsureCapacity(uint32_t neededInstances) {
    if (neededInstances <= capacity_ && instanceRing_.IsValid()) return;
    // 扩容/重建必须在无在途帧时进行（描述符槽也要重绑）
    device_->WaitIdle();
    if (instanceRing_.IsValid()) device_->DestroyBuffer(instanceRing_);
    while (capacity_ < neededInstances) capacity_ *= 2;
    instanceRing_ = device_->CreateBuffer(
        {.size = (uint64_t)capacity_ * kRingFrames * sizeof(SpriteInstance),
         .usage = (uint32_t)rhi::BufferUsage::Storage,
         .debugName = "spriteInstanceRing"});
    ringMapped_ = (SpriteInstance*)device_->MapBuffer(instanceRing_);
    LEMON_LOG("instance ring: %u instances × %u segments (%.1f MB)", capacity_, kRingFrames,
              capacity_ * kRingFrames * sizeof(SpriteInstance) / (1024.0 * 1024.0));
}

void SpriteBatcher::Bake(const AtlasRegistry& atlas, std::span<const SpritePacket> packets,
                         std::span<const SpritePacket> particlePackets,
                         std::span<const SpritePacket> textPackets) {
    auto t0 = std::chrono::steady_clock::now();
    EnsureCapacity((uint32_t)(packets.size() + particlePackets.size() + textPackets.size()));

    SpriteInstance* seg = ringMapped_ + (uint64_t)ringFrame_ * capacity_;
    batches_.clear();
    uint32_t written = 0;

    auto bakeSpan = [&](std::span<const SpritePacket> ps) {
        uint32_t i = 0;
        while (i < ps.size()) {
            // 连续同键 → 一批
            const SpritePacket& head = ps[i];
            uint32_t start = i;
            while (i < ps.size() && ps[i].key.SameBatch(head.key)) ++i;
            uint32_t count = i - start;

            Batch b;
            b.key = head.key;
            b.instanceOffset = written;
            b.instanceCount = count;
            batches_.push_back(b);

            for (uint32_t j = start; j < i; ++j) {
                const SpritePacket& p = ps[j];
                const SpriteInfo& spr = atlas.GetSprite(p.spriteId);
                SpriteInstance& inst = seg[written++];
                FillInstanceAffine(inst, p.posX, p.posY, p.rot, p.scaleX, p.scaleY);
                inst.u0 = spr.u0;
                inst.v0 = spr.v0;
                inst.u1 = spr.u1;
                inst.v1 = spr.v1;
                inst.colorBits = p.colorBits;
                inst.flags = p.flags;
            }
        }
    };
    bakeSpan(packets);
    bakeSpan(particlePackets);
    bakeSpan(textPackets);
    lastInstanceCount_ = written;

    auto t1 = std::chrono::steady_clock::now();
    stats_.bakeMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
}

void SpriteBatcher::Record(rhi::CommandList& cl, const Mat3x2& viewProj) {
    auto t0 = std::chrono::steady_clock::now();

    cl.BindQuadGeometry(cornerVB_, indexIB_);
    cl.BindGlobalDescriptors();
    cl.BindStorageBuffer(instanceRing_);

    SpritePushConstants pc{};
    pc.vpR0[0] = viewProj.m[0];
    pc.vpR0[1] = viewProj.m[1];
    pc.vpR0[2] = viewProj.m[2];
    pc.vpR0[3] = viewProj.m[3];
    pc.vpR1[0] = viewProj.m[4];
    pc.vpR1[1] = viewProj.m[5];
    // 段基 = ringFrame_ * capacity_；每批再加批内偏移
    const uint32_t segBase = ringFrame_ * capacity_;
    for (const Batch& b : batches_) {
        cl.BindPipeline(pipelines_[b.key.blend]);
        pc.baseInstance = segBase + b.instanceOffset;
        pc.atlasIndex = (uint32_t)b.key.textureAtlas;
        pc.samplerIndex = samplerSlots_[b.key.filter];
        cl.PushConstants(&pc, sizeof(pc));
        cl.DrawQuadInstances(b.instanceCount, 0);
    }

    auto t1 = std::chrono::steady_clock::now();
    stats_.recordMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
}

void SpriteBatcher::AdvanceFrame() {
    ringFrame_ = (ringFrame_ + 1) % kRingFrames;
}

} // namespace lemon::renderer
