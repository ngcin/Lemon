# ADR-019：M8 2D 光照——光贴图合成与 Light2D 组件模型（yami 渲染架构 + Unity 组件模型）

- 日期：2026-10-11（M8 批⓪ 设计定形）
- 状态：已采纳（2026-10-11 用户拍板：**架构方向 + D1–D8 均按建议**——放弃 02 §8 原法的"法线 RT 裁剪延迟"，改光贴图合成 + 组件模型；Luma 全套判定过重不采用。批⓪ 文档落账随本 ADR 同日落位）
- 影响：[02-Rendering](../EngineDesign/02-Rendering-Vulkan.md) §8（整节重写）+ §4（Light Pass 行）+ §3.4（frag 注释）+ §10（移植表 Luma 行）、[08-Development-Roadmap](../EngineDesign/08-Development-Roadmap.md) §0 总览表 M8 行 + §2 M8 节 + §4 砍单第 2 条、[M8.md](../Plans/M8/M8.md)（批次拆解总览）、[09-Testing](../EngineDesign/09-Testing.md) §6.11（批② 落实测）、架构债登记 #L20（bindless 槽位记账）/#L34（预计不再触发）/#L5（批⑥ 前检查）

## 背景

1. **原设计 = Luma 裁剪延迟**（02 §8，2026-09 规划）：GBuffer-lite 法线 RT + 点/锥光 + blob 阴影，Luma `DeferredRenderer`/`ShadowRenderer`/`SDFShadow.wgsl` WGSL→GLSL 直译。Luma 光照全家桶实测 **~7,400 行 C++ + ~2,100 行 WGSL**（LightmapBaker/LightProbe/AreaLight/IndirectLighting 等 3D 级件占约 2/3），裁剪移植估 2,000–3,000 行。
2. **美术现实**：法线 RT 的视觉收益完全依赖素材带法线贴图；像素风素材包（含已在用的 yami-dungeon）基本不提供。无法线图时延迟光照算出的就是平面——花 MRT + 变体管理的钱，买到的观感与光贴图乘法相当。割草品类光照的真实价值 = 彩色光池/昼夜氛围/枪口闪光/辉光，光贴图全覆盖。
3. **渲染现状**（2026-10-11 探测核实）：RHI 无 MRT（`colorAttachmentCount=1`，RHI.cpp:1685）、loadOp 恒 CLEAR（`BeginDynamicRendering`，RHI.cpp:1679）、**全库零后处理/ping-pong 设施**；但"离屏渲染→同帧后续 pass 采样 RT"回路已有完整先例（编辑器 gameRT→ImGui 采样，`ViewportRenderer.cpp:411-446` + `ImGuiBackend.cpp:321-326`）；`BlendMode` 已含 Multiply（RHI.h:37）；外部纹理直采先例 `RmlUiBackend.cpp:414-416`。lemon-game 今日零离屏 pass 直渲交换链（GameEntry.cpp:1031-1035，D8 A 案）。
4. **组件设施全反射驱动**：登记即自动入 `.scene` 序列化（SceneArchive 元数据驱动零逐组件代码）、自动出现在 Add Component 菜单与 Inspector 面板（`InspectorPanel.cpp:1-4` 头注"新增登记组件零编辑器代码"验收点）；Enum 下拉/ColorEdit4 等 FieldHint 现成（ComponentRegistry.h:47-76）；视口 overlay 注入口 `PushOverlayQuad/Line/Rect` 现成（ViewportRenderer.h:131-136）。
5. **vtable 现状**：59 槽（批⑧ 后），M23 触发线 72（08 M8 段登记）——余量 13。组件字段读写走既有 op2/op3 通道（AudioSource 同款），**光照 v1 零新槽可行**。
6. **三方案对比结论**（2026-10-11，DevLog 详录）：yami 光贴图（数百行级，后置乘法，无逐对象材质）/ Unity URP 2D（逐排序层光纹理 + 逐精灵材质采样，可选法线）/ Luma 全延迟（最重，3D 级）。**取舍 = yami 的渲染架构 + Unity 的组件模型**：估 1,500–2,500 行（引擎 C++ + shader + SDK + 编辑器），约为法线 RT 方案的 2/3、Luma 全套的 1/4，且不碰精灵热路径（bindless 实例化四边形管线零改动——刚过 fps 门禁的管线零风险）。
7. **移植红线**（AGENTS.md 硬性纪律 + 07 §0）：yami JS 代码禁拷（MIT 仅素材与 schema 可用）；本 ADR 借鉴的是**架构思想**（光贴图叠加/混合模式集/逐对象采样语义），实现全部原创于 Lemon 渲染器；Luma 代码本次同样不拷（原 §8 的 WGSL→GLSL 直译路线随本 ADR 废弃）。
8. **金回放纪律**（ADR-015 背景 4 先例）：组件名无条件入状态哈希流（StateHash.cpp:75）——**加 ECS 组件 = 三档金回放一次性重录**。Light2D + BlobShadow2D 两组件 ⇒ 批① 内闭环重录一次，重录后 mismatches=0 维持（AudioSource 批② 同款）。

