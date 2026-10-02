# M7a 实施计划 —— 独立运行时 + 最简出包（2026-10-01 规划：mac 先行 → Windows 真机收口）

Status: planned（批⓪–批⑧ 拆解完毕待开工；决策点 D1–D8 待开工日拍板 → ADR-016）

> 总览页惯例（M6b/M6c 同款）：每批一个文件（落 `Plans/M7a/`），开工前分解到文件/行级，完工后批文件内勾销；事件流水与实测数字记 [DevLog](../../DevLog/)。设计定形物在 ADR-016（开工日随 D1–D8 拍板定稿），本页只做拆解与验收映射。
>
> 输入：[评审建议书](../../Reports/2026-09-30-engineering-recommendations.md) R1（最小面四件套）+ [架构评审](../../Reports/2026-09-30-architecture-and-defect-review.md) §5.3/§5.4/§5.7（无 GameEntry / 运行时零 PNG 解码 / 资产库住在 lemon-editor-core）+ [M7.md](../M7/M7.md)（批⓪ Gate C 清障已 done）+ [ADR-015](../../ADR/ADR-015-Audio-System-And-Baked-Format.md)（`.baked` 容器家族 v1：音频 LBA1 已定形，version 字段 + packager 单点消费）+ [06 §6.1](../../EngineDesign/06-Asset-Pipeline-Out-of-Box.md)（出包流程图）。

## 0. 平台顺序（2026-10-01 用户拍板）

**mac 先行 → Windows 真机收口**。理由：开发机即 mac，运行时资产层/GameEntry/packager 的全部判据先在本机快速闭环；Windows 依赖真机窗口（M7 批⓪ 尾巴：MSVC 侧从未真机编译过），放末批一次性验证最终形态，减少往返。**目标平台集合不变**——windows-x64 仍是发布 v1（06 §6.1 口径），macos 为开发可用级；"Windows 优先"（2026-09-30 重排）指的是出包线优先于玩法特性（对 M6d 的排序），不是 M7a 内部的平台实现顺序。落账：06 §6.1 注记 + 本页。

## 1. 现状盘点（开工前事实基线，2026-10-01 探索核实）

