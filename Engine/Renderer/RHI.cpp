// Lemon 引擎 — RHI 实现（02 文档 §2，唯一触碰 Vulkan/VMA 的编译单元）
// 验证过的引导路径承自 spike-01/02；本文件新增：特性链(descriptorIndexing/hostQueryReset/
// dynamicRendering/sync2)、bindless 全局描述符集 + 实例 SSBO 槽、动态渲染（无 renderPass 对象）、
// 管线磁盘缓存、GPU 时间戳、设备丢失销毁重建。
#include "Renderer/RHI.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <vector>

#include <vulkan/vulkan.h>
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wall"
#pragma clang diagnostic ignored "-Wextra"
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wnullability-completeness"
#pragma clang diagnostic ignored "-Wunused-private-field"
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>
#pragma clang diagnostic pop

#include "Core/Log.h"

namespace lemon::rhi {

static constexpr uint32_t kFramesInFlight = 2;

static const char* VkResultName(VkResult r) {
    switch (r) {
        case VK_SUCCESS: return "SUCCESS";
        case VK_SUBOPTIMAL_KHR: return "SUBOPTIMAL";
        case VK_ERROR_OUT_OF_DATE_KHR: return "OUT_OF_DATE";
        case VK_ERROR_DEVICE_LOST: return "DEVICE_LOST";
        case VK_TIMEOUT: return "TIMEOUT";
        default: return "OTHER";
    }
}
#define VK_CHECK(x)                                                               \
    do {                                                                          \
        VkResult _r = (x);                                                        \
        if (_r != VK_SUCCESS) {                                                   \
            LEMON_ASSERT(false, "Vulkan error %s(%d) at %s:%d", VkResultName(_r), \
                         (int)_r, __FILE__, __LINE__);                            \
        }                                                                         \
    } while (0)

static uint64_t HashWords(const uint32_t* w, size_t n) {
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; ++i) {
        h ^= w[i];
        h *= 1099511628211ull;
    }
    return h;
}

static VkFormat ToVk(Format f) {
    switch (f) {
        case Format::RGBA8Unorm: return VK_FORMAT_R8G8B8A8_UNORM;
        case Format::BGRA8UnormSrgb: return VK_FORMAT_B8G8R8A8_SRGB;
        case Format::RGBA8UnormSrgb: return VK_FORMAT_R8G8B8A8_SRGB;
        case Format::RGBA16Float: return VK_FORMAT_R16G16B16A16_SFLOAT;
        default: return VK_FORMAT_UNDEFINED;
    }
}

static VkBlendFactor BlendSrcColor(BlendMode m) {
    if (m == BlendMode::Additive) return VK_BLEND_FACTOR_SRC_ALPHA;
    if (m == BlendMode::Multiply) return VK_BLEND_FACTOR_DST_COLOR;
    return VK_BLEND_FACTOR_SRC_ALPHA;
}
static VkBlendFactor BlendDstColor(BlendMode m) {
    if (m == BlendMode::Additive) return VK_BLEND_FACTOR_ONE;
    if (m == BlendMode::Multiply) return VK_BLEND_FACTOR_ZERO;
    return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
}

// ---------------------------------------------------------- CommandList::Impl
struct CommandList::Impl {
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    void* dev = nullptr;         // Device::Impl*（定义顺序原因用 void*，仅本文件内转换）
    uint32_t frameSlot = 0;      // 时间戳槽基址 = frameSlot*2
    uint32_t imageIndex = 0;     // 当前交换链图像（BeginPass 取视图）
};

// ------------------------------------------------------------------ 资源表 --
struct BufferRes {
    VkBuffer buf = VK_NULL_HANDLE;
    VmaAllocation alloc = VK_NULL_HANDLE;
    void* mapped = nullptr;
    BufferDesc desc;
};
struct TextureRes {
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation alloc = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    uint32_t realMipLevels = 1;
    TextureDesc desc;
};
struct SamplerRes { VkSampler sampler = VK_NULL_HANDLE; };
struct ShaderRes { VkShaderModule module = VK_NULL_HANDLE; uint64_t hash = 0; };
struct PipelineRes { VkPipeline pipe = VK_NULL_HANDLE; PipelineDesc desc; };

// ------------------------------------------------------------------ Impl --
struct Device::Impl {
    DeviceDesc desc;
    DeviceInfo info;

    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties physProps{};
    uint32_t graphicsFamily = UINT32_MAX;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;

    // 交换链（单条，M1 够用）
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat swapFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D swapExtent{};
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
    PresentModePref presentPref = PresentModePref::Fifo;
    std::vector<VkImage> swapImages;
    std::vector<VkImageView> swapViews;
    std::vector<VkSemaphore> presentSemaphores; // 按交换链图像持有（spike-01 验证方案）
    SDL_Window* window = nullptr;
    uint32_t imageIndex = 0;

    // 帧同步
    struct FrameSlot {
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        VkFence inFlight = VK_NULL_HANDLE;
    };
    FrameSlot slots[kFramesInFlight];
    VkCommandPool cmdPool = VK_NULL_HANDLE;
    VkFence acquireFence = VK_NULL_HANDLE;
    uint64_t frameCounter = 0;

    VkCommandPool uploadPool = VK_NULL_HANDLE; // 一次性提交（staging）

    // bindless 全局集：binding0 采样图数组(64) / binding1 采样器数组(8) / binding2 实例 SSBO
    VkDescriptorSetLayout bindlessLayout = VK_NULL_HANDLE;
    VkDescriptorPool bindlessPool = VK_NULL_HANDLE;
    VkDescriptorSet bindlessSet = VK_NULL_HANDLE;
    uint32_t boundStorageBufferId = 0; // SSBO 是单一大环形缓冲：绑定一次，帧内不再变

    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE; // 全 M1 管线共享
    VkPipelineCache pipelineCache = VK_NULL_HANDLE;
    bool pipelineCacheDirty = false;

    VkQueryPool tsPool = VK_NULL_HANDLE;
    bool timestamps = false;
    FrameTiming lastTiming{};

    // 资源表（句柄 = 下标+1）
    std::vector<BufferRes> buffers;
    std::vector<uint32_t> bufferFree;
    std::vector<TextureRes> textures;
    std::vector<uint32_t> textureFree;
    std::vector<SamplerRes> samplers;
    std::vector<uint32_t> samplerFree;
    std::vector<ShaderRes> shaders;
    std::vector<uint32_t> shaderFree;
    std::vector<PipelineRes> pipelines;
    std::vector<uint32_t> pipelineFree;
    std::unordered_map<uint64_t, uint32_t> shaderByHash;

    std::vector<std::pair<const char*, std::function<void(Device&)>>> recreateCallbacks;
    bool deviceLost = false;

    // CommandList（Device 持有，BeginFrame 刷新指向）
    CommandList cmdList;
    CommandList::Impl cmdListImpl;

