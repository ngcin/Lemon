# 批⑧：LoadSceneAsync——分帧状态机 + AsyncSceneLoad + 加载屏样例 + 回放激活帧契约

Status: done ✅（2026-10-08 机器面 / 2026-10-09 回归复验收口。**D1–D3 用户未应答按推荐推进待追认**：D1=A staging 暂存+激活帧原子集成 / D2=A Resolve·Assets 双段占位（Assets 段实际承担 DOM 分帧回收——实测驱动补件）/ D3=A 跨同步/异步单槽 last-wins。出口判据全落：单测 **34,716**（+133）/ script-tests **1,818**（+16 TestSceneAsyncSdk）/ ctest 4/4 / 回归 full **21/21**（首跑 19/21 两红 = 并发 worktree 编译负载噪声，安静复跑全绿）/ scene-smoke 五跳版绿 / bench 门禁双跑 ≥76.5 / **金回放三档 mismatches=0**（worktree@5f7cffb 录档→批⑧ 回放）/ 构建零警告。压测（20k 实体×4 组件/4ms 预算）：staged 帧 21–23 帧单帧 ≤6.6ms（多跑带）、Parse 原子帧 ~114–126ms、激活帧 12.9–16.4ms、对照同步单帧 373–505ms。[DevLog](../../DevLog/2026-10-08-m7c-b8-loadscene-async.md)。下一批 = 批⑨ 消费者迁移）

- 日期：2026-10-08
- 关联：[ADR-017](../../ADR/ADR-017-Scene-Management-And-LoadScene.md) D3（统一管线 / 分帧状态机 / 确定性契约）· [b7 批文件](./2026-10-08-b7-sdk-scene-facade.md)（vtable 56 槽 / SceneSourceHooks / F1 失败契约登记项 = 本批定文案）· [b6b 批文件](./2026-10-08-b6b-scene-switch-orchestration.md)（SceneSwitcher 编排 / F3 afterBuild）· [M7c.md](./M7c.md) 批⑧ 行
- 性质：ADR-017 批⑧——批⑥/⑦ 换场核心与 SDK 门面之上的异步装载管线。**一条管线两个门面**（D3：同步 LoadScene = "预算不限、下帧立即激活"特例；不存在两套装载代码——同步路径 Execute 逐字节保持，async 与其共核分节）。

## 1. 现状核实（2026-10-08 探测）

| # | 事实 | 对本批的含义 |
|---|---|---|
| 1 | `SceneSwitcher::Execute`（SceneSwitcher.cpp:21-103）单帧完整协议：预检→清场（D2 除 DDOL 全清）→sweep→BuildInto→打标→档案翻转→afterBuild→三事件 | async 激活段与步骤 ①–⑦ 共核；④ 装载位换集成（D1 裁决） |
| 2 | `SceneArchive::BuildInto` = ParseSceneDoc（解析+迁移+校验）+ ApplySceneName + BuildEntities（两遍：建槽全量→逐实体 ReadEntity） | Parse 段 = ParseSceneDoc；Build 段 = BuildEntities 分帧化；nlohmann 红线（只进 .cpp）→ 预备档 pimpl |
| 3 | Hierarchy 是注册组件、四字段全 EntityRef（ComponentCatalog.cpp:45-47）| 集成段句柄重映射可走 FieldMeta 泛型遍历（含数组段 elemFields），零逐组件手写 |
| 4 | ComponentMeta 有 `sizeOf/readFn/emplaceFn`（ComponentRegistry.h:92-127）；ScriptBox **不入** ComponentRegistry | 组件 memcpy 集成 = 注册表元数据驱动；ScriptBox 特例显式拷 |
| 5 | 实体句柄 = entt 32 位（含 version 位；Scene.h:118-119），回收槽 version+1 | 同步/异步激活等价 ⇒ 集成段必须**复刻同步槽位分配序列**（含坏条目"建N毁k"路径），句柄才逐位一致 |
| 6 | vtable 56 槽（批⑦ 尾加后）；SceneEventKind 三值（Events.h:36-41）；`lemon_scene_event` 导出直推 | 本批尾加 3 槽（56→59）+ kind=3 AsyncCompleted（载荷复用：oldHandle=opId、newHandle=新句柄/0=失败终态） |
| 7 | `<progress>` 原生元素 + `SetAttr value/max` 通道已通（hud.rml 在用；UiSubsystem.h:187 探针）| 加载屏样例零 UI 系统改动 |
| 8 | 回放双流 = InputState + StateHash（bench-sim）；⑥c 孪生世界两测先例（TestSceneSwitchDeterministicTrajectory）| 回放激活帧契约用例 = 孪生形态扩展：A 异步多帧 ∥ B 同步（记录激活帧处 Request）→ 逐帧哈希全等 |
| 9 | 编辑器 Play 世界与 lemon-game 均已装配 Switcher hooks（sweep/afterBuild，批⑦ T5/T4） | async 机住 SceneSwitcher → 两宿主 hooks 自动生效，零编辑器改动 |
| 10 | membership 不入哈希流 / StateHash 只吃主 Scene | **staging 独立 registry ⇒ 预备段期间哈希流逐位不变**——"加载期间旧场照常 tick + 回放可同步重放"的机械基础 |

