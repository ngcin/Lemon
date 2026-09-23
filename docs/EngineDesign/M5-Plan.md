# M5 实施计划 —— 玩法 + vs-survivor 模板（08 §2）

> 滚动计划：每批开工前分解到文件/行级，完工后本页勾销并回写 DevLog。
> **批⓪（战斗闭环）已于 2026-09-22 完成**（实测数字见 DevLog 同日条目；执行中的
> 两处计划勘误：Projectile 实际 48B 非 56B；bench-sim 旗标为 `--frames` 非 `--steps`，
> 二进制路径 `build/mac/Samples/bench-sim/lemon-bench-sim`）。**批① 已于 2026-09-22
> 完成**（§6–§10 分解 + 完工记录见 §10 末；执行勘误：Pickup 密核扫描实测 1.87ms
> 超预估 0.2~0.6ms，登记 09 §6.10 观察项不扩 scope；smoke-ui 飘忽为既有问题）。
> **批② 已于 2026-09-23 完成**（§11–§15 分解 + 完工记录见 §15 末）。批③–④ 待分解。
> 批② 执行勘误两处：① 金回放属**重录**口径非零重录——`ComputeStateHash` 对注册表
> 组件名无条件入哈希（schema 漂移绊线），**新增组件必致全帧哈希漂移**（行为零漂移
> 的旁证：bench-sim 终态与旧档逐项一致），金档换代 m5b0→m5b2（09 §7 已沉淀推论：
> 加字段可零重录、加组件必重录）；② 顺带修复 ScriptHost 事件派发期 g_world/g_scene
> 窗口缺口（此前仅 TickBatch 置位，事件回调内 Ui.Set 等 native 调用静默空转）。
> 关联：09 §6.10（bench-survivor 性能台账）、09 §7（确定性回放口径）、
> DevLog 2026-09-22（P0 iFrames 无递减 + M5 支撑度评审）。

## 0. 批次总览

| 批 | 主题 | 内容概要 | 状态 |
|---|---|---|---|
| ⓪ | 战斗闭环 | iFrames 递减（P0）＋ 命中记忆/穿透收口 ＋ 战斗参数组件化 ＋ bench-survivor 战斗化 | **✅ 完成 2026-09-22** |
| ① | 成长闭环 | Collectible 磁吸系统 ＋ Pickup 事件发射 ＋ XP 入账 ＋ Game RT UI 最小通道 ＋ timeScale | **✅ 完成 2026-09-22** |
| ② | 导演波次 | DirectorSystem 波次表（数据驱动）＋ WaveStart 事件 | **✅ 完成 2026-09-23** |
| ③ | 表现层 | clip .asset ＋ Animator 帧映射 ＋ 最小图集/切片（自 M6 提前）＋ 素材包第一批 | 待分解 |
| ④ | 收口 | 存档 ＋ HUD 完整版 ＋ 模板整合 ＋ Play 调参 ADR | 待分解 |

---

## 1. 批⓪：战斗闭环

### 1.1 三个问题（代码已逐行核对，2026-09-22）

| # | 问题 | 锚点 | 后果 |
|---|---|---|---|
| P0 | iFrames 置 0.1s 后**无任何递减** | `Systems.cpp:372`（判免疫）/`:374`（置窗）；全引擎仅此两处引用 | 受击一次即永久无敌，"多段伤害击杀"从未发生过（bench-sim 7200 帧 alive 恒 10002，DevLog 已证） |
| 1 | 命中去重集缺失（代码注释自认"M5"） | `Systems.cpp:401` | iFrames 修复后，穿透弹对同目标每 0.1s 可再伤一次——穿透语义不成立 |
| 2 | 战斗数值硬编码 | 命中半径 `6.0`（:364）/ 击退 `60.0`（:388）/ 无敌窗 `0.1f`（:374）/ Hazard 半径 `48.0`（:429） | 玩法层无法调参；模板/资产侧接不进来 |

顺带收口：`Projectile.hits` 字段（`BehaviorComponents.h:59`，注释"穿透去重辅助"）目前是死字段，本批启用。

### 1.2 验收判据（全部满足才勾销）

1. `ctest` 3/3 全绿，含新增单测 ≥4 例（§2 各 T 所列）；
2. bench-sim 7200 帧：怪进入**击杀—补充循环**（alive 周期回落、destroyed 显著高于投射物数）——修复前 alive 恒 10002、零怪死亡；
3. C# 布局探针全绿（镜像同步的机械护栏，见 T2）；
4. bench-sim 与 bench-script 金回放**重录**后 `--replay` 一致（09 §7；行为变更属有意，重录即合规）；
5. bench-survivor 播种升级为含玩家弹幕后，判据不变仍 PASS（alive≥10000 且 frameAvg≤22.2ms）且 kills>0；
6. `tools/editor-regression.sh full` 全绿；
7. 回写：09 §6.10 加"战斗化"基线行、08 §3 表注、03 组件字段表、DevLog 批⓪条目。

---

## 2. 任务分解（T1→T5 依序落地）

### T1 iFrames 递减（P0 修复）＋ Health.iframeWindow —— 约 0.5 天

**组件**（`Engine/Components/BehaviorComponents.h`）：Health 追加尾部字段
`float iframeWindow = 0.1f;`（受击无敌窗秒数）→ 12B→16B，static_assert 同步。
**追加尾部的原因**：现存 10 处 `Health{a, b, c}` 聚合构造（Editor 2 / bench-sim 3 / tests 5）按位初始化，尾部追加使它们**默认值正确**；仍全部改 designated initializers（见 T2 清单纪律）。

**递减落点**（`Engine/Systems/Systems.cpp` StatSystem，#11）：仿 StatusEffects 倒计时段，加一段
`View<Health>`：`if (hp.iFrames > 0) hp.iFrames = max(0, hp.iFrames - dt);`（串行遍历，逐实体独立=确定性；1 万实体 ≪ 0.1ms）。

**置窗**（`:374`）：`hp->iFrames = 0.1f` → `hp->iFrames = hp->iframeWindow;`

