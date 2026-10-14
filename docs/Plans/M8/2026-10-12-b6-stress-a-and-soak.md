# M8 批⑥ —— 压测 A 终测 + soak 2h + 架构债 #M4/#L5 对表

Status: in-progress（2026-10-12 开工；**A/B/C/E 全收口,#R1 环境豁免结案 2026-10-14（用户拍板）**——压测 A 三跑 91/90/91 + bench 三门禁维持 + #L5 探针关账 + soak v5 硬判据全过（rc=0/600k 帧/60.4fps/墙钟 10022s/零崩溃转储）+ B2 RSS 红旗归因收口：mac +44.5MB/h = macOS SkyLight 系统行为（引擎自有内存零增长实证 + Windows 同构 61min 零斜率反证,mac 特有性坐实——[定位报告](../../DevLog/2026-10-13-r1-soak-leak-diagnosis.md)、[Windows 反证](../../DevLog/2026-10-14-r1-windows-counterproof.md)）,M8 出口判据"soak 内存曲线平"按引擎侧口径勾销；08 债表 #R1 已勾销;**仅剩敞口 = D 组 #M4 Windows 真机轮（三态对比）,待用户窗口可与批⑤ 走查/W5 合并排期**）

## 1. 输入条件（2026-10-12 探测核证）

- **基线**（批⑤ 走查支持期修复收口后，HEAD 3a57452）：回归 full 26 步全绿（fps=79）/单测 34,919/script-tests 1,911/ctest 4/4/金回放三档 mismatches=0（m8b5 档现录现放）。
- **bench 三口径待重锚**（批⑤ DevLog 尾 + AGENTS.md 基线段登记）：bench-survivor 门禁 76.5（批④ 实测 82、批⑤ 复跑 79，热态软波动）；bench-lights 软门禁 55（批④ 起编辑态 FX 默认在场，口径已变）；bench-survivor-lit 终测归本批（批④ 干跑 97fps 仅作参照）。
- **压测 A 工具就绪**（批④ D2）：`lemon-editor --bench-survivor-lit --frames N`（帧守卫 ≥600；门禁 alive≥10000 + lights≥64 + fxPass==5 + fps≥60）。
- **soak 无先例（首跑）**：lemon-game 自动化 = 每帧恰一步、不限墙钟（`GameEntry.cpp:487` paced 判据——`--frames N` 非交互即自动化）；`--scene` 直开即跑，导演/昼夜（DayNightCycle.cs 挂 Grass Global 灯）/灯池/blob/FX 随场景装配自动在场；内存曲线靠外部采样（进程内无驻留采样口，不加口——soak 是一次性验收非产品能力）。
- **金回放口径**：现录现放（09 §6.8），`Samples/bench-sim/lemon-bench-sim --record/--replay` 三档（sim-st/sim-mt + bench-script）。基准场零 Light2D 实例（M8 批① 加字段零重录先例同款前提），昼夜动画灯**不进**金档哈希流。
- **#M4**（08 M8 段债表）：批量纹理上传 Windows 真机验证，压测 A 前置（mac 已实测负收益回退，M7c b11c——**mac 侧数字与该决策解耦**：切换只影响 Windows 运行路径）。
- **#L5**（ADR-019 注记 + 08 债表）：昼夜脚本逐帧写 Light2D 字段 → 组件字段逐帧入哈希流；触发 = 回放录制耗时成点 → 组件级预哈希（哈希流布局变更 = 金档不兼容判据）。

## 2. 文件/行级变更单

### A 压测 A 终测（mac 侧）

- [x] **A1** 压测 A 终测三跑（冷机/热机/复跑）：`${EDITOR} --bench-survivor-lit --frames 900 --no-reopen`（帧数对齐 bench-survivor 回归口径 900）；裁决 = alive≥10000 + lights≥64 + fxPass==5 + **fps≥60**，三跑全过。—— **✅ 2026-10-12 三跑全 PASS**：fps=91/90/91（alive=10507·lights=64·fxPass=5/5·frameAvg≈11.0ms·frameMax≈19ms），LEMON_NO_ACTIVATE=1。
- [x] **A2** bench 三数字重锚（新门禁值按 A1/复跑实测定，原则 = 基线回归 <10% 余量同 M7a-b8 口径）：—— **✅ 2026-10-12 结论 = 三门禁全维持**：同日读数 bench-survivor **fps=92**（门禁 76.5，余量 +21%）、bench-lights **881**（软门禁 55）、bench-survivor-lit **90–91**（门禁 60）——批⑤ 尾态 74/76 判热态环境噪声成立（今日冷热三态一致 90+）；上调门禁反而放大热态 flake 面，76.5/55/60 原值维持即"基线回归 <10%"口径自洽。`tools/editor-regression.sh:263` 与 `tools/ci-daily.sh` 零改动，读数落 DevLog。
  - `tools/editor-regression.sh:263` bench-survivor 门禁阈值（现 76.5）+ 提示行文案；
  - `tools/ci-daily.sh` bench-lights 软门禁（现 55，FX 在场新口径）；
  - bench-survivor-lit 数字落账（软门禁口径：只登记不进回归，批④ 先例）。
