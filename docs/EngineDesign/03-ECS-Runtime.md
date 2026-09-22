# Lemon 引擎设计 — 03 ECS 运行时

> 运行时内核的心脏：实体/组件/系统、热路径玩法原语、空间分区、物理查询层、寻路、导演与事件。
> 组件目录直接采纳 MoteurJV 编辑器验证过的"行为即组件"切分（Spawner/Chase/Shooter/Projectile…），这正是割草品类的预制组件面。
> 命名约定（ADR-009）：内核叙述用"实体/Entity"；C# 门面与编辑器 UI 层以 **GameObject** 为正名（见 04 §3）——同一 Handle 的两层措辞。

---

## 1. 为什么是 EnTT（而不是自研/flecs）

| 候选 | 结论 | 理由 |
|---|---|---|
| **EnTT（选）** | ✅ | sparse-set 存储、`view<Ts...>`/`group` 达到多组件 O(n)；C++20；无依赖单头；海量实体（百万级）社区验证充分 |
| 自研 | ❌ | MoteurJV Registry 是教学级（多组件 `view` 为 O(n·m) 逐实体 has() 过滤），不可用；自研到 EnTT 水平是数月级工程 |
| flecs | ❌ | C 风格 API + 自带实体关系/meta 体系，与"薄内核 + C# 门面"路线冲突；EnTT 与 STL/JobSystem 组合更自然 |

**封装原则**：`Engine/ECS/` 对 EnTT 做薄封装（`World`、`EntityHandle {uint64_t}`、组件注册表），业务代码不直接 include EnTT 头（保留将来替换/裁剪自由，同 Vulkan 零泄漏纪律）。

## 2. World / Scene / 实体生命周期

```
World（进程级唯一，持资产库/JobSystem/桥等全局服务）
 └─ Scene（一个游戏状态 = 一个 Scene：MainMenu / Level_01 / ...）
     ├─ 实体池（EnTT registry + lemon 组件注册表）
     ├─ 系统管线（FixedTick 阶段序列）
     ├─ 空间哈希 broadphase（每 Scene 一份）
     └─ 导演（Director，可多个）
```

- **实体销毁两阶段**：`Destroy()` 只入销毁队列，`Essential` 阶段统一提交（系统遍历中安全销毁；与触发器/事件队列一致性）。
- **Prefab（PrefabLink + PropertyOverride 模型，Prowl2D 已验证方案，ADR-009）**：资产化的实体模板。实例**只存一个链接 + 覆盖列表**（非全量拷贝）：
  - 链接数据挂实例根：`PrefabLink { uint64_t prefabId; SmallVector<PropertyOverride, 8> overrides; }`（非实例实体零开销）；支持嵌套 prefab（模板本身可以是另一实例）；编辑期 Apply / Revert / Break + Inspector 逐字段 override 高亮（见 05 §5）。
  - **运行时物化零开销**：`Instantiate(prefabId)`（走池，§10）时一次性合成"模板基础值 + 覆盖值"为平坦组件数据，物化后热路径与普通实体无差别；`Meta.prefabId` 保留回链供编辑器往返与重新 Apply。
  - 编辑器拖入 = 实例化 Prefab（Editor-RPG2D 的 GameObject/PlacedGameObject 分离思想 + Unity Prefab 心智）。
- **父子层级**：`Hierarchy {parent, firstChild, nextSibling, prevSibling}` 组件 + 层级系统（渲染 transform 提取时合成世界矩阵；2D 仿射 mat3x2 连乘，深度 ≤ 8 层防环）。

## 3. 组件目录（v1 全量清单）

分组原则：**Core**（引擎骨架，永远存在）｜**Render**（渲染提取消费）｜**Behavior**（摆放即玩法，MoteurJV 验证过的无代码玩法面）｜**Gameplay**（RPG 数值面）。全部纯数据（POD/小 vector），行为在系统里；C# 脚本组件除外（见 04）。

### 3.1 Core

```cpp
struct Transform2D { Vec2 pos; float rot; Vec2 scale; };                 // 世界坐标 = 父链合成
struct Hierarchy    { Entity parent, firstChild, next, prev; };
struct Meta         { uint64_t prefabId; uint32_t team; uint16_t layer; char tag[24]; }; // team→Team 表
struct DestroyQueueTag {};                                               // 帧末统一回收
```

