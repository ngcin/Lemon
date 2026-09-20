# Lemon 引擎设计 — M4 编辑器详细规划

> 定位：[05 编辑器分册](./05-Editor.md)是设计总册（"编辑器应该是什么"）；本册是 **M4 实施详细规划**（"M4 具体做哪些、什么顺序、怎么验收"），2026-09-19 与作者两轮讨论定稿。
>
> **进度（2026-09-19 动工轮）**：M4.0–M4.3 已完成并通过各自可运行验收（实测数字见
> DevLog 同日条目）：冷启 1741→543ms；场景 IO roundtrip CLI 化；SceneView 双视口
> 离屏 + Gizmo/拾取/网格（验证层 4 真错修复后零报错）；**Play 进/出 0.5/0.3ms +
> Stop 逐字节一致断言 PASS（验收 #4/#5）**；Undo 双轨 + 输入路由子集落地。
> **M4.4 已完成**（同日第二 entry）：资产管线（GUID/.meta/manifest/体检红字）、
> PNG 导入 + FileWatcher 热替换（双分支实测）、AssetBrowser 全交互、Inspector
> sprite 槽/Prefab 头栏/ScriptBox 段、Prefab 最小集（SaveEntityTree/LoadEntityTree
> + Apply/Revert/Break）、C# SDK 增量（Input/Assets.SpriteOf/Instantiate +
> NativeApi 表尾 4 项 + lemon_behaviours_list）；脚本刷怪链冒烟 playAlive 6→42、
> engine-tests 12935 全绿。落位偏差：prefab 文件在 Assets/Prefabs/（根级 Prefabs/
> 目录随 M4.5 项目向导）；编辑器资产钩子经 SetEditorAssetHooks 注入（纯运行时=0）。
> **M4.5 已完成（2026-09-20，M4 全部收官）**：热重载 A 线整域重建（动工前探针复测
> 确认仍 pin——ADR-010 同日修订；StateBag 值类型白名单 + OnHotReloadOut/In 协议；
> Play/Edit 双态换装实测 1.24s/1.22s ≤2s、泄漏计数红字、watcher 自动触发 + obj/bin
> 排除 + 0.4s 防抖）；项目向导（blank 模板 06 §1 布局 + 零配置 Game/ 编译装配 +
> 种子资产/spawn 脚本 guid 直连）；自动备份与崩溃恢复（5min 快照 .lemon/autosave/
> + mtime 比对启动恢复提示 + 落盘即清）；资产扫描根改项目根（根级 Prefabs/ 入索引，
> M4.4 落位偏差消除）；Profiler GC 每帧差分红字 + 换装泄漏常驻。**终验 `--final` 全
> PASS**：向导→判据场景（零代码 14 实体）→Play fps 59（≥45）/进出 3.9/0.4ms/逐字节
> 一致→Play 中热重载 StateBag 续跑 66/66 精确断言→Edit 态换装注册表可见→备份恢复链
> →冷启 340ms；engine-tests 12972 / script-tests 1275 / ASan+UBSan 全绿。
> 实现备注：属性轨记录粒度 = 组件级字节快照（字段级拆分列 M5 精化）；纹理后端豁免口
> 与 ctest 隔离断言见 §3.2/§5 落地；构建分层 lemon-editor-core（无 ImGui 编辑器逻辑，
> 单测面）见 Editor/CMakeLists.txt。
> **M4.6 追加轮已规划（2026-09-20）**：M4 收官当日真人实测暴露 3 阻断级 bug（关闭
> 按钮/无项目导入/打开项目未接线，已修 `b7094a9`），共同根因 = 交互路径无自动回归。
> 可用性加固（会话闭环 + 编辑效率 + 交互冒烟）规划移
> [M4.6-Editor-Usability-Plan.md](./M4.6-Editor-Usability-Plan.md)，本册判据链不变。
> 前置状态：M3（2026-09-19 完成）+ M3.5 anim-smoke 全链基线。内核侧地基（反射注册表 / .scene v1（原 .lscene，同日更名）/ ClearViewport / Profiles / 脚本域）已就位，Editor/ 目录零代码。
> 纪律：本册判据与 [08 路线图](./08-Development-Roadmap.md) §0 总表一致；次级取舍以"推荐 + 砍单候补"标注，砍前记 ADR（08 §4）。

---

## 0. 结论速览

- **范围一句话**：ImGui(docking) 单窗口编辑器 `lemon-editor`，核心 7 面板 + 菜单/工具/状态栏，背景图 + Sprite 手摆地图，最小资产导入集（PNG/sprite/图集 + GUID/manifest + FileWatcher），Play 沙盒 + 属性级 Undo，C# 热重载（≤2s），新建项目向导，保存流程（手动 + 自动备份）。
- **出口判据**（不变，出自 08 §0）：**不写一行代码，纯编辑器操作搭出"走地图 + 刷怪"场景并可 Play；Play 中改 C# 热重载 ≤ 2s**；编辑器冷启动 < 2s；进 Play < 0.5s / 停止恢复 < 0.3s；Play 中 ≥ 45fps（00 §4 压测 B 编辑器语境）。
- **工期**：名义 **8 周**（08 窗口上限），六个子阶段 M4.0–M4.5；第 5 周末、第 7 周末两个检查点，滞后即按 §7 砍单序执行。
- **本册新决议**（讨论定稿，详见 §8）：验收场景用背景图 + Sprite 手摆（瓦片系统归 M6）｜面板集冻结为核心 7｜资产做最小导入集｜Play 中允许编辑但只落 Play World｜保存 = 手动 + 自动备份｜单 OS 窗口 docking｜C# SDK 落最小增量（Input/GUID 查询/Instantiate）｜Prefab 做最小集（逐字段 override 高亮列砍单候补首位）。

## 1. 范围与出口判据

### 1.1 In（M4 交付）

