# ADR-014：游戏 GUI 层正式接入 RmlUi——L1 机制契约（M1–M8）与三波实现

- 日期：2026-09-28
- 状态：已采纳（M6a 批③ 开工执行；ADR-008 D2 回退条件三项原文沿用，持续有效）
- 影响：[ADR-008](./ADR-008-Runtime-UI-Strategy.md)（D2 转正式执行、D3 重写——状态行已注记）、[08-Development-Roadmap](../EngineDesign/08-Development-Roadmap.md)（§2 M6a 批③ WBS 重写、§4 砍单 #4 注记、总表 M6a 行注记）、[06-Asset-Pipeline](../EngineDesign/06-Asset-Pipeline-Out-of-Box.md) §8（v1.x 触发注记）、`THIRD_PARTY.md`（RmlUi spike 期 → 正式，批③a 开工时落）、[M6a.md](../Plans/M6a/M6a.md) 批③ 行

## 背景

1. **触发条件成立**（06 §8 v1.x 行原文口径："需要复杂列表/富文本/本地化排版时升级"）：2026-09-28 用户真实游戏（`demo/svr-test`）需求清单 = 启动菜单、局外收集图鉴（物品/技能/武器）、卡片品级色/贴图背景/字体样式、角色起名与 NPC 改名（文本输入）。四品类全集（吸血鬼幸存者 / 经典塔防 / 守卫剑阁类英雄守图 / 鬼谷八荒类 ARPG）形态盘点收敛，见附录 A——**四品类共享同一套机制，仅组合密度不同**。
2. **现状三条不可达**：
   - 呈现层硬编码于编辑器 C++（`Editor/Panels/ViewportPanels.cpp:804-857`：卡片 = ImGui `Button(200×64)` + 定长 `char[48]` 槽），改样式/布局/字号 = 改引擎重编译——**M6a 验收①"全流程零 C++"在 ImGui 通道上不可达**；
   - 打包线零 UI 呈现者（HUD 唯一消费者 = 编辑器 GameView，M8 欠账）；
   - 运行时无 CJK 字体（`BitmapFont` 5×7 ASCII）、无九宫格/UI 图集/样式资产概念。
3. **ADR-008 结论复决**：D2 接入形态与回退条件**不变**（spike-04 三判据 2026-09-22 全过：像素级文档渲染 / 中文字体降级链零乱码 / 合成事件驱动回调可自动化）；D3 窄 API 清单对当前需求**不充分**——无 class 切换（品级色不可做）、无列表手段（图鉴不可做）、未定义错误语义（string id 静默失败风险）、未考虑热重载数据重灌。根因：D3 写于"静态 HUD + 固定按钮"语境，将两个命题捆在"不暴露 DOM"一句里——**"不跨边界暴露 RmlUi 概念/回调/树查询"依然成立**（保升级免疫与回退选项），**"不给结构动态性"不成立**（解药 = 模板克隆，结构仍在文档，非树操作）。
4. 前置就绪：M6a 批② 已收官（2026-09-28，含存档三档 slot/settings/meta）——settings/meta 档即设置菜单与图鉴的持久化底座；RmlUi datamodel 不依赖的结论不变（上游 #748 无 late binding + rbfx 数据模型桥腐化实证，独立于本 ADR 重新验证过）。

## 决策

### D1 RmlUi 正式接入，M6a 批③ 重定义为"RmlUi 地基 + 产品壳"