| # | 事实 | 对本里程碑的含义 |
|---|---|---|
| 1 | `STB_IMAGE_IMPLEMENTATION` 全仓唯一 TU 在 `Editor/Tooling/StbImpl.cpp`（editor-core） | 运行时零 PNG 解码——R1 主体工作项 |
| 2 | 资产 DB 全家（AssetDatabase 扫描/.meta/manifest 读写、AssetGpuCache 解码上传、ClipEdit/ControllerEdit/Csv 解析器）住 `lemon-editor-core` | `Engine/Assets` 层 = 下沉/新建，不是从零发明 |
| 3 | `.baked` 现存唯一类型 = 音频 LBA1（引擎 TU，writer+reader+流式全具备）；图集 `.baked` 不存在（精灵=一文件一纹理页，无装箱） | packager 消费面：音频现烤即得；图集容器 v1 是本里程碑定形物 |
| 4 | EnterPlay 装配清单（全在编辑器）：场景 `SceneArchive::Load`（引擎件✓）+ GUID 归一 `ResolveSpriteRefs` + PrefabCache/`SetSpawnFn` + Clip/Controller/Table 三缓存（编辑器解析器）+ 存档三通道 + `MountSceneUiDocuments` + `MountPlayAudio`/音频后端 + hooks 三族（`SetEditorAssetHooks`/`SetScriptIoHooks`/`SetUiHooks`）+ 相机 follow + InputState 构建 | 批②③ 的搬运清单（`ScriptHost.h:132-146`：hooks 不装 = C# 侧静默返回 0——lemon-game 必须自己装） |
| 5 | 唯一近运行时原型 = `Samples/anim-smoke/main.cpp`（窗口/RHI/SpriteBatcher/World/ScriptHost 最小循环，程序化像素无资产）；`--bench-survivor` 是编辑器内旗标非独立件 | GameEntry 的循环形态对标 anim-smoke + spike-04（RmlUi over RHI 独立渲染先例） |
| 6 | 打包破坏点（R1 判据预言的隐式假设）：`LEMON_SCRIPT_DIR`（编译期指向构建树）/`LEMON_TEMPLATE_DIR`/`LEMON_ENGINE_FONT_DIR`（编译期源码树绝对路径）；pipeline cache cwd 相对（`.lemon/editor/…`、RHI 默认 `.lemon/pipeline-cache.bin`）；CoreCLRHost dotnet 根 POSIX 默认 `/usr/local/share/dotnet`；字体分发形态原归 M8（M7a 便携拷贝过渡） | 批④⑤ 的清障清单 |
| 7 | 存档三通道 `<root>/.lemon/saves/`（项目相对✓，writer 在 EditorContext）；包形态写位（便携 vs OS 用户目录）ScriptHost.h 注记归 M8 | D-5 决策点 |
| 8 | Windows：五条编译阻断已清零（M7 批⓪）但真机从未编译；宽字符/长路径未处置；CI 仅 macOS runner；入口全 `int main`（无 wWinMain 问题） | 批⑦ = 首编清账 + 出包 + CI runner |
| 9 | `demo/svr-test` 已入 git（批⓪ R5）。script-spawn FAIL 两因**已清零 2026-10-01**（[核清](../../DevLog/2026-10-01-m7a-prereq-update-guid-collision.md) + [修复](../../DevLog/2026-10-01-m7a-prereq-fix-low32-collision.md)）：①SpawnerBehaviour = `demo/test` 工程的类（用户已删整目录）；svr-test 残留 ui.scene 悬空槽 → 整场景已删；script-spawn 冒烟门控修正（挂装置类先解析，真项目不再恒 FAIL）；②prefab guid 低 32 位碰撞（Player `7e5741_…01`/Mob `7e5710_…01`，Director/BossMob 同 `…02`；前条目十六进制勘误见修复 DevLog）→ 数据修复**三同步**（.meta / manifest 十进制 / GameFlow 常量；新 guid 随机 + 全宽低 32 双重唯一校验）+ 引擎防线**独立落地**（用户拍板不进 M7a 批）：`GenerateUniqueGuid`（发号期全宽 + 同域低 32 唯一）+ Rescan 五域碰撞红字体检（prefab/clip/animset/controller/table 各自键空间，只报不重发）；"键升 u64"被 03 §69 冻结 schema 否决（Spawner/Shooter.prefabId 恒 u32），C# Instantiate 路径本就全宽 | 已收口；孤儿 meta 一条已随清扫策略落地**自动清除**（2026-10-01 第三轮，[DevLog](../../DevLog/2026-10-01-orphan-meta-sweep-and-tombstone-retirement.md)——svr-test 体检红字归零）；回归夹具用 vs-survivor 模板拷贝件（hermetic）不变 |
| 10 | `Tools/` 目录已存在（editor-regression.sh）；根 CMake 无 `Tools/` 子目录、全仓无 `install()` 规则 | packager 落位 `Tools/packager/` + 新子目录挂载 |
| 11 | 收官基线（M6c 出口）：回归 full 17 步 / ctest 3/3 / 单测 34036 / script-tests 1771 / bench-survivor fps=82 / vtable 46 槽 / 系统 20 / 组件 id 至 31 | 全里程碑的"零降级"对表基线；**M7a 纪律：vtable/组件 id/系统序零变动 → 金回放零重录预期成立**（例外仅批⑥ 若编辑器侧采纳图集） |
| 12 | ECS→渲染提取现存**两套实现**且引擎管线 Extract 阶段空转：产品级 `ViewportRenderer::ExtractScene`（`Editor/Interaction/ViewportRenderer.cpp:404`——视口剔除/分桶/sortKey，签名锚 `EditorContext&` 不可直接搬运）+ 样例简化版 `Samples/anim-smoke/main.cpp:139-168`（匿名 namespace 简化版不可链接复用）；`SystemStage::Extract`（`SystemPipeline.h:17`）全仓零实现、`World::Step` 只跑 Essential+FixedTick（review 2026-10-02 #6） | 批③④ 补提取搬运项——漏列则 GameEntry 按计划对标 anim-smoke 极可能写出**第三套**实现 |

## 2. 决策点（D1–D8，开工日拍板 → ADR-016）

