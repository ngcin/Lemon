// Lemon 引擎 — RHI 薄层公共接口（02 文档 §2）
// 纪律：本头文件【零 Vulkan 类型】；全部实现细节 Pimpl 藏在 RHI.cpp。
//   * 句柄 = 资源表索引（0 无效）；M1 不做代际校验，调试期 LEMON_ASSERT 兜底
//   * 全 M1 绘制走"路径 A"：共享单位四边形 + per-instance SSBO + bindless 纹理数组，
//     管线顶点输入因此内固定（无需通用顶点布局机制；路径 B 进 M6 时再扩）
//   * 帧同步：fence-only acquire + 按交换链图像持有 present 信号量（spike-01 验证层归零方案）
//   * 设备丢失：销毁并重建全部 GPU 资源，游戏态不丢（02 §2 纪律表）
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace lemon::rhi {

constexpr uint32_t kInvalid = 0;
// T3d 过渡容量（64→256，2026-09-27）：多角色多 clip 项目（svr-test 双怪 111 张
// 帧图）超出 M4 最小集——描述符数组/池随本常量同源缩放，显存开销可忽略；真正的
// 解法是 M6c 图集打包（多图合页后槽位需求骤减），届时回落评估。
constexpr uint32_t kMaxTextureSlots = 256;  // bindless sampled image 数组容量
constexpr uint32_t kMaxSamplerSlots = 8;
constexpr uint32_t kMaxPushConstants = 128; // Vulkan 保证下限，M1 用 48B
// 实例环 SSBO 数组槽位数（binding 2；每视口合批器固定占一槽——与 sprite.vert 的
// rings[4] 数组长度一致，改动需两侧同步）
constexpr uint32_t kRingSsboSlots = 4;

// ---------------------------------------------------------------- 句柄 ----
struct Buffer   { uint32_t id = kInvalid; bool IsValid() const { return id != kInvalid; } };
struct Texture  { uint32_t id = kInvalid; bool IsValid() const { return id != kInvalid; } };
struct Sampler  { uint32_t id = kInvalid; bool IsValid() const { return id != kInvalid; } };
struct Shader   { uint32_t id = kInvalid; bool IsValid() const { return id != kInvalid; } };
struct Pipeline { uint32_t id = kInvalid; bool IsValid() const { return id != kInvalid; } };

// ---------------------------------------------------------------- 枚举 ----
enum class Format : uint8_t { Undefined, RGBA8Unorm, BGRA8UnormSrgb, RGBA8UnormSrgb, RGBA16Float };
enum class BlendMode : uint8_t { Opaque, Alpha, Additive, Multiply, Count };
enum class FilterMode : uint8_t { Point, Linear };
enum class AddressMode : uint8_t { Repeat, ClampToEdge, MirrorRepeat };
enum class PresentModePref : uint8_t { Fifo, AutoBest, Immediate }; // IMMEDIATE 仅 bench（02 §3.5）
enum class ShaderStage : uint8_t { Vertex, Fragment };

// ---------------------------------------------------------------- 描述 ----
struct DeviceDesc {
    const char* appName = "lemon";
    bool debugLayer = false;        // 验证层第一天就开（AGENTS 纪律）
    const char* pipelineCachePath = ".lemon/pipeline-cache.bin"; // 磁盘管线缓存（null = 不落盘）
};

struct SwapchainDesc {
    void* nativeWindow = nullptr;  // SDL_Window*（平台层透明传递）
    PresentModePref present = PresentModePref::Fifo;
};

enum class BufferUsage : uint32_t {
    Vertex = 1u << 0,
    Index = 1u << 1,
    Storage = 1u << 2,   // per-instance SSBO
    TransferSrc = 1u << 3,
    TransferDst = 1u << 4,
};

struct BufferDesc {
    uint64_t size = 0;
    uint32_t usage = 0;             // BufferUsage 位或
    bool hostMapped = true;         // 常驻映射（HOST_VISIBLE|COHERENT, 顺序写）
    const char* debugName = "buffer";
};

struct TextureDesc {
    uint32_t width = 1, height = 1;
    uint32_t mipLevels = 1;
    Format format = Format::RGBA8Unorm;
    bool generateMips = false;
    bool renderTarget = false;  // 附加 COLOR_ATTACHMENT 用途（编辑器视口离屏 RT，内核 #11）
    const char* debugName = "texture";
};

struct SamplerDesc {
    FilterMode min = FilterMode::Linear, mag = FilterMode::Linear;
    AddressMode u = AddressMode::ClampToEdge, v = AddressMode::ClampToEdge;
};

struct PipelineDesc {
    Shader vs, fs;
    BlendMode blend = BlendMode::Alpha;
    Format colorFormat = Format::BGRA8UnormSrgb; // 交换链格式（动态渲染下管线唯一格式依赖）
};

struct AcquireResult {
    int32_t imageIndex = -1;  // >= 0 可用
    bool needsRecreate = false;
    bool deviceLost = false;
};

struct FrameTiming {
    double gpuMs = -1.0;   // 上一帧 GPU 时间（时间戳对差；-1 = 尚无数据）
    bool valid = false;
};

struct DeviceInfo {
    const char* deviceName = "";
    uint32_t apiVersion = 0;      // 物理设备支持的最高 Vulkan 版本
    bool discrete = false;
    bool timestampsSupported = false;
};

// ---- Vulkan 互操作句柄（07 例外登记：仅供编辑器 ImGui/外置后端 glue 使用）----
// 纪律：除 Engine/Renderer 的 .cpp 与 Editor 的后端 glue TU 外，任何编译单元
// 不得持有 Vulkan 类型；本结构经 void* 传递原始句柄，是 Vulkan 零泄漏纪律的
// 显式豁免口（引擎语义层（视口渲染等）仍一律走自有句柄，不得使用本通道）。
struct VulkanInteropHandles {
    void* instance = nullptr;          // VkInstance
    void* physicalDevice = nullptr;    // VkPhysicalDevice
    void* device = nullptr;            // VkDevice
    void* queue = nullptr;             // VkQueue（图形/呈现队列）
    uint32_t queueFamily = 0;
    uint32_t apiVersion = 0;           // 实例创建时使用的 apiVersion
    uint32_t swapchainImageCount = 0;
};

// ------------------------------------------------------------ CommandList --
// 由 Device 拥有（每帧在途 2 个）；BeginFrame() 取得本帧录制器，EndFrameAndPresent 提交。
class CommandList {
public:
    void BeginPass(Format colorFormat, uint32_t w, uint32_t h, const float clearColor[4]);
    /// 离屏渲染块（内核 #11：视口 RT 显式声明规约）：渲到 renderTarget 纹理；
    /// EndPass 时转 SHADER_READ_ONLY 供 UI 采样。与 BeginPass 互斥，一帧可交替多块。
    void BeginOffscreenPass(Texture target, const float clearColor[4]);
    void EndPass();
    void BindPipeline(Pipeline p);
    void BindQuadGeometry(Buffer cornerVB, Buffer indexIB); // 路径 A 四边形几何（每帧一次）
    void BindGlobalDescriptors();                            // set 0：bindless 纹理/采样器数组
    /// 实例环形 SSBO → 数组槽（每合批器/视口固定一槽，帧内不改写；M4.7-P0 语义）
    void BindStorageBuffer(Buffer ssbo, uint32_t slot = 0);
    void SetViewportScissor(uint32_t w, uint32_t h);
    void PushConstants(const void* data, uint32_t size);     // ≤ kMaxPushConstants，vert|frag
    void DrawQuadInstances(uint32_t instanceCount, uint32_t baseInstance);
    /// 调试截屏：录制当帧交换链图像 → 中转缓冲（须在 EndPass 之后、EndFrameAndPresent
    /// 之前调用；随后 Device::DebugFetchCapture 取回）。见 Device 段注释。
    void DebugRecordCapture();
    /// 指定纹理回读（须在该纹理 EndPass 之后调用；DebugFetchTextureCapture 取回）。
    /// M4.7-P0：冒烟像素断言扫场景 RT（线性空间，无 UI 合成/sRGB 干扰）
    void DebugRecordTextureCapture(Texture tex);
    /// 本帧原始命令缓冲（void* = VkCommandBuffer）。与 VulkanInteropHandles 同一豁免口：
    /// 仅编辑器后端 glue 与 Renderer 内兄弟后端（RmlUiBackend，gameRT 渲染块内追加
    /// 录制）使用，引擎语义层不得调用。
    void* NativeCommandBuffer() const;

private:
    friend class Device;
    struct Impl;
    Impl* m = nullptr;
};

// ---------------------------------------------------------------- Device --
class Device {
public:
    static std::unique_ptr<Device> Create(const DeviceDesc& desc);
    ~Device();

    // --- 交换链 ---
    bool CreateSwapchain(const SwapchainDesc& desc);
    bool RecreateSwapchain();                 // 尺寸变化 / OUT_OF_DATE / 丢失后
    Format SwapchainFormat() const;
    uint32_t SwapchainWidth() const;
    uint32_t SwapchainHeight() const;
    const char* PresentModeName() const;

    // --- 资源（创建后句柄稳定，直到 Destroy 或设备丢失重建后作废并触发回调重建）---
    Buffer CreateBuffer(const BufferDesc&);
    void* MapBuffer(Buffer);                  // hostMapped=true 时常驻指针
    void DestroyBuffer(Buffer);
    Texture CreateTexture(const TextureDesc&);
    bool UploadTexture(Texture, const void* rgba8Pixels, uint64_t byteSize); // staging 一次性上传（含 mip 链）；false = 设备丢失未上传（M3：恢复由帧循环统一驱动）
    void DestroyTexture(Texture);
    Sampler CreateSampler(const SamplerDesc&);
    Shader CreateShader(ShaderStage, const uint32_t* spirv, size_t wordCount); // 内部按 SPIR-V 哈希去重
    Pipeline CreatePipeline(const PipelineDesc&);

    // --- bindless 槽位（写全局描述符集；slot 上限 kMaxTextureSlots/kMaxSamplerSlots）---
    void BindTextureToSlot(Texture, uint32_t slot);
    void BindSamplerToSlot(Sampler, uint32_t slot);

    // --- 帧循环（单线程 M1；模拟与渲染分线程在 M2 沿用同一 API）---
    AcquireResult AcquireNextImage();
    CommandList& BeginFrame();
    void EndFrameAndPresent(bool& needsRecreateOut, bool& deviceLostOut);
    void WaitIdle();

    // --- GPU 时间戳（EnableTimestamps 后 BeginPass/EndPass 自动写入）---
    void EnableTimestamps();
    FrameTiming LastFrameTiming() const;

    // --- 设备丢失 ---
    bool IsDeviceLost() const;
    void SimulateDeviceLoss();                // 验收钩子：走与真实丢失同一条恢复路径
    // 恢复时按注册顺序调用（纹理/缓冲重上传、管线经缓存重建）；游戏态不丢。
    // 返回 token；拥有者析构时 RemoveRecreateCallback 反注册（M9：捕获 this 的
    // 回调若不摘除，拥有者先于设备销毁后设备丢失重建 = UAF）。契约：反注册方
    // 必须在 Device 存活期间析构——编辑器 Run() 收尾显式 viewport_.reset() 先于
    // device_.reset()（成员声明序不救：Run 内显式 reset 早于成员析构）；样例栈序
    // 天然满足。
    using RecreateCallbackId = uint64_t;
    RecreateCallbackId AddRecreateCallback(const char* name, std::function<void(Device&)> fn);
    void RemoveRecreateCallback(RecreateCallbackId id);
    /// 设备丢失销毁前回调（review 2026-10-02 #24）：HandleDeviceLost 在销毁任何
    /// 句柄前调用——持有原生 VkDevice 对象、不经 RHI 资源表回收的拥有者（编辑器
    /// ImGui 后端）在此窗口合法释放（丢失态下的销毁调用被规范允许，可能 no-op）。
    /// 按注册序调用；token/反注册语义与 RecreateCallback 一致。
    RecreateCallbackId AddPreDestroyCallback(const char* name, std::function<void(Device&)> fn);
    void RemovePreDestroyCallback(RecreateCallbackId id);
    void SavePipelineCache();                 // preheat 后 / 退出前调用

    const DeviceInfo& Info() const;

    // --- Vulkan 互操作（编辑器 ImGui 后端专用；见 VulkanInteropHandles 注释）---
    VulkanInteropHandles GetVulkanInterop() const;
    /// 纹理原生视图（void* = VkImageView；ImGui_ImplVulkan_AddTexture 用，同一豁免口）
    void* GetVulkanTextureViewInterop(Texture t);

    // --- 内部桥（M6b 批③a，ADR-014）：Engine/Renderer 内兄弟后端 .cpp 专用（当前
    //     唯一消费者 RmlUiBackend.cpp）。与 VulkanInteropHandles 同一零泄漏豁免逻辑，
    //     但消费者是 Renderer 内部而非编辑器 glue；编辑器/游戏语义层禁用。 ---
    struct InternalBridge {
        void* device = nullptr;          // VkDevice
        void* physicalDevice = nullptr;  // VkPhysicalDevice
        void* allocator = nullptr;       // VmaAllocator
        void* queue = nullptr;           // VkQueue（图形/呈现）
        uint32_t queueFamily = 0;
    };
    const InternalBridge& GetInternalBridge();
    /// 一次性独占提交（record(commandBuffer = VkCommandBuffer, userData)；纹理上传用）
    using ImmediateRecordFn = void (*)(void* commandBuffer, void* userData);
    void InternalImmediateSubmit(ImmediateRecordFn record, void* userData);

    // --- 调试截屏（编辑器冒烟/CI 视觉回归用；非热路径）---
    // CommandList::DebugRecordCapture()（EndPass 后调用）录制"交换链图像 → 中转缓冲"拷贝；
    // 随后本接口取回内容（内部 WaitIdle）。RGBA8 字节序，自左上角行优先；
    // 交换链为 BGRA 时自动交换通道；alpha 恒写 255（不透明合成）。
    bool DebugFetchCapture(std::vector<uint8_t>& rgbaOut, uint32_t& w, uint32_t& h);
    // CommandList::DebugRecordTextureCapture(tex) 的取回口（M4.7-P0：冒烟扫场景 RT）；
    // 原样拷贝 RGBA8（线性空间，无通道交换——RT 恒 RGBA）
    bool DebugFetchTextureCapture(std::vector<uint8_t>& rgbaOut, uint32_t& w, uint32_t& h);

private:
    friend class CommandList; // 录制器需要访问 Device::Impl（同模块 .cpp 内）
    Device();
    struct Impl;
    std::unique_ptr<Impl> m;
};

} // namespace lemon::rhi
