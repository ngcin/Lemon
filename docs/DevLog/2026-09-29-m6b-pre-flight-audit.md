# M6b 开工前审核复核与三决策拍板（③d 前置冻结件修订）

- 日期：2026-09-29
- 性质：事件流水——用户提供第三方审查报告（4 硬缺陷 + 6 设计空洞 + 5 文档缺陷 + 3 待决策），本会话逐条对码复核（RmlUi 6.3 源码 + 引擎/编辑器/demo 代码 + M6b 全部批文件 + ADR-014 + 纸面验证包）后拍板并落修订。**不改引擎行为**（代码面仅注释/存档档位/资产卫生三件）。

## 复核结论

- **14 条可验事实 13 条属实**：A1（层级序零实现，绘制序 = 装载序 + 焦点副作用——`Context.cpp:293` 装载即挂 root、`:1239-1246` OnFocusChange 里 z-index auto 才 PullToFront、引擎两处 `doc->Show()` 均默认参故 RmlUi 原生 ModalFlag 恒 false、`AnyModalShown` 为引擎自有标记仅作键盘让出门）、A3（UIDocument sketch u64+u8+u8+u16 自然 sizeof=16，批文件误写 8B）、A4（`ShowDocument` 无 modal 形参，置 true 唯一路径 = ApplyOps C# Show op）、B1（全仓零 UI 缩放/参考分辨率，ctx 尺寸直跟 gameRT 物理像素）、B2（零文本编辑器、仓内 0 个 .rcss）、B3（token 机制未落纸；RmlUi 6.3 `--token`/`var()` 原生支持实证 = Samples/basic/variables + Element.cpp:632）、B4（SceneOps 无 reset op、Tweens 无 C# 全局清空——修正：FxChannel 自过期 0.8s/3s 走渲染帧 dt 不构成残留面）、B5（svr-test `vs.best` 落 slot_0 vs 模板同键走 Meta）、B6（`main. rml.meta` 孤儿 meta / MainMenu.scene 空壳 / smoke 为 temp 自播种）、C1（「六屏」列 7 项；纸面验证包⓪ 确证死亡对话 = 卡片文档单条形态）、C2（M6a Status: planned 失准）、C3（主树 29 处「M6a 批③」注释未随迁）、C4（T1 未写注册 id = 30）、C5（③d 前置 §3 归位与 ③b 双击「进 Play 后显示」承诺语义打架）。
- **A2 为误报**：报告把 `EstimateBytes` 的 ×3 上界估算（仅用于 C# arena 预扩容）当成 64 KiB 丢弃检查对象——上限检查的是实写字节 `s_arenaLen`（GameUI.cs:274），500 条实耗 ≈ 50.5KB < 64 KiB 不触发整批丢弃；「出口判据④预算算错」亦错位（④是 ADR-008 的 3ms 时间预算）。**余值采纳**：UiBridge.h「≈25KB」注释失准（实耗口径 ~50KB）、77% 占用贴边、③e 判据补实耗断言。
- 真正「③d/③e 白做」级风险 = **A1 + B1**（层序不可表达 / 七屏无稳定作者坐标系），非报告所标的 A1 + A2。

## 三决策（用户拍板）

| # | 决策 | 内容 |
|---|---|---|
| D1 | 层序 = 甲-轻量 | `ShowDocument` 与 ApplyOps Show 分支在 `doc->Show()` 后显式 `PullToFront()`——层级序 = 最近 Show 序，装载序为初值；ADR-014 M1 注记已落。RmlUi 原生 ModalFlag 不用（模态 = 引擎自有标记 + M7 让出） |
| B1 | 坐标系 = dp 比率 | ctx `SetDensityIndependentPixelRatio(gameRT 高/参考高)` + L2 皮字号/间距全 dp、布局百分比/flex；③d-1 落地，实现期验证 ratio 运行时变化的重排行为（不重排则 resize 时 `ReloadStyleSheets()` 兜底） |
| D2 | ③d 两拆 | ③d-1 样板批（token 单源 + L2 首件 + dp 落地 + HUD/卡片两屏 + 层序断言）/ ③d-2 铺量批（余四屏 + 档1 状态机 + smoke 随迁 + svr-test 接通） |

## 落账修订（本条目同工作单元，未提交）

1. ③d 前置批文件：文首加审核修订注记；T1 16B + 注册 id=30；T2 补 stale 置位 + Show 提层；T3 前置补 `ShowDocument(name, show, modal=false)` 形参（A4 写入口）；§3 归位改 stale 位口径（ApplyOps Show 置位、双击装载不置——③b 预期延续，无需 R10 来源标记）；T5 补 stale/层序断言。
2. M6b.md：批次表 ③d 拆两行；判据① 六文档口径（死亡对话 = 卡片单条形态）；判据② token 单源 + dp 纪律；判据③ 重开 = C# 自律清场 + session-reset op 登记后手；判据④ 补 arena 实耗断言；新增 D3 范围边界句（编辑器内产品壳，打包线归 M8）与决策注记段。
3. M6a.md：Status planned → 代码面收口口径。
4. ADR-014：M1 行落层级序实现语义注记（2026-09-29 拍板）。
5. 代码注释：主树 29 处「M6a 批③」→「M6b 批③」（21 文件，spike 史实不动——与 b3 文件前缀保留同口径）。
6. `UiBridge.h:69-72`：容量注释改实耗口径（500 条 ≈ 50KB 贴边 + ×3 上界仅预扩容说明 + ③e 断言）。
7. `demo/svr-test/PlayerBehaviour.cs:314/588`：`vs.best` 读写改 `Chan.Meta`（与模板 PlayerCombat 同键同档；v1 未发布不做 slot_0 迁移；`svr.*` 诊断键处置登记 ③d-2）。
8. 资产卫生：删孤儿 `demo/svr-test/Assets/UI/main. rml.meta`（带空格，指向不存在文件）；`MainMenu.scene` 空壳保留正名为档1 单场景骨架（③d-2 使用）。

## 下一步

开工 ③d 前置 T1–T6（按修订版批文件）；③d-1 批文件开批时把 dp 坐标系与 token 单源分解到文件/行级。

## 关联

- [M6b.md](../Plans/M6b/M6b.md)（决策注记段 + 拆批批次表）
- [③d 前置批文件](../Plans/M6b/2026-09-29-b3d-pre-uidocument-scene-mount.md)（审核修订注记 + §3/T1–T5 修订）
- [ADR-014](../ADR/ADR-014-Game-UI-RmlUi-Integration.md)（M1 层级序注记）