### 3.2 Render

```cpp
struct SpriteRenderer { uint32_t spriteId; uint32_t colorRGBA; int16_t sortOrder; uint8_t sortingLayer; uint8_t flags; }; // flags: flipX/Y, enabled
struct Animator2D     { uint32_t clipId; float time; float speed; uint8_t loop; uint8_t playOnStart; uint16_t curFrame; }; // 帧动画驱动 SpriteRenderer.spriteId
struct ParticleEmitterRef { uint32_t emitterId; uint8_t playing:1, /*...*/; };
struct SortingOverride { int16_t order; };                               // 运行时覆盖（血条永远压怪物）
```

### 3.3 Behavior（无代码玩法面 —— 编辑器摆放即生效）

```cpp
struct Health        { float max, cur; float iFrames; float iframeWindow; }; // 受击无敌（iFrames 运行时，StatSystem 递减；窗默认 0.1s → 复拍间隔 ceil(窗/dt) tick）
struct Mover         { float speed; };                                   // 匀速朝向 velocity
struct Patrol        { Vec2 a, b; float pauseTime; };                    // 往返巡逻
struct Chase         { float speed; float aggroRange; float keepRange; uint32_t targetTeam; };
struct Flee          { float speed; float range; };
struct Shooter       { uint32_t projectileId; float interval; float range; uint32_t targetTeam; };
struct Projectile    { float speed; float lifetime; float damage; float hitRadius; float knockback; uint8_t pierce; uint8_t homing; uint16_t hits; uint32_t hitMemory[4]; }; // M5批⓪：命中半径/击退强度落弹体；hitMemory=一弹一目标一次（运行时 4 槽环）
struct Spawner       { uint32_t prefabId; float interval; uint16_t burst; float range; uint32_t maxAlive; uint32_t spawnTeam; }; // 刷怪口（prefabId = prefab 资产 GUID 低 32 位，M5 清障②约定；编辑器 EnterPlay 建映射并注册 SpawnFn，M7 烘焙换 dense id 表同语义）
struct Hazard        { float dps; float tickInterval; float radius; };   // 持续伤害区（毒泽/激光）；独立 tick 节拍，不与 iFrames 联动（M5 批⓪ 决策）
struct Collectible   { uint8_t kind; /*gem/coin/heart*/ float magnetRadius; };
struct Trigger2D     { uint32_t triggerId; bool once; };                 // 进入/离开事件
struct Knockback     { float decay; };                                   // 受击退（割草手感核心）
```

### 3.4 Gameplay（RPG/数值面，ARPG 与增量共用）

```cpp
struct Stats         { float moveSpeed, attack, defense, critRate, critDmg, pickupRadius, luck; };  // 乘区在系统侧算
struct StatusEffects { SmallVec<StatusInst,4> active; };                 // buff/debuff（id/剩余时间/层数/来源）
struct Inventory     { SmallVec<ItemStack,16> items; uint32_t gold; };
struct Equipment     { uint32_t weaponId, armorId, relicIds[3]; };
struct XpProgress    { float xp, xpToNext; uint32_t level; };            // VS 曲线（幂函数资产化）
struct IncrementalState { double rate, multiplier; double cached; };     // 增量挂机（M6+）
```

> 组件增减走"组件注册表"（反射元数据：字段名/类型/范围/编辑器控件），Inspector 与序列化全部由注册表驱动（见 05 §5），**新增组件零编辑器代码**。

## 4. 系统管线（FixedTick 60Hz 执行序）

