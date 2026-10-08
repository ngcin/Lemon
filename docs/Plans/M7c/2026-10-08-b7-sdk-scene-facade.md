# 批⑦：SDK 门面——SceneManager/Scene/LoadSceneMode + DontDestroyOnLoad + 三事件 + vtable 尾加

Status: done ✅（2026-10-08 机器面——**四项裁决用户拍板"按推荐"**：D1 根位式 / D2 除 DDOL 系外全清+WARN / D3 同步直推 / D4 双侧分工；单测 **34,583**（+23）/ script-tests **1,802**（含 TestSceneSdk 四跳全链）/ ctest 4/4 / 回归 full 20/21 首跑（唯一红 = bench 机器负载噪声，复跑 **80/81 双绿 ≥76.5**；stash 基线同负载 83/83/80 同带、stepAvg 11.15 vs 11.29 = sim 零回归）/ scene-smoke 四跳版首跑绿 / 金回放零重录抽验（见 §6 收口）/ 构建零警告（附带修 packager b1 遗留 fonts 计数未消费警告——头文件变动触发重编而现形）。[DevLog](../../DevLog/2026-10-08-m7c-b7-sdk-scene-facade.md)。编辑器内换场端到端（前置① 装配面）= 代码落地 + 镜像 GameEntry 既有序列，机器驱动归批⑨ svr-test 迁移首批消费）

- 日期：2026-10-08（批⑥ 收口同日预登记；本文件 = 开工前对齐包落档）
- 关联：[ADR-017](../../ADR/ADR-017-Scene-Management-And-LoadScene.md)（C# API 面 / D1–D8 / 换场帧协议）· [b6b 批文件](./2026-10-08-b6b-scene-switch-orchestration.md) Review 3（前置①②）· [b6c 批文件](./2026-10-08-b6c-replay-extension.md) Review F1/F2（前置③④）· [M7c.md](./M7c.md) 批⑦ 行
- 性质：ADR-017 批⑦——批⑥ 引擎核心之上的 C# 门面。消费入口 = SceneSwitcher（编排本体零改动，只接触发面）。LoadSceneAsync/AsyncSceneLoad 归批⑧，本批不声明死 API。

## 1. 范围与现状核实（2026-10-08 探测）

| # | 事实 | 对本批的含义 |
|---|---|---|
| 1 | vtable 49 槽（`ScriptHost.h:44-147`，register2 尺寸握手 + min 拷贝尾零——尾加旧宿主零扰动，多批先例） | 本批尾加 7 槽 → 56；全低频通道 |
| 2 | 批⑥b 编排完整落地：Request 单槽 last-wins / ValidateParse 原子预检 / Essential #18 / hooks{ sweep, afterBuild } / SceneSwitchReport | 引擎侧只缺触发面（C# op）与事件族 |
| 3 | `EnterPlayProgrammatic`（EditorAppScripts.cpp:221）= 编辑器 Play 装配单源（#82） | 前置① hooks 装配落点 |
| 4 | GameEntry.cpp:566 与 EditorApp.cpp:324 **均注册** HookInstantiate——b6b Review P1 注释"当前无宿主注册"已过时 | 前置② = 两处补 StampTreeMembership + 修过时注释 |
| 5 | 事件队列 #16 帧末派发（Events.h 头注）——晚于当帧 Update | 协议⑤ 时序（sceneLoaded 先于同帧 Start/Update）不能骑 #16 → D3 决策点 |
| 6 | `CreateSceneRecord` 句柄从 1 起（nextSceneHandle_++ 初值 1） | 0 = 未指派哨兵语义稳固，D2 可用 |
| 7 | 回放流 = InputState + StateHash 双流（bench-sim main.cpp）；membership 不入哈希流（⑥a 查②） | LoadScene 回放保障 = C# 确定性执行 + 哈希流捕获后果（§5 契约注记） |

