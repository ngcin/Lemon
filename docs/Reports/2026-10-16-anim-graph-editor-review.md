# 2026-10-16 动画工作流面板可视化编辑（M8 批⑩）code review —— bug 清单

**范围**：批⑩a+⑩b 画布工作台全量——`Editor/Panels/AnimControllerPanel.cpp`（1372 行）、
`Engine/Assets/ControllerAsset.h/.cpp`（layout/editor 块 + 自动布局/网格量化）、
`Editor/SnapshotStack.h`（本地撤销栈）、`Editor/Panels/Panel.h` + `Editor/App/EditorAppChrome.cpp`
（键仲裁）、`Editor/Strings/{zh-CN,en}/anim.json`（15 新 key + 6 死键清理）、
`Editor/App/EditorAppSmoke.cpp` f36/f37/f126-f129、`tests/engine/EditorTests.cpp TestSnapshotStack`。
**约定**：只记录问题与定位，不动代码。行号以当前工作树为准。

**结论速览**：机器面（单测/冒烟/门禁）逻辑扎实，序列化/roundtrip/自动布局/撤销栈/键仲裁
未发现功能性错误。发现 **3 个真人面 bug（1×P1 交互 + 2×P2）**、4 个 P2 可用性/性能项、
若干 P3 打磨项。其中 P1（把手单击误建自环）与 P2-1（Play 态相机冻结）建议批⑫ 真人走查前修掉。

---

## P1 —— 交互 bug（建议必修）

### B1. 把手单击（不拖动）= 意外创建自环边

- **位置**：`AnimControllerPanel.cpp:970-986`（把手按下起步）+ `:1102-1107`（松手落边）。
- **链路**：拖线建边的"按住拖"模式（`edgeDragHeld_`）收尾只判 `!IsMouseDown(Left)`，
  **不要求发生过实际位移**。在把手 8px 激活半径内点一下直接松开：鼠标未动 →
  `hitNodeLi(gm)` 命中源节点自身 → `AddTransitionByIndex(自己, 自己)` → 一条带默认条件的
  自环边落库（PushUndo + 选中跳到边详情）。用户"想点节点"误触右缘中点就会中招。
- **对照**：本仓自家纪律 SceneViewPanel 拖拽有 4px 阈值（D5 防误触，BuiltInPanels.h:154），
  此处未套用。
- **为何机器面没拦**：smoke f37 走 API 级 `AddTransitionForTest`，手势链零覆盖（批文件 §6
  已自知"拖拽交互机器可测性差"）。
- **建议**：松手时若光标位移 < 阈值（如 6px）→ 取消会话不落边；顺带评估把手激活半径
  8px 与把手可视半径 5px 的错位（可视 5px、可点 8px，误触面更大）。

## P2 —— 功能性/可用性问题

### B2. Play 态画布相机全冻结（滚轮/F/平移全灭），Play 高亮功能自断一臂

- **位置**：`AnimControllerPanel.cpp:1271-1273`（`BeginDisabled` 包住面板体含画布）；
  画布相机三操作全部以 `hovered`/`IsItemActive` 为前提（`:952-966`）。
- **机理**：ImGui 1.92.9b 对 disabled 项 `IsItemHovered()` 恒 false（imgui.cpp:5088-5089），
  且 ItemAdd 直接拒绝 disabled 项持有 ActiveId（imgui.cpp:5195-5201，"Release active id
  if turning disabled"）。因此 Enter Play 后：滚轮缩放、F 适配、中键/空格平移**全部失效**。
- **后果**：批⑩ 头牌功能"Play 当前态高亮"看得见但视野锁死——初始 fit 恰好装得下全部节点
  时无感，节点出视野（用户 Play 前缩放过 / 图偏大）就看不到了。
- **定性**：相机是观察行为不是编辑行为。编辑类操作已各自有 `!ro` 门禁（点击/右键/拖动/
  双击），ro 的 `BeginDisabled` 本意是锁顶栏表单，误伤了画布观察面。
- **建议**：画布（或至少相机路径）移出 disabled 域；smoke 补一个"Play 态滚轮缩放后
  canvasZoom_ 变化"断言位。

### B3. 保存后重载链的三连副作用：撤销栈清空 + 画布相机跳回适配 + 选择丢失

- **位置**：`TrySave`（`AnimControllerPanel.cpp:257-267`）落盘 → `RescanAssets` → 内容
  hash 变（AssetEntry.hash = 内容 FNV-1a，AssetDatabase.h:46）→ 下一帧 OnGui 缓存键失配
  （`:1253`）→ `LoadFrom`（`:61-86`）：`undoStack_.Clear()` + `selState_/selEdge_ = -1` +
  `canvasFitPending_ = true`。