    // ---------------------------------------------------------------- 引导
    void CreateInstance() {
        uint32_t instApi = VK_API_VERSION_1_0;
        vkEnumerateInstanceVersion(&instApi);
        instApi = std::min(instApi, (uint32_t)VK_API_VERSION_1_3);

        uint32_t extCount = 0;
        const char* const* sdlExts = SDL_Vulkan_GetInstanceExtensions(&extCount);
        LEMON_ASSERT(sdlExts, "SDL_Vulkan_GetInstanceExtensions failed");
        std::vector<const char*> extensions(sdlExts, sdlExts + extCount);

        bool portability = false, debugUtils = false;
        {
            uint32_t n = 0;
            vkEnumerateInstanceExtensionProperties(nullptr, &n, nullptr);
            std::vector<VkExtensionProperties> props(n);
            vkEnumerateInstanceExtensionProperties(nullptr, &n, props.data());
            for (auto& p : props) {
                if (!std::strcmp(p.extensionName, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
                    extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
                    portability = true;
                } else if (!std::strcmp(p.extensionName, VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
                    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
                    debugUtils = true;
                }
            }
        }

        std::vector<const char*> layers;
        if (desc.debugLayer) {
            uint32_t n = 0;
            vkEnumerateInstanceLayerProperties(&n, nullptr);
            std::vector<VkLayerProperties> props(n);
            vkEnumerateInstanceLayerProperties(&n, props.data());
            for (auto& p : props)
                if (!std::strcmp(p.layerName, "VK_LAYER_KHRONOS_validation")) {
                    layers.push_back("VK_LAYER_KHRONOS_validation");
                    LEMON_LOG("validation layer enabled");
                }
        }

        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.pApplicationName = desc.appName;
        app.pEngineName = "Lemon";
        app.apiVersion = instApi;
        VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        ci.pApplicationInfo = &app;
        ci.enabledExtensionCount = (uint32_t)extensions.size();
        ci.ppEnabledExtensionNames = extensions.data();
        ci.enabledLayerCount = (uint32_t)layers.size();
        ci.ppEnabledLayerNames = layers.data();
        if (portability) ci.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
        VK_CHECK(vkCreateInstance(&ci, nullptr, &instance));

        if (debugUtils) {
            auto create = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
                instance, "vkCreateDebugUtilsMessengerEXT");
            if (create) {
                VkDebugUtilsMessengerCreateInfoEXT mi{
                    VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
                mi.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                     VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
                mi.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
                mi.pfnUserCallback =
                    [](VkDebugUtilsMessageSeverityFlagBitsEXT sev, VkDebugUtilsMessageTypeFlagsEXT,
                       const VkDebugUtilsMessengerCallbackDataEXT* data, void*) -> VkBool32 {
                    const char* tag = (sev & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
                                          ? "VALIDATION-ERROR"
                                          : "VALIDATION-WARN";
                    LogMsg(LogLevel::Error, "%s: %s", tag, data->pMessage);
                    return VK_FALSE;
                };
                create(instance, &mi, nullptr, &messenger);
            }
        }
    }

    void PickPhysicalAndLogical() {
        uint32_t devCount = 0;
        vkEnumeratePhysicalDevices(instance, &devCount, nullptr);
        LEMON_ASSERT(devCount > 0, "no Vulkan physical device");
        std::vector<VkPhysicalDevice> devices(devCount);
        vkEnumeratePhysicalDevices(instance, &devCount, devices.data());

        VkPhysicalDevice chosen = VK_NULL_HANDLE;
        std::vector<const char*> deviceExts;
        for (VkPhysicalDevice dev : devices) {
            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(dev, &props);
            uint32_t qCount = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(dev, &qCount, nullptr);
            std::vector<VkQueueFamilyProperties> queues(qCount);
            vkGetPhysicalDeviceQueueFamilyProperties(dev, &qCount, queues.data());
            uint32_t family = UINT32_MAX;
            for (uint32_t f = 0; f < qCount; ++f) {
                if (!(queues[f].queueFlags & VK_QUEUE_GRAPHICS_BIT)) continue;
                VkBool32 present = VK_FALSE;
                vkGetPhysicalDeviceSurfaceSupportKHR(dev, f, surface, &present);
                if (present) { family = f; break; }
            }
            if (family == UINT32_MAX) continue;
            uint32_t extCount = 0;
            vkEnumerateDeviceExtensionProperties(dev, nullptr, &extCount, nullptr);
            std::vector<VkExtensionProperties> exts(extCount);
            vkEnumerateDeviceExtensionProperties(dev, nullptr, &extCount, exts.data());
            bool hasSwap = false, hasPort = false, hasDynR = false, hasSync2 = false;
            for (auto& e : exts) {
                if (!std::strcmp(e.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) hasSwap = true;
                if (!std::strcmp(e.extensionName, "VK_KHR_portability_subset")) hasPort = true;
                if (!std::strcmp(e.extensionName, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME)) hasDynR = true;
                if (!std::strcmp(e.extensionName, VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME)) hasSync2 = true;
            }
            if (!hasSwap) continue;
            bool needLegacyExt = props.apiVersion < VK_API_VERSION_1_3;
            if (needLegacyExt && (!hasDynR || !hasSync2)) continue; // 动态渲染是 M1 硬前提
            VkPhysicalDeviceProperties cur{};
            if (chosen) vkGetPhysicalDeviceProperties(chosen, &cur);
            bool candDisc = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
            bool curDisc = chosen && cur.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
            if (!chosen || (candDisc && !curDisc)) {
                chosen = dev;
                graphicsFamily = family;
                deviceExts = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
                if (hasPort) deviceExts.push_back("VK_KHR_portability_subset");
                if (needLegacyExt) {
                    deviceExts.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
                    deviceExts.push_back(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
                }
            }
        }
        LEMON_ASSERT(chosen, "no physical device with graphics+present+swapchain+dynamicRendering");
        physical = chosen;
        vkGetPhysicalDeviceProperties(physical, &physProps);

        // --- 特性校验（bindless + 时间戳池重置 + 动态渲染是 M1 硬前提）---
        VkPhysicalDeviceVulkan12Features sup12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
        VkPhysicalDeviceFeatures2 sup2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        if (physProps.apiVersion < VK_API_VERSION_1_3) {
            VkPhysicalDeviceSynchronization2Features supSync2{
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES};
            VkPhysicalDeviceDynamicRenderingFeatures supDyn{
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES};
            sup2.pNext = &supSync2;
            supSync2.pNext = &supDyn;
            supDyn.pNext = &sup12;
            vkGetPhysicalDeviceFeatures2(physical, &sup2);
            LEMON_ASSERT(supSync2.synchronization2, "device lacks synchronization2");
            LEMON_ASSERT(supDyn.dynamicRendering, "device lacks dynamicRendering");
        } else {
            VkPhysicalDeviceVulkan13Features sup13{
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
            sup2.pNext = &sup13;
            sup13.pNext = &sup12;
            vkGetPhysicalDeviceFeatures2(physical, &sup2);
            LEMON_ASSERT(sup13.dynamicRendering && sup13.synchronization2,
                         "device lacks dynamicRendering/synchronization2");
        }
        LEMON_ASSERT(sup12.descriptorIndexing && sup12.descriptorBindingPartiallyBound &&
                         sup12.descriptorBindingSampledImageUpdateAfterBind &&
                         sup12.descriptorBindingStorageBufferUpdateAfterBind,
                     "device lacks descriptorIndexing subset (bindless)");
        LEMON_ASSERT(sup12.hostQueryReset, "device lacks hostQueryReset (timestamps)");

        float prio = 1.0f;
        VkDeviceQueueCreateInfo queueCI{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queueCI.queueFamilyIndex = graphicsFamily;
        queueCI.queueCount = 1;
        queueCI.pQueuePriorities = &prio;

        VkPhysicalDeviceVulkan12Features want12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
        want12.descriptorIndexing = VK_TRUE;
        want12.descriptorBindingPartiallyBound = VK_TRUE;
        want12.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
        want12.descriptorBindingStorageBufferUpdateAfterBind = VK_TRUE;
        want12.hostQueryReset = VK_TRUE;

        VkPhysicalDeviceDynamicRenderingFeatures wantDyn{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES};
        wantDyn.dynamicRendering = VK_TRUE;
        VkPhysicalDeviceSynchronization2Features wantSync2{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES};
        wantSync2.synchronization2 = VK_TRUE;
        VkPhysicalDeviceVulkan13Features want13{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        want13.dynamicRendering = VK_TRUE;
        want13.synchronization2 = VK_TRUE;

        VkDeviceCreateInfo devCI{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        devCI.queueCreateInfoCount = 1;
        devCI.pQueueCreateInfos = &queueCI;
        devCI.enabledExtensionCount = (uint32_t)deviceExts.size();
        devCI.ppEnabledExtensionNames = deviceExts.data();
        if (physProps.apiVersion >= VK_API_VERSION_1_3) {
            want13.pNext = &want12; // 13 → 12 特性链
            devCI.pNext = &want13;
        } else {
            wantDyn.pNext = &wantSync2; // 1.2 设备：KHR 扩展特性链挂 12 特性下
            want12.pNext = &wantDyn;
            devCI.pNext = &want12;
        }
        VK_CHECK(vkCreateDevice(physical, &devCI, nullptr, &device));
        vkGetDeviceQueue(device, graphicsFamily, 0, &queue);

        {
            VmaAllocatorCreateInfo ci{};
            ci.physicalDevice = physical;
            ci.device = device;
            ci.instance = instance;
            ci.vulkanApiVersion = std::min(physProps.apiVersion, (uint32_t)VK_API_VERSION_1_3);
            VK_CHECK(vmaCreateAllocator(&ci, &allocator));
        }

        info.deviceName = physProps.deviceName;
        info.apiVersion = physProps.apiVersion;
        info.discrete = physProps.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
        info.timestampsSupported = physProps.limits.timestampComputeAndGraphics != 0;
    }

    void CreateCommandInfrastructure() {
        {
            VkCommandPoolCreateInfo ci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
            ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            ci.queueFamilyIndex = graphicsFamily;
            VK_CHECK(vkCreateCommandPool(device, &ci, nullptr, &cmdPool));
            ci.flags = 0;
            VK_CHECK(vkCreateCommandPool(device, &ci, nullptr, &uploadPool));
        }
        for (auto& s : slots) {
            VkCommandBufferAllocateInfo ci{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
            ci.commandPool = cmdPool;
            ci.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            ci.commandBufferCount = 1;
            VK_CHECK(vkAllocateCommandBuffers(device, &ci, &s.cmd));
            VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
            fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
            VK_CHECK(vkCreateFence(device, &fci, nullptr, &s.inFlight));
        }
        VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        VK_CHECK(vkCreateFence(device, &fci, nullptr, &acquireFence));
    }

    void CreateBindless() {
        const VkDescriptorBindingFlags bindFlags =
            VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;
        VkDescriptorSetLayoutBinding bindings[3]{};
        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        bindings[0].descriptorCount = kMaxTextureSlots;
        bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings[1].binding = 1;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
        bindings[1].descriptorCount = kMaxSamplerSlots;
        bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings[2].binding = 2;
        bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        // 采样器数组：PARTIALLY_BOUND（未用槽不校验）但无需 UPDATE_AFTER_BIND（只在首帧前写）
        VkDescriptorBindingFlags flagArr[3] = {bindFlags, VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT,
                                               bindFlags};
        VkDescriptorSetLayoutBindingFlagsCreateInfo bfci{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO};
        bfci.bindingCount = 3;
        bfci.pBindingFlags = flagArr;
        VkDescriptorSetLayoutCreateInfo lci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, &bfci};
        lci.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
        lci.bindingCount = 3;
        lci.pBindings = bindings;
        VK_CHECK(vkCreateDescriptorSetLayout(device, &lci, nullptr, &bindlessLayout));

        VkDescriptorPoolSize sizes[3]{{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kMaxTextureSlots},
                                      {VK_DESCRIPTOR_TYPE_SAMPLER, kMaxSamplerSlots},
                                      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4}};
        VkDescriptorPoolCreateInfo pci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pci.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
        pci.maxSets = 1;
        pci.poolSizeCount = 3;
        pci.pPoolSizes = sizes;
        VK_CHECK(vkCreateDescriptorPool(device, &pci, nullptr, &bindlessPool));

        VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        ai.descriptorPool = bindlessPool;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts = &bindlessLayout;
        VK_CHECK(vkAllocateDescriptorSets(device, &ai, &bindlessSet));
        boundStorageBufferId = 0;

        // 管线布局：set0 bindless + 128B push constant（vert|frag）
        VkPushConstantRange pc{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                               kMaxPushConstants};
        VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        plci.setLayoutCount = 1;
        plci.pSetLayouts = &bindlessLayout;
        plci.pushConstantRangeCount = 1;
        plci.pPushConstantRanges = &pc;
        VK_CHECK(vkCreatePipelineLayout(device, &plci, nullptr, &pipelineLayout));
    }

    void LoadPipelineCache() {
        VkPipelineCacheCreateInfo ci{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
        std::vector<char> initial;
        if (desc.pipelineCachePath) {
            std::error_code ec;
            std::filesystem::path p(desc.pipelineCachePath);
            if (!ec && std::filesystem::exists(p, ec)) {
                std::ifstream f(p, std::ios::binary);
                f.seekg(0, std::ios::end);
                size_t size = (size_t)f.tellg();
                f.seekg(0, std::ios::beg);
                if (size > 16) {
                    initial.resize(size);
                    f.read(initial.data(), (std::streamsize)size);
                    ci.initialDataSize = size;
                    ci.pInitialData = initial.data();
                    LEMON_LOG("pipeline cache loaded: %s (%zu bytes)", desc.pipelineCachePath, size);
                }
            }
        }
        VK_CHECK(vkCreatePipelineCache(device, &ci, nullptr, &pipelineCache));
    }

    // ---------------------------------------------------------------- 交换链
    void DestroySwapchainObjects() {
        if (swapchain) vkDestroySwapchainKHR(device, swapchain, nullptr);
        swapchain = VK_NULL_HANDLE;
        for (auto s : presentSemaphores) vkDestroySemaphore(device, s, nullptr);
        presentSemaphores.clear();
        for (auto v : swapViews) vkDestroyImageView(device, v, nullptr);
        swapViews.clear();
        swapImages.clear();
    }

    bool CreateSwapchainObjects() {
        VkSurfaceCapabilitiesKHR caps;
        VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &caps));
        int w = 0, h = 0;
        SDL_GetWindowSizeInPixels(window, &w, &h);
        VkExtent2D extent{};
        if (caps.currentExtent.width == UINT32_MAX) {
            extent.width =
                std::clamp((uint32_t)w, caps.minImageExtent.width, caps.maxImageExtent.width);
            extent.height =
                std::clamp((uint32_t)h, caps.minImageExtent.height, caps.maxImageExtent.height);
        } else {
            extent = caps.currentExtent;
        }
        if (extent.width == 0 || extent.height == 0) return false;

        VkSurfaceFormatKHR format{};
        {
            uint32_t n = 0;
            VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &n, nullptr));
            std::vector<VkSurfaceFormatKHR> fmts(n);
            VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &n, fmts.data()));
            format = fmts[0];
            for (auto& f : fmts)
                if ((f.format == VK_FORMAT_B8G8R8A8_SRGB || f.format == VK_FORMAT_R8G8B8A8_SRGB) &&
                    f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) { format = f; break; }
        }
        {
            uint32_t n = 0;
            VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(physical, surface, &n, nullptr));
            std::vector<VkPresentModeKHR> modes(n);
            VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(physical, surface, &n, modes.data()));
            auto has = [&](VkPresentModeKHR m) {
                return std::find(modes.begin(), modes.end(), m) != modes.end();
            };
            // FIFO 保底；AutoBest→MAILBOX(无则 FIFO)；Immediate 仅 bench（02 §3.5）
            if (presentPref == PresentModePref::Immediate)
                presentMode =
                    has(VK_PRESENT_MODE_IMMEDIATE_KHR)
                        ? VK_PRESENT_MODE_IMMEDIATE_KHR
                        : (has(VK_PRESENT_MODE_MAILBOX_KHR) ? VK_PRESENT_MODE_MAILBOX_KHR
                                                             : VK_PRESENT_MODE_FIFO_KHR);
            else if (presentPref == PresentModePref::AutoBest)
                presentMode = has(VK_PRESENT_MODE_MAILBOX_KHR) ? VK_PRESENT_MODE_MAILBOX_KHR
                                                               : VK_PRESENT_MODE_FIFO_KHR;
            else
                presentMode = VK_PRESENT_MODE_FIFO_KHR;
        }

        uint32_t imageCount =
            std::clamp<uint32_t>(3, caps.minImageCount, caps.maxImageCount ? caps.maxImageCount : 8);
        VkSwapchainCreateInfoKHR ci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
        ci.surface = surface;
        ci.minImageCount = imageCount;
        ci.imageFormat = format.format;
        ci.imageColorSpace = format.colorSpace;
        ci.imageExtent = extent;
        ci.imageArrayLayers = 1;
        ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ci.preTransform = caps.currentTransform;
        ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        ci.presentMode = presentMode;
        ci.clipped = VK_TRUE;
        VK_CHECK(vkCreateSwapchainKHR(device, &ci, nullptr, &swapchain));

        uint32_t n = 0;
        VK_CHECK(vkGetSwapchainImagesKHR(device, swapchain, &n, nullptr));
        swapImages.resize(n);
        VK_CHECK(vkGetSwapchainImagesKHR(device, swapchain, &n, swapImages.data()));
        swapFormat = format.format;
        swapExtent = extent;

        swapViews.resize(n);
        for (uint32_t i = 0; i < n; ++i) {
            VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            vci.image = swapImages[i];
            vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
            vci.format = swapFormat;
            vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            VK_CHECK(vkCreateImageView(device, &vci, nullptr, &swapViews[i]));
        }
        presentSemaphores.resize(n);
        for (uint32_t i = 0; i < n; ++i) {
            VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
            VK_CHECK(vkCreateSemaphore(device, &sci, nullptr, &presentSemaphores[i]));
        }
        LEMON_LOG("swapchain: %ux%u images=%u present=%s", extent.width, extent.height, n,
                  presentMode == VK_PRESENT_MODE_FIFO_KHR        ? "FIFO"
                  : presentMode == VK_PRESENT_MODE_MAILBOX_KHR   ? "MAILBOX"
                  : presentMode == VK_PRESENT_MODE_IMMEDIATE_KHR ? "IMMEDIATE"
                                                                 : "?");
        return true;
    }

    // ------------------------------------------------------------ 一次性提交
    void ImmediateSubmit(const std::function<void(VkCommandBuffer)>& record) {
        VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        ai.commandPool = uploadPool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        VK_CHECK(vkAllocateCommandBuffers(device, &ai, &cmd));
        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VK_CHECK(vkBeginCommandBuffer(cmd, &bi));
        record(cmd);
        VK_CHECK(vkEndCommandBuffer(cmd));
        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cmd;
        VK_CHECK(vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE));
        VK_CHECK(vkQueueWaitIdle(queue));
        vkFreeCommandBuffers(device, uploadPool, 1, &cmd);
    }

