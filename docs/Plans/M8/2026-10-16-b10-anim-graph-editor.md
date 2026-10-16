# M8 批⑩ —— 精简可视化图编辑（.controller 画布工作台）

Status: planned（2026-10-16 用户拍板：可视化图编辑**精简版**插队——b9 三表工作台
机器面全绿但真人"完全不会用"，对照 Unity/Godot 定为图管结构 + 检查器管细节，不抄
mecanim 全量；原系列 ⑩ 帧事件 UI / ⑪ 样本+手册+模板顺延为 ⑪/⑫，真人走查归批⑫
组合验收。设计定形 = [ADR-013](../../ADR/ADR-013-Anim-Control-Layer-And-Asset-Family.md)
D6（2026-10-16 增补）。开工前置 = 批⑨ 先提交（工作树洁净进入））

## 1. 输入条件

- **动因（2026-10-16 用户）**：b9 工作台（三张表：参数/状态/出边）实测不可用——
  结构导航与细节编辑全量同屏，需先建立完整心智模型才能动手；结论 = 图是"结构导航"
  的正确载体，但细节必须按需（检查器），冷启动给默认路径。
- **范围纪律（用户拍板）**：精简核心版——只做"看图/摆位/连线/选中改条件/增删/
  撤销"闭环；不做迷你地图/框选/多选/复制粘贴/注释框/自动整理等（Out 清单见 §2）。
- **ADR 口径**：D4 红线维持（无 any-state / blend tree / layers / sub-FSM）；
  D6 增补 = 图管结构 + 检查器管细节 + 布局随 `.controller` 持久化。
- **既有面（零改动或少改动复用）**：
  - b9 全量管线：TrySave 三门禁（校验→序列化→roundtrip→原子写→Rescan）/
    `ControllerStructuralWarnings` 不可达 lint / exitTime×Loop 跨资产黄字 /
    新建流程 / i18n 双语 / Play 只读快照语义。
  - 渲染先例：ImDrawList 自绘（AnimationPanel.cpp:838 胶片带同款）+ testhooks::Stash
    探针 + smoke-drag 鼠标注入链（可用性机器面）。
  - 数据面：`ParseControllerJson` 白名单读取已知 key（ControllerAsset.cpp:63-130），
    未知字段天然忽略 → `editor` 字段向后兼容、运行时 Play 缓存零感知。
  - Play 高亮：`EditorContext::ActiveWorld()` + `AnimGraph.controllerGuid` +
    `Animator2D.clipId` → `ClipTable::NameOfClip` 反推状态名（按状态名=段名约定），
    `AnimParams` 实时值；全现成，无引擎改动。

## 2. 范围（精简 v1 —— In / Out）

### In（核心闭环，做成即"可用"）

| # | 功能 | 交互规格 |
|---|---|---|
| 1 | 画布 | 节点=状态（名称 + 「入口」徽标 + 选中高亮 + 不可达灰化）；边=transition（贝塞尔 + 箭头 + 条件摘要标签；自环=环线；同 from/to 多条边垂直偏移）；平移（中键/空格拖）、缩放（滚轮锚定光标，0.25–2.0）、fit（F/按钮） |
| 2 | 自动布局 | 无布局数据时 entry BFS 分层（确定性，无随机——测试要）；手动拖过即固定，不自动重排；**不设**"整理"按钮（登记候选池） |
| 3 | 布局持久化 | `.controller` 顶层可选 `"editor": {"nodes": {"<状态名>": [x,y]}}`；整数量化（网格 8px）+ 按 states 序稳定写出；解析容错（坏项忽略）；TrySave roundtrip 比较扩展；状态改名/删除同步迁移 key |
| 4 | 节点编辑 | 拖动移位；点选；双击 inline 改名（复用 b9 交互）；右键菜单 = 设为入口/删除/新建出边；Del 删除（级联过渡一并清，可撤销） |
| 5 | 连线编辑 | 节点 hover 出拖柄 → 拖到目标建边（起步条件 = b9 规则：有参数→首参条件，无参数→段末）；点选边（曲线采样命中，中点小圆点辅助）；Del/右键删除；检查器编辑 to / exitTime / 条件行（复用 b9 控件与算子按类型收窄） |
| 6 | 检查器（右栏） | 选中节点 → 名称/入口/出边汇总；选中边 → 条件详情 + 同 from 求值序 `N/总`（首条命中即切，必须可见）；未选中 → 参数表（b9 原样，默认折叠） |
| 7 | Play 态 | 只读 + **当前状态高亮**（数据面见 §1）；参数实时值可后置 |
| 8 | 校验保留 | b9 全部：不可达灰化上画布 + 黄字区 + TrySave 三门禁 + 新建流程 + i18n 双语 |
| 9 | 撤销/重做 | 面板本地快照栈（任一数据变更前压 `ControllerData` 快照，容量 50；拖动按松手拍一次，非逐帧）；Ctrl+Z / Ctrl+Shift+Z（含 Ctrl+Y）面板聚焦内优先，Play 只读禁用；与场景结构轨 Undo 不互通——登记后续统一 |

### Out（登记候选池，不阻塞本批）

自动整理按钮、迷你地图、框选/多选、复制粘贴、注释框、节点折叠、
"从 .ani 集一键生成"、条件 chip 内联编辑、边折点/锚点、动画预览小窗、多选对齐。

## 3. 文件/行级变更单

