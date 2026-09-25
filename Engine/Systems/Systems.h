// Lemon 引擎 — 03 §4 系统管线 17 系统（13 真实现 + 4 里程碑占位）
// 占位（空 Tick，保管线位置/执行序/F3 可见）：Director(M5 波次)、
// Navigation(M6 FlowField)、CSharpBatch(M3)、Extract 侧无。
// 并行系统均为"逐实体读邻居/配置、只写自身"模式（01 §3.1 原生并行形态②/③）：
// 切分 = 组件池 packed 下标，块内独立、无共享写 → 与确定性回放兼容。
#pragma once

#include <vector>

#include "Core/Math.h"
#include "ECS/Entity.h"
#include "ECS/Events.h"
#include "ECS/SystemPipeline.h"

namespace lemon {
class JobSystem;
}

namespace lemon::ecs {

// ------------------------------------------------- AI 目标板（最近邻）------
// 按 team 预收集的目标位置表。为什么不用哈希大半径查询：Chase/Shooter 的
// 目标 team 通常只有玩家等极少数实体（稀疏目标），大半径 OverlapCircle 扫的
// 是覆盖区全部 cell（O(cells)，retarget 帧尖峰源头）；预收集后线性最近邻为
// O(teamSize)。目标密集的查询（命中/分离/磁吸/触发）仍走空间哈希——各得其所。
struct TargetEntry {
    Entity e;
    Vec2 pos;
};

class TargetBoard {
public:
    void DeclareTeams(const std::vector<uint32_t>& teamIds);
    /// 收集各声明队 + （可选）全部实体的位置（Meta+Transform 池一遍）。
    /// jobs 非空且实体量 ≥ kParallelMin 时并行收集/建桶（2026-09-26 五万场批）：
    /// list 序 = view 迭代序逐位不变（chunk 按 view 序切分、按序归并）；
    /// 桶内池序 = 排序键 (cellKey, 池索引) 唯一 → 并行建桶结果与串行逐位同构。
    void Rebuild(Scene& scene, bool collectAll, JobSystem* jobs = nullptr);
    Entity Nearest(uint32_t team, Vec2 from, float range, Entity exclude) const;
    Entity NearestAny(Vec2 from, float range, Entity exclude) const; // Flee 威胁
    /// 声明队的收集表（view 序；串行/并行同构对拍等测试用）
    const std::vector<TargetEntry>& TeamEntries(uint32_t team) const;

