# M8 批⑨ —— .controller 状态机工作台（动画创作面打磨 A/3）

Status: done（2026-10-16 当日机器面收口；系列 = 动画创作面打磨 批⑨–⑪，
2026-10-16 用户拍板按推荐执行——批⑩ 帧事件 UI / 批⑪ 样本+手册+模板后续另立批文件。
真人走查归批⑪ 组合验收）

## 1. 输入条件

- **动因（2026-10-16 用户）**：动画工作流"还不能很顺利的使用起来"。梳理结论 =
  运行时链路闭环完整（解析→Play 缓存→求值→写回→C#→事件，三层测试覆盖），
  痛点集中在创作面：**.controller 没有任何编辑器 UI**（全 Editor 无
  `ControllerToJson` 调用——手写 JSON 是唯一通道，`Duelist.controller` 12 条
  transitions 全手写），错误要到进 Play 才炸（快照语义当局还不生效）。
- **ADR-013 口径**：v1 负面项自登记"无可视化图编辑（.controller JSON 手写 +
  Inspector 编辑）"——本批补的正是 v1 该有的编辑面，**不做可视化图**
  （"游戏阻塞触发"挂起项不动）、不抄 mecanim（无 blend tree/layers/sub-FSM）。
- **既有数据面（零改动复用）**：`Engine/Assets/ControllerAsset.h/.cpp`
  （`ControllerData` 编辑模型 + `ParseControllerJson` 全量校验 + `ControllerToJson`
  定版序列化，M7a 批② 已自 Editor/Assets/ControllerEdit 下沉引擎）；smoke-anim
  已有 controller 数据面 roundtrip（smokeGraphEditOk）。
- **插队定位**：同批⑧ 先例（用户优先级插队，非 M8 原出口判据范围）。

## 2. 文件/行级变更单

- **Engine（纯逻辑，可单测）**：
  - `Engine/Assets/ControllerAsset.h/.cpp`：新增
    `ControllerStructuralWarnings(const ControllerData&) → vector<string>`——
    entry 出发 BFS 不可达状态（作者错误早发现；解析器管"引用悬空"，lint 管
    "逻辑死状态"）。
- **Editor（新面板 `AnimControllerPanel`，独立文件不塞 2383 行的 AnimationPanel）**：
  - `Editor/Panels/BuiltInPanels.h`：类声明（Name()="AnimController"、按需窗口、
    SetTarget/ForTest 冒烟钩子面）。
  - `Editor/Panels/AnimControllerPanel.cpp`（新建）：三张表工作台——
    左列 = 参数表（name/kind/def，≤8 拦）+ 状态词表（选中/改名/增删，entry 徽标）；
    右区 = 选中状态出边表（to 下拉/exitTime 勾选/条件行 param+op+value，trigger
    参数固定 op）+ 入边只读行。**编辑期校验 = 本批灵魂**：结构 lint（不可达状态，
    引擎函数同源）+ 跨资产脚枪提示（exitTime 出边状态在项目 .ani 集中绑定的
    clip 若为 Loop 模式 → 黄字——`Systems.cpp` exitTime 只认非 loop 收尾边沿的
    登记边角，编辑面堵而非改运行时）；Play 态只读横幅（快照语义提示）。
    保存走 TrySave 三关范式（校验 → 序列化 → roundtrip 预验 → 原子写 → Rescan）。
    新建 = 面板空态 inline（落 `Assets/Controllers/`，ADR-013 D3 目录约定）。
  - `Editor/Panels/Panels.cpp` / `Editor/App/EditorAppChrome.cpp`：注册 + 底部区
    停靠（Animation 同位）。
  - `Editor/App/EditorApp.h` + `EditorAppActions.cpp`：
    `OpenAnimControllerEditor(guid)`（双击路由）+ `OpenAnimControllerCreate(dir)`。
  - `Editor/Panels/AssetBrowserPanel.cpp`：双击 .controller 进工作台；空白区
    右键"新建动画状态机…"。
  - `Editor/Panels/AnimationPanel.cpp:2300`：顺手修 `panel.clipation` 死 key →
    `panel.animation`（双语表均有正名，面板标题现显示的是原文 key——替换事故）。
