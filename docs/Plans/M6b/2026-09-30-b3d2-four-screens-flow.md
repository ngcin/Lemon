# 批③d-2：铺量批——余四屏 + 档1 流程状态机 + svr-test 全流程接通

- 日期：2026-09-30
- Status: **done（2026-09-30 终验关闭：T1–T10 全勾——真人验收过（首轮抓 T10 后修①「再战一局」残屏，已修 + smoke 补断言 + 阴性验证 + 复测过，[回执](../../DevLog/2026-09-30-acceptance-b3d2-flow.md)）；smoke-template ×2 逐位一致（uidoc=6 + flow 九位）、回归 full 16/16、bench-survivor fps=78 零降级、svr-test 无头验放（装载 4/0、settings.sav 落盘）；实现期发现九条见下）**
- 归属：M6b 游戏UI产品壳（总览页 [M6b.md](./M6b.md)；D2 两拆的铺量半批——③d-1 样板三约定已验证，本批铺满六屏 + 流程闭环）
- 关联：[③d-1 样板批](./2026-09-29-b3d1-sample-screens.md)（两屏 = 本批样板；theme token/画布约定/display 显式化全沿用）· [③d 前置](./2026-09-29-b3d-pre-uidocument-scene-mount.md)（装载双通道）· [ADR-014](../../ADR/ADR-014-Game-UI-RmlUi-Integration.md)（D5 RtUi 兼容层去留 M8 前定案——svr-test 战斗 HUD 仍走 RtUi，本批不迁）· [M6b 出口判据](./M6b.md)①③⑤（六文档/流程状态机档1/smoke 全链）
- 性质：交付 vs-survivor 六屏中的余四屏（主菜单/暂停/设置/结算）+ 档1 流程状态机（单场景、重开 = C# 自律清场）+ svr-test 全流程接通。引擎面仅两处小增量（输入位 bit6、元素盒探针出 x/y），流程本体零引擎改动。

## 开工前摸底结论（2026-09-30，均已对码核实）

| 事实 | 位置 | 含义 |
|---|---|---|
| prefab 实例化走 `LoadEntityTree` 同路径、**重放 scripts[]** | `EditorContext.cpp:465-494`（InstantiatePrefabAsset → SceneArchive::LoadEntityTree；scripts 解析在 `SceneArchive.cpp:238+`） | 重开 = 按 tag 清场 + `Player.prefab`/`Director.prefab` 重 spawn 可行；脚本实例/表载/Start 全部随重挂自然重跑，**不需要手工复位清单** |
| WaveDirector 运行时字段（time/waveIndex/cooldown/spawned）随实体销毁归零；C# 镜像有 GetWave/SetWave | `BehaviorComponents.h:140-152`、SDK `Components.cs:311-330` | 导演走 prefab 重 spawn 即复位；无手写复位路径 |
| 全部 run 实体带 `Meta.tag`（Player/Director/Mob/BossMob/Gem/Bullet/PierceBullet/Blade；tag[24]） | 模板场景 + `Prefabs/*.prefab` 实测 | 清场 = `Query.With<Meta>` 扫 tag 命中集；UI/Flow 实体 tag 不同天然豁免 |
| SceneOps.Destroy 是帧首应用的命令缓冲（Create 占位句柄仅当帧有效） | SDK `SceneOps.cs:1-8` | sweep 与 spawn 不能同拍（销毁命令次帧生效，会误杀新 spawn）→ 状态机加 **Spawning 子态**晚一拍 |
| C# 无立即实体查询面（Query/Chunk 仅批量系统形态） | `Scripting.cs:25-74` | 清场用常驻 `RunSweeper` 批量系统 + `SweepArmed` 门控（每 tick 早退；基准影响 T9 红线） |
| `Instantiate.Prefab(guid,pos)` 经编辑器钩子即时建实体；`Behaviours.TypeIdOf<T>()` 可解析注册序 | SDK `Assets.cs:38-42`、`Behaviours.cs:120-133` | prefab 已带 scripts[] → 连 TypeIdOf 都不必用（摸底备用面） |
| UI ops 全集（Show/Hide/SetText/SetAttr/SetClass/SetStyle/SetInnerRml/SetItems）+ 同值去重已交付 | SDK `GameUI.cs`（③c/③c-2） | 四屏零新 op；设置开关按钮 = SetText 翻文案 |
| `TryGetElementBox` 只出 w/h（无 x/y）；点击注入现有机器只认容器条目中心 | `UiSubsystem.h:181`、`EditorAppSmokeTpl.cpp:269-286` | 菜单/结算按钮点击定位需**盒探针扩 x/y**（③d-1 加探针同款先例，T2） |
| 输入位现状：bit0-4 方向/攻击、bit5 Confirm=R；编辑器映射在 gameViewFocused_ 门控内 | `Engine/ECS/Input.h:12`、SDK `Input.cs:9-18`、`EditorApp.cpp:539-540` | bit6 Pause 三处小改（T1）；Esc 与 ImGui 弹窗争键面 = AssetBrowser 改名取消等（`AssetBrowserPanel.cpp:306,544,602`）——GameView 聚焦门控下互斥，实现期核对 |
| 模板场景无金回放档；bench-survivor 自播种与模板场景无关；UI ops 不入哈希 | ③d-1 摸底同源事实 | 零重录口径延续；bit6 新位不改既有位编码，bench-sim 合成输入零影响 |
| smoke-template 回归 grep 位 = `"smoke-template: .* => OK"`（任意行命中） | `tools/editor-regression.sh:68-77,105-107` | verdict 新位并入主行；`--frames 3000` 需上调（催命重锚 + 二死链 + 流程链） |
| 生成器结构：GUID 常量单源在 VsTemplateGen.h；WriteUiAssets(:120)/WriteGameSources(:353)/场景段(:1158-1216)/exportPrefab lambda(:1219) | `Editor/Templates/VsTemplateGen.cpp/.h` | T3/T4/T5 的改动锚点；prefab 段 `0x7e5710…01-06` 满、UI 段 `0x7e5730…01-03` 用过 |
| svr-test：Main.scene = Player(PlayerBehaviour)/Director(WaveTableLoader)/UIDocument(main.rml 动态屏)；MainMenu.scene 空壳；svr.* 诊断键写 Slot 档 | `demo/svr-test/Scenes/*.scene`、`PlayerBehaviour.cs:144-199` | T7 迁移源与键处置落账点 |