| # | 系统 | 读 | 写 | 并行 | 说明 |
|---|---|---|---|---|---|
| 1 | InputSnapshot | 输入队列 | — | — | 主线程采样→模拟线程消费 |
| 2 | DirectorSystem | 导演配置/事件 | Spawner 命令 | — | 波次/刷怪调度（§8） |
| 3 | SpawnSystem | 销毁队列/池 | Transform2D 等 | — | 池取用 + 事件入队 onSpawn |
| 4 | AISystem(Behavior) | Chase/Patrol/Flee/Shooter | Velocity | ✅ grain 256 | 纯函数逐实体 |
| 5 | NavigationSystem | Chase | Velocity | — | 采样 FlowField/避障（§7） |
| 6 | SeparationSystem | Transform2D/Meta.team | Velocity | ✅ | 同队分离力（软碰撞，割草不堆叠的关键） |
| 7 | MovementSystem | Velocity/Mover/Knockback | Transform2D | ✅ | 积分 + 地形碰撞钳制（查询层） |
| 8 | SpatialHashRebuild | Transform2D | 哈希 cell | ✅ 每 cell | 帧重建（§5）；静态层（地形）常驻 |
| 9 | HitboxSystem | 投射物/Hazard/技能盒 | Health/Projectile/事件 | — | Team 判定 → 命中记忆（一弹一目标一次）→ 伤害/置无敌窗/击退（弹体配置）/Hit/Death |
| 10 | TriggerSystem | Trigger2D + 哈希 | 事件 | — | 进入/离开配对（上一帧缓存差分） |
| 11 | StatSystem | Stats/StatusEffects/Xp/Health | Stats/事件 | — | buff 计时/iFrames 递减/升级结算 onLevelUp |
| 12 | AnimatorSystem | Animator2D | SpriteRenderer | ✅ | 帧推进（time+clip→spriteId） |
| 13 | ProjectileLifetime | Projectile | 销毁队列 | ✅ | 越界/超时/穿透耗尽回收 |
| 14 | CSharpBatchSystem | 各（只读 slice） | 命令缓冲 | — | C# `IForEachSystem` 块级回调（04 §5） |
| 15 | ScriptEventDispatch | 事件队列 | → C# | — | 帧末批量派发（04 §4） |
| 16 | DestroyCommit | 销毁队列 | 池归还 | — | Essential 阶段执行 |

顺序声明：系统在注册表里写 `After("Movement")`；编辑器性能面板显示实际序列与毫秒。

## 5. 空间哈希 Broadphase（物理查询层地基）

- 网格 cell = 64px（可配，自动按实体密度调粒度——yami `ScenePartitionManager.optimize()` 思想）；动态层每帧重建（ParallelFor 每 cell 独立），静态层（地形/建筑）增量维护。
- 查询 API（全部 O(cell 内数)）：

```cpp
struct QueryFilter { uint32_t teamMask; uint16_t layerMask; Entity exclude; };
OverlapCircle(center, radius, filter, cb)   // 弹幕命中/拾取磁吸/爆炸
OverlapBox(box, filter, cb)
Raycast2D(origin, dir, maxDist, filter) → {Entity, point, normal}
PointQuery(point, filter)
Neighbors(pos, radius, cb)                  // 分离力专用（迭代器形式，零分配）
```

- **确定性**：cell 内实体按 Entity id 稳定排序，命中顺序与帧率无关（回放/联机预留）。

## 6. 物理 = 查询层 + 运动学（无求解器，ADR-006）

| 能力 | 实现 | 品类对应 |
|---|---|---|
| 地形碰撞 | Tilemap 碰撞标志 → 合并静态矩形（duality 32×32 扇区思想）+ 查询层 `ResolveMove(pos, delta)` 滑墙钳制 | ARPG 走位、TD 怪路径边界 |
| 软碰撞 | SeparationSystem 同队分离力（boids 式，强度随密度衰减） | 万怪不堆叠的手感关键（VS 同款做法） |
| 击退 | Knockback 组件速度衰减 | 割草打击感 |
| 触发器 | Trigger2D + 差分配对 | 拾取/传送/剧情区 |
| 单向平台/重力跳跃 | **不做**（明确排除，原则 5） | — |

## 7. 寻路：FlowField 为主，A* 为辅

- **FlowField 流场**（塔防/ARPG 怪群主路径）：目标点/目标集合 → 一次 BFS 积分场 → 万怪各自采样方向向量 O(1)；场更新摊销（目标移动超阈值才重算，≤ 4 次/秒分帧）。网格分辨率 = Tilemap cell。
- **A***：单位级短路径（Boss 技能冲锋、寻物），预算每帧 ≤ 32 次调用、超出入队下帧。
- 导航网格由 Tilemap 碰撞层自动烘焙；无 Tilemap 场景用手动导航区域笔刷（编辑器工具）。

## 8. 刷怪导演（Director）

