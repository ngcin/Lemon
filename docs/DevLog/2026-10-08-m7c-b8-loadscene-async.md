# M7c 批⑧：LoadSceneAsync——分帧状态机 + AsyncSceneLoad + 加载屏样例 + 回放激活帧契约

- 日期：2026-10-08（机器面）/ 2026-10-09（回归复验收口）
- 关联：[批文件](../Plans/M7c/2026-10-08-b8-loadscene-async.md) · [ADR-017](../ADR/ADR-017-Scene-Management-And-LoadScene.md) D3 · [b7 批文件](../Plans/M7c/2026-10-08-b7-sdk-scene-facade.md)（F1 失败契约登记项 = 本批收口）· [b6b 批文件](../Plans/M7c/2026-10-08-b6b-scene-switch-orchestration.md)（SceneSwitcher 编排 = 本批共核）
- 性质：ADR-017 批⑧——"一条管线两个门面"的 async 面。**D1–D3 用户未应答按推荐推进待追认**：D1=A staging 暂存 + 激活帧原子集成 / D2=A Resolve·Assets 双段占位 / D3=A 跨同步/异步单槽 last-wins。

## 实测数字（出口判据）

| 项 | 值 | 判据 |
|---|---|---|
| lemon-tests | **34,716 checks 全绿**（+133 = 三新测） | 基线 34,583 只增不减 |
| script-tests | **1,818 checks**（+16 = TestSceneAsyncSdk） | 扩项通过 |
| ctest | 4/4 | — |
| 回归 full | 21/21（首跑 19/21 两红 = 并发 worktree 编译的机器负载噪声——安静复跑全绿） | — |
| scene-smoke | 五跳版首跑绿（第 5 跳 = async 门控变体：单调/哈希不变/0.9 封顶/七面） | 宿主端到端 |
| bench-survivor | 门禁双跑 ≥76.5 | 不降 |
| 金回放三档 | worktree@5f7cffb（批⑦ HEAD）录档 → 批⑧ 构建回放 **mismatches=0 ×3** | 零重录 |
| 构建 | 零编译警告（C++；C# 存量警告零新增） | — |

## 大场景分帧压测（出口判据主体；SceneTests::TestSceneAsyncPressure，20k 实体 × 4 组件合成档，预算 4ms）

| 量 | 值 | 说明 |
|---|---|---|
| staged 帧 | **21–23 帧**，单帧最大 **5.7–6.6ms**（多跑带；4ms 预算 + 单块粒度余量） | Build 分帧 + DOM 分帧回收（512 元素/块）；压测断言 = 3× 预算 sanity 天花板（墙钟门对 CI 噪声敏感——首轮回归即实录一次 6ms 硬门假红，按 09 §9 口径放宽；精确数字归本表） |
| Parse 帧 | 113.6ms | **原子段**（ADR D3 既定口径——单次 JSON 解析不可分帧；建槽首块同帧） |
| 激活帧 | **14.2ms** | 原子窗口：清场 + 集成（建槽 1.6 + 组件 memcpy 4.9 + EntityRef 重映射 3.7ms@20k）+ 打标 |
| 对照：同步单帧全量 | 372–505ms（两次跑） | 同档 LoadScene 单帧装载——激活帧约为其 1/26～1/35 |
| 引擎侧总账 | 异步全程 ≈0.21s 摊到 21+ 帧 vs 同步一帧 0.37s+ | 旧场全程照常 tick |

实现期两个实测驱动的补件：① 万实体档 nlohmann DOM 递归析构实测 ~58ms——整释会破预算（落到激活帧尾或独立 staged 帧都超带），改 **Assets 段分帧回收**（entities 数组尾部按块 erase，O(1)/元素）后单帧 ≤5.7ms；② 集成段组件池预 reserve（既有 `reserveFn/countFn` 元数据）——对 20k 档无感（瓶颈在 emplace 而非扩容），保留（语义正确零成本）。

## 架构落账（批文件 D1=A 兑现）

