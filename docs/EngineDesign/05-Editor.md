# Lemon 引擎设计 — 05 编辑器

> 目标：Unity 式工作流（Hierarchy / Inspector / Scene 视口 / Play-Stop），但轻量、秒开、无工程化负担；与运行时**同源双入口**，单一数据事实源，杜绝 yami 的"编辑器预览与实机两套实现"漂移。
> 骨架：Luma 编辑器面板框架（MIT 可移植）+ MoteurJV 的 Play 快照沙盒 + Prowl2D 的 Undo 属性级双轨/Prefab 模式（ADR-009）+ Editor-RPG2D 三件套交互小件（开放署名，可拷）。
> **M4 实施详细规划已定稿（2026-09-19）：[M4-Editor-Plan.md](./M4-Editor-Plan.md)**（面板集冻结/界面规格/子阶段分解/验收矩阵/砍单序）。本册保持设计总册地位。
> M4 收官后追加轮（2026-09-20 定稿）：**M4.6 可用性加固见 [M4.6-Editor-Usability-Plan.md](./M4.6-Editor-Usability-Plan.md)**（会话闭环 + 编辑效率 + 交互路径回归）。
> **M4.7 UI 精美化见 [M4.7-Editor-UI-Polish-Plan.md](./M4.7-Editor-UI-Polish-Plan.md)**（2026-09-21 P0/a/b/c/d 全批次完成：overlay 通道修复/主题 token/自绘图标/视口交互 v2；手测修复归档 [M4.7-HandTest-Fix-Summary.md](./M4.7-HandTest-Fix-Summary.md)）。

---

## 1. 编辑器与运行时的关系

- **同源双入口**（Luma `EditorEntry.cpp`/`GameEntry.cpp` 模式）：编辑器 = 运行时内核 + ImGui 编辑器层，同一份 `libLemon`；打包后的游戏 = `GameEntry` + 用户项目数据，编辑器代码完全不进包。
- **编辑器直接驱动真渲染管线**：SceneView/GameView 是引擎相机渲到纹理贴进 ImGui 视口——所见即实机（Prowl2D S8 教训："编辑器相机走了另一条管线" 这类问题从结构上排除）。
- **Play 沙盒运行在编辑器进程内的独立 World**：Edit World（被编辑的数据）与 Play World（运行实例）物理隔离。

## 2. 面板框架

```cpp
// Editor/Panels/IEditorPanel.hpp（移植 Luma IEditorPanel 形态）
struct IEditorPanel {
    virtual ~IEditorPanel() = default;
    virtual void OnAttach() {}                  // 布局恢复、订阅
    virtual void OnDetach() {}
    virtual void OnImGui(float dt) = 0;         // 每帧绘制（ImGui docking 自动布局）
    virtual void OnPlayStateChanged(PlayState) {}
    virtual std::string_view Name() const = 0;  // 窗口标题/布局持久化键
};
// PanelRegistry：反射注册，编辑器启动扫描注册表构建菜单/默认布局
```

v1 面板清单：

| 面板 | 职责 | 来源/说明 |
|---|---|---|
| **Hierarchy** | GameObject 树（内核实体，门面正名 ADR-009）、搜索、拖拽父子、右键菜单（创建/复制/删除/Prefab 化） | Unity 心智；按 Team/Tag 着色（MoteurJV） |
| **Inspector** | 选中实体的组件列表 + 每组件属性控件 + Add Component 菜单 | 反射注册表驱动（§5），**新增组件零编辑器代码** |
| **SceneView** | 编辑相机（平移/缩放/F 框选/网格吸附）、Gizmo（移动/旋转/缩放四角手柄）、笔刷态、拾取 | 视口 = 引擎相机渲到纹理 |
| **GameView** | 游戏相机预览（可分屏多相机）、宽高比模拟 | Play 时即游戏画面 |
| **AssetBrowser** | 项目资产树、缩略图、拖拽进场景、导入设置、GUID 搜索 | 06 §3 |
| **Console** | 日志分级过滤、脚本异常红字、点击跳脚本行 | — |
| **TilePalette** | 图块选择（含自动瓦片变体预览）、笔刷/矩形/洪水/取色/擦除 | §7 |
| **AnimationEditor** | 帧动画时间轴、拖帧、预览、帧事件打点 | §7 |
| **ParticleEditor** | 发射器参数实时调（Play 预览）、曲线编辑 | §7 |
| **ProfilerPanel** | F3 性能面板（§9） | yami 统计项照抄 + Tracy 桥 |
| **TeamEditor** | 势力关系矩阵编辑 | yami teams.json 的可视化 |
| **DirectorEditor** | 波次表/强度曲线编辑（表格 + 曲线） | 塔防/VS 刚需 |

