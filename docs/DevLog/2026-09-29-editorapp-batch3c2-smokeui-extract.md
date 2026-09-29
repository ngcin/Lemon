# EditorApp 批③c-2：smoke-ui 状态机外迁（A–K 段）

- 日期：2026-09-29（批③c-1 同日续）
- 性质：Run 正文机械外迁（帧号锚定/执行时序逐位不变）；③c 第二族，
  模式复用 ③c-1 定型模板（局部收敛结构体 + Frame/Verdict 两函数 + 挂点原位）。

## 改动

- EditorApp.cpp：3610 → 3177 行
  - Run 前置局部 13 行（uiTarget/uiGate…consoleOk，A–K 断言旗标族）删除；
  - 主循环内 A–K 块（401 行，含 8 行块注释）替换为原位调用 `SmokeUiFrame(frame);`
    ——**挂点紧邻 `ui_->BeginFrame` 段界（bUi0）之前**，注入先于 ImGui 帧消费的
    顺序逐位保持；
  - 裁决行（帧 154 总裁决已块内打印）替换为 `SmokeUiVerdict()` 位。
- EditorAppSmoke.cpp：778 → 1226 行
  - `UiSmokeState` 结构体（字段/初始化/注释逐项原样）+ `g_uiSmoke` 实例；
  - `EditorApp::SmokeUiFrame(uint64_t)`：A–K 十段逐行原样（dedent 4 + 31 个
    局部名 → g_uiSmoke.<field>；`launch` → `Launch()`）；
  - `SubtreeSizeOf` 随迁（C8 段唯一使用者，编辑器侧零残留）；
  - `SmokeUiVerdict()`：返回 g_uiSmoke.uiAllOk。
- EditorApp.h：+4 行（两私有方法声明）。

## 足迹勘察要点（同族后续参考）

- 词边界扫描前先甄别撞名：`smoke-uirml` 子串误中五处、editor-smoke 自检的
  同名局部 `playOk`（嵌套作用域 shadow，留在原处语义不变）、`autosaveOk`
  含 `saveOk` 子串——**族扫描一律词边界 + 逐区人工确认**。
- 本族无预循环播种段（uiTarget/uiGate 选定在块内 frame 3-4 完成），比
  预期干净；smoke-drag 族同构。

## 验证

- 构建一次过（③c-1 教训生效：外层守卫 `if (Launch().smokeUi && scenePanel_)`
  随函数体走，Run 调用点无守卫）。
- **回归 full 16/16 首跑全绿**（smoke-ui 链 = A–K 全段断言 + 帧 154 总裁决行，
  dup/del/undo 计数、rot 增量、布局/console 全在判据内）。
- 提交前程序化 review 五项全过：结构体字段 11 组 == HEAD 局部、帧函数体
  393 行逐行等价（dedent+改名+Launch 重建）、SubtreeSizeOf 迁移一致、
  挂点在位（bUi0 前邻）、旧名零残留。

## 进度与遗留

- EditorApp.cpp：7212 → 3177（累计 -56%）。
- 批③c 后续族：smoke-anim 链（热修③/T3-UX 系/smokeAnim* 局部）→
  smoke-template 采样族（g_tplSmoke 随迁）→ smoke-uirml 双模式（最大）→
  final/bench 计量族。
