# 2026-09-26 · Battle 场角色口径纠正——两型均英雄本体（玩家/队友英雄战斗面）

用户澄清：两角色指 **PlayerBehaviour（玩家英雄）** 与 **AllyBehaviour（队友英雄，
飞剑技能）**——不是"飞剑 prefab 当兵"。上一版把飞剑贴图当了一型单位的本体，
本版纠正。

## 改动（RedVsBlue.cs SpawnUnit 重写）

- **两型共用英雄本体**：dungeon_hero_1 + hero-walk 动画；队友型体型 0.85
  （AllyBehaviour"跟班略小一号"同款观感）。
- **玩家型（≈55%）**：PlayerBehaviour 战斗面——Shooter 发 Bullet.prefab
  （伤 8/速 320/命中即毁），Interval 1.5+r0.5s，压进到 130 停住点射。
- **队友型（≈45%）**：AllyBehaviour 战斗面——Shooter 发 FlySword.prefab
  （穿透），Interval 2.4+r0.8s，压进到 170；飞剑弧线认领链保留（侧旋 +
  预算内追踪 + 剑头朝向 + 指向性贴图交换，上限 768）。
- Hazard 通道退场（两个源角色都无接触伤害）——场景变为纯远程战：两阵线
  对射 + 飞剑弧线越阵。

## 验证

- dotnet 0 error；`--bench-scene --frames 900 --screenshot`：frameAvg
  **8.32ms（解禁 120fps）**、alive 5056、零异常。系统分解：Separation 2.5ms
  居首（密集阵型），AI 2.1ms，**Hitbox 0.76ms**（弹幕量增：直射弹 + 飞剑
  双弹种，仍宽裕）。
- 截图视觉验证（/tmp/battle-shot2.png）：双方全英雄小人（左红右蓝、中央
  混战带），两类弹道清晰可辨——白色小弹丸（玩家型）+ 剑形弹幕（队友型）。
