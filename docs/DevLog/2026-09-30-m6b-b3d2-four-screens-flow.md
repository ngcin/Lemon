# 2026-09-30 · M6b 批③d-2 铺量批代码面收口——余四屏 + 档1 流程状态机 + svr-test 接通

- 日期：2026-09-30
- 性质：批次收口记录（[批文件](../Plans/M6b/2026-09-30-b3d2-four-screens-flow.md)；
  M6b 总览 [M6b.md](../Plans/M6b/M6b.md)）
- 开工四拍板（用户，同日）：死亡策略归游戏侧（模板示例 = 每局一次复活）/svr-test
  MainMenu.scene 唯一入口/设置两真实开关/暂停 bit6 Pause

## 交付面

- **六屏齐**：main/pause/settings/results 四 .rml 落 `Assets/UI/`（theme.rcss 扩
  流程组件区，全 dp + 显式 display 两铁律）；场景 = 6 UIDocument + Flow 实体
  （smoke `uidoc=6` 机器证明，判据① 达成）。
- **档1 流程状态机（零引擎改动主体）**：`GameFlow.cs`（Menu/Spawning/Run/Paused/
  Results/Settings + 设置返回记忆 + Esc 边沿 + 重入守卫）+ `RunSweeper` 档② 清场
  批量系统（tag 命中集、SweepArmed/Observed 握手）。**重开 = 清场 + prefab 重挂**：
  Player/Director 自场景迁 prefab，脚本/表载/Start 与 WaveDirector 运行态随重挂
  自然归零——无手工复位清单。
- **引擎三小增量**：输入位 bit6 Pause（Input.h 注释/SDK 常量/编辑器 Esc|P 映射）；
  `TryGetElementBox` 扩 x/y 出参（按钮点击定位探针）；**`InstantiatePrefabAsset`
  Play 分支补 scripts[] 实例挂载**（见发现②——真缺口）。
- **smoke-template 随迁**：菜单点击前置 + 二死催命 + 结算/重开（旧句柄失效+新
  在场+击杀归零）+ Esc 注入暂停 + 设置翻转 + 回菜单清场；`uidoc 2→6`；saves 断言
  翻转（settings.sav 恒存在 + version/fx.text 解码）；催命块按 Health 锁玩家。
- **svr-test 接通**：四屏新文件（menu 命名避开用户 main.rml）+ theme + Player/
  Director.prefab（自 Main.scene 实体抽取）+ MainMenu.scene（4 UI + Flow，档1
  骨架落账执行）+ svr 版 GameFlow/RunSweeper（清场集 + FastMob/FlySword/
  ScatterBullet/"spawned"；ReturnToMenu 清 RtUi 行）+ PlayerBehaviour 死亡二段
  （首死 RtUi 对话保留/二死 ShowResults）+ Fx 设置门控；svr.* 诊断键落账保留
  Slot 档。

## 实现期发现（全记录见批文件§实现期发现，择要）

1. **EnTT 视图逆序 → 装载 Show 序颠倒**：HUD（首声明实体）逆序下最后 Show =
   置顶，实底菜单被压、点击被 HUD body 吃掉（事件 `doc=hud.rml ev=` 空 = 铁证）。
   修 = GameFlow.Start 显式 re-Show（D1 最近 Show 序；显隐归 C# 铁律）。
2. **prefab scripts[] Play 态实例挂载缺口**：LoadEntityTree 只落槽不建实例（运行时
   spawn 的 prefab 均无脚本 = 路径从未走过）。症状链：HUD 不写/宝石不掉/卡片不弹。
   修 = InstantiatePrefabAsset Play 分支树遍历 ResolveSlotBehaviour（Unity
   Instantiate 重放 behaviour 等价路径；Player.prefab = 首个消费者）。
3. smoke-guid 基线第 7 根改 Player.prefab；基线 script-spawn 模板模式豁免；
   TweenDemo 缺 using System（今晨遗留）解堵。

## 验证实测

- smoke-template ×2 全绿**逐位一致**（kills=180 layer=90/0/93；flow 九位 YES；
  uidoc=6；settings=YES）。
- 回归 `tools/editor-regression.sh full` **16/16 PASS**（drag 首轮抖动复跑绿，
  T1 先例；smoke-guid 修复后过）。
- bench-survivor **fps=78 PASS**（alive=10436、fx 饱和）——RunSweeper 常驻开销
  零降级（与 M6a 批①/③d-1 台账持平）。
- svr-test 无头：MainMenu.scene 进 Play 5 实体、装载 4/0、errors=0、roundtrip
  byte-exact、settings.sav 落盘（version/fx 键）。
- 金回放零影响口径核对（模板无档/bit6 不动既有位/UI ops 不入哈希）。

## 真人验收余项（T10，不阻塞代码面勾销）

模板与 svr-test 各一轮全流程手测：菜单→开始→Esc 暂停→设置开关（飘字/血条即时
生效 + 跨局持久）→二死→结算→重开（场清干净/数值归零）→回主菜单→再开局。

## T10 后修①（同日首轮真人验收反馈）

- **现象**：「再战一局」后结算屏不消失（「回主菜单」正常）。
- **根因**：EnterRun 的 spawn 完成分支只 `Hide(MainDoc)`——只覆盖首次从菜单
  开局的路径，从结算屏重开时 ResultsDoc 无人收。smoke restart 位漏「结算屏
  隐藏」断言 = 盲区（残屏盖新局而 smoke 绿）。
- **修**：EnterRun 入口屏即隐（MainDoc + ResultsDoc——点击即走，比原 spawn
  完成时隐更跟手）；smoke stage4 补 `!IsDocumentShown(ResultsDoc)`。
  **阴性验证过**（临时回退 Hide → `restart/start = NO => FAIL`）；复跑
  smoke ×2 全绿；模板/svr 双侧同步修（generator + 再生成 + svr 直改）；
  干净环境回归 full **16/16 PASS**（并发轮 final 失败 = 负载抖动，单步复跑
  minFps=59 全绿 + 干净全量双证，T1 先例口径）。

## 落账（后修后状态）

[批文件](../Plans/M6b/2026-09-30-b3d2-four-screens-flow.md)（实现期发现 ⑨）、
[M6b.md](../Plans/M6b/M6b.md) 批次表、[AGENTS.md](../../AGENTS.md) 批③段。

## 落账

[批文件](../Plans/M6b/2026-09-30-b3d2-four-screens-flow.md)（Status/实现期发现/
验收判据）、[M6b.md](../Plans/M6b/M6b.md) 批次表、[AGENTS.md](../../AGENTS.md) 批③段。

## 关联

- 前批 [③d-1 样板批收口](./2026-09-29-m6b-b3d1-sample-screens.md)
- 下一批 ③e 图鉴/收集模板（波1 全量消费者）
