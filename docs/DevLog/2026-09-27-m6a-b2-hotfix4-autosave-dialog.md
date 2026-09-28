# 2026-09-27 · M6a 批② 热修④：崩溃恢复弹窗三按钮 + 冒烟恢复检测门控 + 播种自清

## 事件

用户三报收敛（[批文件](../Plans/M6a/2026-09-27-b2-hotfix4-autosave-dialog-smoke-gate.md)）：
①"忽略自动备份后下次打开还弹"；②"player.override 已经存在"；③当日 smoke-anim
整链死（曾误判纯环境抖动）。

要点：

- **忽略永弹根因**：只清内存 `recoveryPath_` 不动文件，untitled 无盘档对照
  → 永判可恢复。用户定案三按钮：恢复 / 忽略（本会话，文件保留——"下次还想
  继续修改"）/ 忽略并删除（`DiscardAutosave`，路径限定 autosave 目录内）。
- **冒烟被恢复模态伏击**：检测只排除 finalTest，`--frames` 有限会话照弹；模态
  每帧 OpenPopup 重开抢栈，后续模态链全憋死。门控改 `!finalTest && frames<=0`。
- **播种自清**：排队集创建前删 `Assets/player.override(+.meta)` + Rescan
  （用户真人验收产物撞路 → TryCreateSet 拒 → 模态滞留级联）；`player` 自此
  为冒烟专属产物名。
- **中毒实证**：毒化副本修复前 set/sheet/multiadd/create 全死 → 两修后全活；
  空工程 `=> OK`；full 14/14。既往"注入链环境抖动"观察项归因收窄：带模态
  链死的一部分实为本毒；纯窗口服务类（smoke-drag 少注入段）仍在、复跑愈。
