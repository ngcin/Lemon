# M2 代码复审检查清单（Review Checklist）

> 2026-09-19 · M2 复核轮产出。约定：**本轮 Engine 零修改**（一行未动），只新增测试
> （tests/engine_tests.cpp 复核节 21 组 / +1276 checks）与本文档。
> 证据测试标注 `[ISSUE-n]` 的断言固化的是"当前行为"；若未来修复，须同步改断言并更新
> §4 状态列。上轮（提交后审计轮）11 项修复见 DevLog 同日条目，其遗留讨论项并入 §4.2。

---

## 0. 结论 TL;DR

- **M2 验收判据维持成立**：修复后 18000 帧双档回放双 PASS（上轮）、bench-sim ≤8ms、
  bench-mow 无回退；本轮 Engine 未改动，判据不受影响。
- **登记问题 8 个，已于 2026-09-19 修复轮全部修复并验证**（§4.1 状态列已更新）：
  ISSUE-5（Flee 特性完全失效）与 ISSUE-2（恶意档驱动越界读写）为 P1，ISSUE-2 曾由
  ASan 探针实锤（Systems.cpp:501 heap-buffer-overflow，见 §7.1）；修复后三套
  sanitizer 12829 checks 全绿、旧档兼容 + 新档双档回放 4×PASS（§7.2）。
- **分层纪律全部合规**（§2，grep 实证）：entt 封装、Vulkan 零泄漏、nlohmann 收敛、
  随机源统一，无违例。
- **测试面**：12821 checks（Release / ASan+UBSan / TSan 三套全绿零报告）；
  覆盖矩阵见 §5，剩余缺口见 §6。

---

## 1. 代码结构地图（M2 范围，行数含头文件）

| 模块 | 文件 | 行数 | 职责 | 依赖（向下） | 被谁依赖 |
|---|---|---|---|---|---|
| Core 基础件 | `Core/Random.h` | 77 | PCG32 确定性随机（seed,stream 子流） | Math | World/Systems/bench |
| | `Core/JobSystem.h/.cpp` | 84+131 | 工作窃取池 + ParallelFor + 单线程档 | FunctionRef | World |
| | `Core/FunctionRef.h` | 60 | 非拥有可调用视图 | — | JobSystem/SpatialHash |
| | `Core/Pool.h` | 71 | 槽位池（Acquire 复用/DeferredRelease） | Log | 粒子层(M1)/后续 |
| | `Core/RingQueue.h` | 89 | 2 的幂环形队列（事件容器） | Log | World(Events) |
| ECS 内核 | `ECS/Entity.h` | 30 | u64 句柄（entt+1 偏移，0=null） | — | 全部 |
| | `ECS/Scene.h/.cpp` | 124+59 | EnTT 封装：两阶段销毁/空组件特判/Each | EnTT(仅此层),SpatialHash | Systems/Samples |
| | `ECS/World.h/.cpp` | 105+34 | 全局服务：管线/事件/子流RNG/Team/输入/边界 | JobSystem,Random,RingQueue… | Samples/未来编辑器 |
| | `ECS/SystemPipeline.h/.cpp` | 59+90 | 三阶段 + Kahn 拓扑序 + 逐系统画像 | Log | World/Systems |
| | `ECS/ComponentRegistry.h` | ~120 | FieldMeta/ArraySegMeta 反射注册表 | Log | Catalog/Archive/Hash |
| | `ECS/TeamTable.h` | 62 | 32×32 对称关系矩阵（零值=Ghost） | Log | World/Systems |
| | `ECS/StateHash.h/.cpp` | ~20+80 | 逐字段 FNV-1a 状态哈希 | Registry | bench-sim 回放 |
| | `ECS/Events.h` `ECS/Input.h` | 35/17 | 48B POD 事件包 / 输入快照 | Entity | World/桥(M3) |
| 组件目录 | `Components/*.h` + `ComponentCatalog.cpp` | 441 | 27 组件全 POD + 登记表（含 3 数组段） | Core/Math,Entity,Registry | Systems/Archive/Hash |
| 物理查询 | `Physics2D/SpatialHash.h/.cpp` | 97+199 | 确定性 cell 排序哈希：圆/盒/射线/点 | FunctionRef,Scene | Systems |
| 系统管线 | `Systems/Systems.h/.cpp` | 181+609 | 16 系统（12 实现+4 占位）+ TargetBoard | Components,Scene,World,Physics | World 安装 |
| 序列化 | `Serialization/SceneArchive.h/.cpp` | ~40+250 | .scene v1 JSON 存取 + 迁移链骨架 | Registry,nlohmann(仅.cpp) | 测试/M4 编辑器 |
| 验收程序 | `Samples/bench-sim/main.cpp` | 279 | 无窗口模拟 + 录制/回放 + 看门狗 | ECS/Systems 全栈 | 08 §3 验收 |
| 单测 | `tests/engine_tests.cpp` | 1759 | 12821 checks（M1+M2+复核节） | 全部 | CI/验收 |