- [ ] **A3** mac 先行说明落账（批文件 §3 D3 裁决）：压测 A 终测 mac 数字不因 #M4 结论重跑；Windows 轮作为 D 组独立补三态对比。—— **✅ 2026-10-12 并入 E1/E3 落账**：08 债表 #M4 行注明"mac 数字与该决策解耦（切换只影响 Windows 运行路径）"；D 组留 Windows 轮独立三态对比。

### B soak 2h（mac 侧，挂机后台）

- [x] **B1** 口径定形 + fps 探测：`lemon-game --project <soak副本> --scene Scenes/Grass.scene --frames N`；先 3,000 帧探测起始 fps。`--validate` 不全程开（D4）。—— **✅ 2026-10-12 三轮定型**：探测 237fps 起始 → **N=3,000,000**（实测 387fps 稳态，预期墙钟 2.5h+）。**挂机姿势两轮教训后定型**：① 第 2 轮 present 路径卡死（`sample` 取证：主线程全堵 `CAMetalLayer nextDrawable` 信号量等待——显示器睡眠/窗口全遮挡 → WindowServer 停止合成 → drawable 不返还 → present 无限等，主线程卡死连事件轮询都停 = 用户实报"画面冻结 ESC/R 无反应"；环境性伪影非引擎缺陷，取证存 `/tmp/m8-b6-soak/sample-run2-freeze.txt`）→ 第 3 轮起 **`caffeinate -is` 压住显示器睡眠**；② 无头局玩家站桩终会被围殴至死亡结算（世界暂停 = soak 退化为空转）→ **项目副本抬玩家 HP 至 1e9**（`/tmp/m8-b6-soak/svr-soak`，`cp -R demo/svr-test` 后仅改 Player.prefab Health cur/max——工作项目零触碰；合围稳态保持：弹幕杀怪 gems 落点在 pickupRadius 96 外 → 无升级弹窗打断）；第 1 轮 12 分钟（窗口被手动关闭前）数据留档 `rss-run1.csv`。soak 第 3 轮已启动（LEMON_NO_ACTIVATE=1 + RSS 每 60s → `rss.csv`，**须保持窗口至少部分可见**——全遮挡同款合成停摆风险）。**第 4–6 轮追加定性（2026-10-12 午后）**：第 4/5 轮（副本 god-HP）经栈采样证实**世界健康步进**（World::Step/Batch 主导、Gem/Mob 持续生成 = 战斗在打），但"合围稳态"画面肉眼近似静止 + 玩家被 blob 叠乘黑团遮住，两次被当作卡死手动关窗（automation 干净退出实证：`exit: frames=117742`）；用户黄调截图实为昼夜黄昏相位 = 世界活着的反证。**看门狗 v3 定型（第 6 轮起）**：`/tmp/m8-b6-soak/soak-runner.sh`——① 副本 project.lemon name 改「M8-soak挂机压测·勿关」（窗口标题可识别，title = pf.name，GameEntry.cpp:532）；② 分段自动续跑（窗口误关 rc=0 / 卡死段 >900s 击杀 rc=137 均自动重启记账，总窗 2.5h；**非零退出 = 真崩溃立即停表**）；③ 活性判定改用日志生成计数器（Prefab 实例化行增长），不依赖肉眼；RSS 曲线含分段边界标记，跨段连续。**第 7 轮 v4 失败归因 + v5 定型（2026-10-12 16:37 起跑、16:47 中断 250k 帧，无退出记录）**：① runner 随工具会话进程组被收（macOS 无 setsid → nohup 双层 + zsh `&!` disown，脚本/游戏双脱离）；② **真根因 = `--vsync` 缺失**：`paced = frames==0`（GameEntry.cpp:492）→ `--frames N` 自动化走 **Immediate 不限速**（实测 422fps），四轮栈采样同族 = macOS 合成器对 IMMEDIATE 不限速灌帧节流 → `nextDrawable` 长阻塞、画面冻结且事件停轮询；`--vsync` 旗标落地（`(paced || vsync) ? Fifo : Immediate`，61.4fps 实测 = 60Hz 真实节奏 = D2 判据 432k 帧 @60Hz 同口径）——本变更随 C1 探针同批提交（GameEntry.cpp）。**v5 runner 定型**：`soak-runner-v5.sh` = nohup 脱离会话 + `--vsync --frames 600000`（600k/61.4fps ≈ 2.7h 墙钟，双判据 ≥7200s 且 ≥432k 帧）+ god-HP 副本 + `caffeinate -is` + LEMON_NO_ACTIVATE=1 + RSS 60s 采样 + 自然退出记 rc（无自动重启/无卡死击杀——soak 要干净 exit 0）；窗口须保持至少部分可见（第 2 轮全遮挡教训）。**2026-10-12 17:04 v5 已挂机启动**（ETA ≈19:45）。
- [x] **B2** 内存曲线采样：外部 shell 每 60s `ps -o rss=` 采样 + 墙钟时间戳落 CSV（`/tmp/m8-b6-soak/`）；判据 = 稳态后无单调上涨趋势（首 30min 爬坡属装载/池预热正常态）+ 墙钟 ≥7200s + 干净退出码 0。—— **✅ 2026-10-13 判读收口：不平 = 真泄漏（缺陷单 08 债表 #R1）**。v5 稳态段（剔前 30min 预热）181→282MB **+44.5MB/h 完美线性 R²=1.0000**；归因对照跑 A（二进制钉 7453595 同源重编 + 摘 Director 零刷怪，60min/216k 帧/60.4fps/rc=0）判别窗 t∈[20,60]min 斜率 **0.742MB/min（44.49MB/h）R²=1.0000，与 v5 比值 1.00** ⇒ ~215B/帧确定性泄漏在引擎/C# 每帧固定路径，B2 场景豁免不成立。曲线与回归参数落 DevLog §4/§5。
- [x] **B3** 崩溃转储检查：soak 收尾查 `~/Library/Logs/DiagnosticReports/` 零新增 lemon-game 报告（08 出口判据"崩溃转储登记"mac 侧口径；Windows 侧归 D 组）。—— **✅ 2026-10-12 v5 收表核查**：最新 lemon 条目停在 10-11（lemon-editor/tests），当日零新增。
- [x] **B4** soak 结论落 DevLog（曲线图/采样表 + 判读）。—— **✅ 2026-10-13**：v5 结果表 + 对照跑归因判读 + 回归参数全落 [DevLog](../../DevLog/2026-10-12-m8-b6-stress-a-and-soak.md) §4/§5；09 §6.4 soak 小节结果同步。