| 块 | 内容 |
|---|---|
| 壳 | `lemon-editor` 可执行目标（EditorEntry + 主循环 + 布局持久化）；ImGui docking（CPM 锁 commit）+ SDL3/Vulkan backend；中文字体内嵌与 IME |
| 面板 | Hierarchy / Inspector / SceneView / GameView / AssetBrowser / Console / Profiler + 菜单栏/工具栏/状态栏（§2） |
| 场景 IO | 打开/保存/另存 .scene、脏标记、Ctrl+S、关闭确认；自动备份与崩溃恢复（§3.8） |
| 编辑交互 | SceneView 视口渲染、Gizmo（移动/旋转/四角缩放）、拾取、网格吸附、模式栈/焦点仲裁/放行约定（Editor-RPG2D A 级拷贝） |
| Play/Undo | Play 沙盒（快照式）+ 属性级双轨 Undo（ADR-009）+ 输入路由（§3.4–3.6） |
| 资产 | GUID/.meta/manifest 数据库 + PNG/sprite/图集导入器 + FileWatcher 热替换 + AssetBrowser UI（§3 落地于 M4.4） |
| 脚本 | ScriptBox 编辑器装配通路（脚本资产 GUID 序列化）+ C# SDK 最小增量 + 热重载（§3.7、§4） |
| 项目 | 新建项目向导（blank 模板，project.lemon/engineVersion，06 §1） |
| Prefab | 最小集：拖入实例化 + Apply/Break + 整体 Revert（§3.9） |

### 1.2 Out（明确不做，防蔓延）

| 项 | 去向 |
|---|---|
| TilePalette / 放置状态机 / 自动瓦片 | M6（08 §2；验收场景用背景图 + Sprite 手摆，本册决议） |
| AnimationEditor / ParticleEditor / TeamEditor / DirectorEditor | M5/M6（05 §2 v1 清单中后四面板，本册冻结出 M4 面板集） |
| audio/font/clip2d/particles/tileset/curve 导入器 | M5+ 随消费者落地（06 §2.2 八类中 M4 只做 sprite 族） |
| multi-viewport（拖出独立 OS 窗口）、GameView 分屏/宽高比模拟 | M5+（本册决议：单窗口 docking；09 §8 的"多窗口补课"随之顺延） |
| Play 中改动回灌 Edit World | M5 验收项或显式禁用提示（05 §4、08 §5 砍单 #5） |
| `.ledit` 编辑器 C++ 插件 | 面板注册表编译期版顶住（08 §4 砍单 #7） |
| `.baked` 烘焙 | M7 |
| 可视化事件树编辑器 | 不在 v1（05 §8） |

### 1.3 出口判据分解

总判据"纯编辑器搭出走地图 + 刷怪场景可 Play"操作化为：**从 `lemon-editor --new-project` 开始，零代码零手改文件**，完成 新建项目 → 导入 PNG 素材 → 摆背景图与若干装饰 Sprite → 拖入玩家实体（Transform + SpriteRenderer + C# 脚本组件）→ Play 中脚本刷怪（Instantiate）→ Stop 后编辑场景无损。量化指标与验收矩阵见 §6。

## 2. 界面设计

### 2.1 主窗口布局（默认布局，ImGui ini 可重排）

```
┌────────────────────────────────────────────────────────────────────────┐
│ File  Edit  Assets  GameObject  Window  Help                            │ 菜单栏
│ [▶ Play] [⏸ Pause] [⏹ Stop] │ [W]移动 [E]旋转 [R]缩放 │ [▣网格吸附] [本地│ 工具栏
├───────────┬──────────────────────────────────────────┬─────────────────┤
│ Hierarchy │ [Scene | Game]                    ⋮  ⊞  │ Inspector       │
│           │                                          │                 │
│ ▾ 场景     │                                          │ ▸ Transform    │
│   Camera  │          SceneView / GameView            │   X [0.0]       │
│   Player  │        （引擎相机渲到纹理，贴 ImGui       │   Y [0.0]       │
│   BG      │              子区域）                    │ ▸ SpriteRender │
│   怪×N    │                                          │   Sprite [▾]   │
│           ├──────────────────────────────────────────┤ ▸ Chase        │
│           │ [Console | Assets]               ⋮  ⊞   │   speed [80]   │
│           │                                          │ [+ Add Component]│
│           │      Console / AssetBrowser              │                 │
├───────────┴──────────────────────────────────────────┴─────────────────┤
│ MyGame ●(脏) │ ▶ Playing │ 60fps 模拟1.2ms/渲染2.1ms GC 0B │ 选中 3 │ zh │ 状态栏
└────────────────────────────────────────────────────────────────────────┘
```

- Scene/Game 与 Console/Assets 各为同一 dock 区域的标签页（Unity 心智）；中央上 = 视口区，下 = 通用面板区；左右分栏可拖。
- Play 中顶部叠加**橙色横幅**（"PLAY MODE — 编辑落 Play World，Stop 即丢"），工具栏 Play→Stop 高亮（§2.4）。
- 状态栏：项目名 + 脏标记 ●｜Play 状态｜性能摘要（Profiler 同数据源的低频快照）｜选中实体数｜输入语言指示。

### 2.2 逐面板规格

每面板按 **数据源（现成地基）→ 关键交互 → 验收点** 给规格；`IEditorPanel` 接口与 PanelRegistry 编译期注册沿用 05 §2 原设计（反射注册降级为编译期注册表，对应砍单 #7）。

**Hierarchy（左）**
- 数据源：`Scene` 实体遍历 + `Hierarchy` 父子组件（M2 已有；深度 ≤ 8 防环）。**内核缺口**：世界矩阵合成提取代码尚无（§4-1）。
- 交互：树形展开/折叠、单击选中、Ctrl 多选；拖拽重排父子（拖到实体上 = 挂子，拖到空区 = 摘根）；右键菜单（Create Empty/Sprite/Camera/删除/复制/重命名/Prefab 化）；按 Team/Tag 行着色（05 §2）；搜索框（按名字段过滤）。
- 验收点：拖拽成环被拒（父不能挂到自身后代——**M2 复审 N6 遗留的环检测测试在此落地**）；Play 中 Hierarchy 显示 Play World 的实体集（与 Edit 隔离可感知）。

