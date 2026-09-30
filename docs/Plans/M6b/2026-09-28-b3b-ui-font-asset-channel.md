# M6a 批③b —— UI 字体与资产通道：Noto 入库 + .rml/.rcss 资产化 + 贴图桥 + 文档热重载

Status: done（2026-09-28 代码面勾销；真人验收已全过 2026-09-30——截图目检判据 4 + 双击装载/热重载手感随 ③d 前置①与 ③d-1 换肤演示覆盖，[DevLog](../../DevLog/2026-09-30-acceptance-b3ab-screenshot-ime.md)）

> [ADR-014](../../ADR/ADR-014-Game-UI-RmlUi-Integration.md) 五子批第二件。③a 交付了呈现地基（RmlUiBackend over RHI +
> UiSubsystem + gameRT 冒烟）；本批把 UI 从"内存文档 + 系统字体 + 无图"升级为
> **资产驱动的通道**：字体随引擎带、文档/样式是正式资产（GUID/.meta/manifest）、
> `<img>`/RCSS 图能吃项目精灵、文件改动热重载。C# API（③c）与模板迁移（③d）不在本批。

## 现状盘点（本批动工面，2026-09-28 摸底）

| 事实 | 出处 | 对本批的含义 |
|---|---|---|
| UiSubsystem 文档 = `LoadDocumentFromMemory`（内存底稿），字体 = 系统链 Hiragino→STHeiti→Songti | `Engine/Ui/UiSubsystem.cpp:44-52,148` | 文档通道加 file 底稿形态；字体通道加显式 LoadFontFace；系统链降级为 Noto 缺失时的兜底 |
| RmlUi 相对路径两路均经 `SystemInterface::JoinPath`：`<link href>`（XMLNodeHandlerHead.cpp:17）与贴图 src（RenderManager.cpp:80） | CPM 缓存 rmlui db14 | **单点覆写 JoinPath = POSIX 绝对路径直通 + 相对按文档目录拼接**；上游默认剥前导 '/' 按 cwd 解（绝对路径装载必坏） |
| `Rml::LoadFontFace(path, fallback)`；无 UnloadFontFace（仅 ReleaseFontResources） | rmlui Core.h:108 | 项目字体只增不卸（族名共存，切项目无害）；引擎 Noto 一次装载 |
| RmlUiBackend `LoadTexture` 返回 0 + 一次性告警（③a 遗留） | `RmlUiBackend.cpp:544-553` | 贴图桥落点：resolver 钩子 → 图集页纹理复用（外部纹理变体，只持描述符集不持 image） |
| `Device::GetVulkanTextureViewInterop(Texture)` 返回纹理自带 view（缓存，无泄漏） | `RHI.cpp:1370-1373` | 外部纹理零新建 view，直接写描述符集 |
| 图集页 tex/dims 存于 AtlasRegistry 但无私有访问器 | `Atlas.h:69-75` | 新增 `AtlasTexture(atlasIndex, w, h)` 只读查询 |
| 设备丢失回调序 = 注册序：viewport → rmlui-backend → ui-subsystem → asset-gpu（后者在 OpenProjectPipeline 才注册） | `EditorApp.cpp:2864` vs `:2585` | ui-subsystem 现回调**立即**重载文档 → 此时图集页未重建（asset-gpu 排后）→ 解析到死纹理。修 = 重载**延迟到下一帧 Update**（全部回调完成后） |
| 资产热重载链：watcher 置脏 → `RescanAssets()` 消费 ChangeSet（sprite 增/改 → ImportSprite；删 → Evict） | `EditorApp.cpp:3490-3491, 2299-2314` | UI 文档/样式消费同点接入：.rml 变 → 单文档重载；.rcss 变 → 全量重载（RmlUi 无单表刷新口） |
| AssetType 序列化按名字符串（"枚举插位自由"）；TypeOf 按扩展名 | `AssetDatabase.cpp:22-33,162-174` | 追加 `Rml`/`Rcss` 两型零迁移成本 |
| 冒烟夹具纪律：空目录自播种（smoke-template 先 remove_all 幂等）；stbi_write_png 可用 | `EditorApp.cpp:5691+`, `:3545` | smoke-uirml 夹具升级为 temp 项目（project.lemon + Assets/UI/ 三件套），走标准 OpenProjectPipeline |
| Noto Sans SC Regular（SubsetOTF，8.3MB，OFL）已入 `Engine/Ui/Fonts/`，族名 = `Noto Sans SC` | 本批 2026-09-28 下载（代理） | 引擎正字；LEMON_TEMPLATE_DIR 同款编译期路径宏 `LEMON_ENGINE_FONT_DIR` |

