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
    float iFrames = 0.0f;   // 受击后无敌剩余秒
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
    uint16_t hits = 0;          // 运行时：已命中数（穿透去重辅助）
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
};

struct Collectible {
    uint8_t kind = 0;           // 0 gem / 1 coin / 2 heart
    uint8_t _pad[3] = {};
    float magnetRadius = 48.0f;
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
static_assert(std::is_trivially_copyable_v<Health> && sizeof(Health) == 12, "Health 布局冻结");
static_assert(std::is_trivially_copyable_v<Mover> && sizeof(Mover) == 4, "Mover 布局冻结");
static_assert(std::is_trivially_copyable_v<Patrol> && sizeof(Patrol) == 24, "Patrol 布局冻结");
static_assert(std::is_trivially_copyable_v<Chase> && sizeof(Chase) == 24, "Chase 布局冻结");
static_assert(std::is_trivially_copyable_v<Flee> && sizeof(Flee) == 8, "Flee 布局冻结");
static_assert(std::is_trivially_copyable_v<Shooter> && sizeof(Shooter) == 32, "Shooter 布局冻结");
static_assert(std::is_trivially_copyable_v<Projectile> && sizeof(Projectile) == 20, "Projectile 布局冻结");
static_assert(std::is_trivially_copyable_v<Spawner> && sizeof(Spawner) == 28, "Spawner 布局冻结");
static_assert(std::is_trivially_copyable_v<Hazard> && sizeof(Hazard) == 12, "Hazard 布局冻结");
static_assert(std::is_trivially_copyable_v<Collectible> && sizeof(Collectible) == 8, "Collectible 布局冻结");
static_assert(std::is_trivially_copyable_v<Trigger2D> && sizeof(Trigger2D) == 12, "Trigger2D 布局冻结");
static_assert(std::is_trivially_copyable_v<Knockback> && sizeof(Knockback) == 12, "Knockback 布局冻结");

} // namespace lemon::ecs