    void TransitionImage(VkCommandBuffer cmd, VkImage image, uint32_t levelCount,
                         VkImageLayout oldL, VkImageLayout newL, VkAccessFlags srcAccess,
                         VkPipelineStageFlags srcStage, VkAccessFlags dstAccess,
                         VkPipelineStageFlags dstStage) {
        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = image;
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, levelCount, 0, 1};
        b.oldLayout = oldL;
        b.newLayout = newL;
        b.srcAccessMask = srcAccess;
        b.dstAccessMask = dstAccess;
        vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &b);
    }

    // ---------------------------------------------------------------- 丢失
    void DestroyAllGpuState() {
        DestroySwapchainObjects();
        for (auto& r : buffers)
            if (r.buf) vmaDestroyBuffer(allocator, r.buf, r.alloc);
        buffers.clear(); bufferFree.clear();
        for (auto& r : textures) {
            if (r.view) vkDestroyImageView(device, r.view, nullptr);
            if (r.image) vmaDestroyImage(allocator, r.image, r.alloc);
        }
        textures.clear(); textureFree.clear();
        for (auto& r : samplers)
            if (r.sampler) vkDestroySampler(device, r.sampler, nullptr);
        samplers.clear(); samplerFree.clear();
        for (auto& r : shaders)
            if (r.module) vkDestroyShaderModule(device, r.module, nullptr);
        shaders.clear(); shaderFree.clear(); shaderByHash.clear();
        for (auto& r : pipelines)
            if (r.pipe) vkDestroyPipeline(device, r.pipe, nullptr);
        pipelines.clear(); pipelineFree.clear();
        if (tsPool) vkDestroyQueryPool(device, tsPool, nullptr);
        tsPool = VK_NULL_HANDLE;
        timestamps = false;
        if (pipelineCache) vkDestroyPipelineCache(device, pipelineCache, nullptr);
        pipelineCache = VK_NULL_HANDLE;
        if (pipelineLayout) vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        pipelineLayout = VK_NULL_HANDLE;
        bindlessSet = VK_NULL_HANDLE; // UPDATE_AFTER_BIND 池不支持 free 单 set，随池销毁
        boundStorageBufferId = 0;
        if (bindlessPool) vkDestroyDescriptorPool(device, bindlessPool, nullptr);
        bindlessPool = VK_NULL_HANDLE;
        if (bindlessLayout) vkDestroyDescriptorSetLayout(device, bindlessLayout, nullptr);
        bindlessLayout = VK_NULL_HANDLE;
        for (auto& s : slots) {
            s.cmd = VK_NULL_HANDLE;
            if (s.inFlight) vkDestroyFence(device, s.inFlight, nullptr);
            s.inFlight = VK_NULL_HANDLE;
        }
        if (acquireFence) vkDestroyFence(device, acquireFence, nullptr);
        acquireFence = VK_NULL_HANDLE;
        if (cmdPool) vkDestroyCommandPool(device, cmdPool, nullptr);
        cmdPool = VK_NULL_HANDLE;
        if (uploadPool) vkDestroyCommandPool(device, uploadPool, nullptr);
        uploadPool = VK_NULL_HANDLE;
    }

    void HandleDeviceLost(const char* reason) {
        if (deviceLost) return;
        deviceLost = true;
        LogMsg(LogLevel::Error, "device lost (%s) — rebuilding all GPU state", reason);
        vkDeviceWaitIdle(device); // 尽力而为；丢失态失败不致命

        DestroyAllGpuState();
        vmaDestroyAllocator(allocator);
        allocator = VK_NULL_HANDLE;
        vkDestroyDevice(device, nullptr);
        device = VK_NULL_HANDLE;

        PickPhysicalAndLogical();
        CreateCommandInfrastructure();
        CreateBindless();
        LoadPipelineCache();
        if (wantTimestamps) EnableTimestampsInternal();
        // 按注册顺序重建引擎侧资源（纹理重上传/几何与实例缓冲/管线经缓存重建）
        for (auto& [name, fn] : recreateCallbacks) {
            LEMON_LOG("recreate: %s", name);
            fn(*ownerDevice);
        }
        deviceLost = false;
    }

    bool wantTimestamps = false;
    Device* ownerDevice = nullptr;

    void EnableTimestampsInternal() {
        if (!info.timestampsSupported) return;
        VkQueryPoolCreateInfo ci{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
        ci.queryType = VK_QUERY_TYPE_TIMESTAMP;
        ci.queryCount = kFramesInFlight * 2;
        VK_CHECK(vkCreateQueryPool(device, &ci, nullptr, &tsPool));
        vkResetQueryPool(device, tsPool, 0, kFramesInFlight * 2); // 创建后必须先重置才能使用
        timestamps = true;
    }

    template <typename Res>
    uint32_t AllocId(std::vector<Res>& table, std::vector<uint32_t>& freeList) {
        if (!freeList.empty()) {
            uint32_t id = freeList.back();
            freeList.pop_back();
            return id;
        }
        table.emplace_back();
        return (uint32_t)table.size(); // 句柄 = 下标+1
    }
};

