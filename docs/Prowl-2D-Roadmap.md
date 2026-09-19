# Prowl2D：基于 Prowl 的纯 2D 游戏引擎重构路线图

> ⚠️ **已归档（2026-09-18）**：主线已转向新引擎 Harvest（C++ + Vulkan + C# 脚本 + ECS 编辑器），设计见 [EngineDesign/](./EngineDesign/00-Executive-Summary.md)。本路线的 M0 已完成并保留为参考；其结论（图集/合批先行、品类优先级、压测场景设计、"GUI 级验收"纪律）已并入新设计文档。若新引擎 M0 技术验证（见 EngineDesign/08）No-Go，则按该文档的回退预案重启本路线 M1。

> 目标玩法类型：**塔防（TD）+ 吸血鬼幸存者类（VS-like）**
> 状态：方案设计阶段（2026-09），未开始编码
> 工程：`GameEngine/Prowl2D/`（独立工程，原版 `Prowl/` 保留作只读参照，绝不就地修改）

---

## 0. 已确认的决策记录

| 决策点 | 结论 |
|---|---|
| 工程组织 | 独立新工程 `Prowl2D/`（git clone 自 Prowl，保留完整历史，fork 基线标签 `fork-baseline`） |
| 3D 代码处置 | **分层精简**：先加体验层和 2D 管线（不删代码），M2 验证 2D 路径后再分波删除 |
| 2D 物理 | **轻量查询层**（overlap/raycast/触发器，不做刚体动力学） |
| 首个玩法验证 | **吸血鬼幸存者-like 先行**（图集/合批/动画/粒子），塔防随后（Tilemap/寻路） |
| duality 参照 | MIT 协议；本地暂无（用户稍后手动下载），按远程调研结论先行 |
| 2DGameEngine | **无许可证** → 仅借鉴算法思想，**禁止拷贝代码** |
| Float3 Transform | **永不改为 2D transform**：2D = XY 平面 + SortingLayer（Unity 2D 同款做法） |

---

## 1. 现状调研结论：可行，地基比预期好

### 1.1 Prowl 已具备（直接复用）

- **Sprite 资产系统**（`Runtime/Resources/Sprite.cs`）：pivot / PixelsPerUnit / 9-slice border / 二级贴图（`_NormalMap` 通道已预留，为 2D 光照埋点）/ tight mesh 轮廓；**`Sprite.PhysicsShape` 多边形碰撞轮廓已为 2D 碰撞体预留（未接线）**
- **完整精灵导入栈**：`TextureImporter`（自动/网格/等距切片、切片 GUID 稳定匹配）+ SpriteEditor 窗口（`Editor/GUI/SpriteEditor/`）
- **SpriteRenderer**（`Runtime/Components/SpriteRenderer.cs`）：走通用 mesh 管线（`OnRenderCollect` → `MeshRenderable`），支持 flip/材质覆盖/SortingOrder（Z 偏移模拟）
- **Camera 正交投影**：`ProjectionType.Orthographic` + `OrthographicSize` 完整可用；编辑器 `EditorCamera` 已支持 ortho 切换 + 滚轮缩放
- **完整保留模式 UI**（`Runtime/Components/UI/`）：GameCanvas（ScreenSpaceOverlay/WorldSpace）、RectTransform 锚点系统、Button/Slider/Scrollbar/ScrollRect/InputField/Dropdown、EventSystem + UIRaycaster、布局组、RectMask/CanvasGroup、SDF 文本（Prowl.Scribe）
- **粒子系统**（`Runtime/Components/ParticleSystem/`）：instanced quad + UV flipbook + 模块栈（发射/生命周期曲线/风/碰撞/光照），3D 向但设计可直接 2D 适配
- **序列化与资产管线**：Prowl.Echo + `AssetRef<T>` + 子资产机制（Sprite 即 Texture2D 的子资产）——**新资产类型（图集/Tilemap/动画片段）即插即用**
- **集成模式清晰**：新组件 = 继承 `MonoBehaviour` + `[AddComponentMenu]`（Inspector/序列化/生命周期全自动）；新子系统 = 仿 `Scene.Physics` 挂 `Scene.FixedUpdate()`；渲染 = `OnRenderCollect` → `IRenderable`

