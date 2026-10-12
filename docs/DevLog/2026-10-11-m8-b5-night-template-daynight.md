# 2026-10-11 M8 批⑤ —— 模板夜战场 + svr-test 昼夜循环（机器面当日过）

## 事件

M8 批⑤（模板与 svr-test 消费）机器面当日完成：vs-survivor 模板新增 **Night 夜战
战斗场**（独立第三场，火把灯池 + 聚光 + blob，UI 按钮入口）+ svr-test **Grass 昼夜
循环**（`Lemon.Lighting.DayNight` 首个真实消费者）。用户开工前拍板两项：夜战 =
独立 Night 场景（菜单按钮入口，不加键盘第三语义位——零引擎 C++ 口径）；昼夜 =
挂 svr-test Grass（模板 Grass 保持白天基准，昼夜属模板外扩展与 Volcano 同口径）。
[批文件](../Plans/M8/2026-10-11-b5-night-template-daynight.md)

## 交付物

- **生成器**（`Editor/Templates/VsTemplateGen.cpp`，引擎运行时零改动）：
  - Night 场景块（Grass 同構双发 buildPlayer/buildDirector + 灯组 12 实体：
    Global 冷蓝暗环境 ×0.30 + 火把 Point 暖橙灯池内 4（r180）外 6（r340）+
    Spot 楔形聚光 ×2 自底部两角打向出生区——方向按 light.frag "local +X 旋转"
    约定 atan2 取角）；
  - `buildPlayer` 加 BlobShadow2D（44/1.35/0.6——light.scene BlobHero 参照，
    prefab 导出与 Grass/Night 内联三路同享）+ Mob prefab builder 加（24/1.2/0.5）；
  - GameFlow 内嵌串：`EnterRun(string scene = kBattleScene)` 参数化 + battleScene
    静态字段（结算重开/StateBag 0/1/2 三态）+ OnSceneLoaded 认领 Night +
    HandleUiEvent `start-night`；main.rml 加「夜战」按钮（menu-list 流式布局零
    定位冲突）；README 串补夜战锚点段。
- **再生成对账入库**：临时目录生成 → `Templates/vs-survivor` diff 对账（预期差异
  = Night.scene 新增 / Player+Mob prefab 加 blob / GameFlow+rml+README；MainMenu
  逐位同（kFlowEntityGuid 钉死生效）、Grass 除 Player blob 外零漂移；prefab 实体
  guid 本就每次生成随机滚（非锚点语义，BossMob 等未动件仅 guid 差异））→ rsync
  入库（排除 .lemon/obj/bin）。
- **svr-test 同构**：Night.scene = 本仓 Grass 实体（svr-test 版 Player/Director——
  模板版脚本未注册不可整拷）+ 模板灯组拼装；GameFlow 三处同構（路由/UI 事件/
  StateBag 三态）；menu.rml 加按钮；Player/Mob/FastMob prefab + Grass Player 实体
  手术加 blob 组件块。
- **昼夜循环**：`Game/DayNightCycle.cs` 新（晨暖白 0.55 → 昼正白 0.75 → 暮橙红
  0.45 → 夜深蓝 0.30 四关键帧环形；Once 补间 + `Tween.Alive` 轮询接力，段长 20s
  InOutQuad；每段发起写 `Save:svr.daynight`；StateBag 迁移段索引、热进重发起）
  + Grass.scene 追加 DayNight Global 灯实体（guid 9103817739530001003 顺段）+
  GameMain 注册。
- **README**：svr-test 补「夜战场与昼夜循环」锚点段（构成/入口/调参/无头验证
  命令）。

## 机器面数字

- 构建：mac preset 零警告（引擎侧仅 VsTemplateGen.cpp 一件）；模板工程与
  svr-test dotnet build 均 0 错误。
- ctest 4/4；单测 **34,919** / script-tests 1,911 持平（引擎零改动）。
- 回归 full 26 步：**25 绿 + bench-survivor 软门禁尾态波动**——套件尾部两轮
  fps=74/76（< 76.5 软线；sim 尖刺帧 41–49ms），**单独复跑 80/80 PASS**；本批
  引擎零改动且 bench-survivor 自播种（tempdir 场，零 Light2D/BlobShadow2D、
  不消费模板）——判 25 步套件尾部热态衰减的机器噪声而非代码回归（首轮 74 的
  并行污染已排除：当时金回放后台任务抢 CPU）。批④ 同日晨间基线 82。数字重锚
  （bench-lights FX 在场同款）归批⑥ 一并处理。