- **i18n（双语对齐）**：`panels.json`（panel.animcontroller）、`anim.json`
  （工作台 ~30 keys）、`assets.json`（ctx.new_controller）。
- **测试**：
  - `tests/engine/EditorTests.cpp`：`TestControllerStructuralWarnings`
    （不可达检出 / 全可达零告警 / 坏档容错）。
  - `Editor/App/EditorAppSmoke.cpp`：--smoke-anim 扩四断言——
    smokeCtrlOpenOk（seed：双击路由置位）/ smokeCtrlLoadOk（f33：面板装载 3 状态 +
    加"Dead"后结构 lint 告警 ≥1）/ smokeCtrlSaveOk（f34：出边补 hit→Dead
    exitTime → 保存 → 盘上重读 4 状态 4 过渡）/ smokeCtrlGateOk（f35：悬空 to
    被保存门禁拒 → 恢复后保存过）；verdict 行进 `SmokeAnimVerdict`。

## 3. 验证（2026-10-16）

- 构建 Release 零警告；单测 **34,937 checks OK**（+5 lint 用例 +2 wizard 目录项）；
  ctest 4/4；金回放零重录（引擎运行时零改动——仅新增纯函数 lint）；vtable/组件/
  系统计数不变。
- **--smoke-anim 全绿**（回归脚本同口径 fresh 项目 + retry）：zh/en 双档 PASS，
  新四断言 `ctrl(open/load/save/gate)` 在全部轮次（含复跑）零抖动；既有
  pick/folder 注入链的偶发红 = 官方登记抖动族（09 §9；HEAD 同场再现取证，
  与本批正交）。
- 机器证：smoke 链内完成 路由→装载→孤岛 lint 告警→出边编辑→保存→盘上
  roundtrip→悬空引用门禁拒→跨资产黄字（walk 段末 × walk.clip=Loop）全链；
  渲染面探针（animctrl.begin/skip）证 OnGui 进渲染链。
- 真人走查：归批⑪ 组合验收（样本/手册就绪后一并过）。

## 4. 过程裁决与顺手修（全在批内闭环）

- **AnimController 浮动默认（不进 DockBuilder）**：预挂 bottomId 会与 Animation
  抢同一标签组——真人侧"编辑 clip 时双击 .controller 把胶片带压成后台标签"
  互踩；smoke 侧 ini 恢复态把"活动 = AnimController"持久化 → Animation 的
  Begin false → 注入链全断（首轮实测定位）。浮动默认 = Audio Mixer 工具窗
  先例，用户停靠一次 ini 记忆；三表表单对窗形不挑。
- **smoke 渲染面探针收尾即关窗（f130）**：开窗态被 ini 持久化（开 + 浮动位置），
  同项目复跑该窗从 f0 浮在 FilePicker 落位上方吃注入点击（复跑实测定位）。
  同项目复跑绿 = 本就口径外场景（HEAD 同场全红取证），关窗使复跑态趋同 fresh。
- **存量测试顺手修齐**（3d021e0 布局归一未随迁，HEAD 上红）：TestProjectWizard
  目录断言 Game→Assets/Scripts 族三处（`wizard dir missing: Game` /
  `SpawnerBehaviour.cs references spawn guid` / `template declares class`）。
- **panel.clipation 死 key 顺手修**：AnimationPanel 标题 key 双语表均无（替换
  事故），动画面板标题一直显示原文 key → 改 `panel.animation`。
- **EnsureLoaded 急切装载**：OpenAnimControllerEditor 装载 + 跨资产 lint 同步，
  无头路径（冒烟）不开窗也持编辑态/告警——OnGui 缓存键判定照旧。

## 5. 遗留与登记（不阻塞本批）

- 可视化图编辑（节点/连线）维持 ADR-013 挂起（"游戏阻塞触发"）。
- exitTime×Loop 运行时语义维持登记（`Systems.cpp` 注记）——本批以编辑期黄字
  吸收，不改求值；per-frame 帧时长 / 帧事件命名化维持 v1.1 登记（帧事件 UI
  归批⑩）。
- any-state 边/过渡优先级显式排序不入 v1（文件序评估，ADR-013 D4 口径）。
- 真人走查归批⑪ 组合验收。
