# M5 批②：导演波次 —— 计划与完工记录

Status: done（2026-09-23 勾销）

> 拆分自 M5 总计划（2026-09-23 收口），章节编号沿用原文件；同批事件与实测数字见 [DevLog 2026-09-23 条目](../../DevLog/2026-09-23-m5-b2-director-waves.md)。

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
180/s、波前移 t=1/6/11s 后 PASS（本文件 §12 T4 注已按此口径）。

---