    /// 并行收集门槛：低于此串行（并行派发不回本）；块粒度（chunk 内保序单元）
    static constexpr uint32_t kParallelMin = 8192;
    static constexpr uint32_t kParallelGrain = 2048;

private:
    struct TeamList {
        uint32_t id;
        std::vector<TargetEntry> list;
        /// 网格桶最近邻（2026-09-25）：many-vs-many 场（红蓝对抗：13500 查询者 ×
        /// 6500 候选 ≈ 25ms）暴露线性全扫的墙。Rebuild 建 CSR 桶（cell 64px），
        /// Nearest 环搜（ring min 距离平方 ≥ bestD2 即停）——密场 O(邻域桶)。
        /// 列表 < kMinList 保留线性快径 = 存量场景（survivor 查单玩家）行为
        /// 逐位不变。等距平局语义：线性保池序靠前者；网格保环扫序先见者——
        /// 严格小于判定不变，m5b2 三档金回放零重录实证（2026-09-25，见 DevLog）。
        struct Grid {
            static constexpr float kCell = 32.0f; // 2026-09-26 五万场：对穿乱斗密场
                                             // 环扫候选减半（64→32 AI 28.5→~15ms）
            static constexpr size_t kMinList = 64;
            std::vector<uint64_t> keys;  // 排序唯一 cell 键（(cx<<32)|cy）
            std::vector<uint32_t> offs;  // CSR 偏移（keys.size()+1）
            std::vector<uint32_t> items; // 指向 list 的索引（同 cell 保池序）
            // 占位位图（bbox 内 1bit/cell）：空环扫 = 一次界限比较 + 一次位读，
            // 不做键二分——空场远距查询（后排索敌无果）从 O(rings×log) 降为
            // O(rings)。bbox 外恒空，直接跳过。
            int minX = 0, minY = 0, maxX = -1, maxY = -1;
            std::vector<uint64_t> occ;
            std::vector<std::pair<uint64_t, uint32_t>> scratch_; // Build 暂存（复用免逐帧分配）
            void Build(const std::vector<TargetEntry>& list);
            Entity Nearest(const std::vector<TargetEntry>& list, Vec2 from, float range,
                           Entity exclude) const;
        };
        Grid grid;
    };
    std::vector<TeamList> teams_;
    std::vector<TargetEntry> all_;
    // 并行收集复用缓冲（稳态零分配）：主线程先按 view 序收集实体（view 无随机
    // 访问，list 序的等距平局语义系于此），再按索引切块并行构条目分桶，
    // 主线程按 chunk 序归并 = 保 view 序。chunkTeams_ 拍平 [chunk][team]。
    std::vector<Entity> collectEnts_;
    std::vector<std::vector<TargetEntry>> chunkTeams_;
    std::vector<std::vector<TargetEntry>> chunkAlls_;
};

// ---- FixedTick 主序（03 §4 表）-------------------------------------------

/// #1 输入快照：M2 输入经 World::ApplyInput 注入（录制/回放同一通道），
/// 本系统保留管线锚位；主线程采样→模拟线程投递在 M4 编辑器/窗口接驳时启用
class InputSnapshotSystem final : public ISystem {
public:
    const char* Name() const override { return "InputSnapshot"; }
    void Tick(World& world, Scene& scene, float dt) override;
};

/// #2 刷怪导演：波次表（WaveDirector 组件，数据驱动）→ 直接经 SpawnFn 出生 +
/// WaveStart/Spawn 事件；capAlive 同队闸门（30 tick 普查"约"语义）。M5 批②。
class DirectorSystem final : public ISystem {
public:
    const char* Name() const override { return "Director"; }
    void Tick(World& world, Scene& scene, float dt) override;

    static constexpr uint32_t kCensusInterval = 30; // 普查周期（tick；SpawnSystem 同款）

private:
    void Census(Scene& scene);
    std::vector<uint32_t> teamCounts_;
    uint32_t censusCountdown_ = 0; // 0 = 本 tick 普查（首 tick 必普查）
    bool warnedNoFactory_ = false; // 无工厂告警一次（仅场景确有导演时）
};

/// #3 出生系统：Spawner cooldown → 预制体工厂实例化 + Spawn 事件；
/// maxAlive 配额按 team 计数（周期普查，半秒级滞后——压测红线语义）
class SpawnSystem final : public ISystem {
public:
    const char* Name() const override { return "Spawn"; }
    void Tick(World& world, Scene& scene, float dt) override;

    static constexpr uint32_t kCensusInterval = 30; // 普查周期（tick）

private:
    void Census(Scene& scene);
    std::vector<uint32_t> teamCounts_;
    uint32_t censusCountdown_ = 0; // 0 = 本 tick 普查（首 tick 必普查）
    bool warnedNoFactory_ = false; // 无工厂告警一次（[ISSUE-4] 仅场景确有 Spawner 时）
};

/// #4 行为 AI：Chase/Patrol/Flee/Shooter → Velocity（并行；最近邻走目标板）
class AISystem final : public ISystem {
public:
    const char* Name() const override { return "AI"; }
    void Tick(World& world, Scene& scene, float dt) override;

private:
    TargetBoard board_;
    std::vector<uint32_t> wantedTeams_;
};

/// #5 寻路：M6 FlowField；M2 占位（Chase 直线逼近即品类够用）
class NavigationSystem final : public ISystem {
public:
    const char* Name() const override { return "Navigation"; }
    void Tick(World& world, Scene& scene, float dt) override;
};

/// #6 分离力：同队 soft-collide 的 boids 式排斥（并行；读上一帧哈希邻居，
/// 只写自身 Velocity；密度上限衰减防堆爆）
class SeparationSystem final : public ISystem {
public:
    const char* Name() const override { return "Separation"; }
    void Tick(World& world, Scene& scene, float dt) override;

