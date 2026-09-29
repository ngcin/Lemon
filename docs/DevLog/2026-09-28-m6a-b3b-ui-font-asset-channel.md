# M6a 批③b：UI 字体与资产通道（Noto 正字 + .rml/.rcss 资产化 + 贴图桥 + 文档热重载）

- 日期：2026-09-28
- 批文件：[Plans/M6b/2026-09-28-b3b-ui-font-asset-channel.md](../Plans/M6b/2026-09-28-b3b-ui-font-asset-channel.md)；决策：[ADR-014](../ADR/ADR-014-Game-UI-RmlUi-Integration.md)（③b 定义 / M2 DocumentReloaded / M6 资产源）
- 性质：代码面完成（验收 1/2/3 ✅ + gameRT 画面机器目检通过；真人截图走查待用户）

## 交付

| 件 | 内容 |
|---|---|
| 引擎正字 | `Engine/Ui/Fonts/NotoSansSC-Regular.otf`（SubsetOTF 8.3MB）+ `OFL.txt` 随仓库；`LEMON_ENGINE_FONT_DIR` 编译期路径宏（LEMON_TEMPLATE_DIR 同款）；THIRD_PARTY 登记 + 06 §2.2 类型表 rml/rcss 行 |
| `UiSubsystem` | `LoadFontFace(path, family, fallback)` + `LoadDocumentFromFile`（Rml::LoadDocument，source URL = 路径 = 相对解析锚）+ `ReloadDocument`（全重载，前置 `Factory::ClearStyleSheetCache()`）+ `ReloadStyleSheets`（逐文档 `ReloadStyleSheet`：保 DOM/元素状态 + 清缓存——③c 数据重灌的轻量底座）+ `UnloadDocument/UnloadAllDocuments` + `SetTextureResolver` 门面；SystemInterface 覆写 `JoinPath`（POSIX 绝对直通 + 相对按文档目录 `lexically_normal` 拼接——**`<link href>` 与贴图 src 两路均经此口**，单点覆盖）；设备丢失文档重载**延迟到翌帧 Update**（asset-gpu 回调注册在后，立即重载会解析到未重建的死纹理） |
| `RmlUiBackend` | 贴图桥：`SetTextureResolver(UiTextureResolver)` + `LoadTexture` 先问解析器 → **外部纹理变体**（借用 `GetVulkanTextureViewInterop` 的 RHI 缓存 view + 只持描述符集；释放/设备丢失只还 set，image 归图集）；`AllocSetForView` 抽出（自有/外部共用）；**双重 delete 修复**（见发现 ②） |
| `AtlasRegistry` | `AtlasTexture(atlasIndex, &w, &h)` 只读页查询（贴图桥数据源） |
| 资产库 | `AssetType::Rml/Rcss`（.meta/manifest 按名字符串序列化，零迁移）+ TypeOf 扩展；AssetBrowser：两枚程序化图标（AssetRml 窗口页/AssetRcss 阶梯行）+ 类型过滤两位 + 紫罗兰/品红 tint + tooltip + **双击 .rml → LoadUiDocument**（装载到游戏 UI，Play 中 GameView 即显——③c 前的手动通道） |
| 热重载 | `RescanAssets` ChangeSet 消费：.rml 增/改 → 单文档全重载；.rcss 增/改 → `ReloadStyleSheets`；.rml 删 → UnloadDocument；切项目 → UnloadAllDocuments + resolver 重装 + 项目字体（Assets/ 下 otf/ttf/ttc 全量 fallback 注册） |
| 贴图桥解析器 | EditorApp `ResolveUiTexture`：JoinPath 后路径 → `fs::relative(项目根)`（越出 = false）→ FindByPath → Sprite → `IsValidSprite` → 图集页；切片子图由 RmlUi 原生 `<img rect>` 表达（引擎零机制） |
| 冒烟 | `--smoke-uirml` 夹具升级：temp 项目（project.lemon + Assets/UI/ 三件套）→ 标准 OpenProjectPipeline → 文档从资产装载；帧 100 改 .rml（文档级 style 标题金→绿）/ 帧 140 改 .rcss（正文灰→蓝）+ 直接 Rescan；VERDICT 扩 font/panel/titleG/bodyB/tex/old/titleTop |

## 实测数字

- 构建 `cmake --build --preset mac` 全绿（两条存量警告非本批引入）。
- `--smoke-uirml --frames 240 --validate`：**doc=1 font=Noto Sans SC panel=111648 titleG=994 bodyB=725 tex=9216 old=0/0 titleTop=981/994 => OK**，验证层零错误，exit 0。
- 回归 `tools/editor-regression.sh` full：**14/14 PASS（首跑全绿）**。
- gameRT 画面（`LEMON_UIRML_DUMP=1` 落盘）：面板/绿标题/蓝正文/品红贴图块全正常，中文渲染无乱码。

## 实现期发现（已折入批文件"实现期发现"）

