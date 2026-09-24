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

- **实体销毁两阶段**：`Destroy()` 只入销毁队列，`Essential` 阶段统一提交（系统遍历中安全销毁；与触发器/事件队列一致性）。**脚本实体死亡豁免**（批④后修④，2026-09-24）：#10 命中系统 HP 归零时，带 `ScriptBox` 的实体**不自动销毁**——生死处置归脚本（VS 模板玩家死亡→对话框复活即此路径）；此前无条件销毁导致复活后读已毁实体连续报错、行为被异常隔离禁用。无脚本数据实体照旧清场。**销毁通知恰好一次（F-08.2）**：`ScriptBox` 头部实体级 `notified` 位去重（M6a 批⓪ 多槽化后从槽级 flags 升格——多槽实体按实体一次通知全部实例，不逐槽各发）。
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
struct Animator2D     { uint32_t clipId; float time; float speed; uint8_t loop; uint8_t playOnStart; uint16_t curFrame; }; // 帧动画驱动 spriteId（clipId=.clip 资产 GUID 低 32 位；M5 批③起，0/未命中=M2 纯计时）
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
struct Collectible   { uint8_t kind; /*gem/coin/heart*/ float magnetRadius; float magnetSpeed; float value; uint8_t state; Entity target; }; // M5批①：磁吸三参数 + RT 状态；触程 = max(magnetRadius, 收集者 Stats.pickupRadius)，收集者 = 持 XpProgress 实体；state/target 运行时
struct Trigger2D     { uint32_t triggerId; bool once; };                 // 进入/离开事件
struct Knockback     { float decay; };                                   // 受击退（割草手感核心）
// M5 批②：导演波次表（1260B；§8 修订形态）——16 波 × 每波 4 条目定长数组段
struct WaveEntry     { uint32_t prefabId; uint16_t count; float interval; float range; }; // 条目：prefab/总量/间隔(受 rampMult 加速)/出生环半径
struct WaveDef       { float startTime; float rampMult; uint8_t entryCount; WaveEntry entries[4]; }; // 波（表序=生效序，重叠=后波接管）
struct WaveDirector  { uint32_t spawnTeam; int32_t capAlive; uint8_t waveCount; WaveDef waves[16]; /* RT: time/waveIndex/cd[4]/spawned[4] */ }; // 挂此组件+Transform2D=出生中心；capAlive 同队闸门(30 tick 普查)
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
| 2 | DirectorSystem | WaveDirector 波次表 | WaveStart/Spawn 事件 + 出生（经 SpawnFn） | — | 波次调度（§8；M5 批②落地：直接出生通道，非 Spawner 配额） |
| 3 | SpawnSystem | 销毁队列/池 | Transform2D 等 | — | 池取用 + 事件入队 onSpawn |
| 4 | AISystem(Behavior) | Chase/Patrol/Flee/Shooter | Velocity | ✅ grain 256 | 纯函数逐实体 |
| 5 | NavigationSystem | Chase | Velocity | — | 采样 FlowField/避障（§7） |
| 6 | SeparationSystem | Transform2D/Meta.team | Velocity | ✅ | 同队分离力（软碰撞，割草不堆叠的关键） |
| 7 | MovementSystem | Velocity/Mover/Knockback | Transform2D | ✅ | 积分 + 地形碰撞钳制（查询层） |
| 8 | SpatialHashRebuild | Transform2D | 哈希 cell | ✅ 每 cell | 帧重建（§5）；静态层（地形）常驻 |
| 9 | PickupSystem | Collectible/XpProgress/Stats + 哈希 | Transform2D/XP/事件 | — | 磁吸（双侧取大触程）→ 直写 pos 飞行 → 触距 8px 入账（gem→XP/coin→gold/heart→治疗）+ Pickup 事件（M5 批①；不用 RNG） |
| 10 | HitboxSystem | 投射物/Hazard/技能盒 | Health/Projectile/事件 | — | Team 判定 → 命中记忆（一弹一目标一次）→ 伤害/置无敌窗/击退（弹体配置）/Hit/Death |
| 11 | TriggerSystem | Trigger2D + 哈希 | 事件 | — | 进入/离开配对（上一帧缓存差分） |
| 12 | StatSystem | Stats/StatusEffects/Xp/Health | Stats/事件 | — | buff 计时/iFrames 递减/升级结算 onLevelUp（XP 入账源 = #9） |
| 13 | AnimatorSystem | Animator2D + ClipTable | SpriteRenderer | ✅ | 帧推进：curFrame=min(n-1, time*fps) 纯函数 + 写 spriteId；playOnStart=0 冻结；loop0 钳末帧；无 clip=M2 旧算术逐位不变（M5 批③；帧映射语义见 §8.1） |
| 14 | ProjectileLifetime | Projectile | 销毁队列 | ✅ | 越界/超时/穿透耗尽回收 |
| 15 | CSharpBatchSystem | 各（只读 slice） | 命令缓冲 | — | C# `IForEachSystem` 块级回调（04 §5） |
| 16 | ScriptEventDispatch | 事件队列 | → C# | — | 帧末批量派发（04 §4） |
| 17 | DestroyCommit | 销毁队列 | 池归还 | — | Essential 阶段执行 |

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
- **查询侧两级加速（2026-09-24 方案 A 修订，实测依据 09 §6.10 Hazard 化行）**：
  Item 内联 team/layer 位（重建时快照自 Meta，利用对齐 padding 零膨胀）+ cell 级
  team 位图整格早退 + 查询方 `TeamTable::HostileMask()` 预过滤——万怪密团里同队
  候选拒绝路径由"每候选 2 跳 entt 随机访问"降为纯顺序数组读/整格跳过
  （Hitbox 36.4→1.08ms）。语义不变量：无 Meta 实体恒放行（不入位图）、越界
  team/layer 恒不命中、命中集合与回调序零漂移（金回放零重录的机制保证）。

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