    float radius = 32.0f;          // 邻居判定半径（px）
    float strength = 140.0f;       // 分离加速度
    uint32_t densityCap = 12;      // 超过此有效邻居数按比例衰减力
    uint32_t maxNeighbors = 10;    // 每实体处理上限（密度截断，见实现注释）
};

/// #7 移动积分：Velocity + Knockback 衰减 → Transform2D（并行；边界钳制）
class MovementSystem final : public ISystem {
public:
    const char* Name() const override { return "Movement"; }
    void Tick(World& world, Scene& scene, float dt) override;
};

/// #8 空间哈希重建（Movement 后、Hitbox 前——命中判新位置，AI 用旧位置）
class SpatialHashRebuildSystem final : public ISystem {
public:
    const char* Name() const override { return "SpatialHashRebuild"; }
    void Tick(World& world, Scene& scene, float dt) override;
};

/// #9 拾取：Collectible 磁吸（双侧取大触程）→ 飞行 → 触距入账 + Pickup 事件。
/// 收集者约定 = 持 XpProgress 的实体（VS 心智：唯玩家拾取）；磁吸直写 pos
/// （不经 Velocity——无 Movement/Separation 竞争）；不用 RNG（子流零扰动）
class PickupSystem final : public ISystem {
public:
    const char* Name() const override { return "Pickup"; }
    void Tick(World& world, Scene& scene, float dt) override;

    static constexpr float kPickupTouch = 8.0f; // 触距（px）：入账判定口径
};

/// #10 命中：投射物/Hazard → Team 判定 → 伤害/击退/Hit/Death 事件
class HitboxSystem final : public ISystem {
public:
    const char* Name() const override { return "Hitbox"; }
    void Tick(World& world, Scene& scene, float dt) override;
};

/// #11 触发器：Trigger2D 圆域差分 → Enter/Exit 事件（once 语义）
class TriggerSystem final : public ISystem {
public:
    const char* Name() const override { return "Trigger"; }
    void Tick(World& world, Scene& scene, float dt) override;
};

/// #12 数值：StatusEffects 计时/到期 + XpProgress 升级（幂曲线，资产化 M5）
class StatSystem final : public ISystem {
public:
    const char* Name() const override { return "Stat"; }
    void Tick(World& world, Scene& scene, float dt) override;
    float xpCurveK = 1.25f; // xpToNext 增长系数
};

/// #13 动画推进（M5 批③：ClipTable 帧映射 → curFrame/spriteId；clipId=0 或
/// 未命中 = M2 旧路径逐位不变——金档零漂移前提，M5.md §18。M6a 批①：换段队列
/// Play/Queue/CrossFade——先推进后判定，语义见实现头注）
class AnimatorSystem final : public ISystem {
public:
    const char* Name() const override { return "Animator"; }
    void Tick(World& world, Scene& scene, float dt) override;

private:
    bool warnedQueueMiss_ = false; // 换段目标未命中告警一次（World 级 = 每局一次）
};

/// #14 投射物回收：寿命/越界 → 销毁队列（并行）
class ProjectileLifetimeSystem final : public ISystem {
public:
    const char* Name() const override { return "ProjectileLifetime"; }
    void Tick(World& world, Scene& scene, float dt) override;
};

/// #15 C# 批量系统：M3（IForEachSystem 块回调）；占位
class CSharpBatchSystem final : public ISystem {
public:
    const char* Name() const override { return "CSharpBatch"; }
    void Tick(World& world, Scene& scene, float dt) override;
};

/// #16 事件派发：帧末批量派发给事件汇后清空（M3 换 C# 桥端）
class ScriptEventDispatchSystem final : public ISystem {
public:
    const char* Name() const override { return "ScriptEventDispatch"; }
    void Tick(World& world, Scene& scene, float dt) override;

private:
    std::vector<EventPacket> dispatchBuf_; // 派发快照（跨帧复用；与队列底层分离）
};

// ---- Essential -------------------------------------------------------------

/// #17 销毁提交：两阶段销毁的统一提交点（Essential 阶段）
class DestroyCommitSystem final : public ISystem {
public:
    const char* Name() const override { return "DestroyCommit"; }
    SystemStage Stage() const override { return SystemStage::Essential; }
    void Tick(World& world, Scene& scene, float dt) override;
};

} // namespace lemon::ecs