```cpp
struct DirectorConfig {                  // 资产化（JSON），波次表驱动
    struct Wave { float startTime; float rampMult; SmallVec<SpawnEntry,4> entries; };
    SpawnEntry { uint32_t prefabId; float weight; float budgetCost; };
    float budgetCurve;                   // 时间→强度曲线（VS 的"分钟数=危险度"）
    int32_t capAlive;                    // 同屏上限（压测红线保护）
};
```

- 导演每 tick 按 `budget(time)` 与场上存活数（按 Team 统计）向激活的 Spawner 派发配额；Spawner 负责出生点（环形 off-screen + 磁吸避墙）。
- 事件：`onWaveStart/onRagePhase`（狂暴阶段切换）供 C#/表现层订阅。
- **压测保护**：`capAlive` 达到即停止 spawn 并告警（引擎内建，不是用户纪律）。

## 9. Team 势力系统（照搬 yami `Data/teams.json` schema）

```json
{ "teams": [
    { "id": 0, "name": "player" }, { "id": 1, "name": "monsters" }, { "id": 2, "name": "neutral" },
    { "id": 3, "name": "playerBullets" }],
  "relations": { "0-1": "hostile", "1-3": "hostile", "0-2": "neutral", "1-1": "soft-collide", ... } }
```

- 关系类型：`hostile`（命中判定）/ `friendly` / `neutral` / `soft-collide`（只分离不伤害）/ `ghost`（互相穿透）。
- 一个 20 行的 JSON 表达塔防与幸存者"玩家队 vs 怪物海"全部敌我语义——**直接采用 yami 的 schema**（数据格式，非代码）。

## 10. 对象池（引擎内建纪律）

- 池对象：怪物实体组件包、投射物、粒子层、飘血数字、掉落物、音源实例。
- API：`Pool<T>::Acquire()/Release()`，帧末统一归还（销毁两阶段配合）；池满走"最老者淘汰"（怪海永不停摆）。
- **用户不可绕过**：`Instantiate/Destroy` 的公开 API 内部即池，文档不提供 new 路径（yami 的 GC 尖峰教训在引擎层面根治）。

## 11. 事件系统（帧内延迟派发，C++ 内 + 桥到 C#）

```cpp
enum class GameEvent : uint16_t { Spawn, Hit, Death, TriggerEnter, TriggerExit,
                                  WaveStart, LevelUp, Pickup, TimerFire, Custom /*用户自定义区*/ };
struct EventPacket { GameEvent type; Entity src, dst; float4 payload; uint64_t userArg; };
RingQueue<EventPacket> gEvents;          // 系统只入队，帧末 ScriptEventDispatch 批量派发
```

- C++ 内部系统也走同一队列（导演/成就/任务监听），保证语义一致与可录制。
- 自定义事件：用户在资产里注册 id + 参数 schema，编辑器指令/可视化事件树（M8 后评估）复用同一通道。

## 12. 确定性与回放（廉价内置）

- 输入快照 + seeded RNG（每系统独立子流）+ 固定步长 + 稳定排序 ⇒ **逐帧可重放**（调试利器：崩溃帧重放）。
- v1 只做"开发者重放"（录输入流到文件，`--replay` 启动参数回放），不做联机回放服务。

## 13. 存档与场景快照

- **场景文件**与**存档**同构（ECS 全量序列化器，组件注册表驱动），存档 = 场景快照 + 用户数据段（双通道接口见 06 §10）。
- 版本迁移：schema 带 `version`，逐版本迁移函数链（老档自动升级，借鉴 duality gzip 版本化思想，压缩用 gzip）。

## 14. 性能预算分解（压测 A 模拟侧 ≤ 10 ms）

| 系统 | 预算 | 手段 |
|---|---|---|
| 重建空间哈希（10k 怪） | 1.2 ms | ParallelFor per-cell |
| AI/Behavior + 流场采样 | 1.5 ms | 纯函数并行 |
| 分离力（邻居查询 10k×~8 邻） | 2.0 ms | 哈希迭代器零分配；密度上限衰减 |
| 移动积分 + 地形钳制 | 0.8 ms | 合批矩形查询 |
| 命中（50k 弹 × 候选~2） | 2.0 ms | 命中结果写事件队列，零分配 |
| 动画/回收/杂项 | 1.0 ms | 并行 |
| C# 批量系统 + 事件派发 | 1.0 ms | 块级回调（04 §4/5 预算内） |
| **合计** | **≤ 9.5 ms** | 余量 0.5 ms |
