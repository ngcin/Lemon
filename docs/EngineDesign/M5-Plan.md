# M5 实施计划 —— 玩法 + vs-survivor 模板（08 §2）

> 滚动计划：每批开工前分解到文件/行级，完工后本页勾销并回写 DevLog。
> **批⓪（战斗闭环）已于 2026-09-22 完成**（实测数字见 DevLog 同日条目；执行中的
> 两处计划勘误：Projectile 实际 48B 非 56B；bench-sim 旗标为 `--frames` 非 `--steps`，
> 二进制路径 `build/mac/Samples/bench-sim/lemon-bench-sim`）。**批① 已于 2026-09-22
> 完成**（§6–§10 分解 + 完工记录见 §10 末；执行勘误：Pickup 密核扫描实测 1.87ms
> 超预估 0.2~0.6ms，登记 09 §6.10 观察项不扩 scope；smoke-ui 飘忽为既有问题）。
> **批② 已于 2026-09-23 完成**（§11–§15 分解 + 完工记录见 §15 末）。**批③ 已于
> 2026-09-23 完成**（§16–§20 分解 + 完工记录见 §20 末；判据 2 零重录兑现）。
> **批④ 已于 2026-09-23 完成**（§21–§25 分解 + 完工记录见 §25 末；判据 2 零重录
> 兑现；真人 10 分钟一局验收待用户执行——M5 代码面就此收口，余项：多脚本 scripts[]
> 重排 M6）。
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
| ③ | 表现层 | clip .asset ＋ Animator 帧映射 ＋ 最小图集/切片（自 M6 提前）＋ 素材包第一批 | **✅ 完成 2026-09-23** |
| ④ | 收口 | 存档 ＋ HUD 完整版 ＋ 模板整合 ＋ Play 调参 ADR | **✅ 完成 2026-09-23**（判据 1–4/6 全过；判据 5 真人验收待用户） |

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

---

## 16. 批③：表现层 —— 现状盘点与设计决策（代码逐行核对，2026-09-23）

### 16.1 现状：管线断点在哪

| 环节 | 现状 | 锚点 |
|---|---|---|
| AnimatorSystem #13 | **仅 time 推进**：占位周期 1.0 回绕；curFrame 帧映射注释自认"接 clip 资产后补，M5" | `Systems.cpp:750` / `Systems.h:163` |
| Animator2D 组件 | 16B 布局已冻结，六字段齐备（clipId/time/speed/loop/playOnStart/curFrame），C# 镜像+探针已同步——**clipId 现语义"动画页基 spriteId"从未实现**（Inspector tip 自认"M5 clip 资产化"） | `RenderComponents.h:23` / `Components.cs:72` / `ComponentCatalog.cpp:62` |
| clip 数据资产通道 | 无。06 §2.2 clip2d=JSON 帧动画（AnimationEditor M6 产出）落空；批② D1 已把"独立数据资产通道"留给本批 | `06:56` / 本页 §11.2 D1 |
| 图集/切片 | M4 最小集：**每 PNG 一页一全幅 sprite**；`.meta` 只存 guid/type/hash（06 §2.1 的 `importer` 配置段未实现）；`AddSpriteAt` 已支持任意号+空洞（resize 哨兵） | `AssetGpuCache.cpp ImportSprite` / `AssetDatabase.cpp:145 SyncMeta` / `Atlas.cpp AddSpriteAt` |
| 素材包 | 06 §7 定 yami MIT 底包"直接采用"，至今**未引入一张**；来源在位：`~/GameProjects/yami-rpg-editor/Project/Templates/arpg-ts-chinese/Assets/`（Dungeon 表 hero 144×32 / monster 128×32 / boss 256×48；yami `.anim` 自带 hframes——hero 9 / monster 8，即 16px 格） | `06:131` |
| 编辑器 Play 桥先例 | SpawnFn（清障②）：EnterPlay 建 prefab 缓存、uint32 = GUID 低 32 位、错绑去重告警——**clip 表同款机制可直接复制** | `EditorContext.cpp:393/:542` |

**管线零重编号 + 零新组件 + 零布局改动**：Animator #13 占位即正式槽位（批②同款无插队成本）；
本批**不新增 ECS 组件、不改任何组件布局/注册表行数/字段序**——批②勘误的教训前置规避
（ComputeStateHash 对组件名无条件入哈希 → 新组件必致金档重录；本批机制上保证零重录，见 §18）。

### 16.2 设计决策（六条，实施时写进代码注释与文档）

**D1 clipId 语义 = clip 资产 GUID 低 32 位；引擎侧 ClipTable（纯 id 数据，零 GPU 依赖）**：
新增 `Engine/ECS/ClipTable.{h,cpp}`——`clipId → {fps, loop, frames[](spriteId)}`，
`World` 持有（`Clips()` 访问）。帧号 = 纯函数 `min(n-1, (u16)(time*fps))`（无逐帧累加
状态机 → 回放确定性）；表只 `Find` 不遍历（unordered_map 迭代序不进任何确定路径）。
映射约定与 prefabId 同款（03 schema uint32 恒定；M7 dense id 表同语义替换）。编辑器
EnterPlay 一次性建表（同 BuildPlayPrefabCache 快照语义：Play 世界 = 进 Play 时刻资产态）；
引擎测试直接填表。**clipId=0 或未命中 → 走 M2 旧路径逐位不变**（time 推进 + 周期 1.0
回绕）——既有场景零漂移 = 金回放零重录的机制保证。

