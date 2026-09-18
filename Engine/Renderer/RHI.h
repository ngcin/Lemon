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

namespace lemon::rhi {

constexpr uint32_t kInvalid = 0;
constexpr uint32_t kMaxTextureSlots = 64;   // bindless sampled image 数组容量
constexpr uint32_t kMaxSamplerSlots = 8;
constexpr uint32_t kMaxPushConstants = 128; // Vulkan 保证下限，M1 用 48B

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

// ------------------------------------------------------------ CommandList --
// 由 Device 拥有（每帧在途 2 个）；BeginFrame() 取得本帧录制器，EndFrameAndPresent 提交。
class CommandList {
public:
    void BeginPass(Format colorFormat, uint32_t w, uint32_t h, const float clearColor[4]);
    void EndPass();
    void BindPipeline(Pipeline p);
    void BindQuadGeometry(Buffer cornerVB, Buffer indexIB); // 路径 A 四边形几何（每帧一次）
    void BindGlobalDescriptors();                            // set 0：bindless 纹理/采样器数组
    void BindStorageBuffer(Buffer ssbo);                     // 实例环形 SSBO（每帧一次）
    void SetViewportScissor(uint32_t w, uint32_t h);
    void PushConstants(const void* data, uint32_t size);     // ≤ kMaxPushConstants，vert|frag
    void DrawQuadInstances(uint32_t instanceCount, uint32_t baseInstance);

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
    void UploadTexture(Texture, const void* rgba8Pixels, uint64_t byteSize); // staging 一次性上传（含 mip 链）
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
    // 恢复时按注册顺序调用（纹理/缓冲重上传、管线经缓存重建）；游戏态不丢
    void AddRecreateCallback(const char* name, std::function<void(Device&)> fn);
    void SavePipelineCache();                 // preheat 后 / 退出前调用

    const DeviceInfo& Info() const;

private:
    friend class CommandList; // 录制器需要访问 Device::Impl（同模块 .cpp 内）
    Device();
    struct Impl;
    std::unique_ptr<Impl> m;
};

} // namespace lemon::rhi
