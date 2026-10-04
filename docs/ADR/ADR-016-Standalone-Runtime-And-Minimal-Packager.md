# ADR-016：M7a 独立运行时 + 最简出包——lemon-game 入口、Engine/Assets 资产层与目录拷贝式 packager v1

- 日期：2026-10-03（M7a 批⓪ 设计定形）
- 状态：**已采纳**（**D2–D8 已拍板 2026-10-03 用户：均按建议**——D2 独立目标 lemon-packager（拍板前经 Unity/Godot/Unreal 三家对照质证：行业不变量 = 打包消费层与运行时读取层同源，宿主二进制不是关键；Unreal RunUAT 即 A 形态）、D3 结构化 JSON 直拷、D4 图集保底+视余量、D5 存档便携、D6 加 entryScene 字段、D7 dotnet self-contained、D8 双 pass 直渲染先行；D1 已拍板 2026-10-01、D9 已拍板并落地 2026-10-01）
- 影响：[08-Development-Roadmap](../EngineDesign/08-Development-Roadmap.md) §2 M7a 行、[06-Asset-Pipeline](../EngineDesign/06-Asset-Pipeline-Out-of-Box.md) §4（双格式现状注记）/§5（图集 LAT1）/§6.1（出包流程对表）、[07-Porting-Matrix](../EngineDesign/07-Porting-Matrix.md)（lemon-game/lemon-packager/dotnet 分发矩阵行，批⑦/批⑧ 落）、01 §5 分叉清单（GameEntry/资产层/packager 三行从「目标」转「现状」，批⑧）、[M7a.md](../Plans/M7a/M7a.md)（批次拆解总览）、M7b（消费包产物与 manifest.pkg.json）

## 背景

1. **评审定调**（[评审建议书](../Reports/2026-09-30-engineering-recommendations.md) R1 + [架构评审](../Reports/2026-09-30-architecture-and-defect-review.md) §5.3/§5.4/§5.7）：引擎内核质量已高，但「还不能出包的二进制」是当前最大风险。三个结构性缺口：
   - **无独立入口**：Play 是编辑器特权，EnterPlay 装配清单（场景装载/GUID 归一/四缓存/UI·音频挂载/hooks 三族/相机 follow）全在编辑器侧，`lemon-game` 不存在（ADR-005「同源双入口」自 M1 起是目标态）；
   - **运行时零 PNG 解码**：`STB_IMAGE_IMPLEMENTATION` 全仓唯一 TU 在 `Editor/Tooling/StbImpl.cpp`（editor-core）——脱离编辑器连贴图都解不出来；
   - **资产库住在 lemon-editor-core**：AssetDatabase 扫描/.meta/manifest 读写、AssetGpuCache 解码上传、ClipEdit/ControllerEdit/Csv 解析器全家在编辑器目标里。
2. **`.baked` 家族半边已定形**：M6c 收官后音频 LBA1（ADR-015）是首个真实落地类型，writer+reader+流式全具备——packager 的音频消费面现成；图集容器是本里程碑第二员。
3. **金回放纪律约束搬运设计**：组件名无条件入状态哈希流——M7a 纪律 = **vtable 尾加以外零变动、组件 id/系统序零变动 → 三档金回放零重录预期成立**（例外仅批⑥ 若编辑器侧采纳图集改纹理布局）。
4. **开工基线**（2026-10-03 刷新，批③ review 修完后）：回归 full 17 步 / ctest 3/3 / 单测与 script-tests 计数 / bench-survivor fps / vtable **47 槽**（46 + review 2026-10-02 #71 audioPausedGet 表尾追加）/ 系统 20 / 组件 id 至 31——实测数字见[开工 DevLog](../DevLog/2026-10-03-m7a-b0-kickoff-adr-baseline.md)。

## 决策

### M1 平台顺序（D1，已拍板 2026-10-01）

**mac 先行 → Windows 真机收口**。开发机即 mac，运行时资产层/GameEntry/packager 全部判据先在本机快速闭环；Windows 依赖真机窗口（MSVC 侧从未真机编译），放末批（批⑦）一次性验证最终形态。**目标平台集合不变**：windows-x64 仍是发布 v1（06 §6.1 口径），macos 为开发可用级。已落账 06 §6.1 注记。

