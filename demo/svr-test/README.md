# vs-survivor 模板（M5 批④）

吸血鬼幸存者式开局模板：8 向移动 + 直射弹（可升级穿透）+ 环绕刃 +
队友英雄跟班（跟随 + 飞剑弧线弹道 + 拖尾 + 齐射升级）+ 经验宝石磁吸 +
升级三选一（数字键 1/2/3 或点击卡片）+ 16 波导演（t=565s Boss 波）+
HUD 状态栏（血条/经验/计时/击杀/纪录/队友）+
死亡结算/最高分存档（死亡对话框点击复活）。

由 `lemon-editor --gen-vs-template <dir>` 生成（改玩法请改 Game/
PlayerBehaviour.cs 或场景后重新生成，勿手改 .prefab 内 guid）。
队友英雄/飞剑为模板外扩展（`7e57…a1` guid 段，生成器不触碰）。

## 素材来源与许可

- `dungeon_*` 精灵表与 `*.clip`：yami-rpg-editor（MIT，Copyright (c) 2025
  Yami & Xuran & Contributors）——随模板再分发需在发布物保留版权声明
  （仓库根 THIRD_PARTY.md 已登记）。
- `gem/bullet/pierce/blade.png`：程序化生成（无版权负担）。
- `flysword.png`：程序化生成（16×16 指向性小剑，与模板同风格；无版权负担）。

## 玩法锚点

- **波次表数据化（M6a 批② 范例，2026-09-26）**：Main.scene 的 16 波已外置到
  `Assets/tables/waves.tab`——**加波/调数值 = 改表，不改代码**。编辑路径二选一：
  ①编辑器 AssetBrowser 选中 waves.tab → 底部表格区双击单格微调（Enter 提交，
  进 Play 后生效）；②Excel/Numbers 改 CSV（导出 UTF-8）→ 拖回 AssetBrowser
  覆盖再导入。列契约（首行列头；prefab 列填 Prefabs/ 资产 16 位 GUID）见
  `Game/WaveTableLoader.cs` 头注释；Director 实体挂该脚本（Awake 读表灌回
  WaveDirector，引擎刷怪系统零改动）。表 GUID = `7e57100000100001`
  （新建表需把 GUID 换成新表的并登记到 GameMain）。
- **武器/升级池/XP 曲线表数据化（M6a 批② T4）**：`Assets/tables/weapons.tab`
  （id/label/prefabGuid/interval/speed/pierce/count/radius/angle——弹体参数归
  prefab、发射参数归表；环绕行 speed=角速/count=刃数/radius=轨道半径）+
  `upgrades.tab`（id/label/kind/value；kind 0-6 派发，value = 倍率/换弹种行
  id/点数）+ `balance.tab`（xpCurveK = XP 曲线系数，Start 写
  `Lemon.Balance`→World）。`PlayerBehaviour.Start` 读三表（缺表 = 空池/保底
  参数 + warn，游戏不炸 Play）；**加升级项/换弹种/调环绕 = 改表 + 需要时拖个
  prefab，不改代码**。表 GUID：weapons `7e57100000100002` / upgrades
  `7e57100000100003` / balance `7e57100000100004`（新表按此段顺延）。
- **验收② 演示内容（散射武器 + fast-mob，引擎运行时零改动）**：
  散射 = weapons.tab `scatter` 行（interval 0.4 / speed 300 / count 3 /
  angle 30°）+ `Prefabs/ScatterBullet.prefab`（`7e57100000100005`，Bullet
  拷贝洋红染色 dmg5）+ upgrades.tab `u7 散射弹` 行（kind 3）——选卡后
  Shooter 切散射弹种，主弹 Spawn 事件触发 `PlayerBehaviour.OnSpawn` 沿主弹
  方向 ±15° 补发 2 枚（补发计数落存档键 `svr.scatter`，无 GUI 断言用）；
  fast-mob = `Prefabs/FastMob.prefab`（`7e57100000100006`，Mob 拷贝天蓝染色
  speed 150 / hp 10）+ waves.tab 第 3/7 波 e0 槽替换。
- 玩家：`Player` 实体（PlayerBehaviour 单脚本；多脚本 scripts[] 属 M5 余项）。
- **冲刺/残影**：空格（`Input.Attack` bit4，编辑器已映射 Space）→ 锁定方向冲刺
  160px / 0.16s（≈基础移速 ×10），冷却 0.5s；无方向输入时取最近一次移动朝向
  （`Input` 只有按住态，沿检测在脚本里用 `_attackPrev` 做）。冲刺中每 0.03s 在
  当前位置生成一枚残影：复刻玩家当帧动画帧（AnimatorSystem 先于 behaviour Update
  跑）、中立队 team 2（怪不追不伤）、桶内层序 0 垫玩家（order 1）身后，0.28s 内
  alpha 渐隐 + 收缩到 0.86；距离预算积分故帧率无关，卡片冻结（dt=0）时冲刺/老化
  同停。死亡结算与热重载换域会清扫残影（StateBag 装不了实体清单）。调参入口 =
  `Game/PlayerBehaviour.cs` 顶部 `kDash*` / `kGhost*` / `kPlayerOrder`。
