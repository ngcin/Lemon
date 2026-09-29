# M6a 批③c：C# API 与波1 机制——UI.Apply(ops) + UiEvent 队列 + 契约响亮失败 + M1/M2/M3/M6 资产源/M7（代码面收口）

- 日期：2026-09-28
- 批文件：[Plans/M6b/2026-09-28-b3c-csharp-ui-api.md](../Plans/M6b/2026-09-28-b3c-csharp-ui-api.md)；决策：[ADR-014](../ADR/ADR-014-Game-UI-RmlUi-Integration.md)（D1 ③c / D2 波1 契约 / D4 提交制）
- 性质：代码面完成（smoke-uirml 全链 OK + script-tests 1699 checks + 回归 full 15/15；真人验收余 GameView 手感 + 文本输入）

## 结论先行

波1 机制（M1/M2/M3/M6 资产源/M7 + 提交制文本输入）全链落地：**C# `UI.*` 入 staging → `UI.Apply()` 序列化（UTF-8 arena + 行块）→ `lemon_ui_ops_pull`（TickBatch 尾）→ UiHooks → `UiSubsystem::ApplyOps` 当帧可见**；事件反向 **文档监听器（click/change/submit/DocumentReloaded）→ UiEventC 队列 → `#16` 头 drain → `lemon_ui_events_dispatch` → `UI.Events` 订阅者**。零 vtable 尾加（两枚新 Entry 导出 + UiHooks 钩子对，EditorAssetHooks 先例形态）。

## 交付

| 件 | 内容 |
|---|---|
| `Engine/Ui/UiBridge.h` | 线格式契约：UiOpC 32B（8 op 型；文本串 NUL 终止入 arena）+ UiEventC 172B（doc/key/ev/payload 定长字段 + 世界坐标位）+ 行块格式 + 容量常量（256 op/64KB/帧） |
| `UiSubsystem` 机制面 | `ApplyOps`（M2 全 op 型）/ `DrainEvents` / `ContractErrorCount` / 模板克隆引擎（`data-template` 容器 + `<ui-template data-name>` 原型 → `Clone()` + class `{{field}}` 插值 + `data-field` 填充 + img GUID→`guid:` 协议 + 稳定 key 寻址表 `容器id/条目key`）/ 响亮失败（LEMON_ERROR + "has:" 提示 + 计数）/ 文档级事件监听 → UiEventC / `WantsKeyboard`（文本焦点）+ `AnyModalShown`（模态让出） |
| `Engine/Ui/SdlTextInputHandler` | IME 预编辑 handler（spike T7 实证逻辑的引擎落位）+ `LemonSystemInterface::ActivateKeyboard/DeactivateKeyboard`（SDL_SetTextInputArea/Start/Stop + 画布→窗口点换算 `SetImeRectTransform`） |
| `ScriptHost` 桥 | `UiHooks{applyOps, drainEvents}` 进程钩子（EditorAssetHooks 先例；未装 = ops 丢弃 warn-once）+ `lemon_ui_ops_pull/events_dispatch` 两导出解析（旧 Entry 缺 = 挂空）+ TickBatch 尾拉取应用（当帧可见）+ DispatchEvents 头派发 |
| `Lemon.SDK/GameUI.cs` | `UI` 静态类（与 RtUi 兼容层 `Ui` 并存，D5）：Show/Hide(modal)/SetText/SetAttr/SetClass/SetStyle/SetInnerRml/SetItems(UiItem{Key,Fields}) + `Apply()`（staging→ready 序列化，arena 缓冲几何增长）+ `UI.Events.Subscribe`（静态表 GC 纪律）+ Reset/PlayReset 域复位挂钩 |
| EditorApp M7 | UiHooks 装配 + `FeedGameUiInput()`（画布矩形内鼠标位/点击边沿/键盘差分（ImGuiKey→UiKey 47 键表）/IME 锚点换算）+ InputState 让出门（`uiHoldsInput = WantsKeyboard ‖ AnyModalShown`）+ ImGuiBackend `SetSdlEventTap`（TEXT_INPUT/EDITING 旁听，Play+GameView 聚焦转发）+ 画布矩形上报（`IsItemHovered` 门）+ resolver `guid:` 分支 |
| 验收资产 | TestScript UiProbeBehaviour（typeId 17）+ GameMain 静态订阅（Click 计数 / DocumentReloaded→UiRefill = M2 重灌契约活样例）+ tests/script `TestUiSdk`（ops 4 条字节对拍 + 行块解码闭合 + 事件反向 c1r0）+ `--smoke-uirml --script` 全链（合成点击经 ImGui 注入 → RmlUi → 事件 → C# 回执 → RtUi uiev 终帧捕获）+ 回归第 15 步 uirml-chain |