## 2. 设计裁决点（开工闸——待用户拍板 2026-10-08 提出）

### D1 DDOL 根下后挂子实体语义（⑥c 前置④）

现状 `MarkDontDestroyOnLoadTree` 一次性标整棵树；后 spawn/attach 到 DDOL 父下的子实体打 active 句柄 → 换场被清、父幸存（Unity = 随根幸存）。本批是 DDOL 唯一消费者（C# 门面）落地时点。

- **方案 A（推荐）根位式**：DDOL 位只标根；清场判据 = 祖先链上有位即幸存（换场时逐实体 walk-up——换场罕见，成本可忍）。全 Unity 对齐：后挂幸存 ✓ / 移出死亡 ✓ / 移出后再挂回幸存 ✓ / 移出 DDOL 树后残留位误幸存 ✗（不存在位残留，天然正确）。改动：`MarkDontDestroyOnLoadTree` → 只标根（O(1)）+ 清场判据祖先感知 + ⑥a/⑥b/⑥c 既有测试断言更新（"flags 位在"断言语义收窄为根）。membership 不入哈希流 → 金回放零影响。
- 方案 B 树标 + 祖先感知清场：保留一次性标树，仅清场判据加祖先链检查。后挂幸存 ✓；移出 DDOL 树的实体残留位仍幸存（Unity 偏差，敞口注记）。改动最小。
- 方案 C 落点继承：spawn/attach 时继承父组——逻辑散布 SceneOps attach / Hierarchy::SetParent / spawn 多路径易漏，不推荐。

### D2 组 0 口径不对称（⑥c 前置③）

`CountSceneGroup(0)` 计未打标实体但 `QueueDestroySceneGroup(0)` 不收——正规路径零未打标 + smoke orphan 断言可抓 = 暗坑非活洞。

- **方案 A（推荐）清场判据改「除 DDOL 外全清」+ WARN**：编排侧清场从 `membership==旧handle` 泛化为「非 DDOL 系全部入队」（Single 语义本体）——组 0 泄漏自愈且 WARN 响亮（自愈不遮羞：有组 0 收编 = 红字一次，回归立现形）。`QueueDestroySceneGroup` 原语保留供测试/未来 Additive。
- 方案 B 仅头注注记：SceneMembership.h 落一行口径说明，行为不动，继续靠 smoke 断言兜底。

### D3 三事件推送机制（sceneLoaded/sceneUnloaded/activeSceneChanged）

协议⑤ 要求 sceneLoaded 在新场脚本 Start/Update 之前、换场当帧窗口内触发；既有事件队列 #16 = 帧末派发（晚于当帧 Update）→ 时序不满足。

- **方案 A（推荐）同步推送**：`IScriptBackend` 加**非纯虚** `SceneEventNotify(kind, oldHandle, newHandle, mode)`（默认空实现——NullBackend/测试后端零波及）；ScriptHost 实现 → Lemon.Entry 新导出 `lemon_scene_event`（UnmanagedCallersOnly，scriptsAttachFn_ 同款导出调用先例）同步分发到 C# SceneManager 静态事件。Execute 内按协议⑤ 序推：sceneUnloaded（step 4 sweep 后）→ sceneLoaded（afterBuild 段尾）→ activeSceneChanged（收口）。仍在"C++→C# 导出调用"这条既有通道类内，不违「批量 API + 事件队列」边界纪律。
- 方案 B 骑 #16 帧末事件队列（GameEvent 尾加三值）：零新机制但事件晚于当帧 Update——违反协议⑤，需回改 ADR 时序契约。

### D4 验收分工（ADR 验收面"smoke-scene 四跳全链"的落法）

回归夹具 = vs-survivor 拷贝、GameFlow 批⑨ 才调 LoadScene → smoke-scene 四跳走 C# 路径需向用户项目注入测试脚本。