**Inspector（右）**
- 数据源：`ComponentRegistry` 元数据（27 组件，`FieldMeta` flags 位已预留编辑器控件扩展）。
- 交互：选中实体（末位选中为主对象）逐组件绘制折叠头 + 字段控件；`Add Component` 菜单按注册表分类；组件移除/上移下移；C# 脚本组件头（ScriptBox → 脚本资产选择器 + `[ShowInInspector]` 字段，元数据通道，05 §5）；Prefab 头栏（Apply/Revert/Break/Select，§3.9）。
- 字段控件最小集：float/int（DragFloat/DragInt，`Range` 特性夹取）、bool（Checkbox）、string（InputText，走 IME）、Vec2（双 Drag）、颜色（ColorEdit）、enum（注册表名表下拉）、**sprite 资产引用（GUID 拖拽槽，M4.4 接通）**。特性最小集：`Range{min,max}`、`Tooltip`、`ColorAs(Hex/Picker)`；其余（TeamPicker/sprite 选择器专属控件）M5 随消费者加。
- 验收点：27 个注册组件全部可显示、全部字段类型可编辑、改动即时反映到 SceneView；新增一个 LEMON_COMPONENT 组件零编辑器代码出现在 Inspector。

**SceneView（中央上，标签页 1）**
- 数据源：`RenderableManager::ClearViewport()` 全量渲染钩子（M2 预留）+ 编辑用 `Camera2D`（与游戏相机同类型仅输入来源不同，02 §3.5）+ RHI 离屏纹理 → ImGui Image。
- 交互：中键/空格拖平移、滚轮以鼠标为中心缩放、`F` 框选聚焦选中集、Gizmo（W/E/R 切换：移动十字箭头 / 旋转圈 / 四角缩放手柄——MoteurJV 手柄形态 + Looper `editor/gizmo` D 级对照自研）、网格吸附开关（平移 8px 档 + 旋转 15° 档）、Ctrl+拖 = 复制放置、像素对齐开关、Y 翻转预览开关（DevLog Y 镜像教训的常驻回归开关）。
- 拾取：点击 → 查询层 PointQuery → 最近 SpriteRenderer/碰撞盒（05 §6）；v1 单选 + Ctrl 多选，**框选 = 砍单候补**。
- 绘制层（Editor Pass，02 §4，§4-12）：网格、选择框、Gizmo 手柄、Sprite 边界。Team 调试色/流场可视化等 M5/M6。
- 验收点：resize/失焦恢复后相机自动校正（zoom/焦点自愈，02 §3.5）；**非对称锚点 GUI 验收清单**——视口内含文字（BitmapFont）、进度条、编号的样例实体，任何变换/翻转操作后锚点不错位（DevLog M3.5 教训固化）。

**GameView（中央上，标签页 2）**
- 数据源：游戏相机（场景中 Camera 实体）渲到离屏纹理；非 Play 时也实时显示（编辑相机与游戏相机解耦可见）。
- 交互：聚焦时键鼠输入进 Play World（§3.6 门控）；无工具（Gizmo 不作用于 GameView）。
- 验收点：Play 中游戏画面即此视口（结构上排除"编辑器预览 vs 实机"漂移，05 §1）；失焦时输入不进 Play World。

**AssetBrowser（中央下，标签页 2）**
- 数据源：项目 `Assets/` 目录扫描 + GUID/manifest 数据库（M4.4，06 §2）。
- 交互：目录树 + 缩略图网格（异步生成，05 §9 纪律）；双击/拖拽 PNG 进 SceneView = 创建 Sprite 实体；拖到 Inspector sprite 槽 = 设 GUID 引用；右键导入/重命名/删除（重命名不破坏引用——manifest 支撑，06 §2）； prefab 资产拖进 Hierarchy/SceneView = 实例化（§3.9）。
- 验收点：重命名/移动资产后场景内引用不断；孤儿 meta/缺失依赖在启动体检中红字报告（06 §2）。

**Console（中央下，标签页 1）**
- 数据源：编辑器侧日志环（引擎日志 + 脚本异常桥 + 热重载/导入器消息）。
- 交互：分级过滤（Info/Warn/Error/Script）、清空、复制行；Error 行点击跳转（M4 范围：跳到 Console 顶部并高亮源标记，**跳脚本行号 M5**）。
- 验收点：毒脚本 60 帧禁用的红字与计数可见（M3 行为在编辑器内可见）。

**Profiler（Window 菜单唤出，非默认布局）**
- 数据源：`SystemPipeline::Profiles()` 每系统计时 + RHI `LastFrameTiming()`（需 `EnableTimestamps`）+ `GcAllocated()`——与 `--stats` 同一数据源（M2 交付承诺）。
- 交互：系统序列按实际执行顺序可视化（03 §4）；GC 每帧托管分配 > 0 热路径直接红字（00 判据）；ResetProfiles 语义按 M2 复审 N8 在此复核（峰值窗口重置）。
- 验收点：F3 数据与 `--stats` 文本输出一致（同源断言）。

### 2.3 键位与 DPI/IME 约定

| 键 | 动作 | | 键 | 动作 |
|---|---|---|---|---|
| W/E/R | Gizmo 移动/旋转/缩放 | | Ctrl+S | 保存场景 |
| F | 框选聚焦选中集 | | Ctrl+Z / Ctrl+Y | Undo / Redo（Play 禁用） |
| Ctrl+P | 进/出 Play | | Ctrl+D | 复制选中实体 |
| Delete | 删除选中 | | Esc | 取消当前模式栈顶/清选 |
| 中键拖 | 平移视口 | | 滚轮 | 以鼠标为中心缩放 |

