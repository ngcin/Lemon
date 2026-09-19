# M0 详细实施计划 — 2D 渲染地基

> ⚠️ **已归档（2026-09-18）**：主线已转向新引擎 Harvest（C++ + Vulkan + C# 脚本 + ECS 编辑器），设计见 [EngineDesign/](./EngineDesign/00-Executive-Summary.md)。Prowl2D 仓库保留为参考实现（回退保险见 EngineDesign/08 的 M0 Go/No-Go）。本文件仅作历史记录，不再更新。

> 上游文档：[Prowl-2D-Roadmap.md](./Prowl-2D-Roadmap.md)（总体路线图）
> 范围：SpriteAtlas 图集 → Sprite 合批（GPU instancing）→ SortingLayer → RenderPipeline2D → Camera2D → 编辑器 2D 模式 → VS 配套工具
> 工程根：`GameEngine/Prowl2D/`。下文所有路径相对该根。
> **状态：S1–S7 已全部实现并提交（提交 S1 `c0cd5a0c` … S7 `b63f380e`），全解决方案编译绿，Runtime.Test 1095/1095 通过。** 需要编辑器 GUI 的人工验收项见文末「实施状态与人工验收清单」。

---

## 10. 实施状态与人工验收清单（2026-09 完成）

### 已提交

| 步骤 | 提交 | 内容 |
|---|---|---|
| S1 | `c0cd5a0c` | SortingLayers 注册表 + SpriteRenderer.SortingLayer + 设置页编辑 + 5 个单元测试 |
| S2 | `f9c15036` | SpriteAtlas 资产 + MaxRects 打包器（5 个测试）+ AtlasPacker（Aperture CPU 解码/gutter 外扩/.packedtex 资产）+ 资产编辑器 + 自动注册 |
| S3 | `c381f49b` | Sprite.shader 实例 UV 分支 + SpriteBatchContext（画家序稳定排序）+ SpriteBatchRenderable（零稳态分配池）+ SpriteRenderer 分流（3 个测试） |
| S4 | `8eee5b55` | UIRenderPass 提取（行为等价重构）+ RenderPipeline2D（正交收集→合批→杂项→UI，无 prepass/阴影/skybox） |
| S5 | `64f1ea75` | Camera2D（正交+2D 管线、像素完美、跟随+边界钳制、ScreenToWorldPoint） |
| S6 | `8ae57793` | Settings2D、SceneView「2D」按钮+状态持久化、EditorCamera 2D 锁、GameObject/2D Object 菜单、2D 场景模板、Add Component 过滤、**Scene.Physics 懒加载门控** |
| S7 | `b63f380e` | PrefabPool + SteeringSeparation（O(n²) 基线）+ Samples/2DStressTest（500 精灵压测） |

### 与计划的偏差（已评审接受）

1. **RenderPipeline2D 暂不支持 ImageEffect**（原计划"透传"）——效果管线深耦合 Default 管线的中间 RT，M0 无 2D 效果需求，记入 M3 光照时一并处理。
2. **2D 场景视图无编辑器网格**——网格注入是 Default 管线私有逻辑；XY 平面网格随 M2 Tilemap 场景工具补。
3. **SpriteRenderer 拾取与拖放平面**（计划中标注"可延后"两项）未做，2D 场景点选走 Transform 兜底。

### 需要编辑器 GUI 的人工验收（对应 A1–A5）

- **A1/A2**：打开编辑器 → 运行 `Samples/2DStressTest` → GameView 统计面板确认 DrawCalls ≈ 2（instanced）、60fps；精灵层叠顺序符合 (Layer, Order) 预期
- **A3**：建一个 .spriteatlas → 拖入若干 Sprite → Pack → 场景中的精灵渲染与原图一致；重打包后引用不失效
- **A5**：Project Settings → 2D → 开启 → File/New Scene 应为正交相机；GameObject/2D Object 三项可用；SceneView 2D 按钮锁定视图
- **A6**：任意 3D 样例（SimpleCube 等）默认管线下渲染正常（回归由 UI 提取重构保证，1095 测试通过）