## 设计定案（实现前冻结；D1–D4 = 2026-09-30 用户拍板）

### D1 死亡策略归游戏侧，流程壳只提供原语（用户拍板）

- `GameFlow` 只提供流程原语：`EnterRun()`（清场+spawn）/ `ShowResults(score,time,kills,best,newBest)` / `ReturnToMenu()` / 暂停对（`SetPaused(bool)`）。**不内置死亡策略**。
- 死亡时"复活还是结算"由游戏脚本决定（复活道具/次数数据化皆游戏侧自由）。模板示例策略 = **每局一次复活**：首死弹 ③d-1 复活对话（卡片文档单条形态保留在流程内，判据① 六文档口径），二死 `GameFlow.ShowResults`。落点：`GameMain.Run.ReviveUsed`（PlayerCombat.Die 分叉），未来改道具/表驱动只动这一处。
- svr-test 示例策略 = **直结算**（用户项目无复活道具系统；道具化留用户后续）。

### D2 svr-test 档1 单场景 = MainMenu.scene 唯一入口（用户拍板，执行开工前审核落账）

- `Main.scene` 的战斗实体（Player/Director）迁入 `MainMenu.scene` + UI 六实体 + Flow 实体 → 产品入口；**Main.scene 原样保留作战斗沙盒**（不动）。
- svr-test 四屏用**新文件**（`Assets/UI/menu.rml/pause.rml/settings.rml/results.rml` + `theme.rcss` 拷自模板）——不动用户现有 `main.rml`（③d 前置验收动态屏，用户资产）。
- svr-test 战斗 HUD 仍走 RtUi 兼容层（ADR-014 D5，去留 M8 前定案）——判据只约束流程屏。

### D3 设置屏 = 两个真实开关（用户拍板）

- 伤害飘字 / 世界血条 开关：`Settings` 档持久化（首键 `version=1` + `fx.text`/`fx.bar` ∈ {"1","0"}），GameFlow.Start 载入静态缓存，PlayerCombat.OnHit 消费门控；提示行「更多设置随音频(M6c)加入」。
- UI 形态 = 按钮翻文案（`[开]/[关]`，Click 通道），不用 checkbox（Change 通道虽在，Click 已验证——零引擎面）。

### D4 暂停 = 输入位 bit6 Pause（用户拍板）

