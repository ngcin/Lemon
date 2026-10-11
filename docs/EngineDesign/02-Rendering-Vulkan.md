# Lemon 引擎设计 — 02 渲染内核（Vulkan）

> 目标：一条管线画完"万级怪物 + 弹幕 + 粒子 + 地形 + HUD"，模拟与渲染解耦，Windows 原生 Vulkan 1.3。
> 运行时下限（2026-09-24 F-12 契约化）：**Vulkan 1.2**——`descriptorIndexing`/`hostQueryReset` 走 1.2 core 特性链，动态渲染/同步 2 低于 1.3 走 KHR 扩展分支；1.1/1.0 设备在枚举期明确拒绝（不实现 EXT 版 feature 链——受益设备近空集，bindless 驱动质量不可信任）。交换链 usage/合成 alpha 按 surface 能力交集选择（`TRANSFER_SRC` 缺能力时调试截屏降级）。
> 蓝本：Luma 的"提取-双缓冲-插值-合批"流水线与 Nut RHI 切面（MIT，可移植）；Looper 的裸 Vulkan 实例化渲染作为实现期对照（暂仅借鉴）。

---

## 1. 渲染帧总览

一帧渲染分四段，全部发生在渲染线程：

```
① 提取 Extract     ：从双缓冲帧数据（S_n-1/S_n）按 alpha 插值出本帧 Renderable 列表（视口剔除 + margin）
② 排序 Sort        ：SortingLayer → 批键 → Z/序号，稳定排序
③ 合批 Batch       ：POD 批键哈希分桶 → 顶点/实例写入帧环形缓冲 → 生成 RenderPacket
④ 录制/提交 Record ：按 pass 录制次级命令缓冲，主命令缓冲只做 pass 间依赖，提交队列
```

设计来源与取舍：

- **提取-插值**（Luma `Application/RenderableManager.h`）：`std::array<FrameArena<RenderableTransform>,2>` + `packetBuffers` 双缓冲 + `activeBufferIndex` 原子切换 + `frameVersion`/`m_lastBuiltAlpha` 缓存跳过重建。**整套移植**（改命名空间与句柄类型）。
- **视口剔除**：`SetViewport(camX, camY, w, h, zoom)` 外扩 `kMargin = 150px`（Luma 同款量级；像素风弹幕高速时 M1 实测调参）；编辑器 SceneView 模式 `ClearViewport()` 全量渲染（编辑相机与游戏相机不一致，Luma 注释过的坑）。
- **渲染不负责游戏语义**：Renderable 是渲染层的最小单位（sprite/text/particle-chunk/line），不知道实体为何物。

## 2. RHI 薄层（Vulkan 封装切面）

以 Luma `Renderer/Nut/` 的职责切面为蓝本，用 Vulkan 原语重实现（不搬 Dawn 代码）。**唯一允许触碰 Vulkan 的模块**。

```cpp
// Engine/Renderer/RHI/rhi.h —— 全部自有句柄，实现细节 Pimpl 隐藏
namespace lemon::rhi {
    struct DeviceDesc { bool debugLayer; bool preferDiscrete; std::vector<const char*> extraExts; };
    class Device {                      // 对应 NutContext：实例/物理/逻辑设备/VMA allocator
    public:
        static Owned<Device> Create(const DeviceDesc&);
        SwapchainHandle  CreateSwapchain(WindowHandle, const SwapchainDesc&);
        BufferHandle    CreateBuffer(const BufferDesc&);      // VMA 包装：Usage/HostVisible|DeviceLocal
        TextureHandle   CreateTexture(const TextureDesc&);    // 2D only：尺寸/格式/层级/采样计数
        ShaderHandle    CreateShader(ShaderStage, std::span<const uint32_t> spirv);
        PipelineHandle  CreatePipeline(const PipelineDesc&);  // 顶点布局+blend+动态状态(视口/scissor)
        SamplerHandle   CreateSampler(const SamplerDesc&);
        ShaderCache&    ShaderCache();                        // SHA256(源+宏+阶段) → SPIR-V 缓存（Nut 同款键策略）
        // 帧同步与提交
        FrameIndex      AcquireFrame(SwapchainHandle);
        void            BeginFrame(FrameIndex);               // N 帧在途 fence 等待 + 帧内临时资源重置
        void            Submit(CommandList&, const SubmitInfo&);
        void            Present(SwapchainHandle);
        bool            IsDeviceLost() const;                 // 设备丢失恢复（Nut/GraphicsBackend 同款）
        void            Recreate(SwapchainHandle);            // 窗口尺寸变化/丢失后重建
    };
    class CommandList { /* BeginPass/BindPipeline/BindDescriptors/SetViewport/Draw/DrawInstanced/EndPass */ };
}
```