布局持久化（ImGui ini + 面板开关状态进项目 `.lemon/`，不进资产目录）。

> M4 决议（2026-09-19）：**面板集冻结为核心 7**（Hierarchy/Inspector/SceneView/GameView/AssetBrowser/Console/Profiler），上表 TilePalette/AnimationEditor/ParticleEditor/TeamEditor/DirectorEditor 后移 M5/M6；**窗口形态 = 单 OS 窗口 docking**（multi-viewport 与 GameView 分屏推 M5+）。见 M4-Editor-Plan §1/§8。
> M4.7c 修订（2026-09-21）：GameView **Aspect 下拉**（Free/16:9/4:3/1:1 letterbox）已交付；分屏与 multi-viewport 维持 M5+。
> M5 批①（2026-09-22）：GameView **Game RT UI 通道**已交付——Play 时画 `World.RtUi` 定长 8 槽
> （C# `Lemon.Ui.Set(key, text, frac)` 写入；左上角文本 + frac≥0 附进度条）。M8 完整 HUD
> 前的最小形态；通道引擎级（World 持有，非编辑器私产——打包游戏同通道复用）。

## 3. 交互内核三件套（直接拷贝 Editor-RPG2D，注明出处）

### 3.1 编辑器模式栈（`src/EditorsManager.cpp`，~40 行）

```cpp
class EditorModeStack {                       // 场景编辑/瓦片笔刷/碰撞盒编辑/放置预览 互斥
    std::vector<EditorMode*> stack;
public:
    template<class T, class... A> T& Push(A&&... a);
    void Pop();
    EditorMode* Top();
    // hover/click/drag/key 只派发栈顶；出栈自动恢复下层
};
```

同构复用：游戏内暂停菜单/背包/对话也是模式栈（运行时库提供同一实现，编辑器与游戏共享心智）。

### 3.2 GUI 双指针焦点仲裁（`src/GUIManager.cpp`）

```cpp
// 每帧开头：Element_hovered = nullptr; （press 保持到释放）
// 控件 OnImGui 自认领：if (hover) Element_hovered = this; if (pressed) Element_pressed = this;
```

ImGui 下语义直接对应 `IsItemHovered()/IsItemActive()`，但保留同一套"谁占用输入"的判定函数，供视口/笔刷/游戏三方统一提问。

### 3.3 "GUI 未占用才轮到场景"放行约定（`CursorOnMap.cpp`）

```cpp
bool SceneAcceptsInput() { return Element_pressed == nullptr || Element_pressed == sceneViewport; }
```

一行判定解决"点在面板上却在场景里落笔"的编辑器经典 bug；游戏内 HUD 与玩法输入共用同一约定。

### 3.4 放置交互状态机（`CursorOnMap.cpp` 719 行的骨架，重写实现）

`Idle → HoverPreview(半透明幽灵+合法性着色) → DragPlacing(连续放置/矩形) → Confirm/Cancel`；**预览与放置共用同一份几何与规则函数**（塔防摆塔直接复用：非法格红色、占位冲突检测、花费预览）。

## 4. Play/Stop 沙盒与 Undo/Redo（Play = MoteurJV 快照方案；Undo = Prowl2D 属性级双轨，ADR-009）

| 机制 | v1（快照式） | 演进触发条件 |
|---|---|---|
| **Play** | 进入前 `EditWorld → JSON 全量快照`；Play World = 快照反序列化的独立 World | 场景 > 5 万实体或进入 > 0.5 s → 差分快照（记录 Play 期间 Edit 侧变更） |
| **Stop** | 丢弃 Play World，反序列化恢复 Edit World（零状态泄漏，绝对正确） | 同上 |
| **Undo** | **属性级双轨（ADR-009，Prowl2D 已验证方案，C++ 重写）**：属性编辑 = 修改前序列化 before 态 → 变更后 diff 生成 `PropertyRecord(before/after)`；结构操作 = action 命令（创建/删除/父子/prefab 操作，带 undo/redo lambda）；按实体持久 Guid（Meta）在 undo 时找回对象（扛住 destroy/recreate）；连续拖拽合并（BeginContinuous/EndContinuous）；上限 100，Play 模式自动禁用 | 超大结构操作（整场景导入等）退化为全场景快照（复用快照器） |
| 剪贴板/复制 | `entityToJson` 单实体序列化（复用快照器） | — |

