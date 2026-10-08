# 批⑥b：换场协议编排——SceneSwitcher + F2 初始打标 + F3 后置 pass + smoke-scene 单跳

Status: done ✅（2026-10-08 机器面全过——单测 **34,524**（+43）/ ctest 4/4 / 回归 full **21/21**（scene-smoke 新步首跑绿）/ bench 门禁 fps=86/87 / 构建零警告；实现期四件发现与回归 smoke-anim 抖动定性处置见 [DevLog](../../DevLog/2026-10-08-m7c-b6b-scene-switch-orchestration.md)。⑥c 回放扩展 ✅ 同日收口 = 批⑥ 整体完成，[批文件](./2026-10-08-b6c-replay-extension.md)）

- 日期：2026-10-08
- 关联：[ADR-017](../../ADR/ADR-017-Scene-Management-And-LoadScene.md)（换场帧协议 / D3 统一管线 / D6 Audio.Paused 强制清）· [b6 批文件](./2026-10-08-b6-scene-membership-core.md) §⑥b 开工前必读（F1–F3）· [M7c.md](./M7c.md) 批⑥ 行
- 性质：批⑥ 切片 b——引擎侧换场编排（数据面 ⑥a 之上的执行面）。C# 门面/vtable/事件族归批⑦，async 分帧归批⑧；两批的消费入口 = 本切片的 SceneSwitcher。

## 设计定案（三件开工前裁决）

### ① F1 形态：选②（单帧先清后装），推翻 review 倾向①

批⑥文件 F1 给了二选一，倾向①（入队后借下帧 #17 通知提交、编排挂 #17 后专用段）。细析后**选②**——编排系统（Essential 段，`After("DestroyCommitSystem")`）单帧内完成"入队 → `backend->NotifyPendingDestroys` → `CommitDestroys` → 清扫 → 装载"：

- **倾向①的硬伤 = 半死窗口**：入队帧与提交帧之间隔一个完整 FixedTick——已入 `DestroyQueueTag` 但未通知未回收的旧场实体照常被系统 tick、被 C# Update 查询到（Alive 仍真），且装载推迟到下下帧——两处偏离 ADR-017 协议"下一帧 Essential 段执行换场……⑥ 本帧 Start → Update 照跑"（装载帧 = 新场 Update 帧）。
- **形态②不踩 F1 预警的本体**：漏 OnDestroy 的根因是**跳过 NotifyPendingDestroys**（`SceneMembership.h:46-48` 头注口径），不是绕开 #17。先 notify 后 commit 的直调对 = 脚本命令路径同款先例（`ScriptHost.cpp:961-976`，恰好一次由 `kScriptFlagDestroyNotified` 去重）。
- **与 #17 同帧共存无冲突**：#17 先跑（常规战斗销毁当帧收口），#18 编排的组清场是新一批入队+提交；同帧两次 CommitDestroys 各自 swap 队列，语义独立。
- 编辑器 edit 世界只装 `DestroyCommitSystem`（`EditorContext.cpp:49`）不装 `InstallDefaultSystems` → 编排系统天然不进编辑态。

### ② OnDestroy 通知序：池序（确定序），ADR"逆创建序"措辞按确定性意图收口

ADR-017 ①"序 = 逆创建序定死——Unity 不保证序，Lemon 保证 = 确定性"的核心意图是**确定性**。现状 `NotifyPendingDestroys` 按 `View<DestroyQueueTag, ScriptBox>` 池序遍历（`ScriptHost.cpp:1020`"池内部序遍历 = 确定序（回放两侧同源）"）——改它 = 战斗销毁通知序变化 → 脚本 OnDestroy 内逻辑次序变 → RNG 消费序变 = **金回放重录**（红线）。⑥b 沿用池序（确定、回放两侧同源）；严格逆创建序需要单实体通知接口且无消费者，不做。偏差随 ⑥c 的 03 分册 §2 注记一并落账。

### ③ 原子性：ValidateParse 预检（坏档不清场）

编排先清场后装载——BuildInto 失败若发生在清场后 = 世界半空。`SceneArchive` 加公共 `ValidateParse`（ParseSceneDoc 试解 + 迁移，零构建）作预检；失败 = 响亮失败（红字 + 失败码），世界逐位不动。预检过了 BuildInto 再失败（实体段构建异常的极端路径）= 红字 + 新档案标未装载，登记为已知敞口（与 Unity 装载失败炸场同级，v1 不做事务回滚）。

## 编排序列（Execute 主体；对齐 ADR-017 换场帧协议 ①–④）