**依赖方向验证**：Samples→ECS/Systems→Components/Physics→Core，无反向引用；
无 DI 容器；Meyers 单例仅 ComponentRegistry（登记表，非服务定位器）。

## 2. 分层纪律核查（grep 实证，2026-09-19）

| 纪律 | 检查方法 | 结果 |
|---|---|---|
| EnTT 头只进 ECS 封装层 | `grep -rln 'entt/' Engine Samples tests --include=*.h` | ✅ 仅 `Engine/ECS/Scene.h` |
| entt:: 符号不泄漏到业务层 | `grep -rln 'entt::' Samples tests` | ✅ 空（.cpp 内部使用限 Scene/SpatialHash/Systems 实现文件） |
| Vulkan 类型零泄漏（头文件） | `grep -rln 'Vk[A-Z]\|vk' Engine --include=*.h`（排除 Renderer） | ✅ 空 |
| nlohmann 只进 SceneArchive.cpp | `grep -rln nlohmann Engine Samples` | ✅ 仅该 .cpp |
| 随机源统一（禁 std::rand 等） | grep 禁用清单 | ✅ 空（唯一命中是 Random.h 的注释本身） |
| 命名空间/扩展名约定 | `lemon::` / `Lemon.*` / `.scene` / `.lemon` | ✅ 合规 |
| 第三方登记 | Luma(JobSystem) / nlohmann / EnTT 均在 THIRD_PARTY.md + 07 矩阵 | ✅ |

## 3. 模块不变量检查清单

图例：✅ 有测试且过 · 👁 审计确认 · ⚠️ 有缺口（见 §6）· ❌ 已登记问题（§4）

### 3.1 Core

| # | 不变量 | 状态 | 测试/证据 |
|---|---|---|---|
| C1 | PCG32 输出位级确定（golden 值锁定） | ✅ | TestRng（4 个 golden 值） |
| C2 | Range 拒绝采样无偏差、Float01 高 24 位 | ✅ | TestRng |
| C3 | JobSystem 并行/单线程档全下标恰一次 | ✅ | TestJobSystem + TestVerifyParallelForCoverage（质数 997/块 64 双档） |
| C4 | 窃取队列空闲休眠（queuedTasks 谓词，不忙等） | 👁 | TSan 零报告佐证 |
| C5 | 任务内禁嵌套 ParallelFor（死锁约束） | 👁 | 头文件注释文档化；无作业图需求（§6 建议静态断言/调试检测） |
| C6 | RingQueue 扩容保序 / 硬上限丢弃计数 | ✅ | TestRingQueue + TestVerifyRingQueueGrowOrder（16→1024 连续扩容 FIFO） |
| C7 | Pool 槽位复用/计数正确 | ✅ | TestPool + TestVerifyPoolSlotReuse |
| C8 | Pool::Release 幂等 | ⚠️ | 无防护（双归还=槽位别名）；API 纪律注记 §4.3-N4 |

### 3.2 ECS 内核