// ------------------------------------------------------------- Device API --
Device::Device() = default;
Device::~Device() {
    if (!m) return;
    if (m->device) {
        vkDeviceWaitIdle(m->device);
        SavePipelineCache();
        m->DestroyAllGpuState();
        vmaDestroyAllocator(m->allocator);
        vkDestroyDevice(m->device, nullptr);
    }
    if (m->surface) vkDestroySurfaceKHR(m->instance, m->surface, nullptr);
    if (m->messenger) {
        auto destroy = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
            m->instance, "vkDestroyDebugUtilsMessengerEXT");
        if (destroy) destroy(m->instance, m->messenger, nullptr);
    }
    if (m->instance) vkDestroyInstance(m->instance, nullptr);
}

std::unique_ptr<Device> Device::Create(const DeviceDesc& desc) {
    auto dev = std::unique_ptr<Device>(new Device());
    dev->m = std::make_unique<Impl>();
    dev->m->desc = desc;
    dev->m->ownerDevice = dev.get();
    dev->m->CreateInstance();
    return dev;
}

bool Device::CreateSwapchain(const SwapchainDesc& desc) {
    LEMON_ASSERT(desc.nativeWindow, "nativeWindow required");
    LEMON_ASSERT(!m->surface, "M1: single swapchain");
    m->window = (SDL_Window*)desc.nativeWindow;
    m->presentPref = desc.present;
    if (!SDL_Vulkan_CreateSurface(m->window, m->instance, nullptr, &m->surface)) {
        LEMON_ASSERT(false, "SDL_Vulkan_CreateSurface failed");
        return false;
    }
    m->PickPhysicalAndLogical();
    m->CreateCommandInfrastructure();
    m->CreateBindless();
    m->LoadPipelineCache();
    if (m->wantTimestamps) m->EnableTimestampsInternal();
    LEMON_LOG("device: %s (api %u.%u.%u, %s)", m->info.deviceName,
              VK_VERSION_MAJOR(m->info.apiVersion), VK_VERSION_MINOR(m->info.apiVersion),
              VK_VERSION_PATCH(m->info.apiVersion), m->info.discrete ? "discrete" : "integrated");
    return m->CreateSwapchainObjects();
}

