# M7a 批⓪ 后半：D2–D8 拍板 + entryScene 字段落地

2026-10-03 · M7a 批⓪ 后半（[批文件](../Plans/M7a/2026-10-03-b0-kickoff-adr.md) §3；前半 = ADR 草稿 + 基线刷新，[前半 DevLog](./2026-10-03-m7a-b0-kickoff-adr-baseline.md)）

## D2–D8 拍板（均按建议）

- **D2 独立目标 lemon-packager**（A 案）。拍板过程：用户两轮追问 Unity/Godot 的实现形态——对照质证结论入 ADR-016 M6：行业不变量 = **打包消费层与运行时读取层同源**（Unity Library/ 导入产物、Godot .godot/imported → pck），宿主二进制不是关键；三家自己也不统一（Unity/Godot 打包住编辑器内但编辑器=引擎同体，Unreal RunUAT = 独立命令行工具链 = A 形态）。Lemon 选 A 的三条具体理由：链接器层面禁打包代码伸编辑器（独立性从纪律变编译期保证）/ 批⑤ 逐次自测与批⑧ CI 出包要秒级冷启动的无头小工具 / 编辑器 Export 按钮留作后续 UX 壳（spawn packager，Unreal 同款）不损失交互体验。
- D3 结构化 JSON 直拷 / D4 图集保底+视余量 / D5 存档便携 / D6 加 entryScene 字段 / D7 dotnet self-contained / D8 双 pass 直渲染先行——一轮拍定均按建议。
- ADR-016 状态行转「已采纳」，各决策段「建议」→「已拍板」回填，M8 补落地注记。

## entryScene 字段落地（D6）

代码面（编辑器侧；引擎侧只读解析归批② ProjectFile，口径不抢跑）：

- `EditorAppScripts.cpp` OpenProjectPipeline：解析可选 `entryScene` → `entryScene_`（`EditorApp.h` getter）；回显 = LEMON_LOG「入口场景声明」；在场性守卫 = 文件缺 → LEMON_WARN（回退链提示）。入口回退链本体归批②④。
- `ProjectWizard.cpp` 两写入点：模板分支重写前读模板 entryScene 随行（重写只换 name/engineVersion/guid）；blank 分支显式写 `Scenes/Main.scene`。`VsTemplateGen.cpp` 模板写入同步（保持 #100 生成器↔入库模板一致纪律——手改模板侧与生成器输出逐字节同格式）。
- 回填：svr-test（`Scenes/MainMenu.scene`，六场景项目的入口歧义了断）+ vs-survivor 模板（`Scenes/Main.scene`）。
- **回归锁**：smoke-template RESULT 增 `entry(wiz/parse)` 双位（wiz = 播种期 fail-loud 断言新项目携带；parse = verdict 期 EntryScene() 读回同值）；**阴性验证过**（剥模板字段 → 「向导复制丢 entryScene」红字链死 → 复原复绿）。

## 门格

- 构建 0 error（既有 ld 重复库 warning 一条，非本批引入）。
- 单点：`--project demo/svr-test --smoke` → 「入口场景声明（entryScene）：Scenes/MainMenu.scene」+ PASS。
- smoke-template 全链：`entry(wiz=YES parse=YES)` + flow 全绿 + second-project ids identical。
- **回归 full 17/17**（出口轮；前一轮 15/17 = drag/ui 双抖动，机器负载 4.36 时窗，两步单独复跑绿——既有先例位，定性记录在批文件 §4）。

## 现场与遗留

- 改动未提交（待用户指令）：编辑器四文件 + 两 project.lemon + 文档五件（ADR-016 新建、批文件、两 DevLog、M7a.md）。
- 用户 svr-test WIP 未跟踪文件未触碰；根部游离 `Assets/` 登记待用户处置。
- 下一步 = 批① 缺陷第二批（评审 §8：D6/D7/D8/M21/M22–M25；D8 修在编辑器原位随批③ 迁移）。