- `Input.h:12` 位分配注释 + SDK `InputButton.Pause=6`/`Input.Pause` + 编辑器 `Esc|P` 映射（`EditorApp.cpp:539-540` 旁，gameViewFocused_ 门控内）。流程状态机本体零引擎改动；输入表扩展是「完整输入动作表」既有方向一步。

### D5 重开/回菜单 = 清场 + prefab 重 spawn（零引擎改动）

- `RunSweeper`：常驻批量系统 `Query.With<Meta>`，`SweepArmed` 静态门控（平时每块早退）；armed 时对 tag ∈ 清场集（Player/Director/Mob/BossMob/Gem/Bullet/PierceBullet/Blade）逐个 `SceneOps.Destroy`，一拍后（帧首应用）GameFlow 进入 Spawning 子态 spawn `Player.prefab`+`Director.prefab`。
- 清场集**随批文件落账**（判据③ 口径）：模板集如上；svr-test 集按其 tag 实况（实现期对码补录本文件）。
- Tween 自清 = 实体销毁自然失效（TweenSystem 存活字段）；各屏 SetItems/SetText 复位 = 新局首帧全量重写（同值去重只跳"值相同"的，语义无损）。

### D6 层序与显隐

- `showOnStart`：HUD=1（常显——菜单实底覆盖、暂停/设置/结算半透 scrim 下保留 HUD 语义正确）、Main=1、Pause/Settings/Results=0；运行时显隐全归 GameFlow（D1 最近 Show 序提层）。
- 设置屏入口双源（主菜单/暂停），返回目标记忆 `settingsFrom`。Esc 仅 Run↔Paused 响应（卡片冻结态忽略）。
- 主菜单**无「退出」按钮**：编辑器内语义归 M8 runtime 侧（登记项，随 M6b 收口汇总）。

### D7 GUID 与资产落位

- UI 四屏：`kMainRml=0x7e57300000100004`、`kPauseRml=…05`、`kSettingsRml=…06`、`kResultsRml=…07`（VsTemplateGen.h 常量块尾加）。
- prefab 两枚：`kPlayerPf=0x7e57100000000007`、`kDirectorPf=0x7e57100000000008`。
- 均落 `Assets/UI/`、`Prefabs/`（+ .meta；AssetType::Rml/Rcss/Prefab ③b/既有通道）。

## 任务分解

### T1 输入位 bit6 Pause（引擎/编辑器/SDK 三小处）

- `Engine/ECS/Input.h:12`：位分配注释补 `6=pause`。
- SDK `Input.cs`：`InputButton.Pause = 6`（:17 旁）+ `Input.Pause` 属性（:44 旁）。
- `EditorApp.cpp:539-540` 旁：`ImGuiKey_Escape || ImGuiKey_P` → `in.buttons |= 1u<<6`（同门控）；实现期核对 Esc 与 ImGui 弹窗争键（AssetBrowser 三处 Esc 消费在自身 widget 活动态，聚焦互斥预期成立——R1）。

### T2 盒探针扩 x/y（Engine/Ui，smoke 定位用）

- `UiSubsystem.h:181` / `.cpp`：`TryGetElementBox(doc,id,&w,&h)` 增 `float* x=nullptr,float* y=nullptr` 尾参（默认参源兼容；Border 盒左上）。smoke 菜单/结算/暂停按钮中心 = (x+w/2, y+h/2)。

### T3 模板资产四屏 + theme 扩展（VsTemplateGen.h/.cpp）

- `VsTemplateGen.h`：GUID 常量四枚（D7）。
- `WriteUiAssets`（`.cpp:120` 数组扩四行）：theme.rcss 扩组件区（`.menu-bg` 实心底全屏、`.menu-title`、`.menu-list`/`.btn-lg`、`.set-row` 设置行、`.stat-row` 结算统计行——全引既有 token，必要时加 `--fs-huge`；全 dp + 显式 display，③d-1 两铁律）+ main/pause/settings/results 四 .rml（body 画布约定 + scrim/panel 复用；按钮 `data-event`：start/settings/resume/tomenu/restart/back/toggle-fxtext/toggle-fxbar）。

### T4 Player/Director prefab + 场景重构（VsTemplateGen.cpp:1158-1231）

