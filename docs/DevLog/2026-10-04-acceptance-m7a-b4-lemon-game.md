# M7a 批④真人验收通过：lemon-game 四屏全流程 + resize（2026-10-04）

[批文件](../Plans/M7a/2026-10-04-b4-gameentry-vertical.md) · [验收勘误 DevLog](./2026-10-04-m7a-b4-acceptance-rtui-misroute.md) · [ADR-016](../ADR/ADR-016-Standalone-Runtime-And-Minimal-Packager.md)

## 事件

用户对 `./build/mac/Engine/Entry/lemon-game --project build/mac/game-fixture`
（vs-survivor 模板副本，含当日构建的 Game.dll）完成真人验收：

- **四屏全流程零 C++**：菜单 → 进局（WASD/空格移动攻击、升级三选一、死亡
  复活/结算、重开不叠曲、Esc|P 暂停）——RmlUi 文档链 + C# 流程 + 存档全通过；
- **窗口 resize**：渲染复原正常（设备整重建回调链：rmlui-backend →
  ui-subsystem → game-assets 三段式，批④新增 TextureStore::RebuildAll 路径）。

验收项目按当日勘误采用模板副本（demo/svr-test 玩法 HUD/三选一/复活对话走
RtUi 编辑器叠层，lemon-game 无呈现面——详见验收勘误条目）。

## 状态

- 批④出口判据三项全闭：真人验收 ①② 用户实测过，③ `--validate` 机器复验
  （300 帧零 VALIDATION-ERROR）；manifest 回退（git clean 模拟）与 fps ≥60
  机器面早闭。
- M7a.md 状态行 / 批次表 ④ 行 / 批文件 §4 / AGENTS.md 状态行同步勾销。
- 批④全链闭环：代码 → 回归 18/18 → review（#1 护栏当场修 + §5 登记三项）→
  提交 `6bedfe1` → 真人验收。

## 下一步

批⑤ packager 最简 + mac 干净包（`Tools/packager/`、lemon-packager 目标、
dylib 闭包、dotnet publish self-contained、`data/` 拷贝 + 音频烤制 +
manifest.pkg.json）——待用户开工指令。
