# M7a 批③：Play 装配下沉（搬家批）

Status: **done**（2026-10-04：六件全落 + D5 两层护栏 + 单测 34210/1780 + ctest 3/3 + 回归 full 17/17 + 金回放跨版本零重录实证 + Editor 净删 399 行；完成情况见 §4）

> [M7a.md](./M7a.md) §4 批③ 落名批文件。**搬家批金回放零重录窗口期纪律：期间不合入无关改动**（M6c 批② / M7a 批② 先例）；vtable 47 / 组件 id 31 / 默认管线系统序 20 零变动 → 零重录预期成立（D5 护栏的 `poolDataFn` 为 ComponentMeta 尾加钩子、`stale` 为 BatchSystemFrame 瞬态尾字段——均不入 StateHash/回放档）。出口判据：回归 full 17/17 + **金回放三档零重录** + 三 smoke 链绿（template/uirml/audio 均在 17 步内）+ 编辑器侧净删行数（diff 佐证）。

## 0. 开工现场核对（2026-10-04，决定设计的事实）

- **D5 两案开工定（评审 §D5）→ 实现期第三轮收敛为两层设计**：
  - **否"禁同步生成通道"（案B）**：实测 `Instantiate.Prefab/Spawn` 在模板与 svr-test 的 **Update 路径大量使用**（`Templates/vs-survivor/Game/PlayerCombat.cs:105,241` 宝石/刀、`demo/svr-test/Game/PlayerBehaviour.cs:202,228,412,436,460`）——档① behaviours 的 Update 本就跑在 `scriptsTickFn_` 窗口内，禁通道 = 既有局当场断裂 + 回归面爆炸。
  - **否"整体重建"（案A 原案字面）**：托管侧中途 re-gather 需新导出 + SDK 改动 + 新实体中途加入迭代 = 确定性风险。
  - **首版"仅基址比对 fail-stop"被 script-chain 冒烟证伪**：SpawnerBehaviour 每帧 1 只的正当负载也会在几何扩容边界（8→16→32→64）触发真实搬移 → 截断该帧批量块 + 红字 → 冒烟 errors=3 FAIL。教训：**基址比对是精确的悬垂判据，但不是合法/病态用例的判据**——边界跨界是正当负载的常态。
  - **终态两层**：第一层 = gather 前对被查询池 `reserve(count + 1024 结构余量)`（`ComponentMeta.reserveFn` 尾加钩子；entBuf_/ptrBuf_ 的"构造期零扩容"技术下沉到组件池——合法 spawn/挂组件在余量内**零重分配零触发**，零行为漂移）；第二层 = 病态兜底（超余量爆量）：基址比对（`poolDataFn`）真实搬移才置位 `BatchSystemFrame.stale`（C++/C# 两侧同步尾加，帧瞬态不入哈希）→ C# 块循环头跳过本帧剩余块 + 红字指路分帧。容量代价 ~24KB/池（Transform2D 1024 × 24B），稳态 reserve no-op。
- **提取下沉的编辑器时序保留**：`ViewportRenderer::ExtractScene` 是**每渲染帧**驱动（`Render(cl, ctx, simAlpha)` 内调用——插值 alpha 契约 + 编辑态场景渲染），不能搬进 `World::Step`（按模拟步频跑）。故下沉 = 核心逻辑（槽位映射/差集释放/整包寻址）入 `renderer::SceneExtractor`，编辑器薄壳保留直调时序；`SystemStage::Extract` 首个真实现（`RenderExtractSystem`）+ `World::Step` 补跑 Extract 阶段——**编辑器默认管线不装**（16→17 系统序变动零），消费方 = 批④ GameEntry 装配期 AddSystem；零系统时 RunStage no-op = sim/编辑两域零影响。
- **SceneOps 应用点在窗口外**：`ApplyStructural` 由 Essential 阶段调用（`Systems.cpp:1479`，帧首）——op 驱动的 Create/AddComponent/Attach 不在 `scriptsTickFn_` 窗口内，无需护栏；`AttachBehaviour`/`ResolveSlotBehaviour` 同理（EnterPlay/ops 两路）。
- **smoke 探针读返回值不读日志**：`smokeUiLoadsP2`（uirml 第二局装载增量）、`aud(mount=%d)`（音频装载数）等 = 捕获函数返回值；日志字符串仍逐字节保留（真人/排查面）。
- **`AssetIndex` 已备 `FindByLowId(type, low32)`**（b2 落地，03 §69 低 32 位映射约定）——运行时 PrefabCache 源的直接口。
- **批量三缓存（Clip/Controller/Table）不在本批**：M7a 批③ 清单六件不含它们（解析器 b2 已下沉、缓存构建归编辑器；批④ GameEntry 装配时再定形态），范围纪律。

