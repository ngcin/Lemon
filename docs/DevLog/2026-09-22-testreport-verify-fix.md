# 2026-09-22 测试报告（Lemon-TestReport-2026-09-22）核实与修复批

黑盒测试报告 3 缺陷 + 6 观察逐条核实：**3 缺陷全部属实（本地复现确认）**，观察
1/2/3/5/6 属实、4 机理可信未复现、7 留用户走查。全批修复：

**BUG-1 CLI --project 静默收养任意目录**：`OpenProjectPipeline` 头部加 project.lemon
存在性守卫（红字 + 失败，统一覆盖 CLI/UI 全入口；向导/最近菜单入口此刻必已有）。
连带：自动重开跳过补 WARN（此前零日志）；**冒烟项目播种补写 project.lemon**——
回归的 ${TMP}/assets|script|ui 一直依赖旧"收养"语义（守卫加上后回归第一时间
抓出，10/11 → 播种补标记后全绿）。复验：不存在目录 → 红字 + exit 1 + **零落盘**。

**BUG-2 坏 project.lemon 静默无视**：管线内最小内容校验（json 可解析 + name 字段）；
坏 = 红字"损坏或缺少 name 字段——按目录继续打开，建议重建工程文件"（不阻断：
Assets/ 场景可能完好）。

**BUG-3 --play 绕过 Play 守卫**：判据抽 `PlayBlockedByScripts()`（!host_ && 有 Game/
工程），交互侧 TryEnterPlay 与程序化侧（--play/--final）共用；程序化侧无头不弹
模态——红字"已阻止进入 Play"+ exit 1。复验：坏档项目 `--smoke --play` → 阻断 +
exit 1、无"进 Play"。

**观察批**：① --frames/--smoke-close 非法值拒启（strtol 校验 + usage + exit 2；
EditorEntry usage 抽公用）；② 截图写失败并入冒烟汇总谓词（此前"PASS 文案 +
exit 1"脱节）+ smoke-close 未知值兜底诊断行；③ 编译红字去重（实测根因 = dotnet
失败时自打两遍：编译段 + 摘要段；ExtractCompileErrors 同文去重，BUG-3 复验输出
单条确认）；⑤ rhi-smoke 未知参数 usage + exit 2（此前静默按默认跑满 300 帧）；
⑥ 场景装载悬空 spriteId 聚合 WARN（合法域 = 程序化页 < SpriteIdBase ∪ DB 记账号
含墓碑；AssetDatabase 补 SpriteIdBase() getter）——复验 4 个改 9999 的
SpriteRenderer 一次性告警。

**复验时新发现并修**：smoke-ui 不带 --smoke 标志，回归曾把 ${TMP}/ui 推成
recent 首条（trap 删目录后成死条目并挤掉用户真实项目）——recent 推送扩到全注入
会话（smoke/smokeUi/smokeDrag/finalTest/smokeClose）都跳过；用户 recent.json 已
清成仅存真实项目（ats）。

**夹具**：etest/Assets/smoke.png(+meta) 遗留已清（文件 + manifest 条目 4→3）；
ats/Game/Probe.cs 用户已自行修复（报告夹具节所述为当时状态）。

**登记不修**：观察 4（smoke assets flaky 新变体——坏档编译阻塞挤压 watcher 帧
窗口）进指南 §3 观察项，M5 冒烟基建改造时处理。修复期间回归偶发一次 basic
smoke FAIL（同二进制立即重跑过 ×2）——同族时序类，留观。

## 回归

`editor-regression.sh full` **11/11 PASS**（重跑两次确认）；三 BUG 复现用例全部
按修复后预期转绿；参数校验/截图汇总/悬空告警逐项单独复验通过。