| # | 决策 | 选项与建议 |
|---|---|---|
| D1 | 平台顺序 | **已拍板 2026-10-01：mac 先行 → Win 收口**（§0） |
| D2 | packager 形态 | A. `Tools/packager/` 独立 C++ 目标 `lemon-packager` 链 lemon-engine（复用 AssetIndex/BakeAudioFile/图集 writer 单源，依赖向下合规）vs B. `lemon-editor --export` 子命令（最快但加深"编辑器即运行时"耦合——违背本里程碑动因）。**建议 A** |
| D3 | 结构化资产发布格式 | M7a = JSON 直拷（场景/Prefab/clip/controller/tab/rml/rcss 原样入包，解析器已在引擎——"目录拷贝"判据本义）；二进制 `.baked` 转换（加载快+轻度混淆，06 §4 双格式策略的发布态半边）归 M7b 资源校验批。**建议直拷** |
| D4 | 图集 `.baked` v1 范围 | 保底 = 容器头定形（家族第二员，魔数 `LAT1`）+ writer（网格/装箱简版）+ 引擎 reader + packager 消费路径；余量升 MaxRects 全量（06 §5 口径）。60fps 判据不依赖它（一文件一页现状可达），排批⑥ 不阻塞主线。**建议保底+视余量** |
| D5 | 包形态存档落位 | M7a = 便携式（包内 `data/.lemon/saves/`，与开发态同相对路径零分支）；OS 用户目录（%APPDATA% 等）归 M8（ScriptHost.h 既有注记口径）。**建议便携** |
| D6 | 入口场景声明 | `project.lemon` 增可选字段 `entryScene`（相对路径）；缺省回退 = 项目唯一 `.scene`，多场景缺字段 = 红字响亮。编辑器 ProjectWizard/VsTemplateGen 写入，demo/svr-test（`Scenes/MainMenu.scene`，M6b ③d-2 已定唯一入口）与模板回填。**建议加字段** |
| D7 | dotnet 分发形态 | packager 用 `dotnet publish <Game.csproj> -r osx-x64/win-x64 --self-contained`（runtime+hostfxr+Game.dll 一次成包，干净机零 dotnet 安装）；引擎件 Lemon.Entry/SDK/runtimeconfig 由 packager 从构建目录拷入同树。**建议 self-contained** |
| D8 | lemon-game 渲染合成 | A. 双 pass 直渲染 swapchain（sprite pass + RmlUi pass，spike-04 先例）vs B. 离屏 gameRT + blit（编辑器 ViewportRenderer 同构）。**建议 A 先行**（更少中间 RT），viewport/裁剪语义冲突则回退 B |
| D9 | 墓碑与孤儿 meta | **已拍板并落地 2026-10-01（独立微批，[DevLog](../../DevLog/2026-10-01-orphan-meta-sweep-and-tombstone-retirement.md)）**：墓碑退役（条目同轮出表；误删恢复 = .meta 随文件走 + 版本管理）；孤儿 .meta 零引用自动清扫 / 仍被引用保留 + 红字（引用面判据 = 数据文本 guid 三形态检索）+ Assets 菜单手动入口。三家引擎对照取证（Unity/Cocos 自动清、Cocos 3.8.1 修复弧线、Godot 社区插件盲清）见 [讨论 DevLog](../../DevLog/2026-10-01-m7a-prereq-update-guid-collision.md) 后续 |

## 3. 批次表