- **方案 A（推荐）双侧分工**：script-tests 新增 `TestSceneSdk`（TestScript 夹具 + 内存场景×4 + 内存 resolveScene 钩子）承 C# 语义全链——SceneManager.LoadScene / LemonBehaviour.DontDestroyOnLoad / 三事件订阅序断言 / DDOL 幸存者句柄稳定 / Additive 红字；smoke-scene 从单跳扩四跳（C++ Request×4：Main→A→B→A 同名重装）承宿主端到端——组归零 / 幸存者 / 文档 origin / Paused / 孤组 / 档案翻转 / isLoaded 逐跳。两侧合并覆盖 ADR 验收面，不碰用户项目。
- 方案 B smoke-scene 四跳全走 C#（回归脚本注入 SmokeSceneDriver.cs）：端到端更真，但回归链耦合 vs-survivor 项目结构，夹具升级易碎。

## 3. 任务清单（文件/行级；D1–D4 裁决后按对应分支微调）

| # | 文件 | 动作 |
|---|---|---|
| T1 ✅ | `Engine/Scripting/ScriptHost.h` + `.cpp` | vtable 尾加 7 槽：`sceneLoadRequest(const char*, uint8)->int32` / `sceneCount()->uint32` / `sceneInfoAt(uint32, SceneInfoC*)->int32` / `sceneInfoByHandle(uint32, SceneInfoC*)->int32` / `activeSceneHandle()->uint32` / `setActiveScene(uint32)->int32` / `markDontDestroyOnLoad(uint64)->int32`；kNativeApi 填表尾加对应 Native*（g_world/g_scene 域线程窗口约定同既有槽）；`SceneInfoC`（handle/isLoaded/rootCount/name[64]/path[256]）与 C# 镜像同源注释钉 |
| T2 ✅ | `Engine/Scripting/ScriptHost.h` + `.cpp` | `SceneSourceHooks { resolveScene(nameOrPath, SceneSwitchRequest*) }` 进程级注册（SetSceneSourceHooks；EditorAssetHooks 同款纪律）——NativeSceneLoadRequest 经钩子寻址（D4：路径 > 唯一 stem > 红字 0）；无钩子 = 红字 + 0；mode != Single = 红字 + 0（C# 侧先拦为第一道）；寻址成功 → `g_world->Switcher().Request(std::move(req))`（当帧照常跑完，下一帧 Essential 执行 = Unity 下帧装载语义） |
| T3 ✅ | `Engine/Assets/ProjectFile.{h,cpp}` | `ResolveEntryScene` 泛化为 `ResolveScene(root, pf, nameOrPath)`（原入口逻辑 = nameOrPath 空 + entryScene 声明回退；新参非空 = 路径精确 → 唯一 stem → 空 = 失败）；ResolveEntryScene 变薄壳调用 |
| T4 ✅ | `Engine/Entry/GameEntry.cpp` | ① 注册 SceneSourceHooks（AssetIndex 版：FindByPath 精确 → stem 全库唯一性扫描 → ReadFileText）；② HookInstantiate（:218）InstantiateJson 后补 `StampTreeMembership(s, root, world.ActiveSceneHandle())`（前置②）；③ 帧尾裸 `CommitDestroys` 兜底（:830）删除（⑥b Review 4 登记项，恒 no-op 零行为差异）；④ smoke-scene 单跳扩四跳（:776 起——Main→A→B→A 同名重装；断言面随 D4 落） |
| T5 ✅ | `Editor/App/EditorApp.cpp` + `EditorAppScripts.cpp` | ① HookInstantiate（:62）同 T4② 补打标；② `EnterPlayProgrammatic`（:221）尾插 `ctx_.PlayWorld()->Switcher().SetHooks({...})`（前置①）：sweep = `gameUi_->UnloadDocumentsByOrigin(Scene)`；afterBuild = `assets::ResolveSpriteRefs(playScene, assets_)` + ctx 复用 ResolvePlayScripts 逻辑（单 registry，scene 引用恒 playScene_）+ `MountSceneUiDocuments()` play 态版 + Reconcile；③ 注册 SceneSourceHooks（AssetDatabase 版） |
| T6 ✅ | `Engine/Scripting/ScriptHost.cpp` | :162-172 NativeInstantiatePrefab 过时注释修正（两宿主均已注册——b6b Review P1 措辞随本批修正） |
| T7 ✅ | `Engine/ECS/World.h` + `ScriptHost.{h,cpp}` + `Lemon.Entry/Exports.cs` | D3 方案 A：IScriptBackend 加非纯虚 `SceneEventNotify`；ScriptHost 解析导出 `lemon_scene_event`（挂空安全同 scriptsDetachFn_ 先例）实现推送；Exports.cs 加 UnmanagedCallersOnly 壳（异常隔离纪律同既有导出） |
| T8 ✅ | `Engine/ECS/SceneSwitcher.cpp` | Execute 三处事件推点（协议⑤ 序：step 4 sweep 后 sceneUnloaded → afterBuild 尾 sceneLoaded → 收口 activeSceneChanged）；D1/D2 裁决分支落地（清场判据 / DDOL 判据） |
| T9 ✅ | `Engine/ECS/SceneMembership.{h,cpp}` | D1/D2 裁决对应改动（A 根位式 = MarkDontDestroyOnLoad 只标根 + 清场祖先感知；或 B 分支注记）；CountSceneGroup 口径头注同步 |
| T10 ✅ | `Engine/Scripting/dotnet/Lemon.SDK/SceneManager.cs`（新） | flat `Lemon.SceneManager`（D8）：LoadScene×2（Additive = 红字"未实现（ADR-017 D2 预留）"）/ GetActiveScene / SetActiveScene / sceneCount / GetSceneAt / GetSceneByName/Path（C# 迭代过滤）/ 三静态事件；`Scene` readonly struct（Handle/name/path/isLoaded/isValid/rootCount——isValid = 句柄非 0 且档案在）；`LoadSceneMode` 枚举；NativeApi.cs 尾加 7 槽镜像 + Native 包装（旧宿主判空降级：查询回 invalid Scene / LoadScene 红字）；C# 侧 handle→Scene 缓存（scene 事件时失效刷新——防逐帧查询字符串分配） |
| T11 ✅ | `Engine/Scripting/dotnet/Lemon.SDK/GameObject.cs` | LemonBehaviour 加 `public static void DontDestroyOnLoad(GameObject go)`（D7）——C++ 侧找根 + 非根 WARN（D5 Unity 兼容） |
| T12 ✅ | `tests/script/main.cpp` + `Engine/Scripting/dotnet/TestScript/` | D4 方案 A：`TestSceneSdk`——内存场景×4（JSON 夹具）+ 内存 resolveScene 钩子 + 全套 C# 语义断言（四跳 / DDOL 句柄稳定 / 事件序 sceneUnloaded→sceneLoaded→activeSceneChanged / Additive 红字 / SetActiveScene WARN / Scene 查询面） |
| T13 ✅ | `tests/engine/SceneTests.cpp` | D1/D2 对应单测更新（⑥a/⑥b/⑥c 断言随根位式收窄）+ 新增：编排事件推点序断言（C++ 侧经假 backend 记录调用序） |
| T14 ✅ | `docs/EngineDesign/04-CSharp-Scripting.md` | §3 SDK 门面：SceneManager/Scene/LoadSceneMode/事件族 + 时序契约（协议⑤）+ v1 口径注（sceneCount = isLoaded 档案数，DDOL 不建模伪场景——Unity 差异；SetActiveScene v1 限当前 active）；§2 影响行同步 ADR-017 |
| T15 ✅ | （复验） | 构建零警告 / lemon-tests 只增不减（基线 34,560）/ script-tests 扩项通过 / ctest 4/4 / 回归 full 21/21（scene-smoke 四跳版）/ bench-survivor 门禁 fps≥76.5 / 金回放三档抽验零重录（vtable 尾加零调用口径） |
| T16 ✅ | （回写） | DevLog 新条目 / M7c.md 批⑦ 行勾销 / AGENTS.md 状态行 / ADR-017 验收映射行（如 D1 选 A：D5 语义细则修订注） |