## 任务分解

### T1 字体通道（引擎正字 + 项目字体）

- `Engine/Ui/Fonts/`：`NotoSansSC-Regular.otf` + `OFL.txt`（已入库；THIRD_PARTY 登记 T7）。
- `UiSubsystem`：新 API `bool LoadFontFace(const char* absPath, const char* family, bool fallback)`
  （family 供 LoadedFontFamily 报告/冒烟断言——RmlUi 按文件内名匹配，调用方传正确名）；
  成功装载 fallback 字体时 fontFamily 更新为该名（引擎正字优先于系统链）。
- `EditorApp::Run`（gameUi_ Init 成功后）：`LoadFontFace(LEMON_ENGINE_FONT_DIR "/NotoSansSC-Regular.otf", "Noto Sans SC", true)`——
  fallback=true 使 RCSS 未命中的族名也有字可渲（③a 发现 RCSS 无逗号回退列表，Noto 单族兜底）。
  装载失败红字不阻断（系统链仍在）。
- `EditorApp::LoadProjectFonts()`（OpenProjectPipeline 内，thumbcache::Init 后）：扫 DB 条目
  扩展名 otf/ttf/ttc → `LoadFontFace(abs, 文件名, /*fallback=*/true)`；逐条日志（族名以文件为准）。
- CMake：`Editor/CMakeLists.txt` 加 `LEMON_ENGINE_FONT_DIR="${CMAKE_SOURCE_DIR}/Engine/Ui/Fonts"`
  （LEMON_TEMPLATE_DIR 同款 87-88 行旁）。

### T2 路径解析（JoinPath 覆写）

- `LemonSystemInterface::JoinPath` 覆写（UiSubsystem.cpp）：source 绝对（'/' 开头）→ 直通；
  否则 dir(document_path) + '/' + source 经 `lexically_normal`（吃 ".."；document_path 空 → 原样）。
  引擎以绝对路径装载文档后，相对 href/src 全部按文档目录解析。

### T3 文档资产通道 + 热重载

- `UiSubsystem`：
  - `bool LoadDocumentFromFile(const char* name, const char* absPath)`——`ctx->LoadDocument(absPath)`
    （文档 source URL = 路径 → 相对解析的锚点）；Doc 底稿扩为 file 形态（sourcePath），
    同名覆盖旧文档、shown 保持。
  - `bool ReloadDocument(name)` / `void ReloadAllDocuments()`——file 底稿重读盘、内存底稿用
    原文本；shown 态保持；设备丢失重载统一走此路。
  - `bool UnloadDocument(name)` / `void UnloadAllDocuments()`（资产删除/切项目用）。
- 设备丢失顺序修正：ui-subsystem 回调只置 `reloadDocsNextUpdate`，`Update()` 首查执行——
  保证 asset-gpu（注册序在后）已重建图集页，贴图桥解析到活纹理。
- `EditorApp::RescanAssets()` ChangeSet 消费扩段：Rcss 增/改 → `ReloadAllDocuments()`；
  Rml 增/改 → 若已按该 relPath 装载 → `ReloadDocument(relPath)`；Rml 删 → `UnloadDocument`；
  Rcss 删 → ReloadAll。文档名约定 = 资产 relPath（"Assets/UI/main.rml"）。
- AssetBrowser 双击 .rml → `EditorApp::LoadUiDocument(guid)`：`LoadDocumentFromFile(relPath, abs)`
  + `ShowDocument(relPath, true)` + 日志（非 Play 提示"进 Play 后 GameView 显示"）；.rcss 双击无操作
  （tooltip 说明经 `<link>` 消费）。

### T4 贴图桥（`<img>`/RCSS 图 → 项目精灵图集）

- `AtlasRegistry::AtlasTexture(uint32_t atlasIndex, uint32_t& w, uint32_t& h) const`
  （Atlas.h/Atlas.cpp；线性查页，UI 图量小无所谓）。
