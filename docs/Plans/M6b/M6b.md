# M6b 实施计划 —— 游戏UI产品壳（RmlUi 屏幕层，2026-09-29 自 M6a 批③ 独立）

Status: in-progress（③a–③c done；③d 前置 planned；③d/③e 待开）

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
| ③d 前置 | [2026-09-29-b3d-pre-uidocument-scene-mount](./2026-09-29-b3d-pre-uidocument-scene-mount.md) | UIDocument 场景挂载：Unity UIDocument 同构粒度（一组件一 .rml）+ 进 Play 自动装载双通道 + EnterPlay 归位 | **planned**（设计定案冻结 2026-09-29，待开工——③d 六屏迁移的公共地基） |
| ③d | 开批新建 | 模板迁移：卡片/死亡对话/HUD/主菜单/暂停/设置/结算 → `.rml` + L2 最小默认皮 + smoke 随迁 | 待开（③d 前置收口后） |
| ③e | 开批新建 | 图鉴/收集模板：波1 全量消费者（纸面验证 ①） | 待开 |

批次顺序理由：③d 前置先行（装载/显隐管理是六屏的公共地基）；③d → ③e 依序（图鉴消费全套机制，是波1 的规模化验收）。

## 出口判据（08 §2 M6b 行）

1. vs-survivor 模板六屏（卡片/死亡对话/HUD/主菜单/暂停/设置/结算）全走 `.rml` 文档 + UIDocument 挂载；RtUiCards 兼容层去留在 M8 前定案（ADR-014 D5）；
2. L2 最小默认皮落地（纯 `.rml/.rcss` 资产，换肤 = 改主题 token 不写代码）；
3. 流程状态机档1 落地（单场景零引擎改动：主菜单/暂停/设置/结算→重开/回菜单）并接通 svr-test 全流程（与 M6a 出口判据① 合流）；
4. 图鉴屏（100~500 条 SetItems）实测 ADR-008 预算内（>3ms 再启虚拟化评估）；
5. smoke 全链绿 + 回归 full（uirml-chain 断言随迁移升级）；
6. LoadScene 档2 评估结论落 ADR（档1 不够用再开工）。

## 登记项（观察，不扩 scope）

- 手柄输入：Steam 发布目标确认 → 追加最小 Gamepad 输入位（自 M6a 登记项随线迁入）；
- C# 动态屏寻址 = relPath + "UI 资产不挪"约定；v2 后手 = 组件查询 / GUID 重载（[③d 前置设计定案 §4](./2026-09-29-b3d-pre-uidocument-scene-mount.md)）。

## 关联

- [ADR-014](../../ADR/ADR-014-Game-UI-RmlUi-Integration.md)（L1 机制契约 M1–M8 + 三波排期）
- [08-Development-Roadmap](../../EngineDesign/08-Development-Roadmap.md) §2 M6b 行 + 文首重排注记
- [M6a](../M6a/M6a.md)（并行游戏线与玩法/内容生产线；批③ 迁出注记）
- 纸面验证包 [Reports/2026-09-28-game-ui-l1-paper-validation.md](../../Reports/2026-09-28-game-ui-l1-paper-validation.md)（四屏样例 = ③c–③e 验收底稿）
