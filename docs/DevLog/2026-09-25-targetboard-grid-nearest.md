# 2026-09-25 · TargetBoard 网格最近邻 + --bench-scene 通用压测模式（Battle 场回灌①）

来源：svr-test 红蓝对抗压测场（[Battle 场手测反馈](./2026-09-25-battle-scene-feedback-fix1.md)
后续）——用户报 40fps、同屏不足万、飞剑行为不对、要求"有效检测引擎性能瓶颈"。

## 发现：AI 系统 25.4ms = 模拟段 88%

新工具 `--bench-scene`（任意 --project/--scene 跑 bench-survivor 同款测量：Immediate
呈现 + 帧八段 + sim 逐系统分解；不播种无判据只报数，退出码恒 0；EditorEntry/
EditorApp.h/EditorApp.cpp 接线，survivor 判据路径零改动）首跑即定位：

```
AI avg=25.36ms（88%）｜Separation 2.43｜Hitbox 0.34｜渲染 scene 段 1.51
```

根因：`TargetBoard::Nearest` 全列表线性扫（Systems.cpp 原实现）。survivor 品类
= 万怪查 1 玩家（目标板 1 候选）从未暴露；红蓝 many-vs-many（~13500 查询者 ×
~6500 候选 ≈ 7400 万距离判定/帧）直接撞墙。**这是引擎级瓶颈**——塔防多路、
阵营战类游戏同款受害面。

## 修复：网格桶 + 占位位图（两级）

1. **CSR 网格桶**（cell 64px，Rebuild 时排序键建桶，同 cell 保池序 = 确定性）：
   Nearest 改环搜（ring min 距离平方 ≥ bestD2 即停）。首轮实测 AI 25.4→19.5ms
   ——不够：**空场远距查询**（后排 3000 码索敌无果）环扫 47 圈、每空格一次
   键二分，比线性还贵。
2. **占位位图 + bbox**（1bit/cell）：空格测试 = 界限比较 + 一次位读，免键
   二分；bbox 外恒空直接跳。配合场景侧**索敌视距 3000→320/340**（战术级，
   本来就不该全图索敌——接战带 ≈ 视距深）。

## 实测（macOS Release，Battle.scene 2400 帧）

| 阶段 | AI avg | 全场 frameAvg | 解禁 fps |
|---|---|---|---|
| 线性（优化前） | 25.36ms | 31.49ms | 32 |
| 网格桶 | 19.50ms | 25.32ms | 39 |
| +位图 +战术视距 | **1.69ms** | **6.17ms** | **162** |

**零漂移三证**：engine-tests 23267 全过；金回放 m5b2 三档原样 `--replay`
**mismatches=0**（零重录——等距平局语义变化未触及三档基准场，09 §6.8 口径）；
bench-survivor 900 帧 **playerHp=275793 逐位一致** PASS（fps=80）。

## 场景侧配套（svr-test，同日）

- 飞剑对齐游戏本体：出膛奇偶侧旋（kArcSwirl 0.7）+ 预算内追踪转向
  （kTurnRate 4/kSteerTime 0.6，AllyBehaviour 同源数学）+ 剑头朝向；逐剑追踪
  上限 768（C# 成本护栏，超出只偏转不追踪）。"飞剑不消失"查实为伪——命中
  穿透耗尽/1.8s 寿命销毁路径都在，系时间膨胀 + 活弹过多的观感。
- 兵力口径重写：kPerSide=10000 总兵力/边 + kDeployAtStart=4000 首波列阵 +
  近线补充（出场即在接战带）——同屏常驻 ~8000 绞肉 + 攻城式持续压力。
- 终测：常驻 ~8000（红 3820/蓝 3795）+ 飞剑弧线在场，**frameAvg 7.26ms
  （解禁 138fps，vsync 60 余量 2.3×）**，CSharpBatch 0.09ms（追踪 768 把
  几乎免费）。零异常零红字。

## 后续候选（观察，不扩本条 scope）

- Separation 1.8ms 成次热点（max 23ms 尖刺——密集接触帧）；密度截断参数
  （03 §14）可再收紧。
- 环搜等距平局语义 = 环扫序先见者（原线性 = 池序靠前者）——已在 Systems.h
  注释声明；基准场无平局案例，金回放实证。
- 09 §6.10 台账：TargetBoard 行待下轮回归并入（bench-survivor 判据数字未变）。