| # | 不变量 | 状态 | 测试/证据 |
|---|---|---|---|
| E1 | 实体回收版本号：旧句柄失效、新句柄可用 | ✅ | TestVerifyEntityRecycleAndVersion |
| E2 | AliveCount 精确（created−destroyed） | ✅ | 同上 + TestVerifySaveExcludesDestroyed |
| E3 | Each() 不遍历死亡槽位（tombstone 过滤） | ✅ | 上轮修复 + 两个 Verify 测试端到端 |
| E4 | 两阶段销毁：Destroy 幂等/当帧可访问/帧首提交 | ✅ | TestSceneLifecycle + TestVerifyProjectileLifetime（提交节奏） |
| E5 | Destroy 多线程安全（并行系统可调） | ✅ | TestConcurrentDestroy + TSan 零报告 |
| E6 | DestroyQueueTag 打标→提交移除 | ✅ | TestDestroyQueueTagLifecycle |
| E7 | 空组件（tag）Emplace/Get/TryGet 特判 | ✅ | TestComponentRegistry/ForEach 路径 |
| E8 | 系统拓扑序确定（同秩按注册序） | ✅ | TestSystemPipelineOrder |
| E9 | 每系统 RNG 子流独立且稳定 | ✅ | TestVerifySystemRngStreams |
| E10 | 子流槽 ≥256 后的行为 | ⚠️ | N5：静默槽位冲突（当前 16 系统安全；验证轮更正原误引的 ISSUE-9） |
| E11 | TeamTable 对称性/零值 Ghost/越界 Neutral | ✅ | TestTeamTable + TestVerifyTeamRangeSafety |
| E12 | World 构造装默认敌我表 | ✅ | 上轮修复 #2 + e2e |
| E13 | 无活动场景 Step = 空步 | ✅ | TestWorldStepWithoutScene |
| E14 | StateHash：重复稳定/裸实体不可见/变化可检 | ✅ | TestVerifyStateHashStability |
| E15 | StateHash 无 padding 依赖（逐字段宽度） | 👁 | FieldWidth 表审计 |
| E16 | AddSystem 重名防线生效 | ✅ | 修复轮：重名查 systems_（ISSUE-3） |
| E17 | RunStage 前置 ResolveOrder 的安全性 | ✅ | 修复轮：入口断言长度一致（ISSUE-3） |

### 3.3 组件目录 / 序列化

| # | 不变量 | 状态 | 测试/证据 |
|---|---|---|---|
| A1 | 27 组件全 trivially copyable | ✅ | M3-0（2026-09-19）起头内有 static_assert 布局冻结（sizeof+trivially_copyable）；此前仅测试内运行期断言，本行初版"static_assert 于头内"表述超前于代码 |
| A2 | roundtrip 不动点 | ✅ | TestSceneArchive（二次存档逐字节相等） |
| A3 | EntityRef 前向引用 remap / null 保持 | ✅ | TestSceneArchive + TestVerifyNullEntityRefRoundtrip |
| A4 | 数组段（active/items/relicIds）内容保真 | ✅ | TestVerifyArchiveArraySegAndRuntimeFields |
| A5 | RT 字段不入档 | ✅ | 同上（iFrames/cooldown 断言） |
| A6 | 输出按句柄排序（Save 幂等） | ✅ | TestSceneArchive |
| A7 | 未知组件跳过 / 坏 JSON 拒绝 | ✅ | TestSceneArchive |
| A8 | 字段类型错不抛穿（try/catch 降级） | ✅ | TestArchiveMalformedTolerance |
| A9 | 版本防线：缺失拒绝/未来版本拒绝 | ✅ | TestVerifySchemaVersionGuards |
| A10 | 版本字段类型安全 | ✅ | 修复轮：显式判型（ISSUE-1），负数/浮点同步拒绝 |
| A11 | 数组段 count 恒 ≤ 容量（读档后） | ✅ | 修复轮：Load 按段表钳制 + warn（ISSUE-2） |
| A12 | Migrate 迁移链 | ⚠️ | v1 无历史，Migrate 不可达（设计预留，§4.3-N3） |
| A13 | EntityRef 指向不存在索引 → 字段跳过 | 👁 | ReadField 返回 false 静默（建议加 warn，§6） |