| 批 | 文件（开工日落名） | 主题 | 预估 | 出口判据 |
|---|---|---|---|---|
| ⓪ | 2026-10-XX-b0-kickoff-adr.md | 设计定形：ADR-016（D1–D8 拍板 + 容器家族全类型口径 + 出包布局图）+ `entryScene` 字段落地（解析/写入/双项目回填）+ 开工核对（svr-test WIP 基线对齐） | 0.5–1 天 | ADR-016 采纳；entryScene 回显；回归 17 步绿 |
| ① | 2026-10-XX-b1-defect-batch2.md | 缺陷第二批（评审 §8）：D6/D7/D8/M21/M22–M25（M13/M14 已证伪划掉）；D8 修在搬运前随批③ 迁移 | 1–1.5 天 | 每修带阴性验证；回归 full 17/17 + 单测增长 |
| ② | 2026-10-XX-b2-engine-assets-core.md | `Engine/Assets` 资产读取核心：stb TU 入引擎 + AssetIndex（meta 扫描 + manifest 只读快路径 + 确定性 spriteId 派生）+ TextureStore（解码→AtlasRegistry）+ 解析器三件下沉 + `ResolveSpriteRefs` 下沉 + project.lemon 引擎侧只读解析 | 2–2.5 天 | ctest 3/3 + 单测增长；编辑器改调后回归 17/17 + **金回放零重录**（纯搬家强验证） |
| ③ | 2026-10-XX-b3-play-assembly-sink.md | Play 装配下沉：PrefabCache/`SetSpawnFn`（**D5 护栏随此**：批量帧指针失效防护）+ SaveStore 三通道 + UiMount + AudioMount + CameraFollow 共享助手；编辑器改薄壳委托（搬家非复制，diff 净删佐证） | 2–2.5 天 | 同批② 口径 + smoke-template/uirml/audio 全绿 |
| ④ | 2026-10-XX-b4-gameentry-vertical.md | `Engine/Entry/GameEntry` + `add_executable(lemon-game)`（无 ImGui 无 editor-core，ADR-005 同源双入口兑现）：装配序列 + hooks 三族安装 + 字体/entry dll 解析链（去编译期宏依赖）+ 直渲染循环（D8）+ `--frames/--smoke` 自动化位 + 回归 game-smoke 步（17→18） | 3–4 天 | **mac `lemon-game --project demo/svr-test` 四屏全流程零 C++（真人初验）**；`git clean -xfd`（删 `.lemon/`）后再跑成功；fps 采样 ≥60 |
| ⑤ | 2026-10-XX-b5-packager-mac.md | `Tools/packager`（D2）：目录拷贝组装（lemon-game + dylib 闭包 @loader_path + `dotnet publish` self-contained runtime/ + data/ 直拷 + 音频现烤入包 + manifest.pkg.json + 最小项目校验） | 2.5–3 天 | **干净目录包（无引擎仓/brew/dotnet）解包即跑四屏 60fps**；packager 自检（otool 依赖闭环 + 文件清单）绿 |
| ⑥ | 2026-10-XX-b6-atlas-baked-v1.md | 图集 `.baked` v1（D4）：`LAT1` 容器 + packager 侧 writer + `Engine/Assets` reader + 消费路径；启动对表（解码耗时/纹理槽/内存）；编辑器是否切换图集页开工定（默认 packager 专用=金回放零影响） | 2–3 天 | 包内 sprite 链路全走图集 `.baked`；60fps 复验；格式字节表入 ADR-016 追记 + 06 §5 注记 |
| ⑦ | 2026-10-XX-b7-windows-closure.md | Windows 真机收口：首编清账（批⓪ 尾巴①：`win` preset 全目标 + 宽字符/长路径 + 07 §3.5 行为表）+ lemon-game/packager win 出包（`-r win-x64` + vulkan-1.dll 闭包）+ CI windows runner 编译门禁 | 2–3 天 + 机器窗口 | **干净 Win 机包跑通四屏 60fps**（08 M7a 判据 Win 半闭环）；07 §3.5 逐项勾 |
| ⑧ | 2026-10-XX-b8-closeout.md | 收官：CI 每日回归 + 性能基线门禁（09 §9 欠账）+ 文档五区落账（06 §4–§6 实测对表 / 07 矩阵行 / 08 勾销 / 05 Play=编辑器特权注记 / 01 分叉消除）+ 真人验收总成 + M7b 移交清单 | 1 天 | 真人验收过；08 M7a 行勾销 |

合计 ≈ 16.5–20.5 工作日。批⑤/批⑥ 顺序可换（图集先行则 Win 一次验证终态；机器窗口先到则批⑦ 可提前，批⑥ 随后补）。

## 4. 各批分解（任务级；行级分解落批文件）

### 批⓪ 设计定形 + 决策拍板

- ADR-016 `docs/ADR/ADR-016-Standalone-Runtime-And-Minimal-Packager.md`：D1–D8 拍板结果；`.baked` 家族全类型口径（音频 LBA1 既有 / 图集 `LAT1` 本里程碑 / 结构化类型 M7a JSON 直拷声明）；出包布局图（下）；saves 便携落位；lemon-game CLI 形态。
- 出包布局（ADR-016 草案，目录拷贝式 v1）：

```
MyGame-mac/                       # 出包根（zip 归 M7b；.app bundle/签名/公证归 M7b）
├── lemon-game                    # 可执行（lemon-engine 静态链入）
├── libvulkan.1.dylib / MoltenVK.dylib / libSDL3*.dylib   # 依赖闭包，rpath=@loader_path
├── runtime/                      # dotnet publish self-contained 产物 + Lemon.Entry/SDK + runtimeconfig
└── data/                         # = 项目数据根：project.lemon / Assets(+meta) / Scenes / Prefabs / UI / tables
                                   #   .lemon/baked/audio/（现烤）/ manifest.pkg.json / Fonts/ / .lemon/saves/(运行期生成)
```

