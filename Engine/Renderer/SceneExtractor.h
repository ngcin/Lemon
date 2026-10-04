// Lemon 引擎 — ECS → RenderableManager 场景提取（M7a 批③；2026-09-26 渲染提取
// 批随迁）自 Editor ViewportRenderer::ExtractScene 下沉（搬家非复制，去
// EditorContext& 锚定改世界参数）：视口剔除前的全量提取——世界变换合成、
// 悬空引用跳过、位索引槽位映射（version 防回收串槽）、纪元差集释放。
// EditorApp（ViewportRenderer，每渲染帧直调——插值 simAlpha 契约）与 lemon-game
//（批④，经 RenderExtractSystem 挂 SystemStage::Extract）两薄壳消费——现状盘点
// #12 / review 2026-10-02 #6，防第三套实现。
#pragma once

#include <cstdint>
#include <vector>

#include "ECS/SystemPipeline.h"
#include "Renderer/Atlas.h"
#include "Renderer/Renderable.h"

namespace lemon::ecs {
class Scene;
}

namespace lemon::renderer {

/// 提取器（消费方各持一份——槽位映射/纪元态属消费方视口会话）
class SceneExtractor {
public:
    /// 单次提取：View<Transform2D, SpriteRenderer> → rm（建槽/整包寻址/SnapPrev）
    /// + 销毁/禁用差集释放。atlas = 精灵有效性判源（悬空引用不建 renderable）。
    void Extract(ecs::Scene& s, const AtlasRegistry& atlas, RenderableManager& rm);

private:
    // Entity → renderable 映射（2026-09-26 渲染提取批：unordered_map → 位索引
    // 数组。原 map find 是五万场提取的单项大头；数组 = Scene::EnttIndex 直下标 +
    // EnttVersion 防回收串槽。容量随实体池只增；稳态零分配）
    struct SlotMap {
        uint32_t rid = 0;     // 0 = 空
        uint32_t version = 0; // 写入时实体 version（校验防串）
        uint64_t lastSeen = 0;
    };
    std::vector<SlotMap> ridBySlot_;
    uint64_t lastSceneStamp_ = 0;
    uint64_t extractEpoch_ = 0; // 提取调用计数（差集判定：lastSeen != 当前纪元 → 释放）
};

/// SystemStage::Extract 首个真实现（M7a 批③）：管线驱动的提取（消费方装配期
/// AddSystem + ResolveOrder——批④ lemon-game；编辑器视口维持每渲染帧直调，不装
/// 本系统＝插值时序契约保留）。引用 AtlasRegistry/RenderableManager 由消费方
/// 注入（装配期两者必须已就位且地址稳定）。
class RenderExtractSystem final : public ecs::ISystem {
public:
    RenderExtractSystem(const AtlasRegistry& atlas, RenderableManager& rm)
        : atlas_(atlas), rm_(rm) {}

    const char* Name() const override { return "RenderExtract"; }
    ecs::SystemStage Stage() const override { return ecs::SystemStage::Extract; }
    void Tick(ecs::World& world, ecs::Scene& scene, float dt) override;

private:
    SceneExtractor extractor_;
    const AtlasRegistry& atlas_;
    RenderableManager& rm_;
};

} // namespace lemon::renderer