> 快照器即引擎序列化器（03 §13）——Play/Stop 沙盒与存档/场景文件同一份代码，一处优化全局受益。属性级 Undo 的 PropertyRecord 也由同一序列化器生成（字段级粒度）。

**Play 中编辑**：v1 禁止改 Edit World（提示"Stop 后可改"）；v1.x 提供"Play 中改动选择回灌"（记录 Play 侧 diff，Stop 时勾选回灌）——这是 VS 类调参的核心爽点，列 M5 验收项。
> M4 决议（2026-09-19，细化上一段）：Play 中**允许编辑、改动只落 Play World**（Unity 心智，Stop 即丢，无确认），Edit World 语义不变；Undo 在 Play 中禁用；回灌仍属 M5。验收 = Stop 后序列化与进 Play 前快照逐字节一致。见 M4-Editor-Plan §2.4/§3.4。
>
> 保存与崩溃恢复（M4 新增决议，原设计空白）：脏标记 + Ctrl+S + 关闭确认 + 定时自动备份（`.lemon/autosave/`）与启动恢复。见 M4-Editor-Plan §3.8。

## 5. Inspector：反射注册表驱动

```cpp
// Engine/ECS/ComponentRegistry（Luma ComponentCatalog 思路）
LEMON_COMPONENT("Chase", Color::Cyan)
struct Chase { LEMON_FIELD(speed, Range{0,500}); LEMON_FIELD(aggroRange); LEMON_FIELD(keepRange);
               LEMON_FIELD(targetTeam, TeamPicker); };
```

- 注册表展开出：字段名/类型/范围/控件（DragFloat/Color/枚举下拉/Team 选择器/sprite 选择器）→ Inspector 绘制、JSON 序列化、C# Source Generator 的 struct 镜像，三处共用一份元数据。
- **可编辑数组段（M5 批②，2026-09-23）**：定长数组段（StatusEffects.active / Inventory.items / WaveDirector.waves）的元素表从只读 Text 升级为按 FieldType 裸派发的编辑控件（DragFloat/InputScalar/Checkbox），active/deactivated 汇入 M4.7d 属性轨（Undo 组件级字节快照天然覆盖）；元素级无 FieldEditorMeta——Range/枚举/prefab 反查等精细化归 M6 波次表编辑器。**WaveDirector 作者路径**：字段行改 `waveCount` 出槽位 → 表格逐格填 startTime/条目（或手改 `.scene` JSON，03 §8 形态示例）。定长标量数组（Equipment.relicIds）维持只读。
- **clip 资产槽（M5 批③）**：`FieldHint::ClipRef`（`Animator2D.clipId`）——下拉
  AssetType::Clip 全列 / AssetBrowser 拖入（drag kind 4）/ 右键清空；值 = `.clip`
  资产 GUID 低 32 位（prefabId 同款约定），反查 entry 显示 relPath。sprite 资产槽
  （AssetRef）同模式先例。
- **C# 脚本组件的 Inspector**：SDK 侧 `[ShowInInspector]` + 字段特性经元数据通道导出，编辑器绘制同样走注册表路径（C# 组件与 C++ 组件在 Inspector 里体验一致）。
- **Prefab 覆盖与头栏（ADR-009，PrefabLink 驱动，03 §2）**：改过字段蓝标 + 逐字段 Revert；组件头 Prefab 栏 Apply / Revert / Break / Select（Prowl2D PrefabUtility 模式）——v1 核心（M4）。
  > M4 范围修订（2026-09-19）：M4 做最小集（实例化/Apply/Break/整体 Revert）；**逐字段 override 蓝标列 M4 砍单候补首位**（富余则做，否则移 M5 记 ADR）。见 M4-Editor-Plan §3.9。

## 6. SceneView 视口工具

- **Gizmo**：移动（箭头/方块）、旋转、四角缩放手柄（MoteurJV 手柄 + Looper `editor/gizmo` 对照）；网格吸附（可关）；Ctrl 拖 = 复制放置。
- **拾取**：点击 → 查询层 PointQuery → 最近可选中实体（SpriteRenderer/碰撞盒命中均可）。
- **2D 专用**：像素对齐开关（像素风）、透明网格、Y 轴方向翻转预览选项、F 框选聚焦、以鼠标为中心缩放（Prowl2D S8-S11 已验证的交互清单）。
- **绘制层**：选择框/Team 调试色/触发器形状/流场可视化（箭头覆盖层，塔防调参刚需）/空间哈希网格开关。