- 场景段：玩家（:1161-1178）与导演（:1180-1204）实体构造**迁入 exportPrefab 产物**（exportPrefab lambda :1219 扩 spriteGuid=0 无渲染分路）；场景只留 6 UI 实体（HUD/Main showOnStart=1，余 0）+ **Flow 实体**（Meta tag "Flow" + GameFlow 脚本）。
- prefab 占位表（:1104-1116）加 Player/Director 两行（GUID D7）。
- README（:1147-1152）流程段增补：六屏清单 + 档1 单场景说明 + 重开清场语义 + 死亡策略游戏侧声明。

### T5 模板 C#（WriteGameSources :353+，新文件 GameFlow.cs + 三文件改）

- **GameFlow.cs**（新增，挂 Flow 实体）：状态机 Menu/Spawning/Run/Paused/Results/Settings；UI 事件路由（GameMain.OnUiEvent 扩分发表 → GameFlow.HandleUiEvent 静态）；EnterRun（SweepArmed 置位→Spawning→spawn 双 prefab→Scale=1→Run 态复位 GameMain.Run）；ShowResults（SetText 五行 + Show）；SetPaused（Scale 0/1 + Show/Hide）；RunSweeper 批量系统（D5）+ 清场集常量；LoadSettings/SaveSettings（D3）+ 设置按钮文案刷新。
- **GameMain.cs**：`Run.ReviveUsed`；UI 事件分发扩容（cards 既有路由保留 + flow 路由）；Settings 静态缓存（FxText/FxBar）。
- **PlayerCombat.cs**：`Die()`（:321-340）二段分叉（ReviveUsed → ShowResults 原语调用，复活对话仅首死）；`OnHit`（:66-81）Fx.Text/Fx.Bar 受 Settings 门控。
- **PlayerHud.cs**：零改（HUD 常显；新局首帧全量重写）。

### T6 smoke-template 断言随迁（EditorAppSmokeTpl.cpp + regression）

- `uiLoads 2→6`（:140/:415 verdict 位）；点击注入机器（:259-286）泛化为 (doc,目标) 参数化（容器条目中心 + 元素盒中心两源）。
- 新链（帧号实现期按实录重锚，催命段 :339-371 整体后移）：menu(shown)→点开始→run（menu hidden + Player 在场）→ 既有链（HUD/卡片/层序/一死复活）→ **二死**（复活后重压血二段催命）→ results(shown + 得分/最高行非空) → 点重开 → 旧 Player 句柄失效 + 新句柄在场 + HUD kills 归零 → Esc 注入（SmokeTplSteer :152 写 `in.buttons|=1<<6` 一帧）→ pause(shown) → 点继续 → resumed（TimeScale>0）→ 设置链（pause→settings→toggle→文案翻转）→ 回主菜单（tomenu→menu shown + Player 不在场）。
- verdict 主行扩 `flow(menu/start/results/restart/pause/settings/tomenu)` 位；saves 断言翻转：settings.sav **存在**且含 version=1 与 toggle 键（:447-466 段改写）。
- `tools/editor-regression.sh:105-107`：步名更新 + `--frames` 3000→3800（实现期按链实测定格）。

### T7 svr-test 接通（用户项目文件，引擎零改动）

- `MainMenu.scene` 重建：迁 Main.scene 战斗实体（json 级实体数组搬移）+ UI 六实体（menu/pause/settings/results + 既有 main.rml 动态屏 + HUD?——svr 战斗 HUD 走 RtUi 不挂 UIDocument，故 = 四流程屏 + main.rml 五实体）+ Flow 实体。
- `Assets/UI/` 四屏 + theme.rcss 新落（D2 命名）；Game/ 新 GameFlow.cs（拷模板适配：清场集按 svr tag 实况、死亡钩子 = 直结算）+ GameMain.cs 注册 + PlayerBehaviour.cs 死亡分叉调 `GameFlow.ShowResults`、Fx 调用接 Settings 门控（`svr.fxtext/fxbar` 诊断键同步受门控——计数仍写）。
- svr.* 诊断键处置落账（判据③ 遗留）：**保留 Slot 档不动**（局内诊断快照语义，随局清合理）+ PlayerBehaviour 头注一句登记。
- `vs.best` → meta 已改（批② T5 先例）；svr 结算得分公式沿用其 PlayerBehaviour 现行口径（实现期对码）。

### T8 文档落账

- DevLog 收口条目（含四拍板与实现期发现）；本批文件勾销；M6b.md 批次表 ③d-2 行 + Status；AGENTS.md 批③段同步；LoadScene 档2 评估结论留 M6b 收口批（判据⑥，本批不涉及）。

