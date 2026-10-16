# 2026-10-16 M8 批⑩ review 修复 —— 三缺陷 + 防回归位（B1/B2/B3/B4/B15）

## 输入

批⑩ 两提交（`123e240` ⑩a + `f659fc9` ⑩b）落账后，同日对全批 diff 走 code
review（报告 [2026-10-16-anim-graph-editor-review.md](../Reports/2026-10-16-anim-graph-editor-review.md)）。
机器面（序列化 roundtrip/自动布局/撤销栈/键仲裁）无功能错；发现 3 个真人面
缺陷 + 1 个输入纪律缺口（B1–B4）+ 1 个文案项（B15）。用户拍板：修 B1–B4
（B15 顺带）+ 附防回归机器面，以新提交落收；B5–B7 与 P3 档（B8–B15）登记不修。

## 修复

- **B1（P1）把手单击误建自环**：右缘把手按下即起建边会话，原地松开时命中源
  节点自身 → `AddTransitionByIndex(from,from)` 落自环边。修 = 会话起点记
  `edgeDragStartX_/Y_`，松手位移 < 6px（36.0f²，SceneView 拖拽同款阈值）→
  取消会话不落边。
- **B2（P2）Play 态相机被只读域冻结**：`BeginDisabled(ro)` 在 OnGui 开头起、
  末尾才收 → 画布（观察行为）落在 disabled 域，ImGui 拒绝 hovered/ActiveId，
  Play 态滚轮/F/中键平移全灭。修 = 禁用域收束到顶栏/表单；画布保持启用
  （编辑类操作各自有 `!ro` 门禁收账）；检查器两个布局分支在 ro 时自裹
  `BeginDisabled/EndDisabled`。
- **B3（P2）保存后回读副作用**：`TrySave` 落盘 → `RescanAssets` → hash 变 →
  下一帧 `EnsureLoaded` 缓存键失效触发 `LoadFrom` → 清除选择/撤销栈/画布相机，
  用户保存一次即"被重置"。修 = 落盘后 `loadedHash_` 直接同步新档 hash
  （落盘内容 = `edit_` 序列化，roundtrip 已证等值）；换档/外部改动路径照旧重读。
- **B4 相机键缺 WantTextInput 守卫**：滚轮/F/空格平移在输入框聚焦（改名 popup
  等）时仍抢键。修 = `const bool textFocus = io.WantTextInput;` 武装三处。
- **B15 文案**：Play 态画布提示共用编辑态文案（"拖把手连线"）误导——新 key
  `animctrl.canvas.hint_play`（zh/en：「Play 只读 · 中键拖动平移 · 滚轮缩放 ·
  F 适配视图」）；ro 时切换。zh/en 各 182 key，无缺无余。
- 顺手：B10（⑩b 头注释「与「+ 状态」按钮同入口」→「与画布右键「新建状态」
  同入口」——「+ 状态」按钮本批已删）。

## 防回归位（smoke 时序，帧预算 132 → 134）

三缺陷各配一条注入断言 + **变异验证**（反向改动后断言必须红，证明非空洞）：

- **B1 位**（编辑态）：f128 把手坐标注入按下 → f129 非空断言会话在场（防整链
  未命中假绿）+ 原坐标注入松开 → f130 断言边数不变且会话归 -1。
  变异验证：去掉 6px 阈值 → `count 4→5`（旧自环复现）→ click=NO FAIL ✓。
- **B2 位**（Play 态）：f130 进 Play（ro）→ f131 画布中心滚轮注入 → f133 断言
  `CanvasZoomForTest()` 变化。变异验证：画布退回 disabled 域（旧结构）→
  zoom=NO + 「相机被只读域冻结」FAIL ✓。
- **B3 位**（f34/f35 链）：f34 保存前 `SelectStateForTest(0)` 记选中 → 保存后
  即断言保留；f35 `EnsureLoaded` 后断言仍保留（旧行为此处 LoadFrom 清选择）。
  变异验证：去掉 `loadedHash_` 同步 → gate=NO + 新文案 FAIL ✓。
- 时序纪律：注入经 ImGui 事件 trickle（`imgui.cpp:11302`：pos 先到则同帧
  wheel 顺延一帧）——注入帧与生效帧差一、断言再推一帧；f125 曾设"先 Play"被
  证伪（编辑态门禁 `!ro` 使 B1 链假绿），改 Edit→Play 分段。
- 新测试钩子：`CanvasZoomForTest` / `EdgeDragFromForTest` / `SelStateForTest`
  （头内联）+ `StateHandlePosForTest`（.cpp，与 DrawCanvas handlePos 同式）。
- `tools/editor-regression.sh` 两处 `--smoke-anim --frames 134`。

## 机器面证据（2026-10-16）

- 构建零警告；单测 **34,959** 持平（改动零 Engine 面）；ctest 4/4。
- `--smoke-anim` zh+en `errors=0`：`ctrl(open load save gate layout edge
  ro=YES zoom=YES click=YES) => OK`（帧预算 134）。
- `--smoke-template`（共享 `BuiltInPanels.h`）：`=> OK`（flow/replay/saves 全
  YES 三行）。
- 三变异验证如上（B1/B2/B3 各反向必红）。
- **金回放零重录**：改动 = Editor/* + Strings + tools 脚本，零 Engine 运行时
  文件——证据沿用批⑩（三档 mismatches=0）。

## 遗留与登记

- B5–B7（待点模式出路/删末态拦截/lintDirty_ 收窄）与 P3 档（B8–B15 余项、
  含 i18n 死 key 复查）登记候选池，不修（批文件 §7 同款）。
- 真人 15 分钟观感过目照旧归批⑫ 组合验收。