**时序语义（写进代码注释 + 03 文档）**：Hitbox(#9) 同 tick 置窗在先、Stat(#11) 递减在后 →
复击间隔 = `ceil(iframeWindow/dt)` tick（60Hz、0.1s → **恰 6 tick**）。

**目录/镜像**：`ComponentCatalog.cpp` kHealth 加 `FIELD(Health, iframeWindow, Float)`＋`ED_RANGE(0, 2)`；`Components.cs` Health 镜像加 `IFrameWindow`；`LayoutTables.cs` 加探针行（1:1）。

**测试**（`Tests/engine_tests.cpp`）：
- 扩 Hitbox 用例（~:1150）：dmg 12 vs hp 30 → 推 3×6 tick → 恰一次 Death、`hits==3`；
- 扩序列化用例（~:1021）：iframeWindow roundtrip；iFrames 仍 RT 不入档。

### T2 Projectile 重布局：命中记忆 ＋ hitRadius/knockback 组件化 —— 约 1 天

**新布局**（20B→48B，尾部追加，理由同上；3 处 `Projectile{...}` 构造点默认值正确）：

```cpp
struct Projectile {
    float speed = 300.0f, lifetime = 3.0f, damage = 10.0f;
    float age = 0.0f;                       // RT
    uint8_t pierce = 0, homing = 0;
    uint16_t hits = 0;                      // RT（本批启用：累计命中数）
    // ---- 批⓪ 追加 ----
    float hitRadius = 12.0f;                // 有效判定半径全量（执行期勘误：SpatialHash
                                            // reach = radius + probe，原 6+6 有效口径即
                                            // 12px——hitRadius 定为全量、probe=0，零漂移）
    float knockback = 60.0f;                // 击退脉冲强度（原 :388 硬编码）
    uint8_t hitHead = 0; uint8_t _pad2[3];  // RT：命中记忆环写指针
    uint32_t hitMemory[4] = {};             // RT：最近 4 个命中目标（一弹一目标一次）
};
```

**命中记忆语义（决策）**：**一弹一目标一次（弹 lifetime 内）**——穿透弹不烧 pierce 反复伤同一目标；iFrames 降级为**跨弹** rate limit。槽存 Entity 低 32 位（entt 句柄自带 version，回收不误判）；4 槽 LRU 环，超出即覆写（10k 怪穿透弹场景足够，零堆分配）。

**HitboxSystem 投射物段改法**（`:356-415`）：
- 查询半径 `6.0f, 6.0f` → `pr.hitRadius, pr.hitRadius`；
- 回调判定序：Meta/Health 存在性 → `cur<=0` → **hitMemory 命中过（新）** → `iFrames>0` → 伤害/置窗/`RememberHit`/`++pr.hits`/击退（`60.0f`→`pr.knockback`）/Death → pierce 判定**不变**。

**目录/镜像**：kProjectile 加 `hitRadius`/`knockback`（FIELD＋ED_RANGE）、`hitHead`＋`hitMem0..3`（**FIELD_RT＋ED_HIDE**，运行时态不入档不进 Inspector）；`Components.cs`＋`LayoutTables.cs` 同步——**布局探针测试（C# 实测 offset 对照注册表）是镜像同步的机械护栏，改漏必红**。

**构造点清单**（改 designated initializers，防未来插字段静默错位）：
`bench-sim/main.cpp:53,71,107,56`、`bench-script/main.cpp:135`、`EditorApp.cpp:101,114`、`engine_tests.cpp:617,831,930,1021,1174,834`。

**测试**：
- 命中记忆：pierce=3 慢弹（speed 10）＋单怪同位重叠 60 tick → `hits==1`、怪只掉一次血；
- 穿透耗尽：pierce=1＋两怪同线 → 双伤、弹毁、`hits==2`；
- 序列化：hitRadius/knockback roundtrip；hitHead/hitMem* 不入档。

### T3 Hazard.radius ＋ 语义决策 —— 约 0.25 天

- Hazard 追加 `float radius = 48.0f;`（12B→16B）；`:429` 的 `48.0f` → `hz.radius`（probe 8.0 常量保留）；
- 目录/镜像/构造点同 T2 模式；
- **语义决策（记入 03 文档）**：Hazard 走自身 `tickInterval` 节拍，**不与 iFrames 联动**（现状即正确：区域伤害独立成拍，互不挤占无敌窗）；若后续玩法要联动，届时加 Hazard 侧字段。

### T4 bench-survivor 战斗化 ＋ 基线刷新 —— 约 0.5 天

现播种（`EditorApp.cpp:87-128` SeedBenchSurvivorScene）**没有任何弹幕源**——纯移动压测，战斗闭环从未在该场被测过。补：
- 弹体 prefab：sprite＋`Projectile{.damage=12, .speed=320, .lifetime=2.5}`（pierce 0=命中即毁）＋Velocity；
- 玩家挂 `Shooter{.projectileId=弹体guid低32位, .interval=0.05, .range=2000, .targetTeam=1}`。

跑 `--bench-survivor --frames 900` 重取基线（判据不变）：预期 sim 段 +0.5~1.5ms（Hitbox 非零、击退入 Movement、死亡/补充 churn）；kills>0 必现。**若 <45fps：profile 分解、登记 09 §6.10 观察项，不在本批扩 scope 优化。** 09 §6.10 加"战斗化"基线行、08 §3 表注。

### T5 文档收口 —— 约 0.25 天

- `03-ECS.md` 组件字段表（:62 Health / :68 Projectile）与 §4 系统说明同步（iFrames 时序、命中记忆、Hazard 语义）；
- DevLog 批⓪条目（修复前后 bench 数字对照）；
- 本页勾销批⓪。

**合计约 2.5 个工作日。**

---

## 3. 确定性与回放影响

- 新逻辑全部**逐实体确定性**：无 RNG、无邻居遍历序变化（Separation/哈希重建/TargetBoard 未动）；
- 金回放差异**仅来自战斗结局本身**（怪会死、位置会散）——属有意行为变更，按 09 §7 重录即合规；
- 重录节奏：T1 落地先录一版（P0 止血点可独立回滚），T2/T3 落地后终录（布局与参数默认同批生效）。

## 4. 风险与对策

| 风险 | 对策 |
|---|---|
| 尾部追加≠免检：构造点若被手改按位填满仍会静默错位 | T2 清单内 13 处全部改 designated initializers；新增构造一律 designated |
| 布局冻结连锁（3 组件改尺寸=破旧回放兼容） | 本批有意；static_assert＋C# 探针双护栏；金回放重录覆盖 |
| bench-survivor 战斗化后 fps 回落越线 | 先测再判；越线则登记观察项，优化与批⓪解耦 |
| editor-regression script-chain `--validate` 若含跨版本金档 | 实施时核对语义，同批重录 |
| hitMemory 4 槽不足（极端穿透场景重伤同批目标） | LRU 覆写=最多多伤一次，非正确性缺口；不足时升 8 槽（+16B）一行改动 |

## 5. 验证命令（批⓪完工口径）

```bash
ctest --test-dir build/mac --output-on-failure                        # 单测＋布局探针＋脚本测试
build/mac/Samples/bench-sim/lemon-bench-sim --frames 3600 --stats     # 击杀回流（alive 不再恒定）
build/mac/Samples/bench-sim/lemon-bench-sim --frames 3600 --threads 1 --stats   # 单线程诊断档
build/mac/Samples/bench-sim/lemon-bench-sim --frames 3600 --record build/goldens/m5b0-sim-mt.txt
build/mac/Samples/bench-sim/lemon-bench-sim --frames 3600 --replay build/goldens/m5b0-sim-mt.txt  # 重录后必一致
build/mac/Samples/bench-sim/lemon-bench-sim --frames 3600 --threads 1 --record build/goldens/m5b0-sim-st.txt
build/mac/Samples/bench-sim/lemon-bench-sim --frames 3600 --threads 1 --replay build/goldens/m5b0-sim-st.txt
build/mac/Samples/bench-script/lemon-bench-script --frames 1800 --record build/goldens/m5b0-script.txt
build/mac/Samples/bench-script/lemon-bench-script --frames 1800 --replay build/goldens/m5b0-script.txt
build/mac/Editor/lemon-editor --bench-survivor --frames 900           # 战斗化基线 ≥45fps
tools/editor-regression.sh full build/mac                             # 编辑器全回归
```

**完工记录（2026-09-22）**：全部判据满足——ctest 3/3（13049 检查，新增 4 类用例）、
bench-sim 击杀回流（alive 10002 恒 → 9682，~25 kills/s，mt 5.14~5.34ms）、双档金回放
mismatches=0 且终态逐项一致、bench-survivor 战斗化三跑 86/87/86 fps PASS、
editor-regression 11/11。细节与数字见 DevLog 2026-09-22 批⓪条目。

---

## 6. 批①：成长闭环 —— 现状盘点与设计决策（代码逐行核对，2026-09-22）

### 6.1 现状：管线断点在哪

| 环节 | 现状 | 锚点 |
|---|---|---|
| Collectible 组件 | 只有 kind/magnetRadius 两字段——**无价值量、无飞行速度、无磁吸状态**，全引擎无任何系统消费它（死组件） | `BehaviorComponents.h:99`（8B 冻结） |
| GameEvent::Pickup | 枚举已备（C++/C# 两侧均已镜像），**从未被发射** | `Events.h:20` / `Components.cs:271` |
| XpProgress + 升级环 | StatSystem 已有完整 while 升级环 + LevelUp 事件（幂曲线 xpCurveK）——**但没有任何入账源**（xp 无人写入） | `Systems.cpp:533-549` |
| Stats.pickupRadius | 字段存在（默认 32），无人读取 | `GameplayComponents.h:18` |
| timeScale | 01 §2 已规划（"作用于模拟步进，不影响渲染插值"）；Time.cs 留注释"随 M5 导演批次接 C++ 步进"；World::Step 无缩放 | `Time.cs:6` / `World.cpp:33` |
| Game RT UI | 无任何通道（GameView Play 时只有一行输入提示文字） | `ViewportPanels.cpp:745` |

### 6.2 设计决策（六条，实施时写进代码注释与文档）

**D1 磁吸触程 = 双侧取大**：`effectiveR = max(Collectible.magnetRadius, 收集者 Stats.pickupRadius)`。
宝石自带吸程（heart 可配大吸程）OR 玩家磁力覆盖（VS 磁铁升级走 Stats.pickupRadius），
任一满足即吸。实现分两段查询（见 D2），两源各一段，语义无歧义。

**D2 收集者约定 = 持 XpProgress 的实体**（VS 心智：唯玩家拾取；ARPG 多角色各持
XpProgress 各自拾取，天然成立）。**不用 RNG、不占子流**（磁吸全确定性；SpawnSystem
的子流 id=2 硬编码不受管线插位影响——InstallDefaultSystems 注释声明）。管线插位：
**SpatialHashRebuild(#8) 之后、Hitbox(原#9) 之前**，新系统 PickupSystem 成表序 #9，
后继 9–16 顺移为 10–17（16→17 系统；`Systems.cpp:622` 注册表 + 03 §4 表同步）。
理由：读当帧新哈希；磁吸**直写 pos** 不经 Velocity（无 Movement 竞争、宝石不必挂
Velocity——分离力/移动系统的 Velocity 守卫天然跳过无速实体）；拾取销毁走两阶段，
与战斗销毁同一提交点（DestroyCommit Essential，断言前多 Step 一拍的批⓪经验）。

**D3 三段逻辑**（PickupSystem 内 A→B→C 固定序）：
- **A 收集者广播**：`View<XpProgress, Transform2D>`，r = Stats.pickupRadius（无 Stats
  跳过）；OverlapCircle → 命中 Collectible 且 state==0 → state=1、target=收集者；
- **B 宝石自检**（补 A 覆盖不到的"宝石吸程 > 玩家磁力"侧）：state==0 的宝石以自身
  magnetRadius 查询 → 首个持 XpProgress 者（排除自身）→ state=1、target=…；
- **C 飞行+拾取**：state==1 → 目标失活（!Alive/无 Transform）→ **回落 state=0**（宝石
  不丢）；dist ≤ kPickupTouch（**8px 常量**，对齐战斗侧"触即判定"口径，暂不组件化）→
  拾取入账 + Pickup 事件 + Destroy；否则 `pos += dir * min(magnetSpeed*dt, dist)`
  （min 钳制防过冲穿越）。同 tick A/B 双磁吸（两收集者）= 池序后写胜出——确定性但
  任意，文档声明。

**D4 入账按 kind 分发**（引擎只入账，表现归 C#/模板）：gem → `XpProgress.xp += value`
（同 tick 由 StatSystem 升级环消费 → LevelUp 事件，幂曲线不动、资产化留后批）；
coin → `Inventory.gold += (uint32)value`（有 Inventory 才入）；heart → `Health.cur =
min(max, cur+value)`（有 Health 才入）。Pickup 事件 payload 约定：`[0]=kind、[1]=value、
[2][3]=拾取点 xy`（VFX 用）；src=宝石、dst=收集者。**C# 侧零改动即可订阅**
（Events.Subscribe(GameEvent.Pickup)，枚举已镜像）。

**D5 timeScale 落 World::Step**：`dt = fixedDt * timeScale_`，全 FixedTick 系统吃缩放
dt → `timeScale=0` = 全冻结暂停（tick 照推、RNG 不消耗、iFrames 不减——回放安全）。
setter clamp [0,8]（负值→0）。C# `Time.Scale` 经 NativeApiVtable **表尾追加**
`get/setTimeScale`（M4.4 追加模式，旧宿主 null 判空零扰动）；`Time.DeltaTime`
自动为缩放值（TickBatch 收到的即缩放后 dt）。确定性：C# 侧 setTimeScale 由事件驱动
= 回放内确定性；编辑器侧改动属调试操作不入输入快照（金回放场景不用 timeScale）。

**D6 Game RT UI = World 级定长槽通道**（非编辑器私产——打包游戏 M8 的 HUD 走同通道）：
`struct RtUiSlot { char key[16]; char text[48]; float frac; }` ×8 定长 + Set（命中覆写/
空槽即占，text 截断 47 字符）/Clear；**不入 StateHash**（StateHash 只哈希 Scene，
已核实 `StateHash.h:15`）；EnterPlay 新建 world 自清零。C# `Ui.Set(key, text, frac)`
走 vtable 尾追 `rtUiSet`；编辑器 GameView Play 时左上角 ImGui 叠加（text + frac≥0
时 ProgressBar 120×8）。frac 语义 = 0~1 进度条（XP 条/血条），-1 = 纯文本。

### 6.3 验收判据（全部满足才勾销）

1. `ctest` 3/3 全绿，含新增引擎测试 ≥6 例（§7 各 T 所列）+ script-tests 扩展；
2. **金回放零重录**：bench-sim/bench-script 用**批⓪旧档**（m5b0-*）直接 `--replay`
   mismatch=0——本批对现有基准场景行为零漂移的机械证明（新系统在无宝石场景空转）；
3. C# 布局探针全绿（Collectible 8→24B 镜像同步）；
4. bench-survivor 成长化后判据不变仍 PASS（alive≥10000 且 frameAvg≤22.2ms）；
5. `tools/editor-regression.sh full` 全绿；
6. 回写：03（组件/管线表/拾取语义）、01（timeScale 落地注）、04（Time/NativeApi）、
   05（GameView 叠加）、09（§6.10 基线行 + §7 零重录注记）、DevLog 批①条目。

---

## 7. 任务分解（T1→T6 依序落地）

### T1 Collectible 重布局 8B→24B —— 约 0.5 天

**组件**（`Engine/Components/BehaviorComponents.h:99`，尾部追加；全库唯一构造点
`EditorApp.cpp:3111` 是无参 `Emplace` 默认构造——**零构造点风险**，但仍立 designated
纪律）：

```cpp
struct Collectible {
    uint8_t kind = 0;            // 0 gem / 1 coin / 2 heart
    uint8_t state = 0;           // 运行时：0 地面 / 1 磁吸中
    uint8_t _pad[2] = {};
    float magnetRadius = 48.0f;  // 磁吸触程（与收集者 Stats.pickupRadius 取大，D1）
    float magnetSpeed = 320.0f;  // 磁吸飞行速度（px/s）
    float value = 1.0f;          // gem→XP / coin→gold / heart→治疗量
    Entity target{};             // 运行时：磁吸目标（收集者）
};
```

static_assert 8→24。**目录/镜像**：kCollectible 加 `magnetSpeed`/`value`（FIELD＋
ED_RANGE(0,2048)/(0,4096)）、`state`/`target`（FIELD_RT＋ED_HIDE，仿批⓪ hitHead/
hitMemory 手工行模式，`ComponentCatalog.cpp:135`）；`Components.cs:171` 镜像 24B +
`LayoutTables.cs:192` 探针行 1:1——**布局探针是镜像同步机械护栏，改漏必红**。

### T2 PickupSystem（磁吸＋拾取＋XP 入账＋Pickup 事件）—— 约 1 天

**落点**：`Engine/Systems/Systems.h` 声明 + `Systems.cpp` 在 SpatialHashRebuild 与
HitboxSystem 段之间插入 `#9 PickupSystem` 段（A→B→C 三段逻辑见 §6.2 D2/D3/D4，
逐行注释决策依据）；`InstallDefaultSystems`（`Systems.cpp:632` 附近）注册序同表序，
注释声明"不用 RNG、子流零扰动"。

**成本预估**：段B 每地面宝石每 tick 一查（半径 48 ≈ 1~2 cell）；2000 宝石 ≈
0.1~0.3ms——bench-survivor 成长化实测覆盖（T5）；万级若超预算登记 09 观察项，
不在本批扩 scope。

**测试**（`Tests/engine_tests.cpp` 新增，注册序在 TestNoDoubleDeathEvents 前）：
- 磁吸收敛：宝石 30px 处（吸程内、触距外）→ 逐 tick 位移朝收集者、 eventually 拾取
  （Destroy 前多 Step 一拍——批⓪ Essential 时序经验）＋ `XpProgress.xp == value` ＋
  EventSink 收到 Pickup（payload kind/value/xy 断言）；
- 双侧取大：宝石 100px ＋ gem.magnetRadius=48：收集者 pickupRadius=120 → 吸（段A）；
  pickupRadius=32 → 不吸（位移为零断言）；
- 三 kind 入账：heart 治疗（cur 上限钳制）、coin 入 Inventory.gold、gem 入 xp；
- 升级联动：xpToNext=5、value=10 → 同 tick level=2、余 xp=5、LevelUp 事件恰一次；
- 目标死亡回落：磁吸中销毁收集者 → 宝石 state 回 0、位置冻结；
- 归档：magnetRadius/magnetSpeed/value roundtrip；state/target RT 不入档（扩
  TestArchiveArraySegAndRuntimeFields 模式）。

### T3 timeScale（World 步进缩放 ＋ C# Time.Scale）—— 约 0.5 天

- `World.h/World.cpp`：`float timeScale_ = 1.0f` ＋ `SetTimeScale`（clamp [0,8]）/
  `TimeScale()`；`Step`（`World.cpp:33`）内 `const float dt = fixedDt * timeScale_;`
  两 RunStage 均传缩放 dt（DestroyCommit 不消费 dt，无语义差）；
- C# 通路：`ScriptHost.h:36` NativeApiVtable **表尾追加** `getTimescale`/`setTimeScale`
  （实现落在 `ScriptHost.cpp:95` kNativeApi 表 + Native* 函数，操作当前 tick 的 World）；
  `NativeApi.cs` 镜像同步 + `Time.cs` 加 `Scale { get; set; }`（:6 注释"随导演批次"
  改为已落地）；核对 TickBatch→Time.Advance 传参链吃的是缩放 dt；
- **测试**：engine_tests——timeScale=0 冻结（位置不变、TickIndex 照推）、0.5 半速
  （同 tick 数位移对半）；script-tests——Time.Scale 往返 get/set。

### T4 Game RT UI 最小通道（World.RtUi ＋ C# Ui.Set ＋ GameView 叠加）—— 约 0.75 天

- `World.h`：RtUiSlot×8 + `RtUiChannel`（Set/Clear/遍历）+ World 持有（§6.2 D6 布局）；
- vtable 尾追 `rtUiSet(const char* key, const char* text, float frac)`；SDK 侧
  `NativeApi.cs` 镜像 + 新 `Ui.cs`（`Lemon.Ui.Set(key, text, frac=-1)`）；
- `ViewportPanels.cpp:745` 现有 Playing 提示块之后：Playing() → 遍历 `Ctx().PlayWorld()`
  RtUi 槽 → 图像矩形内左上角 TextUnformatted + frac≥0 时 `ProgressBar(frac, 120×8)`；
- **测试**：script-tests——C# `Ui.Set("xp","LV 3",0.5)` 后 C++ 侧读 World 槽断言
  （key/text/frac）+ EnterPlay 后槽清零；
- **用途闭环**：TestScript 加订阅样例（Pickup/LevelUp → Ui.Set XP 行）——成长闭环
  在 Game 面板肉眼可见（零 UI 代码门槛的最小验证，M8 完整 HUD 前的过渡形态）。

### T5 bench-survivor 成长化 ＋ 冒烟可看 —— 约 0.5 天

- `SeedBenchSurvivorScene`（`EditorApp.cpp:87` 起）：玩家挂 `XpProgress{.xpToNext=…}`；
  宝石 prefab（sprite＋`Collectible{.kind=0,.value=1}`，中立队）＋ 玩家侧 Spawner
  （interval 0.2 / burst 8 / range 400 / maxAlive 2000）——地面宝石稳态 ~2000 循环
  出生→磁吸→拾取→销毁，PickupSystem 满载 + XP/LevelUp 事件流进基线口径；
- 预期 sim +0.2~0.6ms（现 13.1ms/76fps，判据余量 9ms）；若越线：profile 归因、
  登记 09 §6.10 观察项，不扩 scope（批⓪先例）；
- `SeedSmokeScene`（`EditorApp.cpp:3087`）：玩家补一行 `XpProgress`——手动 Play 即见
  3 个 Collectible 子实体磁吸入账（现播种即有 Collectible，缺的只是收集者标记）；
- 09 §6.10 加"成长化"基线行、08 §3 表注。

### T6 文档收口 —— 约 0.25 天

- `03-ECS.md`：§3.3 Collectible 行（:71）扩字段＋语义；§4 表插 PickupSystem 行、
  后继重编号＋注；拾取语义段（D1–D4）；
- `01-Architecture.md` :93 时间缩放行标注"M5 批① 已落地"；
- `04-CSharp-Scripting.md`：:190 Time.timeScale 注记更新；NativeApi 表"M5 批① 尾追
  3 项"（get/setTimeScale + rtUiSet）；
- `05-Editor.md`：GameView RT UI 叠加说明（Play 专属、通道引擎级）；
- `09-Testing.md`：§6.10 基线行；§7 加"批① 零重录注记"（新系统空转=哈希不变，
  旧档 replay mismatch=0 即证）；
- DevLog 批①条目；本页勾销。

**合计约 3.5 个工作日**（批⓪ 2.5 天量级＋C# 通道与编辑器 UI 面）。

---

## 8. 确定性与回放影响

- PickupSystem 无 RNG、无邻居遍历序变化（OverlapCircle 回调序 = cell 序，既有口径）；
- **现有基准场景（bench-sim/bench-script/bench-survivor 战斗化场）无宝石 → 新系统
  空转 → 状态哈希流不变 → 批⓪金档零重录继续有效**（判据 2 的 mismatch=0 即机械证明；
  T5 成长化只改 bench-survivor 播种，该场无金档，只刷新 fps 基线行）；
- timeScale 默认 1.0 → 既有路径零漂移；=0 时全系统 dt=0 冻结但 tick 照推、RNG 不
  消耗（录制/回放帧对齐保持）；
- Collectible 8→24B 属布局冻结变更：static_assert＋C# 探针双护栏；无旧档含 Collectible
  序列化数据（编辑器场景档按名键控、字段增补向后兼容——M2 既有语义）。

## 9. 风险与对策

| 风险 | 对策 |
|---|---|
| 段B 万级宝石每 tick 查询超预算 | 先测再判（T5 实测覆盖）；越线登记 09 观察项，降频/惰性自检属行为变更需单独批 |
| 同 tick 双收集者磁吸覆写（池序后写胜出） | 确定性但任意——代码注释+03 文档声明；多人同屏拾取公平性归玩法层（分宝石队/分区） |
| 磁吸中收集者销毁 → target 悬垂 | 段C 每帧 Alive+Transform 校验，失活回落 state=0（宝石不丢、可再吸） |
| RT UI text 48B 截断 / 8 槽写满 | Set 内 snprintf 截断到 47 字符；满槽忽略新 key（返回 false）——文档声明 |
| vtable 尾追破坏旧宿主兼容 | M4.4 既有模式：SDK 侧判空调用；script-tests 布局/探针全绿即证 |
| timeScale 极端值（负/巨幅） | setter clamp [0,8]；负值→0 暂停语义 |
| XpProgress 持有者=收集者的脚枪（怪也持 XP 偷吸宝石） | 约定写死 03：XpProgress 是"玩家侧成长条"，怪不掉 XP 进自己条（击杀奖励走事件层）——语义自洽 |

## 10. 验证命令（批①完工口径）

```bash
ctest --test-dir build/mac --output-on-failure                        # 单测＋布局探针＋脚本测试
# 金回放零重录证明（批⓪旧档直接 replay，mismatch 必须 = 0）
build/mac/Samples/bench-sim/lemon-bench-sim --frames 3600 --replay build/goldens/m5b0-sim-mt.txt
build/mac/Samples/bench-sim/lemon-bench-sim --frames 3600 --threads 1 --replay build/goldens/m5b0-sim-st.txt
build/mac/Samples/bench-script/lemon-bench-script --frames 1800 --replay build/goldens/m5b0-script.txt
build/mac/Editor/lemon-editor --bench-survivor --frames 900           # 成长化基线 ≥45fps
tools/editor-regression.sh full build/mac                             # 编辑器全回归
```

**完工记录（2026-09-22）**：全部判据满足——ctest 3/3（engine-tests 13098 +
script-tests 1340 检查，新增 7 类引擎用例 + TestTimeScaleAndUiChannel）、**金回放
零重录证明**（批⓪旧档三档 replay mismatches=0）、bench-survivor 成长化三跑
61/64/64 fps PASS（Pickup avg 1.87ms，密核扫描列 09 §6.10 观察项）、
editor-regression 11/11。过程发现：smoke-ui rename/scrub 子断言约 2/6 飘忽为既有
问题（基线 stash 复测证实），登记 09 §8。细节见 DevLog 2026-09-22 批①条目。

---

## 11. 批②：导演波次 —— 现状盘点与设计决策（代码逐行核对，2026-09-23）

### 11.1 现状：管线断点在哪

| 环节 | 现状 | 锚点 |
|---|---|---|
| DirectorSystem | 空占位（三连 `(void)` 转型），管线位 **#2 早已注册**、RNG 子流 1（注册序）闲置至今 | `Systems.cpp:84` / `Systems.h:53` / `Systems.cpp:726`（注册序=子流 id 注释：仅 Spawn 持 2） |
| WaveStart 事件 | 枚举 C++/C# 两侧**已镜像，从未被发射**（与批① Pickup 同款"备而未发"） | `Events.h:18` / `Components.cs:278` |
| 波次表数据通道 | 无。03 §8 原案 `DirectorConfig` 资产化未落地；**定长数组段机制已在**（StatusEffects/Inventory 用 ArraySegMeta 序列化+状态哈希），但 **Inspector 数组段只读**（`ImGui::Text` 表格，不可编辑=不可作者） | `ComponentRegistry.h:74` / `InspectorPanel.cpp:814` |
| SpawnFn 桥 | 编辑器 Play 已通（清障②：prefab guid 低 32 位 → 实例化 + 错绑告警）——导演可**直接消费，零桥接新代码** | `EditorApp.cpp` EnterPlay/`BuildPlayPrefabCache` |
| capAlive 压测保护 | 仅 `Spawner.maxAlive`（每 Spawner 自闸门，非导演全局）；03 §8 capAlive 未落地 | `BehaviorComponents.h:87` |
| bench-survivor | Spawner interval 0 / burst 64 / maxAlive 10000 拉满，无导演；波次从未在压测场跑过 | `EditorApp.cpp:123-129` |

**管线零重编号**：DirectorSystem 占位即正式槽位（批① 插 PickupSystem 曾 9–17 顺移，本批无此成本）；`TestSystemPipelineOrder`（`engine_tests.cpp:841`）断言 17 系统名序——**本批不动**。

### 11.2 设计决策（六条，实施时写进代码注释与文档）

**D1 数据驱动落点 = WaveDirector 组件 + 定长数组段（不新建 .asset 通道）**：
03 §8 原案 `DirectorConfig` 资产化（JSON）修订为**组件内联波次表**——16 波 × 每波
4 条目，走既有 ArraySegMeta（场景/prefab 档即数据源，`.scene` JSON 可读可 diff，
Inspector 即编辑面）。理由：数组段机制零新基建；独立数据资产通道与批③ clip `.asset`
同一机制需求，届时一并定（WaveDirector 届时可平移）；M6 波次表编辑器在组件数据上
盖专业 UI。**修订注写进 03 §8**。

**D2 导演 = 第二条刷怪通道（直接 SpawnFn），不派发 Spawner 配额**：03 §8 原案
"向激活的 Spawner 派发配额"修订为 DirectorSystem **直接经 `World::GetSpawnFn()`
出生**（与 SpawnSystem 平行）；Spawner 保留"常驻环境刷怪"语义。理由：现 Spawner
无激活/权重面，配额派发需按 prefabId 跨实体匹配、语义绕；两通道各司其职
（Spawner=环境持续、Director=脚本波次），M6 TD 模板波次同消费导演。出生点 =
导演实体 `Transform2D` + 条目 range 环（RNG 口径同 SpawnSystem：
`UnitVec2() * range * Float01()`）；"环形 off-screen/避墙"归玩法层（模板把导演
实体挂玩家跟随即可）。**修订注写进 03 §8**。

**D3 波次状态机（顺序相位语义）**：每导演实体 `time += dt`（**缩放 dt**——
timeScale=0 冻结波次，与批① D5 暂停语义一致）→ `time ≥ waves[waveIndex].startTime`
且游标未尽 → **该波生效**：发 WaveStart（`src`=导演实体、dst=Null、payload
`[0]`=波序号 0 起、`[1]`=本波计划总数 Σcount、`[2]`=startTime 配置值）、重置运行时
（`cd[]=0、spawned[]=0`、entryCount 防御钳 ≤4）。波内条目：`cd[i] -= dt`；
`cd ≤ 0 && spawned < count &&（capAlive=0 或队计数 < capAlive）` → 出生恰 1 个
（`cd += max(interval/rampMult, dt)`——每条目每 tick 至多 1）；cap 顶格时 `cd` 钳
在 0 持币待发（腾位后续 tick 补）。**波重叠 = 后波接管**：同 tick 多波到期按表序
全部生效、仅最后一波持有运行时（前波未完成条目即废止）——顺序相位语义，文档+
测试断言；并行波（每波独立运行时）待 M6 需求出现再扩。空波（entryCount=0 或
Σcount=0）照发 WaveStart——纯宣告波是合法用法（banner 波）。

**D4 capAlive = 导演侧同队闸门（与 Spawner.maxAlive 同款"约"语义）**：
DirectorSystem 自持 64 槽队计数 + **30 tick 普查**（`SpawnSystem::Census` 同模式；
系统间不共享计数——各自乐观自增 `++teamCounts_[team & 63]`，组合过冲 ≤ 每 tick
出生和 × 普查周期，压测红线"约"语义与既有口径一致）；`0 = 不限`。计数对象 =
`Meta.team == spawnTeam` 的存活实体。

**D5 RNG 子流 1（导演注册序；Spawn 的子流 2 不受扰）**：出生环偏移消费
`world.SystemRng(1)`——该子流至今无消费者，本批启用对既有场景零漂移；**多导演
共享子流 1 按池序消费**（确定性但任意，文档声明）。`InstallDefaultSystems` 的
子流注释同步（"仅 Spawn 持 2" → "Director 持 1 / Spawn 持 2"）。

**D6 WaveStart 事件契约（C# 零改动可订阅）**：枚举两侧已镜像（批① Pickup 同款
前置）；`Events.Subscribe(GameEvent.WaveStart)` 开箱即用。payload 约定如 D3；
导演每次出生同发 `Spawn` 事件（与 SpawnSystem 同口径，payload[0..1]=出生点 xy）。
**prefab 失败语义**：SpawnFn 返回 Null（编辑器低 32 位错绑）→ 该条目即刻废止
（`spawned[i] = count`，不逐 tick 重试刷告警——工厂侧告警已去重）。

### 11.3 验收判据（全部满足才勾销）

1. `ctest` 3/3 全绿，含新增引擎测试 ≥6 例（§12 各 T 所列）+ script-tests 扩展
   （WaveStart → Ui.Set 样例）；
2. ~~金回放零重录~~ **执行勘误 → 重录口径**：`ComputeStateHash` 对注册表组件名
   无条件入哈希 → 新增 WaveDirector 即全帧哈希漂移（bench-sim 终态与旧档逐项一致
   = 行为零漂移旁证）。**合规路径 = 重录 m5b2 三档后 replay mismatches=0**（09 §7
   已沉淀"加字段可零重录、加组件必重录"推论）；
3. C# 布局探针全绿（WaveDirector 1260B 镜像 + 数组段行 1:1）；
4. bench-survivor 导演化：判据不变仍 PASS（alive≥10000 且 frameAvg≤22.2ms），
   且 **alive 突破 Spawner 闸门**（播种改 8000；>8000 即导演出生实证）、
   900 帧内 WaveStart ≥3 次（事件计数）；
5. `tools/editor-regression.sh full` 全绿；
6. 回写：03（§3.3 组件行、§4 表 #2 行注、§8 修订+落地 schema、§11 WaveStart
   payload）、04（事件样例注）、05（可编辑数组段）、09（§6.10 导演化基线行 +
   §7 零重录注记）、08 §3 表注、DevLog 批②条目、本页勾销。

---

## 12. 任务分解（T1→T5 依序落地）

### T1 WaveDirector 组件（内联波次表）+ 目录/镜像 —— 约 0.5 天

**组件**（`Engine/Components/BehaviorComponents.h` 追加于文件尾，**登记序=组件 id
只增**——挂 Knockback 之后、id 27，Concept 归 Behavior 但登记表新分组注释
"M5 批②（id 27..）"）：

```cpp
struct WaveEntry {                    // 16B：一条刷怪条目
    uint32_t prefabId = 0;            // prefab guid 低 32 位（Spawner/Shooter 同口径）
    uint16_t count = 8;               // 本条目总出生数
    uint8_t _pad[2] = {};             // 零化（seg 原始字节入状态哈希）
    float interval = 0.5f;            // 出生间隔秒（受波 rampMult 加速）
    float range = 200.0f;             // 出生环半径（绕导演实体位置）
};
struct WaveDef {                      // 76B：一波
    float startTime = 0.0f;           // 波开始局内时刻秒（表序=生效序，期望升序）
    float rampMult = 1.0f;            // 节奏倍率（interval / rampMult）
    uint8_t entryCount = 0;           // 有效条目数 ≤4（消费处防御钳）
    uint8_t _pad[3] = {};             // 零化（同上）
    WaveEntry entries[4];             // 定长 4（元素字段表扁平登记 e0..e3）
};
struct WaveDirector {                 // 1260B
    uint32_t spawnTeam = 1;           // 波次出生队伍（capAlive 计数同队）
    int32_t capAlive = 0;             // 同队存活上限（0=不限；压测红线）
    uint8_t waveCount = 0;            // 有效波数 ≤16（Inspector 可编辑）
    uint8_t _pad[3] = {};             // 零化（waves 对齐衬垫兼哈希确定性）
    WaveDef waves[16];                // 波次表（ArraySegMeta 序列化/哈希/Inspector）
    // ---- 运行时（FIELD_RT：不入档、入状态哈希）----
    float time = 0.0f;                // 局内时刻（缩放 dt 累计）
    float waveCooldown[4] = {};       // 当前波各条目距下次出生
    uint16_t waveSpawned[4] = {};     // 当前波各条目已出生
    uint8_t waveIndex = 0;            // 已生效波数（活跃波 = waves[waveIndex-1]）
    uint8_t _pad2[3] = {};
};
```

static_assert 三条（16/76/1260，trivially_copyable）追加于布局冻结块尾
（`BehaviorComponents.h:135` 后）。**全 pad 字段显式 `= {}`**：StateHash 对 seg 按
元素原始字节直哈（`StateHash.cpp:64`），值初始化保证填充零化确定性。

**目录**（`ComponentCatalog.cpp`）：kWaveDirector 13 行——`spawnTeam`(FIELD,
TeamRef)、`capAlive`(FIELD, Int32 + ED_RANGE(0,1e6))、`waveCount`(FIELD, UInt8 +
ED_TIP("有效波数 0..16，表格见下方数组段"))、RT 10 行全 ED_HIDE（`time`、`waveIndex`、
`cd0..cd3`、`spawned0..3` 手工行仿 hitMem0..3 模式 `ComponentCatalog.cpp:109`）；
数组段 `kWaveDirectorSeg`（仿 kStatusEffectsSeg `:208`）：offset(waves)/elemSize 76/
countOffset(waveCount)/maxCount 16/元素字段表 **19 行**（`startTime`、`rampMult`、
`entryCount` + `e{0..3}{prefab,count,interval,range}` 扁平命名）；登记
`REGISTER_ED` + seg 挂 `REGISTER_SEG` 组合（参照 StatusEffects 的 seg + Inventory
的 ED 并存形态——REGISTER_ED 宏现不传 seg，**本批顺带扩 REGISTER_ED 宏带 seg 槽**）。

**镜像**：`Components.cs` 表尾（XpProgress/IncrementalState 之后、id 27 同序）加
`WaveEntry`(16B 可见字段)、`WaveDef`(76B，`internal fixed byte _entries[64]` 不透明)、
`WaveDirector`(1260B，`internal fixed byte _waves[1216]` + RT fixed 缓冲)；
`LayoutTables.cs` Comps 表尾 1 行（13 字段）+ segs 表尾 1 行（19 元素行挂 Fields
尾，偏移由 WaveDef/WaveEntry 镜像实测 O()）。**布局探针=机械护栏，改漏必红**。

### T2 DirectorSystem 实现 + 引擎测试 —— 约 1 天

**落点**：`Systems.h:53` 类声明补成员（`std::vector<uint32_t> teamCounts_;`
`uint32_t censusCountdown_ = 0;` `bool warnedNoFactory_ = false;` + `void Census(Scene&)`，
全仿 SpawnSystem `:67-74`）；`Systems.cpp:84` 空占位替换为真实现——逐行注释 D2–D6：
普查（30 tick）→ 工厂检查（有 WaveDirector 无工厂告警一次）→ `View<WaveDirector,
Transform2D>` 逐导演：D3 状态机 + D5 子流 1 出生环 + D4 capAlive 闸门 + Spawn/
WaveStart 双事件。`InstallDefaultSystems`（`:726`）注释更新子流占用。

**测试**（`tests/engine_tests.cpp`，注册于 main 表 `TestVerifyPickupXpLevelUp` 之后）：
- `TestWaveDirectorWavesAndEvent`：TestSpawnFactory 计数版工厂；2 波（t=0.5s：
  1 条目 count 3 interval 0.1 range 50；t=1.5s：1 条目 count 2）→ 120 tick：
  WaveStart 恰 2 次（payload[0]==0/1、[1]==3/2、src==导演实体）、出生总数 5、
  位置在环内、team 覆盖生效、`waveIndex==2`；第 3 波 startTime=99s 未生效；
- `TestWaveDirectorRampAndOverlap`：rampMult=2 半间隔实证（interval 0.1 →
  每 ~3 tick 一生 vs 基线 6）；双波同 tick 到期 → 仅后波持有运行时（前波条目废止）；
- `TestWaveDirectorCapAlive`：capAlive=2 + count 10 → 队存活恒 ≤2、出生停滞后
  不再增长（无其他刷怪源 = 精确断言）；
- `TestWaveDirectorTimeScaleFreeze`：timeScale=0 → 60 tick 无 WaveStart、time==0、
  零出生；恢复 1 后波照发（D3 与批① D5 一致性）；
- `TestWaveDirectorArchive`：波表 roundtrip（waveCount/entryCount/prefab/count/
  interval/range/rampMult/startTime/capAlive/spawnTeam）；RT（time/waveIndex/cd/
  spawned）不入档（扩 `TestArchiveArraySegAndRuntimeFields` 模式）；waveCount>16
  读档钳制（`TestVerifyArrayCountClamped` 同款）；
- `TestWaveDirectorDeterminism`：孪生 World 同种子同配置 300 tick → StateHash
  相等（RNG 消费序 + seg 原始字节零化双护栏）。

### T3 Inspector 可编辑数组段 —— 约 0.5 天

**落点**：`InspectorPanel.cpp:814 DrawArraySeg`——元素单元格从 `ImGui::Text`
换为**按 FieldType 的通用编辑控件**（Float→DragFloat、UInt32/UInt16/UInt8→
InputScalar、Bool→Checkbox；Vec2/Double 等暂留只读）；`PushID(seg.field:i:f)` 三段
防撞号；改值 → `ctx.dirty = true`。**Undo 挂属性轨**：签名加 `bool& anyActive,
bool& anyDeactivated` 出参汇入 `:761` 调用点的 M4.7d 提交判定（组件级
SnapshotComponent 天然覆盖 seg 字节）；入参 `const void* comp` → `void* comp`
（写字段需要，调用点本就有可写指针）。waveCount/entryCount 等计数字段走普通
字段行（本就可编辑）——**作者路径 = 改计数出槽位 + 表格填数值**。收益外溢：
StatusEffects/Inventory 条目从此可调参。波次表的专业编辑器（增删波/条目按钮、
prefab 拖拽）归 M6。

### T4 bench-survivor 导演化 + script-tests WaveStart 样例 —— 约 0.5 天

- `SeedBenchSurvivorScene`（`EditorApp.cpp:123-129`）：Spawner maxAlive
  **10000 → 8000**（注释同步"导演让位 2000"）；新增实体 "BenchDirector"
  （Transform (0,0)）挂 `WaveDirector{.spawnTeam=1, .capAlive=10000}`，波表 3 波
  （t=2/7/12s，每波 2 条目：BenchMob ×600 interval 0.02 range 550 + ×300
  interval 0.05 range 300，prefabId=pguid 低 32 位）——导演出生顶到 10000 上限、
  900 帧内 WaveStart ≥3、战斗/成长链路照跑；
- 判据不变：alive≥10000 且 frameAvg≤22.2ms；**alive>8000 即导演通道实证**。
  预期 sim +0.3~0.8ms（现 13.2ms/判据余量 9ms）；越线则 profile 归因、登记
  09 §6.10 观察项，不扩 scope（批⓪① 先例）；
- script-tests：`TestScript.cs` 表尾 typeId 5 `WaveBannerBehaviour`
  （`Events.Subscribe(GameEvent.WaveStart)` → `Ui.Set("wave", $"WAVE {n}", -1)`）；
  `tests/script/main.cpp` 新 `TestWaveStartToUi`（仿 `TestTimeScaleAndUiChannel`
  `:506`：挂 typeId 5 → 人工推 WaveStart 包 → Step → 断言 RtUi 槽 key/text）；
- 09 §6.10 加"导演化"基线行、08 §3 表注。

### T5 文档收口 —— 约 0.25 天

- `03-ECS.md`：§3.3 组件表加 WaveDirector 行（id 27、1260B、字段概要）；§4 表
  #2 行去占位注（"波次/刷怪调度（§8）"补 M5 已落地）；**§8 修订块**（D1 组件化
  替代独立资产、D2 直接出生替代 Spawner 配额派发——两处标注"M5 批②修订"，
  落地 schema/语义/JSON 形态示例）；§11 事件段补 WaveStart payload 约定；
- `05-Editor.md`：Inspector 可编辑数组段说明（通用能力 + WaveDirector 作者路径）；
- `04-CSharp-Scripting.md`：事件表 WaveStart 行（订阅样例指向 script-tests）；
- `09-Testing.md`：§6.10 基线行；§7 零重录注记（批② 同款机械证明）；
- DevLog 批②条目（前后数字对照）；本页勾销。

**合计约 2.75 个工作日**（批⓪ 2.5 / 批① 3.5 同量级；省下管线重编号与 C# 桥，
新付可编辑数组段）。

---

## 13. 确定性与回放影响

- 现有基准场景无 WaveDirector → DirectorSystem 空转（无 RNG 消耗、无实体增删，
  bench-sim 终态与旧档逐项一致 = 行为零漂移旁证）；但 `ComputeStateHash` 对注册表
  **组件名无条件入哈希**（含零实体组件）→ 新增组件必致全帧哈希漂移 → **金档重录
  m5b2**（判据 2 执行勘误，09 §7 已沉淀推论）；
- **RNG 子流 1 首次启用**：无既有消费者、与 Spawn 的子流 2 独立——无导演场景
  零消耗，有导演场景消费序=池序+tick 序确定；多导演共享子流 1（确定性但任意，
  文档声明）；
- 导演全部运行时态（time/waveIndex/cd/spawned）登记 FIELD_RT → **入状态哈希**
  不入档（回放覆盖；改漏字段=哈希盲区——T1 的 13 行登记表即护栏）；
- seg 元素原始字节入哈希 → pad 显式零化 + 值初始化（Emplace/C() placement-new）
  → 双世界哈希测试（T2 末条）机械验证；
- timeScale=0：dt=0 → 导演 time 冻结、cd 冻结、无出生、RNG 不消耗（批① D5
  同语义，回放帧对齐保持）；
- capAlive 普查固定 30 tick 节拍（无墙钟）→ 闸门时序逐帧可重放。

## 14. 风险与对策

| 风险 | 对策 |
|---|---|
| 1260B 大组件（池拷贝/哈希/快照成本） | 导演实体数 ≈1；StateHash 增 1216B/导演（FNV 微不足道）；提取层按池全量拷贝是既有形态，无万级导演场景 |
| seg 元素 padding 入哈希（未零化=回放随机漂移） | 全 pad `= {}` 默认成员初始化 + 值初始化构造链 + T2 双世界哈希测试三重护栏 |
| Inspector 可编辑 seg 破坏 Undo 属性轨 | seg 控件 active/deactivated 汇入 M4.7d 同一提交点（顺序纪律：提交判定在前）；smoke-ui/editor-regression 覆盖 |
| capAlive 与 Spawner.maxAlive 双闸门（同队各自 census+乐观计数） | 组合过冲 ≤ 每 tick 出生和 × 30 tick——"约"语义与 SpawnSystem 既有口径一致；压测红线语义本就不求精确 |
| 波重叠/乱序 startTime 的语义歧义 | 表序=生效序、后波接管（D3 定死）；文档+测试断言 |
| 无 SpawnFn（裸 World/工厂未注册）导演哑火 | warnedNoFactory_ 一次告警（SpawnSystem 同款 [ISSUE-4] 口径）；条目 prefab 失败即废止不重试（D6） |
| entryCount/waveCount 超容（手改 Inspector/JSON） | 读档 ReadArraySeg 钳 + 消费处防御钳双保险（TestVerifyArrayCountClamped 模式） |

## 15. 验证命令（批②完工口径）

```bash
ctest --test-dir build/mac --output-on-failure                        # 单测＋布局探针＋脚本测试
# 金回放（执行勘误：新增组件=哈希流漂移 → 重录 m5b2 三档；重录后 replay 必须 = 0）
build/mac/Samples/bench-sim/lemon-bench-sim --frames 3600 --record build/goldens/m5b2-sim-mt.txt
build/mac/Samples/bench-sim/lemon-bench-sim --frames 3600 --threads 1 --record build/goldens/m5b2-sim-st.txt
build/mac/Samples/bench-script/lemon-bench-script --frames 1800 --record build/goldens/m5b2-script.txt
build/mac/Samples/bench-sim/lemon-bench-sim --frames 3600 --replay build/goldens/m5b2-sim-mt.txt
build/mac/Samples/bench-sim/lemon-bench-sim --frames 3600 --threads 1 --replay build/goldens/m5b2-sim-st.txt
build/mac/Samples/bench-script/lemon-bench-script --frames 1800 --replay build/goldens/m5b2-script.txt
build/mac/Editor/lemon-editor --bench-survivor --frames 900           # 导演化基线 ≥45fps 且 waves≥3 且 teamAlive>8000
tools/editor-regression.sh full build/mac                             # 编辑器全回归
```

**完工记录（2026-09-23）**：全部判据满足——ctest 3/3（engine-tests **13127** 检查，
+29：TestWaveDirector 六件套 WavesAndEvent/RampAndOverlap/CapAlive/TimeScaleFreeze/
Archive/Determinism；script-tests **1460**，+TestWaveStartToUi）；C# 布局探针全绿
（WaveDirector 1260B 镜像 + 数组段 19 元素行 1:1，探针侧计数断言 27→28 两处同步）；
金回放**重录口径**（m5b0 旧档 replay 全帧 mismatch 但终态逐项一致 → 行为零漂移旁证；
重录 m5b2 三档后 mismatches=0 ×2 + script PASS）；bench-survivor 导演化三跑
**66/66/66 fps PASS**（alive 10435、frameAvg 15.11~15.20ms、waves=3、teamAlive 10002
顶满 capAlive、三跑逐位一致；Director avg **0.068ms**）；editor-regression full 11/11。
过程发现并修复：**ScriptHost 事件派发期 g_world/g_scene 窗口缺口**（此前仅 TickBatch
置位——事件回调内 Ui.Set/Time.Scale/Instantiate 静默空转；批① xp 样例恰在 Update 内
调用故未暴露；批② 以 Update 同窗口语义修复，04 §4 已记）。分解预判偏差：波容量估算
漏了自家"后波接管"语义（前波条目废止），首跑 alive 9565 FAIL → 改 4 条目/波
180/s、波前移 t=1/6/11s 后 PASS（M5-Plan §12 T4 注已按此口径）。