> **M5 批②修订（2026-09-23）**：原案 `DirectorConfig` 独立资产 + "向激活 Spawner
> 派发配额"修订为**组件内联波次表 + 直接出生通道**——数据驱动落点 = WaveDirector
> 组件（§3.3；场景/prefab 档即数据源，Inspector 可编辑数组段即编辑面），导演直接
> 经 `World::GetSpawnFn()` 出生（与 SpawnSystem 平行的第二条刷怪通道；Spawner 保留
> 常驻环境刷怪语义，互不派发）。理由：数组段机制零新基建；配额派发需按 prefabId
> 跨实体匹配、语义绕；两通道各司其职，M6c TD 模板波次同消费导演。原 `budgetCurve`
> 由波表自身表达（波表 startTime/rampMult/count 即强度曲线）；独立 `.asset` 数据
> 通道随批③ clip 一并定，`onRagePhase`（狂暴相位）留后续波。波次表编辑器（并入
> M6a 批② 配置表 ADR，2026-09-24 重排）在组件数据上盖专业 UI（增删波/条目、
> prefab 拖拽）。

**状态机（顺序相位语义；决策见 M5.md §11.2 D3–D6）**：
- 每导演实体 `time += dt`（缩放 dt——timeScale=0 冻结波次）；`time ≥ waves[i].startTime`
  → 波生效：发 **WaveStart**、重置运行时；波内条目按 `interval/rampMult` 节拍出生
  （每条目每 tick 至多 1，capAlive 顶格持币待发）；
- **波重叠 = 后波接管**（同 tick 多波到期按表序生效，仅末波持运行时，前波余量废止）；
- **capAlive** 同队闸门：30 tick 普查 + 乐观自增（与 SpawnSystem.maxAlive 同款"约"
  语义；两闸门独立计数，组合过冲 ≤ 每 tick 出生和 × 普查周期）；
- RNG 子流 1（导演注册序；Spawn 的 2 独立）；多导演共享子流按池序消费（确定性）；
  出生点 = 导演实体 Transform + 条目 range 环（"环形 off-screen/避墙"归玩法层——
  模板把导演实体挂玩家跟随）；prefab 失败（工厂返 Null）即废止该条目不重试。

**事件**：`WaveStart`（payload `[0]`=波序号 0 起、`[1]`=本波计划总数 Σcount、`[2]`
=配置时刻；src=导演实体）＋每次出生发 `Spawn`（与 SpawnSystem 同口径）。空波
（Σcount=0）照发 WaveStart——纯宣告波是合法用法。C# `Events.Subscribe(GameEvent.
WaveStart)` 开箱即用（样例：TestScript.WaveBannerBehaviour → `Ui.Set` 波次横幅）。

`.scene` JSON 形态（数组段序列化；手改档即可作者，M6a 配置表落地前的过渡路径）：

```json
"WaveDirector": { "spawnTeam": 1, "capAlive": 10000, "waveCount": 3,
  "waves": [ { "startTime": 1.0, "rampMult": 1.0, "entryCount": 4,
      "e0prefab": 3735928559, "e0count": 999, "e0interval": 0.0167, "e0range": 550.0, ... } ] }