### M2 lemon-game 独立入口（ADR-005 同源双入口兑现）

- 落位 `Engine/Entry/GameEntry.cpp` + CMake `add_executable(lemon-game)`：链 lemon-engine + lemon-csharp，**不链 editor-core/ImGui**——「编辑器→内核→平台层」依赖向下合规的可执行证明。
- **CLI 形态**：

```
lemon-game [--project <dir>] [--scene <rel>] [--frames N] [--smoke] [--validate]
  --project   项目根（含 project.lemon）；缺省 = exe 旁 data/（包形态零参启动）
  --scene     相对路径覆盖入口场景（缺省 = project.lemon entryScene → 回退唯一 .scene）
  --frames N  跑 N 帧退出（自动化）
  --smoke     机器判据：终帧 RESULT 行（回归口径，与编辑器各 smoke 链同款）
  --validate  Vulkan 验证层（开发自测）
```

- 装配序列（对标 `Samples/anim-smoke/main.cpp` 最小循环 + spike-04 RmlUi over RHI 先例 + 批②③ 下沉件）：Window → Device/swapchain（**pipeline cache 显式传包内可写路径或 null**——cwd 相对默认不再触发）→ AssetIndex/TextureStore → AudioEngine（静音降级一等公民）→ UiSubsystem（字体解析链：包内 `data/Fonts/` → 引擎源树回退）→ ScriptHost::Initialize（**entry dll = exe 旁 `runtime/` 定位，替换 `LEMON_SCRIPT_DIR` 编译期宏依赖**）→ LoadUserAssembly（dev 形态 `<root>/.lemon/bin/`；构建归编辑器/packager，lemon-game 只消费）→ hooks 三族安装（spriteOfGuid=AssetIndex / instantiatePrefab=PrefabCache / saveFlush=SaveStore / UI hooks=UiSubsystem——hooks 不装 = C# 侧静默返回 0，`ScriptHost.h:132-146` 注记的坑）→ `SceneArchive::Load(entryScene)` + GUID 归一 → 四缓存 → UiMount/AudioMount → 主循环（InputState 直取 `Window::IsKeyDown` / fixed-step 1/60 / audio tick / camera follow / **ECS→渲染提取** / sprite pass + UI pass）。
- **D8 渲染合成（已拍板 2026-10-03：A 先行）**：双 pass 直渲染 swapchain（sprite pass + RmlUi pass，spike-04 先例）——比编辑器 ViewportRenderer 的离屏 gameRT + blit 少一中间 RT；viewport/裁剪语义冲突则回退 B（离屏 RT + blit，与 ViewportRenderer 同构）。批④ 开工定回退与否。
- 清障清单（现状盘点 #6，批④⑤ 逐项收口）：`LEMON_SCRIPT_DIR`/`LEMON_TEMPLATE_DIR`/`LEMON_ENGINE_FONT_DIR` 编译期源码树绝对路径、pipeline cache cwd 相对、CoreCLRHost dotnet 根 POSIX 默认 `/usr/local/share/dotnet`、字体分发形态原归 M8（M7a 便携拷贝过渡）。

### M3 `Engine/Assets` 运行时资产读取核心（批②）

全部「下沉/新建」而非从零发明：