### 1.2 缺失清单

2D 物理、Tilemap、2D 动画（flipbook/状态机）、**图集打包、Sprite 合批、命名排序层**、2D 光照、网格寻路、UI Toggle/RawImage、GameObject「2D Object」菜单、SceneView 2D 模式。

### 1.3 删除友好特性（为精简层3兜底）

- 编辑器注册全为**反射扫描**（`EditorRegistries.cs`）：删类无需手动注销
- 序列化遇缺失组件回退 `MissingMonobehaviour`：旧场景不崩

### 1.4 参考引擎评估

| 引擎 | 价值 | 许可 | 结论 |
|---|---|---|---|
| **duality** | Tilemap 插件（dirty-rect 协议/gzip 版本化数据/32×32 扇区碰撞/编辑器笔刷）、SpriteAnimator（LoopMode/自定义帧序列）、vendored Farseer、DynamicLighting 示例 | MIT | **首选移植来源**，保留归属注释 |
| 2DGameEngine | 自研冲量求解器（Randy Gaul 教程风格）、编辑器相机交互 | **无许可证** | 只读思路；算法直接看 Randy Gaul 公开教程即可 |

---

## 2. 3D 精简：分层策略

### 层1（立即，随 M0）——「2D 模式」体验层，只加不删

- 编辑器项目级 2D 开关：过滤 AddComponentMenu / GameObject 菜单的 3D 项
- SceneView 2D 模式：锁 XY 平面 + 像素对齐 + F 框选 + 以鼠标为中心缩放
- 新建场景模板：默认正交相机、无灯光
- 隐藏 EnvironmentPanel 等 3D 面板
- 附带运行时小优化：`Scene.Physics` 懒加载门控（存在 3D 刚体才创建/步进；`Runtime/Resources/Scene.cs:221/929` 两处）
- **工作量：小型**

### 层2（M0–M1）——渲染层「事实移除」

- `RenderPipeline2D` 赋给相机作为 2D 项目默认管线：正交收集 → Sprite 合批 → UI
- 完全跳过 MRT prepass / 阴影图 / skybox / TAA / 运动矢量
- 3D 管线代码仍在，但 2D 路径不再经过
- **工作量：中小型**

### 层3（M2 验证 2D 路径后）——真删除，按隔离度分波

每波以「全解决方案编译绿」为门槛；`git diff fork-baseline` 随时对照原版。

- **波①（隔离）**：Recast 导航（`Runtime/Navigation/` + Prowl.Recast）→ Terrain → CharacterController / WheelCollider / 3D 关节（`Physics/Constraints/`）→ 骨骼 Animation / blendshape → ModelImporter（glTF/FBX）→ 3D 示例与相关测试；修复 EnvironmentPanel、`DefaultGameObjectCreators`、自定义编辑器等编译引用
- **波②（渲染内部）**：DefaultRenderPipeline / SceneLightSystem / ShadowAtlas / LightBVH / 3D 光源组件 / skybox / prepass 着色器（前提：`RenderPipeline2D` 已接替）
- **波③（物理，最后）**：Jitter2 + PhysicsWorld + Rigidbody3D + 全部 3D Collider —— **必须等 Physics2D 查询层建好且粒子 CollisionModule 迁移完成**（依赖顺序，非保守）

### 永不删除（2D 承重墙）

Graphics/命令缓冲层、Mesh/Material/Shader 体系、`DrawRenderables` 批处理 + GPU instancing、**Float3 Transform**、序列化/资产库、UI、音频、输入、数学库。

---

## 3. 里程碑总览

