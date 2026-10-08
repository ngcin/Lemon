# M7c 批⑦：SDK 门面——SceneManager/Scene/三事件 + DontDestroyOnLoad + vtable 尾加

- 日期：2026-10-08
- 关联：[批文件](../Plans/M7c/2026-10-08-b7-sdk-scene-facade.md) · [ADR-017](../ADR/ADR-017-Scene-Management-And-LoadScene.md)（C# API 面/D1–D8/换场帧协议）· [⑥b DevLog](./2026-10-08-m7c-b6b-scene-switch-orchestration.md)（前置①②登记）· [⑥c DevLog](./2026-10-08-m7c-b6c-replay-extension.md)（前置③④登记）
- 性质：批⑥ 引擎核心之上的 C# 门面。**四项设计裁决用户拍板"按推荐"开工**（批文件 §2）：D1 DDOL 根位式 / D2 清场判据「除 DDOL 系外全清」+ WARN / D3 三事件同步直推（IScriptBackend 虚方法 + Entry 导出）/ D4 验收双侧分工（script-tests 承 C# 语义 + smoke-scene 扩四跳承宿主端到端）。

## 实测数字（出口判据全过）

| 项 | 值 | 判据 |
|---|---|---|
| lemon-tests | **34,583 checks 全绿**（+23 = D1/D2/D3 三新测） | 基线 34,560 只增不减 |
| script-tests | **1,802 checks 全绿**（新增 TestSceneSdk：四跳全链含同名重装/事件序/时序/DDOL/红字拒/查询面） | — |
| ctest | 4/4 | — |
| 回归 full | 20/21 首跑（唯一红 = bench 噪声，见下）；scene-smoke 四跳版**首跑绿** | — |
| bench-survivor | 复跑 **fps=80/81 双绿**（≥76.5） | 门禁不降 |
| 金回放三档 | **mismatches=0 ×3**（HEAD 9e68553 worktree 录档 → 批⑦ 构建回放：sim-st threads=1 / sim-mt threads=4 / bench-script；sim 终态 alive=8249/created=16003/destroyed=7754 与 ⑥c 金档逐项同值 = 基线连续性旁证） | 零重录 |
| 构建 | 零编译警告（附带修 packager b1 遗留 `fonts` 计数未消费警告——批⑦ 头文件变动触发重编而现形，补对称日志消费） | — |

## bench-survivor 噪声定性（首跑 74 分的排除法）

首跑 74/77/72 低于门禁 → 结构分析（本批无逐帧改动面：七 vtable 槽基准场零调用、lineage walk 仅换场期、HookInstantiate 打标仅低频 Prefab 路径）→ **stash 基线对照**（HEAD 构建同负载三跑 83/83/80，本批复跑 80/81 同带；stepAvg 11.15 vs 基线 11.29 = sim 段零回归）→ 定性 = 机器负载噪声（当日 load≈6：ZCode 编译 + 浏览器；⑥c 的 87/88 为空闲机数字）。两次取优纪律取 80/81 绿记录。低分三跑的尖刺均在 sim 段且逐跑漂移（54/50/62ms max），非稳定增量。

## 落地面（批文件 T1–T16 全勾）

