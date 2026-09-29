# EditorApp 批③c-1：smoke-drag 状态机外迁（Run 正文迁移第一模式族）

- 日期：2026-09-29（批③a/③b 同日续；前情见批① DevLog 总方案）
- 性质：Run 正文机械外迁（帧号锚定/执行时序逐位不变）；批③c 逐模式族推进的
  第一刀，选 smoke-drag 起步因为自包含度最高（25 个专有局部 + 单一连续块
  + 独立裁决）。

## 改动

- EditorApp.cpp：3871 → 3609 行
  - Run 前置局部 25 个（dragTarget…focusDone，五段状态机）删除；
  - 主循环内 `if (launch.smokeDrag && scenePanel_) {…}` 整块（227 行）替换为
    原位调用 `SmokeDragFrame(frame);`；
  - 末帧裁决块替换为 `if (launch.smokeDrag && !SmokeDragVerdict()) exitCode = 1;`。
- EditorAppSmoke.cpp：489 → 778 行
  - `DragSmokeState` 结构体（字段/初始化/行内注释逐项原样）+ 匿名 ns 实例
    `g_dragSmoke`；
  - `EditorApp::SmokeDragFrame(uint64_t)`：五段注入状态机逐行原样（dedent 4 +
    局部名 → g_dragSmoke.<field> 改名；`launch` → `Launch()` 等值访问器）；
  - `EditorApp::SmokeDragVerdict()`：分段计数 + 断言行打印，返回 pass。
- EditorApp.h：+5 行（两个私有方法声明 + 注释——批③起按总方案允许动头文件）。

## 设计要点（后续模式族复用的模式）

- **挂点原位**：`SmokeDragFrame(frame)` 调用点 = 原块所在循环位置，帧号锚定
  链零漂移；
- 两个 `std::function` 闭包（worldToPt/arcWorldToPt）随状态进结构体——它们
  只捕 `this` 成员 + 值捕局部，跨帧存储于 Run 局部原本就成立，进 struct 同构；
- `launch` → `Launch()`：launchCopy_ 在 Run 头一次性赋值后不变，访问器返回
  同一对象，等值替换。

## 过程插曲

- 裁决函数首版没剥掉 `if (launch.smokeDrag)` 外层包装（launch/exitCode 是
  Run 词法域）——编译器实抓后手修（后续族的外层 if 一律由 Run 调用点保留）。

## 验证

- 构建链接过（既有 ent unused-parameter 告警随行号漂移，非新增）。
- **回归 full 16/16 首跑全绿**（smoke-drag 链直接覆盖：press/armed/updates
  计数 + 五段断言 + `=> OK` 判据行）。

## 遗留（批③c 后续族）

- smoke-ui（A–K 段 ~430 行）→ smoke-anim 链 → smoke-template 采样族（g_tplSmoke
  随迁）→ smoke-uirml 双模式（最大）→ final/bench 计量；全部走本批模式
  （DragSmokeState 先例：局部收敛结构体 + Frame/Verdict 两函数 + 挂点原位）。
