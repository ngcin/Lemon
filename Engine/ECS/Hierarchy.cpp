#include "ECS/Hierarchy.h"

#include <cmath>
#include <vector>

#include "Components/CoreComponents.h"
#include "Core/Log.h"
#include "Core/Math.h"

namespace lemon::ecs {

namespace {

/// 从父链上摘除 child（维护 prev/next/parent 的 firstChild；**firstChild 保留**——
/// 子树随行，这是"摘枝不摘孙"的语义）
void Unlink(Scene& s, Entity child) {
    Hierarchy& h = s.Get<Hierarchy>(child);
    if (!h.prev.IsNull()) {
        if (Hierarchy* p = s.TryGet<Hierarchy>(h.prev)) p->next = h.next;
    } else if (!h.parent.IsNull()) {
        if (Hierarchy* p = s.TryGet<Hierarchy>(h.parent)) p->firstChild = h.next;
    }
    if (!h.next.IsNull()) {
        if (Hierarchy* n = s.TryGet<Hierarchy>(h.next)) n->prev = h.prev;
    }
    h.parent = Entity::Null();
    h.next = Entity::Null();
    h.prev = Entity::Null();
}

/// e 是否在 ancestor 的后代子树内（DFS 按链遍历；guard 防链异常死循环）
bool IsDescendantOf(Scene& s, Entity e, Entity ancestor) {
    std::vector<Entity> stack;
    if (const Hierarchy* a = s.TryGet<Hierarchy>(ancestor))
        if (!a->firstChild.IsNull()) stack.push_back(a->firstChild);
    uint32_t guard = 0;
    while (!stack.empty()) {
        Entity cur = stack.back();
        stack.pop_back();
        if (++guard > 100000) break;
        if (cur == e) return true;
        const Hierarchy* h = s.TryGet<Hierarchy>(cur);
        if (!h) continue;
        if (!h->next.IsNull()) stack.push_back(h->next);
        if (!h->firstChild.IsNull()) stack.push_back(h->firstChild);
    }
    return false;
}

/// 子树高度（e 自身为 0；链异常返回大值以触发拒绝）
uint32_t SubtreeHeight(Scene& s, Entity e) {
    struct Frame { Entity e; uint32_t d; };
    std::vector<Frame> stack;
    uint32_t height = 0;
    if (const Hierarchy* h = s.TryGet<Hierarchy>(e))
        if (!h->firstChild.IsNull()) stack.push_back({h->firstChild, 1});
    uint32_t guard = 0;
    while (!stack.empty()) {
        Frame f = stack.back();
        stack.pop_back();
        if (++guard > 100000) return kMaxHierarchyDepth + 2;
        height = std::max(height, f.d);
        const Hierarchy* h = s.TryGet<Hierarchy>(f.e);
        if (!h) continue;
        if (!h->next.IsNull()) stack.push_back({h->next, f.d});
        if (!h->firstChild.IsNull()) stack.push_back({h->firstChild, f.d + 1});
    }
    return height;
}

} // namespace

bool SceneSetParent(Scene& s, Entity child, Entity newParent) {
    if (child.IsNull() || !s.Alive(child)) return false;
    if (child == newParent) return false;
    if (!newParent.IsNull()) {
        if (!s.Alive(newParent)) return false;
        // 成环：目标是我自己的后代 → 拒绝（含"挂到自己的子实体上"）
        if (IsDescendantOf(s, newParent, child)) {
            LEMON_WARN("SetParent 拒绝：%llu 是 %llu 的后代（成环）",
                       (unsigned long long)newParent.id, (unsigned long long)child.id);
            return false;
        }
        // 深度：新 parent 深度 + 1（child 层）+ 子树高度 ≤ 上限
        int pd = HierarchyDepth(s, newParent);
        uint32_t h = SubtreeHeight(s, child);
        if (pd < 0 || (uint32_t)pd + 1 + h > kMaxHierarchyDepth) {
            LEMON_WARN("SetParent 拒绝：深度 %d+%u+1 超上限 %u", pd, h, kMaxHierarchyDepth);
            return false;
        }
    }

    if (s.Has<Hierarchy>(child)) Unlink(s, child);
    if (newParent.IsNull()) return true; // 摘根完成

    // 挂到 newParent 子链头（新子插头：O(1)；Hierarchy 不存在则建，已存在则复用）。
    // 先确保两个组件都存在、再取引用写链：两次取引用之间夹对同一组件池的
    // Emplace，EnTT 池底层 vector 扩容搬移会使先取的引用悬空——child/newParent
    // 均无 Hierarchy 的首次挂接（池 0→1→2 连续扩容）几乎必中（2026-09-24 审查）
    if (!s.Has<Hierarchy>(child)) s.Emplace<Hierarchy>(child);
    if (!s.Has<Hierarchy>(newParent)) s.Emplace<Hierarchy>(newParent);
    Hierarchy& c = s.Get<Hierarchy>(child);
    Hierarchy& p = s.Get<Hierarchy>(newParent);
    c.parent = newParent;
    c.prev = Entity::Null();
    c.next = p.firstChild;
    if (!p.firstChild.IsNull())
        if (Hierarchy* old = s.TryGet<Hierarchy>(p.firstChild)) old->prev = child;
    p.firstChild = child;
    return true;
}

bool SceneDetach(Scene& s, Entity e) {
    if (e.IsNull() || !s.Alive(e)) return false;
    if (!s.Has<Hierarchy>(e)) return true;
    Unlink(s, e);
    return true;
}

void SceneDestroyEntityTree(Scene& s, Entity e) {
    if (e.IsNull() || !s.Alive(e)) return;
    // 先收集子树（firstChild/next 链遍历），再统一摘除+入销毁队列。
    // 销毁顺序父先子后（队列内序），CommitDestroys 幂等校验兜底。
    std::vector<Entity> sub;
    sub.push_back(e);
    for (size_t i = 0; i < sub.size(); ++i) {
        const Hierarchy* h = s.TryGet<Hierarchy>(sub[i]);
        if (!h) continue;
        if (!h->firstChild.IsNull()) sub.push_back(h->firstChild);
        if (i > 0 && !h->next.IsNull()) sub.push_back(h->next); // 根不带兄弟
    }
    for (Entity d : sub) {
        if (s.Has<Hierarchy>(d)) Unlink(s, d);
        s.Destroy(d); // 两阶段入队；编辑器帧内由 Step/显式 CommitDestroys 提交
    }
}

int HierarchyDepth(Scene& s, Entity e) {
    int depth = 0;
    Entity cur = e;
    for (;;) {
        if (depth > (int)kMaxHierarchyDepth + 1) return -1;
        const Hierarchy* h = s.TryGet<Hierarchy>(cur);
        if (!h) return depth; // 到根（或本无 Hierarchy）
        if (h->parent.IsNull()) return depth;
        if (!s.Alive(h->parent)) return -1; // 死引用（悬挂链）
        if (h->parent == e) return -1;      // 直环
        cur = h->parent;
        ++depth;
    }
}

bool ComputeWorldTransform(Scene& s, Entity e, WorldTransform2D& out) {
    // 自根向下复合。TRS 语义（与 Mat3x2::FromTRS 乘积一致）：
    //   world = T(p) R(r) S(sc)；子局部偏移经父系 = R(rot)(scale ⊙ localPos)
    Entity chain[kMaxHierarchyDepth + 1];
    int n = 0;
    Entity cur = e;
    while (true) {
        if (n > (int)kMaxHierarchyDepth) return false; // 过深/环
        const Hierarchy* h = s.TryGet<Hierarchy>(cur);
        if (!h || h->parent.IsNull()) break;
        if (!s.Alive(h->parent)) return false; // 悬挂链
        chain[n++] = h->parent;
        cur = h->parent;
    }
    Vec2 pos{0, 0}, scale{1, 1};
    float rot = 0.0f;
    for (int i = n - 1; i >= 0; --i) {
        const Transform2D* t = s.TryGet<Transform2D>(chain[i]);
        if (!t) continue; // 祖先无 Transform：空组节点，跳过该层
        const float cs = std::cos(rot), sn = std::sin(rot);
        const Vec2 sp{t->pos.x * scale.x, t->pos.y * scale.y};
        pos = Vec2{pos.x + cs * sp.x - sn * sp.y, pos.y + sn * sp.x + cs * sp.y};
        rot += t->rot;
        scale = Vec2{scale.x * t->scale.x, scale.y * t->scale.y};
    }
    const Transform2D* self = s.TryGet<Transform2D>(e);
    if (!self) return false;
    const float cs = std::cos(rot), sn = std::sin(rot);
    const Vec2 sp{self->pos.x * scale.x, self->pos.y * scale.y};
    out.pos = Vec2{pos.x + cs * sp.x - sn * sp.y, pos.y + sn * sp.x + cs * sp.y};
    out.rot = rot + self->rot;
    out.scale = Vec2{scale.x * self->scale.x, scale.y * self->scale.y};
    return true;
}

} // namespace lemon::ecs