## 1. 件清单（引擎侧新建/下沉，全部"搬家非复制"）

| # | 件（目标） | 来源（编辑器） | 要点 |
|---|---|---|---|
| 1 | `Engine/Assets/SaveStore.{h,cpp}` | `EditorContext::{SaveFilePath,WriteSaveFile,LoadSaveFile}`（`EditorContext.cpp:953-1016`） | 纯静态函数（root + ch 参数化）：三档文件名/.bak 轮转/16MiB 上限/坏档兜底/slot 旧 game.sav 惰性迁移逐行同源；EditorContext 三方法删除，EnterPlay/ExitPlay/HookSaveFlush 改直调（净删） |
| 2 | `Engine/Core/FileOps` 增 `WriteFileAtomic` | `Editor/Assets/AssetDatabase.{h,cpp}:52` | 原子写（tmp→RenameReplace + flush 检查 + durable fsync）下沉 FileOps（其头注本就声明该归属）；AssetDatabase.h 留 inline 同名转发（14 调用点零扰动，b2 AssetTypes using 同款纪律） |
| 3 | `Engine/Assets/PrefabCache.{h,cpp}` | `EditorContext::BuildPlayPrefabCache/SpawnPlayPrefab/InstantiatePrefabJson`（`EditorContext.cpp:541-592,558-574`）+ `InstantiatePrefabAsset` 的 scripts[] 树解析（`:504-531`） | `PrefabSource` 轻虚接口（EachPrefab{guid,absPath}；AssetDatabase/AssetIndex 双实现，SpriteRefSource 纪律）；Build（低 32 → {guid,json} + 碰撞告警）/Spawn（未命中去重告警 + 队伍覆盖）/InstantiateJson（无 IO 无日志）；**scripts[] 树解析随迁**（ScriptHost* 可选参——ResolveSlotBehaviour + typeId 解析壳由调用方注入）；EditorContext 成员 `playPrefabCache_/playSpawnWarned_` 删除，EnterPlay 建 `assets::PrefabCache` 局部持有于 playWorld 生命周期（成员改值语义） |
| 4 | `Engine/Ui/UiMount.{h,cpp}` | `EditorApp::MountSceneUiDocuments/ReconcileUiDocuments`（`EditorAppUiBridge.cpp:178-236`；批① D8 修复已在 UiSubsystem 侧随装载走） | `UiDocSource` 轻虚接口（ResolveRml(guid)→{relPath,absPath} + IsHealthyRml(relPath)）；MountSceneDocuments（View<UIDocument> 扫描/去重/红字/装载/声明态归位/ResetDynamicDocuments）+ ReconcileDocuments（FileBacked 对账逐出）；EditorApp 两方法留薄壳（调用点零扰动） |
| 5 | `Engine/Audio/AudioMount.{h,cpp}` | `EditorApp::MountPlayAudio/WirePlayAudioBackend/EnsureClipLoaded` + `BakedPathFor/BakeStale`（`EditorAppScripts.cpp:184-340`） | `AudioMount`（持 AudioEngine& + guid→clipId 表）：MountAll（StopAll/ResetClips/SetPaused(false)/清表/逐件 EnsureClipLoaded + 装载日志）/EnsureClipLoaded（缺烤现烤/Peek/流式分流/注册）/ClipIdOfGuid/WireBackend（World::SetAudioBackend + 静态 thunk——EditorApp 的 ResolveAudioClipThunk 退役）；`AudioItem{guid,srcAbs,loopStart,loopEnd,preload}` + `EachAudio` 虚接口；BakedPath/BakeStale 公开静态（编辑器后台烤制线程复用）；试听态（previewVoice_）与烤制线程留编辑器 |
| 6 | `Engine/Renderer/CameraFollow.{h,cpp}` | `EditorApp::UpdateGameCameraFollow`（`EditorApp.cpp:1060-1150`） | `CameraFollowState{active,camEnt,playerEnt,scriptedEnt}` + `UpdateCameraFollow(Scene&,Camera2D&,State&,Vec2* outTarget)`（目标优先级/轻校验缓存/首帧吸附/刚性跟随逐行同源）+ `ResetCameraFollow`（编辑态默认位 640,360）；EditorApp 薄壳（playDiag 输出 + Playing() 分支） |
| 7 | `Engine/Renderer/SceneExtractor.{h,cpp}` | `ViewportRenderer::ExtractScene`（`ViewportRenderer.cpp:434-500`） | `SceneExtractor`（槽位映射/version 校验/纪元差集/整包寻址/SnapPrev，去 `EditorContext&` 锚定改 `Scene&` + `AtlasRegistry&` + `RenderableManager&`）+ **`RenderExtractSystem : ecs::ISystem`**（SystemStage::Extract 首个真实现；持 extractor + 两引用；消费方装配期 AddSystem——批④ GameEntry）；ViewportRenderer 三成员（ridBySlot_/lastSceneStamp_/extractEpoch_）删、加 extractor_ 成员、ExtractScene 薄壳化 |
| 8 | `Engine/ECS/World.cpp` Step 补跑 | — | `pipeline_.RunStage(Extract)` 追加（FixedTick 后）；头注同步；零 Extract 系统 = no-op |
| 9 | D5 护栏（两层） | 评审 §D5（`ScriptHost.cpp:395-416,540-552`） | ① `ComponentMeta.poolDataFn + reserveFn`（尾加默认 nullptr；四登记宏补两模板——dense 基址 + 容量预留）② `BatchSystemFrame.stale` 尾字段（C++ `ScriptHost.h` + C# `Batch.cs` 两侧同步；帧瞬态不入哈希；sizeof 40→48）③ ScriptHost.cpp：gather **前**布防（watch 表去重被查询 comp + reserve(count+1024) + 记基址）、`scriptsTickFn_` 前挂 `g_batchFrames` 后收防、三结构入口（`NativeSpawnSprite`/`NativeWrite` emplace 分支/`NativeInstantiatePrefab` 钩子返回后）比对基址真实搬移才置位（红字去重/帧 + 计数探针）④ C# 块循环头 `if (fr->Stale != 0) break;`（不计异常禁用计数） |