**D2 clip `.clip` JSON 格式（06 §2.2 clip2d 落地；文本可 diff）**：

```json
{ "schemaVersion": 1, "name": "hero-walk", "fps": 8, "loop": true,
  "frames": [ {"sheet": "5bd31a7c10e9f2c8", "cell": 0}, {"sheet": "...", "cell": 1} ] }
```

帧引用 = **精灵表资产 GUID（hex）+ 切片序号**（行优先），不直接存 spriteId——切片身份
=(sheet guid, cell)，manifest 重排不断链（06"切片 GUID 稳定匹配"同精神）。EnterPlay
解析为 spriteId 数组入 ClipTable；sheet 缺失/越界/JSON 坏 → 红字跳过该 clip（实体回退
M2 路径不炸）。`AssetType::Clip`（扩展名 `.clip`）入资产库类型表（编辑器域，零哈希影响）。
`Animator2D.loop` 为**权威**（实体上热调参）；clip.loop 仅档面默认值。per-frame 时长/
PingPong/Random/Queue/帧事件打点归 M6 AnimationEditor（05 §7）。

**D3 最小图集/切片 = 单页网格切片（不打包、不 MaxRects）**：`.meta` 增 `importer` 段
（06 §2.1 原案格式）：

```json
{ "guid": "...", "type": "sprite", "hash": 0,
  "importer": { "slice": "grid", "cell": [16, 32], "frames": [9, 1] } }
```

- **frames 由作者显式声明**（yami `.anim` hframes/vframes 同款）——DB 层纯文件系统零解码
  即可在 Rescan 时分配**连号切片块**（base..base+cols*rows-1，manifest 记账只增不减）；
  GpuCache 保持 const-DB（读 entry 字段 AddSpriteAt，不参与记账）。
- ImportSprite 切片路径：解码后校验 `cols*cellW ≤ w && rows*cellH ≤ h`（不整除/越界 →
  红字**不切**，宁缺勿错——AddSpriteAt 越界是 assert）；**一页纹理不变**（bindless 槽
  上限 64 不受扰）；登记 = 全幅 sprite（引用兼容/拖入默认）+ 连号切片块。
- 切片块跨会话稳定（manifest 持久）→ 场景引用/clip 解析可复现。frames 增大 → 新块
  （旧块烧号，"只增不减"口径）；缩小 → 多余号留空洞。
- M6 才做：多表 MaxRects 打包、手动/自动切片 UI、切片编辑器、AnimationEditor 时间轴。

**D4 Animator 帧映射语义（M5 版）**：有 clip 时——`playOnStart=0` → 整体冻结（time/
curFrame/spriteId 不动；M5 无 Play() API，此位即暂停开关，文档明示）；否则
`time += speed*dt`（缩放 dt：timeScale=0 冻结动画，批① D5 同语义）→ loop=1 回绕
`period = n/fps`（time 有界）；loop=0 钳末帧（time 钳 total——M2"非 loop 无界增长"
随 clip 落地收口，**无 clip 路径不变**）；负 time 钳 0（speed 负值防御）→
`curFrame = min(n-1, time*fps)`、`sr.spriteId = frames[curFrame]`（SpriteRenderer 可缺 =
纯计时；写入幂等逐帧重写）。time/curFrame 保持 FIELD 行（入档入哈希，**不动字段位**——
动了即破既有场景哈希，违背零重录前提）。

**D5 素材包第一批 = yami Dungeon 精灵表（5–6 张）+ 配套 clip，入库 `Samples/Assets/yami-dungeon/`**：
06 §7 既定"直接采用"（MIT；`THIRD_PARTY.md` 登记来源/许可/再分发声明，07 移植红线合规）。
第一批：hero ×2、monster ×2、boss ×1（.anim hframes 核对切片格：hero 9×16px、
monster 8×16px、boss 以 `首领.anim` 实测为准）+ `hero-walk.clip`/`monster-walk.clip`
样例 + README（来源/许可/切片参数）。**bench-survivor 动画化用程序化 4 帧表自播种**
（temp 项目 hermetic，不依赖仓库相对路径——06 §7"bench 直接用默认素材"的全面接轨推
M6 模板打包）；yami 链路人工核验一次（临时项目 CLI 进 Play）记录 DevLog。

**D6 编辑器消费面 = clip 槽控件（FieldHint::ClipRef）**：`Animator2D.clipId` 行从 ED_TIP
占位升级为 clip 资产槽（下拉 AssetType::Clip 全列 + AssetBrowser 拖入 + 右键清空，
DrawSpriteSlot 同模式；值 = GUID 低 32 位，反查 entry 显示 relPath）。time 的
ED_RANGE(0,1) 放宽到 (0,100)（period 可 >1s）。**sprite 槽的切片直接引用 UI 归 M6**
（本批切片消费面 = clip；Inspector sprite 槽仍全幅）。C# 侧零改动（Animator2D 镜像
已同步，无新 SDK 面；模板用的 Lemon.Anim API 归 M6）。

### 16.3 验收判据（全部满足才勾销）

1. `ctest` 3/3 全绿，含新增引擎测试 ≥6 断言组（§17 T1 所列：帧号纯函数/loop 回绕/
   loop0 钳末帧/playOnStart 冻结/无 clip 回退逐位不变/无 SpriteRenderer 不炸/孪生哈希/
   clipId roundtrip）；
