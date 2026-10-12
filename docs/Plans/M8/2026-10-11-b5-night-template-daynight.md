# M8 批⑤ —— 模板夜战场 + svr-test 昼夜循环 + 真人走查

Status: done（**机器面 ✅ 2026-10-11 当日**（单测 34,919/script-tests 1,911 持平/ctest 4/4/回归 25 绿 + bench-survivor 软门禁热态波动（单独复跑 80/80 PASS，判机器噪声）/金回放三档零重录 mismatches=0/无头 svr.daynight=2 + Night 15/15 零崩，[DevLog](../../DevLog/2026-10-11-m8-b5-night-template-daynight.md)）；**真人走查 ✅ 2026-10-12 零缺陷终态**（§D 四项全过、FX 零调参反馈；过程期实报五项已随支持期修复 3a57452 收敛复验，[验收记录](../../DevLog/2026-10-12-acceptance-m8-b5-walkthrough.md)））

- 总览行：[M8.md](./M8.md) §2 ⑤行；出口判据 = 模板光照场景进 Play 全流程零 C++ / 昼夜循环 C# 样例 / 真人走查过（灯池观感/昼夜过渡/blob 阴影/蒙版聚光灯 + 光+FX 合成观感——批④ 真人观感归本批组合走查）
- 前置：批① 组件（id 32/33）✅ / 批② 编辑器光贴图 ✅ / 批③ lemon-game 装配 + `Lemon.Lighting` 门面 ✅ / 批④ FX 两壳 ✅
- 用户拍板（2026-10-11）：夜战 = **模板新增独立 Night 场景**（菜单 UI 按钮入口，不加键盘第三位——零引擎 C++ 口径）；昼夜循环 = **挂 svr-test Grass**
- 预估 1–2 日；真人走查可与批⑥ #M4 Windows 真机轮合并排期（M8.md §3 注记）

## 1. 输入条件（2026-10-11 探测核证）

1. **模板产物全部由生成器生成**（`Editor/Templates/VsTemplateGen.cpp` 2235 行；场景 = 程序化构建 ECS + `SceneArchive::Save` 落盘，非手写文本）；再生成整目录 remove_all 重建——**夜战场必须改生成器**，手改 `Templates/vs-survivor/` 会被冲掉。模板现只产 MainMenu + Grass 两场（Volcano = svr-test 模板外扩展不随迁，M7c 批⑨ D4）。
2. **Light2D/BlobShadow2D 已在序列化目录注册**（ComponentCatalog id 32/33）——生成器 `Emplace` 即自动入 .scene，零序列化工作。字段：`type/blend/color/intensity/range/angle/innerRatio/maskSprite/flags`；blob `radius/aspect/offsetY/opacity`（LightComponents.h:27-57）。
3. **蒙版聚光灯（SpriteMask）场景化无先例**：`demo/svr-test/Scenes/light.scene` 零 SpriteMask 灯（蒙版仅存在于 smoke 程序化播种）；`maskSprite` 是进程内 spriteId，场景 JSON 手填无重解析口径。**裁决：模板 Night 用 Spot 楔形聚光；蒙版聚光灯走查项 = 编辑器 Inspector 手工摆灯（顺带验证 Light2D 编辑器工作流）**。
4. **svr-test 的 Player/Mob prefab 与模板已显著分叉**（AnimGraph/HP/脚本类/clipId 全不同）——不能整拷模板产物，blob 手术式加组件（JSON 插块）。
5. **`Lemon.Lighting` 门面 API 全就绪**（Lighting.cs）：`SetGlobalAmbient`（整写绕三坑）/ `DayNight`（color+intensity 双 Tween 单程 Once，循环需脚本链式驱动——`Tween.Alive(handle)` 轮询或 `GameEvent.TweenFinished`（userArg=句柄））/ `BlobShadow.Set`。纪律：**补间期间不得 SetColor/SetIntensity 同字段**（存活补间拥有字段）。
6. **宿主断言面安全**：smoke-guid = 资产 guid 解析（与场景内容无关）；template-chain `uiLoadsFinal==7` = code-mount 文档装载计数（加按钮不加文档）；template-chain/game-smoke/scene-smoke 全走 Grass/MainMenu 路径不碰 Night。回归 26 步预期零断言漂移。
7. **场景挂脚本格式**：实体 `scripts:[{"class":"DayNightCycle","guid":0}]`（ani.scene Gem/TweenDemo 先例）；脚本须 `GameMain.Configure` 里 `Lemon.Behaviours.Register<T>()`。
8. **无头断言口惯例**：`Save.SetString("svr.daynight", n)`（svr.* 家族，PlayerBehaviour.cs:195 先例；缺省 Chan.Slot，跑完读 `.lemon/saves/game.sav`）。
9. 基线（批④ 出口）：单测 34,919 / script-tests 1,911 / ctest 4/4 / 回归 full 26 步 / bench-survivor ≥76.5 / 金回放三档 mismatches=0 / vtable 59 / 组件 id 33 / 系统 21。本批**零引擎 C++ schema 改动** ⇒ 金回放零重录、计数全持平。

