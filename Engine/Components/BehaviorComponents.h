// Lemon 引擎 — 组件目录 · Behavior 组（03 文档 §3.3：无代码玩法面，MoteurJV 验证切分）
// 摆放即生效：编辑器拖入组件 → 系统管线按组件存在性驱动行为。
#pragma once

#include <cstdint>
#include <type_traits>

#include "Core/Math.h"
#include "ECS/Entity.h"

namespace lemon::ecs {

struct Health {
    float max = 100.0f;
    float cur = 100.0f;
    float iFrames = 0.0f;       // 受击后无敌剩余秒（运行时）
    float iframeWindow = 0.1f;  // 受击无敌窗（秒）；复拍间隔 = ceil(窗/dt) tick
};

struct Mover {
    float speed = 0.0f;     // 匀速朝向 velocity（由 AI/输入写入方向）
};

struct Patrol {
    Vec2 a{}, b{};
    float pauseTime = 0.0f;
    uint8_t headingToB = 1;
    uint8_t _pad[3] = {};
};

struct Chase {
    float speed = 80.0f;
    float aggroRange = 300.0f;
    float keepRange = 16.0f;
    uint32_t targetTeam = 0;
    Entity target{};        // 运行时缓存（每 N tick 重找；登记为 runtime 不序列化）
};

struct Flee {
    float speed = 80.0f;
    float range = 200.0f;   // 威胁进入范围才逃
};

struct Shooter {
    uint32_t projectileId = 0;  // 投射物 prefab（M2 内部表；GUID 解析在资产侧）
    float interval = 1.0f;
    float range = 260.0f;
    uint32_t targetTeam = 0;
    float cooldown = 0.0f;      // 运行时：距下次开火
    Entity target{};            // 运行时缓存（runtime，不序列化）
};

struct Projectile {
    float speed = 300.0f;
    float lifetime = 3.0f;
    float damage = 10.0f;
    float age = 0.0f;           // 运行时
    uint8_t pierce = 0;         // 剩余穿透次数
    uint8_t homing = 0;
    uint16_t hits = 0;          // 运行时：累计命中数
    // ---- M5 批⓪ 追加（尾部追加：既有聚合构造点按默认值保持正确）----
    float hitRadius = 12.0f;    // 命中判定半径（reach 全量；原 6+6 组合的有效口径）
    float knockback = 60.0f;    // 命中击退脉冲强度（写入目标 Knockback.impulse）
    uint8_t hitHead = 0;        // 运行时：命中记忆环写指针
    uint8_t _pad2[3] = {};      // 衬齐 hitMemory 对齐
    uint32_t hitMemory[4] = {}; // 运行时：最近 4 个命中目标（低 32 位句柄，含 version）

    /// 一弹一目标一次：本弹是否已命中过该目标（0=空槽不算）
    bool HasHit(uint32_t raw) const {
        if (raw == 0) return false;
        for (uint32_t m : hitMemory)
            if (m == raw) return true;
        return false;
    }
    /// 记入命中记忆（4 槽环，满则覆写最旧——穿透场景足够，零堆分配）
    void RememberHit(uint32_t raw) {
        hitMemory[hitHead] = raw;
        hitHead = (hitHead + 1) & 3u;
    }
};

struct Spawner {
    uint32_t prefabId = 0;
    float interval = 1.0f;
    uint16_t burst = 1;
    float range = 0.0f;         // 出生点半径
    uint32_t maxAlive = 0;      // 0 = 不限（导演 capAlive 兜底）
    uint32_t spawnTeam = 1;
    float cooldown = 0.0f;      // 运行时
};

struct Hazard {
    float dps = 10.0f;
    float tickInterval = 0.5f;
    float tickPhase = 0.0f;     // 运行时：相位对齐
    float radius = 48.0f;       // 持续伤害区半径（probe 8 = 目标体近似另加，见 HitboxSystem）
};

struct Collectible {
    uint8_t kind = 0;           // 0 gem / 1 coin / 2 heart
    uint8_t state = 0;          // 运行时：0 地面 / 1 磁吸中（PickupSystem 段 C）
    uint8_t _pad[2] = {};
    float magnetRadius = 48.0f; // 磁吸触程（与收集者 Stats.pickupRadius 取大——M5 批① D1）
    float magnetSpeed = 320.0f; // 磁吸飞行速度（px/s，直写 pos 不经 Velocity）
    float value = 1.0f;         // gem→XP / coin→gold（取整）/ heart→治疗量
    Entity target{};            // 运行时：磁吸目标（收集者 = 持 XpProgress 实体）
};

struct Trigger2D {
    uint32_t triggerId = 0;
    uint8_t once = 0;
    uint8_t inside = 0;         // 运行时：上一帧是否有进入者（差分配对）
    uint8_t fired = 0;          // 运行时：once 触发器已触发标志
    uint8_t _pad = 0;
    float radius = 32.0f;       // 触发半径（OverlapCircle 语义）
};

struct Knockback {
    Vec2 impulse{};             // 当前击退速度（随 decay 衰减）
    float decay = 8.0f;
};

// ---- 布局冻结（M3 桥侧 blittable 前提：C# 镜像 struct 与此逐字节对齐，改动=破回放）----
static_assert(std::is_trivially_copyable_v<Health> && sizeof(Health) == 16, "Health 布局冻结");
static_assert(std::is_trivially_copyable_v<Mover> && sizeof(Mover) == 4, "Mover 布局冻结");
static_assert(std::is_trivially_copyable_v<Patrol> && sizeof(Patrol) == 24, "Patrol 布局冻结");
static_assert(std::is_trivially_copyable_v<Chase> && sizeof(Chase) == 24, "Chase 布局冻结");
static_assert(std::is_trivially_copyable_v<Flee> && sizeof(Flee) == 8, "Flee 布局冻结");
static_assert(std::is_trivially_copyable_v<Shooter> && sizeof(Shooter) == 32, "Shooter 布局冻结");
static_assert(std::is_trivially_copyable_v<Projectile> && sizeof(Projectile) == 48, "Projectile 布局冻结");
static_assert(std::is_trivially_copyable_v<Spawner> && sizeof(Spawner) == 28, "Spawner 布局冻结");
static_assert(std::is_trivially_copyable_v<Hazard> && sizeof(Hazard) == 16, "Hazard 布局冻结");
static_assert(std::is_trivially_copyable_v<Collectible> && sizeof(Collectible) == 24, "Collectible 布局冻结");
static_assert(std::is_trivially_copyable_v<Trigger2D> && sizeof(Trigger2D) == 12, "Trigger2D 布局冻结");
static_assert(std::is_trivially_copyable_v<Knockback> && sizeof(Knockback) == 12, "Knockback 布局冻结");

} // namespace lemon::ecs
