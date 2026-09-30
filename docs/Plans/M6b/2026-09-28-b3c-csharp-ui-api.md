# M6a 批③c —— C# API 与波1 机制：UI.Apply(ops) + UiEvent 队列 + 契约响亮失败 + M1/M2/M3/M6 资产源/M7 + 提交制文本输入

Status: done（2026-09-28 代码面勾销：smoke-uirml 全链 OK + script-tests 1699 checks + 回归 full 15/15；真人验收判据 6 已过 2026-09-30，[DevLog](../../DevLog/2026-09-30-acceptance-b3c-input-events.md)；光标闪烁复测确认正常——前轮未观察到位系 0.7s 闪烁灭相时机，非回归）

> [ADR-014](../../ADR/ADR-014-Game-UI-RmlUi-Integration.md) 五子批第三件（D1 ③c / D2 契约 M1-M3+M6 资产源+M7 / D6 波1）。③a/③b 交付呈现与资产通道；本批把 C# 脚本接上：**单一 `UI.Apply(ops)` 提交口（M2）+ UiEvent 事件队列（M3，无回调跨边界）+ 屏幕栈（M1）+ GUID 贴图源（M6 波1）+ 输入路由与让出（M7）+ 提交制文本输入（D4，T7 已证 RmlUi 自带 `<input>` 过关）**。模板迁移（③d）与图鉴（③e）不在本批。
> 前置门槛 T7 文本输入微 spike ✅ 2026-09-28（三判据一次过，[DevLog](../../DevLog/2026-09-28-m6a-b3a-t7-text-input-spike.md)）。

## 现状盘点（本批动工面，2026-09-28 摸底）

| 事实 | 出处 | 对本批的含义 |
|---|---|---|
| vtable 尾位 = saveGetEx（`NativeApiVtable` 结束于 ScriptHost.h:108）；SceneOps 不走 vtable——Lemon.Entry 导出 `lemon_ops_pull` 经 `opsPullFn_` 拉取（ScriptHost.cpp:441），ApplyStructural 帧首应用 | ScriptHost.h:36-108,239 | **UI ops 同构：零 vtable 尾加**，新 Entry 导出 `lemon_ui_ops_pull`；应用时点 = TickBatch 尾（表现层当帧可见，先于 EditorApp 的 `gameUi_->Update()` :3707） |
| 事件派发先例：`eventsDispatchFn_`（"lemon_events_dispatch"）在 `DispatchEvents`（#16）；钩子先例：`EditorAssetHooks`/`ScriptIoHooks`（装配期注入，未注入 = 降级 no-op） | ScriptHost.cpp:430-432, ScriptHost.h:110-124 | UiEvent 结构（doc/key/ev/payload 字符串）装不进 `EventPacket`（48B 定长）——新导出 `lemon_ui_events_dispatch`，`#16` 头部先派发；编辑器→宿主经 `UiHooks`（applyOps + drainEvents 两函数）注入 |
| UiSubsystem：Pimpl 全在 UiSubsystem.cpp；`docs` map + `Doc{sourceText/sourcePath/doc/shown}`；Update() :297-304（ctx->Update）；事件监听/克隆引擎空白 | Engine/Ui/UiSubsystem.cpp:76-111 | ApplyOps 落点 = `Doc::doc->GetElementById` / 模板容器；克隆状态与事件队列挂 Impl；文档级监听器装在 Load 时 |
| RmlUi 事件冒泡到 document（AddEventListener on document 收 click/change/submit）；`<input>` 聚焦自动 `ActivateKeyboard(光标, 行高)`（T7 实证）；TextInputHandler 经 `Rml::CreateContext` 第 4 参注册 | T7 spike + RmlUi 6.3 源 | 事件监听 = 每 Doc 挂 document 级 listener → UiEventC 队列；引擎 SystemInterface（LemonSystemInterface）需补 ActivateKeyboard/DeactivateKeyboard（SDL_SetTextInputArea/Start/Stop）+ 自备 IME handler（spike `TextInputMethodEditor_SDL` 拷入 Engine/Ui） |
| 编辑器输入管线：键盘 = ImGui 轮询语义态（`gameViewFocused_ && !WantTextInput` 门，EditorApp.cpp:3672-3683）→ `InputState` → `ApplyInput`；SDL 事件全量经 `Window::PollEvents` observer（ImGuiBackend 独占 :151） | EditorApp.cpp:3665-3700, Window.cpp:50-92 | M7：键盘差分（ImGui key 态 → 边沿）转 RmlUi；鼠标 = GameView 画布内位（ViewportPanels.cpp:775-798 letterbox rect）轮询制；文本输入事件（SDL_TEXT_INPUT/EDITING）需 ImGuiBackend 加旁听 tap（单槽 observer 被其占用）；"游戏让出" = `!gameUi_->WantsKeyboard()` 并入 InputState 门 |
| smoke-uirml 夹具（SeedSmokeUiRmlProject :3024 / SeedSmokeUiDocument :3046 / 热重载 :3632 / VERDICT :5562 / Shutdown :5619）；回归 full 14 步 | EditorApp.cpp, tools/editor-regression.sh:55-97 | 冒烟扩契约断言（零契约错误 + 克隆渲染 + 事件回传）；回归插第 15 步（③a 批文件登记的节奏兑现） |
| TestScript 表尾 typeId 17 起（现 16）；script-tests = tests/script/main.cpp（TestTweenSdk 先例：C++ 宿主 + Custom user 码回报 + RtUi 回读） | TestScript.cs:48-50, tests/script/main.cpp | UiProbeBehaviour（typeId 17）+ TestUiSdk：线格式字节对拍（ops_pull 拉回断言）+ 事件反向（dispatch 直灌 → C# 订阅回执）——纯桥面验证不依赖 RmlUi |

