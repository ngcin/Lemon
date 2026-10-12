# 修复：编辑器 UX 四项——资源浏览器名称居中/缩略图拉伸 + 选择器图标视图双击失效

- 日期:2026-10-13（用户实测报告当天；M8 批⑥ 压测/soak 并行期插入修复）
- 性质:用户报障修复，非批次项。引擎内核零改动（纯 Editor 树两个文件）。

## 1. 实报（用户原话口径）

1. 资源中的文件名或者文件夹名称没有居中；
2. 序列帧图显示在资源目录下的文件图标图片会被拉升；
3. 选择图片或者其他文件选择窗口中缩略图视图无法双击进入到子文件夹，列表可以；
4. 无法双击快速选择文件，必须要点击打开按钮才能。

## 2. 根因

1. **名称左对齐**：`AssetBrowserPanel` 网格单元格的文件名/文件夹名是
   `%.12s` 截断 + `PushTextWrapPos` 左对齐换行（且按字节截断，CJK 可能切半字符）。
2. **缩略图拉伸**：`DrawItem` 对 sprite 页纹理按 `(kThumb, kThumb)` 正方形
   `ImageButton` 采样（uv 0..1 全幅）——导入页宽高比被无视，长条序列帧图
   （如 128×32）纵向拉成正方形。
3. **图标视图双击失效（③④同根因）**：`FilePicker::DrawIconEntries` 在
   `InvisibleButton` **返回值**（释放帧为真）分支里查
   `ImGui::IsMouseDoubleClicked`——而该 API（= `MouseClickedCount==2`）只在
   **按下帧**为真 → 永远错过，双击退化为两次单击（进不了子文件夹、选不了
   文件）。列表视图正常是因为 `Selectable+AllowDoubleClick` 走
   `PressedOnDoubleClick`（按下即触发，vendored imgui v1.92.9b-docking
   `imgui_widgets.cpp:7491` 实证）。

## 3. 修法

- **`AssetBrowserPanel.cpp`**：
  - 新增 `DrawCenteredLabel(text, avail)`（匿名命名空间）：单行省略号截断
    （退字符连带退 UTF-8 连续字节 0x80–0xBF 防切半）+ 水平居中；文件夹单元格
    与 `DrawItem` 文件名统一走它（宽 = 单元格宽，列距公式前提保持；完整名走
    悬停 tooltip 不变）。
  - `DrawItem` sprite 等比缩略：`PageInfo` 取导入页宽高 → fit 进 `kThumb`
    见方区，按钮（帧宽 = imgW+2×FramePadding）在单元格内水平居中
    （`SetCursorPosX`，组内行首锁在组起点所以标签仍归位单元格左缘），
    竖向欠高用 `Dummy(0, kThumb-imgH)` 垫满方格 → 行高恒定网格不参差。
    类型图标（形状页）无宽高比问题，保持满格方图不动。
- **`FilePicker.cpp`**：`DrawIconEntries` 点击处理重写为按下帧口径——
  `dbl = IsItemHovered() && IsMouseDoubleClicked(0)`、`pressed = IsItemClicked()`
  （= `IsMouseClicked && IsItemHovered`，imgui.cpp:6667 实证）；双击导航/确认后
  `return`（`NavigateTo` 内 `Refresh()` 重建 `entries_`，`e` 引用失效不可再触），
  单击选择与双击同拍（第一次按下选、第二次按下导航/确认）。smoke 注入点击
  （按下/释放分帧）与语义直调（`ApplyMultiClickForTest`）均兼容。
- **评审轮修（commit 前自查）**：三处双击早退 `return` 各补 `ImGui::PopID()`
  ——循环体 `PushID((int)i)` 与尾部 `PopID` 配对，早退绕过尾部即留下未配对
  Push；vendored imgui v1.92.9b `End() → ErrorRecoveryTryToRecoverWindowState`
  （imgui.cpp:11938）按栈快照校验，未配对 = `IM_ASSERT_USER_ERROR("Missing
  PopID()")` + 静默代弹。Release 断言静音故 smoke 单击链未暴露；mac-debug
  与未来断言面必炸。补齐后按位配对，双击路径三出口全对称。

## 4. 验证

- 构建：Release 零警告（改动 = Editor/Panels/AssetBrowserPanel.cpp +
  Editor/Tooling/FilePicker.cpp 两文件）。
- 回归（与本批改动相关四步全绿）：
  - `--smoke --frames 120`：editor-smoke PASS；
  - `--project … --smoke --frames 240`（资产链：导入/热替换/缩略图）：PASS；
  - `--project … --smoke-anim --frames 132`：`pick(click=YES shift=YES all=YES)`
    ——图标视图按下口径选择 + 注入点击链复验；
  - `--project … --smoke-ui --frames 160`：`dir=1/1`——居中后文件夹标签点击
    导航仍命中。
- 视觉自证（`--screenshot` 1600×900 + 视觉判读）：标签居中（anim-set /
  非常长… 等）；128×32 长条图 letterbox（宽扁居中、上下见底色，未拉伸）；
  两个超长 CJK 名省略号截断且居中；行齐无重叠。临时调试钩子（env 聚焦 +
  ini 浮动窗改法）验后已移除/还原，干净构建复跑 smoke 双绿收口。
- 评审轮修后复验：构建零警告 + 基础 smoke / smoke-anim（`pick(click=YES
  shift=YES all=YES)`）/ smoke-ui（`dir=1/1`）三步全绿。

## 5. 备注

- FilePicker 图标视图瓦片名居中省略号是既有实现（T3-UX4），本批未动；
  其 `pop_back` 截断未退 UTF-8 连续字节（CJK 可能切半显示），与
  `DrawCenteredLabel` 不一致——已知项，候选池待用户定夺是否统一。
- 双击语义对照：资源浏览器网格双击行为（建实体/开面板等）本就挂
  `hovered && IsMouseDoubleClicked`（AssetBrowserPanel.cpp:446），按下帧口径，
  无同类问题。