- 金回放三档**原样 replay 零重录**：sim-st / sim-mt `replay=PASS mismatches=0`、
  bench-script PASS；终态三元组 alive=8249 / created=16003 / destroyed=7754 与
  批① 档逐项一致（零 schema 改动口径兑现）。
- 无头断言：Grass `--smoke --play --frames 1600`（≈26.7s 游戏时）→
  `svr.daynight=2`（两段过渡发起，昼夜链路端到端）；Night `--frames 600`
  15/15 实体 errors=0 零崩。
- 五个模板消费步（template-chain/game-smoke/scene-smoke/light-smoke/fx-smoke）
  全绿零断言漂移（uiLoadsFinal==7 不受按钮影响、smoke-guid 资产面无涉）。

## 设计裁决（批文件 §3 兑现）

1. 夜战入口只走 UI 点击；2. Night 聚光用 Spot 楔形（蒙版场景化无先例）；3. 昼夜
只挂 svr-test Grass；4. blob 上 Player/Mob/FastMob 不上 Boss/Gem/Bullet；5. 昼夜 =
Once 链式循环（非 Yoyo 四态）；6. 回归不加新步（内容批次，M6a 批① 先例）。

## 真人走查清单（待用户，出口判据）

1. **模板工程夜战全流程**：`build/mac/Editor/lemon-editor --project
   Templates/vs-survivor` → Play → 主菜单点「夜战」→ 战斗（**灯池观感**：出生
   区火把暖池/外圈引导；**火把 × bloom 合成观感**（Med+ 档 FX 链在场——批④ 真人
   观感归本批）；**聚光锥形**：底部两角楔形光；**blob 阴影**：玩家/怪群脚下暗斑
   随移动）→ 死亡 → 结算 R 重开 → Esc 回菜单。
2. **svr-test Grass 昼夜**：`--project demo/svr-test` → Play → R 进草地 → 观察
   ≥1 轮（晨→昼→暮→夜 80s/轮；过渡连续性、夜段 ambient 压暗后 HUD/飘字可读性）。
   同场也可走「夜战（灯池）」按钮看 svr-test 版 Night。
3. **蒙版聚光灯**：编辑器开 `demo/svr-test/Scenes/light.scene` → 选中实体 →
   Add Component → Light2D → Type 选 SpriteMask + maskSprite 选图集 sprite →
   观感（顺带验证 Inspector 光照编辑工作流——批② 追记的启用复选框同路径）。
4. **FX 观感反馈**：bloom 阈值/强度、色调对比/暖偏（PostFxParams 四参数：
   0.72/0.45/1.06/0.10）不对味现场反馈，C++ 侧批内调参。

排程注记：本走查可与批⑥ #M4 Windows 真机轮合并排期一次到位（M8.md §3）。

## 意义

批①–④ 的光照/FX 能力首次被"真游戏模板 + 真样例"消费：模板自带夜战形态（生成
即得）、昼夜循环成为 `Lemon.Lighting` 的参照实现。段出口判据"模板光照场景进
Play 全流程零 C++ / 昼夜循环 C# 样例"机器面已证；真人走查为最后出口。下一批 =
⑥ 压测 A 终测 + soak 2h（含 #M4 Windows 真机轮与 bench 数字重锚）。

## 追记（2026-10-12 走查支持期修复）

- **blob offsetY 贴脚修复**（走查实报：椭圆居精灵中心，"黑斑贴身上"）——模板
  生成器 Player offsetY 0→14 / Mob 0→10（yami 小尺寸素材）+ 再生成入库同步；
  svr-test Player 系 0→26 / Mob 系 0→20（player04/fox 64px 素材）——Grass/Night
  场景实体与 Player/Mob/FastMob prefab 六处。走查另确认：无地面观感别扭主因 =
  场景本无地图层（模板史实，M9 Tilemap 段范围），非光照缺陷。