- **DPI**：SDL3 display scale factor → `style.ScaleAllDimensions` + FontAtlas 按缩放重建；视口渲染分辨率独立于 UI 缩放（像素风整数倍，05 §10）。
- **中文字体**：内嵌一款 OFL 授权 CJK 字体子集（候选：思源黑体 SC 子集，登记 THIRD_PARTY）；默认 ImGui 字体不含 CJK，**M4.0 冒烟即验**。
- **IME**：SDL3 文本输入事件 + `io.SetPlatformImeDataFn` 候选窗跟随光标（05 §10）。风险台账 #5 要求首周冒烟——M4.0 验收项。

### 2.4 Play 模式 UI 状态（本册决议：允许编辑，落 Play World）

- 进 Play：全部面板**切换数据源到 Play World**（Hierarchy 显示 Play 实体集；Inspector 编辑落 Play World）；Undo 栈清空并禁用置灰（05 §4）；工具栏 Play→Stop、状态栏 "▶ Playing"；橙色横幅常驻。
- Play 中编辑：照常可用（Unity 心智，为 M5 回灌留口），**改动 Stop 即丢、无确认**（横幅已常驻提示）。
- Stop：面板切回重建后的 Edit World（§3.4），选中集/脏标记恢复进 Play 前状态。
- Pause/单步：Pause 冻结 Step（渲染继续，可平移视口）；单步按钮 = 步进一帧（M4 廉价附带；若涉脚本域节奏复杂则砍单候补）。

## 3. 架构落地

### 3.1 目录与构建

```
Lemon/Editor/                      # 新增顶层目录（01 §5 既定位置）
├── CMakeLists.txt                 # 目标 lemon-editor：链接 lemon-engine + lemon-csharp
├── App/                           # EditorEntry.cpp（main）、EditorApp（主循环/帧节奏）、
│                                  #   LayoutPersistence（ImGui ini + 面板开关 → .lemon/editor/）
├── Panels/                        # IEditorPanel / PanelRegistry（编译期注册表）+ 7 面板
├── Interaction/                   # EditorModeStack / FocusArbiter / SceneAcceptsInput
│                                  #   （Editor-RPG2D 三件套 A 级拷贝，文件头注明出处）
│                                  #   Gizmo2D / Picking / Selection
├── Assets/                        # AssetDatabase（GUID/.meta/manifest 体检）
│                                  #   Importer（PNG 解码 stb_image / sprite / 图集）
│                                  #   FileWatcher（线程轮询，Luma 同款）/ Thumbnail（异步）
├── Tooling/                       # ProjectWizard（blank 模板）/ AutoSave / EditorLog
└── EditorContext.h/.cpp           # §3.3 状态模型（App 之外唯一跨面板共享状态）
```

- 依赖引入（M4.0，CPM 锁 commit + `Lemon/THIRD_PARTY.md` 登记 + [07 移植矩阵](./07-Porting-Matrix.md)加行）：**ImGui（docking 分支，MIT）**、**stb_image（公有领域）**、**CJK 字体子集（OFL）**。ImGui backend：`imgui_impl_sdl3` + `imgui_impl_vulkan`（docking 分支持有）。
- 开窗口 target 必带 RPATH（DevLog P1 约定收进 CMake 公共函数或 lemon-editor 直接继承 anim-smoke 写法）。
- 主循环 = anim-smoke 全链基线（窗口 → World.Step → Extract → SpriteBatcher → Present）外层套 ImGui 帧节奏；视口离屏纹理经 RHI 创建，**每帧渲染视口前后显式声明/恢复输出 RT**（编码规约，Prowl2D `6be927c1` 教训：UI 画进视口 RT）。

### 3.2 边界纪律（比照 Vulkan 零泄漏条款）

- **ImGui 头文件只准出现在 `Editor/`**：`lemon-engine` 不得出现任何 ImGui 引用（编辑器代码不进包的前提）。
- 依赖方向：Editor → Engine 单向（01 §1 铁律）；Engine 内为编辑器新加的钩子（§4 清单）必须是引擎语义（如 ClearViewport），不得是 ImGui 语义。
- 编辑器读写场景一律走公开 API（Scene/ComponentRegistry/SceneArchive），不走内部结构。

### 3.3 EditorContext 状态模型

```cpp
struct EditorContext {              // 唯一跨面板共享状态（Panel 间不互相 include）
    World*        editWorld;        // 被编辑数据（持有编辑 Scene）
    World*        playWorld;        // Play 沙盒；null = 非 Play
    World*        activeWorld;      // 指向 editWorld 或 playWorld（面板统一读它）
    Scene*        editScene;        // 当前 .scene 内容
    std::string   editSnapshot;     // 进 Play 前的 JSON 全量快照（SceneArchive::Save）
    Selection     selection;        // 选中集（实体 id + Meta Guid 双记，undo/往返找回）
    bool          dirty;            // 脏标记（驱动标题栏 ● / Ctrl+S / 关闭确认 / 备份）
    fs::path      scenePath;        // 打开的 .scene
    EditorModeStack modes;          // 交互模式栈（视口工具互斥）
    UndoStack     undo;             // §3.5；上限 100；Play 禁用
    AssetDatabase assets;           // GUID/manifest（M4.4）
};
```

帧节奏：非 Play = 编辑 Step（仅 Essential，供预览）+ 视口渲染；Play = 激活 World 完整 Step（固定步长）+ 双视口渲染；视口不聚焦降频（05 §9）。

### 3.4 Play 沙盒状态边界 checklist（进出全项过，缺一即 bug）

进入 Play（目标 < 0.5s，计时埋点进 Profiler）：

1. `editSnapshot = SceneArchive::Save(*editScene)`（**先固化**：未保存改动进快照但不落盘，dirty 保持）；
2. 若 dirty，横幅追加"（含未保存改动）"——不做自动落盘（决议 §8-5）；
3. 构造 playWorld/playScene ← `Load(editSnapshot)`；脚本域 Load 用户程序集（`lemon_dm_load`）+ 按 ScriptBox 装配（§4-8）；
4. 清 Undo 栈并禁用；保存选中集快照后清空（进 Play 选中集语义跨 World 无意义）；
5. 时间栈：Play World 从 t=0 开始（编辑器时间不注入游戏 Time）；
6. 输入路由切 GameView 门控（§3.6）；光标状态（隐藏/自定义光标）保存并复位；
7. 性能埋点 t0；Play 帧 = 固定步长 + vsync（编辑器不额外限帧，45fps 判据语境）。