## 2. 文件/行级变更单

### A 模板生成器（`Editor/Templates/VsTemplateGen.cpp`）

- [x] **A1 Night 场景块**（Grass 块 L2218-2226 后同構新增）：include `Components/LightComponents.h`；`ctx.NewScene()` + `SetName("Night")`（场景 doc name = C# scene.name 路由匹配面）+ `buildPlayer`/`buildDirector` 复用内联 + 灯实体组（`ctx.CreateEntity("Torch…")` + `Emplace<ecs::Light2D>`）：
  - Global 暗环境（type=0 / 冷蓝夜色 / intensity≈0.30）；
  - 火把 Point 暖橙 Additive 灯池 ×6（出生区外围环形布阵；参数参照 light.scene：intensity 2.6 / range 260）；
  - Spot 楔形聚光 ×2（type=2 / innerRatio 0.35 / 朝向走 Transform2D.rot）；
  - 灯实体不钉 guid（无 C# 引用；避开 `7e57…` 生成器段与 `4730…` smoke 夹具段即可——自动分配满足）。
- [x] **A2 prefab/内联 blob**：`buildPlayer` 加 `Emplace<ecs::BlobShadow2D>`（radius 44 / aspect 1.35 / opacity 0.6——light.scene BlobHero 参照；Player prefab + Grass/Night 场内联三路同享）；Mob prefab builder 加（radius 24 / aspect 1.2 / opacity 0.5）。BossMob/Gem/Bullet 不加（弹体/拾取物无贴地语义）。
- [x] **A3 入口与路由**（内嵌串三处）：main.rml（L485 区）`btn-start` 后加 `<button id="btn-night" class="btn-lg" data-event="start-night">夜战</button>`；GameFlow 串：`kBattleScene` 旁加 `kNightScene = "Night"`、`EnterRun()` 加可选参 `EnterRun(string scene = kBattleScene)` + 静态 `battleScene` 字段（结算 restart 改 `EnterRun(battleScene)`，Start 归位 "Grass"）、`OnSceneLoaded` 认领 `kNightScene`（与 Grass 同分支）、`HandleUiEvent` 加 `start-night`。键盘位不加（菜单 R=Grass 单键维持；零 C++ 口径）。
- [x] **A4 README 内嵌串**（L2014-2068 区）补"夜战场"锚点段（入口/灯构成/blob/走查口径）。
- [x] **A5 再生成对账入库**：临时目录 `--gen-vs-template` → 与 `Templates/vs-survivor` 逐文件 diff（预期差异 = 新增 Night.scene / Player+Mob prefab 加 blob / GameFlow.cs + main.rml + README；**Grass/MainMenu 内容零漂移、kFlowEntityGuid 不变、其余 prefab 零变化**）→ 拷入入库。

### B svr-test 手工同构 + 昼夜循环（`demo/svr-test/`）

