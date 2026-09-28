# 选帧对话框底行三钮右溢热修

2026-09-28 · 批② 动画生产线 · 用户截图报（"添加 0 帧/替换 0 帧被覆盖"）。

## 背景与根因

用户截图：从精灵表添加帧对话框底行只见「取消」「添加 0 帧」且添加右缘被窗边
裁掉、「替换为」整钮不可见；右栏「添加时写入图片 .meta（全项目生效）」文字右
侧同源裁字。

git 考古（`git log -S`）：右对齐公式与「替换为」钮同批落进 9137fcb（T3c→T3-UX4
收口，2026-09-27）——**公式从出生就按两钮算**（`bw*2 + spacing`），底行实为三钮
（取消/添加/替换为 = `bw*3 + spacing*2`），整行右溢 ~150px：替换为整钮出窗、
添加右缘贴边被裁。非回归，是出生缺陷潜伏一天被真人踩中。

同类排查全链（`SetCursorPosX` 右对齐家族）：工具栏中段三钮居中（EditorApp
:1364-1369，按 `btnW*3 + spacing*2` 算对）、右段 Layout 下拉（:1400 按单宽算
对）、Inspector 图标 slack（:473）——**无其他活口**。

## 决策

1. **右对齐按整行三钮宽让位**，锚点改精确式 `cursorX + avail.x - rowW`
   （旧式 `avail.x - w` 少补一个 WindowPadding 偏移，惯用残差 ~8px，一并消掉）。
2. **回归锁钉在"按钮矩形 vs 模态窗矩形"**（而非像素截图）：`sheetpick.replace`
   （底行最右钮）+ `sheetpick.win` 两探针同帧登记，smoke f80 断言替换钮右缘
   ≤ 窗右缘 -2px。cnt=0 按钮禁用但照常渲染登记，锁与选择数解耦；TestHooks
   每帧清空，断言必须落在开窗帧（f80 = 清空注入与计数断言之间的空档帧）。
3. 顺手（同对话框同截图）：meta 提示文字 `PushTextWrapPos(0)` 按右栏 252px
   宽换行，消右侧裁字。

## 改动

- `Editor/Panels/AnimationPanel.cpp`（DrawSheetPicker 底行/右栏）：
  右对齐公式按三钮整行宽 + 精确锚点；`sheetpick.win/replace` 两探针；meta
  提示文字换行。
- `Editor/App/EditorApp.cpp`：smoke-anim f80 越窗断言（`smokeSheetRowOk` +
  LEMON_ERROR）；RESULT `pick(...)` 扩 `sheet(... row=)` 位进 animOk。

## 验证

- **阴性验证**（ux9 同款纪律）：临时回退旧两钮公式重建 → smoke-anim
  `row=NO => FAIL` + 错误行"底行「替换为」按钮越出选帧对话框窗界"（锁真红）；
  恢复修复重建 → `sheet(all=YES clear=YES close=YES row=YES) => OK`。
- 回归 full **14/14**（首跑 12/14：smoke-drag/smoke-ui 负载抖动——T1/T3 同款
  登记先例，整套复跑全绿；anim 链含新 row 位两跑均绿）。
- ctest 3/3（engine-tests/script-tests/imgui-isolation）。

## 遗留

- 真人验收：重开选帧对话框——底行三钮完整可见（取消/添加 N 帧/替换为 N 帧
  右对齐），右栏 meta 提示两行完整不裁字。