2. **金回放零重录**（本批机制保证：零新组件、零布局改动、无 clip 场景走 M2 旧算术）：
   m5b2 三档 `--replay` mismatches=0；若执行中被迫动 schema → 按 09 §7 重录并记勘误；
3. `--smoke-anim` 新冒烟（程序化 4 帧表 + grid meta + .clip → 进 Play → curFrame 推进 +
   sr.spriteId 落切片号区间）PASS，editor-regression full **12/12**；
4. bench-survivor 动画化（万怪 Animator2D）：3× PASS 判据不变（alive≥10000 &&
   frameAvg≤22.2ms && director 证据）+ 新增 animOk 证据项（采样 mob sr.spriteId ∈
   切片号区间）；Animator 系统 avg 进 09 §6.10 台账；
5. 素材包入库：`Samples/Assets/yami-dungeon/`（≥5 PNG + ≥2 .clip + README）+
   `THIRD_PARTY.md` 登记；yami 链路人工核验记录 DevLog；
6. 回写：06（§2.2 clip2d/§2.1 importer 段落地注记 + §5 打包注记 + §7 第一批记）、
   03（§3.3/§5 Animator 帧映射语义）、05（§5 clip 槽）、08、09（测试数/台账）、DevLog、
   本页勾销。

## 17. 任务分解（T1→T5 依序落地）

### T1 ClipTable（引擎）+ AnimatorSystem 帧映射 + 引擎测试 —— 约 1 天

**新文件** `Engine/ECS/ClipTable.h/.cpp`（CMake 源表追加）：`ClipDef{fps, loop,
vector<uint32_t> frames}`；`ClipTable::Add/Find/Clear/Count`（unordered_map，只 Find
不遍历）。`World.h` 持有 `ClipTable clips_` + `ClipTable& Clips()`。

**AnimatorSystem 重写**（`Systems.cpp:750`）：D1/D4 语义；**无 clip 回退 = M2 代码原样
保留**（先跑既有 `TestVerifyAnimatorAdvance` 作逐位锚）。`Systems.h:163` 注释更新
（"M2 最小"→"M5 帧映射"）。

**测试**（`tests/engine_tests.cpp`，计数 13127→+N）：`TestVerifyAnimatorFrameMapping`
（fps8×3 帧：tick 8→frame1、tick 22→frame2、tick 23 回绕 frame0；loop0 钳末帧+time 钳
total；playOnStart=0 冻结三态；speed 0.5 半速；clipId 未命中回退 M2；无 SpriteRenderer
不炸；孪生世界 300 tick StateHash 相等）；扩 `TestVerifyAnimatorAdvance`（未知 clipId
同回退）+ 孪生（若 archive 测试未覆盖 Animator2D → clipId roundtrip 断言并入）。

### T2 切片导入 + 切片号记账 + AssetType::Clip —— 约 1 天

**`AssetDatabase.h/.cpp`**：`AssetEntry` 增 `cellW/cellH/cols/rows/sliceBase/sliceCount`
（0 = 全幅）；`SyncMeta` 读 `importer.slice=grid` 段（坏值红字忽略）；Rescan 时
`cols>0 && sliceBase==0` → 分配连号块（nextSpriteId 推进）；manifest 条目扩
`"slice": {base, count}` 键（读侧缺键 = 兼容旧档全幅）；`FindClipByLowId`；`TypeOf`
`.clip` → `AssetType::Clip` + `AssetTypeName`。

**`AssetGpuCache.cpp ImportSprite`**：切片路径（网格校验 → 全幅 + 连号块 AddSpriteAt；
热重导入同格重切）；`Evict` 切片页同现有路径（号保留）。

**测试**：切片记账为编辑器域——引擎单测覆盖不了 GPU 面，机械验证归 `--smoke-anim`
（T4）+ asset-chain 冒烟（既有）。DB 层 meta 解析逻辑纯文件系统，若 engine_tests 已有
DB 用例则并入（无则记 09 观察项：DB 单测空白沿用现状）。

### T3 clip 解析 + EnterPlay 建表 + Inspector clip 槽 —— 约 0.75 天

**`EditorContext.{h,cpp}`**：`BuildPlayClipCache()`（D2 格式解析；sheet 缺失/越界/坏
JSON 红字跳过；低 32 位碰撞去重告警——BuildPlayPrefabCache 同款）；EnterPlay 在
BuildPlayPrefabCache 后调用（`playWorld_->Clips()`）。

**`ComponentRegistry.h`**：`FieldHint::ClipRef = 1u<<8`。**`ComponentCatalog.cpp`**：
kEdAnimator2D[0] ED_TIP → ED_CLIPREF + tip 更新；time ED_RANGE(0,1)→(0,100)；
curFrame ED_TIP。**`InspectorPanel.cpp`**：`DrawClipSlot`（DrawSpriteSlot 同模式；拖入
payload 增 clip kind）+ dispatch。

### T4 素材包第一批 + smoke-anim + bench-survivor 动画化 —— 约 0.75 天

**素材包**：`Samples/Assets/yami-dungeon/`（hero×2/monster×2/boss×1 PNG + grid meta
+ hero-walk/monster-walk .clip + README）；`THIRD_PARTY.md` 登记（MIT/来源路径/再分发
声明）；人工核验：临时项目 `--project` + 复制素材 + 进 Play 看动画（DevLog 记录）。

