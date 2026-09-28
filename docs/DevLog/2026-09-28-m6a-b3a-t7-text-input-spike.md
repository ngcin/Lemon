# M6a 批③a 收尾：T7 文本输入微 spike 三判据全过（③c 开工门槛清除，D4 定案）

- 日期：2026-09-28
- 批文件：[Plans/M6a/2026-09-28-b3a-rmlui-renderer.md](../Plans/M6a/2026-09-28-b3a-rmlui-renderer.md) T7 节；决策：[ADR-014](../ADR/ADR-014-Game-UI-RmlUi-Integration.md) D4（提交制文本输入分档）
- 性质：③a 批最后一件遗留收口；**③c（C# API 与波1 机制）开工门槛就此清除**

## 结论先行

RmlUi 6.3 自带 `<input type="text">` 在 SDL3 通路的三判据**一次过全绿**（`lemon-spike-rmlui` VERDICT `doc=OK font=OK click=3 text=OK(ime=OK route=OK) => PASS`，exit 0）。**ADR-014 D4 定案：v1 提交制文本输入直接吃核心控件，M8 自定义元素兜底不触发**（成本维持"白送"档）。

## 改动面（全部在 spike/04-rmlui，引擎/编辑器零波及——全量构建 no work to do）

| 件 | 内容 |
|---|---|
| `backends/RmlUi_Backend.h/.cpp` | spike 改造 **⑬ `GetWindow()`**（自定义 SystemInterface 构造需窗口）+ **⑭ `GetTextInputHandler()`**（暴露后端内部的 `TextInputMethodEditor_SDL`——事件循环的 `SDL_EVENT_TEXT_EDITING` 分支早就在喂 `HandleEdit`，但该 handler **从未注册给任何上下文**，等于死代码；经 `Rml::CreateContext` 第 4 参接活） |
| `data/test.rml` | `<input type="text" id="name" maxlength="12">` + label + 样式（:focus 高亮绿框，真人截图走查用）——对位 ADR-014 触发需求"角色起名" |
| `main.cpp` | `SpikeSystem` 改**继承 `SystemInterface_SDL`**（原裸 `Rml::SystemInterface` 的 `ActivateKeyboard/DeactivateKeyboard` 是 no-op，判据②通路根本没接上——继承后自动落 `SDL_SetTextInputArea`/`SDL_StartTextInput`/`SDL_StopTextInput`）；覆写 `ActivateKeyboard` 记录光标坐标/行高供断言；注入器三件 `PushTextInput`/`PushTextEditing`/`PushKey`；帧编排 150–310（注入在 frame N 帧尾、断言在 N+k 帧首——事件下轮 pump 同步落进 RmlUi 状态）；VERDICT 扩 text/ime/route 三位 |

## 三判据实测

| 判据 | 注入序列 | 断言结果 |
|---|---|---|
| ① 中文提交零乱码 | 真实 IME 事件序 `EDITING("柠檬",0,6)→EDITING("",0,0)→INPUT("柠檬")` + 纯 `INPUT("骑士")` | `GetValue()` 逐字节 == `柠檬骑士`（LengthUTF8=4）✅——乱码必字节不等，逐字节比对即机器化判乱码 |
| ② 候选窗贴光标 | 点输入框聚焦 | 聚焦即 `ActivateKeyboard`（RmlUi 核心在 `WidgetTextInput.cpp:1615` 自动调）；光标随打字推进 1052→1084→1116、行高 19；锚点 (1100,278.8) 落输入框 (1040,269) 344×39 内；失焦 `DeactivateKeyboard` 成对 ✅ |
| ③ 事件不串 | 裸 `KEY_DOWN(N)`（无 TEXT_INPUT）/ `BACKSPACE` / `RETURN`（平台层转 `ProcessTextInput('\n')`）/ `←→` / 点按钮 | KEY_DOWN 不插字 ✅ / `'\n'` 被单行 input 吞 ✅ / 方向键只移光标 ✅ / 退格恰删一字（`柠檬骑士`→`柠檬骑`）✅ / 失焦出 change ✅ |

## 对 ③c 的两条白送发现（省机制的实证）

1. **change 在每个提交边界派发**（每次文本落定 + 回车，非只 blur）——本次实测 change 计 4 次：IME 提交、纯提交、退格、RETURN 各一。M3 的 `UiEvent{change/submit}` 映射有现成语义底座；但 D4 是**提交制**语义，③c 映射时注意区分"逐键 change"（RmlUi 原生）与"提交 change/submit"（引擎侧约定——回车/失焦口径可直接采用）。
2. **光标每次移动都重发 `ActivateKeyboard`**——候选窗跟随光标是 RmlUi 核心自带行为，引擎（③c M7）无需自建"更新候选窗位置"机制，只要 SystemInterface 落 SDL 即可。

## 遗余

- 真人 IME 视觉确认（macOS 输入法开着打中文，候选窗实贴光标旁）余用户——机制面（锚点坐标正确性 + 跟随）已全部机器化，不阻塞 ③c。
- 编辑器内 ImGui↔RmlUi 焦点仲裁（判据③的编辑器形态）按 ADR-014 D2 M7 归 ③c，本 spike 只证 RmlUi 单侧通路。
- 行内预编辑视觉（preedit 花样下划线等）维持 D4 后置打磨项定位；`SDL_HINT_IME_IMPLEMENTED_UI="composition"` 后端已设。