RHI 关键纪律：

| 纪律 | 内容 | 来源/理由 |
|---|---|---|
| 无同步漏点 | 帧内所有资源创建走"帧临时堆"（per-frame VMA pool，帧末整体重置）；跨帧资源显式 `GpuTimeline` 信号量 | Vulkan 生命周期的头号坑，从设计上禁止用户态接触 |
| 描述符 | bindless 风格（`descriptorIndexing` 扩展，Vulkan 1.3 core）：一张全局 sampled image 数组 + sampler 数组，纹理注册即得 index，draw 时 push constant 传 index | 海量图集/精灵免描述符重写，2D 合批的前提（Luma 多采样器合批思想的 Vulkan 1.3 实现路径） |
| 推送常量 | 每 draw ≤ 128B push constant（相机矩阵/批参数/纹理 index），零 UBO 绑定 | 2D 批次参数足够小 |
| 管线缓存 | 磁盘 pipeline cache + Shader Preheat（启动时 JobSystem 多线程预编译全部已知管线组合，Luma 同款） | 首帧不卡 |
| 质量分级 | Low/Med/High 映射：MSAA on/off、粒子预算倍率、后处理链长度 | Luma QualityManager 思路 |
| 设备丢失 | `VK_ERROR_DEVICE_LOST` → 销毁并重建全部 GPU 资源（纹理/管线经缓存快速恢复），游戏态不丢 | Nut GraphicsBackend 同款承诺 |

## 3. 合批：批键 + 图集 + 排序

### 3.1 POD 批键（移植 Luma FastSpriteBatchKey）

```cpp
struct SpriteBatchKey {                       // 全部 pack 进 POD，构造时预计算哈希
    uint64_t textureAtlas;                    // 图集句柄（bindless index）
    uint32_t blend   : 4;                     // Alpha/Additive/Multiply/Mask
    uint32_t filter  : 2;                     // Point(像素风默认)/Linear
    uint32_t wrap    : 2;
    uint32_t layer   : 10;                    // SortingLayer id（≤1024 层）
    uint32_t _pad    : 16;
    uint64_t hash;                            // boost-hash_combine(0x9e3779b9...) 预计算
};
static_assert(sizeof(SpriteBatchKey) == 24 && std::is_trivially_copyable_v<SpriteBatchKey>);
```

同键连续 → 同一批。排序键 = `(SortingLayer, layerOrder, batchKey)`，稳定排序保证同层内序号可控（yami 的画家序 + Unity SortingLayer 心智模型）。

### 3.2 图集（运行时侧）

- 引擎维护**全局虚拟图集**策略：编辑器/打包期由图集打包器把项目 sprite 合入 2048/4096 图集（MaxRects，继承 Prowl2D M0 结论与实现思路）；运行时 `AtlasRegistry` 提供 `spriteId → (atlasIndex, uvRect, pivot, pixelsPerUnit)`。
- 单图集失败（超大 sprite）自动降级独立纹理 —— 批键不同自然分批，不需要特殊路径。
- 动态图集：运行时生成的纹理（render-to-texture 小地图、位图数字缓存）注册进动态图集页。

### 3.3 两条绘制路径

| 路径 | 适用 | 形态 | 参照 |
|---|---|---|---|
| **A. 实例化路径（主路径）** | 海量同图集精灵（怪物/弹幕/粒子） | 共享单位四边形 vertex buffer + per-instance SSBO：`{mat3x2 仿射, uvRect, colorRGBA8, flags}` = 48B/实例；每帧只写**脏区间**（环形偏移 + 帧 fence 保护） | Looper `renderer.cpp` 的 per-instance SSBO + `UpdatePerInstanceBuffer` 脏更新（实现期对照） |
| **B. 顶点展开路径** | 顶点级效果（弯曲/顶点色渐变/瓦片地形烘焙） | 32B/顶点：`pos.xy(f32) uv.xy(f32) color(rgba8 unorm) extra(2×u16: rotate/flip/sheet)` | yami webgl.ts 顶点格式思想（整数字节色调省带宽）+ Editor-RPG2D chunk VertexArray 烘焙 |