## 设计定案（实现前冻结）

### 线格式（新 `Engine/Ui/UiBridge.h`——ScriptHost/EditorApp/UiSubsystem 三方共享；零 RmlUi/SDL 类型）

```cpp
enum class UiOpType : uint8_t { Show=0, Hide, SetText, SetAttr, SetClass, SetStyle, SetInnerRml, SetItems };
struct UiOpC {            // 32B；字符串一律 arena 偏移（字节），引擎零分配解码
    uint8_t type; uint8_t flags; uint8_t strCount; uint8_t reserved;
    uint32_t s0, s1, s2, s3;   // 依类型定序（见下）
    uint32_t i0, i1;           // SetItems: i0=行数；Show: flags bit0=modal
    uint64_t reserved2;
};
// 字符串定序：Show/Hide s0=doc；SetText s0=doc s1=key s2=text；SetAttr +s2=attr s3=value；
// SetClass s0=doc s1=key s2=class（flags bit0=加/删）；SetStyle s0=doc s1=key s2=prop s3=value；
// SetInnerRml s0=doc s1=key s2=rml；SetItems s0=doc s1=容器id s2=模板名 s3=行块
// 行块（arena 内，i0=行数）：每行 = u8 keyLen + key + u16 字段数 + Σ(u8 名长 + u16 值长 + 字节)
struct UiEventC {         // 152B 固定 blittable；UI 事件低频（点击级）
    uint8_t kind;         // UiEventKind: Click/Change/Submit/Hover(留位)/DocumentReloaded
    uint8_t modal; uint16_t reserved;
    char doc[32]; char key[48];   // 文档名（资产 relPath）/ 元素 id 或 容器/条目key
    char ev[16];                   // data-event 语义名（"pick"/"tab"/"build"…）
    char payload[64];              // change/submit = 控件值
    float wx, wy;                  // 世界坐标位（M4 预留，波1 恒 0）
};
```

### 关键寻址与克隆（M2）

