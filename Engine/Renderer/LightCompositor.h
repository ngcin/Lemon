// Lemon 引擎 — 光贴图三段式编排器（M8 批②，ADR-019 M1；编辑器 GameView 与
// lemon-game（批③ 装配）共用，防第三套实现——GameFx.h 同款纪律）。
// 职责（纯编排，不持有 RT——RT 生命周期归装配方）：
//   * Prepare：Global 去重（首个生效 + 一次性 WARN——批① 头注承诺的渲染侧语义）、
//     SpriteMask 滤除（渲染归批③，一次性 WARN 提示当前跳过）、光数截断（质量档
//     budget + 一次性 WARN）、ambient 底色计算（Global color×intensity / 无 Global 白）
//   * RenderLightPass：lightRT 动态渲染块（CLEAR = ambient）内录光源四边形
//   * RenderComposite：gameRT 块内录全屏乘法四边形（sceneRT × lightRT，gamma 直乘）
// blob 阴影的实例映射归批③（M8.md ③ 行）；本批跳过条件只看 Light2D。
#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "Core/Math.h"
#include "Renderer/LightBatcher.h"
#include "Renderer/LightTypes.h"
#include "Renderer/RHI.h"

namespace lemon::renderer {

class LightCompositor {
public:
    struct Plan {
        float ambient[4] = {1.0f, 1.0f, 1.0f, 1.0f}; // lightRT CLEAR 底色
        uint32_t drawLights = 0;                     // 参与绘制实例数（滤 Global/Mask、截断后）
        uint32_t droppedLights = 0;                  // 超预算截断数
    };

    /// preheat：光源 3 管线 + composite 管线。sceneTex/lightTex = bindless 槽
    /// （建 RT 时绑一次，M4.7-P0）；lightRtFormat = lightRT 格式（拍板 RGBA8Unorm）；
    /// compositeTargetFormat = 合成目标格式（编辑器 gameRT RGBA8Unorm；lemon-game
    /// 批③ 传交换链格式）。
    void Init(rhi::Device& device, uint32_t ringSlot, uint32_t sceneTexSlot,
              uint32_t lightTexSlot, uint32_t samplerSlot, rhi::Format lightRtFormat,
              rhi::Format compositeTargetFormat);

    ~LightCompositor() {
        if (device_) device_->RemoveRecreateCallback(recreateCbId_);
    }

    /// CPU 编排决策 + 过滤（纯 CPU——单测可直调；实例烘焙在 RenderLightPass 内
    /// 消费 FilteredItems()）
    const Plan& Prepare(std::span<const LightItem> items, uint32_t lightBudget);

    /// 光照段整块：BeginOffscreenPass(lightRT, ambient) → 光源 draw → EndPass
    void RenderLightPass(rhi::CommandList& cl, rhi::Texture lightRT, const Mat3x2& viewProj,
                         uint32_t rtW, uint32_t rtH);

    /// 合成段：当前已 Begin 的 gameRT 块内录全屏乘法（调用方负责块与后续豁免段）
    void RenderComposite(rhi::CommandList& cl);

    void AdvanceFrame() { batcher_.AdvanceFrame(); }
    LightBatcher& Batcher() { return batcher_; }
    const Plan& LastPlan() const { return plan_; }
    const std::vector<LightItem>& FilteredItems() const { return filtered_; }

private:
    void CreateCompositePipeline();

    rhi::Device* device_ = nullptr;
    rhi::Device::RecreateCallbackId recreateCbId_ = 0;
    LightBatcher batcher_;
    rhi::Pipeline compositePipeline_{};
    rhi::Format compositeTargetFormat_ = rhi::Format::RGBA8Unorm;
    uint32_t sceneTexSlot_ = 0, lightTexSlot_ = 0, samplerSlot_ = 0;

    std::vector<LightItem> filtered_; // Prepare 复用（零分配）
    Plan plan_{};
    bool globalDupWarned_ = false; // 三类一次性 WARN 状态
    bool maskSkipWarned_ = false;
    bool truncateWarned_ = false;
};

} // namespace lemon::renderer
