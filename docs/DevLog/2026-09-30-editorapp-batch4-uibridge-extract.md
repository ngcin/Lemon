# EditorApp 批④：游戏 UI 桥七函数外迁（g_tplSmoke 单 TU 化）

- 日期：2026-09-30（批③c-6 同日续）
- 性质：批② 拆出的 Scripts.cpp 尾部两函数 + EditorApp.cpp 尾部五函数按主题
  收敛为独立 UI 桥 TU；连带把 g_tplSmoke 的 TU 外引用清零（结构体自共享头
  退回定义 TU 匿名命名空间）——Run 里最后一段内联 smoke-template 逻辑
  （渲染段 capReq 块）随之挂点化。

## 改动

- 新 TU `Editor/App/EditorAppUiBridge.cpp`（270 行，CMake +1 源）：
  - 自 EditorApp.cpp 尾部五函数（逐行原样）：`LoadUiDocument`（双击 .rml 手动
    装载通道）、`FeedGameUiInput`（Play 段鼠标/键盘/IME 喂入）、
    `ResolveUiTexture`（贴图桥：路径/GUID → 图集页）、`ResolveUiDocument`
    （C# UI.Show 文档解析器）、`LoadProjectFonts`（项目字体 fallback 注册）；
  - 自 EditorAppScripts.cpp 两函数（逐行原样）：`MountSceneUiDocuments`
    （通道 A：EnterPlay 扫 UIDocument 声明装载 + 归位）、`ReconcileUiDocuments`
    （状态对账：relPath 非健康资产逐出——僵尸渲染防线）；
  - **唯一等值变换**：FeedGameUiInput 的 `g_tplSmoke.pointerHold` 直读改
    `SmokeTplPointerHold()` 访问器（程序化复核确认全函数仅此一行差异）。
- g_tplSmoke 单 TU 化（EditorAppSmokeTpl.cpp）：
  - `TplSmokeState` 结构体 + 实例自 EditorAppSmoke.h 退回本 TU 匿名命名空间
    （定义逐字节一致；无捕获 event sink lambda 计数改写与文件级存储刚需不变）；
  - 新增 `SmokeTplCapture(rhi::CommandList&)`：Run 渲染段 capReq 块挂点化
    （守卫 `launch.` → `Launch()` 既定等值变换，体逐行一致；层序三拍时序
    逐位不变——证据块置位 → 次帧录 gameRT → 同帧尾取回）；
  - 新增 `SmokeTplPointerHold() const`：FeedGameUiInput 外迁后的指针保持窗
    读点，返回字段原值；
  - EditorAppSmoke.h -35 行（结构体/extern 退役，只余跨 TU 共享符号），
    SmokeTpl.cpp 的 EditorAppSmoke.h include 随之成死包含删除。
- EditorApp.h：+5 行（两声明 + 注释；`rhi::CommandList` 前置声明已有）。
- EditorApp.cpp：1237 → 1075 行；EditorAppScripts.cpp：487 → 426 行。
- 附带处置（零行为，登记）：
  - EditorApp.cpp 尾部孤儿注释（批③a 迁 SeedSmokeUiRmlProject 时遗留的三行
    夹具说明）迁回 EditorAppSmoke.cpp 函数上方 + 一行迁回注记；
  - EditorApp.cpp 文件尾 5 行多余空行清为单个换行。

## 验证

- 构建：首过（唯一 warning 为 EditorAppSmoke.cpp L1804 未改动旧代码的
  unused-parameter，与本批无关）。
- 回归：`tools/editor-regression.sh full` 16/16 PASS（跑前 pgrep 确认无残留
  lemon-editor 实例）。template 链走 SmokeTplCapture/SmokeTplPointerHold 新
  挂点、uirml 链走 UI 桥全部七函数——两链均一次过。
- 等价复核（程序化，HEAD 7237da5 vs 工作树）：
  - 七函数逐行一致（FeedGameUiInput 仅 pointerHold 一行差异，方向核对）；
  - SmokeTplCapture 体 = capReq 块 dedent4 + Launch() 后逐行一致；挂点前锚
    （lastFrame 块闭 `}`）/后锚（`bool needRe`）与 HEAD 相同、间隔一空行；
  - TplSmokeState 定义逐字节一致；EH 净增 5 行零删、CMake 净增 1 行、
    Smoke.cpp 净增 4 行（孤儿注释迁回）；行数账四文件全对
    （EA -162 / ES -61 / SH -35 / ST +57）。
- 本批零缺陷：前两批各出过一次 return 语句边沿形态的等值变换漏洞，本批
  无 return 变换（七函数原样搬移；capReq 块无 return）。