## 决策

### M1 渲染架构：三段式光贴图合成，关灯整链跳过

**开灯帧**（场景含 ≥1 个 Light2D 或 BlobShadow2D）：

```
[场景段]  精灵/粒子/血条/飘字 → sceneRT（RGBA8Unorm，全分辨率）
[光照段]  lightRT（按质量档缩放）CLEAR 为 ambient 底色 →
          光源四边形按各自混合模式叠加（点光/锥光/蒙版光/blob 暗斑）
[合成段]  全屏不透明四边形 = sceneRT × lightRT → gameRT（编辑器）/ 交换链（lemon-game）
[UI 段]   RmlUi 在合成之后、同一动态渲染块内画（不受光）
```

- **ambient 底色** = Global 类型灯的颜色×强度（昼夜 = 脚本补间该字段）；无 Global 灯时底色 = 白（等价无环境光）；**Global 灯至多一个生效**，重复时取首个 + WARN。
- **关灯零成本**：场景零 Light2D 且零 BlobShadow2D ⇒ 三段链整体跳过，两壳路径与今日**逐字节相同**（编辑器精灵直渲 gameRT；lemon-game 直渲交换链）——08 M8 验收"光照默认关闭零成本"的机械口径。
- **两壳共用引擎件**（GameFx.h:5 "防第三套实现"纪律）：新建 `Engine/Renderer/LightCompositor`（RT 编排 + 光源批量 + 合成录制）+ `LightBatcher`（镜像 SpriteBatcher 结构：实例环 SSBO + 按混合分 3 管线 + `light.frag`）。编辑器装配点 = `ViewportRenderer.cpp:464-552`（仅 idx=1 GameView；SceneView 保持无光照 + 灯范围圈 gizmo）；lemon-game 装配点 = `GameEntry.cpp:1000-1042`（懒建 sceneRT：首帧见灯才建；SpriteBatcher 双格式管线——RGBA8Unorm 渲 sceneRT + 交换链格式合成，编辑器 RGBA8Unorm 实例先例 ViewportRenderer.cpp:374-376）。
- **loadOp 恒 CLEAR 约束天然满足**：合成段是全屏不透明四边形（CLEAR 无害）；光照段 ambient 即 CLEAR 底色；UI 段维持"合成后同块内"唯一合法插入点（ViewportRenderer.cpp:550 注释口径）。
- **提取通道**：SceneExtractor 扩展（现只提 `View<Transform2D, SpriteRenderer>`，SceneExtractor.cpp:31）——加 `View<Transform2D, Light2D>` / `View<Transform2D, BlobShadow2D>` 双缓冲提取（RenderableManager 新实例数组，位索引 + version 防串槽同款）；两壳复用现有 `rm.Extract()` 调用点，渲染侧零轮询 ECS。
- **蒙版光源采样**：maskSprite = spriteId，经 AtlasRegistry 查 UV 采样现有图集绑定（同精灵机制，**零新 bindless 类别**）；sceneRT/lightRT 各占 1 个 bindless 槽（+2，#L20 记账；建 RT 时绑一次，避开 M4.7-P0 同帧改槽竞态）。

### M2 光源组件模型：Light2D + BlobShadow2D，Unity 同构粒度

