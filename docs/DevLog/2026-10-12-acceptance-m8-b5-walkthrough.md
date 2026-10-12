# M8 批⑤ 真人走查通过：模板夜战 + svr-test 昼夜 + 蒙版聚光灯 + FX 组合观感（2026-10-12）

[批⑤ 批文件](../Plans/M8/2026-10-11-b5-night-template-daynight.md) · [批⑤ DevLog](./2026-10-11-m8-b5-night-template-daynight.md)

## 事件

用户完成批⑤ 四项真人走查，**通过、终态零缺陷**（构建口径 = HEAD 3a57452，走查支持期修复收口档；当日在跑工作树仅批⑥ 文档 + 单测探针，不触游戏/编辑器面）。走查口径 = 批文件 §D 四项（m7c-b9 先例格式）：

1. **模板工程夜战全流程**：MainMenu →「夜战」→ Night 战斗全链（灯池观感 / 火把×bloom 合成 / 聚光锥形 / blob 随移动 / 怪群暗部可读）→ 死亡结算 → R 重开 → Esc 回菜单——通过；
2. **svr-test Grass 昼夜**：晨→昼→暮→夜过渡连续、夜段 ambient 压暗后 HUD/飘字可读——通过；
3. **蒙版聚光灯**：light.scene 加 SpriteMask 型 Light2D + mask sprite 观感正常，Inspector 光照编辑工作流（批② 追记的启用复选框同路径）顺带验证——通过；
4. **FX 观感**：bloom 阈值/强度与色调对比/暖偏无需现场调参（零调参反馈）——通过。

走查**过程期**实报缺陷已全部随支持期修复收敛并随本走查复验（[追记](./2026-10-11-m8-b5-night-template-daynight.md)：blob offsetY 贴脚、Ground 地图消费、引擎排序 orderTier、非战斗实体 team 2、TargetBoard 战斗资格过滤——commit 3a57452，金回放重录 m8b5）。

## 意义

批⑤ 出口判据全闭（机器面 2026-10-11 + 真人面本日）→ **批⑤ done**；ADR-019 光照组件模型（灯池/蒙版/blob/昼夜/FX 后处理）首次组合真人验收通过。M8 段剩：批⑥（mac 侧机器线进行中：压测 A 终测三跑 PASS + #L5 不成点关账 + soak 2h 挂机；D 组 = #M4 Windows 真机轮仍待用户窗口）→ 批⑦ 文档站收官。待用户清单剩：① 血条正式素材观感复验（移交不设期） / ③ W5 真机 GPU（不设期） / ④ M4.8 其余 UX 优化清单细化（不设期）——批⑥ #M4 Windows 轮窗口可与之合并排期。