---

## 1. 目标与验收标准

| # | 验收项 | 量化标准 |
|---|---|---|
| A1 | **合批性能** | 500 个动态 Sprite（移动+换色）@ 1080p 60fps；GameView 统计面板 DrawCalls ≤ 图集数+2（UI/清屏），`RenderStats.InstancedDrawCalls` 生效 |
| A2 | **绘制顺序正确** | 跨图集重叠 Sprite 的画家序正确（layer→order→稳定序），半透明叠加无闪烁 |
| A3 | **图集可用** | 打包后 Sprite 渲染像素与原图一致（含 gutter 防渗色）；重打包后 Sprite GUID/引用不失效 |
| A4 | **回退无损** | tight mesh Sprite、材质覆盖、SecondaryTextures Sprite 走原有逐实例路径，行为与基线一致 |
| A5 | **编辑器 2D 模式** | 2D 项目开关生效：新建场景为正交相机无灯光、GameObject/2D Object 菜单可用、SceneView 2D 锁定导航（MMB 平移+滚轮缩放+F 框选） |
| A6 | **3D 路径零回归** | 原有 3D 示例（SimpleCube/PhysicsCubes 等）在默认管线下渲染/运行不变 |

## 2. 代码级技术依据（已实证，含行号）

深读得出的关键事实，设计全部建立在其上：

1. **GPU instancing 端到端已存在**：`InstanceData`（`Rendering/InstanceData.cs:15`，矩阵 16 float + Color 4 + **CustomData 4 float** = 96B）；`Mesh.EnsureInstanceVAO`（`Resources/Mesh.cs:664`，语义 8–13、divisor 1、1.5 倍增长+延迟释放）；`RenderPipeline.DrawInstancedRenderablePass`（`Rendering/RenderPipeline.cs:760`，设 `GPU_INSTANCING` 关键字 → `cmd.UpdateBuffer` → `DrawIndexedInstanced`）。`IRenderable.GetRenderingData` 返回非空 `instanceData` 即自动路由到该路径（RenderPipeline.cs:522-557）。
2. **唯一硬缺口 = 每实例 UV**：`Sprite.shader` 顶点阶段用 `vertexTexCoord0 * _Tiling + _Offset`（**批级 uniform**，`Assets/Defaults/Sprite.shader` vertex main，已核对原文）；而 `VertexAttributes.glsl:52` 已声明 `layout(location=13) in vec4 instanceCustomData`，`:285` 已有 `GetInstanceCustomData()`——**顶点槽位和取值函数都是现成的，着色器没用而已**。`GetInstanceColor()`（instanceColor×vertexColor）已被 Sprite.shader 使用且工作正常。
3. **渲染公共设施全部 public 可复用**：`CollectRenderables / CullRenderables / SortRenderables / DrawRenderables / AssignCameraMatrices / SetupGlobalUniforms`（`RenderPipeline.cs`）。**但 `RenderUIQueue`（DefaultRenderPipeline.cs:492）、`RenderUIWorld`(:544)、`DrawUIItems`(:579)、`BuildScreenOrtho`(:618) 均为 private**——自定管线复用 UI 队列需要小重构（D6）。
4. **现有排序机制**：`IRenderable.GetPosition()` + 距离排序（BackToFront），`SortingOrder` 用模型矩阵 Z 偏移模拟（`SpriteRenderer.cs:83-85`，0.0001/单位）；批的次序由 pass 标签偏移（`"Transparent+N"`）决定（RenderPipeline.cs:584）。**instanced renderable 只有一个 sortPosition，批内顺序 = 实例数组顺序**——批内必须自排序。
5. **编辑器扩展点全部反射注册**（`EditorRegistries.cs`）：`[CreateAssetMenu]`→资产创建菜单（:380-402）、`[MenuItem]`→菜单（含 `UnregisterByPrefix`:101-105 可隐藏 "GameObject/3D Object"）、`[ProjectSettings]`→设置页（:327-348，`GetSettings<T>()`:635）、`GatherComponents`（GameObjectInspector.cs:1564-1602，无分类过滤钩子需加）。**GameView 统计面板已显示 DrawCalls/Batches**（GameViewPanel.cs:324-415，数据源 `RenderStats`）——合批收益直接可视化，无需新建工具。
6. **EditorCamera 已具备 2D 导航原语**：ortho 滚轮缩放（:385-390）、MMB 平移（:447-457）、F 框选（:517-521 `FocusSelection`）、`ToggleProjection`（:72-92）。需加的只是「锁」。
7. **duality 参照结论**：核心**没有**任何打包器（已 grep 确认无 maxrects/skyline/shelf/binpack）——MaxRects 需自研；但其 `TilesetCompiler.FillTileSpacing` 的**边缘像素外扩（gutter/extrusion）防渗色**技术和 `SpriteAnimator` 帧数学可直接借鉴（后者记入 M1）。duality 的双队列排序/零 GC 池化设计作为批处理器的参照范式。

