// Lemon 引擎 — SceneMembership 原语实现（ADR-017 D1；M7c 批⑥，批⑦ D1/D2 修订）
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

bool IsDontDestroyOnLoadLineage(Scene& s, Entity e) {
    int depth = 0;
    for (Entity cur = e; !cur.IsNull() && s.Alive(cur);) {
        const SceneMembership* m = s.TryGet<SceneMembership>(cur);
        if (m && (m->flags & kSceneFlagDontDestroyOnLoad)) return true;
        const Hierarchy* h = s.TryGet<Hierarchy>(cur);
        cur = h ? h->parent : Entity::Null();
        if (++depth > (int)kMaxHierarchyDepth + 1) break; // 脏环护栏（CollectTree 同款）
    }
    return false;
}

uint32_t MarkDontDestroyOnLoad(Scene& s, Entity root) {
    if (!s.Alive(root)) return 0;
    SceneMembership* m = s.TryGet<SceneMembership>(root);
    if (!m) m = &s.Emplace<SceneMembership>(root); // 未打标实体（编辑态根）补标
    m->flags |= kSceneFlagDontDestroyOnLoad;
    return 1;
}

SceneClearReport QueueDestroyAllExceptDdolLineage(Scene& s) {
    SceneClearReport rep;
    s.Each([&](Entity e) {
        const SceneMembership* m = s.TryGet<SceneMembership>(e);
        if (m && (m->flags & kSceneFlagDontDestroyOnLoad)) return; // 根：本位直判
        if (IsDontDestroyOnLoadLineage(s, e)) return;              // 后代：祖先链
        if (!m || m->scene == kSceneHandleUnassigned) ++rep.unassignedCollected;
        s.Destroy(e); // 两阶段：入队 + DestroyQueueTag（Scene::Destroy 内打标）
        ++rep.queued;
    });
    return rep;
}

uint32_t QueueDestroySceneGroup(Scene& s, uint32_t sceneHandle) {
    uint32_t n = 0;
    s.Each([&](Entity e) {
        const SceneMembership* m = s.TryGet<SceneMembership>(e);
        if (!m || m->scene != sceneHandle) return;
        if (IsDontDestroyOnLoadLineage(s, e)) return; // 批⑦ D1：祖先链判据（含根本位）
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

uint32_t StampTreeMembership(Scene& s, Entity root, uint32_t sceneHandle) {
    if (!s.Alive(root)) return 0;
    std::vector<Entity> tree;
    CollectTree(s, root, tree);
    uint32_t n = 0;
    for (Entity e : tree) {
        if (SceneMembership* m = s.TryGet<SceneMembership>(e)) {
            if (m->scene != kSceneHandleUnassigned) continue; // 已指派不动（幂等）
            m->scene = sceneHandle;
        } else {
            s.Emplace<SceneMembership>(e).scene = sceneHandle;
        }
        ++n;
    }
    return n;
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

std::vector<Entity> CollectDontDestroyOnLoadLineage(Scene& s) {
    std::vector<Entity> out;
    s.Each([&](Entity e) {
        if (IsDontDestroyOnLoadLineage(s, e)) out.push_back(e);
    });
    return out;
}

uint32_t CountSceneRoots(Scene& s, uint32_t sceneHandle) {
    uint32_t n = 0;
    s.Each([&](Entity e) {
        const SceneMembership* m = s.TryGet<SceneMembership>(e);
        if (!m || m->scene != sceneHandle) return;
        const Hierarchy* h = s.TryGet<Hierarchy>(e);
        const Entity p = h ? h->parent : Entity::Null();
        if (p.IsNull() || !s.Alive(p)) { // 父缺失/父亡 = 组内根
            ++n;
            return;
        }
        const SceneMembership* pm = s.TryGet<SceneMembership>(p);
        if (!pm || pm->scene != sceneHandle) ++n; // 父属他组（跨组挂接）= 本组根
    });
    return n;
}

} // namespace lemon::ecs
