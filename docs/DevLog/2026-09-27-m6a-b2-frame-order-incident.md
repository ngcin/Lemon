# 2026-09-27 · M6a 批②：帧序事故（多选加帧反序）+ 上一帧图标镜像

## 事件

用户实测报两问题：①动画面板预览条「上一帧/下一帧」图标同形；②svr-test
Monster03 的 dying.anim 经「从图片文件（多选）」全选添加后顺序错乱（000.png
不在第一帧）。当日修复（[批文件](../Plans/M6a/2026-09-27-b2-frame-order-and-icon-fix.md)）。

## 诊断（数据先行）

- **guid → 文件名映射还原** dying.anim：帧序 = Dying_014..Dying_000 **精确
  反序**；walk/attack/idle = 反序带局部交换（17 时旧构建多选确认按点击序
  累积 + 同一根因叠加）；Monster01/02 同名 clip 全部正常（不同会话/通道）。
- **根因 = `InsertFrameAfter` 契约谎言**：注释称"idx=-1 = 末尾追加"，实现是
  `insert(begin() + (-1) + 1)` = **头插**。多图逐张"追加"每次头插 = 整表反序。
  中招调用点四处：多图加帧（HandlePickerResult 流 2）/ 拖图入带尾 / 拖图入
  预览 / 空帧未选中路径。
- **图标**：`##animprev`/`##animnext` 出生起同用 `IconKind::Step` 且无镜像
  （9137fcb 考古确认，非回归）。

## 同类问题排查（用户问"其他地方是否也有"）

全链复核无其他活口：FilePicker `Refresh` 目录枚举后排序 ✓；`OrderedMultiSel`
按显示序归一（双击确认同走 `ConfirmMulti`）✓；AssetDatabase `Rescan` 两段式
路径排序（M5 批④曾修）✓；`DuplicateSelectedFrames` 尾起插+索引补偿 ✓；胶片带
拖拽换序 src<dst 回退一格 ✓；「在之前插入副本」i=0 传 -1 = 头插**恰好正确**
（保留，契约注释写明）。walk/attack/idle 的局部交换 = T3-UX4 之前旧构建的
点击序累积历史产物，现行确认路径已归一，无活 bug。

## 修复

- `InsertFrameAfter` 契约注释重写（-1 = 头插；末尾追加传 `(int)size`——勿再
  改回）；四处"末尾追加"调用点改传 `(int)edit_.frames.size()`。
- `ui::IconButton` 加 `flipX` 参数（UV 交换零成本）；`##animprev` 传 true。
- **回归锁**：FilePicker「打开」登记 `picker.open`；面板加 `EditFrameCount/
  SheetForTest` 访问器；smoke-anim f90–f104 链（重开多图通道 → Ctrl+A 11 图 →
  真实点「打开」→ 逐位断言追加序 = Assets 文件名升序 guid 序）；RESULT 增
  `multiadd(count/order)` 位。**阴性验证**：临时回退 -1 → `order=NO` + FAIL
  （回归确实抓得住）。
- **用户数据修复**：svr-test Monster03 四 clip 帧表按源图文件名重排（guid→
  文件名映射排序，schema/其余字段不动），修后四档全部 OK。

## 回归

- smoke-anim 全绿（含新 multiadd 位）；full 14/14。首跑 anim-chain 抖动一次
  （set create/pick 注入链全灭 = 窗口服务类环境抖动，同 T3-UX5 记录的不可见
  Space 问题），隔离复跑 3/3 + 整脚本重跑 14/14 确认非产品问题。

## 过程教训

阴性验证后用 `git checkout` 还原单行改动，**把整个未提交工作树还原了**
（T3-UX6 + 本轮 AnimationPanel.cpp 改动全灭，逐条重做恢复）。教训：工作树
有未提交改动时严禁 checkout 收尾临时改动——应用 sed 反向替换或 stash。