**编辑器侧薄壳化**（净删佐证）：EditorContext.cpp（三存档方法 + prefab 三函数体 + 两成员）、EditorAppUiBridge.cpp（两函数体）、EditorAppScripts.cpp（音频三函数体 + 两匿名助手 + thunk）、EditorApp.cpp（相机函数体 + 三跟随成员 + gameFollowActive_）、ViewportRenderer.cpp（ExtractScene 体 + 三成员）、AssetDatabase.cpp（WriteFileAtomic 体）。

**不动的**（范围纪律）：批量三缓存（Clip/Controller/Table，批④ 定形态）；`ResolveUiDocument`/`LoadProjectFonts`（编辑器资产域通道，非 Play 装配件）；后台烤制线程/试听态（编辑器便利层）；`SetEditorAssetHooks` 更名（M7a.md §8 登记项不动）；viewport RT/渲染循环本体。

## 2. 单测（engine-tests 增量）

- `TestSaveStore`：temp root → 三档写读回环 / 空通道不落盘 / 主档损坏 .bak 兜底 / slot 旧 game.sav 惰性迁移（新档在场不迁）/ ch 越界钳 slot。
- `TestPrefabCache`：mock 源（temp .prefab 文件 ×2）→ Build 低 32 索引 / Spawn 树装载 + pos 覆盖 + team 覆盖 + prefabId 回链 / 未命中 Null + 告警去重（重复 Spawn 同坏 id 不二刷）/ 低 32 碰撞取先登记者 / scripts[] 树解析（ScriptHost 缺席 = 槽保持 typeId=-1 零挂载不炸）。
- `TestCameraFollow`：无 GPU 纯逻辑——Camera 栭优先 / Player 次之 / 脚本实体兜底；首帧吸附 center；目标死亡轻校验失效重扫；Reset 复位 640,360。
- `TestSceneExtractor`：AtlasRegistry 假精灵 + RenderableManager → 提取建槽/世界变换/禁用跳过/悬空 spriteId 跳过/销毁差集释放/场景换代全清。
- `TestPoolDataStable`（D5 基础）：全组件 `poolDataFn` 非 null / 容量内 emplace 基址不变 / 越容量 emplace 基址变。
- **D5 阴性验证**（script-tests）：TestScript 注册"窗口内爆量 Spawn"批量系统 → 断言 stale 计数 > 0 + 进程不崩 + 后续帧照常（探针 = ScriptHost 暴露 `LastBatchStaleMarks()` 计数）；反向：常规 Spawn（容量余量内）计数恒 0。
- 既有测试全绿 = 搬家对拍全等（不搞双实现并存）。

## 3. 门格

构建 0 error（含 imgui-isolation）→ ctest 3/3（engine-tests 单测计数 ≥ 34145 + 增量 / script-tests ≥ 1776 + 增量）→ 回归 **full 17/17** → **金回放三档零重录**：bench-sim `--replay` st/mt 双档 `mismatches=0` + bench-script 回放 `PASS`（既有录档不重录）→ bench-survivor 900 帧 fps 波动带内零降级 → 编辑器侧 diff 净删行数佐证 → `git status` 无关零改动（窗口期纪律）。