- 原"通用模态面板通道（Cards 泛化）"**退役**——该投资与 RmlUi 呈现正交且转轨即作废；**流程状态机（档1 单场景，零引擎改动）与 LoadScene 档2 评估保留不变**（与呈现无关）。
- 接入形态 = ADR-008 D2 原文：自研 `Rml::RenderInterface` over Lemon RHI（像素空间正交投影 + 每帧 Discard/Commit 共享动态 VB/IB + 真 scissor 去重 + 3 shader 变体 + RmlUi 6.3 CompileGeometry）+ `SystemInterface`；字体 = 随引擎/模板带 Noto Sans CJK（OFL）+ `LoadFontFace(fallback=true)`。**回退条件三项原文沿用**（适配超 2 周不收敛 / 10k 元素级文档 >3ms / macOS 路径绕开成本失控 → 退自研轻量保留模式 UI）。
- 批③ 拆五子批（开批新建 `Plans/M6a/` 批文件，分解到文件/行级）：
  - **③a 渲染地基**：`Engine/Ui` 模块 + RenderInterface + SystemInterface + GameView 接静态文档冒烟（复用 spike-04 验收壳：交换链回读 BMP + VERDICT 行 + validation 层）+ **文本输入微 spike 三判据**（中文提交零乱码 / IME 候选窗贴光标 / 编辑器 ImGui↔RmlUi 焦点仲裁不串）；
  - **③b 字体与资产通道**：Noto Sans CJK 注册 + `.rml/.rcss` 转正式资产类型（GUID/`.meta`/manifest + AssetBrowser 识别）+ 贴图引用桥（`LoadTexture` → AtlasRegistry/sprite GUID）+ 文档热重载；
  - **③c C# API 与波1 机制**：`UI.Apply(ops)` 单一提交口 + UiEvent 事件队列 + 契约校验（响亮失败）+ M1/M2/M3/M6（资产源）/M7 + 提交制文本输入；
  - **③d 模板迁移**：升级卡片/死亡对话/HUD/主菜单/暂停/设置/结算全部转文档 + L2 最小默认皮（见 D7）+ smoke-template 断言随迁；
  - **③e 图鉴/收集模板**：波1 机制全量消费者 + L1 零新增出口判据实证。

### D2 L1 机制契约 M1–M8（冻结；新增须矩阵加行 + ADR）

| # | 机制 | 契约要点 |
|---|---|---|
| M1 | 屏幕栈 | `Show/Hide` + 模态标记 + 层级序；**一屏 = 一文档**；tooltip / toast（横幅）层固定位 |
| M2 | 数据通道 | `SetText/SetAttr/SetClass/SetStyle/SetItems/SetInnerRml`；单一 `UI.Apply(ops)` 每帧批量提交；string id；**SetItems = 模板克隆**（文档内 `<template data-name>` 原型 + `data-field` 契约 + 稳定 key，字段 schema 只活在文档与 C# 两侧，引擎只认数量）；**响亮失败**（id/field 不匹配 → 控制台错误 + stats 计数 + smoke 断言"零契约错误"）；`DocumentReloaded` 事件 → C# 重灌数据（热重载后屏幕不空） |
| M3 | 事件通道 | `UiEvent{doc, key, type, payload}` 走事件队列（**无回调跨边界**）；click/change/submit/hover…；payload 预留字符串值与**世界坐标位**（M4 依赖，第一波就把事件结构定够宽） |
| M4 | 拖放 | DragStart / DragEnter / DragLeave / Drop / DragCancel；**边沿触发不逐帧**；落点 = 目标 key **或世界坐标**（摆塔用例）；**UI 载身份、游戏持语义**（拖什么/能否放/放下发生什么全在 C#）；拖拽视觉引擎托管（源元素快照跟随光标）；droppable 目标在文档声明（`data-droppable`），enter/leave 高亮 = RCSS 类引擎自动施加。**波3 实现、契约即冻结** |
| M5 | 悬停系统 | 引擎托管延迟/跟随/贴边翻转（做一次全品类受益）；内容 = 一次 M2 填充；对比查看 = 悬停面板 + 钉住的普通文档（无专门机制）。**波2 实现** |
| M6 | 纹理源绑定 | `img` 可绑三种源：资产 GUID（图标）/ 世界 RT（小地图、头像）/ **帧序驱动**（冷却扫描 = N 帧贴图按进度选帧；血球/充能/状态箭头同理——**美术能画的不需要机制**）。波1 落资产源，波2 补 RT 与帧序 |
| M7 | 焦点与输入路由 | 模态抓取 / 游戏让出（"菜单打开时脚本让出输入"规则化，M6a 批③ 原案此条继承）；编辑器内 ImGui↔RmlUi 按焦点分派（含文本输入事件归属）；手柄方向键合成焦点移动（登记项，Steam 目标确认后落） |
| M8 | 引擎元素注册表 | RmlUi 自定义元素尾加位（`Rml::Element` 派生注册，官方扩展点）；首件候选 `radial-progress`（帧驱扫描手感不足才做）；**ADR 门控** |