- **D1 根位式**：`MarkDontDestroyOnLoadTree` → `MarkDontDestroyOnLoad`（只标根 O(1)）；清场判据 = `IsDontDestroyOnLoadLineage`（自身或祖先带位，深度护栏同子树遍历）。后挂子实体随根幸存 / 移出随新归属清场（Unity 全对齐，TestDdolRootBitLineageSemantics 钉住）；`CountDontDestroyOnLoad` 语义收窄为根计数，幸存者全集 = 新 `CollectDontDestroyOnLoadLineage`。
- **D2 全清纯 DDOL 系**：`QueueDestroyAllExceptDdolLineage`（SceneClearReport：queued + unassignedCollected）——Single 语义本体，组 0 泄漏自愈且 WARN 响亮（⑥c 前置③ 裁决兑现）；`QueueDestroySceneGroup` 原语保留（判据同步祖先感知）。
- **D3 三事件同步直推**：`IScriptBackend::SceneEventNotify`（**非纯虚**默认空——Null/测试后端零波及）+ `Lemon.Entry` 新导出 `lemon_scene_event`（挂空安全）+ ScriptHost 侧 NativeApiWindow 包裹（订阅方回调内 native 查询可用）；SceneSwitcher::Execute 按协议⑤ 序推：Unloaded（sweep 后）→ Loaded（afterBuild 尾、同帧 Start/Update 之前）→ ActiveChanged（收口）。oldHandle==0 不推 Unloaded/ActiveChanged。
- **vtable 49→56**：sceneLoadRequest/sceneCount/sceneInfoAt/sceneInfoByHandle/activeSceneHandle/setActiveScene/markDontDestroyOnLoad 七槽尾加（register2 尺寸握手惯例）；`SceneInfoC`（332B，name[64]/path[256]）两侧镜像 static_assert 钉。**首个结构性 vtable 通道**——LoadScene 回放保障 = C# 确定性执行 + 哈希流捕获（op 不显式入流，03 §2 落地注③/§12 口径随本批修订）。
- **D4 寻址宿主钩子**：新 `SceneSourceHooks{resolveScene}`（进程级注册，EditorAssetHooks 同款纪律；引擎层不碰 IO）——GameEntry = AssetIndex+文件系统版（`ProjectFile::ResolveScene` 泛化：路径 > 唯一 stem > 空；`ResolveEntryScene` 变薄壳）；EditorApp = 同链 ad hoc 现读版；script-tests = 内存夹具版。`SceneArchive::SceneDocName` 新公共面（宿主填 req.name，无名档回落 stem）。
- **C# SDK**：`Lemon.SceneManager`（flat，D8）+ `Scene` readonly struct（isValid 计算属性；name/path 按句柄记忆化防逐帧分配，PlayReset 清——编辑器重进 Play 句柄从 1 重发）+ `LoadSceneMode` + 三静态事件（异常隔离逐订阅者）+ `LemonBehaviour.DontDestroyOnLoad(GameObject)` 静态（D7；C++ 侧找根 + 非根 WARN 作用于根树，D5）。Additive = C# 红字第一道 + native 兜底红字。
- **前置四项闭环**：① EditorApp `EnterPlayProgrammatic` 尾插 `Switcher().SetHooks`（sweep = UI origin=Scene 卸载；afterBuild = SpriteRef 归一 + ResolvePlayScripts（提为公共）+ MountSceneUiDocuments + Reconcile；捕获 this 与 playWorld 同生命周期，每次 EnterPlay 重装配）+ SceneSourceHooks 注册；② 两宿主 `HookInstantiate` 补 `StampTreeMembership(active)`（⑥b review"无宿主注册"措辞系过时——两宿主一直在册，注释修正）；③④ 即 D2/D1。
- **实现期发现（真缺口）**：F2 初始档案从未置 `isLoaded=true`（换场只翻新旧）——SDK 查询面首测即抓（sceneCount=0/GetActiveScene 无效）；GameEntry/EditorContext::EnterPlay/测试三处补齐。附带：GameEntry 帧尾裸 `CommitDestroys` 兜底删除（⑥b review 4 登记项，恒 no-op 零行为差异）；smoke-scene 单跳扩四跳（Main→B→C→**B 同名重装**→D，每跳七面 + DDOL 系精确清单 + 每载一档新句柄）。
- **验收分工落位（D4 决策）**：script-tests `TestSceneSdk`（SceneProbeBehaviour typeId 20 + 内存场景源 + 1801–1809 mark 链）承 C# 全语义——四跳/事件序全序对拍/**时序断言**（换场帧 Update 时事件已可见 = 协议⑤ 机器证明）/同名重装新句柄/坏名与 Additive 红字拒后世界不动/查询面/DDOL 跨四跳幸存；smoke-scene 四跳承宿主端到端。

## 已知敞口与登记

- **编辑器内换场端到端无机器驱动**：前置① 装配面 = 代码落地 + 镜像 GameEntry 既验证序列；机器驱动归批⑨ svr-test 迁移首批消费（模板 GameFlow 尚无 LoadScene 调用点，现造夹具 = 耦合用户项目，D4 裁决不做）。**真人手测已过（同日补验，见下节）**。
- **UI 点击触发的换场不可回放**（鼠标位不入 InputState——既有敞口，批文件 §4 登记；批⑨ MainMenu→Grass 迁移时处理）。
- `sceneCount`/GetSceneAt 按 isLoaded 档案过滤——DDOL 不建模伪场景（Unity 差异，04 §3 注记）；`SetActiveScene` v1 仅当前 active 合法（Additive 落地后放开）。
- rootCount = O(N) 组根扫描（查询面低频；逐帧轮询勿用——Scene 快照语义同 Unity）。

## 真人手测（2026-10-08 同日，编辑器内 Play 换场——前置① 闭环）

一次性夹具 `/tmp/lemon-b7-test`（vs-survivor 模板拷贝 + Test2.scene = Main 拷贝 + SceneHopProbe 探针：帧 180 `DontDestroyOnLoad` 自标 + `SceneManager.LoadScene("Test2")` + HOP!/ALIVE! 飘字信号）。用户 4 次 Play 往返，日志证据全绿：

- 每次 Play 中段出现第二条「UIDocument 装载：6 成功 / 0 缺失」——编辑器 `MountSceneUiDocuments` 仅两个调用点（EnterPlay 装配 + 批⑦ afterBuild 钩子），该行 = **换场钩子真实执行 + Test2 声明文档装载**（×4）；
- 第 3 次会话完整链：Main 未开局 → 换场 → Test2 菜单点开始 → Player/Director/Blade/Gem 正常生成 = **换场后世界干净可玩**；
- 全程零警告零红字（无 sprite 悬空 / 无未打标自愈 WARN / 无寻址失败）；「退 Play 逐字节一致 ✔」×4。

附带补强（手测发现的可观测性缺口，当日修）：SceneSwitcher 成功路径原为静默——补 `LEMON_LOG("scene switch: '旧' → '新'（清 N / DDOL 幸存 M / 新组 K）")` 一行（纯日志零行为变化；复验构建零警告 + 双测试套件 34,583/1,802 全绿）。

## 下一步

批⑧ LoadSceneAsync（分帧状态机 + AsyncSceneLoad + 加载屏样例 + 回放激活帧契约用例；ADR-017 D3）。