## 2. 设计裁决点（开工闸）

### D1 预备段架构：staging registry + 激活帧原子集成（核心拍板）

分帧状态机各段的工作落点。**张力**：Build 分帧要求实体逐步构造；但主 registry 逐步长入 = ① 系统管线/渲染看见半成品（旧场照常 tick 被破坏）② 加载帧哈希流漂移（回放同步重放不可能逐位一致）。

- **方案 A（推荐）staging 暂存 + 原子集成**：Parse→Build 建进**独立暂存 Scene**（async 机持有；主世界逐位不动、旧场照常 tick、哈希流不变）；门开后在**单个 Essential 窗口**执行 = 清场（协议 ①–④ 原码）→ **集成**（按暂存台账在主 registry 复刻 BuildInto 槽位序列：doc 序建槽全量 → 组件 memcpy + EntityRef 重映射 → 坏槽销毁提交）→ 打标 → 档案翻转 → afterBuild → 事件（⑤ 原码 + kind3 收口）。集成 = create+memcpy（无 JSON 解码）≈ Build 成本的零头，激活帧有界；Awake/Start 归激活帧（Unity 同构；游戏侧重初始化游戏侧分帧 = 模板样例职责）。**句柄逐位一致的结构保证**：集成复刻同步路径的分配序列（同清场、同建槽序、同坏槽回收）。
- 方案 B 预解析 + 激活帧全量 BuildInto：状态机只分帧 Parse（+未来 Assets），Build/Resolve 全量落激活帧。实现最简（无 staging/无集成代码）但 **Build 不分帧**——大场景激活帧尖峰 ≈ 同步路径全额，"分帧压测"验收名存实亡，ADR D3 段列表（Build 为独立分帧段）不兑现。

### D2 Resolve/Assets 段 v1 口径

ADR D3 段列表含 `Resolve（脚本槽/GUID 归一/SpriteRef）`。实测依赖面：GUID 归一/SpriteRef 预解析依赖宿主资产源（新 hook 面）；脚本槽**实例创建**（ResolveSlotBehaviour→Awake）只能发生在最终句柄上（staging 句柄 ≠ 最终句柄，预挂 = D1 否决理由同源的幽灵实例）。

- **方案 A（推荐）双段占位**：Resolve/Assets = 状态机占位段（Assets 原文本就占位；Resolve v1 = 秒过）；GUID/SpriteRef 归一留在激活帧 afterBuild（与同步路径同码、幂等）。压测数字落账后，激活帧若超预算再升级 stageResolve 钩子（登记后手，不预建面）。
- 方案 B 本批做满：SceneSwitchHooks 加 `stageResolve(Scene& staging)` 钩子，两宿主各装配 SpriteRef 预归一 + 脚本槽 typeId 预映射。hooks 面扩张 + 边际收益小（Awake 大头反正在激活帧），压测未证伪前属过度设计。

### D3 在途请求冲突语义（单槽统一）

