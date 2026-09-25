# 2026-09-26 · AISystem Patrol/Shooter 段并行化——五万场 21→33fps

来源：五万场实测（[上一条](./2026-09-26-battle-50k-march.md)）AI 27ms 居首，且
进程 CPU 仅 ~1.4/6 核（用户问"是不是 GPU 没发挥"——帧八段 GPU 段合计 <0.4ms，
瓶颈是引擎模拟循环的单线程段）。用户批准按建议开工。

## 改动（Systems.cpp AISystem 内两段）

1. **Patrol 段并行化**：单线程 view 循环 → `Pool<Patrol>` 池切分 ParallelFor
   （grain 256，守卫同 Chase 段；逐实体纯写、零跨实体访问）。五万行军的
   全量速度写由此入并行。
2. **Shooter 段拆两段**：冷却递减 + `board_.Nearest` 索敌**并行**（Nearest 对
   目标板只读）；开火生成保持**主线程按原 view 序串行**（工厂变更池 + 事件
   入队序 = 回放确定）。净行为与原单线程逐位同构。

## 验证（零漂移全绿）

- engine-tests **23267 checks OK**；
- 金回放 m5b2 三档：sim-st/mt `mismatches=0`、script PASS；
- bench-survivor 900 帧：**playerHp=275793 逐位一致** PASS（fps=79）；
- editor-regression quick **6/6**。

## 实测（五万场，1800 帧 + 截图验证）

| 指标 | 并行化前 | 并行化后 |
|---|---|---|
| AI avg | 27.04ms | **9.86ms（2.7×）** |
| sim 段 | 38.44ms | 19.77ms |
| frameAvg | 48.5ms | **30.0ms** |
| fps（解禁） | 21 | **33** |

## 余量登记（下轮候选，按当前大小序）

1. **AI 余 9.9ms 的大头 = 目标板 Rebuild 串行**（每 tick 全量收集 + 双队排序；
   max 尖刺 47ms 即其指纹）——并行收集/归并排序是正办，动内部结构需按
   09 §6.8 复验（查询只读，理论零漂移）。
2. **渲染提取/烘焙 8.3ms**（双视口 5 万精灵）——提取段并行或 dirty 裁剪；
   动渲染侧按 Vulkan 纪律带 --validate 自测。
3. Separation 5.6ms 调参（densityCap/maxNeighbors，动语义需预判）。

三件全落预计五万场 ~45-50fps；万级口径预期回满 60。