**`--smoke-anim`**（`EditorApp.cpp`）：SeedSmokeProject 扩展（程序化 128×32 四帧表 +
grid meta + .clip 固定 guid）+ 实体（SpriteRenderer+Animator2D）+ 进 Play 断言
（Clips().Count()==1、curFrame 曾 >0、sr.spriteId ∈ [sliceBase, sliceBase+4)）→
`smoke-anim: ... => OK`；`tools/editor-regression.sh` full 增 1 步（11→12）。

**bench-survivor 动画化**：temp 项目自播种程序化 4 帧表 + clip；BenchMob prefab 增
Animator2D（fps 10）→ 万怪动画进压测口径；RESULT 行增 `anim(...)` 证据（采样 mob
spriteId 落切片区间数）；PASS 门槛不变。

### T5 文档收口 + 全量验证 —— 约 0.25 天

06（§2.2 clip2d 落地 + §2.1 importer 段 + §5 注记"单页网格切片 M5 最小集，MaxRects M6"
+ §7 第一批素材记）、03（§3.3 Animator 行 + §5 帧映射语义小节）、05（§5 clip 槽）、
08（素材包里程碑注）、09（§6.10 动画化基线行 + 测试计数）、DevLog 批③条目、本页勾销。

## 18. 确定性与回放影响（批②教训前置规避）

- **零 schema 变化**：不新增组件、不改组件布局、不动注册表行数/字段序/kFieldRuntime
  位——ComputeStateHash 的组件名与字段集不变（批②勘误根因直接规避）。
- **无 clip 路径逐位同 M2**：同一算术（time += speed*dt；period 1.0 回绕）——即便
  金档场景含 Animator2D 实体也逐位一致；m5b2 场景核对（bench-sim/script 播种无
  Animator2D）双保险。
- 有 clip 路径：帧号纯函数 `time*fps` 截断（IEEE 确定）；回绕同款 while 减法；
  curFrame/sr.spriteId 逐帧重写幂等；无 RNG 消费（子流占用不变 Director=1/Spawn=2）。
- 切片 spriteId 跨会话稳定（manifest 连号块记账）→ 场景档/clip 解析可复现；bench
  temp 项目每跑重建但播种序固定 → 分配序确定。
- **预期：金回放零重录**（判据 2）；执行中若被迫动 schema → 09 §7 重录口径 + 本页勘误。

## 19. 风险与对策

| 风险 | 对策 |
|---|---|
| 切片记账破坏旧 manifest 兼容 | 读侧缺键 = 全幅默认（0）；写侧仅 sliceCount>0 补键；版本号不动（向后兼容追加）；`--final` 冷启动链冒烟覆盖旧档路径 |
| yami 表切片格核错（整除失败） | T4 先读 `*.anim` hframes 定格（hero 9/monster 8 已核；boss 实测）；网格校验失败红字不切（宁缺勿错，不 assert） |
| PNG 二进制入库体积失控 | 第一批限 5–6 张（每张 ≤40KB 量级）；音效/UI/tileset 明确排除（后续批） |
| 切片块与"pages_ 按 spriteId 升序"不变式 | AddSpriteAt 任意号+空洞已支持（Atlas.cpp resize 哨兵）；RebuildAll 按资产基号序 → 块内连号保持 |
| bench 万怪动画推高 frameAvg | 门槛不动（22.2ms），实测进 09 台账；超限优化路径预留（spriteId 条件写/并行），本批不预优化 |
| playOnStart=0 无恢复面（无 Play API） | M5 语义 = 暂停开关（文档明示 + Inspector tip）；Play/CrossFade 归 M6 模板 |
| ClipTable 悬挂（ExitPlay 后资产热改） | 表 = 进 Play 快照（同 prefab 缓存语义）；文档注记"Play 中改 clip 不生效" |

## 20. 验证命令（批③完工口径）

```bash
ctest --test-dir build/mac --output-on-failure                        # 单测＋布局探针＋脚本测试
# 金回放零重录（判据 2：零 schema 变化 + 无 clip 场景走 M2 旧算术 → 不重录）
build/mac/Samples/bench-sim/lemon-bench-sim --frames 3600 --replay build/goldens/m5b2-sim-mt.txt
build/mac/Samples/bench-sim/lemon-bench-sim --frames 3600 --threads 1 --replay build/goldens/m5b2-sim-st.txt
build/mac/Samples/bench-script/lemon-bench-script --frames 1800 --replay build/goldens/m5b2-script.txt
build/mac/Editor/lemon-editor --bench-survivor --frames 900           # 动画化后判据不变 + anim 证据，×3
build/mac/Editor/lemon-editor --project <tmp> --smoke-anim --frames 120   # 单跑新冒烟（回归内含）
tools/editor-regression.sh full build/mac                             # 12/12（新增 smoke-anim 步）
```