| 里程碑 | 内容 | 验证目标 | 预估 |
|---|---|---|---|
| **M-1** | ✅ 建 `Prowl2D` 独立工程（clone + fork-baseline 标签 + 编译验证） | 独立基线，原版零改动 | 已完成 |
| **M0** | 图集 + 合批 + 排序层 + Camera2D + `RenderPipeline2D` + 编辑器 2D 模式（精简层1+2） | 数百实体同屏性能 | 3–4 周 |
| **M1** | 帧动画 + 2D 粒子 + UI 补缺 | **VS 玩法 demo** | 2 周 |
| **M2** | Tilemap + 网格寻路 + 笔刷工具 | **TD 玩法 demo** | 4–6 周 |
| **M2 后** | 精简层3 删除波①② | 代码库瘦身 | 1–2 周 |
| **M3** | 2D 光照 + blob 阴影 | 视觉氛围 | 2 周 |
| **M4** | Physics2D 查询层 → 粒子迁移 → 删除波③ | 后续项目 | 2–3 周 |

---

## 4. M0 渲染地基（VS 性能硬前提，最优先）

> 现状每个 SpriteRenderer 一个 draw call、SortingOrder 用 Z 偏移模拟——VS 数百敌人同屏必崩。这是所有后续系统的地基。

1. **`SpriteAtlas` 资产 + 编辑器打包器**（MaxRects 算法），运行时 Sprite→图集 UV 重定向 — 中型
2. **Sprite 合批**：`SpriteBatchRenderer` 收集同图集 sprite → 单个 `InstancedMeshRenderable`（per-instance 矩阵 + 颜色 + UV rect），一图集一 draw call — 中型（`DrawRenderables` 材质哈希批处理 + GPU instancing 已支持）
3. **SortingLayer**：`TagLayerManager` 增命名排序层列表，(layer, order) 进 sort key，替换 Z 偏移 hack — 小型
4. **`RenderPipeline2D : RenderPipeline`**：正交收集 → 合批 → UI（兼作精简层2）— 中小型
5. **Camera2D 辅助**：pixel-perfect、`ScreenToWorldPoint`、跟随 + 边界钳制 — 小型
6. **VS 配套**：PrefabPool 对象池工具类、敌人分离（steering separation）示例脚本 — 小型

---

## 5. 七大专项设计

### 5.1 2D 物理 — 轻量查询层（P4 / M4）

**新建类**：
- `Runtime/Physics2D/PhysicsWorld2D.cs` — 挂 `Scene.Physics2D`，`Scene.FixedUpdate()` 步进（仿 `Physics.Update()` try/catch 模式）；空间哈希 broadphase；API：`OverlapCircle/OverlapBox/OverlapPoint`、`Raycast2D/RaycastAll2D`（+ `QueryFilter`/LayerMask）；触发器登记 + 事件缓冲
- `Components/Physics2D/Collider2D.cs` — 抽象基类（OnEnable/OnDisable 注册模式，仿 3D `Collider`）
- `CircleCollider2D` / `BoxCollider2D` / `PolygonCollider2D`（吃 `Sprite.PhysicsShape` 轮廓）/ `EdgeCollider2D`
- `Rigidbody2D` — 简化版：Kinematic/Static + 可选简单积分（速度 + 重力），**无求解器**
- `TriggerVolume2D`

**集成点**：`SceneDispatcher` 新增 `OnTriggerEnter2D/Stay2D/Exit2D` 回调位；TilemapCollider2D 输出静态矩形；LayerMask 复用 `TagLayerManager`。

**工作量：中型** ｜ **优先级：P4**。VS 用分离力脚本、TD 用 overlap/raycast 已覆盖需求；将来要堆叠/关节再评估 Farseer 移植。精简波③删 Jitter2 前必须先完成本项 + 粒子 CollisionModule 迁移。

### 5.2 Tilemap（P2 / M2）— TD 核心

