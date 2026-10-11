#include "Renderer/PostFx.h"

#include <cstring>

#include "Core/Log.h"
#include "Renderer/Atlas.h" // FX 段 bindless 槽常量本位（#L20 记账锚）
#include "Renderer/EmbeddedShaders.h"

namespace lemon::renderer {

namespace {
// push 常量块（与对应 frag 声明逐字段一致；composite 同款"布局一次定死"纪律）
struct FxExtractPush {
    uint32_t sceneTexIndex;
    uint32_t samplerIndex;
    float threshold;
    uint32_t _pad;
};
static_assert(sizeof(FxExtractPush) == 16);

struct FxFilterPush { // bloom down/up 共用布局（双滤波两 shader 同字段）
    uint32_t srcTexIndex;
    uint32_t samplerIndex;
    float srcTexelX;
    float srcTexelY;
};
static_assert(sizeof(FxFilterPush) == 16);

struct FxFinalPush {
    uint32_t sceneTexIndex;
    uint32_t bloomTexIndex;
    uint32_t samplerIndex;
    uint32_t waveCount;
    float waves[PostFx::kMaxWaves][4]; // xy=中心 z=半径 w=幅度（UV 偏移）
    float params[4];                   // x=bloomIntensity y=toneContrast z=toneWarmth w=aspect
};
static_assert(sizeof(FxFinalPush) == 96); // < kMaxPushConstants(128)
} // namespace

void PostFx::Init(rhi::Device& device, uint32_t samplerSlot, rhi::Format finalTargetFormat) {
    // 幂等（LightBatcher 评审 D3 同款）：重复 Init 先摘旧 token 防回调表累积 UAF
    if (recreateCbId_ != 0) device.RemoveRecreateCallback(recreateCbId_);
    device_ = &device;
    samplerSlot_ = samplerSlot;
    finalTargetFormat_ = finalTargetFormat;
    CreateGeometry();
    CreatePipelines();

    recreateCbId_ = device.AddRecreateCallback("PostFx", [this](rhi::Device& d) {
        device_ = &d;
        cornerVB_ = {};
        indexIB_ = {};
        extractPipe_ = downPipe_ = upPipe_ = finalPipe_ = {};
        sceneRt_.tex = {}; // RT 句柄随设备亡；重建闸 = tex.IsValid()（懒建口径：
        bloomA_.tex = bloomB_.tex = bloomC_.tex = {}; // 下次开链帧 EnsureTargets 重建重绑）
        CreateGeometry();
        CreatePipelines();
    });
}

void PostFx::CreateGeometry() {
    cornerVB_ = device_->CreateBuffer({.size = sizeof(float) * 2 * 4,
                                       .usage = (uint32_t)rhi::BufferUsage::Vertex,
                                       .debugName = "fxQuadCorners"});
    const float corners[4][2] = {{-0.5f, -0.5f}, {0.5f, -0.5f}, {0.5f, 0.5f}, {-0.5f, 0.5f}};
    std::memcpy(device_->MapBuffer(cornerVB_), corners, sizeof(corners));
    const uint16_t indices[6] = {0, 1, 2, 0, 2, 3};
    indexIB_ = device_->CreateBuffer({.size = sizeof(indices),
                                      .usage = (uint32_t)rhi::BufferUsage::Index,
                                      .debugName = "fxQuadIndices"});
    std::memcpy(device_->MapBuffer(indexIB_), indices, sizeof(indices));
}

void PostFx::CreatePipelines() {
    const rhi::Format bloomFmt = rhi::Format::RGBA8Unorm; // 链内 RT 拍板（02 §8 LDR）
    {
        rhi::Shader vs = device_->CreateShader(rhi::ShaderStage::Vertex,
                                               lemon_spv_composite_vert,
                                               lemon_spv_composite_vert_count);
        rhi::Shader fs = device_->CreateShader(rhi::ShaderStage::Fragment,
                                               lemon_spv_bloom_extract_frag,
                                               lemon_spv_bloom_extract_frag_count);
        extractPipe_ = device_->CreatePipeline(
            {.vs = vs, .fs = fs, .blend = rhi::BlendMode::Opaque, .colorFormat = bloomFmt});
    }
    {
        rhi::Shader vs = device_->CreateShader(rhi::ShaderStage::Vertex,
                                               lemon_spv_composite_vert,
                                               lemon_spv_composite_vert_count);
        rhi::Shader fs = device_->CreateShader(rhi::ShaderStage::Fragment,
                                               lemon_spv_bloom_down_frag,
                                               lemon_spv_bloom_down_frag_count);
        downPipe_ = device_->CreatePipeline(
            {.vs = vs, .fs = fs, .blend = rhi::BlendMode::Opaque, .colorFormat = bloomFmt});
    }
    {
        rhi::Shader vs = device_->CreateShader(rhi::ShaderStage::Vertex,
                                               lemon_spv_composite_vert,
                                               lemon_spv_composite_vert_count);
        rhi::Shader fs = device_->CreateShader(rhi::ShaderStage::Fragment,
                                               lemon_spv_bloom_up_frag,
                                               lemon_spv_bloom_up_frag_count);
        upPipe_ = device_->CreatePipeline(
            {.vs = vs, .fs = fs, .blend = rhi::BlendMode::Opaque, .colorFormat = bloomFmt});
    }
    {
        rhi::Shader vs = device_->CreateShader(rhi::ShaderStage::Vertex,
                                               lemon_spv_composite_vert,
                                               lemon_spv_composite_vert_count);
        rhi::Shader fs = device_->CreateShader(rhi::ShaderStage::Fragment,
                                               lemon_spv_fx_final_frag,
                                               lemon_spv_fx_final_frag_count);
        finalPipe_ = device_->CreatePipeline(
            {.vs = vs, .fs = fs, .blend = rhi::BlendMode::Opaque,
             .colorFormat = finalTargetFormat_});
    }
}

void PostFx::SpawnShockwave(Vec2 uvCenter, float maxRadius, float strength, float duration) {
    if (duration <= 0.0f || maxRadius <= 0.0f || strength <= 0.0f) return;
    uint32_t slot;
    if (waveCount_ == kMaxWaves) {
        if (!waveFullWarned_) {
            waveFullWarned_ = true;
            LEMON_WARN("shockwave pool full (%u) — oldest replaced", kMaxWaves);
        }
        // 前移保持 FIFO 序（顶替 = 队首出队）
        for (uint32_t i = 1; i < kMaxWaves; ++i) waves_[i - 1] = waves_[i];
        slot = kMaxWaves - 1;
    } else {
        slot = waveCount_;
        ++waveCount_;
    }
    waves_[slot] = Wave{uvCenter, /*radius=*/0.0f, maxRadius,
                        /*speed=*/maxRadius / duration, /*amp=*/strength, strength};
}

void PostFx::Simulate(float dt) {
    for (uint32_t i = 0; i < waveCount_;) {
        Wave& w = waves_[i];
        w.radius += w.speed * dt;
        const float remain = 1.0f - (w.maxRadius > 0.0f ? w.radius / w.maxRadius : 1.0f);
        w.amp = remain > 0.0f ? w.strength * remain : 0.0f; // 剩余程线性衰减
        if (w.radius >= w.maxRadius || w.amp <= 0.0f) {
            // 寿终回收：尾段前移保持 FIFO 序
            for (uint32_t j = i + 1; j < waveCount_; ++j) waves_[j - 1] = waves_[j];
            --waveCount_;
        } else {
            ++i;
        }
    }
}

void PostFx::EnsureRt(Rt& rt, uint32_t w, uint32_t h, const char* debugName, uint32_t slot) {
    if (rt.tex.IsValid() && rt.w == w && rt.h == h) return;
    if (rt.tex.IsValid()) {
        device_->WaitIdle(); // 低频重建停稳语义（EnsureLightTargets 同款）
        device_->DestroyTexture(rt.tex);
    }
    rt.w = w;
    rt.h = h;
    rt.tex = device_->CreateTexture(
        {.width = w, .height = h, .renderTarget = true, .debugName = debugName});
    device_->BindTextureToSlot(rt.tex, slot); // 建时绑一次（M4.7-P0）
}

void PostFx::EnsureTargets(uint32_t w, uint32_t h) {
    EnsureRt(sceneRt_, w, h, "fxSceneRT", kFxSceneTexSlot);
    EnsureRt(bloomA_, BloomMipSize(w, 4), BloomMipSize(h, 4), "fxBloomA", kFxBloomATexSlot);
    EnsureRt(bloomB_, BloomMipSize(w, 8), BloomMipSize(h, 8), "fxBloomB", kFxBloomBTexSlot);
    EnsureRt(bloomC_, BloomMipSize(w, 16), BloomMipSize(h, 16), "fxBloomC", kFxBloomCTexSlot);
}

void PostFx::RenderChain(rhi::CommandList& cl) {
    last_ = FrameInfo{};
    const float black[4] = {0.0f, 0.0f, 0.0f, 1.0f}; // 全屏不透明四边形覆盖，CLEAR 无关
    auto pass = [&](Rt& dst, rhi::Pipeline pipe, const void* pc, uint32_t pcSize) {
        cl.BeginOffscreenPass(dst.tex, black);
        cl.SetViewportScissor(dst.w, dst.h);
        cl.BindQuadGeometry(cornerVB_, indexIB_);
        cl.BindGlobalDescriptors();
        cl.BindPipeline(pipe);
        cl.PushConstants(pc, pcSize);
        cl.DrawQuadInstances(1, 0);
        cl.EndPass();
        ++last_.chainPasses;
    };
    // extract：fxScene(全) → bloomA(1/4)——阈值提取 + 线性采样降采样
    {
        FxExtractPush pc{kFxSceneTexSlot, samplerSlot_, params_.bloomThreshold, 0u};
        pass(bloomA_, extractPipe_, &pc, sizeof(pc));
    }
    // down：A(1/4)→B(1/8)→C(1/16)
    {
        FxFilterPush pc{kFxBloomATexSlot, samplerSlot_, 1.0f / (float)bloomA_.w,
                        1.0f / (float)bloomA_.h};
        pass(bloomB_, downPipe_, &pc, sizeof(pc));
    }
    {
        FxFilterPush pc{kFxBloomBTexSlot, samplerSlot_, 1.0f / (float)bloomB_.w,
                        1.0f / (float)bloomB_.h};
        pass(bloomC_, downPipe_, &pc, sizeof(pc));
    }
    // up：C(1/16)→B(1/8)→A(1/4)——读写不同 RT 无 hazard（覆盖 down 段残留）
    {
        FxFilterPush pc{kFxBloomCTexSlot, samplerSlot_, 1.0f / (float)bloomC_.w,
                        1.0f / (float)bloomC_.h};
        pass(bloomB_, upPipe_, &pc, sizeof(pc));
    }
    {
        FxFilterPush pc{kFxBloomBTexSlot, samplerSlot_, 1.0f / (float)bloomB_.w,
                        1.0f / (float)bloomB_.h};
        pass(bloomA_, upPipe_, &pc, sizeof(pc));
    }
}

void PostFx::RenderFinal(rhi::CommandList& cl) {
    FxFinalPush pc{};
    pc.sceneTexIndex = kFxSceneTexSlot;
    pc.bloomTexIndex = kFxBloomATexSlot;
    pc.samplerIndex = samplerSlot_;
    pc.waveCount = waveCount_;
    for (uint32_t i = 0; i < waveCount_; ++i) {
        pc.waves[i][0] = waves_[i].center.x;
        pc.waves[i][1] = waves_[i].center.y;
        pc.waves[i][2] = waves_[i].radius;
        pc.waves[i][3] = waves_[i].amp;
    }
    pc.params[0] = params_.bloomIntensity;
    pc.params[1] = params_.toneContrast;
    pc.params[2] = params_.toneWarmth;
    pc.params[3] = sceneRt_.h > 0 ? (float)sceneRt_.w / (float)sceneRt_.h : 1.0f;
    last_.waveCount = waveCount_;

    cl.BindQuadGeometry(cornerVB_, indexIB_);
    cl.BindGlobalDescriptors();
    cl.BindPipeline(finalPipe_);
    cl.PushConstants(&pc, sizeof(pc));
    cl.DrawQuadInstances(1, 0);
}

} // namespace lemon::renderer