**完工记录（2026-09-23）**：全部判据满足——ctest 3/3（engine-tests **13145** 检查，
+18：TestVerifyAnimatorFrameMapping 九组断言——帧界 tick 8/22、回绕 tick 23、loop0
钳末帧、playOnStart=0 冻结三态、半速、负速钳 0、无 SpriteRenderer 不炸、未知
clipId 回退 M2、roundtrip、孪生世界 300 tick StateHash 相等；script-tests **1460
不变**，C# 零改动）；**判据 2 金回放零重录兑现**（m5b2 三档 `--replay`
mismatches=0——零新组件/零布局改动/零字段位变化 + 无 clip 路径逐位同 M2，批②勘误
教训的前置规避成立）；`--smoke-anim` PASS 且 editor-regression full **12/12**（新增
anim-chain 步；首轮 smoke-ui rename=0/1 与 final overlay sel=0 两步时序飘忽，直跑
×3/×2 全绿后重跑全量过——09 §8 已登记同族）；bench-survivor 动画化三跑
**58/67/66 fps PASS**（alive 10435、anim 切片命中 **10003/10003**、三跑逐位一致；
判据门槛 22.2ms 不动，Animator avg **0.128ms**）；素材包第一批入库
`Samples/Assets/yami-dungeon/`（5 PNG 80KB + 3 clip + README，帧数与 yami .anim
hframes 逐表核对；THIRD_PARTY.md 登记）+ yami 真素材链核验（--smoke-anim 拷入项目：
5 表切片全登记 + 4 clip 建表 + 双链断言 OK）。执行勘误：无（分解预判全部兑现——
含 D5 预声明的"bench 用程序化表自播种保 hermetic"）。

---

## 21. 批④：收口 —— 现状盘点与设计决策（代码逐行核对，2026-09-23）

### 21.1 现状：管线断点在哪

| 环节 | 现状 | 锚点 |
|---|---|---|
| 存档 | **零落地**：06 §10 Save API/双通道/防损坏全停留在设计；引擎无任何持久化通道（autosave 是编辑器场景备份，非游戏存档） | `06 §10` |
| HUD | 批① 最小通道：8 槽纯文本+ProgressBar，**无颜色、无卡片、无交互回读**——三选一/死亡结算（06 §7 模板验收项）做不了 | `World.h:29 RtUiSlot` / `ViewportPanels.cpp:752` |
| 模板 | ProjectWizard 只有 **blank**（代码生成）；06 §1 向导"选模板→复制"未落地；vs-survivor 无可玩形态（bench-survivor 是压测场非模板） | `ProjectWizard.cpp:27` |
| Play 调参 | 决议 #5 已实现"落 Play World、Stop 丢弃"，但**无显式提示、无 ADR**——08 §2 M5 验收第三条"'Play 中调参→改动回灌'或显式禁用提示（ADR 记录）"未闭合 | `EditorContext.h:126` |
| 武器/升级 | 直射（Shooter）/穿透（pierce）/成长（XpProgress+LevelUp）组件层齐备；**环绕武器无组件**；三选一无卡片通道；死亡结算无 HUD 面 | `BehaviorComponents.h` |

**管线零重编号 + 零新 ECS 组件 + 零布局改动**（批③同款前置规避）：本批全部
机制走"World 级非 ECS 通道 + vtable 尾追 + 编辑器/脚本层"，ComputeStateHash
的组件名与字段集不变 → 金回放零重录（§23 机制保证）。

### 21.2 设计决策（六条，实施时写进代码注释与文档）

**D1 存档 = World 级 SaveChannel（内存 KV）+ 编辑器域 IO 钩子**：新
`Engine/ECS/SaveChannel.h`——`unordered_map<string, vector<uint8_t>>` 的
Set/Get/Remove/Count/清空/遍历（只 Find 不遍历序敏感路径）；World 持有
（RtUi 先例）。**不入 StateHash**（用户数据段非模拟态）。IO 归编辑器域：
`ScriptIoHooks{ saveFlushFn(World&), saveLoadFn(World&) }` 进程级注入
（EditorAssetHooks 同款模式），编辑器装配期装——flush = 项目根
`.lemon/saves/game.sav`（版本头 + 原子写 tmp→rename + 旧档转 .bak 三件套）；
EnterPlay 自动载入 / ExitPlay 自动兜底落盘（进 Play 快照语义一致）。
纯运行时（打包 M8）未注入 = Flush 红字一次后 no-op。**文件格式 M5 版 =
定长头二进制**（`LEMONSAVE` magic + u32 版本 1 + u32 条目数 + 每条
{u16 keyLen, key, u32 valLen, val}）；gzip 推 M7 packager（06 §10 修订注）。
M5 单档单文件（slot_N/settings/meta 三类分档推 M6——06 §10 注记修订，
模板用 key 前缀约定区分语义）。

**D2 C# Save API（06 §10 形状裁剪）+ vtable 尾追 4 项**：
`saveSet(key, bytes, len)` / `saveGetLen(key)`（返回长度，-1=无）/
`saveGet(key, out, cap)`（返回拷贝数）/ `saveFlush()`（钩子落盘）。
SDK `Save` 静态类：`Set(key, ReadOnlySpan<byte>)` / `Get(key)`（byte[]?）
/ `SetString/GetString`（UTF8 便利层）/ `HasKey` / `Flush`。旧宿主 null
判空（M4.4 尾追既有约定）。Flush 语义 = 全量覆写（幂等，帧末或显式皆可）。

