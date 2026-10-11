#include "Renderer/LightCompositor.h"

#include "Core/Log.h"
#include "Renderer/EmbeddedShaders.h"

namespace lemon::renderer {

namespace {
// ColorHex（r|g<<8|b<<16|a<<24）→ 线性 [0,1] RGBA
void UnpackColorHex(uint32_t c, float out[4]) {
    out[0] = (float)((c >> 0) & 0xFF) / 255.0f;
    out[1] = (float)((c >> 8) & 0xFF) / 255.0f;
    out[2] = (float)((c >> 16) & 0xFF) / 255.0f;
    out[3] = (float)((c >> 24) & 0xFF) / 255.0f;
}
} // namespace

void LightCompositor::Init(rhi::Device& device, uint32_t ringSlot, uint32_t sceneTexSlot,
                           uint32_t lightTexSlot, uint32_t samplerSlot,
                           rhi::Format lightRtFormat, rhi::Format compositeTargetFormat) {
    if (recreateCbId_ != 0) device.RemoveRecreateCallback(recreateCbId_);
    device_ = &device;
    sceneTexSlot_ = sceneTexSlot;
    lightTexSlot_ = lightTexSlot;
    samplerSlot_ = samplerSlot;
    compositeTargetFormat_ = compositeTargetFormat;
    batcher_.Init(device, lightRtFormat, ringSlot);
    CreateCompositePipeline();

    recreateCbId_ = device.AddRecreateCallback("LightCompositor", [this](rhi::Device& d) {
        device_ = &d;
        compositePipeline_ = {};
        CreateCompositePipeline(); // 光源管线由 batcher_ 自登记回调重建
    });
}

void LightCompositor::CreateCompositePipeline() {
    rhi::Shader vs = device_->CreateShader(rhi::ShaderStage::Vertex, lemon_spv_composite_vert,
                                           lemon_spv_composite_vert_count);
    rhi::Shader fs = device_->CreateShader(rhi::ShaderStage::Fragment,
                                           lemon_spv_composite_frag, lemon_spv_composite_frag_count);
    compositePipeline_ = device_->CreatePipeline(
        {.vs = vs, .fs = fs, .blend = rhi::BlendMode::Opaque, // 全屏不透明覆盖
         .colorFormat = compositeTargetFormat_});
}

const LightCompositor::Plan& LightCompositor::Prepare(std::span<const LightItem> items,
                                                      uint32_t lightBudget) {
    plan_ = Plan{};
    filtered_.clear();
    bool globalTaken = false;
    for (const LightItem& it : items) {
        if (it.type == 0u /* Global */) {
            // ADR-019 M1：Global 至多一个生效，重复取首个 + WARN（昼夜脚本误配双
            // Global 时首灯胜出——幂等可预期）
            if (globalTaken) {
                if (!globalDupWarned_) {
                    globalDupWarned_ = true;
                    LEMON_WARN("multiple Global lights — first one wins, rest ignored");
                }
                continue;
            }
            globalTaken = true;
            float col[4];
            UnpackColorHex(it.color, col);
            for (int i = 0; i < 3; ++i)
                plan_.ambient[i] = col[i] * it.intensity > 1.0f
                                       ? 1.0f
                                       : col[i] * it.intensity; // LDR 钳 [0,1]
            continue; // Global 无几何（ambient 即底色）
        }
        if (it.type == 3u /* SpriteMask */) {
            if (!maskSkipWarned_) {
                maskSkipWarned_ = true;
                LEMON_WARN("SpriteMask light rendering lands in M8 b3 — skipped for now");
            }
            continue;
        }
        if (plan_.drawLights >= lightBudget) {
            ++plan_.droppedLights;
            if (!truncateWarned_) {
                truncateWarned_ = true;
                LEMON_WARN("light budget exceeded (%u) — extras silently truncated",
                           lightBudget);
            }
            continue;
        }
        ++plan_.drawLights;
        filtered_.push_back(it);
    }
    return plan_;
}

void LightCompositor::RenderLightPass(rhi::CommandList& cl, rhi::Texture lightRT,
                                      const Mat3x2& viewProj, uint32_t rtW, uint32_t rtH) {
    batcher_.Bake(filtered_); // Prepare 的过滤产物（纯 CPU 决策与 GPU 烘焙分离）
    cl.BeginOffscreenPass(lightRT, plan_.ambient); // loadOp 恒 CLEAR = ambient 底色
    cl.SetViewportScissor(rtW, rtH);
    batcher_.Record(cl, viewProj);
    cl.EndPass();
}

void LightCompositor::RenderComposite(rhi::CommandList& cl) {
    cl.BindQuadGeometry(batcher_.CornerVB(), batcher_.IndexIB());
    cl.BindGlobalDescriptors();
    // 环绑定确定性（composite 不读实例——共享布局下绑定态显式化，免依赖前序 pass 残留）
    cl.BindStorageBuffer(batcher_.RingBuffer(), batcher_.RingSlot());
    CompositePushConstants pc{sceneTexSlot_, lightTexSlot_, samplerSlot_, 0u};
    cl.BindPipeline(compositePipeline_);
    cl.PushConstants(&pc, sizeof(pc));
    cl.DrawQuadInstances(1, 0);
}

} // namespace lemon::renderer
