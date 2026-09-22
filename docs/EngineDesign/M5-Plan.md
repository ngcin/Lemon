# M5 实施计划 —— 玩法 + vs-survivor 模板（08 §2）

> 滚动计划：每批开工前分解到文件/行级，完工后本页勾销并回写 DevLog。
> **批⓪（战斗闭环）已于 2026-09-22 完成**（实测数字见 DevLog 同日条目；执行中的
> 两处计划勘误：Projectile 实际 48B 非 56B；bench-sim 旗标为 `--frames` 非 `--steps`，
> 二进制路径 `build/mac/Samples/bench-sim/lemon-bench-sim`）。批①–④ 待分解。
> 关联：09 §6.10（bench-survivor 性能台账）、09 §7（确定性回放口径）、
> DevLog 2026-09-22（P0 iFrames 无递减 + M5 支撑度评审）。

## 0. 批次总览

| 批 | 主题 | 内容概要 | 状态 |
|---|---|---|---|
| ⓪ | 战斗闭环 | iFrames 递减（P0）＋ 命中记忆/穿透收口 ＋ 战斗参数组件化 ＋ bench-survivor 战斗化 | **✅ 完成 2026-09-22** |
| ① | 成长闭环 | Collectible 磁吸系统 ＋ Pickup 事件发射 ＋ XP 入账 ＋ Game RT UI 最小通道 ＋ timeScale | 待分解 |
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