**新建类**：
- `Components/Tilemap/Grid.cs` — 网格坐标系（cell size + 矩形/等距布局）
- `Components/Tilemap/Tilemap.cs` — 纯数据组件，持有 `TilemapData`（flat 数组 + **dirty-rect 更新协议** `BeginUpdate/EndUpdate` + 变更事件 + gzip 版本化序列化——照搬 duality 设计）
- `Resources/Tile.cs` — struct：BaseIndex / DepthOffset / AutoTileCon 连接位掩码
- `Resources/Tileset.cs` + 编译器 — 资产：烘焙图集 + 每 tile 碰撞标志 + depth 信息 + autotile 变体表
- `Components/Tilemap/TilemapRenderer.cs` — 32×32 chunk mesh，只重建脏区；DepthOffset 平/斜模式；`MeshRenderable` 输出
- `Components/Tilemap/TilemapCollider2D.cs` — 从 tile 碰撞标志生成合并静态矩形，供 Physics2D 与寻路
- **网格寻路（建议纳入的第 8 项，TD 必需）**：A* + FlowField 流场（流场对怪物群更优）

**编辑器**：TilePalette 窗口（选 tile）、SceneView 笔刷（笔刷/矩形/椭圆/洪水填充/取色/擦除，扩 `SceneToolManager`）、Tileset 编辑器。

**集成点**：EngineObject + AssetCreateMenu + Echo；排序层参与 sort key。

**工作量：运行时中型 + 编辑器中型 + 寻路小型 = 大型**

### 5.3 2D 动画（P0 / M1）— VS 角色动画必需

**新建类**：
- `Resources/AnimationClip2D.cs` — 帧列表（`AssetRef<Sprite>[]` 或图集索引区间）、每帧时长、Loop 模式（Once/Loop/PingPong/Random/Queue）、动画帧事件（帧号 → 回调名）
- `Components/2D/SpriteAnimator.cs` — 播放/暂停/速度/当前帧，驱动 `SpriteRenderer.Sprite`；支持自定义帧序列；可选平滑帧混合（duality SpriteIndexBlend 思路）
- 后期可选 `Animator2D` 简化状态机：状态 = clip、转移 = 参数条件（速度/触发器）。**不做** Unity Animator 级复杂度

**集成点**：SpriteRenderer / Echo 序列化 / PropertyGrid Inspector 全现成，零额外接线。

**工作量：flipbook 小型（3–5 天），状态机中型**

### 5.4 2D 光照（P3 / M3）

**新建类**：
- `Components/Lights/Light2D.cs` — 类型 Global/Point/Spot；颜色/强度/衰减/混合模式（加法/乘法）
- `LitSprite.shader` — 在 `Default/Sprite` 基础上加法线光照项（`Sprite.SecondaryTextures._NormalMap` 通道现成）
- 渲染集成：在 `RenderPipeline2D` 内做 法线 pass → RT → 光源合成
- 阴影：blob shadow（椭圆暗斑）小中型可做；**几何硬阴影大型、建议不做**（TD/VS 很少需要）

**工作量：光照中型；优先级：P3（视觉增强，非阻塞）**

### 5.5 2D 粒子（P1 / M1）— 适配而非新写

**改造 `ParticleSystemComponent`**：
- SimulationSpace 增加 XY 平面模式（z 锁 0，重力/风力投影到平面）
- 正交相机下 billboard 已等效 2D；另加 FixedFacing 固定轴向（像素风常用）
- CollisionModule 加 None/2D 模式（M4 后接 Physics2D）
- 若适配成本高，备选新建 `ParticleSystem2D` 轻量组件（无 3D 模块开销）

**工作量：小型（2–4 天）**

### 5.6 2D UI（P1）— 缺口很小

- 补 `UIToggle`（复用 Selectable 框架）、`UIRawImage`（直接贴 Texture2D，小地图用）— 小型
- 血条/经验条：现有 `UIImage.AddFilled`（径向/水平填充）已够
- SortingLayer 与 Canvas.SortOrder 打通
- 产出示例 prefab：TD 建造/升级面板、VS 升级三选一卡片、经验条、小地图

### 5.7 2D 工具链（随各里程碑）

- `GameObject/2D Object` 创建菜单（扩 `DefaultGameObjectCreators`）— 小型（M0）
- SceneView「2D 模式」= 精简层1（M0）
- Tile 笔刷 + TilePalette + Tileset 编辑器（M2）
- SpriteAnimator 预览窗（后期）、SortingLayer 编辑 UI（M0/M1）

