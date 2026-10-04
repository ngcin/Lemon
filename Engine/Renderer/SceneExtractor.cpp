// Lemon 引擎 — ECS → RenderableManager 场景提取实现（M7a 批③ 自 Editor
// ViewportRenderer 搬家，逐行同源）
#include "Renderer/SceneExtractor.h"

#include <algorithm>

#include "Components/CoreComponents.h"
#include "Components/RenderComponents.h"
#include "Core/Log.h"
#include "ECS/Hierarchy.h"
#include "ECS/Scene.h"

namespace lemon::renderer {

void SceneExtractor::Extract(ecs::Scene& s, const AtlasRegistry& atlas,
                             RenderableManager& rm) {
    // 内核 #4：场景对象变化（新建/打开/Play 切换 → Scene 指针不同）→ 映射全失效
    const uint64_t stamp = (uint64_t)(uintptr_t)&s;
    if (stamp != lastSceneStamp_) {
        for (SlotMap& sl : ridBySlot_)
            if (sl.rid) rm.Destroy(sl.rid);
        std::fill(ridBySlot_.begin(), ridBySlot_.end(), SlotMap{});
        lastSceneStamp_ = stamp;
    }

    rm.BeginSimTick();
    const uint64_t epoch = ++extractEpoch_;
    // 容量随实体池只增（entt index < 池 capacity；回收槽 tombstone 留池内）
    const size_t wantSlots = s.Pool<entt::entity>().capacity() + 1;
    if (ridBySlot_.size() < wantSlots) ridBySlot_.resize(wantSlots);
    for (auto [ent, tf, sr] : s.View<ecs::Transform2D, ecs::SpriteRenderer>().each()) {
        (void)tf;
        ecs::Entity e = ecs::Scene::FromEntt(ent);
        if (!(sr.flags & ecs::kSrEnabled)) continue;
        // 悬空引用（资产已删/未导入）：不建 renderable（Inspector 槽红显 + 体检红字；
        // GetSprite 越界断言的编辑器侧防线）。空洞号（退役资产）同理不渲染
        if (!atlas.IsValidSprite(sr.spriteId)) continue;

        // 世界变换（内核 #1 消费端；链异常回退本地，保持可渲染）
        ecs::WorldTransform2D wt{};
        if (!ecs::ComputeWorldTransform(s, e, wt)) {
            wt.pos = tf.pos;
            wt.rot = tf.rot;
            wt.scale = tf.scale;
        }
        // 位索引映射（2026-09-26 渲染提取批）：unordered_map find（五万实体 ~1ms/帧）
        // → 直下标 + version 校验。同槽 version 不同 = 回收后的新一代实体：上一代
        // 已死但其 rid 尚未走差集释放，就地回收防泄漏
        const uint32_t idx = ecs::Scene::EnttIndex(e);
        LEMON_ASSERT(idx < ridBySlot_.size(), "slot array must cover entity pool");
        const uint32_t ver = ecs::Scene::EnttVersion(e);
        SlotMap& slot = ridBySlot_[idx];
        const bool freshSlot = slot.rid == 0 || slot.version != ver;
        if (freshSlot) {
            if (slot.rid) rm.Destroy(slot.rid);
            slot.rid = rm.Create({.spriteId = sr.spriteId,
                                  .colorBits = sr.colorRGBA,
                                  .sortingLayer = sr.sortingLayer,
                                  .order = sr.sortOrder,
                                  .blend = (uint8_t)renderer::BlendKind::Alpha,
                                  .filter = (uint8_t)renderer::FilterKind::Linear,
                                  .flags = (uint8_t)(sr.flags & ecs::kSrFlipMask)});
            slot.version = ver;
        }
        slot.lastSeen = epoch;
        // 整包一次寻址推送：逐 setter ×N 实体是大场提取的纯耗成分
        rm.SetAll(slot.rid, sr.spriteId, sr.colorRGBA, sr.sortingLayer, sr.sortOrder,
                  wt.pos, wt.rot, wt.scale);
        // 新建/换代槽首帧基线对齐（复审 2b）：插值开启（alpha<1）时免从 Create 缺省
        // 原点拉丝——spawn 密集场（Spawner/弹幕）每帧都有新槽
        if (freshSlot) rm.SnapPrev(slot.rid);
    }
    // 内核 #2：销毁/禁用差集 → renderable 释放（纪元比对；含空槽全容量顺序扫）
    for (SlotMap& slot : ridBySlot_) {
        if (slot.rid && slot.lastSeen != epoch) {
            rm.Destroy(slot.rid);
            slot = SlotMap{};
        }
    }
}

void RenderExtractSystem::Tick(ecs::World& world, ecs::Scene& scene, float dt) {
    (void)world;
    (void)dt; // 提取不消费时间（插值系数归渲染侧）
    extractor_.Extract(scene, atlas_, rm_);
}

} // namespace lemon::renderer