## 实测数字

- `--script TestScript.dll --smoke-uirml --frames 240 --validate`：**`doc=1 font=Noto Sans SC panel=146119 titleG=713 bodyB=725 tex=9216 old=0/0 titleTop=706/713 items=2/1 ev=c1r2 contract=1/textOK => OK`** exit 0（③b 旧六位零回归）。
- script-tests：**1699 checks OK**（TestUiSdk 新增 ~30 断言；behaviours 清单计数 17→18）。
- 回归 `tools/editor-regression.sh` full：**15/15 PASS**（uirml-chain 新步纳入）。

## 实现期发现（详录见批文件"实现期发现"）

1. **`<template>` 标签被 RmlUi 原生占用**（XMLNodeHandlerTemplate = 其模板注入/datamodel 家族；无 `src` 时子元素漏进父容器）→ 原型载体改 **`<ui-template data-name>`** 自有标签，ADR-014 D2 文档约定注记同步。
2. 探针实体挂载时序（Init 期创建被场景装配块换掉 → 移到进 Play 分支）；uiev 回读时序（Play 世界 RtUi 退 Play 即毁 → 帧 238 捕获）；冒烟扫掠与点击注入帧冲突（60/61 让位）。
3. `Buffer.MemoryCopy` 未固定托管数组 = SIGABRT（NRE in Memmove）——`fixed` 钉住。
4. `WantCaptureMouse` 悬停任意 ImGui 窗口即真，不能当 UI 鼠标门——改面板内 `IsItemHovered`。

## 代码面 review 修复（2026-09-29，提交前一轮）

- **P1**：`lemon_ui_ops_pull`/`lemon_ui_events_dispatch` 补 try/catch（M4.6 导出纪律：UCO 未捕获托管异常 = coreclr abort）；pull 异常路径 `DiscardPending()` 把 -1 的"整批已丢弃"契约做实。**更正 review 结论**：`EstimateBytes` ×3 原本就安全——UTF-8 每 UTF-16 char 上限 3B（增补面 4B 字符 = 一对代理 = 2 char），未放宽到 ×4，改为把不变量写成注释。
- **P2×2**：画布/悬停/焦点改"消费后失效"（`gameUi_->Update()` 后清零，帧尾面板重新上报——GameView 关闭后残值不再喂鼠标/键盘/IME；帧序：上报在 BuildUI、消费在下一帧头部 = 一帧固有延迟不变）；`SetImeRectTransform` 调用点守卫 0/0=NaN（无效画布 → 恒等中性 (0,0,1,1)）。
- **顺手**：GameUI 空串偏移别名修复（恒 `PutStr`——原捷径让空 B/C/D 偏移指向后续串/上批残字节，引擎可读出脏文本）；UiBridge.h UiEventC 注释与实现对齐（截断/溢出 = 静默防洪水，非响亮计数）。
- 复跑：smoke-uirml 同串 OK（`items=2/1 ev=c1r2 contract=1/textOK`）+ script-tests 1699 + 回归 full 15/15。

## 遗余

- 真人验收两件（余用户）：GameView 点击 data-event 按钮的事件回传手感；起名框中文输入（候选窗贴光标——T7 机制的编辑器形态，M7 键盘差分 + SDL tap 已接）。
- hover 事件留位不派发（M5 波2 tooltip）；SetItems 溢出策略 = 整批丢弃红字（容量 256/64KB 观察位）；submit = form 原生 + input 回车（T7 语义）双口径。
- ③d 模板迁移（卡片/HUD/主菜单/暂停/设置/结算转 .rml + L2 皮 + 流程状态机档1）为下一子批；RtUiCards 兼容层维持（D5，M8 前定去留）。