- **`Light2D`**（新 `Engine/Components/LightComponents.h`，POD ≤40B 布局冻结 static_assert，AudioComponents.h:14-37 样板）：
  - `type` 枚举：**Global**（环境光）/ **Point**（径向衰减）/ **Spot**（锥光：方向 = Transform2D 旋转）/ **SpriteMask**（蒙版光——贴图造型光源，聚光灯/异形光，yami 区域光 + Unity Sprite 灯同位）。
  - `blend` 枚举：**Additive / Screen / Subtract** 三种（yami 的 Max 砍掉，见 D3）；`color`（ColorHex）/ `intensity` / `range`（点/锥光半径）/ `angle`（锥光张角，Degree hint）/ `innerRatio`（锥光内角占比，软边）/ `maskSprite`（spriteId，SpriteMask 用）。
  - 衰减：`smoothstep(1 - d/range)` 径向（细节批② 定形入 02 §8）。
- **`BlobShadow2D`**（独立小组件）：`radius` / `aspect`（椭圆纵横比）/ `offsetY` / `opacity`——渲染为 lightRT 上的 **Subtract 椭圆暗斑**；无光源时 ambient=白、仅暗斑仍生效（blob 阴影独立于灯存在，Unity 同款体验）。
- 两组件 `ComponentCatalog.cpp` REGISTER_ED **表尾追加（id 32/33，只增不改序）**；序列化零代码（元数据驱动）；Inspector 零代码（Enum/ColorHex/Degree/Range hint 现成）；i18n 加 `display.Light2D[.field]` / `display.BlobShadow2D[.field]` 双语 key。
- **SceneView gizmo**：选中含 Light2D 的实体时画灯范围圈（PushOverlayLine 折线圆，选框 ViewportPanels.cpp:276-322 先例）；BlobShadow2D 画椭圆虚框。
- **编辑器形态回答**（用户 2026-10-11 问）：光源**不是** Create 菜单项——Unity 同构 = **Add Component 挂 Light2D**，反射驱动自动出现。

### M3 逐对象受光：v1 全局模式 + 段豁免；anchor 锚点采样列 v1.1 触发

- **v1 = 全局模式**：全屏乘法覆盖全体世界内容（层 0..250：精灵/粒子）。
- **段豁免（不受光）**：血条（251）/飘字（252）/位图字体文本（254）与 RmlUi UI 移到**合成之后**画（SpriteBatcher Bake 段序按合成点切分；GameFx/BitmapFont 层常量现成）。观感口径 = UI 类浮动元素不吃光照（品类惯例）。
- **anchor 锚点采样**（yami 的"角色走进光里"技巧：按锚点采样光贴图、整体均匀受光）列 **v1.1 触发式**——触发 = 出现真实消费者（如 svr-test 角色受光需求）。实现路径已备：CPU 逐锚点算光值染 `colorBits`（实例色现成通道）+ 该对象划入合成后段，**零 shader 改动**。

### M4 SDK 面与 vtable：零新增槽位

- C# 侧 = 镜像 struct（`Interop/Components.cs` + `LayoutTables.cs` 探针行，AudioSource 样板含"镜像默认值坑"注释）+ `Lemon.Lighting` 门面（**纯 C# sugar**：ambient 补间助手/灯开关语法糖，全部走既有 op2/op3 组件读写通道）。
- **vtable 槽位数不变（59）**——M23 余量保持 13；若 v1.1 anchor 或运行时灯动画 API 需要新命令通道，优先 World 非 ECS 通道（Tween/Fx 先例，零哈希流影响）而非加槽。
- 昼夜循环 = 脚本逐帧写 Global 灯组件字段（op2 整写），组件字段入 StateHash ⇒ 可入回放（与 AudioSource 同语义）。

### M5 质量分级：QualityManager 首次接入两壳

- lightRT 分辨率：Low=1/4 / Med=1/2 / High=全分辨率；光源数上限：64（Low=32，超出静默截断 + 一次性 WARN）。
- Quality.h 头注预留的"M5+ 扩展：后处理链长度"由本里程碑兑现（光照 + 批④ 后处理链 Med+ 档）；QualityManager 现仅 bench-mow 消费（Quality.h 现状）——批② 起编辑器/lemon-game 两壳接入。

### M6 已否决路径与二阶段触发条件

- **法线贴图路径**（Unity 逐精灵式或 Luma 裁剪延迟式）：**触发 = 素材带法线贴图的游戏立项**，届时另立 ADR；不进 M8。
- **SDF 几何硬阴影**：维持 08 §4 砍单序列第 2 条治理，不做。
- **Max 混合模式 / 逐精灵光照材质 / 阴影投射体 / 光照贴图烘焙**：Non-goals（下节）。

