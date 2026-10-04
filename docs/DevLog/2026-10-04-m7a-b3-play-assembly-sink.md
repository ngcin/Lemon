# M7a 批③：Play 装配下沉（搬家批）+ D5 批量帧护栏

**日期**：2026-10-04　**状态**：done（代码面收口；批④ GameEntry 未开工——本批六件为其备料）　**批文件**：[Plans/M7a/2026-10-04-b3-play-assembly-sink.md](../Plans/M7a/2026-10-04-b3-play-assembly-sink.md)

**纪律**：搬家批金回放零重录窗口期——期间不合入无关改动（M6c 批② / M7a 批② 先例）；vtable 47 / 组件 id 31 / 默认管线系统序 20 零变动。用户 svr-test WIP 资产（untracked）未触碰。

## 搬运六件（编辑器 → 引擎，全部"搬家非复制"）

| 件 | 目标 | 编辑器侧去向 |
|---|---|---|
| 存档三通道 | `Engine/Assets/SaveStore`（纯静态） | EditorContext 三方法删除，EnterPlay/ExitPlay/HookSaveFlush 直调 |
| 原子写 | `Engine/Core/FileOps::WriteFileAtomic`（+FsyncFile 随迁） | AssetDatabase.h 同名 inline 转发 = 14 调用点零扰动 |
| Play Prefab 工厂 | `Engine/Assets/PrefabCache`（Build/Spawn/InstantiateJson/ResolveTreeScripts） | playPrefabCache_/playSpawnWarned_ 两成员出编辑器；DbPrefabSource 适配 |
| UIDocument 装载 | `Engine/Ui/UiMount`（MountSceneDocuments/ReconcileDocuments） | EditorApp 两薄壳；DbUiDocSource 适配 |
| 进 Play 音频装载 | `Engine/Audio/AudioMount`（MountAll/EnsureLoaded/WireBackend/BakedPath/BakeStale） | audioClips_ 退役；试听态/后台烤制线程留编辑器 |
| 相机跟随 | `Engine/Renderer/CameraFollow`（CameraFollowState + Update/Reset 纯函数） | EditorApp 薄壳（playDiag 回传）；四成员出编辑器 |
| ECS→渲染提取 | `Engine/Renderer/SceneExtractor` + `RenderExtractSystem`（**SystemStage::Extract 首个真实现**） | ViewportRenderer 三成员删、薄壳（**每渲染帧直调时序保留**——插值 simAlpha 契约）；`World::Step` 补跑 Extract 阶段（零注册系统 = no-op） |

接口形态承 b2 `SpriteRefSource` 纪律：每件最小虚接口（PrefabSource/UiDocSource/AudioSource）+ 编辑器适配器小结构；运行时（AssetIndex）侧适配归批④。日志字符串与返回值语义逐字节保留（smoke 探针读返回值：`aud(mount=N)` 改读 `AudioMount::LoadedCount()`）。

## D5 批量帧护栏——三层收敛史（本批最有价值的一课）

评审 §D5：批量块组件指针全在 `lemon_scripts_tick` 前收集、生命周期跨整个托管 tick；窗口内 Spawn/get-or-create 可使 EnTT packed 池越容量重分配 → 悬垂（潜伏 AV）。

1. **否案B（禁同步生成）**：模板/svr-test 的 Update 路径大量 `Instantiate.Prefab/Spawn`（刀/宝石/盟友），禁通道 = 既有局断裂。
2. **首版（精确比对 fail-stop）**：gather 期记录被查询池 dense 基址（`ComponentMeta.poolDataFn`），窗口内三结构入口比对，真实搬移才置 `BatchSystemFrame.stale`（C# 块循环头跳过剩余块）。**被 script-chain 冒烟证伪**：SpawnerBehaviour 每帧 1 只的正当负载也会在几何扩容边界触发真实搬移 → 截断 + 红字 → errors=3 FAIL。教训：**基址比对是悬垂的精确判据，不是正当/病态的判据**——边界跨界是合法负载常态。
3. **终态两层**：第一层 = gather 前 `reserve(count + 1024)`（`ComponentMeta.reserveFn`；entBuf_ "构造期零扩容"技术下沉组件池）→ 合法负载零重分配零触发（修复后 script-chain 单步复验 PASS、零红字）；第二层 = 超余量爆量的病态兜底（比对 + stale + 红字指路分帧）。代价 ~24KB/池，稳态 reserve no-op。

`BatchSystemFrame` 尾加 `stale`（sizeof 40→48，C++/C# 两侧同步——帧瞬态结构不入 StateHash/回放档）。

## 验证

| 门 | 结果 |
|---|---|
| 构建 | 0 error（引擎/编辑器/测试/imgui-isolation） |
| ctest | 3/3 |
| engine-tests | **34210**（34145→+65：SaveStore 直测改写 +2 探针 / PrefabCache / CameraFollow / SceneExtractor / PoolDataStable） |
| script-tests | **1780**（1776→+4：D5 负例 = BurstSpawnSystem 首块爆 3000 > 余量 1024 → stale 置位 / 生成照常落地 / 后续帧零误报） |
| 回归 | **full 17/17**（首轮 15/17：smoke-drag 注入抖动——HEAD 基线同场再现、与批③ 无因果，复跑绿 = T1 先例；script-chain FAIL = D5 首版误伤，两层修正后过） |
| 金回放零重录 | **双证**：①三档现录现放 mismatches=0（sim st/mt + script）②**跨版本**——HEAD stash 构建录档 → 批③ 构建回放 mismatches=0（sim+script，终态 alive/created/destroyed 逐项一致） |
| bench-survivor | 1000 帧 ×3 = fps=**77/77/80** 全 PASS（alive=10493 与 b0 基线一致、playerHp=144823 三跑逐位一致、波动带 73–84 内零降级） |
| 净删佐证 | Editor diff **−399 行**（155+/554−）；新引擎件 12 文件 867 行 |

## 登记/移交

- smoke-drag 抖动本次三跑三态（OK/FAIL-selΔ/FAIL-模式漂移）——注入时序类，建议后续批把该步改"两次取优"或放宽子判据容差（既有 T1 先例只是复跑，机器化收口欠账）。
- 批④ 装配序列全就位：六件 + RenderExtractSystem（AddSystem 挂 Extract）+ hooks 三族 + 字体/entry dll 解析链。
- 音频运行时源需 importer 字段（loop/preload）——AssetIndex manifest 扩展或 .meta 回读，批④ 开工定。