### 3.4 空间哈希

| # | 不变量 | 状态 | 测试/证据 |
|---|---|---|---|
| P1 | cell→id 双关键字排序命中序确定 | ✅ | TestSpatialHash（命中序断言） |
| P2 | 圆/盒/点查询计数与过滤（team/layer/exclude） | ✅ | TestSpatialHash + TestVerifyBoxQueryAndStaleEntries |
| P3 | 越界 team/layer 静默不命中 | ✅ | TestSpatialHashRangeClamp（上轮 #5 回归） |
| P4 | 销毁后重建前：valid 过滤不误报 | ✅ | TestVerifyBoxQueryAndStaleEntries（惰性语义固化） |
| P5 | Raycast：命中距离/法线/maxDist/背面/退化 | ✅ | TestVerifyRaycast |
| P6 | Raycast 注释"id 最小优先"跨 cell 语义 | ⚠️ | N10：实为 cell key 序（文档漂移；验证轮更正原误引的 ISSUE-10） |
| P7 | 每帧重建 O(n log n) 预算内 | ✅ | bench-sim 0.18ms@10k |

### 3.5 系统管线（16 系统）

| # | 不变量 | 状态 | 测试/证据 |
|---|---|---|---|
| S1 | InputSnapshot/Director/Navigation/CSharp 占位空跑 | ✅ | e2e/管线序测试 |
| S2 | Spawn 配额 maxAlive 有界稳定 | ✅ | TestVerifySpawnQuota |
| S3 | 普查计数语义（含同队非怪实体） | ⚠️ | §4.3-N1（"约"语义，文档说明） |
| S4 | Chase 追击/keepRange/失目标归零 | ✅ | TestVerifyAISystemChase |
| S5 | TargetBoard 最近邻确定性（严格 <） | ✅ | 实现审计 + 回放 PASS |
| S6 | Flee 反向逃逸 | ✅ | 修复轮：exclude 自身（ISSUE-5），逃逸方向+保持速度双断言 |
| S7 | Patrol 端点折返 | ✅ | 修复轮：折返同帧改向（ISSUE-6） |
| S8 | Separation 对称/密度截断/确定性 | ✅ | TestSeparationForce + 回放 |
| S9 | Movement 积分/击退衰减/边界钳制 | ✅ | TestVerifyMovementKnockbackAndClamp |
| S10 | Hitbox 命中/穿透/iFrames/击退/Death | ✅ | TestNoDoubleDeathEvents + e2e |
| S11 | 同帧多源防双死 | ✅ | 上轮 #8 回归测试 |
| S12 | Trigger 差分 Enter/Exit + once | ✅ | 修复轮：触发器互不触发守卫（ISSUE-7）+ 共置回归 |
| S13 | Stat 状态到期压缩保序 | ✅ | TestVerifyStatEffectsAndXp |
| S14 | Stat xpToNext=0 终止性 | ✅ | 同上（回归防线） |
| S15 | Animator loop 有界推进 | ✅ | TestVerifyAnimatorAdvance |
| S16 | ProjectileLifetime 寿命/越界两帧提交 | ✅ | TestVerifyProjectileLifetime |
| S17 | ScriptEventDispatch 帧末清空 | ✅ | TestNoDoubleDeathEvents 断言 Size==0 |
| S18 | 无 SpawnFn 时告警且不崩 | ✅ | 修复轮：告警仅 Spawner 存在时（ISSUE-4） |

### 3.6 bench-sim / 回放

| # | 不变量 | 状态 | 证据 |
|---|---|---|---|
| B1 | 1 万怪 ≤8ms/步 | ✅ | 5.10ms（实录）｜7.37ms（后台负载） |
| B2 | 双档回放逐帧一致（含哈希函数变更后重录） | ✅ | 原证据（18000 帧 ×2）经 ISSUE-9 为**恒真**（空注册表哈希逐帧恒等）；M3-0 修复后重录真基线双档 PASS（§4.4） |
| B3 | 看门狗 EMA>250ms 自中止 | 👁 | 代码审计 + 单测不覆盖（需注入慢步，§6） |
| B4 | --frames 0 防御 / RESULT 退出码 | ✅ | 上轮杂项修复 + CI 式退出码约定 |

