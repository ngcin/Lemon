# 批⑨：消费者迁移——svr-test 拆多场景（MainMenu + Grass/Volcano）+ RunSweeper 退役 + 加载屏接入 + 模板随迁

Status: done ✅（2026-10-09 机器面。**D1–D5 用户未应答按推荐推进待追认**：D1=A code-mount 壳+DDOL 种子+重装自毁守卫 / D2=A 战斗场自含 / D3=A 键盘位（UI 指针入流登记候选池）/ D4=A 模板两场景 / D5=A async 进场+同步回菜单。出口判据全落：单测 34,716 逐位不变（引擎内核零改动）/ script-tests 1,818 / ctest 4/4 / game-smoke OK（uidoc=7 code-mount 恰值）/ scene-smoke OK（CSharp 幸存者双断言新口径）/ **template-chain OK = 批⑦ 敞口① 收口** / smoke-guid OK / RunSweeper 全仓代码零引用 / 回归 full 21/21（首跑四红 = 机器级既有敞口——smoke 帧数窗按 60Hz 标定 vs 显示器睡眠无节流 500fps+，本批顺手修复 = smoke 会话 18ms 帧率下限，HEAD 基线同败实证非本批引入）/ 构建零警告。**真人走查待用户**。[DevLog](../../DevLog/2026-10-09-m7c-b9-svr-test-multiscene.md)。下一批 = 批⑩（可选）或 M7c 收尾转 M8）

- 日期：2026-10-09
- 关联：[ADR-017](../../ADR/ADR-017-Scene-Management-And-LoadScene.md) 批⑨ 行 · [b6b](./2026-10-08-b6b-scene-switch-orchestration.md)（SceneSwitcher 编排/清场）· [b7](./2026-10-08-b7-sdk-scene-facade.md)（SDK 门面；§4 两条敞口本批收口）· [b8](./2026-10-08-b8-loadscene-async.md)（LoadSceneAsync + 加载屏样例）· [M7c.md](./M7c.md) 批⑨ 行
- 性质：批⑥–⑧ 换场引擎核心 + SDK 门面 + 异步管线的**首次真实项目落地**——svr-test（用户活项目）拆多场景、RunSweeper 游戏侧补丁退役、加载屏样例消费、vs-survivor 模板随迁；批⑦ 登记的两条敞口（编辑器内换场端到端机器驱动 / UI 点击换场不可回放）本批处理。

## 1. 现状核实（2026-10-09 探测）