- `entryScene`：`EditorAppScripts.cpp:40-69`（OpenProjectPipeline 解析）+ `ProjectWizard.cpp:109-134` / `VsTemplateGen.cpp:1760`（写入）+ 引擎侧只读解析进批②；demo/svr-test 与 Templates/vs-survivor 回填（模板 `Scenes/Main.scene`）。
- 开工核对（**已全部收口 2026-10-01**，[核清](../../DevLog/2026-10-01-m7a-prereq-update-guid-collision.md) + [修复](../../DevLog/2026-10-01-m7a-prereq-fix-low32-collision.md)）：①`demo/test` 用户已删 + ui.scene 整场景删 + script-spawn 冒烟门控修正；②guid 碰撞数据修复（.meta/manifest/GameFlow 三同步——manifest 记账优先会洗回 .meta 的坑实测撞出）+ 低 32 位防线独立落地（`GenerateUniqueGuid` 发号唯一 + 五域体检红字，用户拍板不进 M7a 批）；单测 +4 → 34040、ctest 3/3、回归 17/17 两轮、svr-test 碰撞红字 2→0。

### 批① 缺陷第二批（先行小批）

排序理由：毁资产类先修（D7 转义/M21 manifest 写加固——packager 与运行时都将消费这些文件）；D8 修在编辑器原位，批③ 搬运时随迁，避免"先搬后修"双触。明细见评审 §6/§8 对应条（D6 Play 只读旁路 / D7 JSON 转义+128B / D8 装载回滚 / M21 fsync+.bak / M22 events 越界 / M23 改名态复位 / M24 返回值 / M25 集 dirty）。流程照 M6c review 热修先例：修 + 回归锁 + 阴性验证。

### 批② Engine/Assets 资产读取核心

- `Engine/Assets/StbImage.cpp`：`STB_IMAGE_IMPLEMENTATION`（+ resize2 若 ThumbCache 需要；write 留编辑器截图用或全迁——开工按消费面定；THIRD_PARTY 无新登记，stb 已在册只动 TU 归属）。
- `Engine/Assets/AssetIndex.{h,cpp}`：只读索引。扫 `<root>/{Assets,Prefabs,Scenes}/**` 旁 `.meta`（guid/type/slice），**确定性 spriteId 派生**（路径排序单调发号——guid 是真源、id 是进程内派生号，M6a 批⓪ 口径，runtime 与编辑器 id 数值不同无害，装载后 `ResolveSpriteRefs` 按 guid 归一）；`.lemon/manifest.json` 在场且合法 = 快路径（直读 guid/spriteId/slice），缺失/损坏 = 回退扫描红字——**"git clean -xfd 后可启动"的机器保证**。
- `Engine/Assets/TextureStore.{h,cpp}`：解码 → `renderer::AtlasRegistry` 注册（AssetGpuCache 的运行时子集：无 watcher/缩略图/LRU；一文件一页现状形态不变）。
- 解析器三件下沉：`Editor/Assets/{ClipEdit,ControllerEdit,Csv}` 的 Parse 函数族 → `Engine/Assets/`（编辑器编辑态类原地留、改调引擎；`EditorContext::BuildPlay{Prefab,Clip,Controller,Table}Cache` 改调）。
- GUID 归一下沉：`EditorContext::ResolveSpriteRefs` 纯函数化入引擎（场景/Prefab 装载后统一归一）。
- `Engine/Assets/ProjectFile.{h,cpp}`：project.lemon 最小只读解析（name/guid/engineVersion/entryScene；engineVersion 不匹配 = 警告不阻断）。
- 单测：AssetIndex 扫描 vs manifest 快路径一致性（同项目两路 guid→path 全等）；解析器新旧对拍（搬家期双跑，全等后删旧）。
- 扫描纪律继承（2026-10-01 已定形落地，[DevLog](../../DevLog/2026-10-01-orphan-meta-sweep-and-tombstone-retirement.md)）：AssetIndex 复用孤儿清扫判定（GuidReferenced 引用面判据）/ 条目出表语义；顺手清 `missing` 恒真守卫 30+ 处（AssetDatabase/EditorContext/面板——字段仅剩编辑器内 Remove 的同帧隐藏位）。
- 风险：与编辑器 AssetDatabase 暂成双扫描器（写侧 DB vs 只读索引）——`.meta`/manifest 是共同事实源故无漂移，合并归 M8（登记项）。

