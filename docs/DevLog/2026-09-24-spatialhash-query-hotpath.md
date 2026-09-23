# 2026-09-24 · 性能批：Hazard 万怪密团查询热路径——SpatialHash 查询侧两级加速（Perf10k 51.1→12.5ms 同形状）

来源：2026-09-23 模拟人工验收轮的 Perf10k 压测遗留（09 §6.10 注记）——万怪聚堆
带 Hazard **frameAvg 51.1ms（~19.6fps）**、去 Hazard 17.2ms，~66% 帧时在
HitboxSystem 的 Hazard 查询。本轮分析与修复：

**根因（代码走读 + 成本对账，非猜测）**：不是查询次数——tickPhase 默认 0（出生
当帧首跳）+ 连续刷怪 960/s → tick 随出生时刻自然摊开 ≈ 208 查询/帧，无同帧风暴；
是**单次查询拒绝路径**：万怪 Chase 原点聚成 ~3px 间距密团（数百实体/cell），每次
`OverlapCircle(reach 32px)` 扫 1~4 格 ~800 候选，其中 ~100% 是必拒的同队怪，而
拒绝一个候选 = `PassFilter` 取一次 Meta + 回调再取一次 Meta 判 Hostile（两次 entt
随机访问 ~200ns）——208×800×200ns ≈ 33ms，恰对 51.1−17.2=33.9ms 实测差。
DevLog 前注"距离平方早退"方向已存在（`r2<=reach²`）且排在两次 Meta 取之后，无效；
Separation 不炸是因为有 10 邻居截断，Hazard 查询无任何截断。

**修复（方案 A，查询层一处修，引擎级）**：
- `SpatialHash` Item 内联 team/layer 位（重建时快照自 Meta——运行时零 `Meta.team`
  突变路径，grep 实证；u64+u32 对齐 padding 复用，Item 仍 16B）：拒绝路径纯顺序
  数组读，免逐候选 Meta 取；
- cell 级 team 位图（`CellInfo{teams, hasNoMeta}` 平行数组）：查询前 `teamMask`
  整格早退——密团格零候选扫描；无 Meta 实体恒放行不入位图（旧语义保持）；
- `TeamTable::HostileMask(team)`（行内 32 字节扫）：Hitbox 弹幕/Hazard 两查询点
  传入 hostile 掩码（回调内 Hostile 复核保留，纵深防御）。
- 语义不变量（差分等价单测钉板）：越界 team/layer 恒不命中、无 Meta 恒放行、
  掩码查询命中序 ≡ 默认查询 + 回调手过滤逐项一致。

**验收（负向实验先行：播种红字 → 修复绿字）**：bench-survivor 播种 Hazard 化
（BenchMob 加 Hazard dps 8/radius 24/tick 0.8 对齐 vs-survivor 模板 Mob.prefab；
玩家 HP 500→1e6 防死亡扰计量；判据加 hazard 证据项 playerHp<1e6）：
- **红字（修复前）**：frameAvg **52.05ms fps=19 FAIL**；sim 49.41、系统分解
  **Hitbox avg 36.43ms**（弹幕路径本身 ~0.1ms 量级，几乎全是 Hazard 查询）；
  hazard 证据 playerHp=275793 生效；
- **绿字（修复后三跑）**：frameAvg **12.17~12.59ms fps 79~82 PASS**（12.59/
  12.51/12.17）；**Hitbox 36.43→1.08ms（~34×）**、SpatialHashRebuild 0.52→0.57
  （Meta 快照 +0.05ms 几乎免费）；alive/waves/anim/hazard 四证据与红字跑逐位
  一致（playerHp 同为 275793 = Hazard 伤害行为零漂移的直接旁证）。
- **回归全绿**：engine-tests **13173**（+15：内联位/整格早退/越界/noMeta 钉格/
  HostileMask/差分等价）；ctest 3/3；**金回放 m5b2 三档原样 replay mismatches=0
  （零重录——命中集合与回调序零漂移的机械证明，schema 一字不动）**；
  editor-regression full **13/13**（smoke-ui 首轮即绿，无飘忽）。

**台账口径决策（09 §6.10）**：播种并入而非独立变体档——Hazard 化后判据阈值不动
（22.2ms），历史行不删，表加红绿两行 + 口径变更注记（批②③同款格式）；"带 Hazard
模板怪海"自此为 bench-survivor 判据形状（Perf10k 同形状场，无需第二个命令）。
Pickup 密核扫描观察项维持（Collectible 非 team 语义，掩码不适用——宝石侧查询
降频/惰性自检属行为变更，仍留单独批）。

---