---

## 4. 问题登记

### 4.1 问题登记（8 项已于 2026-09-19 修复轮全部修复，验证见 §7.2）

| ID | 级 | 位置 | 描述 | 证据 | 修法建议 | 状态 |
|---|---|---|---|---|---|---|
| ISSUE-1 | P1 | SceneArchive.cpp `Load` | `schemaVersion` 为字符串时 `doc.value()` 抛 `type_error` 抛穿加载器（上轮 #7 只包了字段级读取，版本字段漏网；`"schemaVersion":"1"` 即触发） | TestVerifySchemaVersionGuards | contains+is_number_unsigned 显式校验替代 value() | 已修复（2026-09-19 修复轮） |
| ISSUE-2 | P1 | SceneArchive/StatSystem | 数组段 `count` 是普通序列化字段：档里只有 `{"count":200}`（无 active/items 键）时不受 ReadArraySeg 截断 → StatSystem::Tick 按 count 越界读写 StatusEffects.active[4]。**ASan 已实锤**（Systems.cpp:501 heap-buffer-overflow READ；8 实体小场景越界落池内、ASan 静默，1000 实体场景直接命中池边界）。范围修正：ComputeStateHash **安全**（读侧有 clamp，StateHash.cpp:63——本表初版误报）；Inventory.count 同路径但 M2 无消费方（休眠） | ASan 探针三段证据（§7.1）+ TestVerifyArrayCountClamped | Load 字段循环后按段表 clamp count；或把 count 从字段表移除、只由段驱动 | 已修复（2026-09-19 修复轮） |
| ISSUE-3 | P2 | SystemPipeline.cpp | ① AddSystem 重名断言查 profiles_（ResolveOrder 前为空）→ 恒过，防线空转；② 未 ResolveOrder 即 RunStage 时 profiles_[i] 越界 UB（Release 无 ENTT/容器断言） | 代码审计（Readonly） | 重名查 systems_；RunStage 入口断言 profiles_.size()==systems_.size() | 已修复（2026-09-19 修复轮） |
| ISSUE-4 | P3 | Systems.cpp SpawnSystem | 无工厂告警无条件触发：上轮 #11 外提检查时丢失"场景有 Spawner"前提，任何未注册 SpawnFn 的 World 首帧必告警（探针场景无 Spawner 亦复现）。附带：`warned` 是函数级 static——多 World 实例共享同一标志（第二个 World 的告警被吞） | /tmp 探针输出 | 告警移回"存在 Spawner 且配额未满"分支内；warned 改成员变量 | 已修复（2026-09-19 修复轮） |
| ISSUE-5 | **P1** | Systems.cpp AISystem Flee 块 | **Flee 特性完全失效**：`NearestAny(pos, range, Entity::Null())` 不排除自身 → 自己 d²=0 恒为最近"威胁" → away=零向量 → 速度恒零。bench-sim 无 Flee 实体故从未暴露 | TestVerifyFleeAndPatrol（固化现状） | exclude 传 `Scene::FromEntt(ent)`；顺带补"无威胁保持现速"断言 | 已修复（2026-09-19 修复轮） |
| ISSUE-6 | P3 | Systems.cpp Patrol 块 | 折返帧速度滞后一帧：先翻转 headingToB，vel 仍按翻转前 toD 归一化 → 过冲端点 ~1px 后才回头（60Hz） | TestVerifyFleeAndPatrol（固化现状） | 翻转后重算 dest/toD 再写速度 | 已修复（2026-09-19 修复轮） |
| ISSUE-7 | P3(设计) | Systems.cpp TriggerSystem | 触发器无层/组件过滤：重叠的触发器互为"非 ghost 进入者" → 开局即互发假 Enter（无需真实目标）、anyInside 恒真、Exit 永不产生（注释已计划 M5 资产侧过滤，属已知缺口，先登记） | TestVerifyTriggerOnceSemantics 注释 + 探针 | M5 过滤落地前：文档明示共置触发器禁用；或短期加 `!Has<Trigger2D>` 跳过 | 已修复（2026-09-19 修复轮） |
| ISSUE-8 | P3 | ComponentCatalog.cpp kEquipment | relicIds 同时登记为普通 UInt32 字段与数组段（同名键）：Save 先写标量后被数组覆盖（无害）；Load 字段循环对数组 `get<uint32_t>` 抛 type_error → 被字段级 catch 吞成 warn → **每次含 Equipment 的合法档读入必发一条假告警**（数据由 ReadArraySeg 正确读入，行为无损） | 探针 probe1（§7.1：合法档即告警 + relicIds=[1,2,3] 正确） | 从 kEquipment 字段表删 relicIds 一行（Save 输出不变，旧档兼容） | 已修复（2026-09-19 修复轮） |