### 批③ Play 装配下沉

搬运清单（编辑器 → 引擎，全部"搬家非复制"）：

| 件 | 源（编辑器） | 目标（引擎） |
|---|---|---|
| PrefabCache + spawn | `EditorContext::BuildPlayPrefabCache/SpawnPlayPrefab/InstantiatePrefabAsset`（`EditorContext.cpp:466-566`；scripts[] 递归解析随迁） | `Engine/Assets/PrefabCache`；**D5 护栏**（批量帧预收集指针失效防护，评审 §D5 两案开工定） |
| 存档三通道 | `EditorContext::{Load,Write}SaveFile`（`EditorContext.cpp:915-978`；.bak 轮转 + game.sav 迁移） | `Engine/Assets/SaveStore`；ScriptIoHooks 宿主实现共用 |
| UIDocument 挂载 | `EditorAppUiBridge::MountSceneUiDocuments`（`EditorAppUiBridge.cpp:178-222`；批① D8 修复随迁） | `Engine/Ui/UiMount` |
| 音频挂载 | `EditorAppScripts::MountPlayAudio/WirePlayAudioBackend`（`EditorAppScripts.cpp:172-298`；EnsureClipLoaded/烤制落 `.lemon/baked/audio/`（目录可写即建——判据允许运行期生成，不允许依赖预存在）/流式分流） | `Engine/Audio/AudioMount` |
| 相机 follow | `EditorApp::UpdateGameCameraFollow`（`EditorApp.cpp:1060-1109`）纯函数化 | `Engine/`（EditorApp/GameEntry 两薄壳，杜绝双实现漂移） |
| ECS→渲染提取 | `ViewportRenderer::ExtractScene`（`Editor/Interaction/ViewportRenderer.cpp:404-470`：视口剔除/分桶搬运/sortKey 装配） | `Engine/` 提取下沉（去 `EditorContext&` 锚定改世界参数，`SystemStage::Extract` 首个真实现 + `World::Step` 补跑 Extract 阶段；ViewportRenderer/GameEntry 两薄壳消费——现状盘点 #12 / review 2026-10-02 #6，防第三套实现） |

出口 = 回归 17 步 + 金回放零重录 + 三 smoke 链绿 + 编辑器侧净删行数（diff 佐证）。

### 批④ GameEntry + lemon-game 竖切

- `Engine/Entry/GameEntry.cpp` + CMake：`add_executable(lemon-game)` 链 lemon-engine + lemon-csharp（**不链 editor-core/ImGui**）。
- CLI：`--project <dir>`（缺省 = exe 旁 `data/`，包形态零参启动）+ `--scene <rel>` 覆盖 + `--frames N --smoke`（RESULT 行，回归口径）+ fps 采样打印。
- 装配序列（对标 `Samples/anim-smoke/main.cpp` 循环 + 批②③ 引擎件）：Window → Device/swapchain（pipeline cache 显式传包内可写路径或 null——cwd 相对默认不再触发）→ AssetIndex/TextureStore → AudioEngine → UiSubsystem（字体解析链：包内 `data/Fonts/` → 引擎源树回退）→ ScriptHost::Initialize（entry dll = exe 旁 `runtime/` 定位，**替换 LEMON_SCRIPT_DIR 编译期宏依赖**）→ LoadUserAssembly（dev 形态 `<root>/.lemon/bin/`，构建归编辑器/packager，lemon-game 只消费）→ hooks 三族安装（spriteOfGuid=AssetIndex / instantiatePrefab=PrefabCache / saveFlush=SaveStore / UI hooks=UiSubsystem）→ `SceneArchive::Load(entryScene)` + GUID 归一 → 四缓存 → UiMount/AudioMount → 主循环（InputState 直取 `Window::IsKeyDown` / fixed-step 1/60 / audio tick / camera follow / **ECS→渲染提取（批③ 下沉件，`SystemStage::Extract`）** / sprite pass + UI pass 直渲染，D8）。
- 回归：`tools/editor-regression.sh` 增 game-smoke 步（夹具 = 向导复制 vs-survivor 模板拷贝件，hermetic；17→18）；svr-test 留真人验收。