地形（Tilemap）走路径 B 的**chunk 烘焙缓存**：16×16 tile/chunk → 一次生成静态顶点段，脏 chunk 才重建（Editor-RPG2D `Chunk.cpp` 思路 + duality dirty-rect 协议，详见 05/03 文档 Tilemap 节）。

### 3.4 Sprite 着色器（GLSL 草案）

```glsl
// sprite_instanced.vert —— 路径 A
layout(push_constant) uniform PC { mat3x2 viewProj; vec2 atlasCount; } pc;
layout(location=0) in vec2 aCorner;                 // 单位四边形 [-0.5,0.5]
struct Instance { mat3x2 model; vec4 uvRect; uint color; uint flags; };  // SSBO, 48B
layout(location=1) in Instance i;
void main() {
    vec2 world = pc.viewProj * vec3(i.model * aCorner + i.model[2], 1.0);
    gl_Position = vec4(world, 0.0, 1.0);
    vUV = mix(vec2(i.uvRect.xy), vec2(i.uvRect.zw), aCorner + 0.5);
    vColor = unpackUnorm4x8(i.color);
    // flags: bit0 flipX, bit1 flipY（UV 翻转）
}

// sprite.frag —— 路径 A（光照不进精灵着色器——M8 光贴图合成方案，ADR-019；法线采样变体 = 二阶段触发项）
layout(set=0, binding=0) uniform texture2D uAtlases[64];   // bindless 数组
layout(set=0, binding=1) uniform sampler   uSamplers[8];
layout(push_constant) uniform PCF { uint atlasIndex; uint samplerIndex; } pcf;
void main() { oColor = texture(sampler2D(uAtlases[pcf.atlasIndex], uSamplers[pcf.samplerIndex]), vUV) * vColor; }
```

### 3.5 相机与视口（M1；Camera2D 清单，Prowl2D 已验证方案，ADR-009）

- **坐标系约定（2026-09-19 修订）**：世界坐标 Y 向下（屏幕像素语义），`Mat3x2::Ortho` 直接映射到 **Vulkan NDC Y 向下**（世界下方 = NDC +1 = 屏幕下方）。原实现按 GL 语义做 Y 翻转（世界下方 → NDC -1 = Vulkan 屏幕顶），导致全管线垂直镜像（文字倒印、布局上下颠倒）；对称轨道内容的 bench 从未暴露，anim-smoke 截图实锤后修正（`Math.h` + `engine_tests` 两处断言同步修订）。
- **正交相机组件**：`Camera2D { halfSize; pixelPerfect; snapToGrid; bounds; follow; }`；像素完美 = 整数缩放 + 像素网格 snap（像素风默认开）。
- **跟随**：指数阻尼跟随（smooth-damp 手感）+ 可选 look-ahead 预判；有界钳制 clamp 到世界包围盒（TD/ARPG 刚需）。
- **编辑器相机自愈**：resize/失焦恢复后自动校正 zoom 与焦点（Prowl2D `[ExecuteAlways]` 自愈教训）；编辑相机与游戏相机共用同一 Camera2D，仅输入来源不同（编辑器走真渲染管线，§1）。
- **呈现模式**：FIFO 默认、IMMEDIATE 仅 bench；MoltenVK 无 MAILBOX 的现实纳入质量分级逻辑（M0 Go/No-Go 报告 §4）。

## 4. Pass 结构与后处理链

```
[Game Pass]  不透明地形chunk → 不透明sprite → alpha sprite(排序) → 弹幕(additive可配) → 粒子(按BlendMode三条管线)
[Light Pass] (M8) lightRT 光贴图合成（见 §8；2026-10-11 ADR-019 修订，法线 RT 路线已否决）
[FX Pass]    后处理链（Med 以上开启）：Bloom(阈值1/4降采样) → HitFlash/Shockwave(UV扰动) → 色调曲线 → CRT/扫描线(可选风格)
[UI Pass]    HUD（ImGui 视口直绘）+ 位图数字/血条（走路径 A，同为 sprite）
[Editor Pass] 仅编辑器：网格/Gizmo/选择框/刷子预览（线与矩形批）
```

后处理为"链式 full-screen pass 池"，每个 pass 双 RT ping-pong；关闭 FX 时 game pass 直接渲到 swapchain。**全屏 UV 扰动类特效**（冲击波/传送门）是割草手感的廉价大杀器，进 M5 首发特性集。