**D3 HUD 完整版 = 槽样式扩展 + 卡片通道 + 交互回读**：
RtUiSlot 追加 `uint32_t color`（0=默认，ABGR；12→16B 纯 C++ 侧无镜像无哈希）
＋ RtUiChannel 加 `Clear(key)`（删单行——结算后清 HUD）。新 `RtUiCards`
（World 持有）：`{bool active; char title[48]; char labels[3][48]; int32_t pick}`
——三选一卡片状态 + 选择回读。vtable 尾追 4 项：`rtUiClear(key)` /
`uiCards(show, title, a, b, c)` / `uiCardPick()`（消费式：返回后置 -1）。
GameView：槽行 color 时 PushStyleColor（文本/进度条着色）；卡片 active 时
居中半透明面板 + 3 按钮（点击写 pick）；窗口聚焦时数字键 1/2/3 写 pick
（卡片期间脚本已 Time.Scale=0 冻结，与游戏输入无冲突）。**交互确定性口径**
：卡片选择属用户 IO 不入 InputSnapshot 录制流（金档场景通道空转 = 零漂移；
依赖卡片选择的场景不进金回放口径——同 Inspector Play 编辑的"游玩操作"
分类，09 §7 注记）。SDK：`Ui.Clear(key)` / `Ui.ShowCards(title,a,b,c)` /
`Ui.HideCards()` / `int Ui.CardPick()`。

