# 修复：资源浏览器等比缩略图垂直居中（追修——顶对齐"只占顶部一点"）

- 日期:2026-10-13（用户实测复报，同日追修；6fe4aa7 落地后的观感缺陷）
- 性质:用户报障追修，非批次项。引擎内核零改动（AssetBrowserPanel.cpp 单文件）。

## 1. 实报（用户原话口径）

dungeon_boss_1.png / bar_fg.png 等没有占满格子，只占了顶部一点，需要修复。
图片选择窗口里面是占满了各自（格子）。

## 2. 根因

前批等比缩略（6fe4aa7）只做了**水平**居中：按钮钉在方格顶部，竖向空隙全靠
下方 Dummy 垫——宽扁条图（实测 bar_fg.png 64×10、dungeon_boss_1.png 256×48，
等比后 72×11~13 横条）呈现"贴顶一条缝 + 下方大片空白"。FilePicker 瓦片的
图区是**双向**居中（`c = (area - dim) * 0.5f`），故用户以它为正确参照。

## 3. 修法（DrawItem）

- 按钮双向居中：`imgH < kThumb` 时 `SetCursorPosY(cellY + (kThumb-imgH)/2)`
  （与既有水平 `SetCursorPosX` 对称；帧 padding 双向对称抵消，偏移精确）。
- 标签 Y 钉固定单元格相对位（替代 Dummy 垫高）：`labelY = cellY + kThumb +
  2×FramePadding + ItemSpacing.y`（= 满格方图单元格的标签位），前向钳
  `max(cur, labelY)` 防回退。Dummy 方案的缺陷：夹在按钮与标签之间会多出一个
  `ItemSpacing.y` 行距，混合行（条图 + 方图同排）行高漂移 ~8px；钉 Y 后行高
  逐位 == 方格单元格。

## 4. 验证

- Release 零警告。
- 视觉自证：svr-test 真实素材拷 /tmp 夹具（cwd 隔离 ini，不扰用户会话/仓库
  布局），`--screenshot` 1600×900 + 视觉判读——bar_fg/bar_bg/dungeon_boss_1/
  blade 等条图竖向居中（上下留白对称，不再贴顶）；标签 Y 恒定行齐；无拉伸、
  无重叠。
- 回归三步绿（基础 smoke / smoke-anim `pick(click/shift/all=YES)` / smoke-ui
  `dir=1/1`）。
