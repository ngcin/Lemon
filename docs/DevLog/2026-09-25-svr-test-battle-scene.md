# 2026-09-25 · svr-test 新增红蓝大作战压测场景（Battle.scene + RedVsBlue.cs）

用户需求：在 `demo/svr-test` 建性能测试场景——经典红蓝对抗，随机复用现有两角色
（玩家 / 飞剑队友），红蓝各 5000~10000 对战压测。

## 交付物

- `demo/svr-test/Scenes/Battle.scene`：单导演实体（tag BattleDirector）+
  `scripts[]`（schemaVersion 2 首个用户项目用例——Main.scene 仍是 v1 单 script，
  装载链 v1→v2 迁移兼容验证顺带成立）。
- `demo/svr-test/Game/RedVsBlue.cs`（注册进 GameMain）：播种/补充兵/死亡计数/
  HUD 与存档读数；战斗链全引擎侧。
- README 压测场景节（跑法/口径/调参入口）。

## 设计决策（受默认势力表约束的定案）

1. **阵营 = team 1（红）vs team 3（蓝）**：默认表唯一现成敌对对（1↔3；0↔1 被
   玩家语义占用且 0 队无自分离）。红队 1↔1 soft-collide 自分离（阵型散开）、
   蓝队 3↔3 互穿（接敌后更密集）——两种密度形态同场，恰是 Hazard/分离查询的
   双形状压测。**弹体势力继承射手**（`Systems.cpp` AISystem bulletTeam）→ 双方
   Shooter 对射零脚本干预（此前唯一弹队 = 3，若红队用 0/1 会打不中 3 之外的目标
   或误伤——继承语义解掉）。
2. **两型单位五五混编（确定性 LCG）**：英雄型 = 玩家皮 + hero-walk 动画 +
   Chase/Hazard 近战冲阵；飞剑型 = 飞剑皮 + 站桩 Shooter 发 `FlySword.prefab`
   （间隔/首射抖动防同步波）。角色身份对齐用户现有资产（dungeon_hero_1 /
   flysword.png / FlySword.prefab / hero-walk.clip 全部复用，零新资产）。
3. **C# 只做场务**：播种（首 Update 一帧 10000 实体，Instantiate.Spawn +
   ~5 SetComponent/单位）、补充兵（跌破一半每 2s ≤400，防单帧尖刺）、死亡计数
   （GameEvent.Death + Health 过滤弹体寿命）、HUD（RtUi battle/perf 两行 +
   战况条）与存档读数（`rvb.red/blue/fps`，Flush 落盘 = 无 GUI 跑法的机器出口）。

## 验证（macOS Release，编辑器内）

- `dotnet build` 零告警（一次 CS1955：Vec2.Zero 是属性非方法，修）。
- 900 帧：`script-spawn playAlive=10152`、`play-roundtrip byte-exact=YES`、
  存档 rvb.red=4979 / blue=4961 / fps=60.0——双方 5000 起步已接火伤亡。
- 2400 帧（~40s）：red=3092 / blue=2797（均衡绞肉，~40% 战损）、fps EMA 仍 60
  （编辑器 vsync 满帧 = 10k 单位下不构成瓶颈；真实余量看 F3 帧时）、零异常
  零红字、ExitPlay 场景逐字节还原。
- 踩坑两记：①`--scene` 要绝对路径（相对路径按 CWD 解析，回归脚本同款口径）；
  ②`--play` 单独给不进 Play——`EditorApp.cpp:2710` 要求 `--smoke --play` 组合
  （smoke 的 overlay 断言假设播种场景，对本场景恒 FAIL，以 script-spawn/
  roundtrip 行与存档计数为准）。

## 余量与口径注记

- fps HUD = EMA；编辑器呈现同步于显示器 = 上界 60。压真实负载：F3 Profiler 帧
  时分解，或 `kPerSide` 调 10000（两万单位）看 HUD 何时跌破 60。
- 战损速率设计为"慢烧"：飞剑 dps 12×pierce vs 血量 36/60，接敌后加速；若要
  更快清场/更频繁补充，调 RedVsBlue 顶部 consts。