- **C# 侧形态与既有纪律同构**：ops 命令缓冲（SceneOps/ADR-004 先例）+ 事件队列（hostfxr 无回调纪律）+ vtable 尾加；**UI 状态不入 StateHash**（表现层，Tween/Fx 先例）；**UI 交互 = 用户 IO 不入输入快照**（RtUiCards 先例）——金回放零重录。
- **通用性三支柱**：封闭机制集（附录 A 矩阵全行纸面验证通过，[验证包](../Reports/2026-09-28-game-ui-l1-paper-validation.md)）+ L2 资产化组件库（纯 `.rml/.rcss`，换皮 = 改主题 token，不写代码）+ ADR 门控扩展。**机制之外的新 UI 需求默认答案 = 资产（RCSS/贴图/模板/组合），不是加机制**——这条默认答案是防蔓延的墙。
- 结构逃生舱预留不实现：`Create/Append/Remove` 作为 op 尾加位写进设计、v1 不做。触发信号（任一）：①两层都动态的列表套列表且嵌套模板表达不动；②脚本运行时决定 UI 树形状；③id 约定被绕着滥用（表达力到顶）。

### D3 分层与边界

| 层 | 内容 | 形态 | 改动成本 |
|---|---|---|---|
| L0 基座 | RmlUi + RenderInterface + 字体 | 引擎 C++ | 接入批一次 |
| L1 机制 | M1–M8 冻结契约 | 引擎 C++ + vtable | **ADR 门控** |
| L2 组件库 | 按钮/九宫格框/槽位格/品级色 token/tooltip 框/滚动列表/横幅/卡片模板 | **纯 `.rml/.rcss` 资产** | 游戏侧随便改 |
| L3 屏幕 | 各游戏界面 | 游戏项目文档 + C# 流程逻辑 | 纯游戏侧 |

模块落位 `Engine/Ui`；**RmlUi 类型不出头文件**（自有句柄，同 Vulkan 零泄漏纪律形状）；世界空间 HUD（血条/飘字）恒走 sprite 合批管线（06 §8 恒定原则不变，M6a 批① 已落地）；RmlUi 文档/事件成为编辑器 GameView 与打包 runtime 的**同一呈现者**——顺手消解 M8 打包 HUD 欠账。

### D4 文本输入分档（修订 v1 红线）

- v1 = **提交制**：SDL3 文本输入 API → `Context::ProcessTextInput` → RmlUi `<input>`（核心自带最小表单控件：光标/选中/maxlength）；起名/改名级够用；事件 = `UiEvent{type=change/submit, payload=值}`。
- 行内预编辑（preedit overlay，SDL text-editing 事件画在光标旁）与 IME 富编辑 = **后置打磨项**。
- RmlUi 表单控件质量三判据微 spike 在 ③a 验证；不过关 → M8 自定义元素兜底（成本自"白送"升"一个子批"的升级路径预先声明）。

### D5 存量通道处置

`RtUiChannel`/`RtUiCards` 短期保留（回归防线不破，`--smoke-template` 断言继续有效）；③d 模板迁移后降级为兼容层/数据绑定糖（C# `Ui.Set` 可继续写、引擎绑到文档元素）；**M8 前定去留**。

### D6 三波实现（设计一次冻结、实现随消费者）

| 波 | 时机 | 机制 | 验收消费者 |
|---|---|---|---|
| 1 | 批③a–③e（M6a 内） | M1 / M2 / M3 / M6（资产源）/ M7 + 提交制文本输入 | vs-survivor 模板 + svr-test 全屏幕 **L1 零新增** |
| 2 | M6c | M5 悬停、M6（RT + 帧序）、M8 视手感需要 | TD 模板（建造栏/塔详情/波次横幅/小地图）**L1 零新增** |
| 3 | 剑阁/ARPG 立项前 | M4 拖放、打字机对话（C# 定时切片驱动 SetText，不动机制层） | 物品栏/装备拖放/对话屏 **L1 零新增** |

出口判据统一口径：**各品类模板全屏幕零 L1 新增**；加了 = 矩阵漏行 → 补行 + ADR，不允许顺手改。