| # | 事实 | 对本批的含义 |
|---|---|---|
| 1 | svr-test 单场景形态：entryScene=`Scenes/MainMenu.scene`（project.lemon:6）；MainMenu.scene = 4 个 UIDocument 实体（UI_Menu/Pause/Settings/Results，仅 menu showOnStart=1）+ Flow 种子实体（GameFlow）；战斗 = EnterRun 清场握手后 `Instantiate.Prefab` 挂 Player/Director——全部同一场景组 | 拆场主体：流程壳留 MainMenu、战斗内容进 Grass/Volcano 新场景 |
| 2 | `Game/GameFlow.cs` 363 行 = 状态机 + 设置/音量 + RunSweeper（:327–362，12 tag 批扫 + SweepArmed/SweepObserved 握手 :27–29/:49–60） | 瘦身主体；行数对比入 DevLog（出口判据） |
| 3 | 换场 sweep **无条件**卸 origin=Scene 文档（`UiSubsystem.cpp:832 UnloadDocumentsByOrigin`——不感知 DDOL） | 跨场 UI 必须走 code-mount（`UI.Show` 通道 B = origin=CSharp 跨场幸存；通道 B 以 relPath 现载 `UiSubsystem.h:27`——批⑧ loading.rml 样例形态，`LoadingScreen.cs:42`） |
| 4 | DDOL API = `LemonBehaviour.DontDestroyOnLoad(gameObject)` 静态（GameObject.cs:178；C++ 找根 + 非根 WARN 作用于根树） | GameFlow 种子跨场幸存的挂点 |
| 5 | 模板生成器 = `Editor/Templates/VsTemplateGen.cpp`（2076 行）：Game/*.cs **内嵌源串合成**（GameMain:647 / GameFlow:766 / PlayerMovement:1137 / PlayerCombat:1163 / PlayerHud:1572）、场景经 OpenScene+Save 构建、entryScene 写死 `"Scenes/Main.scene"`（:1847–1850）、README 内嵌串（:1869+）。`Templates/vs-survivor` = 生成产物入库；svr-test 已分叉为用户活项目（手改，**不重生成**） | 模板随迁 = 改生成器内嵌串 + 场景构建 + 重生成入库；svr-test = 手改 |
| 6 | 机器消费面三条：template-chain smoke = `--smoke-template` 3400 帧（`EditorAppSmokeTpl.cpp`——entryScene 断言写死 `"Scenes/Main.scene"` :169–175、种子开 Main.scene :188–189、play 驱动 btn-start 点击 + ScriptBox/Health 世界级断言）；game-smoke = lemon-game `--smoke` 900 帧（`GameEntry.cpp:816–924`，frame 150 指针直灌点击进局）；scene-smoke = 合成夹具五跳（`GameEntry.cpp:399–420`，首跳自 SceneB 起，**不解析模板场景文件**） | 前两条 = 编辑器/独立运行时换场端到端机器证（批⑦ 敞口① 收口面）；第三条零波及（改 entryScene 不影响）；smoke-template 的 entryScene 断言与种子路径必须随迁 |
| 7 | bench-survivor = 独立播种临时项目（`EditorAppSmoke.cpp:106 SeedBenchSurvivorScene`）——与模板/svr-test 场景零耦合 | 零波及 |
| 8 | Input 语义位面：Confirm=bit5（映射 R 键「确认/重开」Input.cs:17）/ Pause=bit6（Esc/P）/ Attack=bit4（Space）——键盘路径全在 InputState = 可回放；UI Click 事件在输入流外（批⑦ 敞口②） | 敞口② 修法落点（D3） |
| 9 | 表权威：WaveDirector.waves 清零、waves.tab 唯一权威（`WaveTableLoader.cs:8–9`）；表 GUID = C# const（:24） | Volcano 变体 = const→虚属性 + 子类 + 新表 |
| 10 | .meta 手工可造（`{"guid","type"}` 最小面——waves.tab.meta 实证）；场景文件无 .meta（按路径>唯一 stem 解析） | 新表/新 rml 可手落；新场景纯文件 |

## 2. 设计裁决点（用户未应答按推荐推进，待追认）

- **D1 跨场壳形态（核心）**：**code-mount UI + DDOL GameFlow 种子 + 重装自毁守卫**——四屏流程文档从 UIDocument 场景实体改为 GameFlow 代码装载（通道 B 现载，origin=CSharp 跨场幸存）；GameFlow 种子 Awake 自标 DDOL；MainMenu 重装时新种子 Booted 守卫自毁（静态 + StateBag 随热重载）。否决备选：A) Boot 独立场景两跳（入口多一跳 + 项目打开落在近空场景，编辑器 UX 差）；B) UIDocument 实体标 DDOL（origin=Scene 仍被 sweep 无条件卸载——事实 #3，不可行）。
- **D2 战斗场景内容形态**：**自含实体**（Player + Director(+载表脚本) 内联进 Grass/Volcano.scene——Main.scene 先例）vs sceneLoaded 后分帧铺 spawn 初始化器（批⑧ T9 登记样例）。推荐自含：svr-test 战斗场 ≤3 实体、零初始化握手；分帧铺 spawn 指引已在 LoadingScreen.cs 头注 + 04 分册，代码样例待真实大场景消费者（M9 tilemap）再落（登记 M9 注）。
- **D3 UI 点击换场回放敞口修法**（批⑦ 候选二选一）：**键盘位**（候选 A）——菜单/结算的换场动作补语义位边沿（Menu：R=开始·草地 / Space=开始·火山；Results：R=重开 / Esc=回菜单；Pause Esc 既有）——入 InputState = 确定性回放，两场全可回放。UI 指针入流（候选 B）= 引擎面改动（InputState schema → 回放格式 → 金档重录风险），登记候选池待真实需要（playtest 回放采集）再上。
- **D4 模板随迁范围**：MainMenu + Grass 两场景（模板单战斗场即足演示形态；Volcano 第二战斗场 = svr-test 游戏内容不随迁）。
- **D5 换场门面分配**：进战斗（menu 开始 / 结算重开）= `LoadSceneAsync` + `LoadingScreen.Begin`（样例消费 + smoke 机证 async 端到端）；回菜单（暂停/结算 tomenu）= 同步 `LoadScene`（小场景无屏闪）。

## 3. 任务清单（文件/行级）

| # | 文件 | 内容 |
|---|---|---|
| T1 | `demo/svr-test/Game/GameFlow.cs` | 重写：删 RunSweeper（:327–362）+ 握手（:27–29/:49–60）+ Spawning 态；`EnterRun(scene)` = `LoadingScreen.Begin`（记 `s_battleScene`）；`SceneManager.sceneLoaded` 订阅（订阅归 GameMain.Configure = UI.Events.Subscribe 同生命周期，热重载随域重建重订）→ GameFlow.OnSceneLoaded（MainMenu→Menu+show menu；Grass/Volcano→Run+`Time.Scale=1`）；DDOL 种子（Awake 自标 + Booted 守卫 + StateBag 随包）；键盘位边沿（D3）；回菜单 = 同步 LoadScene("MainMenu") |
| T2 | `demo/svr-test/Game/GameMain.cs` | 删 RunSweeper 注册（:33）；Configure 加 sceneLoaded → GameFlow.OnSceneLoaded 路由 |
| T3 | `demo/svr-test/Game/WaveTableLoader.cs` | const kWavesTable（:24）→ `protected virtual string TableGuid`；新增 `VolcanoTableLoader` 子类（新表 GUID） |
| T4 | `demo/svr-test/Game/LoadingScreen.cs` + `Assets/UI/loading.rml`(+.meta) | 批⑧ 模板样例适配拷贝（theme.rcss 类名/变量核对 svr-test 主题；新 meta GUID 查全仓无碰撞） |
| T5 | `demo/svr-test/Assets/UI/menu.rml` | btn-volcano 按钮（data-event="start-volcano"）+ hint 行更新（R/空格/Esc 键位） |
| T6 | `demo/svr-test/Scenes/`：MainMenu.scene / Grass.scene / Volcano.scene | MainMenu 删 4 UIDocument 实体（留 Flow 种子）；Grass/Volcano 新建（Player + Director(+VolcanoTableLoader) 内联；实体 GUID 新发；name 字段 = Grass/Volcano） |
| T7 | `demo/svr-test/Assets/tables/waves_volcano.tab`(+.meta) | 更难变体（快坡度/FastMob 提前/Boss 提前；复用既有 prefab GUID；数值 = 占位待用户调） |
| T8 | `demo/svr-test/README.md` | 流程段更新（多场景 + 键位 + 编辑器直开战斗场 = 裸战斗无流程壳——与今日 Play Main.scene 同口径） |
| T9 | `Editor/Templates/VsTemplateGen.cpp` | 内嵌 GameFlow 源串同款重写（code-mount 壳 + DDOL 种子 + LoadSceneAsync 进场/同步回菜单）；GameMain 源串（订 sceneLoaded/删 RunSweeper 注册）；场景构建改 MainMenu（壳）+ Grass（自 Main 迁）；entryScene（:1847–1850）与 README 串（:1869+）更新 |
| T10 | `Templates/vs-survivor/`（重生成产物） | 生成器重跑 + 入库 diff 核对（.prefab/.meta GUID 与 spriteIdBase 规则不动） |
| T11 | `Editor/App/EditorAppSmokeTpl.cpp` | entryScene 断言 → `"Scenes/MainMenu.scene"`（:169–175）；SmokeTplSeedScene 开 MainMenu.scene（:188–189）；play 驱动逐 stage 断言核对（世界级查询天然跨组；async 激活多 2–3 帧容差核对） |
| T12 | `Engine/Entry/GameEntry.cpp` | game-smoke 点击进局后激活帧容差核对；scene-smoke 零波及复核（事实 #6）；**Engine 内核零改动红线** |
| T13 | （验证） | dotnet build ×2（svr-test/模板）+ lemon-tests 34,716 只增不减 + script-tests 1,818 + ctest 4/4 + 回归 full 21/21 + 构建零警告 + GameFlow 瘦身行数对比表（svr-test/模板两侧）入 DevLog；金回放：引擎 vtable/组件零动 → 零重录预期，注明跳过实测的依据 |
| T14 | （回写） | DevLog 新条目 / M7c.md 批⑨ 行勾销 / AGENTS.md 状态行 / 真人走查清单（menu R·点击 → Grass 加载屏 → 战斗 → 死亡复活 → 结算 → R 重开 → Esc 回菜单 → 火山；编辑器内同流程） |

## 4. 出口判据（对齐 M7c.md 批⑨ 行 + ADR-017 批⑨ 行）

- svr-test 真人全流程走查过（上列清单；编辑器 + lemon-game 两宿主）；
- GameFlow 瘦身行数对比入 DevLog（价值量化；svr-test 与模板两侧）；
- 回归 full 21/21 + 单测 34,716 只增不减 + ctest 4/4 + 构建零警告；
- RunSweeper 全仓零引用（svr-test / 模板 / 生成器内嵌串三处grep证）；
- 批⑦ 敞口①（编辑器内换场端到端机器驱动）= template-chain smoke 过；敞口②（UI 点击换场不可回放）= 键盘路径落地 + 候选 B 登记候选池。

## 后修（2026-10-09 真人走查首轮，[DevLog](../../DevLog/2026-10-09-m7c-b9-post-fix.md)）

- **实报①火山不刷怪**：VolcanoTableLoader 漏注册（场景脚本需 `Behaviours.Register`）→ GameMain 补。
- **实报②Stop→再 Play 全灭**：`Booted` 用户静态跨局存活（`lemon_play_reset` 清不了用户静态）误杀新局种子 → SDK 新公共面 `Lemon.Events.PlayResetHook`（跨局复位广播）+ GameFlow/LoadingScreen 静态构造订阅自清（svr-test + 模板内嵌串两侧；`s_driver` DDOL 句柄同 bug 类一并清）。
- **连带挖出：lemon-game runtime 暂存陈旧**（POST_BUILD 只随重链触发，SDK .cs 单独重编不刷新 exe 旁 runtime/——本例新 API 缺方法炸静态构造的表象即由此来）→ staging 改每构建跑的 `lemon-game-runtime` custom target。
- 诊断小改：Behaviours 构造器异常红字打全异常链。
- 验证：单测 34,716 / script-tests 1,818 逐位不变、game-smoke OK、template-chain OK、svr-test boot 零异常零契约红（类型 9 = 注册面含 VolcanoTableLoader 实证）。
- 登记候选：smoke-template 双 Play 段（stop→replay→menu 再显）机器钉板归批⑩/收尾批。

## Review 轮（2026-10-09 真人复测过后全量自查）

- **F1（P1 健壮性，当日修）EnterRun 失败软锁**：`LoadingScreen.Begin` 返回 op 无效（场景改名/删档）时两 GameFlow 均忽略——St 恒 Loading、入口屏已隐、BGM 在播、世界冻结且无恢复路径。修 = op 无效即回菜单态 + StopBgm 自愈（svr-test + 模板内嵌两侧）。
- **F2（P3，当日修）孪生实现分叉**：svr-test OnSceneLoaded 缺「未知场景隐菜单」默认分支（模板有）——补齐对齐。
- **F3（P3，当日修）注释风格**：4 处 `///` 混入 `//` 块（合法但噪声）——清理；Events.PlayResetHook 捕获日志 Message→全异常链（与 Behaviours 同口径）。
- 确认面（无缺陷）：Events.PlayResetHook（锁外快照广播/逐订阅隔离/静态构造订阅每域一次）；LoadingScreen Begin 代收 + 跨局复位；CMake staging（custom_target ALL + DEPENDS 次序，copy_if_different 无变化廉价 no-op）；smoke pacing 谓词（排除 bench/交互/smoke-close 正确）；GameEntry/SmokeTpl 断言面（回归实证）；prefab 实体 guid 重生成轮换（语义惰性——资产 guid/spriteId 不动，smoke-guid 实证）。
- 验证：复测模板 game-smoke OK（uidoc=7）/ template-chain OK + editor-smoke PASS / svr-test boot 零错误 / script-tests 1,818 / 构建零新警告。

## 5. 红线自查（开工前对齐）

- **Engine 内核零改动**（GameEntry smoke 面可动；若 smoke 断言需要内核让步 = 设计错误回 D1–D5 重议）；vtable 59 / 组件 id / 系统序零动 → 金回放零重录。
- 确定性：换场走 SDK 同步/异步两门面（批⑥⑧已证确定性执行）；键盘位入 InputState；UI Click 路径保持人用（非回放口径，文档注明）。
- 模板 GUID 纪律：.prefab/.meta 勿手改；重生成走生成器（spriteIdBase 规则不变）。
- svr-test = 用户活项目：只动流程/场景/表结构，玩法数值（Volcano 占位除外）与既有场景（Main/ani/UiTest/Perf10k/Battle 测试场）不碰。