## 7. 内容编辑器（Tilemap / 动画 / 粒子）

| 编辑器 | 核心功能 | 来源 |
|---|---|---|
| **Tilemap** | TilePalette（含自动瓦片 47 变体预览）、笔刷/矩形/椭圆/洪水/取色/擦除、碰撞标志层编辑、depth 视角检查 | 自动瓦片 Group+friends 数据结构（Editor-RPG2D `Tileset.hpp`，拷贝改造）；dirty-rect 协议（duality，MIT 拷贝）；渲染走运行时 chunk 烘焙（02 §3.3）——编辑器不另做预览路径 |
| **AnimationEditor** | 帧序列拖拽、每帧时长、Loop 模式（Once/Loop/PingPong/Random/Queue，duality SpriteAnimator 设计）、实时预览、帧事件打点 | duality 思想 + 自研 |
| **ParticleEditor** | 发射器全参数 + 曲线（颜色/尺寸/速率）实时预览、预设保存为资产 | Luma 思想；模拟即运行时粒子系统 |

## 8. 编辑器扩展性

- 面板/工具经 `EditorModule` 注册表开放（C++ 插件，动态加载 `.ledit` 模块——Luma Plugins 机制裁剪版）；
- **可视化事件树编辑器（yami 式）不在 v1**：其数据格式（指令树 JSON schema）在 06 §11 冻结预留，编辑器本体待 ARPG 品类实际开工前评估（yami 已证明它是"全项目最大成本单点"，roadmap 决策放弃的理由对我们同样成立）。

## 9. 性能面板（F3）与调试体验

- 统计项照抄 yami `main.ts:255-290` 并扩展：FPS/帧时间分解（模拟/渲染/脚本/事件 ms）、可见实体数、活跃实体数（按 Team）、投射物数、粒子数、draw call/批次数、纹理内存、托管分配 B/帧、GC 计数、池水位、事件队列长度。
- 一键场景截图/录帧（`--replay` 回放，03 §12）；Tracy 接入标记 C++/C# 双侧 zone。
- 编辑器自身开销纪律：视口不聚焦时降频渲染；资产缩略图异步生成；**编辑器冷启动 < 2 s** 是验收红线（00 文档）。

## 10. DPI、IME 与本地化

- 高 DPI：SDL3 scale factor + ImGui `style.ScaleAllDimensions`，视口渲染分辨率独立配置（像素风整数缩放）。
- 中文输入：ImGui 输入路径 + 系统 IME（SDL3 文本输入事件，候选窗跟随光标）——编辑器/对话调试通用。
- 编辑器 UI 本地化：字符串表（zh-CN 首发，en 随后；yami 5 语言方案的子集，架构预留）。

## 11. 移植对照速查（本册）

| 项 | 源 | 处置 |
|---|---|---|
| IEditorPanel + 面板组织 + ComponentCatalog | Luma `Application/Editor/` | 面板清单与框架移植（MIT） |
| Play 快照沙盒 / 全场景快照器 | MoteurJV `examples/05_editor/main.cpp` | 方案直接借鉴，代码重写（量小）；快照器降级为 Play 沙盒专用（ADR-009） |
| Undo 属性级双轨 + Guid 找回 + 连续合并 | Prowl2D `Prowl.Editor/Core/Undo.cs`（980 行，自有已验证） | 方案移植（§4 主方案），C++ 重写 |
| Prefab 头栏 Apply/Revert/Break + override 高亮 | Prowl2D `Prowl.Editor/Prefabs/PrefabUtility{.Api,.Overrides}.cs`、`Prowl.Runtime/GameObject/PrefabLink.cs` | 方案移植（映射到 ECS PrefabLink，03 §2） |
| 模式栈 / 双指针仲裁 / 放行约定 / 放置状态机 | Editor-RPG2D `EditorsManager.cpp`、`GUIManager.cpp`、`CursorOnMap.cpp` | **代码拷贝改造，文件头注明出处（开放署名）** |
| 自动瓦片 Group+friends | Editor-RPG2D `Tileset.hpp` | 数据结构拷贝 + 坐标表外置数据文件 |
| Tilemap dirty-rect / SpriteAnimator LoopMode | duality `Source/Plugins/Tilemaps/`、`SpriteAnimator` | MIT 拷贝（保留归属注释） |
| F3 统计项清单 | yami `main.ts:255-290` | 统计项照抄（事实清单，非代码） |
| Gizmo/编辑器相机交互 | Looper `editor/gizmo`、Prowl2D S8-S11 交互清单 | 对照实现 |
