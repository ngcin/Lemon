// Lemon 引擎 — 03 §4 系统管线 16 系统（12 真实现 + 4 里程碑占位）
// 占位（空 Tick，保管线位置/执行序/F3 可见）：Director(M5 波次)、
// Navigation(M6 FlowField)、CSharpBatch(M3)、Extract 侧无。
// 并行系统均为"逐实体读邻居/配置、只写自身"模式（01 §3.1 原生并行形态②/③）：
// 切分 = 组件池 packed 下标，块内独立、无共享写 → 与确定性回放兼容。
#pragma once

#include <vector>

#include "Core/Math.h"
#include "ECS/Entity.h"
#include "ECS/SystemPipeline.h"

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
    /// 收集各声明队 + （可选）全部实体的位置（Meta+Transform 池一遍）
    void Rebuild(Scene& scene, bool collectAll);
    Entity Nearest(uint32_t team, Vec2 from, float range, Entity exclude) const;
    Entity NearestAny(Vec2 from, float range, Entity exclude) const; // Flee 威胁

private:
    struct TeamList {
        uint32_t id;
        std::vector<TargetEntry> list;
    };
    std::vector<TeamList> teams_;
    std::vector<TargetEntry> all_;
};

// ---- FixedTick 主序（03 §4 表）-------------------------------------------

/// #1 输入快照：M2 输入经 World::ApplyInput 注入（录制/回放同一通道），
/// 本系统保留管线锚位；主线程采样→模拟线程投递在 M4 编辑器/窗口接驳时启用
class InputSnapshotSystem final : public ISystem {
public:
    const char* Name() const override { return "InputSnapshot"; }
    void Tick(World& world, Scene& scene, float dt) override;
};

/// #2 刷怪导演：M5（波次表/budget/capAlive）；M2 占位
class DirectorSystem final : public ISystem {
public:
    const char* Name() const override { return "Director"; }
    void Tick(World& world, Scene& scene, float dt) override;
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

/// #9 命中：投射物/Hazard → Team 判定 → 伤害/击退/Hit/Death 事件
class HitboxSystem final : public ISystem {
public:
    const char* Name() const override { return "Hitbox"; }
    void Tick(World& world, Scene& scene, float dt) override;
};

/// #10 触发器：Trigger2D 圆域差分 → Enter/Exit 事件（once 语义）
class TriggerSystem final : public ISystem {
public:
    const char* Name() const override { return "Trigger"; }
    void Tick(World& world, Scene& scene, float dt) override;
};

/// #11 数值：StatusEffects 计时/到期 + XpProgress 升级（幂曲线，资产化 M5）
class StatSystem final : public ISystem {
public:
    const char* Name() const override { return "Stat"; }
    void Tick(World& world, Scene& scene, float dt) override;
    float xpCurveK = 1.25f; // xpToNext 增长系数
};

/// #12 动画推进（M2 最小：time 推进；curFrame 帧映射接 clip 资产后补，M5）
class AnimatorSystem final : public ISystem {
public:
    const char* Name() const override { return "Animator"; }
    void Tick(World& world, Scene& scene, float dt) override;
};

/// #13 投射物回收：寿命/越界 → 销毁队列（并行）
class ProjectileLifetimeSystem final : public ISystem {
public:
    const char* Name() const override { return "ProjectileLifetime"; }
    void Tick(World& world, Scene& scene, float dt) override;
};

/// #14 C# 批量系统：M3（IForEachSystem 块回调）；占位
class CSharpBatchSystem final : public ISystem {
public:
    const char* Name() const override { return "CSharpBatch"; }
    void Tick(World& world, Scene& scene, float dt) override;
};

/// #15 事件派发：帧末批量派发给事件汇后清空（M3 换 C# 桥端）
class ScriptEventDispatchSystem final : public ISystem {
public:
    const char* Name() const override { return "ScriptEventDispatch"; }
    void Tick(World& world, Scene& scene, float dt) override;
};

// ---- Essential -------------------------------------------------------------

/// #16 销毁提交：两阶段销毁的统一提交点（Essential 阶段）
class DestroyCommitSystem final : public ISystem {
public:
    const char* Name() const override { return "DestroyCommit"; }
    SystemStage Stage() const override { return SystemStage::Essential; }
    void Tick(World& world, Scene& scene, float dt) override;
};

} // namespace lemon::ecs
