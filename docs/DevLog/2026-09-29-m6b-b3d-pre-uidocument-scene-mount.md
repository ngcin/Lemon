# M6b 批③d 前置收口 —— UIDocument 场景挂载：双通道装载 + EnterPlay 归位 + 层序定轨

- 日期：2026-09-29
- 批文件：[Plans/M6b/2026-09-29-b3d-pre-uidocument-scene-mount.md](../Plans/M6b/2026-09-29-b3d-pre-uidocument-scene-mount.md)（T1–T6）
- 提交：T1 `6e3bf78`（组件+镜像+Inspector 槽）/ T2 `ecae41f`（通道 B + stale + 提层）/ T3 `bf99f18`（通道 A + 归位 + modal 形参）/ T4 `4c932f6`（Hierarchy 创建流）/ T5+T6（smoke 断言面 + 文档，见 git log 当日尾两笔）

## 交付面

| 件 | 落点 |
|---|---|
| UIDocument 组件（id 30，16B 冻结） | `Engine/Components/UiComponents.h` + Catalog 注册 + SDK 镜像 + LayoutTables 探针；计数 30→31 四处同步 |
| 通道 A（声明式装载） | `EditorApp::MountSceneUiDocuments()`——EnterPlay 成功后扫 playWorld：FindByGuid（missing/非 Rml = 红字响亮）→ 去重（WARN）→ `LoadDocumentFromFile` + `ShowDocument(showOnStart, modal)` 声明态归位；挂载点全覆盖（TryEnterPlay/阻断重试模态/--play/--final/smoke-uirml/bench×2） |
| 通道 B（Show 落空兜底） | `UiSubsystem::SetDocumentResolver` + ApplyOps 的 Show op 未装载现载（同批后续 SetText/SetItems 可达）；EditorApp 装配 `ResolveUiDocument`（FindByPath→type==Rml→abs） |
| EnterPlay 归位（大扫除） | `ResetDynamicDocuments(declared)`：未声明且 stale（`Doc::shownDuringPlay`，仅 ApplyOps Show 置位）→ Hide + 清 stale（装载保留）；未声明且非 stale（Edit 双击装载）保持——③b 预览承诺延续，跨 Play 残留（R2）单点清场 |
| 层序（D1 甲-轻量 + 补全） | `ShowDocument` 尾加 `bool modal=false`（A4：modal 声明态写入口）；Show 后显式 `PullToFront()`；**showSeq 发号 + 重载尾部 RestoreDocumentOrder 复排**（见实现期发现①） |
| 编辑器三件 | Hierarchy「+ 创建/右键 → UI 文档」选择弹窗（取消 = 不建实体，结构轨 Undo）；Inspector `FieldHint::RmlRef` 槽（DrawGuidSlot 复用：下拉/拖入 kind 8/清空/未挂提示）+ showOnStart/modal 勾 |
| 冒烟断言面 | smoke-uirml 双模式升级（见下）；smoke-template `uidoc=0` 基准护栏位 |

## 实测数字（smoke-uirml --frames 240 --validate，双模式各一次）

```
无脚本：… items=-1/1 contract=1 uidoc(a=1/b=1/c=1 loads=4/1) layer(bTop=18409 aTop=0) p2(stale=1 keepC=1+8176px dyn=OK) => OK
脚 本：… items=2/1 ev=c1r4 contract=1/textOK … layer(bTop=18180 aTop=0) … => OK
```

- 双通道：a=1（场景声明装载，第二局增量恰 1 = 只重装声明文档）、b=1（dyn.rml 场景不声明，Show 落空兜底现载）、c=1（editprev Edit 双击装载）；
- 层序（帧中点两捕获，D1）：B 后 Show 在上 bTop=18409/18180 → 帧 175 重 Show A → aTop=0（被盖住）；
- 归位（帧 200 Stop / 203 重进）：stale=1（动态 Show 过的 dyn 第二局被 Hide）、keepC=1+8176px（Edit 双击装载跨 Play 保持可见）；
- script 模式 dyn=OK = C# 同批 Show+SetText 到刚兜底装载的文档（顺序契约）；
- ctest 3/3（组件计数 31 + 布局对拍；TestUiSdk 扩通道 B 2 条 op 字节对拍）；回归 full 15/15
  （首跑 13/15：TestUiSdk op 计数未同步 + smoke-drag 负载抖动（T1 先例），修后复跑全绿）。

## 实现期发现（三件，全修）

1. **文档热重载 = 从 context 根重挂 → 隐式提层**：D1「层级序 = 最近 Show 序」在 .rml 热重载后被装载序覆写（smoke 无脚本模式实抓：dyn Show 后 frame100 重载把 A 提到其上）。脚本模式原本靠 DocumentReloaded → C# 重灌的 Show 自愈，纯引擎文档会漂层。修 = `Doc::showSeq`（Show 发号/Hide 清零/装载归零）+ `ReloadDocument`/`ReloadAllDocuments` 尾部按序复排——不变量跨热重载成立。
2. **通道 B 兜底成功误计契约错误**：FindDoc 先响亮再兜底 → 每次成功兜底 contract +1（实测 2）。改 LookupDoc 先行、兜底仍失败才 ContractFail；FindDoc 无调用方删除。
3. **负面契约 op 与容器生命周期**：EnterPlay 重装主文档 = InvalidateContainers——160 时点（第一局）的负面容器活不到终帧；移 210（第二局内）恢复 contract 恰 1 / negN=1。uiev 捕获同理移 199（第二局 RtUi = 新世界，槽恒空）。

## 既知边界（沿批文件 §5 登记）

- 运行时动态加 UIDocument 不生效（装载钩只认 EnterPlay 扫描）；Play 期翻 showOnStart/modal 只影响下次进 Play。
- C# 无法感知 Show 是否成功（引擎侧响亮、C# 无回执——③c 既定口径）；`UI.IsShown` 只读查询按需再议。
- M8 前裸运行时未装 resolver：通道 B 维持响亮失败（既定口径，打包线接装）。

## 真人验收清单（余用户，不阻塞 ③d 开工）

①Hierarchy 建 UI Document → 挂 svr-test 的 .rml → 进 Play 见屏、退 Play 干净、再进 Play 初始态；②删 .rml 后进 Play 得红字（不空屏）；③svr-test 纯 C# `UI.Show` 动态屏不经场景声明可用。

## 关联

- [ADR-014](../ADR/ADR-014-Game-UI-RmlUi-Integration.md) M1（装载语义补章 + 层序 showSeq 复排注记，本批 T6 落）
- 下一批：③d-1 样板批（theme.rcss token 单源 + L2 首件 + dp 坐标系 + HUD/卡片两屏 + 层序断言规模化）