## 3. 总体设计决策

### D1：合批 = 共享单位四边形 Mesh + GPU Instancing + instanceCustomData 装 UV
- 全体批处理 Sprite 共用一个单位四边形 `Mesh`（4 顶点 UV(0..1) + Colors32=白 + 6 索引）；
- 每实例：`ModelRow0..3` = TRS 矩阵（含翻转=基镜像，`Cull Off` 下无需绕序修复）+ `Color` = tint + `CustomData = (u0, v0, du, dv)` 即图集子矩形；
- **tight mesh Sprite 不进批**（顶点数不定，无法共享 quad），自动回退现有 `MeshRenderable` 路径（满足 A4）。

### D2：Sprite.shader 单着色器双路径（改 2 行，不加新着色器）
```glsl
#ifdef GPU_INSTANCING
    vec4 cd = GetInstanceCustomData();               // xy=uv偏移 zw=uv缩放
    texCoord0 = vertexTexCoord0 * cd.zw + cd.xy;
#else
    texCoord0 = vertexTexCoord0 * _Tiling + _Offset;
#endif
```
- 非实例路径（现有 SpriteRenderer、材质覆盖）字节不变；实例路径 UV 来自每实例数据；
- 批与非批共用同一个 `DefaultShader.Sprite` / `DefaultSpriteMaterial`，材质状态哈希天然同组，`DrawRenderables` 的批分组直接生效；
- `_Tiling/_Offset` 语义在实例路径下保留给「整图特例」（CustomData 默认 0 时退化，不冲突）。

### D3：合批只在 RenderPipeline2D 中激活，DefaultRenderPipeline 零改动
- `SpriteRenderer.OnRenderCollect(camera, …)` 内判断：`camera.Pipeline is RenderPipeline2D p2d && p2d.SpriteBatching` → 向 `SpriteBatchContext.Current` 注册（不发自己的 renderable）；否则走现有逐实例路径；
- `RenderPipeline2D.Render()` 在 `CollectRenderables` 前后 `SpriteBatchContext.Begin()/Flush()`；
- 收益：3D 场景零风险（A6），2D 场景全收益；无跨管线钩子污染。

### D4：排序 = SortingLayer/SortingOrder 全局画家序，pipeline2D 显式执行
- 运行时新增 `SortingLayers`（静态，仿 `TagLayerManager`：`List<string>`，默认 "Default"，工程设置页编辑）；
- `SpriteRenderer` 增 `public int SortingLayer;`（索引）+ 保留 `SortingOrder`；
- **批处理路径**：pipeline2D 对全部 sprite item 按 `(SortingLayer, SortingOrder, 提交序)` 全局稳定排序 → 按图集分组连续 emit（排序分组用「连续段」而非全局哈希分组，保证跨图集穿插时的画家序正确，满足 A2）；
- **回退路径**（3D 场景/非批）：延续 Z 偏移，扩展为 `z = SortingLayer * LayerBias + SortingOrder * SortBias`（LayerBias=0.01，总量 <1 世界单位；文档标注正交相机 near/far 需包络 ±1）。