- `Engine/Assets/StbImage.cpp`：`STB_IMAGE_IMPLEMENTATION` TU 入引擎（THIRD_PARTY 无新登记——stb 已在册只动 TU 归属；write 留编辑器截图用，开工按消费面定）。
- `Engine/Assets/AssetIndex`：只读索引。扫 `<root>/{Assets,Prefabs,Scenes}/**` 旁 `.meta`（guid/type/slice）；**确定性 spriteId 派生**（路径排序单调发号——guid 是真源、id 是进程内派生号，runtime 与编辑器 id 数值不同无害，装载后 `ResolveSpriteRefs` 按 guid 归一）；`.lemon/manifest.json` 在场且合法 = 快路径，缺失/损坏 = 回退扫描红字——**「git clean -xfd 后可启动」的机器保证**。
- `Engine/Assets/TextureStore`：解码 → `renderer::AtlasRegistry` 注册（AssetGpuCache 的运行时子集：无 watcher/缩略图/LRU；一文件一页现状形态不变）。
- 解析器三件下沉：ClipEdit/ControllerEdit/Csv 的 Parse 函数族 → `Engine/Assets/`（编辑器编辑态类原地留、改调引擎）；GUID 归一 `ResolveSpriteRefs` 纯函数化入引擎。
- `Engine/Assets/ProjectFile`：project.lemon 最小只读解析（name/guid/engineVersion/entryScene；engineVersion 不匹配 = 警告不阻断）。
- 扫描纪律继承（2026-10-01 定形）：AssetIndex 复用孤儿清扫判定（GuidReferenced 引用面判据）/条目出表语义。
- **已知债务（登记不阻塞）**：与编辑器 AssetDatabase 暂成双扫描器（写侧 DB vs 只读索引）——`.meta`/manifest 是共同事实源故无漂移，合并归 M8。

### M4 Play 装配下沉（批③，搬家非复制，diff 净删佐证）

| 件 | 源（编辑器） | 目标（引擎） |
|---|---|---|
| PrefabCache + spawn | `EditorContext::BuildPlayPrefabCache/SpawnPlayPrefab/InstantiatePrefabAsset`（scripts[] 递归解析随迁） | `Engine/Assets/PrefabCache`（D5 护栏：批量帧预收集指针失效防护） |
| 存档三通道 | `EditorContext::{Load,Write}SaveFile`（.bak 轮转 + game.sav 迁移） | `Engine/Assets/SaveStore`（ScriptIoHooks 宿主实现共用） |
| UIDocument 挂载 | `EditorAppUiBridge::MountSceneUiDocuments` | `Engine/Ui/UiMount` |
| 音频挂载 | `EditorAppScripts::MountPlayAudio/WirePlayAudioBackend`（烤制落 `.lemon/baked/audio/`，目录可写即建） | `Engine/Audio/AudioMount` |
| 相机 follow | `EditorApp::UpdateGameCameraFollow` 纯函数化 | `Engine/`（EditorApp/GameEntry 两薄壳，杜绝双实现漂移） |
| ECS→渲染提取 | `ViewportRenderer::ExtractScene`（视口剔除/分桶/sortKey） | `Engine/` 提取下沉（去 `EditorContext&` 锚定改世界参数；**`SystemStage::Extract` 首个真实现** + `World::Step` 补跑 Extract 阶段——现状两套实现且引擎管线 Extract 空转，防第三套） |

### M5 `.baked` 家族全类型口径（本 ADR 定形物）

| 资产类型 | M7a 出包形态 | 容器 |
|---|---|---|
| 音频（.wav/.ogg/.mp3/.flac） | **现烤 `.baked` 入包**（M6c 已定形，ADR-015） | `LBA1` v1 已定形——writer/reader/流式全具备 |
| 图集（sprite 页） | 批⑥ 保底定形：packager 侧 writer + 引擎 reader（D4 已拍板：保底 + 视余量升 MaxRects 全量；60fps 判据不依赖它，排批⑥ 不阻塞主线） | `LAT1` v1——本 ADR 草案头表（下），字节表批⑥ 定稿追记实测 |
| 结构化（.scene/.prefab/.clip/.controller/.tab/.rml/.rcss） | **D3 已拍板 2026-10-03：JSON 直拷**（「目录拷贝」判据本义；解析器已在引擎） | 无容器——二进制转换（加载快+轻度混淆）归 M7b 资源校验批（06 §4 双格式策略的发布态半边） |

**家族共同口径**（ADR-015 M2 确立，本 ADR 重申并扩展到全类型）：magic 前缀 `L` + 类型字母 + 版本号（`LBA1`/`LAT1`/…）；**version 字段保升级通道**（v1 有误可升 v2 不破包）；**packager 单点消费**（编辑器/运行时只读）；运行时零解码器/零解析热路径。