```
Request(name, path, jsonText)          // 单槽 last-wins（重复请求 WARN 覆盖；Unity 连调 LoadScene 最后者赢的同构简化）
→ 下一帧 Essential #18 SceneSwitchSystem::Tick：
  0. ValidateParse 预检——失败 = 红字 + 世界不动（原子性）
  1. QueueDestroySceneGroup(旧 handle)   // 组内非 DDOL 入队（协议 ①③前半）
  2. backend->NotifyPendingDestroys      // OnDestroy 补发（池序，恰好一次）
  3. scene.CommitDestroys()              // 提交（协议 ③前半）
  4. world.Fx().Clear()                  // 随行清扫：Fx 非实体附着整场清（引擎承诺引擎兑付）
     world.AudioBackend()->SetPaused(false)  // D6 强制清（引擎承诺引擎兑付）
     hooks.sweep()                       // 宿主件：UI origin=Scene 文档卸载（R10）
  5. CreateSceneRecord → BuildInto → StampSceneMembership   // 协议 ④（打标仅收编未指派）
  6. 旧档案 isLoaded=false；新档案 isLoaded=true；SetActiveSceneHandle(新)   // 档案面收口先于 afterBuild——新场 Awake 内 GetActiveScene 已新场（Unity sceneLoaded 时序同构；协议 ⑤ 的数据面，事件推送归批⑦）
  7. hooks.afterBuild(scene)             // F3 后置 pass：SpriteRef 归一 / ScriptBox 解析（Awake/OnEnable）/ UIDocument 声明装载
```

DDOL 重标签（协议 ②）不在编排内——D5 语义在打标时兑现（`MarkDontDestroyOnLoadTree`，批⑦ C# 门面触发），清场判据只看 flags 位（⑥a 已实现）。

**Instantiate 打标（F3 尾项裁决）**：prefab spawn 通道 = `World::SpawnPrefab` 包装（spawnFn_ 调用 + **子树打标 active 句柄**）——`DirectorSystem`/`SpawnSystem`/`Shooter` 三处消费点改调（`Systems.cpp:313/436/594`）。即时打标（非 BuildInto 时的 Stamp 收编）保"零未打标"不变量：否则 spawn 实体被下次 Stamp 误收编进新场组。

## 任务清单（文件/行级；全勾 2026-10-08）

| # | 文件 | 动作 |
|---|---|---|
| T1 ✅ | `Engine/ECS/SceneSwitcher.h`（新） | SceneSwitchRequest/Hooks/Report/Status + SceneSwitcher（单槽 pending + SetHooks + Execute 声明）+ SceneSwitchSystem（Essential，After "DestroyCommit"——Name() 实际串，实现期发现①见 DevLog） |
| T2 ✅ | `Engine/ECS/SceneSwitcher.cpp`（新） | Execute 主体（上节序列）+ 系统 Tick 壳（`world.Switcher().Execute`）+ 报告结构填数 |
| T3 ✅ | `Engine/ECS/SceneMembership.{h,cpp}` | 加 `StampTreeMembership(Scene&, Entity root, handle)`（spawn 子树打标；CollectTree 复用 + 仅收编语义） |
| T4 ✅ | `Engine/ECS/World.{h,cpp}` | `switcher_` 成员 + `Switcher()` 访问器；Audio 复用既有 `AudioSink()` 访问器（探测发现，未新加）；`HasSpawnFn()` + `SpawnPrefab()`（fn 调用 + StampTreeMembership(active)） |
| T5 ✅ | `Engine/Systems/Systems.cpp` | InstallDefaultSystems 尾插 SceneSwitchSystem（不消费 RNG 不占子流）；三处 spawn 消费改 `world.SpawnPrefab`（Director/Spawn/Shooter） |
| T6 ✅ | `Engine/Serialization/SceneArchive.{h,cpp}` | 加公共 `ValidateParse`（ParseSceneDoc 试解 + 迁移链到当前版本，零构建零副作用） |
| T7 ✅ | `Engine/Ui/UiSubsystem.{h,cpp}` | 加 `UnloadDocumentsByOrigin(UiDocOrigin)`（先收集名再逐个 Unload——遍历中不改容器） |
| T8 ✅ | `Engine/Entry/GameEntry.cpp` | F2：装载后建档 + Stamp + SetActiveSceneHandle；装配段 lambda 化（resolveSceneScripts/mountSceneUi）+ hooks 装配（sweep = UI 批量卸载；afterBuild = SpriteRef 归一 + 脚本解析 + 声明装载 + Reconcile）；`--smoke-scene` CLI（帧 80 预置脏态+DDOL+Request → 下一帧 Essential 执行 → 七面断言 + RESULT 行） |
| T9 ✅ | `Editor/EditorContext.cpp` | F2：EnterPlay 装载后建档（Name() + scenePath_）+ Stamp + SetActiveSceneHandle |
| T10 ✅ | `Engine/CMakeLists.txt` | 源清单 + SceneSwitcher.cpp |
| T11 ✅ | `tests/engine/SceneTests.cpp` | 编排七测（+43 checks；全语义/原子性/Fx 清/钩子序/no-op/Essential 集成/SpawnPrefab 树打标）+ `EcsTests` 系统数 20→21 断言更新（有意变化，登记 DevLog） |
| T12 ✅ | `tools/editor-regression.sh` | full 加 scene-smoke 步（回归 20 → **21 步**，09 文档两处口径已同步）；附带 anim-chain 升级 retry_step（注入抖动定性处置，DevLog"实现期发现"） |

