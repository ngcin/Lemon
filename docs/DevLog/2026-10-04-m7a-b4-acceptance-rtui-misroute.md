# M7a 批④验收日勘误：demo/svr-test 误指 + RtUi 边界放大（2026-10-04）

[批文件](../Plans/M7a/2026-10-04-b4-gameentry-vertical.md) · [ADR-014](../ADR/ADR-014-Game-UI-RmlUi-Integration.md) · [ADR-016](../ADR/ADR-016-Standalone-Runtime-And-Minimal-Packager.md)

## 事件

真人验收首报三症状：对战 HUD（hp/exp/飞剑数）不显示、升级无三选一、死亡无复活
选项——问"场景设计还是 bug"。排查结论：**批④登记的设计边界，非渲染 bug**——
`demo/svr-test` 的对战 HUD（`Ui.Set("hp"/"xp"/"time"/"kills"/"ally")`）、升级三选一、
首死复活对话（`Ui.ShowDialog`/`Ui.CardPick`）全押 **RtUi 通道**＝编辑器 GameView
的 ImGui 叠层（批文件 §0/§5 既有登记："lemon-game 无呈现面"）。lemon-game 里 demo
仅菜单/暂停/设置/结算四屏可用，**首死无复活对话会卡死流程**。

连带失误认领：批④出口判据与完工指引把真人验收项目写成 `demo/svr-test`——误指。
**正确验收项目 = vs-survivor 模板副本**（PlayerHud/三选一/死亡对话已全迁 RmlUi
`hud.rml`/`cards.rml`，③d-1/② 口径；模板无预编译 `.lemon/bin`，验收前需
`cp -R Templates/vs-survivor <副本>` + `dotnet build Game.csproj -o <副本>/.lemon/bin`）。

## 机器面补强（本次实际改动）

- `GameEntry.cpp` smoke：RESULT 增 `cards=` 观察位（`IsDocumentShown("Assets/UI/
  cards.rml")` 锁存——升级三选一/死亡对话同文档）**并入 ok 判据**——demo 暴露的
  回归缺口：原断言只到 hudShown（场景声明文档），未覆盖 `UI.Show` 运行时动态弹卡
  链。模板夹具四跑 900 帧 `cards=1` 且 alive/visible/batches 逐位一致（自动化档
  确定性步进），判据无抖动风险。
- 批文件四处勘误（引言验收项目、件 13 字段、§4 验收行、§5 登记项放大后果）。

## 验证

lemon-game 重建 + 模板夹具 game-smoke 四跑全 `hud=1 cards=1 => OK`；full 回归
18/18（game-smoke 步随强化判据复验）。

## 登记

- demo 的 RtUi→RmlUi 迁移归 ADR-014「M8 前定去留」线（RtUiCards 降级为兼容层/
  数据绑定糖），不在 M7a 内动；
- smoke `cards` 断言限定模板语义（demo 的卡走 RtUi，不在此链）——已在批文件 §5 记明。