### 批⑤ packager 最简 + mac 干净包

- `Tools/packager/`（目录已存在装 editor-regression.sh，新增子目录）+ 根 CMakeLists 挂载；目标 `lemon-packager` 链 lemon-engine（D2）。
- CLI：`lemon-packager --project <dir> --runtime <engine-build-dir> --out <pkg>`。
- 组装（§4 批⓪ 布局图）：二进制 + dylib 闭包（`otool -L` 递归收 + `install_name_tool` rpath=@loader_path + ad-hoc 签名）；`runtime/` = `dotnet publish -r osx-x64 --self-contained`（D7）+ 引擎 Scripting 构建件拷入；`data/` = 项目直拷 + **音频现烤**（BakeAudioFile 源→包内同相对路径 `.lemon/baked/audio/`，运行时零分支）+ 字体拷入 + `manifest.pkg.json`（AssetIndex 导出，运行时快路径消费）+ entryScene 校验。
- 项目校验最小面：guid 冲突 / entryScene 缺失 / 引用悬空（.scene/.prefab 的 spriteGuid+prefab ref 扫描对索引）红字。
- 干净机判据模拟：全新临时目录解包即跑；packager 自检步（依赖闭环 + 文件清单 diff）。

### 批⑥ 图集 `.baked` v1

- `LAT1` 容器头（magic/version=1/页数/页宽高/条目数/条目表：guid+页号+uvRect；对齐 ADR-015 家族口径：version 字段 + packager 单点消费）；writer（D4 范围）落 packager；reader 落 `Engine/Assets/AtlasStore`（页位图→AtlasRegistry，运行时零 PNG）。
- 编辑器是否切换图集页：默认 **packager 侧专用**（编辑器维持一文件一页——像素断言/回归面不扩大，金回放零影响）；若开工定编辑器也切，则 spriteId 派生变 → 三档重录批内闭环（UIDocument/Tween 先例）。
- 对表：包启动耗时 / 纹理槽数（kMaxTextureSlots=256 消费面）/ 常驻内存，前后对比入 DevLog。

### 批⑦ Windows 真机收口

- 首编清账：`cmake --preset win`（VS2022 x64）全目标编译；宽字符/长路径处置（07 §3.6 伴生项"归真机首调"）；07 §3.5 行为验证表逐项（CJK 标题/拖拽路径/codepage）。
- 出包：`dotnet publish -r win-x64` + vulkan-1.dll loader 闭包（ICD 归显卡驱动）；packager win 路径自检（dumpbin 依赖）。
- CI：`.github/workflows/ci.yml` 加 windows runner（编译 + ctest 逻辑面，无 GPU 口径同 mac）。

### 批⑧ 收官落账

- CI 每日回归（定时 full 17→18 步）+ 性能基线门禁（bench-survivor fps 阈值）——09 §9 与 M7.md 关联节的欠账。
- 文档：06 §4（双格式策略现状注记）/§5（图集实测）/§6.1（出包流程对表）；07 矩阵加行（lemon-game/lemon-packager/dotnet 分发）；08 M7a 行勾销；05（"Play 从运行游戏的唯一方式降级为编辑器特权"落注）；01（§5 分叉清单消项：GameEntry/资产层/packager 三行从"目标"转"现状"）。
- 真人验收清单：svr-test 四屏全流程（零 C++）/ 干净包移交即玩 / 设置与存档持久化 / 暂停挂起续响。
- M7b 移交清单：压缩/加密/增量、.baked 全类型二进制转换+资源校验、安装器/Steam、.app bundle 签名公证、干净 Win 虚拟机安装回归。

## 5. 出口判据映射（08 §2 M7a 行 → 批）

| 08 判据 | 落点 |
|---|---|
| 干净 mac+Win 机 `lemon-game --project demo/svr-test` 跑满 60 帧、四屏全流程零 C++ | mac = 批④（--project 形态）+ 批⑤（干净包复验）；Win = 批⑦ |
| `git clean -xfd` 后无需 `.lemon/` 缓存 | 批②（AssetIndex 回退扫描 = 机器保证）+ 批④ 实测复验 |
| 隐含判据（R1 价值所在）：撞出工作目录/绝对路径/缓存位置隐式假设 | 批④⑤ 清障清单（现状盘点 #6）逐项收口 |