bool Device::RecreateSwapchain() {
    vkDeviceWaitIdle(m->device);
    m->DestroySwapchainObjects();
    return m->CreateSwapchainObjects();
}

Format Device::SwapchainFormat() const {
    switch (m->swapFormat) {
        case VK_FORMAT_B8G8R8A8_SRGB: return Format::BGRA8UnormSrgb;
        case VK_FORMAT_R8G8B8A8_SRGB: return Format::RGBA8UnormSrgb;
        case VK_FORMAT_R8G8B8A8_UNORM: return Format::RGBA8Unorm;
        default: return Format::Undefined;
    }
}
uint32_t Device::SwapchainWidth() const { return m->swapExtent.width; }
uint32_t Device::SwapchainHeight() const { return m->swapExtent.height; }
const char* Device::PresentModeName() const {
    switch (m->presentMode) {
        case VK_PRESENT_MODE_FIFO_KHR: return "FIFO";
        case VK_PRESENT_MODE_MAILBOX_KHR: return "MAILBOX";
        case VK_PRESENT_MODE_IMMEDIATE_KHR: return "IMMEDIATE";
        default: return "OTHER";
    }
}

Buffer Device::CreateBuffer(const BufferDesc& desc) {
    LEMON_ASSERT(desc.size > 0, "buffer size must be > 0");
    uint32_t id = m->AllocId(m->buffers, m->bufferFree);
    auto& r = m->buffers[id - 1];
    VkBufferUsageFlags usage = 0;
    if (desc.usage & (uint32_t)BufferUsage::Vertex) usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if (desc.usage & (uint32_t)BufferUsage::Index) usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if (desc.usage & (uint32_t)BufferUsage::Storage) usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    if (desc.usage & (uint32_t)BufferUsage::TransferSrc) usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if (desc.usage & (uint32_t)BufferUsage::TransferDst) usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    VkBufferCreateInfo ci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    ci.size = desc.size;
    ci.usage = usage;
    if (desc.hostMapped) {
        VmaAllocationCreateInfo aci{};
        aci.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
        aci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                    VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VmaAllocationInfo info{};
        VK_CHECK(vmaCreateBuffer(m->allocator, &ci, &aci, &r.buf, &r.alloc, &info));
        r.mapped = info.pMappedData;
    } else {
        VmaAllocationCreateInfo aci{};
        aci.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        VK_CHECK(vmaCreateBuffer(m->allocator, &ci, &aci, &r.buf, &r.alloc, nullptr));
        r.mapped = nullptr;
    }
    r.desc = desc;
    return Buffer{id};
}