**D4 模板 = 仓库 Templates/vs-survivor 完整项目目录 + 向导复制模式**：
模板即项目（project.lemon + Assets + Scenes + Prefabs + Game/*.cs），随仓库
版本管理、可直接打开调试；向导"复制模板"（06 §1 流程）：目录拷贝 →
project.lemon 重写（新项目 GUID=存档隔离键/name）→ Game/*.csproj HintPath
重锚（创建期固化 sdkDir）。**模板内资产 GUID 不重生成**（引用稳定；项目
GUID 才是隔离语义）。模板路径经编译期 `LEMON_TEMPLATE_DIR` 注入（CMAKE
源树推导，LEMON_SCRIPT_DIR 同款）。素材 = yami-dungeon 5 表 + 3 clip 拷贝
（THIRD_PARTY 已登记，模板 README 再声明）+ 程序化 gem/bullet/blade 小图。
玩法组装零新组件：环绕武器 = C# OrbitBladesBehaviour 驱动 blade prefab
（sprite+Hazard）绕玩家旋转；三选一 = HudBehaviour 订阅 LevelUp →
Time.Scale=0 + Ui.ShowCards（固定序池：移速/磁力/射速/穿透/生命上限/环绕+1，
零 RNG 确定性）→ 轮询 CardPick → 写玩家组件 → 恢复；死亡结算 = 订阅
Death（src==玩家）→ 结算 HUD + Save.SetString("vs.best") + Flush →
按 attack 位复活（清场重计数）。**机械验收 = `--smoke-template`**
（复制→build→Play 600 帧→断言 HUD 槽≥4/WaveStart≥2/kills>0/LevelUp≥1/
卡片出现且可选→OK），进 editor-regression（12→13 步）；真人 10 分钟一局
验收归用户（06 §7 判据，DevLog 记录）。

**D5 Play 调参 ADR-011 = 显式不回灌 + Inspector 横幅**：维持决议 #5
（Play 中编辑落 Play World、Stop 丢弃——Unity 默认同款），**不做字段级回灌**
（M5）。理由：回灌需 (guid, compId, bytes) diff + 回链匹配，Play 结构性变化
（spawn/destroy/prefab 实例化/脚本态）下语义模糊；Unity/Godot 4 均默认丢弃。
**显式提示**：Inspector 顶部 Play 中橙色横幅"▶ Play 模式：改动随 Stop 丢弃
（ADR-011）"；GameView 既有输入提示保持。M6+ 重评条件（编辑操作录制轨）
写进 ADR"后续"节。

**D6 输入位扩一位**：InputState bit5 = confirm（R 键映射，编辑器采样行
`ImGuiKey_R`）——模板复活/确认语义用；Input.h 注释与 C# `InputButton.Confirm`
同步。录制/回放不受扰（既有位序不变，新位只在按下时非零）。

### 21.3 验收判据（全部满足才勾销）

1. `ctest` 3/3 全绿，含 script-tests 新增（Save 通道 set/get/roundtrip/Flush
   钩子 + Ui.Clear/ShowCards/CardPick 消费语义 + InputButton.Confirm）；
2. **金回放零重录**（零新组件/零布局改动/新通道不入哈希）：m5b2 三档
   `--replay` mismatches=0；
3. `--smoke-template` PASS + `tools/editor-regression.sh full` **13/13**；
4. bench-survivor ×3 判据不变 PASS（本批不动播种，守门跑）；
5. 真人验收（用户执行）：新建 vs-survivor 项目 → Play 完整一局（HUD 四要素/
   三选一/波次/死亡结算/最高分续显）——10 分钟口径；
6. 回写：ADR-011、03（SaveChannel 注记）、04（vtable 尾追 8 项 + Save/Ui API
   + Input bit5）、05（向导模板选择/Inspector 横幅/GameView 卡片）、
   06（§7 模板落地 + §8 HUD v1 落地 + §10 M5 口径修订）、08 §3 表注、
   09（测试数 + §7 零重录注记）、DevLog 批④条目、本页勾销。

## 22. 任务分解（T1→T5 依序落地）

### T1 SaveChannel + vtable 4 项 + 编辑器 IO —— 约 0.75 天

- `Engine/ECS/SaveChannel.h/.cpp`（CMake 源表追加）：定长头二进制
  `Encode/Decode`（SaveChannel ↔ bytes）+ 内存 KV；World 持有 + `Save()`
  访问器；
- `ScriptHost.h`：vtable 尾追 4 项（D2 签名）+ `ScriptIoHooks` 注入面 +
  g_world 窗口实现（`ScriptHost.cpp` kNativeApi 表尾）；SDK `Save.cs`
  + `NativeApi.cs` 镜像；
- 编辑器：`EditorApp` 装配期 `SetScriptIoHooks`（flush =
  `.lemon/saves/game.sav` 原子写 + .bak；load 同路径解码）；EnterPlay
  载入 / ExitPlay 兜底落盘（`EditorContext.cpp`）；
- script-tests：`TestSaveChannel`（set/get/覆盖/删除/GetString 往返 +
  Flush 后 C++ 侧读 World 槽断言 + EnterPlay 清零/载入）。

### T2 HUD 完整版：槽样式 + 卡片通道 + GameView 交互 —— 约 0.75 天

- `World.h`：RtUiSlot + color（16B）；RtUiChannel::Clear；RtUiCards 结构 +
  World 持有 + ShowCards/HideCards/SetCardPick/ConsumeCardPick；
- vtable 尾追 4 项（D3）+ SDK `Ui.cs` 扩（Clear/ShowCards/HideCards/CardPick）；
- `ViewportPanels.cpp` GameView：槽 color 着色；卡片居中面板 + 3 按钮 +
  数字键 1/2/3（聚焦时）；RtUi 槽布局微调（行距/进度条宽）；
- script-tests：`TestUiCardsAndClear`（ShowCards 后 C++ 读 World 卡片态 +
  SetCardPick 模拟选择 → C# CardPick 消费恰一次 → 二读 -1 + Ui.Clear 删行）。

### T3 vs-survivor 模板 + 向导复制 + smoke-template —— 约 1.25 天

- `Templates/vs-survivor/`：project.lemon（占位 guid）/Assets（yami 拷贝 +
  程序化 gem/bullet/blade png + meta + 3 clip）/Prefabs（mob/bullet/gem/blade/
  boss）/Scenes/Main.scene（玩家 hero+clip+Health/Stats/XpProgress+Shooter+
  InputMover 脚本/BenchSpawner 风格宝石源改为杀怪掉落版 + WaveDirector
  16 波含 Boss 波）/Game/*.cs（GameMain+InputMoverBehaviour 软钳制+
  HudBehaviour+OrbitBladesBehaviour）/README（素材来源与许可）；
- `ProjectWizard`：ProjectDesc.template + Create 模板分支（拷贝/重写/重锚）；
  向导 UI 模板下拉（blank/vs-survivor）；CMake `LEMON_TEMPLATE_DIR` 注入；
- `--smoke-template`（`EditorApp.cpp`）：临时目录复制模板 → OpenProject →
  BuildGameProject → 装配 → EnterPlay 600 帧 → 断言（D4 列表）→
  `smoke-template: ... => OK`；`tools/editor-regression.sh` full 增 1 步；
- Input bit5 confirm（Input.h 注释 + ImGuiBackend 采样行 + C# InputButton）。

### T4 ADR-011 + Inspector Play 横幅 —— 约 0.25 天

- `docs/ADR/ADR-011-Play-Mode-Tweak-Disposition.md`：决策（显式不回灌+
  横幅）/备选分析（字段级回灌/录制轨）/Unity·Godot 对照/M6+ 重评条件；
- `InspectorPanel.cpp` OnGui 头部：Playing() 时橙色横幅（文案含 ADR-011 指向）。

### T5 全量验证 + 文档收口 —— 约 0.5 天

§25 命令全跑 + §21.3 回写清单逐项落（06 §7/§8/§10 修订注、03/04/05、
08 §3 表注、09 测试数与 §7 注记、DevLog 批④条目、本页勾销）。

**合计约 3.5 个工作日**（批① 3.5 天同量级；新付模板组装与冒烟，省零管线成本）。

## 23. 确定性与回放影响（批③教训延续）

- **零 schema 变化**：零新组件/零布局改动/零字段位变化——ComputeStateHash
  输入不变（批②勘误根因持续规避）；
- SaveChannel/RtUiCards/RtUiSlot.color 全不入 StateHash（呈现与用户数据段，
  非模拟态）——bench 金档场景这些通道空转，m5b2 零重录 replay=0 即机械证明；
- 卡片选择/存档 Flush 属用户 IO 副作用，不入输入快照——依赖卡片选择的场景
  （模板玩法）不进金回放口径，09 §7 分类声明；
- Input bit5 只在按下时非零，既有录制流（bit0..4）逐位不变——bench 场景
  无 R 键按下 = 零漂移；
- 模板脚本零 RNG（三选一固定序、掉落按击杀序），C# Random 不引入。

## 24. 风险与对策

| 风险 | 对策 |
|---|---|
| 模板冒烟 dotnet build 时长（回归 +N 秒） | 只引 SDK dll 无包依赖，--final blank 向导同量级（秒级）；超 30s 则缓存装配产物 |
| 模板玩法不平衡（10 分钟一局验收依赖真人） | 冒烟只断言机械链路（HUD/波次/击杀/升级/卡片）；波表与三选一数值保守初版，真人调参后回写 |
| 卡片 ImGui 焦点与输入路由冲突 | 卡片期间脚本 Time.Scale=0（模拟冻结）；数字键消费在 ImGui 层不进 InputState |
| 存档并发写（ExitPlay 兜底 + 显式 Flush 同帧） | Flush 幂等（全量覆写）；原子改名保证中途不损 |
| .lemon/saves 权限/路径异常（项目只读卷） | IO 失败红字一次不炸；SaveChannel 内存态照常（会话内 Get/Set 有效） |
| RtUiSlot 12→16B 布局改动 | 纯 C++ 内部结构（C# 经参数传递不 blit、不入哈希）——零镜像零回放影响 |
| yami 素材模板再分发 | MIT + THIRD_PARTY 批③已登记；模板 README 复述来源/许可声明 |
| 向导模板拷贝中断（半成品目录） | 目标目录不存在才创建（既有防重入）；拷贝失败清目录返回空 |
| 模板项目 GUID 与资产 GUID 混淆 | 模板 README + 向导注释双声明：资产 guid 稳定、project.lemon guid 重生成 |

## 25. 验证命令（批④完工口径）

```bash
ctest --test-dir build/mac --output-on-failure                        # 单测＋布局探针＋脚本测试
# 金回放零重录（零 schema 变化 + 新通道不入哈希 → 不重录）
build/mac/Samples/bench-sim/lemon-bench-sim --frames 3600 --replay build/goldens/m5b2-sim-mt.txt
build/mac/Samples/bench-sim/lemon-bench-sim --frames 3600 --threads 1 --replay build/goldens/m5b2-sim-st.txt
build/mac/Samples/bench-script/lemon-bench-script --frames 1800 --replay build/goldens/m5b2-script.txt
build/mac/Editor/lemon-editor --bench-survivor --frames 900           # 守门跑 ×3（本批不动播种）
build/mac/Editor/lemon-editor --smoke-template --frames 1800          # 模板链路单跑（风筝断言需 1800 帧）
tools/editor-regression.sh full build/mac                             # 13/13（新增 template 步）
```

**完工记录（2026-09-23）**：判据 1–4/6 满足——ctest 3/3（engine-tests **13158**
持平；script-tests **1477**，+17：TestSaveChannelAndUiCards——SaveChannel 编解码
往返/坏档拒收/边界 + C# Save/Ui 完整版/Confirm 位探针（typeId 6 表尾））；**判据 2
金回放零重录兑现**（m5b2 三档 replay mismatches=0：零新组件/零布局改动 + 新通道
SaveChannel/RtUiCards 全不入 StateHash——批②推论"schema 一字不动 = 旧档即证"
的第二次机械验证）；`--smoke-template` PASS（HUD 四要素/存档载入回显/波次行/
172 击杀/1 升级/卡片出现→选择→隐藏/saveFile）+ editor-regression full **13/13**；
bench-survivor ×3 **60/55/66 fps PASS**（守门跑，三跑逐位一致）。**判据 5 真人
10 分钟一局验收待用户执行**。执行勘误三处 + 过程发现两项（SDK 字符串跨界 UTF-16
截字节→改 `CopyUtf8`；Rescan 目录序不确定→两段式排序入账；冒烟玩家站桩死亡链
→切向轴注入风筝；⑤模板 spriteId 基号陷阱（生成器写死 100 vs 管线
程序化图集+1=104——生成器挪 viewport 后同规则取 base 重生成，无 manifest 也
逐位可复现）；多脚本 scripts[] 未落地→单 PlayerBehaviour 规避重排 M6）——
细节见 DevLog 同日条目。ADR-011 落笔（`docs/ADR/ADR-011-Play-Mode-Tweak-
Disposition.md`）。

**批④后修（2026-09-23 用户反馈轮，提交后追加）**：①**脚本域跨局残留**——
Stop→Play 后环绕刃每局 +2：`Behaviours.Slots`/`Events.Subscribe` 为 C# 静态态，
ExitPlay 弃 playWorld 不清、EnterPlay 同实体 id（快照确定性重排）重 Attach =
同实体双实例双 tick。修 = `lemon_play_reset`（EnterPlay 硬清实例/订阅/热重载包，
类型注册表保留；bench/回放不经 = 金档零扰动，m5b2-script replay 仍
mismatches=0）。②**AssetBrowser HiDPI 末列裁剪**——列数按硬码 92 换列 vs 主题
实测节距 72+24k（displayScale），末列溢出且无横向滚动可达；修 = 实测节距换列 +
文件名 `PushTextWrapPos` 钉宽。回归：ctest 3/3（script-tests 1485，+8
TestPlayDomainReset）+ 金回放三档 mismatches=0 + regression full 13/13。用户
问询三项（编辑器动画创建 / 新技能作者路径 / 波次数值外置配置表）登记 08 §M6。

**批④后修③（2026-09-23 用户实测 demo/svr-test：玩家/怪物全不显示）**：换项目
spriteId 基号漂移——`OpenProjectPipeline` 基号取注册表现存计数+1，而注册表跨
项目累计不复位；同会话第二个项目打开 → 全体 id 后移上个项目精灵数（实测 +31）
→ 场景烘焙引用悬空。修 = 换项目注册表复位（Reset+Build+ClearPages+ImGui 重绑，
设备重建同配方），基号恒定；用户项目删 manifest 重开自愈（hero 116==116）；
回归防线 = smoke-template 同进程第二拷贝记账全等断言。regression 13/13。
细节 DevLog 同日条目 + 06 §2 修订注记。