- **key 解析双形**：① 文档内元素 id（`GetElementById`）；② `容器id/条目key` 路径（SetItems 克隆时引擎登记 `itemRoots` 映射，`SetClass("bt/arrow/root", …)` 即达克隆根）。
- **模板克隆**：容器（`data-template="card"`）→ `<template data-name="card">` 原型 → 每行 `Clone()`：class 属性内 `{{field}}` 插值替换 + `data-field` 元素填充（文本子元素 = SetText；img = src，GUID 16hex 值 → `"guid:<hex>"` 源，M6 波1）+ 条目根挂 `data-key`（事件回传身份）。稳定 key：克隆先清旧行（容器内非 template 子全删）再建——SetItems 全量语义。
- **响亮失败**：id/容器/模板不命中、field 不在模板字段集 → `LEMON_ERROR`（含"has: …"提示，纸面验证原案）+ `contractErrors_++` + smoke 断言零契约错误。模板字段集 = 首次克隆时扫描缓存（`data-field` 名 + class `{{}}` 占位名）。
- **DocumentReloaded**：③b 热重载路径（ReloadDocument/ReloadStyleSheets）后向事件队列注入该文档的 DocumentReloaded → C# 重灌（M2 契约；克隆 map 同步重建标记）。

### 数据流（帧时序）

```
脚本 tick（域线程）                EditorApp 主循环（帧）
  UI.* 入 staging              ──▶  （World 步进 = TickPlay 内含脚本 tick + #16）
  UI.Apply() → ready                 TickBatch 尾：lemon_ui_ops_pull → UiHooks.applyOps
                                       → EditorApp 侧即转 gameUi_->ApplyOps（当帧）
                                     gameUi_->Update()（RmlUi 事件在 ctx->Update 产生
                                       → document listener → UiEventC 队列）
                                     下一帧 #16 头：UiHooks.drainEvents
                                       → lemon_ui_events_dispatch → C# UI.Events 订阅者
```

- **UI 状态不入 StateHash、UI 交互不入输入快照**（ADR-014 D2 纪律）——基准场零调用零漂移；金回放零重录。
- ops 容量：ops 256 条 / arena 64KB 每帧；溢出 = 红字截断（一屏富 UI < 8KB 实测量级，留观察）。

### M7 波1 口径（编辑器形态）

- 鼠标：GameView 画布 letterbox 矩形内（ViewportPanels :794-798 已算 off/imgW/imgH——面板存画布 rect 给 app）→ 画布像素坐标 → `UiSubsystem::SetPointer(x, y, inside)` + 点击边沿（ImGui IsMouseClicked 且画布内）；指针在 UI 元素上（ctx->GetElementAtPoint 非空）时游戏鼠标语义让出（当前游戏输入无鼠标位——此项波1 自然满足，登记口径）。
- 键盘：`gameViewFocused_` 时 ImGui key 态差分 → `ProcessKey(rmlKey, down)`（ImGuiKey→RmlUi KI 小表 ~28 项：字母/数字/方向/退格/回车/空格/Home/End/Del）；`gameUi_->WantsKeyboard()`（ctx 有焦点文本控件）为真 → InputState 键盘门关闭（"菜单打开时脚本让出输入"规则化）。
- 文本输入：ImGuiBackend 加 `SetSdlEventTap`（旁听 TEXT_INPUT/TEXT_EDITING，不夺 ImGui 事件流）→ Play 且 GameView 聚焦且 RmlUi 有焦点控件时喂 `UiSubsystem::ProcessTextInput/TextEditing`（内建 IME handler，spike 同款）；候选窗贴光标 = SystemInterface::ActivateKeyboard → `SDL_SetTextInputArea`（T7 已证核心自动调，光标随移随发）。
- 模态（M1）：`Show(doc, modal=true)` → flags 记 `Doc::modal`；modal 文档 shown 时 `WantsKeyboard` 面扩大 + 屏蔽下方文档指针命中（RmlUi 文档 z 序天然支持——modal 文档铺满透明拦截层由文档 RCSS 自理，引擎只记标记供事件携带与让出判定）。层级序 = 文档 Show 顺序（RmlUi z 默认），v1 不做显式 z 参数。

## 任务分解

### T1 线格式 + UiSubsystem 机制面（Engine/Ui）