## 6. 范围边界（不做）

压缩/加密/增量更新（M7b）；结构化类型 `.baked` 二进制转换（M7b 资源校验批，D3）；安装器/Steam/云档/depot（M7b）；mac `.app` bundle/签名/公证（M7b）；OS 用户目录存档（M8，D5）；字体分发终案（M8，M7a 便携拷贝过渡）；ASTC/压缩纹理、NativeAOT（04 §8，后续）；模拟/渲染分线程（评审 R6 明确不动）；Assets.GuidOf C# 反查（M6c 登记项，批② 顺手则带否则不动）；编辑器图集化改造（默认不做，批⑥ 开工定）。

## 7. 风险与兜底

- **RmlUi 直渲染 swapchain 未知数**（UiSubsystem 现只经 ViewportRenderer gameRT 消费）：spike-04 已证 RmlUi over RHI 独立渲染；冲突则回退离屏 RT + blit（D8 双案）。
- **dylib 闭包**（干净机无 brew/无 Vulkan SDK）：otool 递归收齐 + @loader_path 重锚 + ad-hoc 签名；packager 自检步机器化。
- **self-contained publish 体积/时长**（~100MB 级）：登记不阻塞（压缩归 M7b）。
- ~~svr-test WIP 既有 FAIL~~ **已清零 2026-10-01**（ui.scene 删除 + guid 数据修复 + 冒烟门控修正，[DevLog](../../DevLog/2026-10-01-m7a-prereq-fix-low32-collision.md)）；回归夹具仍用模板拷贝件隔离（hermetic 纪律不变）。
- **搬家批回归面**：金回放零重录 + 17 步全绿 = "纯搬家"强验证；批②③ 期间不合入无关改动（M6c 批② 重录窗口先例）。
- **Windows 机器窗口**（批⑦ 唯一外部依赖）：mac 判据先行独立成立，批⑦ 可等窗口；批⑤⑥ 顺序可换以适配机器 availability。
- **图集改纹理布局触像素断言**：默认 packager 专用 = 编辑器回归零影响；若编辑器采纳则三档重录批内闭环。

## 8. 登记项（观察，不扩 scope）

- 双扫描器合并（AssetDatabase 写侧 vs AssetIndex 只读侧）→ M8；
- pipeline cache 包形态（M7a 先 null/显式路径，PSO 缓存收益后评）；
- `SetEditorAssetHooks` 更名（runtime 也装，名字撒谎；cosmetic 不急）；
- CI Windows GPU 形态长期案（无 GPU 逻辑门禁为 M7a 口径）；
- `dotnet publish` 产物确定性/缓存（CI 化后评）。

## 9. 并行依赖

- **上游**：M6c 已收官（音频 LBA1 容器 v1 定形 = packager 全类型消费的既有半边）；M7 批⓪ Gate C 清障 done。
- **消费面**：`demo/svr-test`（真人验收对象）+ `Templates/vs-survivor`（回归夹具源）。
- **下游**：M7b 发行侧（消费包产物与 manifest.pkg.json；.baked 校验扩展）、M8（字体分发终案/用户目录存档/双扫描器合并）、M9。

## 关联

- [M7.md](../M7/M7.md)（M7 家族枢纽：批⓪ done / 批② M7b）
- [08-Development-Roadmap](../../EngineDesign/08-Development-Roadmap.md) §2 M7 行 + 文首 2026-09-30 重排注记
- [06-Asset-Pipeline](../../EngineDesign/06-Asset-Pipeline-Out-of-Box.md) §4 双格式 / §5 图集 / §6.1 出包流程（mac 先行注记）
- [07-Porting-Matrix](../../EngineDesign/07-Porting-Matrix.md) §3.5 行为表 / §3.6 阻断处置
- [09-Testing](../../EngineDesign/09-Testing.md) §9 CI 门禁（每日回归/性能基线欠账）
- [ADR-005]（01-Architecture-Overview §ADR 表：同源双入口——批④ 兑现）/ [ADR-015](../../ADR/ADR-015-Audio-System-And-Baked-Format.md)（容器家族口径）
- [评审建议书](../../Reports/2026-09-30-engineering-recommendations.md) R1 / [架构评审](../../Reports/2026-09-30-architecture-and-defect-review.md) §5、§8