- [x] **B1 同构**：`Scenes/Night.scene` 拷模板产物；`Game/GameFlow.cs`（:27 battleScene / :76-77 菜单键盘位不动 / :132 路由加 "Night" / :202 HandleUiEvent 加 start-night / :364+378 StateBag battle 键 0/1 → 0/1/2 三态）；`Assets/UI/menu.rml` 加 btn-night；`Prefabs/Player.prefab` + `Mob.prefab` 手术加 BlobShadow2D 组件块 + `Scenes/Grass.scene` Player 实体同款（svr-test Player 场景内联）；FastMob.prefab 同款加（Mob 拷贝一致性）。
- [x] **B2 `Game/DayNightCycle.cs` 新**（`Lighting.DayNight` 首个真实消费者）：
  - 四关键帧常量表：晨暖白 (255,236,200)×0.55 → 昼正白 (255,255,255)×0.75 → 暮橙红 (255,160,96)×0.45 → 夜深蓝 (96,128,200)×0.30，段长 `kSeconds`（~20s，顶部可调）；
  - `Start`：`Lighting.SetGlobalAmbient` 落首帧初值（组件已在场 = 即时 true）；
  - `Update`：`!Tween.Alive(_handle)` 时推进索引环形 + `_handle = Lighting.DayNight(gameObject, next…)`（Once 单程链式循环；handle=0 组件缺失容错重试）；
  - 每段发起时 `Save.SetString("svr.daynight", n)`（无头断言口）；
  - `OnHotReloadOut/In`：StateBag 迁移段索引（句柄跨域不可靠——热进置 `_handle = 0` 触发重发起）；
  - 头注记纪律：补间期间禁 SetColor/SetIntensity 同字段。
- [x] **B3 Grass 场景挂载**：`Scenes/Grass.scene` 追加 Global 灯实体（tag "DayNight" / guid 顺 Grass 段 9103817739530001003 / scripts 挂 DayNightCycle）+ `GameMain.cs` 注册行。**模板 Grass 保持白天基准零改动**（昼夜 = svr-test 模板外扩展，与 Volcano 同口径；裁决记本文件）。Volcano/Night 不挂（静态夜归 Night、昼夜动态归 Grass，职责分离）。
- [x] **B4 README.md**：补"夜战场（模板同构）"与"昼夜循环"锚点段 + 无头验证跑法（`--smoke --play --frames N` 直开 Grass 读 `svr.daynight`；Night 直开 ≥600 帧零崩口径）。

### C 机器面验证

- [x] **C1** `cmake --preset mac && cmake --build --preset mac` 零警告 + `ctest --test-dir build/mac` 4/4；单测 34,919 / script-tests 1,911 持平（demo 脚本不进 script-tests）。
- [x] **C2** `tools/editor-regression.sh full`（前台会话）26 步全绿；五步（template-chain/game-smoke/scene-smoke/light-smoke/fx-smoke）自动消费新模板；断言漂移核对（预期零——§1.6）。
- [x] **C3** 金回放三档原样 replay 零重录（零 schema 改动；昼夜 tween 不入金档场景流；#L5 正式检查归批⑥ soak 前）。
- [x] **C4** 无头断言手跑记 DevLog：Grass 直开 `--smoke --play --frames 1600`（≈26s 游戏时 = 两段过渡）读 `svr.daynight`≥2；Night 直开 `--frames 600` 零崩零警告刷屏；模板临时产物 dotnet build + template-chain 已覆盖（C2）。
- [x] **C5** bench 门禁随回归（bench-survivor ≥76.5）；ci-daily bench-lights ≥55 软门禁（FX 在场数字重锚归批⑥）。

### D 真人走查（✅ 2026-10-12 通过，出口判据）

清单落批 DevLog（m7c-b9 先例格式）：

