# 2026-09-30 · ③c 真人验收回执：事件回传手感 + 中文输入全过（附 UiEcho 注册漏项教训 + 光标观察遗留）

- 日期：2026-09-30
- 性质：手测轮回执——③c 判据 6 两件（GameView 内点击按钮的事件回传手感 + 起名框中文输入）在 `demo/svr-test` 新建测试场景实测通过。

## 测试载具（本日新建，用户项目侧零引擎/编辑器改动）

- `demo/svr-test/Scenes/UiTest.scene`：单实体 UI_Main，UIDocument 挂 `Assets/UI/main.rml`（showOnStart=1）——③c-①/② 的专用净屏（避开 Main.scene 游戏画面叠加）。
- `demo/svr-test/Game/UiEcho.cs`：UI 事件回显脚本（订阅 `UI.Events`，逐条打 `[ui-echo] Kind doc/key/ev/payload`；OnDestroy 退订防跨局双份——订阅表 play reset 不清）。

## 过程中的坑（教训登记）

**脚本类型必须 `GameMain.Configure` 显式注册**：首轮测试全无日志 + 红字「Play 装配：脚本类型未注册（跳过）'UiEcho'」。引擎不自动扫描 LemonBehaviour 子类（`Behaviours.Register<T>()` 为唯一注册口，Behaviours.cs 注记），加脚本漏注册 = 装配期静默跳过（仅一行红字）。已在 GameMain.cs 补 `Register<UiEcho>()`。给用户项目写新脚本时的固定动作：类文件 + 注册行成对。

## 验收结果（四观察全过）

| 观察项 | 结果 |
|---|---|
| 悬停按钮变色（鼠标路径 hover） | ✅ |
| 点「开始游戏」→ `[ui-echo] Click doc=… key='start'`（事件回传手感） | ✅ |
| 点输入框打中文 → 字上屏、候选窗贴光标（IME 链路） | ✅ |
| 输入过程 `[ui-echo] Change … payload='…'`（提交制文本输入值回传） | ✅ |

判据 6 成立（注：main.rml 按钮无 data-event 属性，验证面 = 文档级监听的全量 Click 路径——与 data-event 按钮同管线，仅 ev 语义名为空）。

## 遗留观察（判据外，轻微）

**输入框光标闪烁本轮未观察到**（前一轮未挂脚本时可见）。RmlUi 侧机制：caret = 0.7s 周期翻转（WidgetTextInput OnUpdate + 内部 Clock），渲染条件 = 可见+无选区+未禁用；引擎侧无脚本可影响之路。可能性：观察时点恰处灭相 / 会话窗口状态差异 / 真实小回归。不阻塞验收；复测一眼确认，若确实不亮再立案微查。

## 关联

- [M6b.md](../Plans/M6b/M6b.md)（③c 行勾销）、[批③c 文件](../Plans/M6b/2026-09-28-b3c-csharp-ui-api.md)（判据 6）
- 上午条目 [2026-09-30-acceptance-m5-uidoc-d1.md](./2026-09-30-acceptance-m5-uidoc-d1.md)（余项清单中 ③c 两件随本轮勾销）