- **分帧状态机**（`AsyncSceneLoader`，住 SceneSwitcher；Essential #18 内 `Execute → TickAsync` 序）：Parse（原子）→ BuildCreate/BuildDecode（分帧建进**暂存 Scene**——独立 registry，主世界逐位不动、旧场照常 tick）→ Assets（占位 + DOM 分帧回收）→ Gate（`allowSceneActivation`，关 = 停 0.9 每帧重查）→ 激活（**单个 Essential 窗口原子执行**）。工作小一帧走完 = 与同步路径同帧效（script-tests fc2 断言）。
- **集成段**（`IntegrateStaged`）：按暂存台账（doc 序全槽账，含坏条目槽）在主 registry **复刻 BuildInto 槽位分配序列**——同建槽序、同坏槽"建 N 毁 k"回收、全槽 EntityRef 重映射（注册表元数据泛型遍历，Hierarchy 四 EntityRef 字段自动覆盖；ScriptBox 特例显式拷）⇒ 异步/同步激活**句柄逐位一致**（孪生用例机械证明，见下）。
- **回放激活帧契约**（`TestSceneAsyncReplayFrameContract`）：孪生世界 A（异步低预算，观测激活帧 M）∥ B（同步 Request 于帧 M、同帧 Essential 执行）→ **逐帧 ComputeStateHash 全等**（含加载帧——staging 零可见性的结构证明）+ 激活后实体句柄集合相等 + 激活后 10 帧锁步一致。
- **progress 契约**：Parse 0.09 / 建槽 0.045 / 解码 0.675 / Assets 0.09 → 0.9；门关封顶 0.9；激活 1.0；单调有测。**响亮规则**（04 分册）：progress = 纯呈现量，玩法分支只许挂 sceneLoaded/isDone/completed。
- **C# 面**：`AsyncSceneLoad` readonly struct（opId 句柄）——`completed` 走**静态注册表自定义 add/remove**（struct 值拷贝下事件可用的唯一形态；订阅已终态 op = 立即同步触发）；`GetAwaiter` 自定义 awaiter = **域线程同步续跑**（恢复点 = 激活 Essential 收口、先于当帧 Time.Advance——script-tests 断言续跑点读到上一帧号 = "激活帧内、Update 前续跑"的时序证明）；`PlayReset` 扩三注册表清空。
- **单槽统一**（D3=A）：同步 Request 取消在途 async / async 清除未消费 pending / async 取代 async——均 WARN；被取代 op completed 不推（script-tests fc10 断言 _c4 恒 0）。
- **失败契约**（批⑦ F1 登记项收口）：请求期拒 = opId 0；Parse 段失败 = 世界逐位不动 + 终态 + kind3（newHandle=0）；集成段无失败路径（预备段已过全部 JSON 校验）；BuildInto 极端失败 = 同步/异步两门面共口径共文案（设计定案③ 既有敞口）。
- **vtable 56→59 尾加三槽**（request/setActivation/query，全低频；基准场零调用零漂移）+ `SceneEventKind::AsyncCompleted`（kind3，载荷复用 oldHandle=opId/newHandle=新句柄或 0）——批⑦ D3 同通道类（lemon_scene_event 直推，非帧末队列）。

## 测试面