### D5：SpriteAtlas = 编辑器时打包资产，运行时只查表；MaxRects 自研
- **运行时绝不打包**：编辑器侧从源贴图（importer 原始文件）取像素 → MaxRects（BSSF/BAF 可选）→ 写出 PackedTexture 资产 + 每 Sprite 的 uvRect 表；
- **gutter 防渗色**：借鉴 duality `TilesetCompiler.FillTileSpacing`，边缘像素向外复制 1px（设置项，默认开）；
- **引用稳定**：entries 以 Sprite 资产 GUID 为键，重打包 GUID 不变（A3）；
- 运行时解析：`SpriteAtlasRegistry`（静态）登记 已加载图集 的 `GUID→uvRect+page`；`SpriteBatchContext` 解析 Sprite 时先查 registry，命中→图集 UV+图集纹理，未命中→原纹理路径（未入图集的 Sprite 也能工作，按原纹理自动成组）；
- SecondaryTextures（法线）图集化**不在 M0**（记入 M3 光照一起做），带 SecondaryTextures 的 Sprite 暂不进批（回退路径，A4）。

### D6：UI 队列共享 = 小重构，不复制代码
- 将 `RenderUIQueue/RenderUIWorld/DrawUIItems/BuildScreenOrtho`（+ `s_uiTmp`）从 `DefaultRenderPipeline` 提取为 `internal static class UIRenderPass`（或基类 protected 成员），两条管线共用；
- 纯移动+可见性调整，行为等价；`DefaultRenderPipeline` 调用点改为转发。

### D7：Camera2D = 伴生组件，不动 Camera 类
- `Camera2D : MonoBehaviour`，`[RequireComponent(typeof(Camera))]`，`[AddComponentMenu("Rendering/Camera 2D")]`；
- Awake 强制 `ProjectionMode = Orthographic`；提供 `PixelPerfect`（PPU 对齐 `OrthographicSize`，相机位置吸附像素网格）、`Follow(target, damp, bounds)`、`ScreenToWorldPoint(Float2)`（复用 `Camera.ScreenPointToRay` + z=0 平面求交，正交已支持）。

## 4. 工作分解（S1→S7 顺序执行，每步独立提交）

### S1 · SortingLayer 基础设施（~2 天）
**新建**：
- `Prowl.Runtime/GameObject/SortingLayers.cs` — 静态类：`List<string> layers`（默认 `["Default"]`）、`GetLayerIndex/GetLayerName/GetLayers`、`ResetDefault`（照抄 TagLayerManager 形态）
**修改**：
- `Prowl.Runtime/Components/SpriteRenderer.cs` — 增 `SortingLayer` 字段；Z 偏移公式改为 D4 的复合式；`DrawGizmosSelected` 顺带显示层名
- `Prowl.Editor/Projects/Settings/TagsAndLayersSettings.cs` — 增 Sorting Layers 编辑 UI（克隆本文件 Tags 列表 UI :49-113 的写法）
**验收**：非批路径下改层/序号，重叠 Sprite 顺序正确；设置页可增删层并随工程持久化。

