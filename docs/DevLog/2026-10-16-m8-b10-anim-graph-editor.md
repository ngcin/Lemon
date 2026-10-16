# 2026-10-16 M8 批⑩ —— 精简可视化图编辑（.controller 画布工作台，动画创作面打磨②/④）

## 输入

同日 b9（三表工作台，`8ff48d1`）机器面全绿后真人实测**完全不会用**：参数/状态/
出边三表同屏，需先建立完整心智模型才能动手。用户拍板"可视化图编辑**精简版**"
插队，对照 Unity/Godot 定形 **图管结构 + 检查器管细节**（[ADR-013](../../ADR/ADR-013-Anim-Control-Layer-And-Asset-Family.md)
D6，规划期落账）：只做"看图/摆位/连线/选中改条件/增删/撤销"闭环，不做 mecanim
全量（D4 红线维持）。编号顺延：原⑩ 帧事件 UI → ⑪、⑪ 样本+手册+模板 → ⑫，
真人走查归批⑫ 组合验收。批文件 [2026-10-16-b10-anim-graph-editor.md](../../Plans/M8/2026-10-16-b10-anim-graph-editor.md)。

## 交付（⑩a 画布与布局 + ⑩b 连线与检查器）

- **Engine/Assets/ControllerAsset.h/.cpp（纯扩展，运行时语义零改动）**：
  - `ControllerData` 尾加 `layout`（`{state,x,y}` 数组）；`ParseControllerJson`
    容错读 `editor.nodes`（非对象/坏坐标 = 忽略单项；悬空键忽略）；`ControllerToJson`
    布局非空时尾部按 states 序输出 `editor` 块（保存即清洗悬空键）；
  - 新纯函数 `ControllerAutoLayoutNodes`（entry BFS 分层 + 不可达尾列，确定性）+
    `ControllerSnapGrid`（8px 最近格量化）。
- **Editor/Panels/AnimControllerPanel.cpp（重构主承载，~1350 行）**：
  - **画布（⑩a）**：ImDrawList 自绘 + 相机（中键/空格平移、滚轮锚定缩放 0.25–2.0、
    F 适配）；节点=状态（名称/entry 徽标/选中高亮/不可达灰化/Play 当前态 warn
    色外框）；边=三次贝塞尔 + 箭头 + `EdgeSummary` 条件摘要标签（同 from/to 垂直
    偏移；自环=右侧环线）；拖节点移位（手→图空间换算，SetNodePosition 单一落位
    通道 + 网格量化）。
  - **连线与交互（⑩b）**：节点右缘把手拖线建边（起步条件 = b9 规则：有参数 →
    首参条件/无参数 → exitTime）；右键菜单（节点：设为入口/新建出边/删除状态；
    空白：新建状态——自动唯一名 State/State2，落点=右键处）；双击节点改名 popup；
    边命中=贝塞尔 24 段采样 6px 阈值（自环=圆环距离）；Del 删边优先、否则删状态。
  - **检查器（选择驱动，ADR-013 D6）**：选中边 → to/exitTime/条件行（b9 控件
    函数化 `DrawCondRow`）+ 同 from 求值序 `N/总`（首条命中即切，必须可见）；
    选中节点 → 改名/设为入口/出边汇总行（点选转边详情）/删除状态；未选中 →
    参数表（改默认折叠，b9 原样）+ 操作提示。三表（状态词表/出边表）废弃删除。
  - **撤销/重做**：`Editor/SnapshotStack.h`（新，无 ImGui 依赖 editor-core 头，
    容量 50、新变更清重做分支、双栈对称封顶）——所有变更走单一入口 PushUndo
    先压快照；拖动/滑杆类"松手拍一次"（`IsItemActivated` 起拍）。键 = Ctrl+Z /
    Ctrl+Shift+Z / Ctrl+Y（`HandleKeys`：ro/输入中/手势中自守卫）。
  - **Play 高亮**：`ActiveScene().View<AnimGraph,Animator2D>` + controllerGuid
    匹配 + `ClipTable::NameOfClip` 反推段名 = 状态名（零引擎改动）。