**M8 批④ 落地注（2026-10-11）**：v1 形态 = `PostFx`（两壳共用，防第三套）——bloom 双滤波三级（extract 阈值提取 @1/4 → down 1/8 → down 1/16 → up 1/8 → up 1/4）+ **fxFinal 合并式终段**（冲击波 UV 扰动 + bloom 加色 + 色调曲线单 pass——02 §4 顺序语义保持：bloom 提取基于无扰动图、扰动在最终采样施加；无活跃波时扰动项为零 = 零额外 pass）。质量分级：`QualityParams.fxChain` 位 Low 关 / Med+ 开（EMA 自动降档 = FX 链先卸；**关链帧与无 FX 版路径逐位同**，两壳 `--smoke-fx` 机器证）。两壳接线：编辑器 GameView **显示终点 gameRT 照旧**（ImGui 采样不动），合成/场景段终点前移 `fxSceneRT`（RGBA8Unorm，02 §8 LDR 拍板）→ FX 链 → gameRT 块内 fxFinal + 豁免段 + RmlUi；lemon-game 合成终点由"→交换链"重构为"→FX 链→交换链"（显示块内 fxFinal + 豁免段 + UI）。RT 槽位顶部段 245–248（fxScene/bloomA/B/C，#L20 记账 +4）。冲击波 = 引擎侧 `PostFx::SpawnShockwave`（表现层、渲染帧钟 Simulate、不入 StateHash/回放；C# 门面触发式——消费者出现时随 World 非 ECS 通道议，ADR-019 M4 口径）。HDR bloom（16F RT）与 CRT/扫描线风格列 v1.1。

## 5. 帧内存与上传通路

| 资源 | 策略 |
|---|---|
| 实例数据 | 环形 SSBO（`kRingFrames=3` 段），模拟帧号取段，脏区间 memcpy，fence 保护 |
| 顶点（路径 B） | 帧环形 buffer（8MB 起步，VMA HostVisible|WriteCombined） |
| 地形 chunk 顶点 | device-local 静态 buffer + 传输队列异步上传（脏 chunk 队列，≤ 4 chunk/帧防尖峰） |
| 纹理 | 异步暂存上传 + mipmap 生成走计算/传输队列，主线程只登记 |
| push constant | 128B/批次，零内存压力 |

## 6. 粒子系统（割草特效底座，近乎原样移植 Luma）

数据布局（Luma `Data/ParticleData.h` 的方案直接采用）：

```cpp
struct alignas(16) ParticleData {          // CPU AoS
    Vec2 pos, vel; float age, lifetime;
    uint32_t color0, color1;               // 起止色（rgba8 各一）
    float size0, size1, rot, rotSpeed;
    float mass, drag; int32_t texIndex;    // 图集内子纹理
    uint32_t flags;                        // gravity/inheritVel/spherize...
};
struct ParticleGPUData {                   // GPU 平行数组 = 4×vec4，上传即画
    vec4 posAndRot; vec4 color; vec4 sizeAndUV; vec4 uvScaleAndIndex;
};
```

- **池**：`ParticlePool`（每发射器层 4096 起步，全局预算分级：High 100k / Med 50k / Low 20k）；`EmitBatch(n)` 一次 resize；死亡回收 **swap-and-pop O(1)**；CPU/GPU 平行数组按活粒子数前缀上传。
- **渲染**：按 BlendMode 三条管线（alpha/additive/multiply）+ 按纹理子批；实例缓冲弹性扩容（`EnsureInstanceBufferCapacity`，Luma 同款）。
- **发射器**：cone/burst/inheritVelocity/起止插值/mass/drag（Unity Shuriken 风格子集）；曲线资产化（`CurveAsset`，编辑器可视化编辑）。
- **模拟**：逐发射器 `ParallelFor`（grain=256）跑 JobSystem；2D 重力/风/阻力闭式积分，无碰撞（M8 可选加地面高度查询）。
- **降级联动**：帧率低于阈值自动切粒子预算档（VS 的"性能模式"，yami roadmap 同款思路）。

## 7. 文本渲染（两阶段）