1. **RmlUi Factory 按路径缓存 `<link>` 样式表解析结果**——文档全重载不自动失效：改 .rcss 后重载文档，正文仍旧灰（文档级 `<style>` 与贴图不缓存，故两者正常）。修 = 全重载前置 `Factory::ClearStyleSheetCache()`（公开 API，Factory.h:133）；.rcss 专属路径用 `ElementDocument::ReloadStyleSheet()`（内部自清缓存 + **保 DOM/元素状态**，对齐 M2 "DocumentReloaded → C# 重灌"语义）。**教训**：`bodyB` 断言初值 4476 其实匹配的是面板边框 #60a0ff（与正文 #80c0ff 逐通道差 32 < tol 44）——像素断言的颜色窗口要核邻近色；收紧 tol 24 后文字本体 725。
2. **③a 遗留双重 delete 隐患实锤**：`ReleaseTexture` 进 3 帧延迟环但不摘 liveTex（摘除在 DrainDeferred）——`Rml::Shutdown` 释放的贴图恰在环内时，`DestroyAll` 按环 delete 一次、`Shutdown` 再按 liveTex delete 一次 = malloc double-free abort（崩溃报告 2026-09-28-204243，SIGABRT 栈直指 RmlUiBackend::Shutdown）。③a 冒烟文档无贴图、环恒空所以从未触发；③b `<img>` 贴图首次触发。修 = DestroyAll/RecreateAfterLoss 的环处理先从 liveTex 摘除再销毁（RecreateAfterLoss 同族悬垂一并修）。
3. **主循环退出判据读 `launch.frames`（原值）而 ③a 的"缺省 180"只写 `launchCopy_`**——不带 --frames 实际无限跑 + 末帧捕获永不触发（pixel 全零的假象）。修 = 参数门禁（--frames ≥ 240，smoke-template 先例）。
4. **RmlUi 默认 `JoinPath` 剥前导 '/'**（"根 = 数据根"约定）——绝对路径装载下 `<link>`/`<img>` 相对引用全按 cwd 解析。覆写后 `<link href>`（XMLNodeHandlerHead）与贴图（RenderManager）两路统一走我们的 POSIX 语义。
5. 设备丢失回调序 = 注册序，asset-gpu（图集重建）在 ui-subsystem 之后注册——ui 侧文档重载必须延迟翌帧 Update，否则贴图桥借到死 view（编译期无感知、运行期才炸的顺序债）。

## 追记（同日热修：验收截图只见 Scene 不见 UI）

- **用户走查报 `uirml-b3b-screenshot.png` 是 Scene 视图、UI 不可见**。实况链：截屏帧确在
  Play 中（gameRT 像素断言全过、UI 真的画进 gameRT），但编辑器交换链截图里中央区
  标签页停在 Scene——GameView 在后面的隐藏 tab。
- 三层根因：①③a 的 smoke-uirml 独立进 Play 分支漏设 `tabFocusPending_`（Play 按钮/
  菜单路径都设——进 Play 自动翻 Game 页，Unity 心智）；②补上后仍不翻——消费时序：
  标志在主循环前设置，首帧 BuildUI 里默认布局**同帧更晚**才建，`SetWindowFocus` 对
  未建窗口落空且标志已清零（真人按 Play 在窗口早已存在的帧 N，从未暴露）——修 =
  目标窗口/tab 栏未建时标志不清零，留待下帧；③最深处：**ImGui 1.92 `FocusWindow`
  的 dock tab 选择段被上游注释**（imgui.cpp:13930-13935，"For #2304 we avoid applying
  focus immediately before the tabbar is visible"）——无头会话 `SetWindowFocus` 只改
  nav 不翻标签页，真人点击翻页走的是交互路径。修 = 消费块补回被注释逻辑的等价操作
  （`w->DockNode->TabBar->NextSelectedTabId = w->TabId`，imgui_internal 已在
  EditorApp 引用面内）。
- 配套：`--screenshot` 在 smoke-uirml 下自动另出 `<名>-gamert.png`（gameRT 原像素，
  与断言同源、无编辑器铬——验收正主画面）；交换链截图现也能看到 GameView + UI
  （机器目检：Game 标签选中，深蓝面板/绿标题/品红贴图块全可见）。
- 复跑：smoke-uirml OK（验证层零错）+ 回归 full **14/14**（翻页机制改动无副作用）。

## 遗余

- 贴图桥当前仅路径源（图鉴卡片贴图背景通路已就绪）；GUID 直引/世界 RT/帧序源归 ③c/波2（M6 矩阵）。
- 打包 runtime 字体分发形态（OFL 许可证随字体再分发）归 M8 打包线。
- Noto 仅 Regular（无 Bold）——RCSS font-weight: bold 回退 Regular 渲染；Bold 入库随用户需求。
- smoke-uirml 入回归脚本第 15 步仍随 ③c 契约断言一起接（③a 批文件已登记的节奏不变）。
- 真人验收：编辑器会话双击 .rml 装载 + Play 中显示 + 改样式热重载手感（截图 `.lemon/uirml-b3b-screenshot.png`）。