### 4.2 上轮遗留（并入跟踪，见上轮汇报）

spike/03-csharp/dotnet 构建产物在版本库（建议 .gitignore+untrack，待定）；Hazard 半径
48 硬编码（M5 资产化补字段）；Spawner.cooldown RT 化后读档即触发一轮（语义可接受）。

### 4.3 设计注记（确认非缺陷 / 纪律提示，不修）

| ID | 内容 |
|---|---|
| N1 | Spawn 配额普查把同队非怪实体计入存量（bench alive=10002 = 10000 cap + spawner 自身 +1 的来源）；"约"语义已在头注释声明 |
| N2 | 击退离散积分比连续理想值大 ~7%（v0·dt/(1−e^(−decay·dt))）；exp 同源两侧一致不破回放，测试按离散值断言 |
| N3 | Migrate() v1 永不可达（迁移链骨架，风险台账 #7 的 CI 位）；首个 v2 落地时激活 |
| N4 | Pool::Release 双归还无防护（槽位别名）；Blob24 Save 依赖内存 NUL（读档路径保证）——引擎内部 API 纪律，登记备查 |
| N5 | World::SystemRng 槽位 256 按位掩码：systemId ≥257 静默与早流合流（确定性不破、独立性破）；M5 扩系统时改断言或扩容 |
| N6 | FastSin 负角度插值走段外推（误差 ≤~2× 正常 LUT 误差）；当前调用点 Angle()∈[0,2π) 全正，暂无害 |
| N7 | 跨平台回放不保证：std::exp/sqrt/ceil 依赖 libm；当前验收目标=同机逐帧（02/09 文档口径一致） |
| N8 | ResetProfiles 语义 = 峰值窗口重置（maxMs←lastMs，runs/total 不动），F3 刷新周期用；语义待 M4 面板时复核 |
| N9 | Scene::Emplace/Get 对空组件返回共享 static 可变引用——空类型无数据，调用方写入无意义；保持现状 |
| N10 | Raycast 退化方向（零向量）走点查询，hit.distance 保持哨兵 3.4e38——语义未文档化，建议 M4 补注释 |

### 4.4 M3-0 新登（2026-09-19，M3 开工盘点发现）

| ID | 级 | 位置 | 描述 | 证据 | 修法 | 状态 |
|---|---|---|---|---|---|---|
| ISSUE-9 | **P1** | `ECS/World.cpp` / `Samples/bench-sim/main.cpp` | `RegisterAllComponents()` 只有测试/探针显式调用，引擎内无自动调用点；bench-sim 漏调 → StateHash 遍历**空注册表**，逐帧哈希恒为 FNV 基数 `14650fb0739d0383`（未混入任何字节）→ **M2 回放验收恒真空转**（B2 原证据失效：录制与回放哈希恒等，mismatches=0 不含信息量）。M2 确定性结论本身经修复后复验仍然成立 | 实测：修复前 `--record 120 帧` 哈希去重数 = 1；修复后 = 120 | World 构造自动调用 `RegisterAllComponents()`（幂等守卫已有）；回归测试 TestVerifyWorldAutoRegistersCatalog；重录 18000 帧真基线双档回放 | 已修复（2026-09-19，M3-0；[ADR-010](../ADR/ADR-010-M3-Scope-Thread-RNG.md) 同批落账） |