### S2 · SpriteAtlas 资产 + MaxRects 打包器（~4 天）
**新建（Runtime）**：
- `Prowl.Runtime/Resources/SpriteAtlas.cs` — `[CreateAssetMenu("2D/Sprite Atlas", Extension=".spriteatlas")]`；字段：`List<AssetRef<Sprite>> Sprites`、`AtlasPackingSettings Settings`（MaxSize/padding/gutter/POT/旋转开关）、`AssetRef<Texture2D> PackedTexture`、`Dictionary<string /*spriteGUID*/, AtlasEntry> Entries`、`string PackHash`（输入指纹，脏检测）；`AtlasEntry { Float4 UVRect; int Page; }`
- `Prowl.Runtime/Rendering/SpriteAtlasRegistry.cs` — 静态：`Register/Unregister(atlas)`、`TryResolve(Guid spriteGuid, out uvRect, out atlasTexture)`
- `Prowl.Runtime/Utils/MaxRectsPacker.cs` — 纯 C# MaxRects（BSSF+BAF 两种启发式），输入输出矩形数组
**新建（Editor）**：
- `Prowl.Editor/GUI/CustomEditors/AssetEditors/SpriteAtlasEditor.cs` — `[CustomAssetEditor(typeof(SpriteAtlas))]`：包含 Sprite 列表（拖入）、打包按钮/自动打包（AssetWatcher 触发 + PackHash 脏检测）、结果预览、每 Sprite 命中率显示；打包流程 = 取各 Sprite 源像素（经 TextureImporter 源文件解码）→ MaxRects → 写像素（含 gutter 外扩）→ 保存 PackedTexture 子资产 + Entries
- 文件图标：`[FileIcon(".spriteatlas")]` 注册
**验收**：A3；未入图集 Sprite 行为不变（registry 未命中走原纹理）。

### S3 · Sprite 合批核心（~5 天）
**修改**：
- `Prowl.Runtime/Assets/Defaults/Sprite.shader` — D2 的 2 行条件（GPU_INSTANCING 分支）
**新建**：
- `Prowl.Runtime/Rendering/SpriteBatchContext.cs` — 静态帧上下文：`Begin(camera)` / `Add(item)` / `Flush(List<IRenderable>)` / `End()`；item 池化（`SpriteBatchItem` struct：sprite 引用、矩阵、tint、layer、order、seq）；Flush 内：解析 atlas（S2 registry）→ 剔除 tight/材质覆盖/SecondaryTextures 项（回退 emit 各自 MeshRenderable）→ 全局稳定排序（D4）→ 按图集纹理分组连续段 → 每段产出一个 `SpriteBatchRenderable`
- `Prowl.Runtime/Rendering/SpriteBatchRenderable.cs` — 实现 `IRenderable`：持有共享 quad `Mesh` + 每图集**常驻 grow-only** `InstanceData[]`（仅容量增长时重分配，帧内 Clear+重填，遵守引擎零 GC 帧循环范式）；`GetRenderingData` 返回 instanceData → 自动走 `DrawInstancedRenderablePass`；`GetCullingData` = 实例包围盒并集；共享一个 `PropertyState`（`_MainTex`=图集纹理，Clear+重填）
- `Prowl.Runtime/Components/SpriteRenderer.cs` — OnRenderCollect 按 D3 分流；公共静态 `UnitQuadMesh`（懒初始化）
**验收**：临时用测试脚本直连（下一项）验证 500 sprite = 每图集 1 次 instanced draw；A4 回退项逐一验证。

### S4 · RenderPipeline2D（~3 天）
**修改**：
- `Prowl.Runtime/Rendering/DefaultRenderPipeline.cs` — D6 提取 UI 队列为共享 `UIRenderPass`（行为等价重构，3D 路径回归由 A6 保证）
**新建**：
- `Prowl.Runtime/Rendering/RenderPipeline2D.cs` — `RenderPipeline` 子类；流程：
  1. `camera.UpdateRenderData` + `SetupGlobalUniforms` + `AssignCameraMatrices`（跳过 prepass/阴影/skybox/TAA 全部 3D 步骤）
  2. `SpriteBatchContext.Begin()` → `CollectRenderables` → `Flush()` → `CullRenderables`（视锥+LayerMask）
  3. **Sprite 画家序绘制**（显式按 Flush 的段序 `DrawRenderables(cmd, spriteRenderables, "RenderOrder", "Transparent", …)`，该列表已预排，不再距离排序）
  4. 非 sprite 透明物（LineRenderer/TextMesh 等）：BackToFront 混排策略 = 并入第 3 步前的全局排序（以 GetPosition.z 参与 key，M0 简化为「sprite 段后绘制」并文档标注）
  5. 世界空间 UI：`UIRenderPass.Render(css, target, World)`
  6. PostProcess 图像效果透传（复用 `ImageEffect` 机制）
  7. 最终 Blit → Overlay UI（`UIRenderPass.Render(css, target, Overlay)`）→ 还原后台缓冲
  - 属性：`public bool SpriteBatching = true;`
