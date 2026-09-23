# 2026-09-23 M5 模拟人工验收轮：svr-test 一整局 + 万级实体压测（Perf10k.scene）

应用户要求对 `demo/svr-test` 做模拟人工测试（M5 验收表口径）+ 新建 scene 压测上万
物体。**测试经由一个临时注入通道完成**（把 `--smoke-template` 的风筝走位/选卡/
事件计数按环境变量 `LEMON_SIM` 门控泛化到任意 `--play` 会话——与现网注入同源；
数据采完后**已按用户指示整体还原**，`EditorApp.cpp` 恢复原样并重建原版二进制，
以下保留结果与问题记录。后续要做同款自动化需重新评审落地方式）。

## 一局（Main.scene，38400 帧 ≈ 局内 640s 口径）

- **链路全通**：16 波表前 6 波正常推进（WaveStart 事件/波次横幅）；击杀 286、
  全实体死亡 874、升级 4 次、三选一 4 张全被选（固定序：移速→磁力→射速→穿透，
  截图可见 PierceBullet 在场）；宝石掉落/磁吸正常（Prefab 实例化日志流）。
- **死亡结算数学精确**：t=182s 第 6 波阵亡，`286×10+182 = 3042` 分，
  HUD"★ 新纪录 3042 分！按 R 复活"；`Save.Flush` 落盘 `.lemon/saves/game.sav`
  （hexdump 核对 `vs.best="3042"`）——判据"死亡结算/最高分存档"通过。
- **存档回显链**：开局 HUD best 行首现"最高纪录 0"（空档载入）；`--smoke-template`
  同日复跑全 OK（`saveLoad=YES`：预置 123 → EnterPlay 载入 → HUD 回显；
  hud/wave/cards(seen/pick/hidden)/saveFile/second-project ids 全 OK）——
  载入→回显机制机械证明在案。
- **问题 1（待真人复验）：R 复活未触发**。死亡 60 帧后注入单帧 bit5（Confirm）
  一次，over 行持续到局末、HP 恒 0——复活未发生。script-tests 有 Confirm 位探针
  （批④判据），疑点在"单帧点按"与"真人按住"的差异或冻结期（Time.Scale=0）采样
  时序；**真人开一局死一次按住 R 即可定论**。
- **问题 2（验收观察点）：自动化口径未到 Boss 波**。风筝走位（绕原点 r≈200 匀速
  圆）在第 6 波（t≈182s）承压阵亡，波表 16 波/t=565s Boss 未达。真人走位变向+
  三选一取舍更优，10 分钟口径仍归真人验收；自动化只能证"前中段链路健康"。
- **备注**：程序化 EnterPlay 不切 GameView 前台（`tabFocusPending_` 仅交互路径
  置位）——HUD 不入自动化截图，真人路径无此问题；自动化启动若上次会话 autosave
  较新会挂"崩溃恢复"模态无人应答（不阻 sim，观感问题）。

## 万级实体压测（新建 `demo/svr-test/Scenes/Perf10k.scene`）

场景 = 玩家（无脚本、HP 1e6，防脚本侧冻结干扰计量）+ 4 个 WaveDirector 并行刷怪
（每导 4 条目 × interval 0.004 = 240/s/导，聚合 960/s；capAlive 闸门；4 导程
range 320–560 环）。**功能验证全过**：载入 5 实体、Play 往返逐字节一致、
alive 精准顶格 12005（=12000 闸门+玩家+4 导）、视口可见 12001、零 ImGui 错误。

计时（本机、同进程口径；Fifo 但帧时远超 vsync 上限即真实负载；官方
`--bench-survivor` 同机对照 PASS：alive 10435 / frameAvg 16.44ms / sim 13.98ms）：

| 场景 | alive | 帧均 | fps | vs 22.2ms 预算 |
|---|---|---|---|---|
| Perf10k（capAlive 12000） | 12005 | 70.9ms | ~14 | 超预算 3.2× |
| Perf10kCapped（10000） | 10004 | 51.1ms | ~19.6 | 超预算 2.3× |
| 同上但 **Mob.prefab 去掉 Hazard** | 10006 | **17.2ms** | ~58 | **达标** |
| 官方 bench-survivor（怪无 Hazard） | 10435 | 16.44ms | 61 | PASS |

- **归因（对照实验，内容层变量）**：帧时的 ~66%（51.1→17.2ms）来自
  **Mob.prefab 自带的 Hazard 组件**（dps 8/radius 24/tick 0.8）——万怪"聚堆成球"
  （全部 Chase 向原点静止玩家）拓扑下，每怪近邻查询 = O(N×局部密度) 放大器。
  bench 怪**不带** Hazard，这正是同数量级下 3× 差距的全部来源。
- **发现（记 M6 优化，未改任何引擎代码）**：HazardSystem 在高密度场是主 sim 成本。
  方向：按 `tickInterval` 错峰分帧（0.8s tick 本就允许每怪 1/48s 粒度）、查询
  距离平方早退、或按格聚合一次查询多怪共享。模板场景"怪海围玩家"是该系统真实
  工作形状，**M5 判据的 22.2ms 预算对"带 Hazard 的模板怪 10k"不成立**——
  09 §6.10 台账若加"模板怪海"基线行应注明口径差。
- 实体数边际成本 ~21µs/实体/帧（10004→12005 线性）；本次全部为内容层实验
  （副本 `/tmp/lemon-svr-run` 内改 prefab/scene），**真实工程只新增
  `Scenes/Perf10k.scene`，引擎零改动**。
- 附：`--scene` 直开无选中实体 → smoke overlay 像素断言（sel/handle≥20）FAIL 属
  冒烟前置不满足，非缺陷；不影响 playAlive/往返/错误数等其余断言。