## 5. 测试覆盖度矩阵（摘要）

| 面 | 测试 | 状态 |
|---|---|---|
| Core 数学/容器/池/RNG/Job | TestVec2…TestRingQueue（M1+M2 节）| ✅ |
| ECS 生命周期/服务/注册表 | TestSceneLifecycle/WorldServices/ComponentRegistry | ✅ |
| 存档 roundtrip/容错 | TestSceneArchive + Malformed + ArraySeg + NullRef + SchemaGuards + SaveExcludes | ✅ |
| 空间查询全 API | TestSpatialHash + RangeClamp + Box/Stale + Raycast | ✅ |
| 系统 16/16 | Spawn(S2)/AI(S4)/Flee(I5)/Patrol(S7)/Separation/Movement(S9)/Hitbox(S10,11)/Trigger(S12)/Stat(S13,14)/Animator(S15)/Projectile(S16)/Dispatch(S17)/Commit(S4)/占位×4(S1) | ✅（Flee 修复轮补全逃逸断言） |
| 确定性回放 | bench-sim 双档 18000 帧（上轮）| ✅ |
| 并发 | ConcurrentDestroy + ParallelFor 覆盖 + TSan 全套 | ✅ |
| 恶意输入 | Malformed + ISSUE-1/2 修复断言（负数/浮点版本、count 钳制） | ✅ |

## 6. 测试缺口与建议（后续轮次）

1. **看门狗触发路径**无测试（需注入慢步系统）——建议 bench-sim 加 `--watchdog-test`。
2. **Migrate 链**待 v2 落地时补"老档升级"测试（风险台账 #7 承诺）。
3. **EntityRef 悬垂索引**（"e999"）当前静默跳过，建议加 warn + 断言测试。
4. **Raycast 跨 cell 最近命中序**、OverlapBox probeRadius 边界值可再加密断言。
5. JobSystem 嵌套 ParallelFor 目前靠纪律——建议 Debug 档加"任务内 ParallelFor"检测断言。
6. 层级（Hierarchy）链路渲染合成在 M4 落地时需补环检测测试（文档已声明深度 ≤8 防环）。

## 7. 验证环境与复现

```bash
cd GameEngine/Lemon
cmake --build --preset mac && ./build/mac/tests/lemon-tests     # 12829 checks OK
# sanitizer 三套（构建目录 build/mac-san / build/mac-tsan 已配置）
cmake --build build/mac-san  && ./build/mac-san/tests/lemon-tests   # 0 报告
cmake --build build/mac-tsan && ./build/mac-tsan/tests/lemon-tests  # 0 报告
# 双档回放（上轮修复后重录重验）
./build/mac-audit/Samples/bench-sim/lemon-bench-sim --n 10000 --frames 18000 --record /tmp/m2-final.lrp
./build/mac-audit/Samples/bench-sim/lemon-bench-sim --n 10000 --frames 18000 --threads 1 --replay /tmp/m2-final.lrp  # PASS
./build/mac-audit/Samples/bench-sim/lemon-bench-sim --n 10000 --frames 18000 --replay /tmp/m2-final.lrp              # PASS
```

| 项 | 值 |
|---|---|
| 单测 | 复核轮 12821 → **修复轮 12829 checks OK**（Release + ASan/UBSan + TSan 三套，零报告） |
| Engine 改动 | 复核轮 0 行；修复轮 5 文件 8 项（见 §7.2） |
| 工作树 | 未提交（等用户过目后决定提交方式） |

### 7.1 验证轮记录（2026-09-19，对 §4.1 逐项复核）

用户要求对清单问题做"是否真实存在"的独立复核。结论：**8 项全部属实**（含补登的
ISSUE-8），其中 1 项范围修正。Engine 仍零修改；证据来自源码逐行核对 + Release 套件
复跑（12821 checks OK）+ ASan 探针（/tmp/probe_issue2.cpp，独立编译 Engine 源）：