- `RmlUiBackend`：
  - 公开 `using UiTextureResolver = std::function<bool(const std::string& source, rhi::Texture& tex, uint32_t& w, uint32_t& h)>`
    + `SetTextureResolver(fn)`（引擎句柄形态，头文件零 Vulkan/RmlUi）。
  - `TextureRes` 扩 `bool external`：外部纹理 = 只持描述符集 + 借用 view
    （`GetVulkanTextureViewInterop`）；Release/Destroy 只 free set（image 归图集）；
    设备丢失 RecreateAfterLoss 对 external 只清 set（不重传、不重建 image），待文档重载再解析；
    RenderGeometry 的 textured 判定已含 `t->set` 非空 → 间隙帧降级为无纹理绘制（可接受瞬态）。
  - `LoadTexture`：resolver 命中 → 建 external TextureRes + dims 出参 → 返回句柄；
    未命中保留 ③a 一次性告警语义（文案改为"项目内无此精灵资产"）。
- `UiSubsystem::SetTextureResolver(fn)` 门面转发 backend。
- `EditorApp`（OpenProjectPipeline 内 resolver 安装 + 切项目重装）：
  `ResolveUiTexture(source, tex, w, h)`：绝对路径 → `fs::relative(项目根)`（越出根 = false）→
  `FindByPath` → Sprite 且 `IsValidSprite(spriteId)` → `GetSprite().atlasIndex` →
  `AtlasTexture(...)`。切片子图由 RmlUi 原生 `<img rect="x y w h">` 表达（引擎零机制）。

### T5 .rml/.rcss 资产类型 + AssetBrowser 识别

- `AssetType` 追加 `Rml, Rcss`（Generic 前）；`TypeOf`：`.rml`/`.rcss`；`AssetTypeName`："rml"/"rcss"。
- `IconKind` 追加 `AssetRml, AssetRcss`（程序化图标：Rml = 文档框 + `< >` 笔画；Rcss = 文档框 + 三横线）；
  AssetBrowserPanel：IconFor 映射、类型过滤 combo 追加两项、行 tint（Rml 品蓝 / Rcss 紫灰）、
  tooltip（"双击：装载到游戏 UI" / "文档 <link> 引用；热重载生效"）。
- manifest/.meta 全链零改动（类型名字符串序列化天然兼容）。

### T6 冒烟升级（--smoke-uirml 扩为资产链验收）

- 夹具（`SeedSmokeUiRmlProject`，temp_directory_path/lemon-uirml-<pid>，先 remove_all 幂等）：
  `project.lemon` + `Assets/UI/uirml.rml`（面板/标题/正文 + `<img src="tex.png">`，`<link>` 引 rcss）
  + `Assets/UI/uirml.rcss`（font-family: Noto Sans SC；body 色 #e0e0e0）+ `Assets/UI/tex.png`
  （96×96 纯 #d04080 四角亮标，stbi_write_png）→ `launchCopy_.projectDir` → 标准 OpenProjectPipeline
  → `SeedSmokeUiDocument` 改为 `LoadDocumentFromFile("Assets/UI/uirml.rml", abs) + Show`。
- 热重载两段（帧循环挂钩，同 asset-smoke 3532 先例）：frame 100 改 .rml（文档级
  `<style>` 标题色 #ffd060→#40ff90）+ 直接 `RescanAssets()`；frame 140 改 .rcss
  （正文色 #e0e0e0→#80c0ff）+ Rescan。
- frames 启动参数门禁 ≥ 240（实现期发现：主循环判 launch.frames 原值，"缺省"写
  launchCopy_ 无效）；injectionSession 门（2621）补 `smokeUirml`（temp 项目不进最近列表）。