- **v1（M1）**：位图字体。引擎内置生成器（离线把 TTF → 等距/距离图集 + Kerning 表）；HUD 数字（伤害飘字/计数）走**位图数字页 + UV 换页**（yami 性能四件套之二：高频数字绝不动态栅格化）；低频富文本（对话/日志）走 ImGui 文本路径（编辑器/菜单足够，字体加载与 IME 白送）。
  - **2026-10-07 批① 落地注（M7c）**：TTF→图集离线烘焙器正式落地——`Assets/FontBake`（FreeType 光栅 + 描边 + shelf 装箱 → LBF1 `.baked`：位图页 + 度量表，paramsHash 头增量重烘）；字体资产 `.ttf/.otf` 进管线（.meta charset/size/outline 词表配置），编辑器后台烤制（ADR-015 音频 worker 同款）+ packager 包形态现烤；`BitmapFont` 外部页与内置 5×7 并存（**字形产包走 SpritePacket UV 覆盖通道——sprite 表零登记，资产号段结构性无冲突**），缺字形回退内置页 + 红字一次；UTF-8 寻址（中文飘字「暴击」可显）。运行时零 FreeType 零栅格化（红线不破，RmlUi 运行时字体链并存各管各的）。Fx 飘字选字体 = project.lemon `fxFont`（缺省内置页）。
- **v2（M8 后按需）**：SDF 文本（msdf-atlas-gen 离线生成 + 运行时 outline/shadow 着色器变体），服务游戏内富文本（ARPG 对话）。**决策点**：若 v1 + ImGui 已覆盖品类需求，v2 可无限期推迟。

## 8. 2D 光照与阴影（M8，非阻塞增强）

> **（2026-10-11 修订，已由 [ADR-019](../ADR/ADR-019-Light2D-Lightmap-Component.md) 修订）**：原设计为 Luma 裁剪延迟（法线 RT + WGSL→GLSL 直译），因像素风素材基本无法线贴图（视觉收益不成立）且 MRT/变体管理成本高而**整体否决**——法线路径转为二阶段触发项（触发 = 素材带法线贴图的游戏立项，届时另立 ADR）。现设计 = **光贴图合成 + Light2D 组件模型**（架构思想参照 yami、组件粒度参照 Unity URP 2D；实现原创，不拷两家代码——07 §0 移植红线）。

- **三段式合成**：场景段渲 `sceneRT` → 光照段渲 `lightRT`（CLEAR 为 ambient 底色，光源四边形按混合模式叠加）→ 合成段全屏四边形 `sceneRT × lightRT` → UI 段其后同块内画（不受光）。血条（层 251）/飘字（252）/位图文本（254）与 RmlUi 划入合成后段豁免。
- **组件模型**：`Light2D`（type = Global/Point/Spot/SpriteMask；blend = Additive/Screen/Subtract；color/intensity/range/angle/innerRatio/maskSprite，POD ≤40B 冻结）+ `BlobShadow2D`（radius/aspect/offsetY/opacity → lightRT 上 Subtract 椭圆暗斑，无光源时白底仅暗斑仍生效）。SceneExtractor 加 `View<Transform2D, Light2D|BlobShadow2D>` 双缓冲提取通道，两壳共用 `LightCompositor` 装配（防第三套实现）。
- **衰减定形（2026-10-11 批② 拍板，ADR-019 M2 遗留收口）**：径向 `t = clamp(1 − d/range)`，`att = t²(3−2t)`（smoothstep 端点零导数——range 边界无亮环/硬切）；Spot 角向 = `cos(外半角) → cos(外半角×innerRatio)` 的 smoothstep 软边（方向 = Transform 旋转、local +X）；blob 暗斑复用同径向公式（椭圆半轴 = radius×aspect / radius，Subtract 白贡献 ×opacity）。CPU 纯函数（LightTypes.h，单测锚）与 light.frag 两处同式，改动须同步。
- **SceneView gizmo 形态（批② 落地）**：选中含 Light2D 的实体画 range 折线圈（32 段，Point/Spot）；Spot 额外画锥光楔形两边线（张角/朝向直观性）；Global 不画（range 无意义）；BlobShadow2D 椭圆虚框（隔段虚线、offsetY 贴地偏移）。SceneView 恒不渲光照（gizmo 圈代替实时预览——ADR-019 Non-goal）。
- **光源批量**：`LightBatcher`（实例环 SSBO + 按混合分 3 管线 + `light.frag`：点光 smoothstep 径向衰减 / 锥光（方向 = Transform 旋转）/ 蒙版光采样图集页 UV）；蒙版走现有图集绑定零新 bindless 类别；sceneRT/lightRT 各占 1 槽（+2，#L20 记账）。
- **蒙版光与暗斑渲染语义（2026-10-11 批③ 落地）**：
  - **SpriteMask 蒙版因子 = `texel.rgb × texel.a`**（白 RGB + alpha 渐变的 glow 类与不透明灰度蒙版两类素材通吃）；**无径向衰减**——贴图即造型（Unity Sprite 灯同语义）；四边形 = 2×range 方形、旋转生效；悬空 maskSprite（无效 spriteId）→ Prepare 侧 dropped 计数 + 一次性 WARN（提取不阻断——SceneExtractor 头注口径的渲染期回退落点）。
  - 蒙版页槽按实例自 `typeBlend` 低 24 位（打包 = type<<28 | blend<<24 | atlasIdx）——light.frag 以 `nonuniformEXT` 索引 uAtlases[]（draw 内不动态一致）；机制前提 = RHI 开 `shaderSampledImageArrayNonUniformIndexing` 特性位（descriptorIndexing 同族子集，缺失即启动期 assert 响亮失败；回退预案 = 蒙版灯按灯单 draw + push 常量页槽，未启用）。
  - **blob = Point 路径实例**：`FillBlobInstance` 纯函数映射（color 白 × intensity=opacity、range=radius、仿射 edgeX=2·radius·aspect / edgeY=2·radius、平移 +offsetY——与编辑器 gizmo 同约定 +Y 上）计入 Subtract 桶尾部（暗斑最后扣）——零 shader 改动（dn=2|vLocal| 归一化对拉伸仿射天然给椭圆边界）。**opacity=0 = 关**（提取侧滤除，镜像 kLightEnabled——BlobShadow2D 无 flags 字段的关灯口径）；blob 截断独立计数（drawBlobs/droppedBlobs，同 budget 数值——灯满员不挤暗斑）。
