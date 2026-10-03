// Lemon 引擎 — sprite 引用 GUID 归一（M6a 批⓪ T2 语义 / M7a 批② 纯函数化下沉
// 自 EditorContext::ResolveSpriteRefs）。场景/Prefab 装载后统一归一，编辑器
//（EditorContext）与运行时（lemon-game，批④）两薄壳共用——杜绝双实现漂移。
//
// .scene 双写 {spriteGuid（真源，.meta 随文件走）, spriteId（AtlasRegistry 进程内
// 派生号）}。装载/恢复/重建后调本函数归一：
//   * guid≠0 命中 → 覆写 spriteId（改名/移位/manifest 重建 id 漂移后引用不断链）；
//     id 已在合法域（整图 = 本体号；切片表 = cell 区间）则保号不覆写——切片表的
//     本体号与 cell 号相邻，跨进程漂移后旧 cell-0 可能恰好撞新本体号（smoke-guid
//     BossMob 实证），无法甄别 → 切片表一律按 cell 口径归一：区间外回 cell 0；
//     整图区间外回本体号
//   * guid≠0 查无/非 sprite → 保留 spriteId + 计入 danglingGuid（渲染仍用旧号，
//     引用槽红显可修；不静默清零）
//   * guid=0 且 spriteId 恰为资产本体号 → 回填 guid（FindBySpriteId 只匹配本体
//     号：切片 cell 号/程序化页号查无 = 天然不回填）。回填计数归调用方（编辑器
//     据此标 dirty——存量档下次保存即升级 guid 主键）
// 高频 spawn 路径不走此函数：同进程 id 即真值，热路径零扫表。
#pragma once

#include <cstdint>

#include "Components/RenderComponents.h" // SpriteRenderer
#include "ECS/Scene.h"

namespace lemon::assets {

/// 归一统计（EditorContext::SpriteRefStats 同款语义；编辑器壳聚合告警用）
struct SpriteRefStats {
    uint32_t danglingGuid = 0; // guid≠0 查无（spriteId 保留旧值渲染）
    uint32_t backfilled = 0;   // 存量 guid=0 → 补写（保存后升级 guid 主键）
};

/// 条目只读视图（归一判定所需字段的最小面；AssetDatabase/AssetIndex 各自适配。
/// alive=false = 编辑器 Remove() 后到下轮 Rescan 前的同帧隐藏位——归一侧按
/// 悬空处理，与原 AssetDatabase 消费面同语义）
struct SpriteEntryView {
    uint64_t guid = 0;
    uint32_t spriteId = 0;
    uint32_t sliceBase = 0, sliceCount = 0;
    bool alive = true;
};

/// 查询面接口：编辑器 AssetDatabase（含 missing 同帧语义）与运行时 AssetIndex
/// 各实现。装载期 N 实体 × O(1..M) 查询——虚调用成本可忽略。
/// 方法名避开 FindByGuid/FindBySpriteId（AssetDatabase 既有同名 API 返回条目
/// 指针，签名不同不可共载）。
struct SpriteRefSource {
    virtual ~SpriteRefSource() = default;
    /// guid → 视图（非 sprite/查无 = nullptr；spriteId==0 的异常态与 alive=false
    /// 照实返回，由归一侧判 dangling）
    virtual const SpriteEntryView* SpriteByGuid(uint64_t guid) const = 0;
    /// 本体号 → 视图（切片 cell 号/程序化页号 = nullptr——回填只认本体号）
    virtual const SpriteEntryView* SpriteByWholeId(uint32_t spriteId) const = 0;
    /// 程序化图集基号（id < 基号 = 程序化页，无 guid 语义不回填）
    virtual uint32_t SpriteIdBase() const = 0;
};

/// 场景内全部 SpriteRenderer 的 guid/id 归一（语义见文件头；幂等——已归一场景
/// 再跑零写入）。dirty 位归调用方（backfilled > 0 时编辑器侧标 dirty）。
SpriteRefStats ResolveSpriteRefs(ecs::Scene& scene, const SpriteRefSource& src);

} // namespace lemon::assets