### D7 范围红线（修订版）

- **不做**：可视化 UI 设计器；UI 时间轴动画编辑器（RCSS transition 够用）；RmlUi datamodel 依赖（ADR-008 结论不变）；行内预编辑与 IME 富编辑（后置）；列表虚拟化（稳定 key 设计预留、实现后置——RmlUi 10k 元素 3ms 预算内品类屏幕 <1k 元素）；Play 中 UI 可视化 diff。
- **做**：L2 最小默认皮（九宫格框/按钮/槽位格/品级色 token/tooltip 框/横幅/卡片模板；yami-dungeon MIT 素材已在库 `Samples/Assets/yami-dungeon/`）；本地化维持中文单语，但 L2 文案约定走 key→表间接层（成本近零，留门）。

## 验收与回归资产

- ③a 复用 spike-04 验收壳（像素回读 + VERDICT + validation 层 + 文本输入三判据）；
- ③c 起新增 smoke 契约零错误断言（响亮失败的回归防线）；③d 起 smoke-template 断言随迁（卡片出现-选择-隐藏）；
- bench-survivor 不回归门 + UI 帧预算（10k 元素 3ms，ADR-008 口径）；
- 终验：M6a 验收① 在文档化 UI 上达成——**改样式/布局/文案 = 改游戏项目资产，引擎零改动可现场演示**。

## 后续动作（本 ADR 生效时完成）

- [x] 08 §2 M6a 批③ WBS 重写 + §4 砍单 #4 注记 + 总表 M6a 行注记
- [x] M6a.md 批③ 行更新
- [x] ADR-008 状态行修订注记
- [x] 06 §8 v1.x 触发注记
- [ ] `THIRD_PARTY.md` RmlUi spike 期 → 正式（③a 开工时落，FreeType 行同步核对）
- [ ] 07 移植矩阵 RmlUi 行状态同步（③a 开工时核对）
- [ ] 批③a 批文件开工新建（`Plans/M6a/YYYY-MM-DD-b3a-rmlui-renderer.md`，分解到文件/行级）

## 附录 A：品类 × UI 形态矩阵（十四行收敛为八机制）

| UI 形态 | VS | 塔防 | 剑阁守图 | 鬼谷类 ARPG | 机制 |
|---|---|---|---|---|---|
| 固定 HUD 槽位每帧刷新 | ✓ | ✓ | ✓ | ✓ | M2 |
| 屏幕切换 / 模态栈 | ✓ | ✓ | ✓ | ✓ | M1/M7 |
| 动态列表/网格 | ✓ | ✓ | ✓ | ✓ | M2(SetItems) |
| 品级/状态着色 | ✓ | ✓ | ✓ | ✓ | M2(SetClass) |
| 悬停 tooltip | ✓ | ✓ | ✓ | ✓ | M5 |
| 拖放（槽间/拖到世界） | — | ✓ | ✓ | ✓ | M4 |
| 径向冷却扫描 | ✓ | — | ✓ | ✓ | M6(帧序)/M8 |
| 富文本（彩色段+内嵌图标） | ✓ | — | ✓ | ✓ | M2(SetInnerRml) |
| 横幅/公告 | ✓ | ✓ | ✓ | ✓ | M1(toast 层) |
| 滚动长列表 | ✓ | — | — | ✓ | M2(RCSS 滚动) |
| tab 页签/嵌套面板 | — | — | ✓ | ✓ | M2(SetClass) |
| 世界画面进 UI（小地图/头像） | — | ✓ | ✓ | ✓ | M6(RT) |
| 对话/剧情（打字机+选项） | — | — | — | ✓ | M3 + C# 切片 |
| 手柄焦点导航 | Steam 目标全品类 | | | | M7 |

## 附录 B：L1 纸面验证包

[Reports/2026-09-28-game-ui-l1-paper-validation.md](../Reports/2026-09-28-game-ui-l1-paper-validation.md)：四屏幕（升级三选一卡片 / 局外图鉴 / TD 建造栏 / 剑阁式物品栏）的文档结构 + C# 调用序列 + 事件流推演，全部走通 = 契约闭合性证据；各屏应力点与登记项随附。