### T9 验证

- `--gen-vs-template` 再生成 + diff 核对（预期：+4 rml ×2 文件、+2 prefab ×2、场景重构（Player/Director 出场景、+5 实体）、Game/ +GameFlow.cs + 三文件改、README）。
- smoke-template 全绿 ×2（确定性两轮数值一致）；smoke-uirml 双模式不回退；回归 full 16/16（负载抖动复跑判读 T1 先例）。
- **bench-survivor 复测红线**：RunSweeper 常驻系统开销不得使 fps 台账降级 >2fps（超标则门控前移/查询缩窄优化）。
- 口径核对：金回放零影响（模板无档/bit6 不改既有位编码）；装载点只在 EnterPlay 扫描（uidoc=6）。

### T10 真人验收（余用户，不阻塞代码面勾销）

- 模板：菜单→开始→一局→Esc 暂停→设置开关（飘字/血条即时生效+跨局持久）→二死→结算→重开（场清干净：残怪/宝石/刃全清、数值归零）→回主菜单→再开局。
- svr-test：MainMenu.scene 同流程全通（与 M6a 验收① 合流的 UI 屏部分现场形态）。

## 实现期发现（偏离批文件预设计的落账，2026-09-30）

1. **EnTT 视图逆序迭代 → 装载 Show 序颠倒**：`MountSceneUiDocuments` 按场景实体
   视图迭代装载+Show，而 EnTT 视图**逆创建序**——HUD（首个声明实体）反而最后
   Show = 置顶，实底主菜单被压在 HUD 下、菜单按钮点击被 HUD body 吃掉（事件
   `doc=hud.rml ev=` 空 = 铁证）。修 = `GameFlow.Start` 显式 re-Show 主菜单（D1
   最近 Show 序；"运行时显隐归 C#"铁律本就指向这里——装载序只是初值，流程屏
   层序自 GameFlow 起）。
2. **prefab scripts[] 的 Play 态实例挂载缺口（真引擎缺口，本批补）**：
   `LoadEntityTree` 只落 ScriptBox 槽**不建 C# 实例**——运行时 spawn 的 prefab
   此前均无脚本，该路径从未被走过（EnterPlay 装配扫描只覆盖进 Play 时已在场
   实体）。症状全链：HUD 不写/宝石不掉/升级卡不弹（ScriptBox 在、实例无）。
   修 = `InstantiatePrefabAsset` Play 分支树遍历 `ResolveSlotBehaviour`（与
   EnterPlay 装配同款；Unity Instantiate 重放 behaviour 的等价路径）。
   Player.prefab 重开重挂 = 首个消费者。
3. **smoke-guid 基线随迁**：「Player + 六 prefab 根」→ Player 迁 prefab 后场景
   无 Player——第 7 根改实例化 `Player.prefab`（顺带覆盖新 prefab 的编辑态
   guid 解析链）。
4. **基线 script-spawn 断言模板模式豁免**：菜单先行使 Stop 时点（回菜单态）无
   run 实体，`playAlive > seeded+10` 恒假——spawn 证明由 smoke-template 流程链
   （重开重挂/mobs/gems 峰值）承接。
5. **Esc 边沿 + EnterRun 重入**：Input.Pause 按住 = 每帧置位会连切暂停——
   GameFlow 加 prevPause 边沿；清场两拍窗口内菜单仍可见可点——EnterRun 加
   Spawning 重入守卫。
6. **svr-test Game 编译阻塞**：TweenDemo.cs 缺 `using System;`（今晨 Tween
   验收批遗留）——补一行解堵（与本批无关但挡 T7 编译）。
7. **smoke 催命块按 Health 过滤锁玩家**：Flow 实体也挂 ScriptBox（无 Health），
   原扫描取"最后一个脚本实体的 Transform"会漂到 Flow 原点——两处催命块加
   Health 过滤。
8. **svr-test 适配补充**（T7 实况）：清场集比模板多 `FastMob/FlySword/
   ScatterBullet` + **`spawned`**（Instantiate.Spawn 的残影/盟友无 prefab 态）；
   `ReturnToMenu` 追加 RtUi HUD 行清屏（D5 兼容层收屏——玩家已销毁无人再写）；
   svr GameFlow 无 GameMain.Run 复位（其局态全在 PlayerBehaviour 实例字段，
   prefab 重挂自然归零）。