- **Engine/Assets/ControllerAsset.h/.cpp**（纯扩展，运行时语义零改动）：
  - `ControllerData` 尾加布局容器（状态名 → 整数坐标；`operator==` 自然含）；
  - `ParseControllerJson`：`editor.nodes` 容错读取（非对象/坐标非法 = 忽略单项）；
  - `ControllerToJson`：布局非空时尾部输出 `editor` 块（states 序，仅写已列状态）；
  - 新纯函数 `ControllerAutoLayoutNodes(const ControllerData&)`（entry BFS 分层，
    确定性；无布局档/首次打开用）。
- **Editor/Panels/AnimControllerPanel.cpp**（重构主承载，b9 校验/保存函数保留）：
  - 画布 child：InvisibleButton + ImDrawList 自绘 + 相机变换（screen↔graph 纯函数）+
    命中测试（节点矩形 / 边曲线采样 / 拖柄）；
  - 交互状态机：平移/缩放/拖节点/拖线建边/选中/删除/右键菜单/双击改名；
  - 检查器 child：选中态驱动，b9 出边/条件控件函数化复用；
  - Play 高亮查询；探针 = `animctrl.begin` 保留 + 新增画布/选中/高亮探针。
- **Editor/Panels/BuiltInPanels.h**：成员扩展（选中态/相机/拖拽状态/renameIdx 沿用）。
- **i18n**：`Strings/{zh-CN,en}/anim.json` 新 key（画布提示/右键菜单/确认框/
  自动布局等，双语对齐）。
- **Editor/App/EditorAppSmoke.cpp**：ctrl 段扩展——拖拽移动坐标落盘 / 拖线建边
  transitions +1 / 删边 -1 三个断言 + 画布探针；既有 ctrl(open/load/save/gate)
  四断言零回归；`SmokeAnimVerdict` 加位。
- **tests/engine/EditorTests.cpp**：布局 roundtrip（含 editor 块 golden）/坏
  editor 数据容错/自动布局确定性/坐标量化。
- **文档**：ADR-013 D6 ✅ 规划期已落账；本文件 + M8.md 行；完工时 05-Editor.md
  功能盘点 + DevLog 条目；06 §2.2 补 `.controller` 可选 `editor` 字段注记。

## 4. 分期（单批两段 + 超期预案）

- **⑩a 画布与布局（约 1.5 日）**：schema（editor 字段解析/序列化/roundtrip/单测）
  → 面板两栏重构 → 相机 + 节点/边渲染 + 拖节点 + 自动布局 + 保存链路。
  出口：打开 Duelist.controller（或任意 3–4 态档）可见图、拖动落盘 roundtrip；
  单测绿；smoke 既有 ctrl 四断言 + 新画布探针。
- **⑩b 连线与检查器（约 1.5 日）**：拖线建边 / 边选中删除 / 检查器接线
  （复用 b9 控件）/ 右键菜单 / 改名迁移 / 入口标记 / Play 高亮 / 不可达灰化 +
  黄字定位 / 撤销重做快照栈。
  出口：smoke 三新断言（移动/建边/删边）+ 检查器探针；**15 分钟真人观感过目**
  （对照 b9 表单体验）。
- **超期预案**：拆 ⑩a / ⑩b 两个批文件（⑩a 出口即上表，⑩b 顺延），批大小纪律
  ≤2k 行 / ≤3 天。

## 5. 验证判据

- **机器面**：构建零警告；单测（+~10 用例，含撤销栈逻辑与布局函数）；ctest 4/4；
  script-tests 不变；`--smoke-anim` zh+en PASS（新断言全轮零抖动 + 既有四断言零
  回归）；探针（画布 rect/节点数/边数/选中/高亮）；undo/redo 往返断言（smoke 一步）；
  **金回放零重录**（编辑器专属改动；唯一触点 = `ControllerToJson` 增可选 `editor`
  块，运行时解析忽略，仓内存量 `.controller` 只在编辑器保存时写入——批内实测确证）。
- **真人面**：15 分钟观感（打开 Duelist 图 → 拖节点/连线/改条件 → 保存 → Play
  高亮）；组合验收归批⑫。
- **判据对照**：本批为插队批，不涉 08 M8 出口判据；对齐"每步可运行/可量化验收"。

## 6. 风险与对策

| 风险 | 对策 |
|---|---|
| 拖拽交互机器可测性差 | API 级断言为主（建边/移动/删除走面板接口面）+ smoke 注入链为辅；探针证 UI 面活着 |
| roundtrip 门禁被新字段拦保存 | 单测先行：TrySave 比较扩展与序列化同批落；坏数据容错用例 |
| ImDrawList 缩放文字点阵模糊 | `AddText(font, size*zoom)`；矢量线不糊可接受；v1 登记 |
| 布局与求值序混淆 | 图上不画优先级；序号只在检查器（b9 `N/总` 沿用） |
| 面板重构破坏 `animctrl` 探针 | 探针名保持/扩展，smoke 同步改，报错消息点名探针 |
| 撤销与全局 Undo 栈不互通 | 面板聚焦内优先拦截 Ctrl+Z/Ctrl+Y；不与场景结构轨交叉消费；登记后续统一（§7） |
| b9 未提交叠加污染 | 开工前置 = 批⑨ 提交（已 ✅ 2026-10-16 `8ff48d1`，单独 commit），批⑩ 工作树洁净进入 |

## 7. 遗留与登记

- Out 候选池全项（§2）——触发条件 = 真人使用反馈出现对应需求。
- mecanim 全量（blend tree / layers / any-state / sub-FSM）维持 D4 永久红线。
- 帧事件 UI（原⑩）顺延批⑪；样本+手册+模板（原⑪）顺延批⑫（真人走查归批⑫ 组合验收）。
- Play 参数实时值浮层 = v1.1 触发式（状态高亮先行）。