- `DefaultAssets.cs`/场景默认相机装配：2D 模式新建场景时 `camera.Pipeline = RenderPipeline2D.Default`（静态单例，仿 `DefaultRenderPipeline.Default`）
**验收**：2D 场景视觉与 DefaultRenderPipeline 下一致（雾/伽马路径核对：Sprite.shader 的 `ApplyFog` 依赖全局雾参数，pipeline2D 需上传默认雾或跳过——以实测为准，预计零密度=无操作）；A2 顺序用例。

### S5 · Camera2D 辅助组件（~1.5 天）
**新建**：`Prowl.Runtime/Components/Camera2D.cs`（D7 全部内容）
**验收**：PixelPerfect 下无半像素抖动；Follow+边界钳制可用；`ScreenToWorldPoint` 与拾取一致。

### S6 · 编辑器 2D 模式 = 精简层1（~4 天）
**新建**：
- `Prowl.Editor/Projects/Settings/Settings2D.cs` — `[ProjectSettings("2D", order: 15)]`：`bool Is2DProject`、SortingLayers 转发（或并入 TagsAndLayersSettings）
**修改**（全部有精确落点，来自编辑器深读）：
- `GUI/SceneView/EditorCamera.cs` — 增 `Is2D`：启用时强制 ortho+`SetOrientation(0,0)` 并在 `ProcessInput` 门控 orbit(:404-424) 与 RMB look/WASD(:459-514)；`FocusSelection`(:551) 2D 分支改设 `OrthographicSize=radius`
- `GUI/Panels/SceneViewPanel.cs` — 「2D」工具栏按钮（DrawDefaultToolbar :160-198 追加）；状态持久化（SerializeState/RestoreState :634-666）；2D 时隐藏视图立方体（DrawViewManipulator :953）；`GetDropPosition`(:674-712) 2D 分支与 XY 平面求交；（改进项，可延后）`PickObjectAt`(:478-515) 支持 SpriteRenderer 拾取
- `GUI/DefaultGameObjectCreators.cs` — `[MenuItem("GameObject/2D Object/Sprite", priority: 20)]`（+ Sprite Empty/Camera2D 入口）；2D 模式下 `MenuItemAttribute.UnregisterByPrefix("GameObject/3D Object")`
- `EditorSceneManager.cs` — `CreateDefaultScene`(:67-114) 按 `Is2DProject` 分支：正交相机(0,0,-10)+`RenderPipeline2D`+`Camera2D`，无灯光/地面
- `GUI/CustomEditors/GameObjectInspector.cs` — `GatherComponents`(:1564-1602) 增 2D 模式分类过滤（隐藏 3D 专属组件，白名单式）
- `Runtime/Resources/Scene.cs` — **Physics 懒加载门控**（:221 字段改 `PhysicsWorld?`，:929 步进判空；Rigidbody3D 注册时触发创建）
**验收**：A5；3D 工程切回无影响（A6）。

### S7 · VS 配套 + 压测样例（~2 天）
**新建**：
- `Prowl.Runtime/Utils/PrefabPool.cs` — 静态对象池（`Spawn/Release/Prewarm`，按 Prefab GUID 分桶，基于 `GameObject.Instantiate`）
- `Prowl.Runtime/Samples`(或 Samples 工程) `Scripts/SteeringSeparation.cs` — 邻域分离力示例（VS 敌群核心算法，M0 先给 O(n²) 版占位，M4 接空间哈希）
- `Samples/2DStressTest/` — 压测样例工程：500–2000 个 Sprite（移动+变色+换图集），正交相机+RenderPipeline2D；场景含跨图集重叠排序用例
**验收**：A1 用 GameView 统计面板截图记录基线（合批前）与结果（合批后）对比。