- **Editor/Panels/Panel.h + EditorAppChrome.cpp（键仲裁）**：新虚函数
  `CapturesUndoKeys()`（窄位，不影响 AnimationPanel）；BuildShortcuts 双捕获
  循环——面板聚焦内 Ctrl+Z/Ctrl+Y/Del 归面板本地栈，全局结构轨让位（登记后续
  统一，批文件 §7）。
- **i18n**：zh/en 各 +15 key（ctx 菜单/node/edge/rename/insp.hint/edge_drag_hint），
  更新 `canvas.hint`（拖把手连线/双击改名/右键菜单）；顺手清 6 个死 key
  （states.header/new_hint、trans.no_state/out_header/in_header/in_exit）。
- **EditorAppSmoke.cpp**：f37 连线链（`AddTransitionForTest(0,2)` → 撤销 → 重做 →
  `DeleteTransitionForTest` 计数往返 4→5→4→5→4）；f126/127/129 渲染面探针修正
  （f126 只开窗——LoadFrom 会清选择，f127 再选，f129 断言 begin 路径下
  `animctrl.canvas` + `animctrl.insp.node` 双探针在场）；verdict 加
  `ctrl(... layout edge)` 位。
- **tests/engine/EditorTests.cpp**：`TestSnapshotStack`（空栈/往返/分叉清重做/
  容量封顶丢最旧/Clear/容量 0 兜底，+11 checks）。

## 机器面证据（2026-10-16）

- 构建零警告（唯一 ld duplicate-library 警告 = b1 候选池已登记项）。
- 单测 **34,959** checks OK（⑩a +11 → 34,948；⑩b +11）；ctest 4/4；script-tests
  1,911（⑩b 未触脚本面）。
- `--smoke-anim` zh+en：`ctrl(open=YES load=YES save=YES gate=YES layout=YES
  edge=YES) => OK`（隔离跑双绿；出边=新链 f37 + f129 双探针断言）。
- 回归 full 26 步：25 PASS + bench-survivor 热态红（fps=76 vs 门禁 76.5，单
  present 尖刺家族）→ 隔离复跑 **fps=78 PASS**（frameMax=20.94ms 无尖刺）；
  anim-chain zh 在回归内一次红 = 已登记注入抖动家族（pick click/shift + folder
  位）——隔离复跑绿（本次会话隔离跑双绿在前），非回归。
- **金回放零重录**：⑩a 已证（HEAD 态录 → ⑩a 态放，三档 mismatches=0，stash
  两段器）；⑩b 改动仅 Editor/* + tests + Strings + 新头文件，**零 Engine 运行时
  文件**——证据沿用，无需重录。

## 顺手修

- `SelectStateForTest` 直赋 selState_ → 改走 `SelectState`（清边选 + 同步改名
  缓冲，测试钩子与 UI 入口同语义）。
- smoke f126 选择被 LoadFrom 清零的隐性时序（⑩a 起探针只证 begin/skip；本批
  修正为 f127 选、f129 断言检查器探针）。

## 遗留与登记

- Out 候选池（自动整理/迷你地图/框选多选/复制粘贴/注释框/节点折叠/从 .ani 集
  一键生成/条件 chip 内联/边折点/预览小窗/多选对齐）——触发 = 真人反馈。
- 面板本地撤销栈与场景结构轨 Undo 不互通——后续统一（批文件 §7 登记）。
- Play 参数实时值浮层 = v1.1 触发式（状态高亮先行）。
- **真人面**：15 分钟观感过目（对照 b9 表单体验）待用户窗口；组合验收归批⑫。
  截图自证：`--smoke-anim` 场景图（可复跑）；Duelist.controller 4 态图待真人。