- 新 `Engine/Ui/UiBridge.h`：UiOpType/UiOpC/UiEventKind/UiEventC + static_assert（32B/152B）+ arena 行块编解码自由函数（Encode/DecodeItems——C++ 侧给冒烟/测试用；C# 侧独立实现，两侧字节一致 = 线格式契约）。
- `UiSubsystem.h/.cpp`：
  - `void ApplyOps(const UiOpC* ops, uint32_t n, const char* arena, uint32_t bytes)`；`uint32_t DrainEvents(UiEventC* dst, uint32_t cap)`；`uint32_t ContractErrorCount() const`；`bool WantsKeyboard() const`。
  - `Impl` 扩：`std::deque<UiEventC> events`；`Doc` 扩 `bool modal` + `std::unordered_map<std::string, Rml::Element*> itemRoots` + 模板字段集缓存；document 级监听器（click/change/submit——挂 `Rml::EventId` 三枚，Load/Reload 时装、Unload 时随文档销毁）。
  - 克隆引擎 + 响亮失败 + `guid:<hex>` 源合成（resolver 侧分支在 EditorApp）。
  - 热重载（ReloadDocument/ReloadStyleSheets/ReloadAll）成功后注入 DocumentReloaded 事件 + itemRoots 失效清理。

### T2 引擎输入面（Engine/Ui + 少量 Editor）

- `Engine/Ui/SdlTextInputHandler.h/.cpp`：`TextInputHandler_SDL`（Rml::TextInputHandler 派生，spike `RmlUi_Platform_SDL.cpp:524-569` 同款 HandleEdit）。
- `LemonSystemInterface` 扩 `ActivateKeyboard/DeactivateKeyboard`（SDL_SetTextInputArea/Start/Stop——窗口句柄经 Init 新参 `void* sdlWindow` 注入）+ `GetTextInputHandler` 供给 `Rml::CreateContext("game", …, handler)` 第 4 参（UiSubsystem.cpp:136 处）。
- `UiSubsystem` 增 `SetPointer(int x, int y, bool inside)` / `ProcessMouseButton(int btn, bool down)` / `ProcessKey(uint32_t rmlKey, bool down)` / `ProcessTextInput(const char* utf8)` / `ProcessTextEditing(const char* utf8, int start, int len)`——内部转 ctx 调用（坐标 = 画布像素；密度 = 1：ctx 尺寸即 RT 物理像素，③a 已定）。

### T3 桥面（Engine/Scripting）

- `ScriptHost.h`：`struct UiHooks { void(*applyOps)(const UiOpC*, uint32_t, const char*, uint32_t); uint32_t(*drainEvents)(UiEventC*, uint32_t); }` + `SetUiHooks`（EditorAssetHooks 旁，:110-124 区段）；成员 `uiOpsPullFn_`/`uiEventsDispatchFn_` + 拉取缓冲（UiOpC 256 + arena 64KB + UiEventC 64，复用型）。
- `ScriptHost.cpp`：导出解析两枚（:441 旁，旧 Entry 缺 = null 安全）；`TickBatch` 尾拉 ops → hooks（无 hooks 或无导出 = warn-once 丢弃，纯运行时降级同 EditorAssetHooks）；`DispatchEvents` 头 drain → dispatch。
- include `Ui/UiBridge.h`（Scripting→Ui 头依赖合法：同 Engine 内向下无环）。

### T4 SDK 面（dotnet）

- `Lemon.Entry/Exports.cs`：`lemon_ui_ops_pull(UiOp*, int, byte*, int)` / `lemon_ui_events_dispatch(UiEvent*, int)`（UnmanagedCallersOnly + try/catch 纪律，Interop struct 镜像 UiBridge.h）。
- `Lemon.SDK/UI.cs`：`public static class UI`（与既有 `Ui`（RtUi 兼容层，D5）并存）——`Show/Hide/SetText/SetAttr/SetClass/SetStyle/SetInnerRml/SetItems(container, template, IEnumerable<UiItem>)` 入 staging；`Apply()` 序列化（串 UTF8 → arena + 行块编码）→ ready 队列；`UiItem { Key; Fields }`；`UI.Event` struct + `UI.Events.Subscribe/Unsubscribe`（静态表，GC 纪律）；`Reset/PlayReset` 挂 DomainManager 既有调用点（Events.Reset 同位）。

### T5 EditorApp 接线（M7 + 装配）

