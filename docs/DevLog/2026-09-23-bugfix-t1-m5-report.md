# 2026-09-23 · 测试报告缺陷修复批：BUG-1~5 全修（T1-M5 模拟人工测试轮副产物）

依据 [测试报告](../Reports/2026-09-23-test-report-m1-m5.md) §2 缺陷清单，5 项全部核实属实并修复：

- **BUG-1（P1）管线缓存损坏→启动断言崩溃**：`RHI.cpp` Load 侧对
  `vkCreatePipelineCache` 非 SUCCESS 且带 initialData 时告警弃档（可恢复缓存未命中
  语义）+ 空缓存重建重试；Save 侧改临时文件 + rename 原子替换（写入中断不留半截档）；
  `VkResultName` 补 `INITIALIZATION_FAILED` 名。实测：截断档（9672→4096B）与垃圾
  字节档双形态均告警重建、exit 0、无 `.tmp` 残留；二次启动命中新档。
- **BUG-2（P2）Profiler 系统表 Play 中恒空**：`ProfilerPanel.cpp` 两处
  `ctx.World()` → `ctx.ActiveWorld()`（Play 中 = playWorld）。实测：bench-survivor
  会话（ini 保持 Profiler 活动标签）截图系统表有行（Separation/Movement/Census 等，
  runs=600），帧率曲线/GPU/GC 不变。
- **BUG-3（P3）首启 Profiler 浮窗遮挡 Hierarchy**：`SetupDefaultLayout` 补
  `DockBuilderDockWindow("Profiler", bottomId)`（先于 Console/Assets 停靠 = 隐藏
  标签不抢当前页）；smoke 注入会话显式关闭保留（注释同步）。实测：全新目录首启
  截图底部标签 = Profiler|Console|Assets（Assets 活动态），无浮窗。
- **BUG-4（P3）bench-script --cpp-compare 未分档**：按 ADR-010 D5 实现——
  `--n ≥100000` 才 1.5× 硬判，小档只打印"非 100k 档仅记录"；RESULT 判分与头注释
  同步。实测：5k 档 ratio 2.33× → RESULT PASS（旧口径恒 FAIL）；100k 档 1.43× →
  PASS（硬判语义不变）。
- **BUG-5（P2 文档/易用性）bench-mow 无 --frames 永不退出**：frames=0 时启动即
  打印"runs until window close / ESC (unattended runs need --frames N)"；
  09 §5 示例命令补 `--frames 900` 并注记缺省语义。

**回归全绿**：ctest 3/3（engine 13098 / script 1340 checks）；editor-regression
`full` **11/11** 首轮（smoke-ui 未飘忽）；bench-mow 900 帧 ×2（129.2fps，缓存
load/save 往返正常）；bench-survivor 900 帧 **PASS**（alive=10467 frameAvg=16.05ms
fps=62，尖刺 38 个/跑——Census 既有观察项维持）。观察项 OBS-1/2/4 属口径/设计讨论，
本轮不动（报告 §3 留档待拍板）。

---
