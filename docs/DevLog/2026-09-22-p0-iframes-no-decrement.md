# 2026-09-22 · 【P0 发现】iFrames 无递减——受击一次即永久无敌（M5 支撑度评审副产物）

M5 支撑度评审（三路并行代码探查）复核确认：`Health.iFrames` 全引擎仅两处引用——
`Systems.cpp:372`（`>0` 判免疫）与 `:374`（受击置 0.1s），**无任何递减/倒计时**。
后果：实体受击一次后引擎侧永久免疫，"多段伤害击杀"从未发生过。

**算术佐证**（bench-sim 7200 帧）：玩家 Shooter 20 发/s × 120s ≈ 2400，
`destroyed=2401` 恰为投射物寿命回收数；怪 dmg 12 vs hp 30 需 3 击，第一击后
免疫 → alive 恒 10002（= n + 玩家 + spawner，无减员）。M2 判据未抓到的原因：
判据只看步时/回放哈希，不含击杀数。

**波及**：① 本日性能批②"密度差"归因修正（弹幕击杀搅散蜂群不成立，已改
"玩家走位拖拽 + range 差"，数字结论不变）；② Hazard 路径（`Systems.cpp:436`）
本就不查 iFrames，不受影响；③ M5 首批第一项 = 补 `StatSystem` 或独立递减
（0.1s@60Hz = 6 tick），连带 hits 命中集合/穿透去重一并落（09 §8 欠账）。

同批评审另记：WaveStart/Pickup 事件零发射端、Collectible/XpProgress 零系统
消费、Director/Navigation 占位空跑、Animator 帧映射待 clip 表——均 M5 主线
已知范围（08 §2），不算新发现。

---