## 4. 完成情况（2026-10-04 收口）

| # | 状态 | 实施与验证摘要 |
|---|---|---|
| SaveStore | ✅ | 三静态函数逐行同源（路径/.bak 轮转/16MiB/坏档兜底/legacy 迁移）；EditorContext 三方法删、EnterPlay/ExitPlay/HookSaveFlush 直调；既有 TestSaveChannelSplits 改直测引擎件（+2 探针：空 root 路径/空 root 写 no-op） |
| WriteFileAtomic | ✅ | 连同 FsyncFile 下沉 `Engine/Core/FileOps`（头注归属兑现）；AssetDatabase.h inline 同名转发 = 14 调用点零扰动 |
| PrefabCache | ✅ | Build/Spawn（低 32 索引 + 碰撞红字 + 告警去重 + 队伍覆盖）/InstantiateJson/ResolveTreeScripts（scripts[] 树解析随迁，编辑器钩子路径复用）；`PrefabSource` 轻虚接口 + EditorContext 内 DbPrefabSource 适配；两成员（cache/warned）出编辑器 |
| UiMount | ✅ | MountSceneDocuments + ReconcileDocuments 逐行同源（日志字符串逐字节保留）；`UiDocSource` 接口 + DbUiDocSource 适配；EditorApp 两薄壳 |
| AudioMount | ✅ | MountAll/EnsureLoaded（现烤/Peek/流式分流）/ClipIdOfGuid/WireBackend（thunk 随迁）/BakedPath+BakeStale 公开静态（后台烤制线程复用）；`AudioItem`+`AudioSource` 接口 + DbAudioSource 适配；audioClips_ 成员退役（LoadedCount() 供 smoke-template aud(mount=) 探针）；试听态/烤制线程留编辑器 |
| CameraFollow | ✅ | `renderer::CameraFollowState` + Update/Reset 纯函数（优先级/轻校验/首帧吸附/刚性跟随）；EditorApp 薄壳（GameCam 路由 + playDiag 回传）；四成员出编辑器 |
| SceneExtractor | ✅ | 槽位映射/纪元差集/整包寻址/SnapPrev 全量下沉（去 EditorContext 锚定）；`RenderExtractSystem : ISystem`（SystemStage::Extract 首个真实现，批④ lemon-game 装配期消费）；ViewportRenderer 三成员删 + extractor_ + 薄壳（每渲染帧直调时序保留——插值契约）；`World::Step` 补跑 Extract（零注册系统 no-op） |
| D5 护栏 | ✅ | 两层设计（见 §0 第三轮收敛）；单测锁：TestPoolDataStable（全组件钩子 + 稳定段/搬移拍双观察）+ script-tests 负例（BurstSpawnSystem 首块爆 3000 > 余量 1024 → stale 置位/生成照常落地/后续帧零误报） |

**门格（2026-10-04）**：构建 0 error；ctest 3/3；engine-tests **34210**（34145→+65）/ script-tests **1780**（1776→+4）；回归 **full 17/17**（首轮 15/17 = smoke-drag 注入抖动 [OK/FAIL/FAIL 三跑同型于 T1 先例，复跑全绿] + script-chain FAIL = D5 首版误伤 [改两层后单步复验 PASS]）；**金回放零重录双证**：①三档现录现放 mismatches=0（sim-st/sim-mt/script）②**跨版本实证**——HEAD（批③ 前）stash 构建录档 → 批③ 新构建回放 `mismatches=0`（sim + script，终态 alive/created/destroyed 逐项一致）；bench-survivor 1000 帧 ×3 = **fps=77/77/80 全 PASS**（alive=10493 = b0 千帧基线一致、playerHp=144823 三跑逐位一致、历史波动带 73–84 内零降级）；Editor 侧 diff 净删 **399 行**（155+/554−），新引擎件 12 文件 867 行（含契约注释）。

**实现期发现**：① D5 首版"仅比对"把合法边界跨界当病态截断（script-chain errors=3 实锤）→ 两层收敛；② smoke-drag 抖动与批③ 无因果（HEAD 基线同场再现，注入时序类）；③ `SceneArchive` 在 `ecs::` 命名空间（PrefabCache.cpp 限定）；④ BatchSystemFrame sizeof 40→48（stale 尾加按 8 对齐，C# 镜像同规则）。

**移交批④**：RenderExtractSystem 装配（lemon-game AddSystem）；PrefabSource/UiDocSource/AudioSource 的 AssetIndex 侧运行时适配（音频侧需 manifest 扩 importer 字段或回读 .meta）；SaveStore/PrefabCache/UiMount/AudioMount/CameraFollow/SceneExtractor 六件装配序列。