退出 Play（目标 < 0.3s）：

1. 丢弃 playWorld（两阶段销毁 + Renderable 释放，§4-2）；脚本域 Unload（`lemon_dm_unload`）；
2. **重建** editScene ← `Load(editSnapshot)`（整体替换而非回滚——零状态泄漏的结构保证）；
3. 恢复选中集/光标/输入路由/帧节奏；Undo 保持禁用清空态；t1 埋点。

验收（可量化）：**Stop 后 `SceneArchive::Save(*editScene)` 输出与 editSnapshot 逐字节一致**——即使 Play 中做过任意编辑（"编辑落 Play World、Stop 即丢"的可执行断言，进 M4.3 自动化测试）。

### 3.5 Undo 双轨（ADR-009，对照 Prowl `Undo.cs` 边界情形 C++ 重写）

- **属性轨 PropertyRecord**：编辑提交时由 ComponentRegistry 元数据生成 before/after 字段值（同一序列化 codec）；按实体 `Meta.Guid` 找回目标（扛 destroy/recreate 往返）；连续拖拽合并（Inspector 控件 `IsItemActivated/Deactivated` 夹 BeginContinuous/EndContinuous）；多选批量编辑 = 每实体一 record 同组提交。
- **结构轨命令**：Create/Destroy Entity、SetParent、Add/RemoveComponent、AttachScript、Prefab 操作——显式逆操作数据（非 lambda 捕获重对象）。
- 栈上限 100（FIFO 淘汰）；Play 中禁用（§2.4）；剪贴板 = 单实体 `entityToJson`（05 §4），粘贴为结构命令。
- 与脏标记联动：任何 record 提交 = dirty。**Undo 本身不改 dirty 回退语义**（简化：undo 后仍 dirty，不追踪"回到已保存态"——砍单候补外，不进 v1）。

### 3.6 输入路由（SDL 语义采样子集自 M5 提前至 M4）

```
SDL 事件 ─→ ImGui（WantCaptureKeyboard/Mouse 为真 = GUI 占用）
        └→ 未占用 → 模式栈顶（Gizmo/框选/笔刷态）
                 └→ SceneAcceptsInput()（放行约定，05 §3.3）
                          └→ Play 中且 GameView 聚焦 → 语义化（键位映射表）→ World::InputState
```

- M4 提前落"键盘 + 鼠标位"语义子集（编辑器所需）；手柄/完整输入动作表仍 M5。
- 编辑相机输入（pan/zoom）在 ImGui 放行且光标位于 SceneView 内时生效，与 Play 输入互斥。
- IME 输入框聚焦时视口快捷键（W/E/R 等）屏蔽——首周 DPI/IME 冒烟的检查项。

### 3.7 C# 热重载（ADR-010 D2 路线复测 + 两分支）

- **M4.0 探针复测**（ADR-010 既定"动工前以诊断探针复测为准"）：.NET 10.0.12 实测活线程触碰 ALC 即永久 pin；复测当前 runtime，结论写回 ADR-010。
- **A 线（默认预期）：整域重建**。FileWatcher 监视用户 `.cs`/csproj → 外置 `dotnet build`（04 §6：M4 用外置 CLI，内嵌 Roslyn 后置）→ `lemon_dm_unload` 旧域（就地卸载，pin 遗留）→ `lemon_dm_load` 新程序集 → 全部 ScriptBox 重装配 + StateBag 迁移 → **泄漏计数红色告警**（Console + Profiler 常驻显示累计字节数，ADR-010 D2 口径：每次 ~百 KB 级，会话内可接受）。
- **B 线（探针转绿才启用）：ALC 换装**。新 ScriptLoadContext 加载新程序集 → StateBag 迁移 → 旧 ALC Unload + 强制 GC×2 + 泄漏断言。
- **StateBag 协议**：`OnHotReloadOut(ref StateBag)` / `OnHotReloadIn(in StateBag)`；key = 字段名，类型不匹配或缺失 = 丢弃；无法迁移默认丢弃并 Console 提示（04 §6）。字段粒度白名单（可迁移 = 值类型与引擎句柄；不可迁移 = 托管对象引用）写入 04 分册回填（§9）。
- **≤2s 判定口径**：`.cs` 文件保存时刻 → 新逻辑首次执行时刻（计时埋点进 Console 消息与 Profiler）。编译超时（>2s）提示走整域重建/重试，不阻塞编辑。
- Play 中热重载可用（05 §4 承诺）：Play 中触发 = A/B 线同流程 + Play World 内 behaviour 原位换装（StateBag 保 Play 状态）。

### 3.8 保存与崩溃恢复（本册新增决议：手动 + 自动备份）

- **手动链路（M4.1）**：dirty 置位（§3.5 联动）；Ctrl+S / 菜单 Save → `SceneArchive::Save` 落盘 `scenePath`；另存为 = 换路径；关闭/切场景/退出时有 dirty → 确认框（保存/丢弃/取消）。
- **自动备份（M4.5）**：每 5 分钟且 dirty 且**非 Play** → 写 `.lemon/autosave/<场景名>.scene`（单份滚动）；启动时检测 autosave 新于磁盘 .scene → 提示恢复（恢复 = 打开 autosave 并保持 dirty，由用户决定落盘）。
- 快照与备份共用 SceneArchive（一处优化全局受益，05 §4 注）。

### 3.9 Prefab 最小集（推荐范围 + 砍单候补首位）

