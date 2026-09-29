# M6b 实施计划 —— 游戏UI产品壳（RmlUi 屏幕层，2026-09-29 自 M6a 批③ 独立）

Status: in-progress（③a–③c done；**③c-2 同值去重微批 done 2026-09-29**（T8 后续卫生批：SDK staging setter 级等值早退 + 失效五处 + script-tests ③④ 段真阴性，回归 full 16/16）；③d 前置 done 代码面 2026-09-29（真人验收编辑器侧已过用户口述）；**③d-1 done 代码面 2026-09-29**（smoke-template ×2 全绿 + smoke-uirml 双模式 dp 位全绿 + 回归 full 15/15 首跑过；真人验收两屏视觉走查/换肤演示余用户；当日 T8 后修双联 5f9566b——display 显式化 + 进度条原生 progress 化）；③d-2/③e 待开）

> 2026-09-29 重排：UI 线自 M6a 批③ 整体迁入本里程碑（动因：M6a 承载过多——玩法/内容生产/UI 三线并进观感混乱；UI 已长成独立一条线。用户拍板）。**子批编号沿用 ③a–③e 不重编**（文件名与提交史实保留，"b3" 文件前缀 = 原 M6a 批③ 史实）。原 M6b 音频 → M6c、原 M6c Tilemap+TD → M6d（映射见 [08 文首重排注记](../../EngineDesign/08-Development-Roadmap.md)）。里程碑总览页：每批一个文件，开工前分解到文件/行级，完工后批文件内勾销；事件流水与实测数字记 [DevLog](../../DevLog/)。

## 并行依赖

- 消费者 = 用户幸存者游戏（`demo/svr-test`，M6a 并行游戏线延续）+ vs-survivor 模板（③d 迁移对象）。
- M6a 出口判据①（主菜单 → 一局 → 死亡结算 → 重开/回菜单）的 **UI 屏部分由本里程碑承接**。

## 批次文件

| 批 | 文件 | 主题 | 状态 |
|---|---|---|---|
| ③a | [2026-09-28-b3a-rmlui-renderer](./2026-09-28-b3a-rmlui-renderer.md) | RmlUi 渲染地基：`Engine/Ui` + RenderInterface over RHI + gameRT 叠画 + T7 文本输入微 spike | **done**（2026-09-28，M6a 期内；纵向翻转热修 + 上半幅位置断言机器化） |
| ③b | [2026-09-28-b3b-ui-font-asset-channel](./2026-09-28-b3b-ui-font-asset-channel.md) | 字体与资产通道：Noto Sans SC + `.rml/.rcss` 正式资产 + 贴图桥 + 双路热重载 | **done**（2026-09-28，M6a 期内） |
| ③c | [2026-09-28-b3c-csharp-ui-api](./2026-09-28-b3c-csharp-ui-api.md) | C# API 与波1 机制：`UI.Apply(ops)` + UiEvent 队列 + 契约响亮失败 + M6 资产源 + M7 输入路由 | **done**（2026-09-28，M6a 期内；smoke-uirml 全链 `items=2/1 ev=c1r2 contract=1/textOK` + script-tests 1699 + 回归 15/15 + bench fps=73 + replay 零重录；提交 13d4871；真人验收余 GameView 手感/文本输入两件） |
| ③d 前置 | [2026-09-29-b3d-pre-uidocument-scene-mount](./2026-09-29-b3d-pre-uidocument-scene-mount.md) | UIDocument 场景挂载：Unity UIDocument 同构粒度（一组件一 .rml）+ 进 Play 自动装载双通道 + EnterPlay 归位 | **done 代码面**（2026-09-29 T1–T6；smoke-uirml 双模式（脚本/无脚本）双通道/层序/stale/零装载全断言 OK + 回归 15/15；实现期发现 = 热重载隐式提层 → showSeq 复排，见批文件；同日三轮删除残留热修（资产墓碑/事件吞噬/删实体形态 D）——末轮 = R10 来源标记启用 + StopPlay 清场 + Reset origin 判据，[DevLog](../../DevLog/2026-09-29-uidoc-entity-delete-exit-clear.md)；真人验收三件余用户——Hierarchy 建 UIDocument 挂 .rml 进出 Play / 删资产红字 / svr-test 纯 C# 动态屏） |
| ③c-2 | [2026-09-29-b3c2-ui-ops-staging-dedup](./2026-09-29-b3c2-ui-ops-staging-dedup.md) | T8 后续卫生批：staging 同值去重（幂等五 op setter 级早退 + 失效五处） | **done**（2026-09-29；script-tests ③ 恰 2-op + ④ 重装载复位 6-op 真阴性；smoke-template 数值逐位一致 = 零漂移；回归 full 16/16；发现：事件驱动写下一帧可见 / 终帧三局 Play 会洗掉中途重装载断言——真阴须事件注入） |
| ③d-1 | [2026-09-29-b3d1-sample-screens](./2026-09-29-b3d1-sample-screens.md) | 样板批：主题 token 单源（`theme.rcss`，`--token`/`var()`）+ L2 组件库首件 + **dp 坐标系落地** + HUD/卡片两屏样板 + 层序断言 | **done 代码面**（2026-09-29；ratio 原生重排实证免兜底 / RmlUi 全屏元坑 = body 画布约定 / 合成点击改引擎直灌 / 催命保死亡链余量；smoke-template ×2 全绿 + uirml 双模式 + 回归 15/15；真人验收余视觉走查/换肤两件） |
| ③d-2 | 开批新建 | 铺量批：余四屏（主菜单/暂停/设置/结算）+ 档1 流程状态机（重开清场清单）+ smoke 随迁 + svr-test 全流程接通 | 待开（③d-1 收口后） |
| ③e | 开批新建 | 图鉴/收集模板：波1 全量消费者（纸面验证 ①） | 待开 |

