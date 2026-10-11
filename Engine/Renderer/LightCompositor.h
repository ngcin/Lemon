// Lemon 引擎 — 光贴图三段式编排器（M8 批②，ADR-019 M1；编辑器 GameView 与
// lemon-game（批③ 已装配）共用，防第三套实现——GameFx.h 同款纪律）。
// 职责（纯编排，不持有 RT——RT 生命周期归装配方）：
//   * Prepare：Global 去重（首个生效 + 一次性 WARN——批① 头注承诺的渲染侧语义）、
//     SpriteMask 蒙版有效性把关（批③：无效 maskSprite → dropped + 一次性 WARN，
//     有效则放行——Bake 填 UV/页槽）、光数/blob 数独立截断（质量档 budget + 各自
//     一次性 WARN）、ambient 底色计算（Global color×intensity / 无 Global 白）
//   * RenderLightPass：lightRT 动态渲染块（CLEAR = ambient）内录光源四边形 + blob
//     椭圆暗斑（批③）
//   * RenderComposite：gameRT 块内录全屏乘法四边形（sceneRT × lightRT，gamma 直乘）
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
        uint32_t drawLights = 0;                     // 参与绘制实例数（滤 Global、截断后）
        uint32_t droppedLights = 0;                  // 超预算/蒙版无效截除数
        uint32_t drawBlobs = 0;                      // 批③：椭圆暗斑实例数（截断后）
        uint32_t droppedBlobs = 0;                   // 批③：超预算截除数
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
    /// 消费 FilteredItems()/FilteredBlobs()）。atlas = 蒙版 spriteId 校验源
    /// （nullptr = 所有 SpriteMask 灯判无效）
    const Plan& Prepare(std::span<const LightItem> items, std::span<const BlobItem> blobs,
                        uint32_t lightBudget, const AtlasRegistry* atlas);

    /// 光照段整块：BeginOffscreenPass(lightRT, ambient) → 光源/blob draw → EndPass
    ///（atlas = Bake 的蒙版解析源，与 Prepare 同一图集）
    void RenderLightPass(rhi::CommandList& cl, rhi::Texture lightRT, const AtlasRegistry* atlas,
                         const Mat3x2& viewProj, uint32_t rtW, uint32_t rtH);

    /// 合成段：当前已 Begin 的 gameRT 块内录全屏乘法（调用方负责块与后续豁免段）
    void RenderComposite(rhi::CommandList& cl);

    void AdvanceFrame() { batcher_.AdvanceFrame(); }
    LightBatcher& Batcher() { return batcher_; }
    const Plan& LastPlan() const { return plan_; }
    const std::vector<LightItem>& FilteredItems() const { return filtered_; }
    const std::vector<BlobItem>& FilteredBlobs() const { return blobs_; }

private:
    void CreateCompositePipeline();

    rhi::Device* device_ = nullptr;
    rhi::Device::RecreateCallbackId recreateCbId_ = 0;
    LightBatcher batcher_;
    rhi::Pipeline compositePipeline_{};
    rhi::Format compositeTargetFormat_ = rhi::Format::RGBA8Unorm;
    uint32_t sceneTexSlot_ = 0, lightTexSlot_ = 0, samplerSlot_ = 0;

    std::vector<LightItem> filtered_; // Prepare 复用（零分配）
    std::vector<BlobItem> blobs_;     // 批③：blob 截断产物（RenderLightPass 消费）
    Plan plan_{};
    bool globalDupWarned_ = false;    // 四类一次性 WARN 状态
    bool maskInvalidWarned_ = false;  // 批③改义：蒙版无效（原批② = 无条件滤除提示）
    bool lightTruncateWarned_ = false;
    bool blobTruncateWarned_ = false;
};

} // namespace lemon::renderer
