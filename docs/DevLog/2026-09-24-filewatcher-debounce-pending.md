# 2026-09-24 · F-15：FileWatcher 防抖吞事件——DebounceGate 窗内转 pending

来源：[全栈审查](../Reports/2026-09-24-code-review-546a755.md) F-15（Medium）。
旧实现（`EditorApp` 主循环）：`scriptWatcher_.ConsumeDirty()` 即清脏标志，防抖窗
（0.4s）未到时直接跳过——**窗口内发生的第二次脚本保存被消费但不排队重编译**，
无 pending 状态，改动丢失（下次保存才可能再触发）。

**修复**：防抖决策抽成 `DebounceGate`（`FileWatcher.h`，10 行，editor-core 可单测）——
窗口锚定在触发点；窗内取到的脏事件转 `pending_`，窗过后照常触发；连续变更合并为
一次；窗外首脏同帧即触发（与原"立即编译"节奏一致）。`EditorApp` 主循环换用：
`ConsumeDirty()==true → OnDirty(now)`，每帧 `Due(now)` 决定排队重编译。

**验收**：
- engine-tests **13218 checks OK**（+7：首脏立即触发/不重复触发/窗内变更等待/
  窗过后触发（原吞点）/连续脏合并恰一次）；
- ctest 3/3；editor-regression full **13/13**（script-chain 与 final 验收的
  热重载链路全绿）。

端到端 GUI 时序冒烟（500ms 轮询 × 0.4s 窗口的两次精确落盘）因抖动不可靠未做：
吞事件缺陷的决策点就在门逻辑本身，单测已钉板；接线为 4 行，由既有热重载冒烟
覆盖回归。