- 装配（:2914-2930 段）：UiSubsystem Init 传 `window_->NativeHandle()`；`scripting::SetUiHooks({applyOps, drainEvents})`（applyOps = `gameUi_->ApplyOps` 直转；drainEvents = `gameUi_->DrainEvents` 直转）。
- `ViewportPanels.cpp` GameViewPanel（:794-798）：画布 rect（off + imgW/imgH + dpi）存 `app.SetGameViewCanvas(...)`。
- EditorApp Play 段（:3707 `gameUi_->Update()` 前）：`FeedGameUiInput()`——鼠标位/点击边沿/键盘差分（ImGuiKey→RmlUi 表）/让出门（:3673 条件并入 `!(gameUi_ && gameUi_->WantsKeyboard())`）。
- `ImGuiBackend`：`SetSdlEventTap(fn)`（EventThunk 内 TEXT_INPUT/TEXT_EDITING 旁听转发）；EditorApp 装配 tap → Play 且 RmlUi 聚焦时喂 gameUi_。
- `ResolveUiTexture`（③b 装配处）扩 `"guid:"` 前缀分支 → AssetDatabase FindByGuid → sprite → AtlasTexture。

### T6 验收资产

- TestScript.cs：`UiProbeBehaviour`（typeId 17，表尾注册）——帧1 正面链（Show modal/SetText/SetItems×2 行含 guid 图标字段/SetClass/Apply）→ Custom 码；帧2 UI.Events 订阅回执累计经 `Ui.Set("uiev", n)` RtUi 回读；自毁。
- tests/script/main.cpp `TestUiSdk`：①SDK staging→Apply→`lemon_ui_ops_pull` 拉回字节对拍（op 流/arena 串/行块解码）；②`lemon_ui_events_dispatch` 直灌 Click/Change 各一 → C# 订阅回执 Custom 码；③响亮失败不适用此层（引擎面）。
- `--smoke-uirml` 升级：夹具 .rml 扩模板容器（data-template/data-template 名/data-event 按钮/data-field label）+ 独立 ops 注入探针——①probe 直灌（EditorApp 侧构造 UiOpC：SetText/SetItems/错 field 反例）断言克隆像素（卡片行色块 >阈）+ `ContractErrorCount()==1`（反例恰好一错，正例零）；②`--script TestScript.dll` 全链（probe Apply → 引擎克隆 → 合成点击画布按钮 → UiEvent → 下一帧 C# 回执 → RtUi "uiev" 回读）；VERDICT 扩 `items/ev/contract` 位。
- `tools/editor-regression.sh`：full 段插第 15 步 `--smoke-uirml --frames 240 --validate`（grep "smoke-uirml: .* => OK"）。

## 验收判据（全过才勾销）

1. ✅ `cmake --preset mac && cmake --build --preset mac` 全绿（UiBridge.h 头 + SdlTextInputHandler.cpp 入 lemon-engine；SDK/Entry/TestScript 重编）。
2. ✅ script-tests：TestUiSdk 全过（ops 4 条字节对拍 + 行块解码闭合 + 事件反向 c1r0）+ 既有面 **1699 checks OK**（behaviours 清单 17→18 计数随注）。
3. ✅ `--script TestScript.dll --smoke-uirml --frames 240 --validate`：VERDICT **OK**
   （`items=2/1 ev=c1r2 contract=1/textOK`——克隆 2 行 + 负面容器 1 行；合成点击×1
   + DocumentReloaded 重灌×2 回读；契约错误恰 1 = 负面 op 直灌；③b 旧六位不回归）+ exit 0。
4. ✅ 回归 `tools/editor-regression.sh` full：**15/15 PASS**（uirml-chain 第 15 步新增）。
5. ✅ 基准面零漂移：bench-survivor **fps=73 PASS**（fx 饱和口径，≥55 门）+ bench-sim 录制/回放 `replay=PASS mismatches=0`；架构位 = UI 通道不入 StateHash/输入快照、基准场零调用（Tween/Table 先例同款证明）。
6. ✅ 真人验收（2026-09-30 过，[DevLog](../../DevLog/2026-09-30-acceptance-b3c-input-events.md)）：GameView 内点击按钮的事件回传手感（Click → C# 日志实证）+ 起名框打中文（字上屏、候选窗贴光标、Change 值回传）。

## 实现期发现（偏离批文件预设计的落账）