void* Device::MapBuffer(Buffer b) { return m->buffers[b.id - 1].mapped; }

void Device::DestroyBuffer(Buffer b) {
    if (!b.IsValid()) return;
    auto& r = m->buffers[b.id - 1];
    LEMON_ASSERT(r.buf, "double destroy");
    vmaDestroyBuffer(m->allocator, r.buf, r.alloc);
    r = {};
    m->bufferFree.push_back(b.id);
}

Texture Device::CreateTexture(const TextureDesc& desc) {
    LEMON_ASSERT(desc.mipLevels == 1 || desc.generateMips, "multi-mip requires generateMips");
    uint32_t id = m->AllocId(m->textures, m->textureFree);
    auto& r = m->textures[id - 1];
    r.realMipLevels =
        desc.generateMips
            ? (uint32_t)std::floor(std::log2((double)std::max(desc.width, desc.height))) + 1
            : 1;
    VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = ToVk(desc.format);
    ci.extent = {desc.width, desc.height, 1};
    ci.mipLevels = r.realMipLevels;
    ci.arrayLayers = 1;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
               VK_IMAGE_USAGE_TRANSFER_SRC_BIT; // SRC 供 mip blit
    VmaAllocationCreateInfo aci{};
    aci.usage = VMA_MEMORY_USAGE_AUTO;
    VK_CHECK(vmaCreateImage(m->allocator, &ci, &aci, &r.image, &r.alloc, nullptr));

    VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vci.image = r.image;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = ToVk(desc.format);
    vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, r.realMipLevels, 0, 1};
    VK_CHECK(vkCreateImageView(m->device, &vci, nullptr, &r.view));
    r.desc = desc;
    return Texture{id};
}