1. **ISSUE-2 ASan 实锤**：`{"StatusEffects":{"count":200}}`（无 active 键）读入后
   StatSystem::Tick → `heap-buffer-overflow READ @ Systems.cpp:501`（1000 实体场景
   命中池末尾；分配栈 = Load→EmplaceComponent→entt 池）。8 实体小场景越界落在本
   分配内部、ASan 静默（越界写真实存在，只是不碰分配边界）。
2. **ISSUE-2 范围修正**：ComputeStateHash 读侧有 clamp（StateHash.cpp:63），初版
   清单"StateHash 越界"为误报，已更正；实际越界消费方仅 StatSystem。
3. **补登 ISSUE-8**：合法 Equipment 档读入必发 `field 'Equipment.relicIds' type
   mismatch skipped` 假告警（字段+数组段同名双登记），数据本身读入正确。
4. 引用更正：E10 行误引 ISSUE-9 → N5；P6 行误引 ISSUE-10 → N10（两编号不存在）。
5. 其余 6 项（1/3/4/5/6/7）源码逐行核对属实，描述与清单一致；其中 ISSUE-5 后果
   确认比初版表述更重：Flee 实体速度被主动清零（Normalize 零向量返回 Zero），
   且会覆盖 Chase 先写入的速度 → 带 Flee 的实体完全冻结。

### 7.2 修复轮记录（2026-09-19，用户批准"逐项修复验证"后实施）

**改动（5 文件 8 项，合计 ~25 行）**：

| 文件 | 修复 |
|---|---|
| `Serialization/SceneArchive.cpp` | ISSUE-1：`ReadSchemaVersion()` 显式判型（is_number_unsigned；超 u32 按"未来版本"拒绝），替换两处 `doc.value()`；ISSUE-2：Load 组件循环尾按段表钳制 count + warn |
| `ECS/SystemPipeline.cpp` | ISSUE-3：AddSystem 重名查 `systems_`（原查 ResolveOrder 前恒空的 profiles_）；RunStage 入口断言 `profiles_.size()==systems_.size()` |
| `Systems/Systems.h` + `Systems.cpp` | ISSUE-4：告警门控 `Pool<Spawner>().size()>0`，`warned` 改成员 `warnedNoFactory_`；ISSUE-5：Flee `NearestAny` exclude 传 `Scene::FromEntt(ent)`；ISSUE-6：Patrol 折返翻转后重算 dest/toD；ISSUE-7：Trigger 回调首行 `Has<Trigger2D>(other)` 跳过 |
| `Components/ComponentCatalog.cpp` | ISSUE-8：kEquipment 删 relicIds 普通字段（只由数组段驱动；Save 输出不变。StateHash 值变化仅影响含 Equipment 的场景——bench-sim 不含，实测旧档兼容） |
| `tests/engine_tests.cpp` | [ISSUE-n] 证据断言全部改为断言正确行为（Flee 逃逸方向/幅值/无威胁保持速度、Patrol 折返帧即改向、字符串/负数/浮点版本拒绝、count 钳 4 + StatSystem tick 安全）+ 新增共置触发器互不触发/真实访客双 Enter 回归块 |

**验证**：

| 项 | 结果 |
|---|---|
| 单测三套 | **12829 checks OK** ×3（Release / ASan+UBSan / TSan 零报告；复核轮 12821 → +8 修复断言） |
| 探针复跑 | probe1 假告警消失且 relicIds=[1,2,3] 正确；probe2 count 200→4、tick 后归 0、内存零改动；probe3 1000 实体 ASan 零报告（修复前同场景 heap-buffer-overflow） |
| 回放 | 旧档（修复前录制）双档 replay **PASS ×2**（mismatches=0，修复逐帧兼容）；新档重录后双档 replay **PASS ×2** |
| 性能 | bench-sim 18000 帧 avg **2.68ms**（多线程档）≤ 8ms 预算，无回退 |

顺带补齐：Load 迁移循环内重读版本失败时的显式拒绝（原经 value() 的类型异常路径
隐式失败；v1 不可达，防御性补齐）。