## 4. vtable 尾加与金回放口径（敏感面落账）

- 七槽全低频；基准场（bench-sim/bench-script/金档）零调用 = 零漂移——M5 批④/M6a/M6c/M7c 批① 尾追同款口径，register2 尺寸握手保旧宿主方向。
- **LoadScene = 首个结构性 vtable 通道**（此前尾加全是表现/参数通道）：回放保障 = C# 侧确定性执行（同输入同种子同帧调用）+ StateHash 哈希流自动捕获换场后果；b6 查②"op 入回放"按此口径落地（op 不显式入流，错图 = 哈希分歧现形）。前提 = 回放两侧项目文件一致（回放流程既有保证）。
- **已知敞口登记（非本批引入）**：UI 点击触发的换场不可回放——鼠标位不入 InputState（UI 事件管线在输入流外）。批⑨ svr-test 迁移 MainMenu→Grass（UI 按钮换场）时回来处理（候选：换场确认走键盘位 / UI 指针入流）。
- 事件族不入 StateHash（帧内瞬态，GameEvent 同款口径）；DDOL 位/组清场不改哈希流（⑥a 查② 沿袭）。

## 5. 出口判据（对齐 ADR-017 批⑦ 行）

script-tests TestSceneSdk 全绿（四跳 / 事件序 / DDOL / 红字面）+ 单测 checks 基线 34,560 只增不减 + ctest 4/4 + 回归 full 21/21（scene-smoke 四跳扩展版）+ bench-survivor 门禁不降 + 构建零警告 + 金回放抽验零重录 + 前置①②③④ 四项闭环落账。