**`LAT1` 容器头 v1 定稿**（小端；2026-10-04 批⑥ writer 实现定稿，替换下方原草案——差异：页尺寸表按页存储（裁剪页/专属页不可用单值表达）、`payloadBytes` 落 28（64 位域对账防回绕，LBA1 review 先例）、条目表 18B 紧排字节装配）：

| 偏移 | 大小 | 字段 | 说明 |
|---|---|---|---|
| 0 | 4 | magic | `"LAT1"`（Lemon Baked Atlas v1） |
| 4 | 2 | version | u16 = 1 |
| 6 | 2 | headerSize | u16 = 32（自校验，对齐 LBA1 布局习惯） |
| 8 | 2 | pageCount | u16 页数（≥1；0 页拒载） |
| 10 | 2 | flags | u16 = 0 预留（采样/旋转策略归消费侧渲染配置，不入容器） |
| 12 | 4 | pageWidth | u32 虚拟装箱页宽（=4096；参考值，oversized 专属页不改变此字段） |
| 16 | 4 | pageHeight | u32 同上 |
| 20 | 4 | entryCount | u32 条目数（≥1） |
| 24 | 4 | reserved | u32 = 0 |
| 28 | 4 | payloadBytes | u32 Σ页 w×h×4（读取侧 64 位域对账；>4GiB 拒烤——压缩/分卷归 M7b） |
| 32 | 8×pageCount | pageDims | 每页 w u32 + h u32——**裁剪后真实尺寸**（4px 对齐；oversized 条目 = 精灵尺寸同规则） |
| … | 18×entryCount | entries | guid u64 + page u16 + x/y/w/h u16——页内**像素矩形 = 精灵本体**（gutter 是装载期采样防线，不入几何；spriteId 不入容器：manifest 记号账、LAT1 记几何账，packager 同轮生成保证一致，装载以 guid 为 join 键） |
| … | Σ | payload | 页序 RGBA8 top-down 紧排（stb 解码行序，直传 `UploadTexture`——运行时零解码） |

**几何/装箱语义（批⑥ 定稿）**：虚拟 4096 装箱 + shelf 行式（确定性排序 h desc → w desc → guid asc——同输入同字节，包可复现面）；精灵间 gutter 2px 透明（线性采样防渗色）；页右/下裁剪到用到 extent（4px 对齐——vs-survivor 模板 7 精灵一页 124KiB，固定 4096 页则 64MiB）；任一边 >4096 的精灵 → **专属页**（(0,0) 起独占，页边缘 clamp-to-edge 兜底）；任一边 >16384 拒烤（GPU maxImageDimension2D 域）。切片像素几何不入容器——装载期从 `.meta` 网格重派生子矩形（`RegisterGridSlices`，dev 一文件一页路径同款语义）。落位 `<root>/.lemon/baked/atlas/atlas.baked`（单图集 v1；多图集组归 06 §5 后续）。磁盘代价：RAW RGBA 相对 PNG 压缩源膨胀（模板实测 48KB→124KiB，2.6×）——启动零解码判据优先，压缩归 M7b；MaxRects 全量升级视余量（D4 保底口径）。

### M6 packager 形态与出包布局（D2，已拍板 2026-10-03：A 独立目标）

- **D2 拍板 A 独立目标**：`Tools/packager/` 子目录 + 根 CMakeLists 挂载；目标 `lemon-packager` 链 lemon-engine（复用 AssetIndex/BakeAudioFile/图集 writer 单源，依赖向下合规）。弃 `lemon-editor --export` 子命令案——打包逻辑住 editor-core 则「引擎独立于编辑器」退化为口头承诺（A 案 = 链接器层面不可违），且批⑤ 逐次自测/批⑧ CI 出包拖全编辑器二进制。编辑器侧「Export…」按钮留作后续 UX 壳（spawn lemon-packager，Unreal RunUAT 同款形态），不改变代码归属。
- CLI：`lemon-packager --project <dir> --runtime <engine-build-dir> --out <pkg>`。
- 组装步骤：二进制 + dylib 闭包（`otool -L` 递归收 + `install_name_tool` rpath=@loader_path + ad-hoc 签名）→ `runtime/`（D7）→ `data/` 项目直拷 + **音频现烤**（BakeAudioFile 源→包内同相对路径 `.lemon/baked/audio/`，运行时零分支）+ 字体拷入 + `manifest.pkg.json`（AssetIndex 导出，运行时快路径消费）+ entryScene 校验。
- **项目校验最小面**：guid 冲突 / entryScene 缺失 / 引用悬空（.scene/.prefab 的 spriteGuid + prefab ref 扫描对索引）红字。
- **出包布局（目录拷贝式 v1）**：

