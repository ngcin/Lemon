// Lemon 引擎 — SceneMembership 原语实现（ADR-017 D1；M7c 批⑥）
#include "ECS/SceneMembership.h"

#include <vector>

#include "Components/CoreComponents.h"
#include "ECS/Hierarchy.h"

namespace lemon::ecs {

uint32_t StampSceneMembership(Scene& s, uint32_t sceneHandle) {
    uint32_t n = 0;
    s.Each([&](Entity e) {
        if (SceneMembership* m = s.TryGet<SceneMembership>(e)) {
            if (m->scene != kSceneHandleUnassigned) return; // 已指派不动（增量装载）
            m->scene = sceneHandle;
        } else {
            s.Emplace<SceneMembership>(e).scene = sceneHandle;
        }
        ++n;
    });
    return n;
}

uint32_t QueueDestroySceneGroup(Scene& s, uint32_t sceneHandle) {
    uint32_t n = 0;
    s.Each([&](Entity e) {
        const SceneMembership* m = s.TryGet<SceneMembership>(e);
        if (!m || m->scene != sceneHandle) return;
        if (m->flags & kSceneFlagDontDestroyOnLoad) return;
        s.Destroy(e); // 两阶段：入队 + DestroyQueueTag（Scene::Destroy 内打标）
        ++n;
    });
    return n;
}

namespace {

/// 子树收集（父先于子；SceneArchive::CollectSubtree 同款链遍历 + 深度护栏防脏环）
void CollectTree(Scene& s, Entity e, std::vector<Entity>& out, int depth = 0) {
    if (depth > (int)kMaxHierarchyDepth + 1) return;
    out.push_back(e);
    const Hierarchy* h = s.TryGet<Hierarchy>(e);
    if (!h) return;
    for (Entity c = h->firstChild; !c.IsNull() && s.Alive(c);) {
        const Hierarchy* ch = s.TryGet<Hierarchy>(c);
        Entity next = ch ? ch->next : Entity::Null();
        CollectTree(s, c, out, depth + 1);
        c = next;
    }
}

} // namespace

uint32_t MarkDontDestroyOnLoadTree(Scene& s, Entity root) {
    if (!s.Alive(root)) return 0;
    std::vector<Entity> tree;
    CollectTree(s, root, tree);
    for (Entity e : tree) {
        SceneMembership* m = s.TryGet<SceneMembership>(e);
        if (!m) m = &s.Emplace<SceneMembership>(e); // 未打标实体（如编辑态根）补标
        m->flags |= kSceneFlagDontDestroyOnLoad;
    }
    return (uint32_t)tree.size();
}

uint32_t CountSceneGroup(Scene& s, uint32_t sceneHandle) {
    uint32_t n = 0;
    s.Each([&](Entity e) {
        const SceneMembership* m = s.TryGet<SceneMembership>(e);
        const uint32_t scene = m ? m->scene : kSceneHandleUnassigned; // 无组件 = 未指派组
        if (scene == sceneHandle) ++n;
    });
    return n;
}

uint32_t CountDontDestroyOnLoad(Scene& s) {
    uint32_t n = 0;
    s.Each([&](Entity e) {
        const SceneMembership* m = s.TryGet<SceneMembership>(e);
        if (m && (m->flags & kSceneFlagDontDestroyOnLoad)) ++n;
    });
    return n;
}

} // namespace lemon::ecs
