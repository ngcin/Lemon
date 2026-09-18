// Lemon 引擎 — 空间哈希实现
#include "Physics2D/SpatialHash.h"

#include <algorithm>
#include <cstring>

#include "Components/CoreComponents.h"

namespace lemon::physics2d {

using ecs::Entity;
using ecs::Scene;
using ecs::Transform2D;

void SpatialHash::Rebuild(Scene& scene) {
    items_.clear();

    // 收集（Transform2D 池遍历；push_back 均摊零分配——容量只增）
    auto view = scene.View<Transform2D>();
    items_.reserve(scene.Pool<Transform2D>().size());
    for (auto [ent, tf] : view.each()) {
        int32_t cx = (int32_t)std::floor(tf.pos.x * invCell_);
        int32_t cy = (int32_t)std::floor(tf.pos.y * invCell_);
        items_.push_back({CellKey(cx, cy), (uint32_t)ent});
    }

    // 双关键字排序：cellKey → entityId（cell 内 id 升序 = 确定性命中序）
    std::sort(items_.begin(), items_.end(), [](const Item& a, const Item& b) {
        return a.key != b.key ? a.key < b.key : a.ent < b.ent;
    });

    // 压出 cell 区间表（前缀）
    cellKeys_.clear();
    cellStarts_.clear();
    cellStarts_.push_back(0);
    for (uint32_t i = 0; i < items_.size();) {
        uint64_t key = items_[i].key;
        uint32_t begin = i;
        while (i < items_.size() && items_[i].key == key) ++i;
        cellKeys_.push_back(key);
        cellStarts_.push_back(i);
        (void)begin;
    }
}

void SpatialHash::CellRange(uint64_t key, uint32_t& begin, uint32_t& end) const {
    // 二分 cellKeys_（元素少、缓存友好；cell 数 ≈ n/密度）
    auto it = std::lower_bound(cellKeys_.begin(), cellKeys_.end(), key);
    if (it == cellKeys_.end() || *it != key) {
        begin = end = 0;
        return;
    }
    uint32_t idx = (uint32_t)(it - cellKeys_.begin());
    begin = cellStarts_[idx];
    end = cellStarts_[idx + 1];
}

bool SpatialHash::PassFilter(Scene& s, uint32_t entRaw, const QueryFilter& f) const {
    entt::entity ent = (entt::entity)entRaw;
    if (!s.Registry().valid(ent)) return false; // 重建后已销毁
    Entity e = Scene::FromEntt(ent);
    if (e == f.exclude) return false;
    const ecs::Meta* meta = s.TryGet<ecs::Meta>(e);
    if (meta) {
        // team/layer 均为位索引（0..31 / 0..15）；≥ 上限视为数据错误 → 静默不命中
        //（审计修复：原判断写反，越界值实际会"跳过过滤"被放行）
        if (meta->team >= 32 || !(f.teamMask & (1u << meta->team))) return false;
        if (meta->layer >= 16 || !(f.layerMask & (1u << meta->layer))) return false;
    }
    return true;
}

void SpatialHash::OverlapCircle(Scene& scene, Vec2 center, float radius,
                                const QueryFilter& f, float probeRadius,
                                FunctionRef<bool(Entity, const Transform2D&)> cb) const {
    float reach = radius + probeRadius;
    int32_t x0 = (int32_t)std::floor((center.x - reach) * invCell_);
    int32_t x1 = (int32_t)std::floor((center.x + reach) * invCell_);
    int32_t y0 = (int32_t)std::floor((center.y - reach) * invCell_);
    int32_t y1 = (int32_t)std::floor((center.y + reach) * invCell_);

    for (int32_t cy = y0; cy <= y1; ++cy) {
        for (int32_t cx = x0; cx <= x1; ++cx) {
            uint32_t begin, end;
            CellRange(CellKey(cx, cy), begin, end);
            for (uint32_t i = begin; i < end; ++i) {
                uint32_t entRaw = items_[i].ent;
                entt::entity ent = (entt::entity)entRaw;
                if (!PassFilter(scene, entRaw, f)) continue;
                const Transform2D* tf = scene.Registry().try_get<Transform2D>(ent);
                if (!tf) continue;
                float r2 = LengthSq(tf->pos - center);
                if (r2 <= reach * reach) {
                    if (!cb(Scene::FromEntt(ent), *tf)) return;
                }
            }
        }
    }
}

void SpatialHash::OverlapBox(Scene& scene, Rect box, const QueryFilter& f,
                             float probeRadius,
                             FunctionRef<bool(Entity, const Transform2D&)> cb) const {
    Rect reach = box.Expanded(probeRadius);
    int32_t x0 = (int32_t)std::floor(reach.min.x * invCell_);
    int32_t x1 = (int32_t)std::floor(reach.max.x * invCell_);
    int32_t y0 = (int32_t)std::floor(reach.min.y * invCell_);
    int32_t y1 = (int32_t)std::floor(reach.max.y * invCell_);

    for (int32_t cy = y0; cy <= y1; ++cy) {
        for (int32_t cx = x0; cx <= x1; ++cx) {
            uint32_t begin, end;
            CellRange(CellKey(cx, cy), begin, end);
            for (uint32_t i = begin; i < end; ++i) {
                uint32_t entRaw = items_[i].ent;
                entt::entity ent = (entt::entity)entRaw;
                if (!PassFilter(scene, entRaw, f)) continue;
                const Transform2D* tf = scene.Registry().try_get<Transform2D>(ent);
                if (!tf) continue;
                if (reach.Contains(tf->pos)) {
                    if (!cb(Scene::FromEntt(ent), *tf)) return;
                }
            }
        }
    }
}

RayHit SpatialHash::Raycast(Scene& scene, Vec2 origin, Vec2 dir, float maxDist,
                            const QueryFilter& f, float probeRadius) const {
    RayHit best;
    best.entity = Entity::Null();
    best.distance = 3.4e38f; // FLT_MAX：未命中的哨兵（tHit < best 比较基线）

    Vec2 d = Normalize(dir);
    if (d == Vec2::Zero()) {
        // 退化：按点查询处理
        Entity e = PointQuery(scene, origin, f, probeRadius);
        best.entity = e;
        best.point = origin;
        return best;
    }

    // 粗扫：沿线段采样覆盖 cell（步长 = cell 对角线一半，简单稳健；
    // DDA 优化待 profile 数据（命中候选通常稀疏））
    float cellSize = 1.0f / invCell_;
    float step = cellSize * 0.5f;
    Vec2 start = origin - d * probeRadius;
    float total = maxDist + probeRadius;

    for (float t = 0.0f; t <= total; t += step) {
        Vec2 p = start + d * t;
        int32_t cx = (int32_t)std::floor(p.x * invCell_);
        int32_t cy = (int32_t)std::floor(p.y * invCell_);
        // 该采样点 cell 及邻接 cell（防步长跨 cell 漏检）
        for (int32_t oy = -1; oy <= 1; ++oy) {
            for (int32_t ox = -1; ox <= 1; ++ox) {
                uint32_t begin, end;
                CellRange(CellKey(cx + ox, cy + oy), begin, end);
                for (uint32_t i = begin; i < end; ++i) {
                    uint32_t entRaw = items_[i].ent;
                    entt::entity ent = (entt::entity)entRaw;
                    if (!PassFilter(scene, entRaw, f)) continue;
                    const Transform2D* tf = scene.Registry().try_get<Transform2D>(ent);
                    if (!tf) continue;
                    // 精确：点到射线参数 t̂ 与垂距
                    Vec2 toT = tf->pos - origin;
                    float tHit = Dot(toT, d);
                    if (tHit < 0.0f || tHit > maxDist) continue;
                    float perp = LengthSq(toT - d * tHit);
                    if (perp <= probeRadius * probeRadius && tHit < best.distance) {
                        best.entity = Scene::FromEntt(ent);
                        best.distance = tHit;
                        best.point = origin + d * tHit;
                        // 法线 = 从实体中心指向命中点（圆近似）
                        Vec2 n = best.point - tf->pos;
                        best.normal = Length(n) > 1e-6f ? Normalize(n) : -d;
                    }
                }
            }
        }
        if (!best.entity.IsNull() && best.distance <= t + step) break; // 已过最近命中
    }
    return best;
}

ecs::Entity SpatialHash::PointQuery(Scene& scene, Vec2 point, const QueryFilter& f,
                                    float probeRadius) const {
    Entity found = Entity::Null();
    OverlapCircle(scene, point, probeRadius, f, 0.0f,
                  [&found](Entity e, const Transform2D&) {
                      found = e;
                      return false; // 第一个（id 最小）即返回
                  });
    return found;
}

} // namespace lemon::physics2d