- **M4 In**：Prefabs/ 目录资产（JSON 实体段格式，06 §4；**扩展名 `.prefab`，2026-09-19 定，与 Unity 一致，已登记 00 §8 命名记录**）；Hierarchy 右键"Prefab 化"= 选中实体导出 + 实例挂 `Meta.prefabId` 回链；AssetBrowser 拖入 = 实例化；Inspector 组件头栏 **Apply**（实例改动写回源）/ **Break**（断链成普通实体）/ **Revert（整体）**（回到源资产态）。
- **砍单候补 #1**：逐字段 override 蓝标 + 逐字段 Revert（05 §5 原标"v1 核心（M4）"，本册裁剪——若 M4.4 前进度富余则做，否则移 M5 并记 ADR）。overrides 深合并 schema（06 §4）不裁，仅裁 UI 粒度。
- 判据场景不依赖 Prefab（怪物由脚本 Instantiate 直建实体），故此项超期不阻塞出口。

## 4. 内核侧配套改造清单（Engine/ 内、Editor 触发）

| # | 项 | 现状与出处 | 子阶段 |
|---|---|---|---|
| 1 | Hierarchy 世界矩阵合成提取 | 组件已有、合成代码缺（M2 交付边界；渲染合成进 M4 时补环检测测试——M2 复审 §6-6） | M4.1–4.2 |
| 2 | 实体销毁 → RenderableManager 释放路径 | DevLog M3.5 "M4 需补" | M4.1 |
| 3 | Extract 增量/脏标记 | DevLog M3.5（当前全量，编辑器规模可后置优化；先接口占位） | M4.2（接口）/M5（优化） |
| 4 | 多 Scene 切换 Entity→renderable 映射失效 | DevLog M3.5 | M4.1 |
| 5 | Meta 组件补持久 Guid 字段（若 M2 未含） | ADR-009 Undo 找回 + prefabId 回链共用 | M4.1 |
| 6 | ComponentRegistry 编辑器元数据：flags 位启用 + 特性（Range/Tooltip/ColorAs）+ enum 名表 + 资产引用字段类型 | 注册表注释"M4 扩展，接口预留" | M4.1 |
| 7 | ScriptBox 序列化记脚本资产 GUID（04 §2.1 留 M4）+ 编辑器装配通路接 `ScriptHost::AttachBehaviour` | 桥运行时态不入注册表的既定决策 | M4.3–4.4 |
| 8 | C# SDK 最小增量（本册决议）：Input 语义读取（读 InputState 快照）+ `Assets.GUID→spriteId` 查询 + `Instantiate`/`Spawn` 语法糖（包装现有 SceneOps Create） | ADR-010 D4 分期落地；Audio/LemonAwait/Profiler → M5 | M4.4 |
| 9 | AssetDatabase/Importer/FileWatcher/缩略图 | 零资产系统（素材全程序化）→ 全新 | M4.4 |
| 10 | Editor Pass：网格/选择框/Gizmo 的线与矩形批（02 §4） | RHI 现无线绘制 | M4.2 |
| 11 | 离屏纹理 + 视口 RT 显式声明/恢复 | RHI CreateTexture 已备；规约新立 | M4.0–4.2 |
| 12 | SDL 键鼠语义采样子集（§3.6） | 原计划 M5，提前 | M4.3 |
| 13 | 编辑相机输入来源分离（Camera2D 同类型双实例） | 02 §3.5 既定 | M4.2 |
| 14 | 日志通道聚合（引擎日志 + 脚本异常 + 导入器消息 → 编辑器环） | 01 §7 日志已有、编辑器消费端新立 | M4.1 |

## 5. 子阶段分解（每阶段可运行验收；周循环按 08 §6）

| 阶段 | 周期 | 内容 | 可运行验收 |
|---|---|---|---|
| **M4.0 骨架与风险前置** | 1 周 | ImGui docking 引入（CPM/THIRD_PARTY/07）+ lemon-editor 空壳（anim-smoke 主循环移植）+ 默认布局 + 布局持久化；**DPI/中文字体/IME 冒烟**（风险 #5 红线）；**ADR-010 热重载探针复测**；stb_image/字体登记；离屏纹理与 RT 规约 | 空壳冷启动 < 2s；中文输入（候选窗跟随）与 DPI 缩放冒烟通过；探针结论写回 ADR-010；ImGui 不泄入 lemon-engine（编译期断言） |
| **M4.1 数据面板与场景 IO** | 1.5 周 | 面板框架（IEditorPanel/PanelRegistry）；Console（日志聚合）；Profiler（Profiles/FrameTiming/GcAllocated 同源）；Hierarchy（树/选择/父子/防环）；**Inspector 只读版**（注册表驱动 27 组件）；.scene 打开/保存/另存/脏标记/Ctrl+S/关闭确认；内核 #1/2/4/5/6/14 | 打开 anim-smoke 场景：全组件字段可见；编辑 Inspector（升级为可写）改动即时反映 SceneView；保存 roundtrip（复用 M2 测试口径）；防环测试落地 |
| **M4.2 SceneView 与编辑交互** | 1.5 周 | 视口离屏渲染 + 编辑相机（pan/zoom/F/自愈）；Gizmo 三态 + 网格吸附 + Ctrl 拖复制；拾取（PointQuery，单选 + Ctrl 多选）；Editor Pass（#10）+ 绘制层；GameView 标签页；模式栈/焦点仲裁/放行约定拷贝；Undo 属性轨首版（Transform 拖拽合并）；内核 #3(接口)/11/13 | **非对称锚点 GUI 验收清单**（文字/进度条/编号实体）全过；resize/失焦自愈；拾取与 Gizmo 操作生成 Undo 记录可回退 |
| **M4.3 Play 沙盒 + Undo 完整 + 输入路由** | 1.5 周 | Play/Stop 全 checklist（§3.4）；Pause/单步；结构轨命令全量；多选批量编辑；GameView 聚焦门控 + 语义输入子集（#12）；ScriptBox 装配通路（#7）；Play 编辑行为（§2.4） | 进/出 Play 计时 < 0.5s/0.3s（埋点可见）；**Stop 后序列化 == editSnapshot 逐字节断言**（含 Play 中编辑干扰）；Play 中键盘控制实体 + 脚本刷怪冒烟（程序化图集即可）；Undo 全边界用例（destroy 往返/多选/合并） |
| **M4.4 资产管线 + AssetBrowser + SDK 增量** | 1.5 周 | AssetDatabase（GUID/.meta/manifest/启动体检）；PNG 导入 + sprite/图集 + 缩略图异步；FileWatcher 热替换；AssetBrowser 全交互；Inspector sprite 槽/资产引用字段；Prefab 最小集（§3.9）；SDK 增量（#8） | 判据场景素材链：导入 PNG → 拖入场景 → 改文件落盘 → 编辑器即时可见；重命名资产引用不断；启动体检红字通路；脚本 Instantiate 刷怪用上导入 sprite |
| **M4.5 热重载 + 项目向导 + 终验** | 1 周 | 热重载 A/B 线（按 M4.0 探针结论）+ StateBag + 泄漏告警；新建项目向导（blank 模板/project.lemon/engineVersion/复制模板进编辑器，06 §1）；自动备份与崩溃恢复（§3.8）；Profiler GC 红字校验；判据场景终验 + 全量化汇总 | **总判据全流程录屏 + 量化表**（§6）：热重载 ≤2s（Edit 与 Play 中各测）；冷启动 <2s（二启含管线缓存）；从新建项目到刷怪场景零代码 |