9. **T10 后修①（2026-09-30 真人验收反馈）**：「再战一局」后结算屏不消失
   （回主菜单正常）——根因 = EnterRun 的 spawn 完成分支只 `Hide(MainDoc)`
   （覆盖首次从菜单开局的路径），从结算屏重开时 ResultsDoc 无人收；smoke 的
   restart 位只断言清场/归零，**漏了结算屏隐藏**（真人抓的盲区）。修 =
   EnterRun 入口屏即隐（MainDoc + ResultsDoc，点击即走不留残屏——比原
   spawn 完成时隐更跟手）；smoke stage4 补 `!IsDocumentShown(ResultsDoc)`
   断言，**阴性验证过**（回退 Hide → restart/start 位 NO => FAIL）。

## 验收判据（全过才勾销）

1. ✅ smoke-template 全绿 ×2（两轮逐位一致：kills=180 layer=90/0/93）：既有位不回退 + `flow(menu/start/results/restart/pause/set/resume/tomenu)` 九位全 YES + `uidoc=6` + saves 位 `settings=YES`（version=1 + fx.text=0 解码断言）。
2. ✅ smoke-uirml 双模式全绿（dp 位等不回退）；回归 **full 16/16**（2026-09-30 复跑，drag 首轮抖动复跑绿 = T1 先例）。
3. ✅ 模板再生成 diff 干净（+4 rml ×2、+2 prefab ×2、场景 7 实体重构、Game/ +GameFlow.cs + 三文件改、README；旧六 prefab guid 抖动 = 再生成固有）。
4. ✅ bench-survivor **fps=78 PASS**（alive=10436/fx 饱和口径）——与 M6a 批①/③d-1 台账持平，RunSweeper 常驻门控开销零降级（09 §6.10 台账行注记随批）。
5. ✅ 金回放零重录口径成立（模板场景无档；bit6 不改既有位编码；bench-sim 合成输入不涉模板脚本；UI ops 不入哈希）。
6. ✅ svr-test 无头验放：MainMenu.scene 进 Play（5 实体）→ UIDocument 装载 **4 成功/0 缺失**、editor-smoke errors=0、play-roundtrip byte-exact=YES、settings.sav 落盘（LEMONSAV + version/fx.text/fx.bar）。全流程手测归真人验收（判据 8）。
7. ✅ 判据① 六文档全 `.rml`+UIDocument 挂载成立（smoke `uidoc=6` 机器证明）。
8. ⏳→✅ 真人验收（T10）：模板/svr 全流程手测过（首轮抓 T10 后修①、修复复测过，
   [回执](../../DevLog/2026-09-30-acceptance-b3d2-flow.md)）。

## 风险与既知边界

| # | 坑 | 处置 |
|---|---|---|
| R1 | Esc 与 ImGui 争键（AssetBrowser 改名取消/ViewportPanels 三处） | GameView 聚焦门控下互斥（争键点均在自身 widget 活动态）；P 键别名保底；实现期核对 + smoke Esc 注入链即回归锚 |
| R2 | RunSweeper 每 tick With<Meta> 枚举成本（C# 早退但 C++ 构块仍在） | SweepArmed 门控 + bench 复测红线（T9）；模板/svr 规模实测预期 <0.05ms 级 |
| R3 | 催命重锚后波 2 余量（③d-1 已贴边）+ 二死链再加长 | 帧数 3800 预算 + 武装帧实现期实测定格；二死催命复用 deathArmed 二段 |
| R4 | Spawning 晚一拍间隙的 sim 帧（Scale 尚未归零/新导演已 spawn？） | EnterRun 先置 Scale=0 再 armed；新导演 time=0 首波 startTime≥5s——一帧无怪；smoke 断言锚 |
| R5 | settings.sav 断言翻转牵动旧宿主语义（空档跳过 → 现必存在） | 本批起模板恒写 version 键（LoadSettings 兜底建档）；回归 grep 位同步（T6） |
| R6 | svr-test 用户项目改动面（场景迁移 + PlayerBehaviour 钩） | Main.scene 沙盒原样保留可回退；死亡钩 = 单点调用 GameFlow 原语，策略面留用户 |
| R7 | 同值去重 vs 新局首帧重写（值恰好相同的跳过） | 语义无损（同值即同显示）；kills/time 归零必异值必发送 |

## 关联

- 下一批：③e 图鉴/收集模板（波1 全量消费者——本批四屏的 SetItems 规模化验收底座）。