### C #L5 检查（soak 前收口）

- [x] **C1** 探针：`tests/engine/LightTests.cpp` 加计时打印探针（**只打印不断言**——计时不设 CI 门，手跑记数）：两组各 240 帧——(a) 真实规模 10 灯逐帧写 intensity/color（DayNightCycle 同款双字段路径）、(b) 上限余量 10,000 灯同款；每组打印 ComputeStateHash 每帧均摊 µs，对照组零灯。—— **✅ 2026-10-12**：`TestLightStateHashCostProbe` 落地（DayNightCycle 同款 intensity+color 逐帧双字段写，sink 异或防优化；单测 34,919→**34,922**，+3 = 探针三组各一 Expect）。
- [x] **C2** 判定：含光增量 <500µs/帧（60Hz 帧预算 16.7ms 的 3%）→ 不成点，#L5 关闭登记；超阈值 → 组件级预哈希立项（带金档不兼容迁移判据）。结论落 08 债表 + DevLog。—— **✅ 2026-10-12 判定 = 不成点，#L5 关账**：ctl-0=0.5µs/帧、**real-10=1.6µs/帧**（真实规模，增量 1.1µs ≈ 阈值 0.2%）、stress-10000=1119µs/帧（纯线性 ≈0.11µs/灯，压测 A 真实上限 64 灯 ≈7µs）——四数量级余量，组件级预哈希不立项；昼夜动画灯不进金档基准场（零 Light2D 实例，批① 先例），金回放零影响。

### D #M4 Windows 真机轮（待用户窗口，可与批⑤ 走查合并排期）