- **后果**（每次点"保存"都会发生）：
  1. **撤销历史整条清空**——"保存"变成 Ctrl+Z 的终点，且无任何提示（b10 新增代价；
     LoadFrom 注释"新目标 = 新会话"针对的是换档，被保存回读路径搭车）；
  2. **画布相机重置为 fit-all**——缩放到局部调条件 → 保存 → 视野跳走（b10 新增；
     b9 表单时代无此痛感）；
  3. 选择清空（b9 已有，画布化后高亮丢失更显眼）。
- **背景**："保存回读 = 重读"是 AnimationPanel 同款既有模式（DevLog f35 注释表明已知），
  且 f126/f127 已为"LoadFrom 清选择"做过 smoke 时序适配。但 1/2 两条是批⑩ 新引入的用户
  可感退化，建议定性：保存路径在 TrySave 成功后直接更新 `loadedHash_`（内容本来就是
  edit_ 序列化的，无需重读），或 LoadFrom 区分"保存回读"与"外部改动"（前者不清撤销/
  相机/选择）。

### B4. 画布相机键不受 WantTextInput 守卫：输入框打字时悬停画布会误触发

- **位置**：`AnimControllerPanel.cpp:959-960`——`if (hovered && ImGui::IsKeyPressed(ImGuiKey_F))`
  与 `spaceDown = ImGui::IsKeyDown(Space)`。
- **链路**：`IsKeyPressed/IsKeyDown` 不看 `WantTextInput`。在检查器改名框/参数名框输入时
  （焦点在输入框、鼠标恰好停在画布上），每敲一个 `f` 画布就重适配一次；`HandleKeys`
  （`:476`）有 WantTextInput 守卫，画布侧相机键漏了。滚轮不产字符无此问题。
- **建议**：两处补 `!ImGui::GetIO().WantTextInput` 前置。

### B5. 待点建边模式（右键「新建出边」）被画布外交互搁置后无出路提示、可叠开菜单

- **位置**：`AnimControllerPanel.cpp:1050-1054`（菜单进入待点）+ `:1089-1115`（收尾只认
  画布内 `hovered`）。
- **链路**：进入待点模式后，点击检查器/其他面板不消费也不取消，`edgeDragFrom_` 静默
  挂起（提示文案只画在画布内，用户视线已离开画布）；期间再次右键画布还能叠开另一个
  上下文菜单；此后任意一次画布左键——包括用户只是想"点空白清选择"——都会直接建边。
  Esc 可取消但用户此刻未必知道处于待点态。
- **建议**：任一画布外左键（帧内 hovered=false 的点击）取消待点；或待点态下右键改为
  取消而非开新菜单。

### B6. 删除最后一个状态后档案落入"必拒保存"形态，报错措辞不知所云

- **位置**：`DeleteState`（`:364-385`）无末态守卫；`TrySave` roundtrip 门禁
  （`:243-248`）会被 ParseControllerJson 的 "缺 states[]（或为空）" 拒。
- **链路**：UI 一路允许删到 0 状态 → 保存必失败，红字是解析器措辞，用户不知道要
  右键画布补一个状态才能救。schema 侧 states 非空是硬约束（合理），UI 面未对齐。
- **建议**：删末态时 UI 拦截（tooltip 说明），或保存前兜底回一个占位状态。
- **备注**：b9 起既有，非批⑩ 引入。

### B7. 拖动条件阈值/参数默认值期间每帧触发全项目跨资产 lint 扫描

- **位置**：`DrawCondRow:786` / `DrawParamsTable:574`——`DragFloat` 返 true 即
  `MarkDirty()` → `lintDirty_ = true` → 下一帧 `RecomputeLint`（`:165-218`）。
- **链路**：RecomputeLint 每次全项目重读**全部 .ani**（ifstream + ParseAnimSetJson）+
  引用的 .clip（`clipLoop` 缓存是函数局部量，跨帧不存）。拖一个滑杆 = 拖动期间每帧
  全项目文件 IO + JSON 解析。阈值/默认值变更不改变 lint 的判据集（exitTime 出边的
  from 状态名集合），本不必触发重算。
- **影响**：小项目无感；.ani 一多，每次拖动即卡顿。exitStates 为空时可提前返回，
  故仅对带段末边的档发作。
- **建议**：lintDirty_ 只由真正的结构变更置位（边的增删/exitTime 勾选/状态改名），
  数值拖动只置 dirty_；或 clipLoop 缓存提为面板成员按 (guid,hash) 常驻。
- **备注**：b9 起既有，非批⑩ 引入。

## P3 —— 小问题 / 打磨（登记不阻塞）

- **B8** 节点完全重叠时点选命中"布局序最先"而非视觉最上层（`hitNodeLi` 正序返回首个，
  绘制后者盖前者）——拖开后自愈，低频。
- **B9** `LoadFrom`/`StartCreate` 不清 `saveMsg_`/`saveOk_`——换档后顶栏残留上一档的
  "已保存/失败"消息直至下次保存。