- VERDICT 扩：`doc font panel titleG tex hot => OK/FAIL`——doc=HasDocument、font=="Noto Sans SC"、
  panel>3000、titleG(绿)>20、titleTop 位置断言沿用、tex(#d04080)>500、
  hot = titleG>20 && bodyB(#80c0ff)>20 && 旧色残留(金/灰)各 <5。
- ③a 旧断言口径（金标题/灰正文）换为新终态口径；`--screenshot` 路径不变（真人验收留用户）。

## 验收判据（全过才勾销）

1. ✅ `cmake --preset mac && cmake --build --preset mac` 全绿。
2. ✅ `./build/mac/Editor/lemon-editor --smoke-uirml --frames 240 --validate`：VERDICT OK
   （doc=1 font=Noto Sans SC panel=111648 titleG=994 bodyB=725 tex=9216 old=0/0
   titleTop=981/994）+ **验证层零错误** + exit 0。
3. ✅ 既有回归 `tools/editor-regression.sh` full：**14/14 PASS（首跑全绿）**。
4. ✅ `--smoke-uirml --screenshot` 目检（追记 2026-09-28：用户报首版截图只见 Scene——
   ③a 独立进 Play 漏设翻页标志 + ImGui 1.92 `FocusWindow` 的 dock tab 选择段被上游
   注释（#2304）双重根因，已修（`NextSelectedTabId` 显式翻页）+ 自动另出
   `<名>-gamert.png`（gameRT 原像素）；交换链截图已见 GameView+UI（机器目检过），
   **真人复验 ✅ 2026-09-30**——整屏 GameView 有 UI + gamert 原像素面板/中文字形正常）。

## 实现期发现（偏离批文件预设计的落账）

- **RmlUi Factory 按路径缓存 `<link>` 样式表**：文档全重载不自动失效——批文件预设计的
  ".rcss 变更 → ReloadAllDocuments" 实测无效（文档级 `<style>` 与贴图不缓存，两者正常）。
  修 = 全重载前置 `Factory::ClearStyleSheetCache()`（公开 API）；.rcss 专属路径改走
  `ElementDocument::ReloadStyleSheet()`（内部自清缓存 + **保 DOM/元素状态**，比预设计
  的全量重载更轻，且对齐 M2 "DocumentReloaded → C# 重灌"语义）。`UiSubsystem` 相应
  提供 `ReloadStyleSheets()` 而非复用 ReloadAllDocuments。
- **③a 遗留双重 delete 实锤**：延迟释放环与 liveTex 的登记交叠（ReleaseTexture 不摘
  liveTex，摘除在 3 帧后的 DrainDeferred）——`Rml::Shutdown` 释放的贴图恰在环内时
  DestroyAll 与 Shutdown 各 delete 一次 = malloc abort（崩溃报告 2026-09-28-204243）。
  ③a 无贴图文档从未触发；③b `<img>` 首爆。DestroyAll/RecreateAfterLoss 统一"先摘除
  再销毁"（后者同族悬垂一并修）。
- **像素断言的邻近色陷阱**：bodyB 初值 4476 实为面板边框 #60a0ff（与正文 #80c0ff
  逐通道差 32 < tol 44）——容差盒必须核对画面内邻近色，收紧 tol 24 后正文本体 725。
- **主循环退出判据读 launch.frames 原值**：③a "缺省 180" 只写 launchCopy_ = 无效设计
  （无 --frames 实际无限跑 + 零捕获）。批文件 T6 的"缺省 240"改为启动参数门禁
  （--frames ≥ 240，smoke-template 先例）。
- 夹具贴图去四角白标：白色 (255,255,255) 落进旧灰断言 (224±30)³ 逐通道带内（角标
  本意是覆盖度目检，实测成为假阳性源）——纯色 + 精确计数断言足够。

## 风险与既知边界

- **外部纹理间隙帧**：设备丢失 → backend 清 set → 下一帧 Update 才重载文档，间隙帧该图降级
  无纹理绘制（颜色几何仍在）——macOS 设备丢失本身罕见，接受。
- **字体不卸载**：RmlUi 无 UnloadFontFace；切项目后旧族名仍可命中（无害共存）。
- **LoadDocument 走 RmlUi 默认 FileInterface**（fopen 绝对路径）；自定义打包 IO 归 M8 打包线。
- **rcss 热重载 = 全文档重载**：文档量小（<10）成本可忽略；量大时 ③c 再议增量。
- **图鉴贴图背景的完整形态**（M6 GUID 直引/帧序）归 ③c/波2；本批 = 路径引用桥。
- Noto 单 Regular（无 Bold）：RCSS font-weight: bold 回退 Regular 渲染；Bold 入库随用户需求。

## 关联

- [ADR-014](../../ADR/ADR-014-Game-UI-RmlUi-Integration.md)（D1 ③b 定义 / D2 M2 DocumentReloaded / M6 资产源）
- 前批：[③a 渲染地基](./2026-09-28-b3a-rmlui-renderer.md)（含实现期发现：RCSS 无逗号回退列表等）
- 后续：③c C# API（T7 文本输入微 spike 前置）→ ③d 模板迁移 → ③e 图鉴
