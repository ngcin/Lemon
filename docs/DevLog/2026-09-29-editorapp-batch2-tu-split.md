# EditorApp 批②：同类多 TU 机械拆分（Chrome / Actions / Scripts）

- 日期：2026-09-29（批①同日续）
- 性质：纯机械拆分（零行为变化）；EditorApp.cpp 瘦身序列批②，
  前情与总方案见 [批①](./2026-09-29-editorapp-batch1-vstemplate-extract.md)
- 动机：批①后 EditorApp.cpp 5911 行，UI 骨架/编辑动作/脚本管线三块职责
  与 Run() 无共享状态，C++ 成员函数可跨 TU 定义——拆文件不改 EditorApp.h。

## 改动

- `App/EditorAppChrome.cpp`（880 行）：SetupDefaultLayout / BuildMenuBar /
  BuildToolbar / BuildLayoutDropdown / Save·LoadLayoutIni / ListSavedLayouts /
  BuildStatusBar / BuildNoProjectCard / BuildShortcuts / BuildUI /
  BuildPickersAndModals（原 407–1229 行，12 函数）。
- `App/EditorAppActions.cpp`（368 行）：场景 IO 六件（New/Open/Recent/Save/
  SaveAs/ConfirmUnsaved）+ 资产与动画入口（Import/OpenAnimation*·Create*/
  ClosePanel/AssetBrowserDir/RescanAssets）+ 剪贴板与拖入（Copy/Paste/
  ImportDroppedFile）+ PickerStartDir（原 1231–1542 行，19 函数）。
- `App/EditorAppScripts.cpp`（487 行）：Game 编译与热重载（QueueScriptRebuild/
  LogCompileErrors/FindGameProject/OpenProjectPipeline/InitScriptHostFrom/
  ScriptSourceChanged/TryHotReloadScripts）+ Play 管线（PlayBlockedByScripts/
  TryEnterPlay/StopPlay）+ UIDocument 装载对账（MountSceneUiDocuments/
  ReconcileUiDocuments）+ DrawRecoveryModal/HotReloadCount（原 1544–1972 行，
  16 函数；UIDocument 两件批④再归 GameUiBridge）。
- `App/RecentProjects.{h,cpp}`（19+53 行，**批②唯一非纯机械件**）：最近项目
  四函数自匿名命名空间外迁——`SaveRecentProjects`（BuildMenuBar→Chrome）/
  `PushRecentProject`（OpenProjectPipeline→Scripts）/`LoadRecentProjects`
  （Run 留守）三 TU 共用，内部链接保不住；函数体逐行原样，仅改外部链接。
- EditorApp.cpp：5911 → 4309 行（-1602）；剩 = 匿名 ns 辅助（冒烟像素计数/
  事件 sink/钩子/smoke 夹具常量）+ Run + UI 桥尾段 + Seed 系。
- CMake 登记四文件；新 TU 的 include 集为 EditorApp.cpp 超集拷贝（保证编译的
  机械保底，按 TU 精简留给后续卫生批）。

## 验证

- 构建**一次过**（依赖图预判完整：三区段经 grep 预扫无文件级静态/匿名 ns
  引用——`s_gameUiForHooks`/`g_app`/`SubtreeSizeOf`/`CountPixels*`/`kAnim*`
  全部只有 Run 侧使用点，留守正确）。
- 成员函数守恒：Chrome 12 + Actions 19 + Scripts 16 + 留守 14（ctor/dtor/
  Run/UI 桥 6 件/Seed 5 件）= 61，与拆分前列表一一对应。
- **回归 full 16/16 首跑全绿**（同批①口径；本批无 CLI 产物可对拍，
  行为零变化由回归与链接完备性背书）。

## 遗留与登记

- 批③ SmokeHarness：`g_tpl*` 全局、`final*` 计量、Run 内联冒烟状态机、
  Seed 系 + `kAnim*` 常量块 + `WriteAnimSheetAssets` + `SeedBenchSurvivorScene`
  （批①顺延件）+ `kSmokePngGuid` 死常量处置，一并归 harness；Run 只留
  播种/每帧/末帧裁决三挂点。
- 批④ GameUiBridge（LoadUiDocument/FeedGameUiInput/Resolve*/LoadProjectFonts/
  MountSceneUiDocuments/ReconcileUiDocuments 自 Scripts TU 再迁）+
  ScriptReloadPipeline（可选）。
- 新 TU include 超集精简（低优先卫生项）。