---

## 6. 关键风险

1. **性能是 VS 第一风险**：M0 图集 + 合批必须最先做，否则数百实体不可行
2. **层3 删除的隐性耦合**：面板/自定义编辑器/测试的编译引用需逐个修复；反射注册保证运行时安全，但编译期引用仍要扫；每波后全解决方案编译绿作门槛
3. **合规**：duality MIT（保留归属注释）；2DGameEngine 无许可证只读思路
4. **架构原则**：不引入 DI/服务注册器；2D 系统一律仿现有 per-Scene 子系统 + MonoBehaviour 模式；默认渲染管线在精简层3 之前不动，2D 走 `Camera.Pipeline` 替换

---

## 7. 关键文件速查（调研所得，动工时用）

| 关注点 | 路径（相对 Prowl2D/） |
|---|---|
| Sprite 资产（含 PhysicsShape 预留） | `Prowl.Runtime/Resources/Sprite.cs` |
| SpriteRenderer | `Prowl.Runtime/Components/SpriteRenderer.cs` |
| 渲染管线基类（合批/排序/instancing） | `Prowl.Runtime/Rendering/RenderPipeline.cs` |
| 默认 3D 管线（层2 替换对象） | `Prowl.Runtime/Rendering/DefaultRenderPipeline.cs` |
| instanced 渲染（合批参照） | `Prowl.Runtime/InstancedMeshRenderable.cs` |
| 3D 物理世界（仿建 Physics2D 的模板） | `Prowl.Runtime/Physics/PhysicsWorld.cs` |
| Scene 步进点（Physics 懒加载门控处） | `Prowl.Runtime/Resources/Scene.cs:221,929` |
| 组件回调分发（加 2D 触发事件位） | `Prowl.Runtime/GameObject/SceneDispatcher.cs` |
| 标签/层（SortingLayer 扩展点） | `Prowl.Runtime/GameObject/TagLayerManager.cs` |
| 相机（正交已支持） | `Prowl.Runtime/Components/Camera.cs` |
| UI 体系 | `Prowl.Runtime/Components/UI/` |
| 粒子系统 | `Prowl.Runtime/Components/ParticleSystem/` |
| 编辑器扩展注册（反射扫描） | `Prowl.Editor/EditorRegistries.cs` |
| GameObject 创建菜单 | `Prowl.Editor/GUI/DefaultGameObjectCreators.cs` |
| 编辑器相机（ortho 已支持） | `Prowl.Editor/GUI/SceneView/EditorCamera.cs` |
| 场景工具管理器（笔刷扩展点） | `Prowl.Editor/GUI/SceneView/SceneToolManager.cs` |
| 纹理导入（切片栈） | `Prowl.Editor/AssetsDatabase/Importers/TextureImporter.cs` |
| 精灵编辑器窗口 | `Prowl.Editor/GUI/SpriteEditor/SpriteEditorWindow.cs` |

---

## 8. Prowl2D 工程信息

- 位置：`GameEngine/Prowl2D/`（git clone 自 `Prowl/`，保留完整提交历史）
- 基线：`a85619c9`（updated Prowl Packages to v3.5.0），标签 **`fork-baseline`** —— 此后所有改动可 `git diff fork-baseline` 对照原版
- 远程：`prowl-reference` → 本地 `Prowl/`（参照）；`upstream` → `https://github.com/ProwlEngine/Prowl.git`（官方上游）
- 原版 `Prowl/` 保持零改动

## 9. 下一步

1. **M0 详细实施计划已出：[M0-Implementation-Plan.md](./M0-Implementation-Plan.md)**（含代码级依据、7 项设计决策、S1–S7 工作分解、验收标准）
2. duality 已下载到本地（`GameEngine/duality/`），M2 动工前对照细化 Tilemap 移植细节（其 dirty-rect 协议 / gzip 版本化序列化 / 32×32 扇区碰撞的落点已在调研中确认）