void Device::UploadTexture(Texture t, const void* rgba8Pixels, uint64_t byteSize) {
    auto& r = m->textures[t.id - 1];
    LEMON_ASSERT(r.image, "invalid texture");
    LEMON_ASSERT(byteSize >= (uint64_t)r.desc.width * r.desc.height * 4, "pixel data too small");

    VkBuffer staging = VK_NULL_HANDLE;
    VmaAllocation stagingAlloc = VK_NULL_HANDLE;
    {
        VkBufferCreateInfo ci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        ci.size = byteSize;
        ci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        VmaAllocationCreateInfo aci{};
        aci.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
        aci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                    VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VmaAllocationInfo info{};
        VK_CHECK(vmaCreateBuffer(m->allocator, &ci, &aci, &staging, &stagingAlloc, &info));
        std::memcpy(info.pMappedData, rgba8Pixels, (size_t)byteSize);
    }

    VkImage img = r.image;
    const uint32_t levels = r.realMipLevels;
    m->ImmediateSubmit([&](VkCommandBuffer cmd) {
        m->TransitionImage(cmd, img, 1, VK_IMAGE_LAYOUT_UNDEFINED,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
                           VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                           VK_PIPELINE_STAGE_TRANSFER_BIT);
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {r.desc.width, r.desc.height, 1};
        vkCmdCopyBufferToImage(cmd, staging, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

        if (levels > 1) {
            m->TransitionImage(cmd, img, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
                               VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                               VK_PIPELINE_STAGE_TRANSFER_BIT);
            for (uint32_t i = 1; i < levels; ++i) {
                m->TransitionImage(cmd, img, 1, VK_IMAGE_LAYOUT_UNDEFINED,
                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
                                   VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                                   VK_PIPELINE_STAGE_TRANSFER_BIT);
                int32_t srcW = std::max(1, (int32_t)r.desc.width >> (i - 1));
                int32_t srcH = std::max(1, (int32_t)r.desc.height >> (i - 1));
                int32_t dstW = std::max(1, (int32_t)r.desc.width >> i);
                int32_t dstH = std::max(1, (int32_t)r.desc.height >> i);
                VkImageBlit blit{};
                blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, i - 1, 0, 1};
                blit.srcOffsets[1] = {srcW, srcH, 1};
                blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, i, 0, 1};
                blit.dstOffsets[1] = {dstW, dstH, 1};
                vkCmdBlitImage(cmd, img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, img,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
                m->TransitionImage(cmd, img, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                   VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                   VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
            }
            m->TransitionImage(cmd, img, levels, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                               VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                               VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        } else {
            m->TransitionImage(cmd, img, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                               VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                               VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        }
    });
    vmaDestroyBuffer(m->allocator, staging, stagingAlloc);
}

void Device::DestroyTexture(Texture t) {
    if (!t.IsValid()) return;
    auto& r = m->textures[t.id - 1];
    LEMON_ASSERT(r.image, "double destroy");
    vkDestroyImageView(m->device, r.view, nullptr);
    vmaDestroyImage(m->allocator, r.image, r.alloc);
    r = {};
    m->textureFree.push_back(t.id);
}

Sampler Device::CreateSampler(const SamplerDesc& desc) {
    uint32_t id = m->AllocId(m->samplers, m->samplerFree);
    auto& r = m->samplers[id - 1];
    VkSamplerCreateInfo ci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    ci.magFilter = desc.mag == FilterMode::Point ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
    ci.minFilter = desc.min == FilterMode::Point ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
    ci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    ci.addressModeU = desc.u == AddressMode::Repeat       ? VK_SAMPLER_ADDRESS_MODE_REPEAT
                      : desc.u == AddressMode::MirrorRepeat
                          ? VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT
                          : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    ci.addressModeV = desc.v == AddressMode::Repeat       ? VK_SAMPLER_ADDRESS_MODE_REPEAT
                      : desc.v == AddressMode::MirrorRepeat
                          ? VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT
                          : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    ci.maxLod = 16.0f;
    VK_CHECK(vkCreateSampler(m->device, &ci, nullptr, &r.sampler));
    return Sampler{id};
}

Shader Device::CreateShader(ShaderStage stage, const uint32_t* spirv, size_t wordCount) {
    (void)stage;
    uint64_t hash = HashWords(spirv, wordCount);
    auto it = m->shaderByHash.find(hash);
    if (it != m->shaderByHash.end()) return Shader{it->second}; // 同 SPIR-V 复用模块

    uint32_t id = m->AllocId(m->shaders, m->shaderFree);
    auto& r = m->shaders[id - 1];
    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = wordCount * sizeof(uint32_t);
    ci.pCode = spirv;
    VK_CHECK(vkCreateShaderModule(m->device, &ci, nullptr, &r.module));
    r.hash = hash;
    m->shaderByHash[hash] = id;
    return Shader{id};
}

Pipeline Device::CreatePipeline(const PipelineDesc& desc) {
    LEMON_ASSERT(desc.vs.IsValid() && desc.fs.IsValid(), "pipeline needs both stages");
    uint32_t id = m->AllocId(m->pipelines, m->pipelineFree);
    auto& r = m->pipelines[id - 1];

    auto& vsMod = m->shaders[desc.vs.id - 1];
    auto& fsMod = m->shaders[desc.fs.id - 1];
    VkPipelineShaderStageCreateInfo stages[2]{
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
         VK_SHADER_STAGE_VERTEX_BIT, vsMod.module, "main", nullptr},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
         VK_SHADER_STAGE_FRAGMENT_BIT, fsMod.module, "main", nullptr}};

    // 路径 A 固定顶点输入：binding0 = 单位四边形角点 vec2（实例数据走 SSBO）
    VkVertexInputBindingDescription binding{0, sizeof(float) * 2, VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attr{0, 0, VK_FORMAT_R32G32_SFLOAT, 0};
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &binding;
    vi.vertexAttributeDescriptionCount = 1;
    vi.pVertexAttributeDescriptions = &attr;

    VkPipelineInputAssemblyStateCreateInfo ia{
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vsState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vsState.viewportCount = 1;
    vsState.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blend{};
    blend.blendEnable = desc.blend == BlendMode::Opaque ? VK_FALSE : VK_TRUE;
    blend.srcColorBlendFactor = BlendSrcColor(desc.blend);
    blend.dstColorBlendFactor = BlendDstColor(desc.blend);
    blend.colorBlendOp = VK_BLEND_OP_ADD;
    blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend.dstAlphaBlendFactor =
        desc.blend == BlendMode::Opaque ? VK_BLEND_FACTOR_ZERO
                                        : VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend.alphaBlendOp = VK_BLEND_OP_ADD;
    blend.colorWriteMask = 0xF;
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 1;
    cb.pAttachments = &blend;

    VkDynamicState dynStates[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates = dynStates;

    VkFormat colorFmt = ToVk(desc.colorFormat);
    VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &colorFmt;

    VkGraphicsPipelineCreateInfo ci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    ci.stageCount = 2;
    ci.pStages = stages;
    ci.pVertexInputState = &vi;
    ci.pInputAssemblyState = &ia;
    ci.pViewportState = &vsState;
    ci.pRasterizationState = &rs;
    ci.pMultisampleState = &ms;
    ci.pColorBlendState = &cb;
    ci.pDynamicState = &dyn;
    ci.layout = m->pipelineLayout;
    ci.pNext = &rendering; // 动态渲染：无 renderPass/framebuffer 对象
    VK_CHECK(vkCreateGraphicsPipelines(m->device, m->pipelineCache, 1, &ci, nullptr, &r.pipe));
    m->pipelineCacheDirty = true;
    r.desc = desc;
    return Pipeline{id};
}

void Device::BindTextureToSlot(Texture t, uint32_t slot) {
    LEMON_ASSERT(t.IsValid() && slot < kMaxTextureSlots, "bad texture slot bind");
    auto& r = m->textures[t.id - 1];
    VkDescriptorImageInfo info{};
    info.imageView = r.view;
    info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = m->bindlessSet;
    write.dstBinding = 0;
    write.dstArrayElement = slot;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    write.pImageInfo = &info;
    vkUpdateDescriptorSets(m->device, 1, &write, 0, nullptr);
}

void Device::BindSamplerToSlot(Sampler s, uint32_t slot) {
    LEMON_ASSERT(s.IsValid() && slot < kMaxSamplerSlots, "bad sampler slot bind");
    auto& r = m->samplers[s.id - 1];
    VkDescriptorImageInfo info{};
    info.sampler = r.sampler;
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = m->bindlessSet;
    write.dstBinding = 1;
    write.dstArrayElement = slot;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    write.pImageInfo = &info;
    vkUpdateDescriptorSets(m->device, 1, &write, 0, nullptr);
}

AcquireResult Device::AcquireNextImage() {
    AcquireResult out;
    if (!m->swapchain) { // 丢失重建后延后建链
        if (!m->CreateSwapchainObjects()) { out.needsRecreate = true; return out; }
    }
    VK_CHECK(vkResetFences(m->device, 1, &m->acquireFence));
    VkResult r = vkAcquireNextImageKHR(m->device, m->swapchain, UINT64_MAX, VK_NULL_HANDLE,
                                       m->acquireFence, &m->imageIndex);
    if (r == VK_ERROR_OUT_OF_DATE_KHR) { out.needsRecreate = true; return out; }
    if (r == VK_ERROR_DEVICE_LOST) {
        m->HandleDeviceLost("acquire");
        out.deviceLost = true;
        out.needsRecreate = true;
        return out;
    }
    VK_CHECK(r == VK_SUBOPTIMAL_KHR ? VK_SUCCESS : r);
    VK_CHECK(vkWaitForFences(m->device, 1, &m->acquireFence, VK_TRUE, UINT64_MAX));
    out.imageIndex = (int32_t)m->imageIndex;
    return out;
}

CommandList& Device::BeginFrame() {
    uint32_t fi = (uint32_t)(m->frameCounter % kFramesInFlight);
    auto& slot = m->slots[fi];

    VK_CHECK(vkWaitForFences(m->device, 1, &slot.inFlight, VK_TRUE, UINT64_MAX));
    vkResetFences(m->device, 1, &slot.inFlight);

    // 时间戳：读上一轮本槽位结果并重置（fence 已保证该轮完成）
    if (m->timestamps && m->frameCounter >= kFramesInFlight) {
        uint32_t base = fi * 2;
        uint64_t ts[2] = {0, 0};
        VkResult qr = vkGetQueryPoolResults(m->device, m->tsPool, base, 2, sizeof(ts), ts,
                                            sizeof(uint64_t), VK_QUERY_RESULT_64_BIT);
        if (qr == VK_SUCCESS && ts[1] >= ts[0]) {
            double period = (double)m->physProps.limits.timestampPeriod; // ns/tick
            m->lastTiming.gpuMs = (double)(ts[1] - ts[0]) * period / 1e6;
            m->lastTiming.valid = true;
        }
        vkResetQueryPool(m->device, m->tsPool, base, 2);
    }

    VK_CHECK(vkResetCommandBuffer(slot.cmd, 0));
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(slot.cmd, &bi));

    m->cmdListImpl.cmd = slot.cmd;
    m->cmdListImpl.dev = (void*)m.get();
    m->cmdListImpl.frameSlot = fi;
    m->cmdListImpl.imageIndex = m->imageIndex;
    m->cmdList.m = &m->cmdListImpl;
    return m->cmdList;
}

void Device::EndFrameAndPresent(bool& needsRecreateOut, bool& deviceLostOut) {
    needsRecreateOut = false;
    deviceLostOut = false;
    uint32_t fi = (uint32_t)(m->frameCounter % kFramesInFlight);
    auto& slot = m->slots[fi];
    VK_CHECK(vkEndCommandBuffer(slot.cmd));

    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &slot.cmd;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &m->presentSemaphores[m->imageIndex];
    VkResult submitted = vkQueueSubmit(m->queue, 1, &si, slot.inFlight);
    if (submitted == VK_ERROR_DEVICE_LOST) {
        m->HandleDeviceLost("submit");
        deviceLostOut = true;
        needsRecreateOut = true;
        ++m->frameCounter;
        return;
    }
    VK_CHECK(submitted);

    VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &m->presentSemaphores[m->imageIndex];
    pi.swapchainCount = 1;
    pi.pSwapchains = &m->swapchain;
    pi.pImageIndices = &m->imageIndex;
    VkResult presented = vkQueuePresentKHR(m->queue, &pi);
    if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR) {
        needsRecreateOut = true;
    } else if (presented == VK_ERROR_DEVICE_LOST) {
        m->HandleDeviceLost("present");
        deviceLostOut = true;
        needsRecreateOut = true;
    } else {
        VK_CHECK(presented);
    }
    ++m->frameCounter;
}

void Device::WaitIdle() { vkDeviceWaitIdle(m->device); }

void Device::EnableTimestamps() {
    if (m->timestamps) return;
    m->wantTimestamps = true;
    m->EnableTimestampsInternal();
}
FrameTiming Device::LastFrameTiming() const { return m->lastTiming; }

bool Device::IsDeviceLost() const { return m->deviceLost; }

void Device::SimulateDeviceLoss() { m->HandleDeviceLost("simulated (acceptance hook)"); }

void Device::AddRecreateCallback(const char* name, std::function<void(Device&)> fn) {
    m->recreateCallbacks.emplace_back(name, std::move(fn));
}

void Device::SavePipelineCache() {
    if (!m->desc.pipelineCachePath || !m->pipelineCache || !m->pipelineCacheDirty) return;
    size_t size = 0;
    VK_CHECK(vkGetPipelineCacheData(m->device, m->pipelineCache, &size, nullptr));
    if (size == 0) return;
    std::vector<char> data(size);
    VK_CHECK(vkGetPipelineCacheData(m->device, m->pipelineCache, &size, data.data()));
    std::error_code ec;
    std::filesystem::path p(m->desc.pipelineCachePath);
    if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary);
    f.write(data.data(), (std::streamsize)size);
    m->pipelineCacheDirty = false;
    LEMON_LOG("pipeline cache saved: %s (%zu bytes)", m->desc.pipelineCachePath, size);
}

const DeviceInfo& Device::Info() const { return m->info; }

// ----------------------------------------------------------- CommandList --
void CommandList::BeginPass(Format colorFormat, uint32_t w, uint32_t h, const float clearColor[4]) {
    (void)colorFormat;
    auto* d = (Device::Impl*)m->dev;
    VkImage image = d->swapImages[m->imageIndex];

    // PRESENT_SRC → COLOR_ATTACHMENT（动态渲染需手动屏障）
    {
        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = image;
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        // oldLayout=UNDEFINED 是获取屏障的规范写法：首次使用(实际 UNDEFINED)与
        // 后续(实际 PRESENT_SRC)都合法，内容本就要 clear
        b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        b.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        b.srcAccessMask = 0;
        b.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        vkCmdPipelineBarrier(m->cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0,
                             nullptr, 1, &b);
    }
    if (d->timestamps)
        vkCmdWriteTimestamp(m->cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, d->tsPool, m->frameSlot * 2);

    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageView = d->swapViews[m->imageIndex];
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.clearValue.color = {{clearColor[0], clearColor[1], clearColor[2], clearColor[3]}};
    VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
    ri.renderArea = {{0, 0}, {w, h}};
    ri.layerCount = 1;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments = &color;
    vkCmdBeginRendering(m->cmd, &ri);
}

void CommandList::EndPass() {
    vkCmdEndRendering(m->cmd);
    auto* d = (Device::Impl*)m->dev;
    if (d->timestamps)
        vkCmdWriteTimestamp(m->cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, d->tsPool,
                            m->frameSlot * 2 + 1);

    // COLOR_ATTACHMENT → PRESENT_SRC
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = d->swapImages[m->imageIndex];
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    b.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    b.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    b.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    b.dstAccessMask = 0;
    vkCmdPipelineBarrier(m->cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
}

void CommandList::BindPipeline(Pipeline p) {
    auto* d = (Device::Impl*)m->dev;
    vkCmdBindPipeline(m->cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, d->pipelines[p.id - 1].pipe);
}

void CommandList::BindQuadGeometry(Buffer cornerVB, Buffer indexIB) {
    auto* d = (Device::Impl*)m->dev;
    VkBuffer vb = d->buffers[cornerVB.id - 1].buf;
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(m->cmd, 0, 1, &vb, &offset);
    vkCmdBindIndexBuffer(m->cmd, d->buffers[indexIB.id - 1].buf, 0, VK_INDEX_TYPE_UINT16);
}

void CommandList::BindGlobalDescriptors() {
    auto* d = (Device::Impl*)m->dev;
    vkCmdBindDescriptorSets(m->cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, d->pipelineLayout, 0, 1,
                            &d->bindlessSet, 0, nullptr);
}

void CommandList::BindStorageBuffer(Buffer ssbo) {
    // 实例环形 SSBO 是单一大缓冲：仅在缓冲对象变化时重写描述符（resize/设备丢失后）。
    // 绘制期间该槽不被重写 → 无 UPDATE_AFTER_BIND 在用竞争。
    auto* d = (Device::Impl*)m->dev;
    if (d->boundStorageBufferId == ssbo.id) return;
    LEMON_ASSERT(ssbo.IsValid(), "invalid storage buffer");
    VkDescriptorBufferInfo info{};
    info.buffer = d->buffers[ssbo.id - 1].buf;
    info.offset = 0;
    info.range = VK_WHOLE_SIZE;
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = d->bindlessSet;
    write.dstBinding = 2;
    write.dstArrayElement = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.pBufferInfo = &info;
    vkUpdateDescriptorSets(d->device, 1, &write, 0, nullptr);
    d->boundStorageBufferId = ssbo.id;
}

void CommandList::SetViewportScissor(uint32_t w, uint32_t h) {
    VkViewport vp{0, 0, (float)w, (float)h, 0.0f, 1.0f};
    vkCmdSetViewport(m->cmd, 0, 1, &vp);
    VkRect2D scissor{{0, 0}, {w, h}};
    vkCmdSetScissor(m->cmd, 0, 1, &scissor);
}

void CommandList::PushConstants(const void* data, uint32_t size) {
    LEMON_ASSERT(size <= kMaxPushConstants, "push constant too large");
    vkCmdPushConstants(m->cmd, ((Device::Impl*)m->dev)->pipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, size, data);
}

void CommandList::DrawQuadInstances(uint32_t instanceCount, uint32_t baseInstance) {
    // baseInstance 由调用方经 push constant 传入着色器（SSBO 下标偏移）；
    // vkDrawIndexed 的 firstInstance 恒 0 —— 规避个别驱动 baseInstance 怪癖
    (void)baseInstance;
    vkCmdDrawIndexed(m->cmd, 6, instanceCount, 0, 0, 0);
}

} // namespace lemon::rhi