1. **模板工程夜战全流程**：MainMenu → 点"夜战" → Night 战斗（灯池观感/火把×bloom 合成观感/聚光锥形/blob 随移动/怪群暗部可读）→ 死亡结算 → R 重开 → Esc 回菜单；
2. **svr-test Grass 昼夜**：进战斗观察 ≥1 轮（晨→昼→暮→夜过渡连续、夜段 ambient 压暗后 HUD/飘字可读）；
3. **蒙版聚光灯**：编辑器开 light.scene → Add Component Light2D → SpriteMask 型选 mask sprite → 观感（顺带验证 Inspector 光照工作流）；
4. **FX 观感反馈**：bloom 阈值/强度、色调对比/暖偏不对味现场反馈（PostFxParams 四参数 C++ 侧批内调）。

**结果（2026-10-12）**：四项全过，FX 零调参反馈，终态零缺陷——[验收记录](../../DevLog/2026-10-12-acceptance-m8-b5-walkthrough.md)。过程期实报（blob 贴脚/地图层/orderTier/TargetBoard 过滤）已随支持期修复 3a57452 收敛并随本走查复验。

通过后另立 acceptance DevLog（仿 2026-10-10-acceptance-m7c-b9-b10.md）+ AGENTS.md 行动项同步。

### E 文档落账

- [x] 批文件勾销（本文件）→ M8.md ⑤ 行 ✅ + Status 行"下一批 = ⑥" → AGENTS.md 当前阶段/基线行 → DevLog 新条目 `2026-10-11-m8-b5-night-template-daynight.md`（含 D 走查清单）。

## 3. 设计裁决（默认执行，用户可否决）

1. **夜战入口只走 UI 点击**，不加键盘第三语义位（Input 位 = 引擎 C++ 改动，破坏"模板光照零 C++"口径；菜单 R=Grass 维持）。
2. **Night 聚光用 Spot 楔形**、蒙版灯不入模板（场景化 maskSprite 无先例，§1.3）；蒙版走查归编辑器手工摆灯。
3. **昼夜只挂 svr-test Grass**：模板 Grass 保持白天基准（昼夜 = 模板外扩展与 Volcano 同口径）；Night = 静态夜、Grass = 动态昼夜，职责分离。
4. **blob 只上 Player/Mob/FastMob**（贴地角色语义）；BossMob/Gem/Bullet 不上。
5. **昼夜循环 = Once 补间链式驱动**（Update 轮询 Tween.Alive 推进下一关键帧），不用 Yoyo（四态非二态往返）。
6. **回归不加新步**：批⑤ 是内容/消费批次（M6a 批① 受击表现同款先例——无头手跑记 DevLog，不进回归）。

## 4. 排程（预估 1–2 日）

日1 上午 = 批文件 + A1–A5；日1 下午 = B1–B4 + C4 无头断言；日2 上午 = C1–C3/C5 全量 + E 落账；日2 下午 = D 真人走查。超期预案：B2/B3 昼夜可独立后挪（先保夜战静态走查）；A+B1 为主干。

## 5. 完工记录（2026-10-11 机器面）

- 数字与过程见 [DevLog](../../DevLog/2026-10-11-m8-b5-night-template-daynight.md)。
- 再生成对账：MainMenu 逐位同（kFlowEntityGuid 钉死生效）；Grass 除 Player blob 外零漂移；prefab 实体 guid 每次生成随机滚 = 既有行为（非锚点）。
- bench-survivor 套件尾态 74/76 两轮 < 76.5 软线、单独复跑 80/80 PASS——本批引擎零改动且 bench 自播种不消费模板，判热态噪声；重锚归批⑥（与 bench-lights FX 在场同口径）。
- 六项裁决全兑现（§3）；回归零新步零断言漂移。
- 移交：真人走查（§D 四项清单）；批⑥ = 压测 A 终测 + soak + #M4 Windows 真机轮 + bench 数字重锚。
- 走查支持期修复（10-12，详 DevLog 追记）：blob 贴脚 / mapbg 地面消费 / 排序
  orderTier（层→tier→批键）/ 非战斗实体 team 2 / **TargetBoard 战斗资格过滤
  （入板 = 有 Health——怪追弹幕鬼畜折返根因，金回放重录 m8b5：sim 两档新录
  自洽 + 终态定向对账，script 档续用）/ lemon-game `--screenshot` 调试口。