```
MyGame-mac/                       # 出包根（zip 归 M7b；.app bundle/签名/公证归 M7b）
├── lemon-game                    # 可执行（lemon-engine 静态链入）
├── libvulkan.1.dylib / MoltenVK.dylib / libSDL3*.dylib   # 依赖闭包，rpath=@loader_path
├── runtime/                      # dotnet publish self-contained 产物 + Lemon.Entry/SDK + runtimeconfig
└── data/                         # = 项目数据根：project.lemon / Assets(+meta) / Scenes / Prefabs / UI / tables
                                   #   .lemon/baked/audio/（现烤）/ manifest.pkg.json / Fonts/ / .lemon/saves/(运行期生成)
```

### M7 dotnet 分发（D7，已拍板 2026-10-03：self-contained）

packager 用 `dotnet publish <Game.csproj> -r osx-x64/win-x64 --self-contained`——runtime + hostfxr + Game.dll 一次成包，**干净机零 dotnet 安装**；引擎件 Lemon.Entry/SDK/runtimeconfig 由 packager 从构建目录拷入同树。弃 framework-dependent 案（干净机判据直接不过）。体积 ~100MB 级登记不阻塞（压缩归 M7b）。

### M8 入口场景声明（D6，已拍板 2026-10-03：加字段）

`project.lemon` 增**可选**字段 `entryScene`（相对路径，如 `Scenes/MainMenu.scene`）；解析回退链：`entryScene` → 项目唯一 `.scene` → 多场景缺字段 = 红字响亮。写入面：ProjectWizard/VsTemplateGen；回填面：`demo/svr-test`（`Scenes/MainMenu.scene`，M6b ③d-2 已定唯一入口）+ `Templates/vs-survivor`（`Scenes/Main.scene`）。引擎侧只读解析随批② ProjectFile 落地。

> **落地注记（2026-10-03 批⓪）**：编辑器侧已收口——OpenProjectPipeline 解析回显（`EntryScene()` + 在场性守卫：声明但文件缺 = WARN 走回退链）+ 向导双写点（模板分支重写前读模板 entryScene 随行、blank 分支显式写 `Scenes/Main.scene`）+ VsTemplateGen 写入 + 双项目回填；smoke-template RESULT 增 `entry(wiz/parse)` 双位锁（向导携带 + 开项目解析回显）带阴性验证（剥模板字段 → 播种期红字链死）。

### M9 包形态存档落位（D5，已拍板 2026-10-03：便携）

M7a = **便携式**：包内 `data/.lemon/saves/`，与开发态 `<root>/.lemon/saves/` 同相对路径——**零分支**（SaveStore 搬家即用）。OS 用户目录（%APPDATA%/~/Library/Application Support）归 M8（ScriptHost.h 既有注记口径）。writer 在编辑器 EditorContext 的现状随批③ SaveStore 下沉一并解决。

## 拍板项（2026-10-03 用户确认，D2–D8 均按建议）