- 队友英雄：`PlayerBehaviour.EnsureAlly` 生成/自愈（环绕刃同款语义），
  挂 `AllyBehaviour`：贴身跟随 + `Shooter` 引擎自动索敌发射 `FlySword.prefab`
  （穿透弹道）。飞剑朝向/贴图/弹道/粒子拖尾由 `AllyBehaviour` 经 `GameEvent.Spawn`
  （Dst=射手）认领代驱——预制体 Play 中途实例化不挂脚本（`ResolvePlayScripts`
  仅 EnterPlay 一次），表现层归射手脚本。弹道 = 出膛扇形偏转（kArcSwirl 侧旋 +
  kArcFan 齐射步进）后预算内转向扑靶（kTurnRate/kSteerTime，对齐/耗尽/目标亡
  即直飞余程），引擎弹道本体零改动（改写 Velocity，弹速守恒）。齐射追加剑在
  认领时刻以引擎剑为轴克隆，升级"飞剑 +1"每级多一把（上限 +4，
  `PlayerBehaviour._swordExtra` 持有并经 StateBag 迁移）。拖尾光点挂中立队
  （team 2）不入敌对目标板（怪 AI 不追光点）。
- 波次：`Director` 实体 WaveDirector（Inspector 数组段可调参）。
- 三选一池：PlayerBehaviour.kOptions（7 项固定序轮换，零 RNG = 回放友好）。

## 受击表现（M6a 批①引擎能力的项目侧接线）

- **资产**：`Assets/hero-hit.clip`（guid `5bd31a7c20000003`）与 `monster-hit.clip`
  （`5bd31a7c20000004`）——yami 表尾帧组合（hero cells[8,7] / monster cells[7,6]
  @12fps loop0），GUID 与 vs-survivor 模板同号；行走段仍是 hero-walk/monster-walk。
- **接线**（`PlayerBehaviour.OnHit`，模板 PlayerCombat 同款）：怪受击 =
  `Anim.Play(怪, monster-hit, loop:false) + Anim.Queue(怪, monster-walk)`（受击段
  立即打断、播完自动回行走）+ 伤害飘字 + 显伤条（sticky 3s 自隐）；玩家受击
  （Hazard 接触）= hero-hit 段同款 + 受伤黄字 + 绿条常显。Fx 色是 RGBA 序
  （与 Ui 的 ABGR 不同，不能混用）。
- **手工核验**：编辑器打开 Main.scene → Play → GameView 看怪被打闪受击帧、
  头顶冒伤害数字、脚下血条；玩家被围时自身头顶黄字 + 绿条。
- **无头验证**（存档出口，rvb.* 同款）：`lemon-editor --project demo/svr-test
  --scene <绝对路径>/Scenes/Main.scene --smoke --play --frames 900 --no-reopen`
  跑完读 `.lemon/saves/game.sav` 的 `svr.mobhit / svr.playerhit / svr.fxtext /
  svr.fxbar`（fxtext = mobhit + playerhit 自洽即通过）。
- **Battle.scene（五万压测场）不接此线**：C# 逐命中订阅在万怪级命中率下跨界
  成本不可接受（M6a 批①计划 §6 风险表口径）——大规模 fx 饱和由
  `--bench-survivor`（C++ 直写通道，fx(256,128) 饱和）覆盖。

## 压测场景：红蓝大作战（Scenes/Battle.scene）

两军对冲性能场（`Game/RedVsBlue.cs`）+ 引擎瓶颈检测器（`--bench-scene`）。
兵力口径：**总兵力每边 25000 = 五万同屏**（`kPerSide`，间距 4.5、160 行×~157
列，分帧播种 800/边/帧防尖刺）。**全员 Patrol 行军**：路径点 = 出生位镜像穿中
（引擎固定速 60），大军持续穿越中线对流、边走边射——无 Chase（五万规模的
索敌是 AI 巨耗源，行军本身消灭"后排站桩"）。五万为单波无预备队（想要持续战
改 kPerSide > kDeployAtStart 恢复补充兵逻辑）。单位两型随机混编（**都是英雄本体** dungeon_hero_1 + hero-walk 动画，对齐
游戏两个角色）：玩家型（PlayerBehaviour 战斗面 = 直射 Bullet.prefab 点射，
约 55%）与队友型（AllyBehaviour 战斗面 = 飞剑技能，齐射 FlySword.prefab +
弧线：出膛奇偶侧旋 + 预算内追踪转向 + 剑头朝向，逐剑追踪上限 768 = C# 成本
护栏，体型 0.85 同款观感，约 45%）。

- **阵营 = 默认势力表现状**：红 = team 1（自分离，阵型散开）、蓝 = team 3
  （1↔3 敌对；同队互穿 → 更密集——两种密度形态同场压测）。弹体势力继承
  射手，双方对射零脚本干预；战斗链全引擎侧。
- **跑法**：编辑器打开 Battle.scene → Play（HUD：battle 行 = 双方存活 + 战况
  条；perf 行 = 模拟步/s + 单位数 + 在追飞剑数）。**瓶颈检测**（逐系统分解、
  Immediate 解禁帧率）：
  `lemon-editor --project demo/svr-test --scene <绝对路径>/Scenes/Battle.scene --bench-scene --frames 2400`
  输出 = 帧八段 + sim 逐系统 avg/max + RESULT（只报数无判据，退出码恒 0）。
  存档键 `rvb.red/rvb.blue/rvb.steps` 为机器可读出口。
- **性能史（本机，同场景口径）**：优化前 AI 线性索敌 25.4ms/帧（88% 占比，
  40fps 卡顿）→ 引擎 TargetBoard 网格桶 + 占位位图 + 战术视距后 **AI 2.2ms、
  全场 7.3ms（解禁 138fps；vsync 60 余量 2.3×）**——金回放 m5b2 三档
  mismatches=0 零重录、bench-survivor playerHp 逐位一致（见
  docs/DevLog/2026-09-25-targetboard-grid-nearest.md）。
- 编辑器双视口（Scene+Game）都画战场——观感测试时关掉 SceneView 省一半绘制。
- 调参入口：`RedVsBlue.cs` 顶部 consts（总兵力/首波密度/占比/视距/弧线/补充
  节拍/染色）。改完热重载 → Stop/Play 重播。