## smoke-scene 单跳断言面（lemon-game `--smoke-scene`；编辑器外无头形态）

夹具 = 回归现有 vs-survivor 拷贝（`${TMP}/game`）；第二场景 = 内嵌最小 JSON（3 实体 + 1 个 UIDocument 声明，guid 抄夹具内 .rml）。流程：装载 Main.scene → 跑至菜单就绪 → C++ 直调 `MarkDontDestroyOnLoadTree`（按内嵌 guid 定位 Main.scene 的非 UI 根实体）→ `Audio.SetPaused(true)` + `Fx().PopupText` 灌脏 → `Switcher().Request(第二场景)` → 下一帧 Essential 执行 → 终态断言：

1. 旧组非 DDOL 归零：`CountSceneGroup(old) == DDOL 幸存数`（幸存者 scene 保留来源组）
2. DDOL 幸存者 Alive + 句柄不变 + flags 位在
3. 新组打标数 = 第二场景实体数；`CountSceneGroup(0) == 0`（零孤组）
4. 换场前 origin=Scene 文档（main.rml）卸载；新声明文档装载
5. `Audio.IsPaused() == false`（D6 强制清——换场前手动置 true 的复原）
6. `Fx().TextCount() == 0`（非实体附着整场清）
7. 档案面：旧 record isLoaded=false、新 record isLoaded=true、active handle = 新

## 出口判据（⑥b 切片）——全过 2026-10-08

单测新增七测全绿 + checks 基线 34,481 只增不减（**34,524**，+43）+ ctest 4/4 + 回归 full **21/21**（新步首跑绿；首整跑 20/21 唯一红 = smoke-anim 注入抖动，独立复跑两连绿定性非本批引入，retry_step 机器化后整链绿）+ bench-survivor 门禁不降（fps=86/87 两跑 ≥76.5）+ 构建零警告。批⑥ 整体收口（⑥c 金回放零重录验证）另批。

## 红线自查（开工前对齐；收口复核 ✔）

- vtable 49 槽不动（C# 边界零变化——op/事件族归批⑦）→ 金回放零重录预期成立
- 组件 id / 系统注册序不动（新系统尾插 + 不消费 RNG）→ 三金档哈希流逐位不变
- `NotifyPendingDestroys` / `CommitDestroys` / `Destroy` 本体零改动 → 战斗销毁路径零漂移
- membership 不入注册表/序列化/StateHash（⑥a 口径沿袭）

## Review 补丁（2026-10-08 收口后自查，当日修）

1. **P1（本批不变量的真实漏洞）**：C# 侧建实体三条路径漏打标——`NativeSpawnSprite`（`Instantiate.Spawn`，svr-test 在用）/ `ApplyStructural case 0`（`SceneOps.Create`）建实体无 membership → 换场组清场收不走 = 旧场实体泄漏进新场（档1 RunSweeper 手工清的同类病）。触发面在批⑦（C# LoadScene op 落地）但属本批"零未打标"承诺，**当日补**：两处 Create 后打 `ActiveSceneHandle()`（`ScriptHost.cpp`）。`NativeInstantiatePrefab` 钩子当前无宿主注册（恒 0 失败路径）——注册者责任注释登记（批⑦ 接）。复验：构建零警告 / 单测 34,524 / ctest 4/4（script-tests = C# 结构命令路径无回归的机器面）/ scene-scene OK / script-chain（含 `--validate` 验证层）PASS。打标的正向断言面随批⑦ smoke-scene 四跳（零孤组）收口。
2. **P3（防御）**：`World::SpawnPrefab` 补 `active_` 空判（现网无路径，Step 前置同款防御）。
3. **登记批⑦（两项前置）**：① 编辑器 Play 世界缺 `Switcher().SetHooks` 装配（sweep/afterBuild）——C# LoadScene op 接入前必须补，否则编辑器 Play 内换场 = UI 文档不卸载 + 新场脚本全哑（lemon-game 本批已装，编辑器侧归批⑦ 与 UI/事件设计同批落地）；② `instantiatePrefab` 钩子注册者负责子树打标（`World::SpawnPrefab` 的 StampTreeMembership 同款责任边界）。
4. **既有面交底（不动）**：`GameEntry.cpp` 帧尾 `if (scene.PendingDestroyCount() > 0) scene.CommitDestroys()`（Step 后兜底）——现状恒 no-op（Essential #17/#18 双提交点已清空队列），编排时代语义上属"裸 commit"形态；批⑦ 顺手评估收编或注释，本批不动（零行为差异）。