- **两壳装配（批③ 齐）**：编辑器 GameView（批②）与 lemon-game（批③）共用 `LightCompositor`；游戏壳 = 双格式管线（sceneBatcher RGBA8Unorm 渲 sceneRT 场景段 / 原交换链格式 batcher 承担豁免段与关灯帧——帧内各 Bake 一次无 append）+ 合成段直写交换链（compositeTargetFormat = 交换链格式，头注预留位）+ sceneRT/lightRT 懒建（首帧见灯才建）+ 程序化 glow 蒙版页（槽 249，白 RGB + LightRadialAttenuation 同式 alpha 渐变——蒙版夹具与模板复用；顶部段 249/250/251 三槽 #L20 记账）。
- **ambient/昼夜**：Global 灯（至多一个生效）颜色×强度 = lightRT 底色；昼夜循环 = 脚本补间该组件字段（可入回放）。**使用注意（批② 真人观感期实证）**：无 Global 灯 = ambient 白，LDR 光贴图钳 1.0 下 Additive/Screen 对满值只加不亮（数学空操作）、唯 Subtract 显形——摆加亮类灯的场景 ambient 须留暗（<1）；ambient=白是「全亮无影」语义而非中性底。
- **质量分级**：lightRT 分辨率 Low=1/4 / Med=1/2 / High=全分辨率；光源数上限 64/帧（Low 32，超出截断 + 一次性 WARN）——QualityManager 接入编辑器（批②）/lemon-game（批③，渲染帧钟 EMA 同款）两壳。
- **RT 格式与色彩空间（2026-10-11 评审拍板，批② 开工核证项）**：v1 = LDR（sceneRT/lightRT 均 RGBA8Unorm）+ **gamma 空间乘法合成**——像素风惯例（yami 同款），有意决策防后人当 bug 修；代价 = lightRT 叠加在 1.0 截断、bloom 只能做 LDR 阈值（近白像素）。**HDR bloom（16F RT）列 v1.1 触发项**——若翻案将牵动 lemon-game 双格式管线与批② 开灯帧断言基线，故格式批② 定死、批④ bloom 按本口径实现。
- **关灯零成本**：场景零 Light2D 且零 BlobShadow2D ⇒ 三段链整体跳过，两壳路径与无光照版逐字节相同（默认管线 = 无光照直出，符合像素风默认审美；smoke-light 像素断言机器证）。
- **逐对象采样**：v1 全局模式（全屏乘法覆盖层 0..250）；**anchor 锚点采样**（移动实体按锚点采样光值整体受光，CPU 染色 colorBits 零 shader 改动）列 v1.1 触发式（消费者出现即做）。
- **不做**：法线贴图任何形式（二阶段触发）、SDF 几何硬阴影（砍单序列第 2 条维持）、Max 混合、逐精灵光照材质、阴影投射体、光照贴图烘焙（Non-goals 全表见 ADR-019）。