**进度检查点**：第 5 周末（M4.3 应完成，否则启动砍单序）｜第 7 周末（M4.4 应完成，热重载可挪 M4.5 内消化）。名义 8 周 = 窗口上限，无额外缓冲——砍单是缓冲。

## 6. 验收清单汇总

| # | 验收项 | 判据 | 方式 | 阶段 |
|---|---|---|---|---|
| 1 | 出口总判据 | 新建项目→纯编辑器操作→走地图+刷怪场景可 Play，零代码 | 录屏 + 步骤清单复走 | M4.5 |
| 2 | 热重载 | `.cs` 保存→新逻辑执行 ≤ 2s（Edit/Play 各一例）；泄漏计数可见 | 计时埋点 | M4.5 |
| 3 | 冷启动 | < 2s（首启与二启含管线缓存各测） | 计时 | M4.0/M4.5 |
| 4 | Play 进出 | < 0.5s / < 0.3s | Profiler 埋点 | M4.3 |
| 5 | Play 零泄漏 | Stop 后序列化 == 进 Play 前快照（含 Play 中编辑） | 自动化测试 | M4.3 |
| 6 | Play 性能 | 判据场景 Play ≥ 45fps | Profiler | M4.5 |
| 7 | GC 纪律 | 示例脚本热路径托管分配 0，面板红字口径正确 | Profiler | M4.5 |
| 8 | DPI/IME | Retina 缩放正确；中文输入候选窗跟随；输入时快捷键屏蔽 | 冒烟清单 | M4.0 |
| 9 | 非对称锚点 | 文字/进度条/编号在变换/翻转/缩放下锚点不错位 | GUI 清单 | M4.2 |
| 10 | 序列化 | .scene roundtrip 不动点（M2 口径回归）+ 编辑器保存路径 | 单测 | M4.1 |
| 11 | 防环 | Hierarchy 拖拽成环拒绝（M2 复审 N6 落地） | 单测 | M4.1 |
| 12 | 资产引用稳健 | 重命名/移动资产引用不断；孤儿 meta 体检红字 | 操作复走 | M4.4 |
| 13 | 工程回归 | lemon-tests 全绿（12830 checks 基线只增不减）；sanitizer 档随改随跑 | CI | 每阶段 |

**编辑器专用冒烟（提案，Prowl2D"单测全绿≠编辑器可用"教训）**：`lemon-editor --project <路径> --smoke`——无头启动（离屏渲染、跳过交互）→ 打开项目 → 加载默认布局 → 打开默认场景 → 跑 120 帧 → 断言零异常零验证层错误 → 退出码。进 CI 与 bench 同列。09 分册回填（§9）。

## 7. 风险与砍单顺序

| 风险 | 缓解 |
|---|---|
| ImGui DPI/IME 细节坑（台账 #5） | M4.0 首周冒烟红线；中文字体 M4.0 就位 |
| 热重载 A/B 不确定 | M4.0 探针先行；A 线（整域重建）为默认可交付线，B 线纯增益 |
| MoltenVK 视口/离屏纹理行为差异 | 验证层常开；RT 显式声明规约；M4.0/M4.2 冒烟各验一次 |
| 单人范围蔓延（台账 #4） | 面板集冻结 7；In/Out 清单（§1）+ 周检查点 + 下列砍单序 |
| Play 往返隐性状态泄漏 | §3.4 checklist 全项 + 逐字节断言（验收 #5） |

**M4 内砍单序**（滞后触发，按序砍、砍前记 ADR；与 08 §4 全局砍单序衔接）：

1. Prefab 逐字段 override 高亮/逐字段 Revert（整体 Revert 顶住）；
2. GameView 宽高比模拟/分屏（单视口顶住——已在 Out，此处指若 M4.4 富余回捞的限度）；
3. 框选多选与多选批量编辑（Ctrl 点选保留）；
4. Pause 单步（纯 Play/Stop 顶住）；
5. 自动备份/崩溃恢复（纯手动保存顶住）。

## 8. 决议记录（2026-09-19 两轮讨论定稿）

