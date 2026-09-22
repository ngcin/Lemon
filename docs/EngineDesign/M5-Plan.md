# M5 实施计划 —— 玩法 + vs-survivor 模板（08 §2）

> 滚动计划：每批开工前分解到文件/行级，完工后本页勾销并回写 DevLog。
> **批⓪（战斗闭环）已于 2026-09-22 完成**（实测数字见 DevLog 同日条目；执行中的
> 两处计划勘误：Projectile 实际 48B 非 56B；bench-sim 旗标为 `--frames` 非 `--steps`，
> 二进制路径 `build/mac/Samples/bench-sim/lemon-bench-sim`）。**批① 已于 2026-09-22
> 完成**（§6–§10 分解 + 完工记录见 §10 末；执行勘误：Pickup 密核扫描实测 1.87ms
> 超预估 0.2~0.6ms，登记 09 §6.10 观察项不扩 scope；smoke-ui 飘忽为既有问题）。
> 批②–④ 待分解。
> 关联：09 §6.10（bench-survivor 性能台账）、09 §7（确定性回放口径）、
> DevLog 2026-09-22（P0 iFrames 无递减 + M5 支撑度评审）。

## 0. 批次总览

| 批 | 主题 | 内容概要 | 状态 |
|---|---|---|---|
| ⓪ | 战斗闭环 | iFrames 递减（P0）＋ 命中记忆/穿透收口 ＋ 战斗参数组件化 ＋ bench-survivor 战斗化 | **✅ 完成 2026-09-22** |
| ① | 成长闭环 | Collectible 磁吸系统 ＋ Pickup 事件发射 ＋ XP 入账 ＋ Game RT UI 最小通道 ＋ timeScale | **✅ 完成 2026-09-22** |
| ② | 导演波次 | DirectorSystem 波次表（数据驱动）＋ WaveStart 事件 | 待分解 |
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