```

### 8.1 帧动画（Animator 帧映射，M5 批③）

**clip 表（`ECS/ClipTable.h`，World 持有）**：`clipId → {fps, loop, frames[](spriteId)}`。
clipId = `.clip` 资产 GUID 低 32 位（prefabId 同款映射约定）；编辑器 EnterPlay 一次性
建表（进 Play 时刻资产快照，Play 中改 .clip 不生效）；引擎测试/玩法层直接填。
格式与切片通道见 06 §2.2。

**帧映射语义（#13 AnimatorSystem）**：

- 帧号 = **纯函数** `curFrame = min(n-1, (u16)(time*fps))`——无逐帧累加状态机，
  回放确定性；curFrame/spriteId 逐帧重写幂等。
- `time += speed*dt`（缩放 dt：timeScale=0 冻结动画）；loop=1 回绕 `period=n/fps`
  （time 有界）；loop=0 **钳末帧**（time 钳 total——M2"非 loop 无界增长"随 clip 收口）；
  负 time 钳 0（负 speed 防御）。
- `playOnStart=0` = **暂停开关**（time/curFrame/spriteId 三态全冻结；M5 无 Play()
  API，Play/CrossFade 归 M6a 批①（2026-09-24 重排））。`Animator2D.loop` 为权威（实体上热调参）；
  clip.loop 仅档面默认。
- `SpriteRenderer` 可缺 = 纯计时推进；在场则 `sr.spriteId = frames[curFrame]`。
- **无 clip（clipId=0/表未命中）= M2 旧算术逐位保留**（time 推进 + 占位周期 1.0
  回绕）——既有场景/金档零漂移（M5.md §18 零重录前提）；RNG 零消费。

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
- payload 约定（M5 累积）：`WaveStart` `[0]`波序号 0 起/`[1]`计划总数/`[2]`配置时刻（src=导演实体）；`Pickup` `[0]`kind/`[1]`value/`[2][3]`拾取点 xy。
- 自定义事件：用户在资产里注册 id + 参数 schema，编辑器指令/可视化事件树（M8 后评估）复用同一通道。

## 12. 确定性与回放（廉价内置）

- 输入快照 + seeded RNG（每系统独立子流）+ 固定步长 + 稳定排序 ⇒ **逐帧可重放**（调试利器：崩溃帧重放）。
- v1 只做"开发者重放"（录输入流到文件，`--replay` 启动参数回放），不做联机回放服务。

## 13. 存档与场景快照

- **场景文件**与**存档**同构（ECS 全量序列化器，组件注册表驱动），存档 = 场景快照 + 用户数据段（双通道接口见 06 §10）。
- 版本迁移：schema 带 `version`，逐版本迁移函数链（老档自动升级，借鉴 duality gzip 版本化思想，压缩用 gzip）。**M6a 批⓪ 首例（v1→v2）**：`scripts[]` 多脚本格式——迁移 = 单数 `script` 对象包成单元素数组 + 版本号回写；`.prefab` 不走迁移链（高频 spawn 工厂零额外 pass），靠 ReadEntity 双读（复数优先/单数兼容）隐式升级、下次保存自然改写复数。
- **M5 批④ 落地注**：用户数据段已落地为 **World 级 `SaveChannel`**（`ECS/SaveChannel.h`，内存 KV key→bytes；C# `Lemon.Save` 写读，IO 归宿主钩子——编辑器 = `.lemon/saves/game.sav` 定长头二进制 + 原子写 + .bak；06 §10 口径修订见彼处）。**不入 StateHash**（用户数据非模拟态）。场景快照入档（整场景存档）M6+；Game RT UI 通道（`World::RtUi` 槽 + `World::Cards` 三选一卡片）同为 World 级非 ECS 通道（呈现层，不入哈希）——**本通道族新增机制一律走"World 持有 + vtable 尾追"，不动组件注册表**（批②勘误的哈希漂移教训）。

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