| # | 议题 | 决议 | 备注 |
|---|---|---|---|
| 1 | 验收场景"走地图"形态 | **背景图 + Sprite 手摆**；瓦片系统归 M6 | TilePalette/放置状态机不进 M4 |
| 2 | M4 面板集 | **核心 7**（Hierarchy/Inspector/SceneView/GameView/AssetBrowser/Console/Profiler）+ 菜单/工具/状态栏 | 05 §2 v1 清单 12 个中 5 个后移；面板集冻结 |
| 3 | 资产管线深度 | **最小导入集**：PNG + sprite/图集 + GUID/.meta/manifest + FileWatcher | 其余六类导入器 M5+；stb_image/stb 登记第三方 |
| 4 | 规划文档形态 | **单文件本册**（05 保持总册，指针互链） | M0-Go-NoGo 先例 |
| 5 | Play 中编辑行为 | **允许编辑，落 Play World，Stop 即丢**；Undo 禁用；回灌仍 M5 | 05 §4 "v1 禁止改 Edit World"细化为本决议（不改 Edit World 语义不变） |
| 6 | 保存与恢复 | **手动 + 自动备份**（脏标记/Ctrl+S/确认框 + 5 分钟快照 `.lemon/autosave/` + 启动恢复） | 原设计空白，本册新增 |
| 7 | 窗口形态 | **单 OS 窗口 docking**；multi-viewport 与分屏/宽高比推 M5+ | 09 §8 "多窗口 M4 补"顺延，随之记录 |
| 8 | C# SDK M4 增量 | **最小**：Input 读取 + GUID→spriteId + Instantiate/Spawn | Audio/LemonAwait/Profiler API → M5（ADR-010 D4 细化） |
| 9 | Prefab 范围 | **最小集**（实例化/Apply/Break/整体 Revert）；逐字段 override 列砍单 #1 | 05 §5 "v1 核心"作范围修订记录 |
| 10 | 次级取舍默认值 | 单选 + Ctrl 多选（框选砍单候补）；Console 跳行号 M5；Undo 不追踪"回到已保存态" | 审阅可改 |
| 11 | 文件扩展名 | **与 Unity 一致**：场景 `.scene`、Prefab `.prefab`、烘焙 `.baked`（原 `.lscene`/`.lbaked` 更名；仅扩展名字符串，格式 schema 不变；`.lemon/` 目录不变） | 00 §8 已回填；文档与代码注释已同步更名（DevLog 历史条目保留原名） |
| 12 | 命名集最佳实践定稿 | 数据资产扩展名 **`.asset`**（Unity `.asset`/Godot `.tres`/UE DataAsset 三家共识，Data/ 目录 Team 表/波次/曲线用）；资产索引 **`manifest.json` 归 `.lemon/`**（生成物 gitignore，消除与 Unity `Packages/manifest.json` 撞名）；项目清单 **`Game.project` → `project.lemon`**（Godot `project.godot` 同款） | 00 §8/06 §1-2/AGENTS.md 已回填；图集描述扩展名（候选 `.spriteatlas`）M6 用到再定 |

**沿用决议索引**：ADR-005（ImGui docking + 同源双入口，01 §10）｜ADR-009（属性级 Undo/PrefabLink/门面正名）｜ADR-010（热重载移 M4、D2 整域重建、D4 SDK 分期）｜05 §1-3/§5-6/§9-10（视口即实机/面板框架/三件套/反射 Inspector/Gizmo/性能/IME）｜06 §1-2/§4（项目布局/GUID schema/Prefab 格式）｜02 §3.5/§4（相机自愈/Editor Pass）。

## 9. 交叉引用与回填清单

已随本册回填：**05**（头部指针 + §2/§4/§5 决议句）｜**08**（§0 M4 行指针）｜**00 §8**（扩展名更名 `.scene`/`.prefab`/`.baked`，决议 #11）｜**AGENTS.md**（命名行）｜其余分册（04/06/07/09/M2-复审）扩展名同步更名。

建议后续动工时回填（不阻塞本册）：

- [x] 04 §6：热重载 StateBag 字段粒度白名单与本册 §3.7 两分支引用（**M4.5 已落地**：
      白名单 = 基元值类型/枚举/Lemon.Vec2（SDK 常驻 ALC 身份）；类型不匹配/缺失 = 丢弃
      不抛异常；A 线整域重建交付（2026-09-20 探针复测仍 pin，ADR-010 修订记录），
      B 线（ALC 换装）无需代码变更——探针转绿后 `ReloadScript` 的泄漏计数自然归零；
      见 04 分册同日注记与 StateBag.cs 头注）；
- [x] 04 §2.1：ScriptBox 脚本资产 GUID 序列化格式落定后回写（**M4.4 已落地**：
      `.scene` 实体 `"script":{"guid","class"}` 成员；ScriptBox 扩 scriptGuid +
      className[24]，typeId 注册序不持久、按 className 解析；见 04 分册同日注记）；
- [x] 06 §2：M4 导入器范围（sprite 族先行）与本册 §1.2 对齐注记（**M4.4 已落地**：
      每 PNG 独立纹理页 + 一页一全幅 sprite；切片/图集打包 M5+/M6；删除 = 墓碑保号；
      见 06 分册同日注记）；**M4.5 补**：资产扫描根改为项目根（06 §1 布局），
      根级 `Prefabs/` 入索引、`Game/Scenes/Data/Builds/obj/bin` 排除，
      M4.4 旧 manifest 键（相对 Assets/）同号迁移；
- [ ] 09 §8：多窗口补课顺延 M5 的记录 + 编辑器 `--smoke` 无头冒烟条目（验收 #13 之提案）；
- [x] 00 §8：扩展名更名（`.scene`/`.prefab`/`.baked`，2026-09-19 随决议 #11 回填）与 `.lemon/autosave/` 命名记录；
- [x] ADR-010：M4.0 探针复测结论（A/B 线判定）写回（**M4.5 动工前复测，2026-09-20**：
      runtime 10.0.12 行为与 M3-2b 矩阵一致——UCO 一次性线程 OK / 域线程全形态 pin /
      pin 后重载可用；**A 线整域重建定案交付**，换装实测 1.2–1.3s（含 dotnet build）、
      每次泄漏计数 +1 红字告警；B 线挂起待 runtime 升级探针复跑；见 ADR 同日修订）；
- [ ] ADR-009：Inspector 逐字段 override 若移 M5，记 ADR 修订。