- **方案 A（推荐）跨同步/异步单槽 last-wins + 取消不回调**：任一新请求 WARN 后取代在途者（同步 Request 取消在途 async；async 覆盖 sync pending / 在途 async）；被取消 op：completed **不推**（Unity 无取消概念——单槽裁决的代价，WARN 响亮交底 + 文档钉"被取代 op 语义 = 已取消"）。
- 方案 B 排队（Unity 多 op 队列形态）：与既有单槽 last-wins 纪律（⑥b 设计定案）冲突，v1 Single 唯一装载下队列无消费者语义，不做。

## 3. 任务清单（文件/行级）

| # | 文件 | 动作 |
|---|---|---|
| T1 ✅ | `Engine/Serialization/SceneArchive.{h,cpp}` | `StagedSceneBuild`（pimpl 全在 .cpp，nlohmann 红线不破）：`Parse(jsonText)->unique_ptr`（nullptr=失败，ValidateParse 同链）/ `EntityCount()` / `Name()` / `CreateSlots(Scene&, maxN)`（doc 序建槽分帧）/ `DecodeEntities(Scene&, maxN)`（逐实体 ReadEntity 分帧；坏条目暂存槽即时回收、台账保留原句柄）/ `Ledger()`（doc 序全槽账——集成段复刻 + EntityRef 全槽重映射依据，有效性 = staging.Alive() 判据）/ `CreatedCount/DecodedCount`（进度权重面）/ `ReleaseDocChunk`（Assets 段 DOM 分帧回收——实测驱动补件）。ParseSceneDoc/ReadEntity 零改动复用 |
| T2 ✅ | `Engine/ECS/SceneSwitcher.{h,cpp}` | ① `AsyncSceneLoader` 状态机（Phase: Parse→BuildCreate→BuildDecode→Assets→Gate→Idle；墙钟预算默认 4ms `SetBudgetMs`；Parse 段原子不可分帧——落账头注）；② `IntegrateStaged`（台账复刻：doc 序建槽全量→注册表元数据 memcpy + EntityRef/数组段重映射 + ScriptBox 特例→坏槽销毁+Commit）；③ Execute 共核分节（步骤 ①–⑦ 提私有段，同步路径行为逐字节不变——孪生测/金回放机械复核）；④ 激活 = 清场原码 + 集成替代 BuildInto + `SceneEventKind::AsyncCompleted`（Events.h 尾加 =3）收口推送；⑤ 单槽统一（D3：Request/AsyncRequest 互斥取消 + WARN）；⑥ `Query(opId, *progress, *isDone)`（在途/终态缓存双命中；门关 progress 封顶 0.9；终态 1.0）+ `SetActivation(opId, allow)` |
| T3 ✅ | `Engine/Scripting/ScriptHost.{h,cpp}` | vtable 尾加 3 槽（56→59，全低频、基准场零调用零漂移）：`sceneLoadAsyncRequest(const char*, uint8_t)->uint32`（opId；0=失败——mode 拒/寻址红字在先；经 SceneSourceHooks 同步寻址） / `sceneAsyncSetActivation(uint32, int32)->int32` / `sceneAsyncQuery(uint32, float*, uint8_t*)->int32`；Native 三实现（g_world 域线程窗口同既有槽） |
| T4 ✅ | `Engine/Scripting/dotnet/Lemon.SDK/NativeApi.cs` | 表尾镜像 3 槽 + Native 包装（旧宿主判空降级：opId 0 / 查询 false / 门设丢弃） |
| T5 ✅ | `Engine/Scripting/dotnet/Lemon.SDK/SceneManager.cs` | `LoadSceneAsync(string, LoadSceneMode = Single)->AsyncSceneLoad`（Additive = 红字 + 无效 op）；`AsyncSceneLoad` readonly struct（opId 句柄；`progress`/`isDone` = native 查询；`allowSceneActivation` get=静态镜像 set=native；`completed` **自定义 add/remove → 静态注册表**（struct 值拷贝下事件可用的唯一形态——订阅状态集中存，kind3 推送时按 opId 触发+注销）；`GetAwaiter()` 自定义 awaiter（IsCompleted=native / OnCompleted=completed 一次性包装——**域线程同步续跑**，无线程池跳Hop）；`OnNativeSceneEvent` kind3 分发（completed 后于 sceneLoaded/activeSceneChanged——订阅方可查新场）；`PlayReset` 扩（注册表+镜像清空） |
| T6 ✅ | `Engine/Entry/GameEntry.cpp` | smoke-scene 第 5 跳异步（C++ 直发 `Switcher().AsyncRequest`，宿主端到端走 hooks 全链）：默认门直通——终态七面断言 + `isDone`/`progress==1.0` + 门关变体（0.9 封顶 + 世界不动帧断言 + 开门激活） |
| T7 ✅ | `tests/engine/SceneTests.cpp` | 三测：① `TestSceneAsyncMachineContract`（小档一帧效=同帧激活；大合成档+低预算多帧推进 + **逐帧断言预备期主世界哈希不变**（staging 零可见性）+ progress 单调/0.9 封顶/终态 1.0 + completed 恰一次（假 backend 记 kind 序：Unloaded→Loaded→ActiveChanged→AsyncCompleted）+ 零孤组/DDOL 稳定/档案翻转）；② `TestSceneAsyncReplayFrameContract`（孪生：A 异步低预算观测激活帧 M ∥ B 于 M-1 帧 Request 同步——**逐帧 ComputeStateHash 全等**（含加载帧与激活后 N 帧）+ 激活后实体句柄集合相等——"回放走同步路径+记录激活帧"契约机械证明）；③ `TestSceneAsyncPressure`（20k 实体×4 组件合成档 + 4ms 预算：staged 帧每帧墙钟 ≤ 预算+容差、激活帧墙钟/总帧数/对照同步单帧墙钟——**数字入 DevLog**，无硬门禁） |
| T8 ✅ | `tests/script/main.cpp` + `dotnet/TestScript/TestScript.cs` | `TestSceneAsyncSdk` + `AsyncProbeBehaviour`（typeId 表尾）：C# 契约面——op 返回/progress 样本单调/门关 0.9 封顶跨帧 + 开门激活/completed 恰一次计数/**await 续跑点**（Mark 序证域线程同步续跑）/completed 晚于 sceneLoaded（log 全序）/被取代 op WARN 语义/Additive 红字 op 无效；引擎面对拍：终态 active/零孤组/句柄记忆化不撞 |
| T9 ✅ | `Templates/vs-survivor/` | 加载屏样例：`Assets/UI/loading.rml`（`<progress id="load-bar">` 原生元素）+ `Game/LoadingScreen.cs`（DDOL 根实体 + code-mounted 文档 origin=CSharp 跨场幸存 + Update 轮询 progress 驱动 SetAttr + **游戏段权重混合进度样例**（引擎段 ×0.8 + 自报段 ×0.2 注释样例）+ 完成收尾自毁）；README 用法段（`await SceneManager.LoadSceneAsync(...)` 与轮询两形态）；初始化器样例（sceneLoaded 后分帧铺 spawn）**归批⑨** svr-test 实测后回填 |
| T10 ✅ | `docs/EngineDesign/04-CSharp-Scripting.md` | §3 扩：LoadSceneAsync/AsyncSceneLoad 面 + 进度契约（单调/0.9 封顶/completed 恰一次/**禁挂 progress 分支**——呈现量非确定性量）+ await 域线程续跑 + 失败契约（见 §5）+ v1 口径（单在途/取消语义/被取代 op） |
| T11 ✅ | `docs/EngineDesign/03-ECS-Runtime.md` + `docs/ADR/ADR-017` | 03 §12 扩 async 确定性口径（staging 零可见性 = 加载帧哈希不变；激活帧记录 + 同步重放等价；Parse 段原子注）；ADR-017 验收映射批⑧ 行落账 + D3 落地注（staging 架构 / Resolve·Assets v1 占位口径 / D3 裁决） |
| T12 ✅ | （复验） | 构建零警告 / lemon-tests 只增不减（基线 34,583）/ script-tests 扩项 / ctest 4/4 / 回归 full 21/21（scene-smoke 五跳版）/ bench-survivor 门禁 ≥76.5 / 金回放三档抽验零重录（vtable 59 尾加 + 组件 id/系统序零动） |
| T13 ✅ | （回写） | DevLog 新条目（含压测数字）/ M7c.md 批⑧ 行勾销 / AGENTS.md 状态行 |

## 4. 进度权重与门契约（验收判据的机械定义）

- progress = `0.9 × staged 工作完成度`：Parse 10% / Build 80%（建槽 5% + 解码 75%，按实体数）/ Assets+Resolve 5%（占位即完成）→ 门开激活跳 1.0。
- `allowSceneActivation=false`：staged 完成后**停在 0.9**（isDone=false，世界逐位不动，每 Essential 重查门）；置 true 后下一 Essential 激活 → 1.0 + isDone + completed。
- 契约测试断言面：progress 采样序列单调不减 / 门关期间样本恒 ≤0.9 / isDone=true ⇔ progress==1.0 / completed 恰一次 / completed 晚于 sceneLoaded。
- **响亮规则（ADR D3 原文转正进 04 分册）**：玩法逻辑只许挂 sceneLoaded/isDone/completed；progress = 纯呈现量（预算依赖、跨机器不确定），分支违约 = 已知敞口（契约测试 + 文档，无机器强制——Unity 同级）。

## 5. 失败契约（批⑦ F1 登记项本文案收口）

| 失败位 | 行为 | 世界状态 |
|---|---|---|
| 请求期（寻址失败 / Additive / 空名） | opId=0 返回 + 红字（宿主 resolveScene / mode 拒） | 逐位不动 |
| Parse 段（坏 JSON / 迁移失败 / 段校验） | 红字 + op 终态（isDone / completed 推 kind3 newHandle=0） | 逐位不动（预检在 staging——同步路径 ParseFailed 同构，世界从未被碰） |
| 集成段极端失败（预检过仍坏） | 与同步路径 BuildInto 极端失败**同族敞口**（⑥b 设计定案③）：世界已清场、新场未完整，红字 + 档案 isLoaded=false | 半空（既有已知敞口，两门面共口径、共文案） |

## 6. 红线自查（开工前对齐）

- vtable 只尾加不插队（56→59）；组件 id / 系统注册序 / 系统子流零动 → 金回放零重录预期成立；基准场（bench-sim/bench-script/金档）零 async 调用 = 零漂移。
- 同步 `Execute` 行为逐字节不变（共核重构 = 纯结构提取；孪生测 + 21 步回归 + 金回放三抽验机械复核）。
- membership 三不入（注册表/序列化/StateHash）沿袭；staging Scene 上的实体**永不打标**（集成后主 registry 单点 StampSceneMembership——与同步同码）。
- OnDestroy 通知只经 `NotifyPendingDestroys`（清场段原码复用 = 红线自动保持）；staging 内销毁（坏条目）无脚本无通知面（staging 实体从未 Resolve 脚本）。
- nlohmann/json 只进 SceneArchive.cpp（StagedSceneBuild pimpl）；Vulkan 零涉及；C++/C# 边界 = vtable + kind3 事件两条既有通道类。
- `NotifyPendingDestroys`/`CommitDestroys`/`Destroy` 本体零改动。

## Review 轮（2026-10-09 收口后全量自查）

四发现当日修：F1 C# 注册表取消路径残留（s_inFlight 代收清退）/ F2 集成段无名档 SetName("") 分歧（HasName 条件覆写，ApplySceneName 同构）/ F3 模板 Begin 双调孤儿驱动（代收上一驱动）/ F4 终态缓存被在途 op 遮蔽（Query 去 Idle 条件）。确认面与观察项明细见 [DevLog](../../DevLog/2026-10-08-m7c-b8-loadscene-async.md) Review 轮节。修后全绿：单测 34,716 / script-tests 1,818 / ctest 4/4 / scene-smoke OK / 模板编译过。

## 7. 出口判据（对齐 ADR-017 批⑧ 行）

大场景分帧压测预算实测入 DevLog（staged 帧超预算即红）+ progress 契约测试（单调/封顶/completed 恰一次/await 续跑）+ **回放 async 装载同步回放逐位一致**（孪生哈希全等用例）+ 单测/script-tests 只增不减 + ctest 4/4 + 回归 full 21/21 + bench 门禁不降 + 金回放三档抽验零重录 + 构建零警告。