## 红线自查（开工前对齐）

- vtable 只尾加不插队；组件 id / 系统注册序不动 → 金回放零重录预期成立；
- Vulkan 零泄漏 / 第三方零新增 / C++C# 边界两通道纪律（D3 方案 A 论证在案）；
- membership 不入注册表/序列化/StateHash（⑥a 口径沿袭，D1 改动不触碰）；
- `NotifyPendingDestroys` / `CommitDestroys` / `Destroy` 本体零改动（D2 改的是入队判据侧 QueueDestroy* 层，不碰销毁三件套本体）。

## Review 轮（2026-10-08 收口后全量自查）

结论：无阻断缺陷。抽查确认面 = 清场迭代中 Destroy（两阶段既有模式）/ hooks 生命周期（编辑器捕获 this 与 playWorld 同亡同生，每次 EnterPlay 重装配）/ isLoaded 三处补齐口径一致 / SceneInfoC 两侧布局（TestSceneSdk 名/路径断言 = 机械证明）/ D5 批量帧护栏不受 Request 影响（不建实体）。处置三项：

- **F1（登记不修）**：BuildInto 极端失败路径（预检过仍失败）= **事件半推**——sceneUnloaded 已推、sceneLoaded/activeSceneChanged 不推。与「世界半空」敞口（⑥b 设计定案③）同族：旧场确实卸了、新场确实没装，语义自洽；随批⑧ async 失败契约一并定文案。
- **F2（当日修，注释一处）**：`SceneManager.OnNativeSceneEvent` 头注原称"单订阅者异常不阻断后续派发"不准确——try 包整个 dispatch，单订阅者异常中断同事件后续订阅者（与 Unity 行为一致，属正确行为；改注释交底，不做 GetInvocationList 逐订阅者隔离）。
- **F3（观察项不动）**：C# `LoadScene` 失败时 native 已红字交底 + C# 再补一条泛化行 = 双红字——响亮失败文化内的小噪音，不改。