## 5. 核心数据结构草图

```csharp
// SpriteBatchContext 帧内项（池化 struct）
struct SpriteBatchItem {
    public AssetRef<Sprite> Sprite; public Float4x4 Matrix; public Color Tint;
    public int Layer, Order, Seq; public AssetRef<Material>? MaterialOverride;
}

// Flush 分组产物：一段连续同图集实例
class SpriteBatchRenderable : IRenderable {
    Mesh _quad; Material _mat; PropertyState _props;   // _MainTex = 图集纹理
    InstanceData[] _instances;                          // grow-only，帧内重填
    AABB _bounds;                                       // 实例包围盒并集（缓存失效同 EnsureWorldBounds 范式）
    // GetRenderingData → instanceData 非空 ⇒ DrawInstancedRenderablePass
}

// SpriteAtlas（资产，.spriteatlas）
class SpriteAtlas : EngineObject {
    List<AssetRef<Sprite>> Sprites;  AtlasPackingSettings Settings;
    AssetRef<Texture2D> PackedTexture;                 // 打包输出（子资产）
    Dictionary<string, AtlasEntry> Entries;            // spriteGUID → uvRect+page
    string PackHash;                                   // 输入指纹（含源贴图 mtime/尺寸）
}
```

排序比较器（D4）：`(a.Layer, a.Order, a.Seq)` 元组字典序（Seq=提交序，保证稳定性）。

## 6. 风险与预案

| 风险 | 概率 | 预案 |
|---|---|---|
| Sprite.shader 雾参数在 pipeline2D 下未初始化导致异常色调 | 低 | S4 实测；必要时 pipeline2D 上传零密度雾（`UploadFogUniforms` 同款调用） |
| 共享 quad 的单 instance buffer 被多图集段串行覆写低效 | 低 | 段数通常 ≤4；若实测成瓶颈，改为每段独立 quad Mesh（EnsureInstanceVAO 按 Mesh 独立） |
| 编辑器取源贴图像素通道缺失（压缩纹理） | 中 | 打包一律从 importer 源文件解码，不读 GPU 纹理 |
| `DrawRenderables` 对预排列表二次距离排序破坏画家序 | 中 | S4 第 3 步明确跳过 SortRenderables；写集成测试锁定段序 |
| tight mesh 回退路径与批路径 Z 不一致 | 低 | D4 复合 Z 偏移公式两条路径共用 |
| UI 队列提取重构引入 3D 回归 | 中 | 独立提交+跑全部 3D 样例（A6 门槛） |

## 7. 测试计划

- **单元**：MaxRectsPacker（已知布局用例+边界溢出）、SortingLayers 序列化、SpriteAtlas Entries GUID 稳定性（重打包前后）
- **集成**：`Prowl.Runtime.Test` 增 SpriteBatchContext 排序/分组/回退三用例（纯逻辑可测，不依赖 GL）
- **手动**：A1–A6 逐条过；2DStressTest 前后对比记录进本文档附录

## 8. 工期汇总

S1(2) + S2(4) + S3(5) + S4(3) + S5(1.5) + S6(4) + S7(2) ≈ **3 周**（单人，含测试与文档），与路线图 M0 预估一致。

## 9. 明确不在 M0 范围（防蔓延）

- tight mesh 批处理、SecondaryTextures 图集化（→M3）
- 2D 光照/法线（→M3）、粒子 XY 模式（→M1）
- SpriteAnimator（→M1）、Tilemap（→M2，duality 已就位待细化）
- UI Toggle/RawImage（→M1）