## 9. 渲染侧性能预算分解（压测 A：10k 怪 + 50k 弹 + 100k 粒）

| 段 | 预算 | 手段 |
|---|---|---|
| 提取+插值 | 1.0 ms | 视口剔除后剩 ~15%（1.5k 实体级 Renderable）；FrameArena 零分配；alpha 缓存 |
| 排序 | 0.3 ms | 剔除后基数小；radix on batch key |
| 合批/写缓冲 | 1.2 ms | 批键分桶（数百批）；实例 memcpy 带宽级 |
| 命令录制 | 0.5 ms | 次级命令缓冲按 pass 复用；bindless 零重绑 |
| GPU 执行 | 8 ms @1660 1080p | 实例化 draw ≤ 200 次；粒子 additive 合批；过载时质量分级降粒子 |
| **合计 CPU 渲染线程** | **≤ 4 ms** | 与 00 文档红线一致 |

## 10. 移植对照速查（本册涉及）

| 移植项 | 源 | 方式 |
|---|---|---|
| 提取-双缓冲-插值全套 | Luma `Application/RenderableManager.h/.cpp` | 直接拷贝改造（MIT，保留声明） |
| POD 批键 + 预计算哈希 | Luma `Application/SceneRenderer.h` | 直接拷贝改造 |
| 粒子数据布局/池/BlendMode 管线 | Luma `Data/ParticleData.h`、`Particles/*` | 近原样移植 |
| RHI 切面（Context/ShaderCache/MSAA/设备丢失） | Luma `Renderer/Nut/*` | 接口蓝本重实现（Vulkan 原语） |
| per-instance SSBO + 脏更新 | Looper `engine/renderer/renderer.cpp` | 实现期对照（许可待核实，暂不拷贝） |
| chunk 烘焙 + 相机剔除 margin | Editor-RPG2D `Chunk.cpp`/`Map.cpp` | 思想移植 + C++ 重写（可拷，注明出处） |
| 顶点格式（32B/整数字节色） | yami `webgl.ts` | 仅借鉴格式思想 |
| 2D 光照/SDF 阴影 | Luma `Renderer/DeferredRenderer` 等 + `Shaders/*.wgsl` | **v1 未采用（2026-10-11 ADR-019 改光贴图合成）**；法线路径二阶段触发时再议直译 |

## 11. M1 实测（2026-09-18，RX 590 / MoltenVK 1.3.357，验证层全程开启）

| 判据 | 结果 |
|---|---|
| bench-mow 10 万精灵+5 万粒子 ≥60fps | **107.0fps**（IMMEDIATE，全可见；FIFO 61–62 贴 vsync），GPU 1.52ms |
| CPU 渲染线程 ≤4ms（§9 压测 A 语境：1 万精灵+10 万粒子） | **3.05ms**（extract 1.96 + bake 1.08 + record 0.008）|
| 粒子 10 万 GPU ≤4ms | **0.74ms** @ 存活 8.7 万 |
| 图集切换不闪帧 | 两图集三段，批数恒定 4，帧间零波动 |
| 设备丢失模拟恢复 | 帧计数保持，验证层零错误 |

要点与偏差：
- 合批实测 4 批（精灵1+粒子2+文本1）画 15 万实例；bindless + push constant 路径成立。
- 实例环形 SSBO 3 段 × 弹性扩容（131072×3×48B=18MB @10 万级），fence 保护无泄漏。
- §9 预算的"提取 1.0ms"是压测 A 剔除后 1.5k 可见实体口径；bench-mow 10 万**全可见**
  时提取 4.1ms（剔除遍历 O(全实体)），判据按压测 A 构成实测 3.05ms 达标。
- 热路径优化四件套（批键完整哈希分组 / 计数桶免排序 / sin-cos LUT / 单遍提取+搬运
  分桶）与三条 MoltenVK 教训记录于 `docs/DevLog/` 对应日期条目。
- 帧时间看门狗（EMA>250ms 中止）进全部 bench——防"填充率失控拖死桌面"事故重演。
