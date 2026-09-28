# 热修④：崩溃恢复弹窗三按钮（忽略并删除）+ 冒烟会话恢复检测门控 + 播种自清

2026-09-27 · 批② · 用户三报收敛（"忽略后下次还弹" / "player.override 已经
存在" / 冒烟整链死的"环境抖动"真身）。

## 现象与根因（三层）

1. **用户报：忽略自动备份后，下次打开编辑器还弹。** 根因：忽略只清内存
   `recoveryPath_` 不动文件；untitled 场景无盘档对照 → `DetectAutosaveRecovery`
   永远判"可恢复" → 每次启动必弹。
2. **同日 smoke-anim 在 demo 副本上整链死（此前误判纯环境抖动）。** 根因一：
   启动恢复检测只排除 `finalTest`，冒烟会话照被伏击——恢复模态每帧
   `OpenPopup` 重开抢占模态栈，把 animset.create / FilePicker / 选帧对话框
   等后续模态全憋死。根因二：冒烟集创建目标名 `player` 与用户真人验收产物
   `Assets/player.override` 撞路 → `TryCreateSet` 拒 → 模态滞留级联。
3. **夹具违规**：smoke-anim 的 pick 链假设 Assets 仅 11 张播种图（tile 序 =
   文件名序）；demo 自带 blade/bullet/dungeon 等 10+ 张 → Ctrl+A 计 21 ≠ 11。
   正确姿势 = 空目录自播种（`editor-regression.sh` 的 `${TMP}/anim` 一直如此）。

## 修复

- **弹窗三按钮**（用户定案：忽略 ≠ 删除——"忽略可能只是这次不想修改，下次
  还想继续修改"）：`恢复` / `忽略`（只关本会话弹窗，文件保留）/ `忽略并删除`
  （丢弃备份不再提示）。新增 `EditorContext::DiscardAutosave`——路径限定
  `<root>/.lemon/autosave/` 内才删（防误删任意文件原语）。
- **恢复检测门控**：`!finalTest && frames <= 0` 才检测——一切 `--frames` 有限
  的自动化会话（冒烟/终验/压测）不再被启动模态伏击；交互会话不受影响。
- **播种自清**：smoke-anim 播种块排队集创建前删已存在的
  `Assets/player.override(+.meta)` + Rescan——`player` 自此为冒烟专属产物名，
  链不再依赖工程基线（用户工程演化不再打断回归）。

## 验证

- 毒化副本（带 autosave + player.override 的 demo 拷贝）：set(create/flow)、
  sheet(all/clear/close)、multiadd、create(folder/dblclip) 链全数复活（修复前
  全死）——中毒因果实证。
- 空工程夹具 smoke-anim `=> OK`；full 回归 14/14（smoke-drag 一次窗口服务类
  抖动隔离 3/3 复跑愈，同既往记录类）；final-acceptance 的 autosave
  snapshot+recovery+save-clear 链不受 `DiscardAutosave` 影响。

## 归因收窄

既往 DevLog 记录的"注入链环境抖动"至少一部分实为本毒（恢复模态伏击 + 撞路
滞留）——带模态的链死而纯语义/视口链活，签名一致。纯窗口服务类（smoke-drag
`press=2` 少注入段）仍存在，复跑即愈。

## 遗留

- 真人验收：弹窗三按钮（demo/svr-test 里的孤儿 autosave 可用「忽略并删除」
  一次性清掉）。
