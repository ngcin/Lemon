// Lemon 引擎 — 空间哈希 Broadphase（03 文档 §5：物理查询层地基）
// 结构：动态层每帧重建——(cellKey, entity) 对排序数组 + 每 cell 连续区间。
//   * 排序键 = (cellKey, entityId) 双关键字 → cell 内实体恒按 id 升序，
//     命中顺序与帧率/线程调度无关（确定性回放前提，03 §5）；
//   * 重建 O(n log n)；查询 O(覆盖 cell 内实体数)，零分配；
//   * 静态层（地形常驻、增量维护）M6 Tilemap 接入，接口预留。
// 实体侧形状语义：查询层无 solver、无实体半径组件（03 目录）；命中判定按
// 查询方给定的探测半径参数（probeRadius：弹幕 4px、磁吸 32px 等，与
// QueryFilter 分离——同一过滤器可用于不同探测半径的多次查询）。
#pragma once

#include <cstdint>
#include <vector>

#include "Components/CoreComponents.h"
#include "Core/FunctionRef.h"
#include "Core/Math.h"
#include "ECS/Entity.h"
#include "ECS/Scene.h"

namespace lemon::physics2d {

struct QueryFilter {
    uint32_t teamMask = 0xFFFFFFFFu; // 命中 team 位掩码（Meta.team ∈ [0,32)）
    uint16_t layerMask = 0xFFFFu;    // 命中 layer 位掩码（位索引 0..15）
    ecs::Entity exclude{};           // 排除实体（通常是自己）
};

struct RayHit {
    ecs::Entity entity{};
    Vec2 point{};
    Vec2 normal{};
    float distance = 0.0f;
};

class SpatialHash {
public:
    static constexpr float kDefaultCellSize = 64.0f; // 03 §5：64px 起步

    void Configure(float cellSize = kDefaultCellSize) { invCell_ = 1.0f / cellSize; }

    /// 动态层重建：收集全部 Transform2D 实体 → 排序成 cell 区间。
    /// 单线程（03 §14 预算 1.2ms@10k；不达标再上并行排序）
    void Rebuild(ecs::Scene& scene);

    /// 圆重叠查询：命中（圆-圆：实体中心距 < radius + probeRadius）回调 cb；
    /// cb 返回 false 提前终止。回调序 = cell 序 → id 升序（确定性）。
    void OverlapCircle(ecs::Scene& scene, Vec2 center, float radius, const QueryFilter& f,
                       float probeRadius,
                       FunctionRef<bool(ecs::Entity, const ecs::Transform2D&)> cb) const;

    /// 盒重叠查询（AABB，含 probeRadius 外扩）
    void OverlapBox(ecs::Scene& scene, Rect box, const QueryFilter& f, float probeRadius,
                    FunctionRef<bool(ecs::Entity, const ecs::Transform2D&)> cb) const;

    /// 射线查询：返回最近命中（DDA 走格 + 圆判定）；无命中 entity.IsNull()
    RayHit Raycast(ecs::Scene& scene, Vec2 origin, Vec2 dir, float maxDist,
                   const QueryFilter& f, float probeRadius) const;

    /// 点查询
    ecs::Entity PointQuery(ecs::Scene& scene, Vec2 point, const QueryFilter& f,
                           float probeRadius) const;

    /// 邻域遍历（分离力专用：回调全部位于 pos 半径内的实体，含 team 过滤）
    void ForEachNeighbor(ecs::Scene& scene, Vec2 pos, float radius, const QueryFilter& f,
                         FunctionRef<void(ecs::Entity, const ecs::Transform2D&)> cb) const {
        OverlapCircle(scene, pos, radius, f, 0.0f,
                      [&cb](ecs::Entity e, const ecs::Transform2D& t) {
                          cb(e, t);
                          return true;
                      });
    }

    // ---- F3 统计 ----
    uint32_t ItemCount() const { return (uint32_t)items_.size(); }
    uint32_t CellCount() const { return (uint32_t)cellKeys_.size(); }

private:
    struct Item {
        uint64_t key;   // (cy << 32) | cx
        uint32_t ent;   // entt::entity 原始位（含 version）
    };

    uint64_t CellKey(int32_t cx, int32_t cy) const {
        return (uint64_t)(uint32_t)cy << 32 | (uint32_t)cx;
    }
    /// key 在 cellKeys_ 中的区间 [begin,end)；无此 cell 返回 0 区间
    void CellRange(uint64_t key, uint32_t& begin, uint32_t& end) const;
    bool PassFilter(ecs::Scene& s, uint32_t entRaw, const QueryFilter& f) const;

    float invCell_ = 1.0f / kDefaultCellSize;
    std::vector<Item> items_;     // 排序后（key, ent）
    std::vector<uint64_t> cellKeys_;
    std::vector<uint32_t> cellStarts_; // size = cellKeys_.size()+1（前缀）
};

} // namespace lemon::physics2d