- [ ] **D1** win-ci 构建 + 压测 A 一轮（`--bench-survivor-lit`；W1–W7 清单见 M7a b7 DevLog §4）。
- [ ] **D2** 批量纹理上传三态对比（维持回退 / 单发 / 强制批量），正收益切批量、负收益维持登记（08 债表勾销）。
- [ ] **D3** 窗口充裕则顺跑：批③ Windows 面（lemon-game 开灯场景 60fps）+ W5 真机 GPU 清单项。

### E 文档落账

- [x] **E1** `docs/EngineDesign/09-Testing.md`：压测 A 终测数字（§7 bench 表）+ soak 口径与结果（§6.8 后新小节或 §7）+ #L5/#M4 结论。—— **✅ 2026-10-13**：压测 A 表由批⑦ 落账（68606d3）；soak 口径 + v5/对照判读结果落 §6.4（含 #R1 缺陷指针）；#L5 关账/#M4 解耦注记在 08 债表。
- [x] **E2** `AGENTS.md` 回归基线段重锚 + 本页 ⑥ 行勾销 + DevLog 条目（`2026-10-12-m8-b6-stress-a-and-soak.md`）。—— **✅ 2026-10-13**：基线段 fps 门禁数字维持（A2 三门禁不动的结论随 68606d3 重锚落）；本页 ⑥ 行与 AGENTS 状态段随判读收口更新；DevLog §5 结果补齐（含二进制钉源补记）。
- [x] **E3** `docs/EngineDesign/08-Development-Roadmap.md`：M8 行出口判据对表（压测 A 全绿/soak 平/零崩溃）+ M8 段债表 #M4/#L5 行勾销。—— **✅ 终表 2026-10-14**：压测 A 全绿 ✅ / 零崩溃（转储登记）✅ / **soak 平 ✅ 按引擎侧口径（#R1 环境豁免结案：引擎自有内存零增长实证 + Windows 同构 61min 零斜率反证，mac 侧涨幅 = macOS SkyLight 系统行为；08 债表 #R1 已勾销）**；#L5 关账 ✅、#M4 留 D 组 Windows 轮。

## 3. 设计裁决（默认执行，用户可否决）

- **D1 soak 场景 = svr-test Grass 直开**：唯一全链场景（昼夜动画灯 + 灯池 + blob + 导演刷怪 + FX），Night 是静态夜战信息量低；模板面归批⑤ 走查不重复。用户游戏工作项目即 soak 对象 = "引擎卡点"口径最真实。
- **D2 soak 判据 = 墙钟 ≥7200s + 曲线平 + 零崩溃 + exit 0**：帧数当量下限 432k（2h@60Hz）；自动化不限速意味着同墙钟内帧数远超实时（更强的泄漏证据），墙钟与帧数双判据各取其义。
- **D3 压测 A mac 先行，不等 #M4**：批量上传 mac 已实测负收益回退，Windows 结论只影响 Windows 路径；串行等待会把整批吊在人肉窗口上。
- **D4 `--validate` 不全程开**：验证层 CPU 开销扭曲 fps/内存口径；批②③④ 的 smoke-light/smoke-fx 已带 `--validate` 短段覆盖 API 纪律，soak 开头 240 帧 validate 干净即足。

## 4. 排程（预估 1–2 日，soak 挂机并行）

- 日1 上午：A1–A2 压测终测 + 重锚；C1–C2 L5 探针；B1 fps 探测定 N。
- 日1 午后：B soak 启动挂机（后台，不占人力）；期间 D 窗口预约交用户。
- 日2：B2–B4 判读落账 + E 文件收口；D 窗口出现即插（可与批⑤ 走查同窗）。
- 超期预案：D 组（Windows）可整组后挪不阻塞批⑥ mac 侧收口——批文件勾销时 D 组留敞口登记，M8.md ⑥ 行注明。

## 5. 完工记录

（A 压测三跑 PASS + 门禁维持 / C #L5 探针关账 / B soak v5 硬判据全过——数字落 DevLog `2026-10-12-m8-b6-stress-a-and-soak.md` §1–§5。B2 RSS 红旗全案：10-13 定位（引擎内存面全干净,增长 = macOS SkyLight 64KB VM 块,场景级二分矩阵）→ 10-14 Windows 同构反证（61min 零斜率）→ **#R1 环境豁免结案（用户拍板 2026-10-14）**,M8 出口判据按引擎侧口径勾销。敞口：D 组 #M4 Windows 真机轮待用户窗口。）
