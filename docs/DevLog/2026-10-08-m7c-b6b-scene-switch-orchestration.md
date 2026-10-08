# M7c 批⑥b：换场协议编排（SceneSwitcher + F2/F3 落地 + smoke-scene 单跳）

- 日期：2026-10-08
- 关联：[ADR-017](../ADR/ADR-017-Scene-Management-And-LoadScene.md) · [批文件](../Plans/M7c/2026-10-08-b6b-scene-switch-orchestration.md) · [⑥a DevLog](./2026-10-08-m7c-b6a-scene-membership.md)
- 性质：批⑥ 切片 b——⑥a 数据面之上的执行面；C# 门面/vtable/事件族归批⑦，async 分帧归批⑧

## 实测数字（出口判据全过）

| 项 | 值 | 判据 |
|---|---|---|
| lemon-tests | **34,524 checks 全绿** | 基线 34,481 只增不减（+43 = 编排七测） |
| ctest | 4/4 | — |
| 回归 full | **21/21**（新步 scene-smoke 首跑绿） | 首跑 20/21 唯一红 = smoke-anim 注入抖动（下文），复跑全绿 |
| bench-survivor | 门禁 **fps=86/87**（两跑） | ≥76.5 不降 |
| 构建 | 零编译警告 | — |
| scene-smoke 单跳 | 七面全绿（oldLeft=1/newGroup=3/orphan=0/ddol=1/uiMainGone=1/uiCardsShown=1/unpaused=1/fxCleared=1/档案翻转） | 批文件 §smoke-scene 断言面 |

## 落地件（批文件 T1–T12 全勾）

- **SceneSwitcher**（`Engine/ECS/SceneSwitcher.{h,cpp}` 新）：单槽 last-wins Request + Execute 编排主体（ValidateParse 预检 → 组清场 → NotifyPendingDestroys → CommitDestroys → Fx 清 + Audio.Paused 强制清（D6 引擎直兑）+ sweep 钩子 → BuildInto + Stamp → 档案收口 → afterBuild 钩子）+ SceneSwitchSystem（Essential，After "DestroyCommit"，InstallDefaultSystems 尾插不占 RNG 子流）。
- **F2 初始打标**：GameEntry 装载后 + EditorContext::EnterPlay 装载后建档 + StampSceneMembership + SetActiveSceneHandle——"零未打标"从进 Play 第一帧成立。
- **F3 后置 pass**：GameEntry 装配段 lambda 化（resolveSceneScripts / mountSceneUi），入口装配与换场 afterBuild 复用同一序列；hooks = sweep（UI origin=Scene 批量卸载，`UiSubsystem::UnloadDocumentsByOrigin` 新）+ afterBuild（SpriteRef 归一 + 脚本解析 + UIDocument 声明装载）。
- **出生打标**：`World::SpawnPrefab`（spawnFn + StampTreeMembership 子树打 active 句柄，`SceneMembership` 新原语）；Director/Spawn/Shooter 三处消费点改调——裸 GetSpawnFn 绕打标从此禁新调用面。
- **SceneArchive::ValidateParse**（新公共面）：ParseSceneDoc 同链零构建预检——换场原子性（坏档不清场，世界逐位不动，单测钉住）。
- **smoke-scene**：lemon-game `--smoke-scene`（编辑器外无头形态；内嵌第二场景档 + DDOL 目标 = 模板 GameFlow 实体）+ 回归 full 第 21 步。

## 三件开工前裁决（批文件"设计定案"详文）

1. **F1 形态选②（单帧先清后装），推翻 review 倾向①**：倾向①的入队帧/提交帧之间隔一个完整 FixedTick——已入 `DestroyQueueTag` 未通知未回收的半死旧场实体照常被系统 tick 且装载推迟一帧，两处偏离 ADR"装载帧 = 新场 Update 帧"。形态②的 notify→commit 直调不踩 F1 本体（漏 OnDestroy 的根因 = 跳过 NotifyPendingDestroys，`ScriptHost.cpp:961` 脚本命令路径同款恰好一次去重）。
2. **OnDestroy 序 = 池序（确定序）**：改 `NotifyPendingDestroys` 遍历序 = 战斗销毁通知序变 → RNG 消费序变 = 金回放重录（红线）。ADR"逆创建序"措辞按"确定性"意图收口，偏差随 ⑥c 的 03 分册注记落账。
3. **原子性预检**：先 ValidateParse 再清场；预检过的 BuildInto 极端失败（实体段异常）= 红字 + 档案标未装载，v1 不做事务回滚（与 Unity 装载失败炸场同级，已知敞口登记批文件）。

实现期第四件（计划外）：**档案面收口先于 afterBuild**——新场 Awake/OnEnable（afterBuild 内 ResolveSlotBehaviour 同步触发）执行时 GetActiveScene 必须已是新场（Unity sceneLoaded 时序同构）；批⑦ 事件族挂在 afterBuild 段尾。

## 实现期发现

- `DestroyCommitSystem::Name()` 返回 `"DestroyCommit"`（非类名）——SceneSwitchSystem::After 依赖名首跑断言炸（拓扑解析"names unknown system"），修正后过。既有 `TestSystemPipelineOrder` 断言系统数 20 → **21**、Essential 段断言扩为两系统序（有意变化：尾插新系统，RNG 子流/组件 id/vtable 零变动，金回放零重录红线不受碰——⑥c 用例终验）。
- **回归首跑 smoke-anim `sheet(all=NO)` 红**：zh 态、与批③ i18n 已知 en 态登记项不同链。独立复跑两连绿、断言链（ImGui 选帧对话框帧锚定输入注入）与本批改动正交（本批编辑器改动仅 EnterPlay 建档打标；smoke-anim 的 play-roundtrip byte-exact=YES 同跑过）——定性注入抖动（回归链内连续 20+ 步 GPU 负载下边沿偶失，smoke-drag 同类）。处置 = anim-chain 升级 `retry_step`（09 §9 两次取优机器化，脚本内注释登记）；复跑整链 21/21。

## Review 补丁（2026-10-08 当日，收口后自查）

- **P1 修复**：C# 建实体漏打标三路径——`NativeSpawnSprite`（Instantiate.Spawn）与 `ApplyStructural case 0`（SceneOps.Create）建实体无 membership → 换场收不走 = 旧场泄漏（触发面在批⑦ C# LoadScene，但属本批"零未打标"承诺，当日补：Create 后打 active 句柄）；`NativeInstantiatePrefab` 钩子无宿主注册，注册者打标责任注释登记。复验 = 构建 0 警告 / 单测 34,524 / ctest 4/4（script-tests 覆盖 C# 结构命令路径无回归）/ scene-smoke OK / script-chain `--validate` PASS。正向断言面随批⑦ smoke-scene 四跳收口。
- **P3 防御**：`World::SpawnPrefab` 补 active_ 空判。
- **批⑦ 登记两项**：编辑器 Play 世界 hooks 装配（缺 sweep/afterBuild——C# op 前必补）；instantiatePrefab 钩子注册者子树打标责任。
- **既有面交底**：GameEntry 帧尾裸 `scene.CommitDestroys()` 兜底（现状恒 no-op）——批⑦ 评估收编，本批不动。

## 批⑥ 进度

⑥a 数据面 ✅ + **⑥b 换场编排 ✅（本条）**；余 ⑥c StateHash/回放扩展用例（多次换场 + DDOL 幸存者回放轨迹 + 金回放零重录终验 + 03 分册 §2 注记收口）随后小批推进——批⑥ 整体收口判据见 [b6 批文件](../Plans/M7c/2026-10-08-b6-scene-membership-core.md)。