| # | 决策 | 结果 |
|---|---|---|
| D1 | 平台顺序 | ✅ 已拍板 2026-10-01：mac 先行 → Win 收口（M1） |
| D2 | packager 形态 | ✅ **A 独立目标 `lemon-packager`**（拍板前 Unity/Godot/Unreal 三家对照质证——行业不变量 = 打包消费层与运行时读取层同源，宿主二进制非关键；Unreal RunUAT 即 A 形态。编辑器 Export 按钮留作后续 UX 壳） |
| D3 | 结构化资产发布格式 | ✅ JSON 直拷（二进制 `.baked` 转换归 M7b 资源校验批） |
| D4 | 图集 `.baked` v1 范围 | ✅ 保底（容器头 + writer/reader 简版）+ 视余量升 MaxRects 全量 |
| D5 | 包形态存档落位 | ✅ 便携（包内 `data/.lemon/saves/`；OS 用户目录归 M8） |
| D6 | 入口场景声明 | ✅ `project.lemon` 加可选 `entryScene` 字段（缺省回退链 + 多场景缺字段红字） |
| D7 | dotnet 分发形态 | ✅ self-contained（干净机零 dotnet 安装） |
| D8 | lemon-game 渲染合成 | ✅ A 双 pass 直渲染先行（冲突回退 B 离屏 RT + blit） |
| D9 | 墓碑与孤儿 meta | ✅ 已拍板并落地 2026-10-01（独立微批，墓碑退役 + 引用面清扫） |

## 后果与风险

- **金回放零重录预期**：批②③ 纯搬家 + vtable 尾加以外零变动 → 零重录强验证（回归 17 步 + 三档金回放全绿 + diff 净删）；**重录窗口期纪律**：批②③ 期间不合入无关改动（M6c 批② 先例）。例外路径仅批⑥ 编辑器侧采纳图集（默认不采纳——packager 专用，编辑器维持一文件一页，回归面零扩大）。
- **RmlUi 直渲染 swapchain 未知数**（UiSubsystem 现只经 ViewportRenderer gameRT 消费）：spike-04 已证独立渲染可行；冲突则 D8 回退 B 案。
- **dylib 闭包**（干净机无 brew/无 Vulkan SDK）：otool 递归收齐 + @loader_path 重锚 + ad-hoc 签名；packager 自检步（依赖闭环 + 文件清单 diff）机器化。
- **self-contained 体积/时长**（~100MB 级）：登记不阻塞（压缩归 M7b）；`dotnet publish` 产物确定性/缓存 CI 化后评。
- **双扫描器**（AssetDatabase 写侧 vs AssetIndex 只读侧）：共同事实源 = `.meta`/manifest，无漂移；合并归 M8。
- **Windows 真机窗口**（批⑦ 唯一外部依赖）：mac 判据先行独立成立，批⑦ 可等窗口；批⑤⑥ 顺序可换适配机器 availability。

## 范围边界（不做，M7a.md §6 口径重申）

压缩/加密/增量更新、结构化类型 `.baked` 二进制转换、安装器/Steam/云档/depot、mac `.app` bundle/签名/公证、OS 用户目录存档、字体分发终案（M7a 便携拷贝过渡）、ASTC/压缩纹理、NativeAOT、模拟/渲染分线程——全部 M7b/M8/后续；编辑器图集化改造默认不做（批⑥ 开工定）。

## 关联

- [M7a.md](../Plans/M7a/M7a.md)（批次拆解：批⓪–批⑧；本 ADR = 其 §2 决策点的拍板载体）
- [ADR-015](./ADR-015-Audio-System-And-Baked-Format.md)（`.baked` 家族口径与 LBA1 字节表）
- [06-Asset-Pipeline](../EngineDesign/06-Asset-Pipeline-Out-of-Box.md) §4 双格式 / §5 图集 / §6.1 出包流程
- [07-Porting-Matrix](../EngineDesign/07-Porting-Matrix.md) §3.5 行为表 / §3.6 阻断处置
- [09-Testing](../EngineDesign/09-Testing.md) §9 CI 门禁（批⑧ 补每日回归/性能基线欠账）
- [评审建议书](../Reports/2026-09-30-engineering-recommendations.md) R1 / [架构评审](../Reports/2026-09-30-architecture-and-defect-review.md) §5、§8
- [开工 DevLog 2026-10-03](../DevLog/2026-10-03-m7a-b0-kickoff-adr-baseline.md)（基线刷新实测数字）
