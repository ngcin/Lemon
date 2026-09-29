# EditorApp 批③a：冒烟播种族外迁 EditorAppSmoke

- 日期：2026-09-29（批②同日续；总方案见批① DevLog）
- 性质：纯机械搬移（零行为变化）；EditorApp.cpp 瘦身序列批③第一步，
  按"风险从零递增"排序——本批全是**已独立成函数**的播种代码，零 Run 控制流改动。

## 改动

- 新建 `App/EditorAppSmoke.h`（39 行）+ `EditorAppSmoke.cpp`（489 行）：
  - 成员函数五件：SeedSmokeUiRmlProject / SeedSmokeUiDocument / SeedSmokeScene /
    SeedSmokeProject / SeedJudgementScene（跨 TU 定义，EditorApp.h 零改动）。
  - 自由函数两件：WriteAnimSheetAssets / SeedBenchSurvivorScene（批①顺延件，
    原匿名命名空间 → 外部链接）。
  - **夹具常量上移头文件单源**：kAnim* 八件 + kYami* 两件——Run 内联采样链
    （40+ 处）与播种族两 TU 共用，同 RecentProjects 批②处理。
- **kSmokePngGuid 死常量删除**（批① DevLog 登记的处置项：全仓仅定义无引用，
  git 双证）。
- EditorApp.cpp：4310 → 3863 行；CMake 登记。

## 过程插曲（教训登记）

- 首次切分用了**硬编码行号**取 include 块——批②在文件顶插过一行注记把 include
  区整体 +1，stb_image_write.h / Tooling/TestHooks.h 被切出（smoke.cpp 编译报
  stbi 未声明）；且原文件尾的 `} // namespace lemon::editor` 被 seed 尾段带走
  （EditorApp.cpp 差一个收口括号）。git checkout 复原后改**内容定位**
  （按 `#include "App/EditorApp.h"` 与 `namespace lemon::editor {` 锚点取块、
  按内容判断文件尾 ns 归属）重做一次过。
- 规矩固化：跨批次脚本不得复用行号切片，一律内容锚定 + 断言。

## 验证

- 构建一次过（含上述教训轮）；成员函数守恒 9 + 5 = 14。
- **回归 full 16/16 首跑全绿**（anim-chain / template-chain 直覆盖播种族；
  --bench-survivor 不在回归表但 SeedBenchSurvivorScene 函数体逐行原样）。
