# 2026-09-26 · SeparationSystem 队掩码预滤——互穿队零成本 + Battle 场万级同屏

用户需求：Battle 场同屏单位 3000 → **1 万以上**。首波列阵提到 5000/边（间距 10）
后实测 33.79ms/帧（30fps），新瓶颈 **Separation 14.16ms** 居首。

## 根因：互穿队在做全量邻居扫描

`SeparationSystem` 查询用默认 `QueryFilter{}`（teamMask 全开）——蓝军（team 3，
3↔3 互穿 = 无任何 soft-collide 关系）5000 单位每 tick 全量扫邻居、逐候选
`SoftCollide` 判定后全数拒掉：密场下纯耗 ~7ms。而空间哈希的 **cell 级队伍位图
早退 + 候选级内联拒**（`SpatialHash.h` L14/L125，2026-09-24 spatialhash 批落地）
分离系统没有用上。

## 修复：soft-collide 队掩码预计算（每 tick 32×32 查表一次）

- 掩码 = 0 的队（互穿队）**整体跳过查询**；非零队 `QueryFilter.teamMask = 掩码`
  进查询 = 整格早退 + 候选内联拒，免逐候选 Meta 取。
- **语义不变**：掩码命中的候选 = 原 SoftCollide 判定恰会接受的候选；无 Meta
  实体恒放行不入位图（回调 `om==nullptr` 分支保留，同旧序）；`f.exclude=自身`
  与回调自排双保险。
- **零漂移三证**：engine-tests 23267 全过；金回放 m5b2 三档 mismatches=0；
  bench-survivor 900 帧 playerHp=275793 逐位一致 PASS。

## Battle 场最终口径（万级同屏）

| 配置 | frameAvg | 解禁 fps |
|---|---|---|
| 5000/边 @间距 9（修复前） | 33.79ms | 30 |
| 同上 + 分离掩码修复 | 20.16ms | 50 |
| + 射速放缓（削弹量：AI 板/Hitbox 双受益）+ 视距 256 | **17.94ms** | **56** |

- 同屏 ~10000 单位 + ~4900 弹幕（alive 14902）；分解：AI 6.8 / Separation 2.3
  （14.2→2.3，6.2×）/ Hitbox 2.1。截图验证：万级英雄大军铺满全屏、中央混战带。
- **密度↔帧率旋钮**（README 已记）：kDeployAtStart 3500/边 ≈ 60fps+、5000/边
  ≈ 56fps。

## 后续候选（登记不扩 scope）

- **AI 查询内部**（6.8ms，新任首位）：接战带单位环搜扫描大量已占格候选；
  候选项需 cell 内距离序或分级缓存——动 Chase/目标板语义需按 09 §6.8 预判
  （弹体入目标板是既有语义：bench-sim 金档怪追玩家弹在档，改动=重录口径）。
- Separation max 尖刺 24ms（密集接触帧）观察项；densityCap/maxNeighbors 调参
  随 AI 批一并评估。