- **B10** 注释烂账：`BuiltInPanels.h:530` `AddStateForTest` 注释"与「+ 状态」按钮同入口"
  ——批⑩ 已无该按钮（画布右键是唯一建状态入口）；`animctrl.canvas.hint` 未提 Del 键
  （Del 删边/删状态已实现且是核心快捷键）。
- **B11** 批文件 In 表 #1 写 "fit（F/按钮）"，实现只有 F 键，无按钮（可归 Out 候选池）。
- **B12** 边命中 24 段定长采样：zoom 高 + 长边时相邻采样点弦差可超 6px 阈值，出现
  "看得见点不中"的死区；可按屏幕长度自适应段数（如每 8px 一段，设上限）。
- **B13** Play 高亮多实体同 controller 只亮第一个（`:1166-1172` 循环 `break`）——多角色
  共图场景信息不全；至少注释声明口径，或入候选池。
- **B14** bool 参数的既有条件算子显示恒 "=="：档面手写 `{"param":bool, ">":x}`（schema
  不校验 op×kind）时检查器显示与实际求值不符（EdgeSummary 显示真实 op，两处不一致）。
  schema 层旧账，非本批引入，与 b9 行为一致。
- **B15** Play 态画布底部操作提示照常渲染，但其中的编辑动作（拖/连线/双击/右键）在
  Play 全禁用——提示与可用动作不符（随 B2 一起顺手处理）。

---

## 已核查、未发现问题的面（抽查记录）

- **SnapshotStack**：Undo/Redo 交换语义、分叉清重做、双栈对称封顶、容量 0 兜底均正确；
  `UndoLocal/RedoLocal` 中 `Undo(edit_, out)` 先压当前态再换出快照的顺序无误；
  单测 11 checks 覆盖与实现一致（EditorTests.cpp:765-802）。
- **序列化/roundtrip**：`ControllerToJson` editor 块仅写仍列状态、states 序；parse 侧
  容错（非对象/坏坐标/悬空键逐项忽略）与归一序同构；`TrySave` 前 entry 显式化 +
  `NormalizeLayout` 保证六字段 roundtrip 比较成立；`AppendFloat` 防值漂移沿用。
- **自动布局**：BFS 入队序 = transitions 文件序（确定性，无哈希序——`rowsInLayer` 只做
  键访问不迭代）；不可达组件"各起新列"（`maxLayer+1` 逐组件递进）符合批文件口径；
  192/96 列行距 8px 对齐。
- **相机数学**：滚轮锚定（先取 anchor 再改 zoom 再反解 pan）与 FitCanvas 居中
  （pan = avail/2 − 中心×zoom）公式正确；缩放双端 clamp 0.25–2.0 一致。
- **几何**：贝塞尔 t=0.5 中点 (a+3c1+3c2+b)/8 正确；箭头 tip 的盒边投影
  （min(hw/|dx|, hh/|dy|)，除零有 1e-4 下限）正确；自环圆环距离命中与绘制同源。
- **键仲裁**：`CapturesUndoKeys` 窄位虚函数不影响 AnimationPanel；BuildShortcuts 双位
  检出后跳过全局 Ctrl+Z/Y 与 Del/Ctrl+D 正确；`HandleKeys` 的 ro/WantTextInput/手势中
  三重守卫齐全；弹窗打开时 `keysFocused_`（ChildWindows 口径）自然为 false，不会误吃
  Del。滞后一帧读取为既知模式（与 AnimationPanel 一致）。
- **迭代器安全**：DrawNodeInspector/DrawEdgeInspector 删除路径 `return` 收口、
  DrawCondRow 删行返回值协议、DrawParamsTable 删参数后 `return` 全部正确；PushUndo
  （按值拷贝快照）不产生引用失效问题；改选中/落边后引用 `name`/`t` 的存活窗口核对无误。
- **Play 高亮数据面**：`AnimGraph.controllerGuid`/`setGuid` 低 32 位与 `ClipTable::
  NameOfClip(setId, clipId)` 契约吻合；`ActiveWorld()` 在 Play 态返回 playWorld 正确。
- **i18n**：15 个新 key zh/en 双语齐全；6 个死键已从两份 json 删除且代码零残留引用
  （grep 确认）。
- **冒烟/单测对齐**：f37 计数链推导（4→5→undo 4→redo 5→del 4、`ni==t0`）与实现一致；
  f126→f127→f129 的"装载清选择 → 再选 → 双探针"时序与代码吻合；f36 盘上 editor 块
  roundtrip 断言（layout.size==states.size + 目标坐标）成立。

## 建议的处置顺序（供拍板，未动代码）

1. **必修（真人走查前）**：B1（自环误触）、B2（Play 相机）——都是批⑩ 新引入、
   直接打击"15 分钟观感"主打体验。
2. **应修**：B3（保存三连副作用，至少保撤销栈 + 相机）、B4（F 键误触发）。
3. **择机**：B5/B6/B7 + P3 清单，B6/B7 为 b9 旧账可另开小批。