1. **RmlUi 原生 `<template>` 被占用**：`XMLNodeHandlerTemplate`（Factory.cpp:263）把
   `<template>` 当"内联模板注入"（需 `src` 属性指向 TemplateCache——正是 ADR-014 拒掉的
   datamodel 家族）；无 `src` 时**子元素直接漏进父容器**（原型根本不进 DOM，首跑
   "template not in container" 实锤）。修 = 原型载体改自有标签 **`<ui-template data-name>`**
   （未知标签 → 通用元素，子树完整保留、引擎 display:none 压制）——ADR-014 D2 M2 的
   文档约定注记同步（`<template data-name>` → `<ui-template data-name>`）。
2. **探针实体挂载时序**：Init 期创建的实体会被后续场景装配块（OpenScene/SeedSmoke 系）
   换掉——首跑 "进 Play 快照 0 实体"。修 = 挂载移到 smokeUirml 进 Play 分支内
   （场景定型后、EnterPlay 前）。
3. **uiev 回读时序**：C# 探针写的是 **Play 世界**的 RtUi 槽，退 Play 后 play world 即毁
   ——终帧 VERDICT 读编辑世界恒空。修 = 帧 238 在 Play 中捕获快照 `smokeUiEvText_`。
4. **冒烟扫掠与注入帧冲突**：`--smoke` 隐含的鼠标扫掠每帧 SetMouseOverride 覆写注入
   位置（点击落点漂走）。修 = smoke-uirml 帧 60/61（点击注入帧）扫掠让位。
5. **`Buffer.MemoryCopy` 必须钉住托管数组**：`GetArrayDataReference` 裸 ref 转 void* =
   未固定可移动对象——script-tests SIGABRT 实锤（NRE in Memmove）。修 = `fixed` 包裹。
6. **WantCaptureMouse 不能当 UI 鼠标门**：悬停任意 ImGui 窗口（含 Game 窗本身）即真——
   画布点击全被门掉。修 = 门 = 面板内 `ImGui::IsItemHovered()`（浮窗遮挡时为假），
   经 `SetGameViewCanvas(..., hovered)` 上报。
7. **（2026-09-29 review 修复）**：① 两枚 UI 导出补 try/catch（M4.6 纪律——UCO 未捕获
   异常 = coreclr abort；pull 异常路径 `DiscardPending()` 落实 -1 语义）；② 画布/悬停/
   焦点消费后失效（帧序 = 上报在 BuildUI、消费在翌帧头部；GameView 关闭后残值不再喂
   输入/IME）+ `SetImeRectTransform` 0/0=NaN 守卫；③ SDK 空串偏移别名（恒 PutStr 写独立
   NUL）；④ UiBridge.h UiEventC 截断/溢出注释与实现对齐（静默，非响亮计数）。复跑
   smoke-uirml 同串 OK + script-tests 1699 + 回归 15/15。

## 风险与既知边界

- **ops/arena 容量**：256 op/64KB 每帧，溢出红字截断；图鉴 500 条 ≈ 25KB（行块均 50B）——留 09 §6 台账观察行。
- **hover 事件**：enum 留位不派发（M5 波2 tooltip 归属）；submit = RmlUi form submit 或 input 回车（引擎映射 change/submit 双发口径：input 的 change（失焦/每次提交边界）+ 回车 Submit 语义——按 T7 实测定序）。
- **modal 拦截层**：v1 引擎只记标记（事件携带 + WantsKeyboard），全屏透明拦截 = 文档 RCSS 自理（L2 皮约定）。
- **打包 runtime（M8）**：UiHooks 在纯运行时无人装 = ops 丢弃 + warn-once（同 EditorAssetHooks 降级语义）。
- **旧 Entry 程序集**：无两新导出 = null 挂空（既定纪律，热重载旧域兼容）。

## 关联

- [ADR-014](../../ADR/ADR-014-Game-UI-RmlUi-Integration.md)（D1 ③c / D2 M1-M3,M6,M7 / D4 提交制 / D6 波1）
- 前批：[③a 渲染地基](./2026-09-28-b3a-rmlui-renderer.md)（T7 收尾 ✅）· [③b 字体资产](./2026-09-28-b3b-ui-font-asset-channel.md)
- 后续：③d 模板迁移 → ③e 图鉴