## 拍板项（2026-10-11 用户确认，均按建议采纳）

- **D1 渲染架构** ✅：三段式光贴图合成（sceneRT → lightRT → 全屏乘法 → UI），关灯整链跳过逐字节同今日。
- **D2 组件模型** ✅：Light2D 单组件四类型（Global/Point/Spot/SpriteMask）+ 独立 BlobShadow2D；Add Component 自动出现。
- **D3 混合模式集** ✅：v1 = Additive/Screen/Subtract 三种；Max 砍掉；blob = lightRT Subtract 椭圆。
- **D4 逐对象采样分期** ✅：v1 全局 + 段豁免（251/252/254 与 UI 不受光）；anchor 锚点采样 v1.1 触发式（消费者出现即做，路径已备零 shader 改动）。
- **D5 法线路径** ✅：二阶段触发 = 素材带法线贴图的游戏立项；不进 M8。
- **D6 SDF 阴影** ✅：不做，维持砍单序列第 2 条。
- **D7 vtable** ✅：v1 零新增槽（59 不变）；新命令需求优先 World 非 ECS 通道。
- **D8 质量分级** ✅：lightRT = 1/4·1/2·全分辨率（Low/Med/High）；光数上限 64（Low 32）。

## 后果与风险

- **金回放一次性重录**：两组件注册入哈希流 ⇒ 三档金回放重录一次，批① 内闭环（重录→mismatches=0）；此后光照场不入金档（回放 = 模拟面，光照纯表现）。
- **bindless 槽位 +2**（sceneRT/lightRT）：#L20 记账（256 槽 vs 实测占用，未近界）；蒙版光走图集页零新槽。
- **#L34 预计不再触发**：蒙版光走 spriteId/AtlasRegistry 先例，Renderer 无需包含 Assets 类型；登记保留，触发时再议。
- **#L5 挂批⑥**：脚本逐帧动画光源（昼夜循环）使组件字段逐帧入哈希流——soak 前检查回放录制耗时是否成点（成点则组件级预哈希，注意回放文件不兼容判据）。
- **lemon-game 双格式管线**：精灵管线按 RGBA8Unorm + 交换链格式各建一套（4 混合 × 2 格式，init 期一次性，运行时零开销）；关灯帧零额外 pass。
- **回归口径同步**：批② smoke-light 进回归（full 22→23 步）时，09 §8 L448 与 §9 L478 的"21 步"滞后口径一并修正（M7c 批⑫ 已加至 22，b6b 先例"文档两处口径同步"）。
- **像素断言读回**：smoke-light 用 SceneRenderTarget/GameRenderTarget 读回口（ViewportRenderer.h:151-154）断言"灯下/灯外像素差 + 关灯帧与基线逐位同"——关灯零成本的机器证。

## Non-goals（v1 砍单，防 scope 膨胀）

法线贴图任何形式（D5 触发另行）；SDF/几何阴影（D6）；Max 混合；逐精灵光照材质；阴影投射体（ShadowCaster）；光照贴图烘焙/光探针；每光源独立阴影；光源数 >64 分档策略；光照对 SceneView 的实时预览（gizmo 圈代替）。

## 关联

- [02-Rendering §8（已由本 ADR 重写）](../EngineDesign/02-Rendering-Vulkan.md) + §4 Light Pass 行 + §3.4 注释清理 + §10 Luma 行注记
- [08-Development-Roadmap](../EngineDesign/08-Development-Roadmap.md) §0 M8 行 / §2 M8 节设计变更注记 / §4 砍单第 2 条语义收窄
- [M8.md](../Plans/M8/M8.md)（批次总览：⓪ 设计落账 → ① 组件 → ② 编辑器光贴图竖切 → ③ lemon-game+蒙版+blob+SDK → ④ 后处理链 → ⑤ 模板消费 → ⑥ 压测 A+soak → ⑦ 文档站+收官）
- [DevLog 2026-10-11 光照架构决策](../DevLog/2026-10-11-light-architecture-lightmap-decision.md)（三方案对比与拍板记录）
- 先例：[ADR-015](./ADR-015-Audio-System-And-Baked-Format.md) M3（组件注册重录/尾插纪律/op2-op3 通道）、[ADR-017](./ADR-017-Scene-Management-And-LoadScene.md)（D 点拍板流）；登记债：08 M8 段 #M23/#L20/#L34/#L5
