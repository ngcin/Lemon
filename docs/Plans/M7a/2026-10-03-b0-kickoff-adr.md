# M7a 批⓪：设计定形——ADR-016 定稿 + 基线刷新 + entryScene 落地

Status: **done**（2026-10-03 一日收口：ADR-016 采纳（D2–D8 拍板均按建议）+ 开工基线刷新 + entryScene 字段落地 + 回归锁 + 出口复验 full 17/17）

> [M7a.md](./M7a.md) §4 批⓪ 分解的落名批文件。出口判据：ADR-016 采纳 ✅；entryScene 回显 ✅；回归 17 步绿 ✅。

## 任务分解与完成情况

| # | 任务 | 状态 |
|---|---|---|
| 1 | ADR-016（[ADR-016](../../ADR/ADR-016-Standalone-Runtime-And-Minimal-Packager.md)）：D1–D9 决策段、`.baked` 家族全类型口径（LBA1 既有 / LAT1 草案字节表批⑥ 定稿追记 / 结构化 JSON 直拷声明）、出包布局图、lemon-game CLI 形态、saves 便携落位 | **done**（草案 → 当日拍板转正） |
| 2 | D2–D8 拍板（用户）→ ADR 转正 | **done 2026-10-03，均按建议**。D2 拍板前经 Unity/Godot/Unreal 三家对照质证（行业不变量 = 打包消费层与运行时读取层同源；Unreal RunUAT 即独立工具形态；Godot 能放编辑器内因其编辑器=引擎同体，Lemon 无此前提）→ A 独立目标 lemon-packager，编辑器 Export 按钮留作后续 UX 壳 |
| 3 | 开工基线刷新（review 三批 2fce9f0→172975a 后全量重跑，§2 对表） | **done** |
| 4 | 开工核对（svr-test WIP 基线对齐） | 2026-10-01 三轮已收口（M7a.md §4 批⓪ 注记），本批不重复 |
| 5 | entryScene 字段落地（编辑器侧；引擎侧只读解析归批② ProjectFile） | **done**（§3 明细） |
| 6 | 批出口复验：回归 full 17 步绿 + entryScene 回显 | **done**（final 轮 17/17；前一轮 15/17 抖动定性见 §4） |

## 2. 开工基线对表（2026-10-03 实测；构建 = 172975a 增量零改动）

| 指标 | M6c 收官基线（M7a.md §1 #11） | 2026-10-03 刷新 | 漂移归因 |
|---|---|---|---|
| 回归 full | 17 步 | **17/17**（基线轮） | — |
| ctest | 3/3 | **3/3** | — |
| engine-tests | 34036 checks | **34073 checks** | +37：review 三批（#61 TestSpawnFreezeNoLeak、#63 Pool 防护、#48/#68 等） |
| script-tests | 1771 | **1776 checks** | +5：review #71 Paused get 断言（mark 1553）等 |
| bench-survivor（1000 帧） | fps=82 | **fps=84**（alive=10493 / waves=3 / anim 10000/10000 切片命中 / fx texts=256 bars=128 饱和 / 尖峰>25ms = 0） | +2：review #64/#82/#84 微收 |
| vtable | 46 槽 | **47 槽** | +1：review #71 `audioPausedGet` 表尾追加（零重录口径：基准场零调用） |
| 系统 | 20 | **20** | — |
| 组件 | id 至 31 | **id 至 31**（目录 32，tests 断言双点核） | — |

**M7a 纪律前提成立**：vtable 仅表尾 +1（零重录口径内）、组件 id/系统序零变动 → 批②③ 搬运期「金回放零重录」预期不变。

## 3. entryScene 落地明细（D6，ADR-016 M8）

- **解析回显**：`OpenProjectPipeline`（`EditorAppScripts.cpp`）project.lemon 校验块顺手解析可选 `entryScene` → `EditorApp::entryScene_` + `EntryScene()` getter；回显 = 开项目 LEMON_LOG「入口场景声明」，**在场性守卫** = 声明但文件缺 → LEMON_WARN（走回退链提示）。入口回退链本体（entryScene → 唯一 .scene → 多场景缺字段红字）归批② ProjectFile / 批④ lemon-game。
- **写入双点**（`ProjectWizard.cpp`）：模板分支重写前先读模板 project.lemon 的 entryScene 随行（重写只换 name/engineVersion/guid——丢了 = 新项目入口裸奔）；blank 分支显式写 `Scenes/Main.scene`（blank 唯一场景，第一天带字段）。`VsTemplateGen.cpp` 模板 project.lemon 写入同步。
- **回填**：`demo/svr-test`（`Scenes/MainMenu.scene`——六场景项目的多入口歧义就此了断）+ `Templates/vs-survivor`（`Scenes/Main.scene`）。
- **回归锁**：smoke-template RESULT 增 `entry(wiz=%s parse=%s)` 双位——wiz = 播种期读新项目 project.lemon 断言携带（fail-loud：断 = 红字链死）；parse = verdict 期 `EntryScene()` 读回同值。**阴性验证过**：剥模板 entryScene 字段再跑 → 「向导复制丢 entryScene」红字链早死，模板复原后复绿。
- 单点实证：`--project demo/svr-test --smoke` → 「入口场景声明（entryScene）：Scenes/MainMenu.scene」+ PASS；smoke-template 全链 `entry(wiz=YES parse=YES) => OK`。

## 4. 回归抖动记录（本日三轮 16/17、一轮 15/17，均复跑绿）

- 基线轮①：`script-chain` FAIL（overlay `sel=3(≥20)`）→ 单独复跑 ×2 绿（sel=190 稳定）。
- 基线轮②：`smoke-ui` FAIL（`save=0`）→ 单独复跑绿。
- 出口轮①（entryScene 代码后）：`smoke-drag` + `smoke-ui` 双挂（rot 尾差/tools=0/dir 位）→ 两步单独复跑绿；当时机器负载 4.36（Tor Browser + WindowServer 抢资源）。
- 定性：失败位**轮换**、失败子位轮换、全部涉事步骤单独跑确定性绿 → 满载环境抖动（M6c 批④ 起反复记录的既有先例位恰好 = drag/ui/script-chain overlay）。出口轮② full 17/17 收口。
- 工具坑（登记，CI 化时批⑧ 处置）：full 回归经 `\| tail` 管道调用会 mask 脚本退出码（管道取尾命令 0）——判定以 `summary: PASS=/FAIL=` 行为准，裸调用退出码实证。

## 5. 遗留与移交

- entryScene 绝对路径硬拒：编辑器在场性守卫用 `path(root)/entryScene`——手改成绝对路径时 fs 语义为整段替换根，仅影响日志行判据（编辑器不装载）；批② ProjectFile 落地时拒绝绝对路径（review 2026-10-03 登记，非缺陷）。
- ADR-016 LAT1 字节表：批⑥ writer 实现时定稿追记（草案头表已在 ADR M5）。
- 编辑器「Export…」菜单壳（spawn lemon-packager）：UX 增强，不占 M7a 批次（批⑤ 后随手或 M8）。
- 根部游离 `Assets/` 目录（未跟踪，非本批产物）：登记待用户处置；用户 svr-test WIP 未跟踪文件未触碰。
- 全部改动未提交（待用户指令；改动面 = 编辑器四文件 + 模板/演示两 project.lemon + 文档五件）。