批次顺序理由：③d 前置先行（装载/显隐管理是六屏的公共地基）；③d **两拆**（2026-09-29 拍板 D2）——③d-1 先以两屏样板验证 dp 坐标系、层序语义与 token 单源三个新约定，过了 ③d-2 再铺量（高风险项隔离在最小批）；③d → ③e 依序（图鉴消费全套机制，是波1 的规模化验收）。

## 出口判据（08 §2 M6b 行）

1. vs-survivor 模板**六文档**（卡片（死亡对话 = 卡片文档的单条形态）/HUD/主菜单/暂停/设置/结算——2026-09-29 审核修正口径：原「六屏列 7 项」名实不符）全走 `.rml` 文档 + UIDocument 挂载；RtUiCards 兼容层去留在 M8 前定案（ADR-014 D5）；
2. L2 最小默认皮落地（纯 `.rml/.rcss` 资产，换肤 = 改单一 `theme.rcss` 的主题 token（RCSS 自定义属性 `--token`/`var()`，RmlUi 6.3 原生支持）不写代码；**字号/间距全 dp 单位**——ctx `SetDensityIndependentPixelRatio(gameRT 高/参考高)`，窗口缩放时 UI 物理比例恒定，布局用百分比/flex）；
3. 流程状态机档1 落地（单场景零引擎改动：主菜单/暂停/设置/结算→重开/回菜单）并接通 svr-test 全流程（与 M6a 出口判据① 合流）。**重开 = C# 自律清场**（Destroy run 实体使 tween 自清 / 常驻实体逐个 `KillAll` / 各屏 `SetItems` 重灌 + `SetText` 复位清单随 ③d-2 批文件落账），session-reset op 登记后手不实现；svr-test 存档档位随批对齐（`vs.best` → meta 已改，`svr.*` 诊断键 → ③d-2 统一处置）；
4. 图鉴屏（100~500 条 SetItems）实测 ADR-008 预算内（>3ms 再启虚拟化评估）**+ 单帧 UI ops arena 实耗 < 64 KiB 断言**（500 条实耗 ≈ 50KB ≈ 77% 占用贴边——2026-09-29 审核余值；`EstimateBytes` ×3 为上界仅用于 C# 预扩容，勿当实耗读）；
5. smoke 全链绿 + 回归 full（uirml-chain 断言随迁移升级）；
6. LoadScene 档2 评估结论落 ADR（档1 不够用再开工）。

**范围边界（2026-09-29 审核落账 D3）**：本里程碑只交付**编辑器内**产品壳——出口判据不含可跑打包产物；runtime 侧 resolver 装配与打包线 UI 呈现归 M8（③d 前置通道 B 对未装 resolver 的裸运行时维持现行响亮失败是既定口径，非欠账）。

## 2026-09-29 开工前审核与拍板

第三方审核（14 条可验事实 13 条属实；A2「arena 撑爆」为误报——把估算上界当实耗）+ 用户三拍板，事件与证据见 [DevLog](../../DevLog/2026-09-29-m6b-pre-flight-audit.md)：

- **D1 层序 = 甲-轻量**：`ShowDocument` 与 ApplyOps 的 Show 分支在 `doc->Show()` 后显式 `PullToFront()`——层级序 = 最近 Show 序，装载序为初值。ADR-014 M1 注记随 ③d 前置 T6 落；RmlUi 原生 `ModalFlag` 仍不用（模态语义继续走引擎自有标记 + M7 让出门）。
- **B1 坐标系 = dp 比率**：`SetDensityIndependentPixelRatio(gameRT 高/参考高)` + L2 皮 dp 纪律，③d-1 落地；实现期验证 ratio 运行时变化是否触发全文档重排（不重排则 resize 时 `ReloadStyleSheets()` 兜底）。
- **D2 ③d 两拆**：③d-1 样板批 / ③d-2 铺量批（批次表已拆行）。
- 附带落账：`demo/svr-test/Scenes/MainMenu.scene`（空壳）保留并正名为档1 单场景骨架（③d-2 使用）；孤儿 meta `Assets/UI/main. rml.meta`（带空格）已删；`UiBridge.h` 图鉴容量注释改实耗口径。

## 登记项（观察，不扩 scope）

- 手柄输入：Steam 发布目标确认 → 追加最小 Gamepad 输入位（自 M6a 登记项随线迁入）；
- C# 动态屏寻址 = relPath + "UI 资产不挪"约定；v2 后手 = 组件查询 / GUID 重载（[③d 前置设计定案 §4](./2026-09-29-b3d-pre-uidocument-scene-mount.md)）。

## 关联

- [ADR-014](../../ADR/ADR-014-Game-UI-RmlUi-Integration.md)（L1 机制契约 M1–M8 + 三波排期）
- [08-Development-Roadmap](../../EngineDesign/08-Development-Roadmap.md) §2 M6b 行 + 文首重排注记
- [M6a](../M6a/M6a.md)（并行游戏线与玩法/内容生产线；批③ 迁出注记）
- 纸面验证包 [Reports/2026-09-28-game-ui-l1-paper-validation.md](../../Reports/2026-09-28-game-ui-l1-paper-validation.md)（四屏样例 = ③c–③e 验收底稿）
