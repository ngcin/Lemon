# Lemon 引擎设计 — 02 渲染内核（Vulkan）

> 目标：一条管线画完"万级怪物 + 弹幕 + 粒子 + 地形 + HUD"，模拟与渲染解耦，Windows 原生 Vulkan 1.3。
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

// sprite.frag —— 路径 A（Lit 变体在 M8 增加法线采样分支）
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
[Light Pass] (M8) 法线RT → 光照合成RT（见 §8）
[FX Pass]    后处理链（Med 以上开启）：Bloom(阈值1/4降采样) → HitFlash/Shockwave(UV扰动) → 色调曲线 → CRT/扫描线(可选风格)
[UI Pass]    HUD（ImGui 视口直绘）+ 位图数字/血条（走路径 A，同为 sprite）
[Editor Pass] 仅编辑器：网格/Gizmo/选择框/刷子预览（线与矩形批）
```

后处理为"链式 full-screen pass 池"，每个 pass 双 RT ping-pong；关闭 FX 时 game pass 直接渲到 swapchain。**全屏 UV 扰动类特效**（冲击波/传送门）是割草手感的廉价大杀器，进 M5 首发特性集。

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
- **v2（M8 后按需）**：SDF 文本（msdf-atlas-gen 离线生成 + 运行时 outline/shadow 着色器变体），服务游戏内富文本（ARPG 对话）。**决策点**：若 v1 + ImGui 已覆盖品类需求，v2 可无限期推迟。

## 8. 2D 光照与阴影（M8，非阻塞增强）

借鉴 Luma 延迟光照全套（`Renderer/DeferredRenderer`、`Systems/ShadowRenderer`、`Shaders/GBuffer/DeferredLighting/SDFShadow.wgsl`），但**裁剪到 2D 割草实际需要的子集**：

- GBuffer-lite：法线 RT（sprite 法线贴图通道，Prowl Sprite 的 `_NormalMap` 思想）+ 颜色 RT。
- 光源：ambient + 点光/锥光（数量上限 64/帧，分档）；合成 pass 相乘+加法混合。
- 阴影：**blob shadow**（椭圆暗斑，M8 必做）+ SDF 几何硬阴影（**可选**，Luma 的 SDFShadow.wgsl WGSL→GLSL 直译，成本高收益看美术风格）。
- 光照全部在 `Light Pass`，关灯零成本（默认管线 = 无光照直出，符合像素风默认审美）。

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
| 2D 光照/SDF 阴影 | Luma `Renderer/DeferredRenderer` 等 + `Shaders/*.wgsl` | M8 裁剪移植（WGSL→GLSL 直译） |

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