- **用户导入 mapbg.png（1254×1254，meta 预写 guid 7e57100000100012）作世界固定
  地面**：Grass/Night 加 Ground 实体（scale ×4 ≈5016px 覆盖，sortOrder -100，
  受光层内——昼夜/灯池/blob 落地即生效）。走查实报"玩家被背景挡住"暴露**引擎
  排序契约缺陷**：包列表按槽（批）拼接、跨批绘制序 = 槽比较器 `(layer, hash)`，
  独立图集页背景的负 order 压不到别页实体之下。**两轮修复**：①首轮改 sortKey
  位序（order 先于 hash）= 无效操作——sortKey 只决槽内序，跨批序在槽比较器
  （教训：改前应追完整数据流到 Bake 连续段约束）；②真修复 = 批键加
  `orderTier`（order>>12 偏移 +16，位域切自 _pad 6bit），槽比较器变
  `(layer, orderTier, hash)`——tier 间分桶、tier 内混批如旧（order ∈{0,1} 的
  角色/怪同 tier 同批零扰动）；Renderable.h/02 §排序行同步。复验：ctest 4/4 +
  template-chain smoke 全 OK + **lemon-game `--screenshot` 机器自证**（Grass 180
  帧：玩家立于地图之上、背景铺满）。
- **lemon-game 新增 `--screenshot <path>`**（末帧交换链 PNG，配 `--frames N`；
  stbi_write_png 前向声明借 StbImage.cpp 实现 TU 符号，不外扩头包含域）——
  走查调试判据设施，后续观感问题可无头复现。
- **怪不追玩家/原点堆叠（走查实报 → 根因 → 修复）**：TargetBoard 注册面 =
  全部 `Meta+Transform2D` 实体（无战斗资格过滤），Ground/DayNight/Director
  三个 team 0 静态原点实体全在可追击表内——Chase 每 tick 重查最近邻，玩家
  一走远怪即转向原点堆叠（Director 系 M7c 起潜伏，mapbg 让玩家走远后显形）。
  修复 = 非战斗实体全部移出可追击队（team 2 = 冲刺残影/宝石中立队先例，
  怪不追不伤）：svr-test 四场景（Ground/Director/DayNight/Flow）+ Director
  prefab + 模板生成器（exportPrefab("Director",2) + Grass/Night 内联 team 2）
  再生成入库。复验：lemon-game Grass 900 帧无头截图 = 怪四面合围玩家（对比
  修复前"散点+原点堆叠"），弹道/暴击/击杀正常。
- **AI 鬼畜折返根因修复（走查实报"偶尔前进一步又退回去"→ TargetBoard 战斗
  资格过滤）**：弹幕/环绕刃（Meta 继承射手 team、无 Health）全在可追击表内，
  Chase 每 tick 重查最近邻 → 怪的目标在玩家与逼近的子弹间高频翻转（子弹
  0.1s 内飞过/命中即消失）= 折返鬼畜；飞剑×19 + 弹幕密集后翻转频率大增显形。
  修复 = `TargetBoard::Rebuild` 入板资格改 `Meta+Transform2D+Health`（战斗
  实体限定；Systems.h 契约注释 + 03 §AI 行同步）；三个钉行为的单测补 Health
  （板资格口径，检查数 34,919 持平）。分离力排除嫌疑（vel += 叠加式标准
  flocking，非主因）。
- **金回放重录 m8b1 → m8b5**（行为有意变更口径，09 §6.8 重录先例族）：sim-st
  / sim-mt 新录 + replay 自洽 `mismatches=0`；终态对账 = alive 8249→8099
  （−150）/ destroyed 7754→7904（+150）/ created 16003 不变——恰为"怪不再追
  子弹、直接接战玩家、18k 帧内多消灭 150 只"的定向变化，零不可解释漂移；
  script 档原样 replay **PASS**（场景无 Chase/Shooter，过滤无涉，m8b1 续用）。
- 复验：lemon-game Grass 900 帧无头截图 = 怪群成列合围玩家、接战点收敛于
  弹幕交界（对比修复前全向折返）。
- **回归收口（10-12）**：orderTier 修复后 full 重跑 **26/26 全绿** fps=79；AI
  资格过滤后 full 重跑 25 绿 + bench-survivor 环境尖刺（present 段 947ms 卡顿
  打崩均值——sim 尖刺均值反由 41–49ms 降至 17.4ms = AI 修复后模拟更便宜），
  单独复跑 fps=77 PASS；bench 数字重锚仍归批⑥。