- SceneTests +133：`TestSceneAsyncMachineContract`（一帧效/门控 0.9 世界逐帧不动/多帧分帧 + 预备期哈希逐帧全等/失败契约四件/单槽三向互斥）/ `TestSceneAsyncReplayFrameContract`（孪生）/ `TestSceneAsyncPressure`（数字入本 DevLog）。
- script-tests +16：`TestSceneAsyncSdk` + `AsyncSceneProbeBehaviour`（typeId 21 表尾）：fc1-fc11 逐帧 mark 链——直通激活 + 事件全序（completed 晚于 sceneLoaded）+ 门控 0.9/开门终态 + await 续跑点（读到上一帧号）+ 取代取消 + Additive/坏名无效 op；引擎对拍：档案数/零孤组/DDOL 幸存。**实现期坑**：新 World 不清 C# 句柄记忆化 = 跨测试撞车（TestSceneSdk 缓存把本世界句柄 2 误映射 Grass）——测试起始补 `lemon_play_reset`（新 World = 新一局，编辑器重进 Play 同款口径；SDK 无缺陷）。
- scene-smoke 第 5 跳（宿主端到端，C++ 直发 `RequestAsync` + 门控变体）：RESULT 行扩 async 字段，回归第 21 步判定式不变。
- 加载屏样例：vs-survivor 模板 `Game/LoadingScreen.cs`（Begin/holdGate/ReportGameProgress + DDOL 驱动自毁）+ `Assets/UI/loading.rml`（code-mounted origin=CSharp 跨场幸存，`<progress>` 原生元素）+ README 用法段；注册不挂载（单场景现状零影响，消费归批⑨）；模板 Game.csproj 编译过 + 双 smoke（game/scene）绿。

## 文档收口

- [04 分册](../EngineDesign/04-CSharp-Scripting.md) §3：LoadSceneAsync 门面条（进度契约/await 时序/v1 口径/失败契约）。
- [03 分册](../EngineDesign/03-ECS-Runtime.md) §12：LoadSceneAsync 确定性条目（staging 零可见性 + 集成复刻槽位序列 + 孪生证明 + 压测数字指针）。
- [ADR-017](../ADR/ADR-017-Scene-Management-And-LoadScene.md)：D3 批⑧ 落地注（staging 架构/Resolve·Assets 占位口径/单槽）+ 验收映射批⑧ 行 ✅。

## Review 轮（收口后全量自查，四发现当日修）

- **F1（当日修）C# 注册表取消路径残留**：被取代 op 的 completed/awaitOnce/allowMirror 注册表项永不回调 ⇒ 订阅闭包跨 PlayReset 前累积。修 = `s_inFlight` 追踪 + 新请求发行时代收清退（`RetireInFlight`——正常终态路径 FireCompleted 已自清，此处只兜取消路径）。
- **F2（当日修）集成段档名覆写分歧**：档缺 "name" 段时同步路径 ApplySceneName 保持主场景现名、异步集成原实现无条件 `SetName("")`——两门面语义分叉（无名手写档才触发，现有测试档均有名故未现形）。修 = `StagedSceneBuild::HasName()` + 条件覆写（ApplySceneName 同构）。
- **F3（当日修）模板驱动孤儿**：重复 `LoadingScreen.Begin` = 引擎单槽取代旧 op（永不 isDone）+ 旧驱动实体常驻 tick 不自毁。修 = Begin 代收上一驱动实体（`s_driver.Alive → Destroy`）。
- **F4（当日修）终态缓存被在途 op 遮蔽**：`Query` 原要求 `phase_==Idle` 才查 doneOpId_ 缓存——新 op 在途时旧完成 op 查询回 unknown（progress 掉 0/isDone 掉 false，Unity 完成 op 恒可查的偏差）。修 = 去掉 Idle 条件（在途检查优先级在前，无冲突）。

确认面（抽查过，无缺陷）：TickAsync 激活后 afterBuild 内新请求的重入序（FinishActivation 护栏 + kind3 对旧 opId 推送仍正确——旧 op 的换场确实完成了）；Parse 失败路径 opId/mode 先捕再 FinishActivation；IntegrateStaged 坏槽"建 N 毁 k"与同步 BuildEntities 槽位序列逐位对应；EntityRef 全槽重映射（坏槽引用 = 已建后毁句柄，两路径同值）；editor play 世界装默认管线 = async 机在编辑器 Play 同样推进。观察项（不动）：await 同 op 双续跑后订覆盖先订（v1 单续跑语义，文档已注）；取消/取代帧承担 staging Scene 析构成本（罕见路径，DOM 已分帧、registry 拆池为廉价级）。

## 下一步

批⑨ 消费者迁移（svr-test 拆多场景 + RunSweeper 删除 + 加载屏接入 + vs-survivor 模板随迁 + UI 点击换场不可回放敞口处理）。
