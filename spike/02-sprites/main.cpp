// ---------------------------------------------------------------------------
// Lemon M0-W2 spike：实例化精灵压测
// 单次 draw call 画 N 个动画精灵（默认 100,000），per-instance 顶点属性
// （VK_VERTEX_INPUT_RATE_INSTANCE，M1 将按设计文档换 per-instance SSBO）。
// CPU 每帧全量更新实例数据（mapped HOST_COHERENT），统计更新耗时。
// 用法：lemon-spike-sprites [--n N] [--frames N] [--immediate] [--validate]
// Go/No-Go：本机 10 万动画精灵 ≥ 60fps（IMMEDIATE 模式）。
// 设计依据：docs/EngineDesign/02-Rendering-Vulkan.md §3.3 路径 A
// ---------------------------------------------------------------------------

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
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

extern const unsigned int lemon_spv_sprite_vert[];
extern const unsigned int lemon_spv_sprite_vert_count;
extern const unsigned int lemon_spv_sprite_frag[];
extern const unsigned int lemon_spv_sprite_frag_count;

#define VK_CHECK(x)                                                          \
    do {                                                                     \
        VkResult _r = (x);                                                   \
        if (_r != VK_SUCCESS) {                                              \
            std::fprintf(stderr, "[lemon] Vulkan error %d (%s:%d)\n",        \
                         (int)_r, __FILE__, __LINE__);                       \
            std::exit(2);                                                    \
        }                                                                    \
    } while (0)

namespace {

constexpr uint32_t kFramesInFlight = 2;
constexpr uint32_t kTexSize = 64;

struct AppArgs {
    uint32_t instances = 100000;
    int  frames = 0;
    bool immediate = false;
    bool validate = false;
    int  width = 1280, height = 720;
};

// 24B / instance；属性布局见 pipeline 顶点输入
struct Instance {
    float pos[2];
    float rot;
    float scale;
    uint32_t color;   // rgba8 pack
};

struct FrameSync {
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFence inFlight = VK_NULL_HANDLE;
};

const char* PresentModeName(VkPresentModeKHR m) {
    switch (m) {
        case VK_PRESENT_MODE_FIFO_KHR: return "FIFO";
        case VK_PRESENT_MODE_MAILBOX_KHR: return "MAILBOX";
        case VK_PRESENT_MODE_IMMEDIATE_KHR: return "IMMEDIATE";
        default: return "OTHER";
    }
}

uint32_t PackRGBA(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16) | ((uint32_t)a << 24);
}

} // namespace

int main(int argc, char** argv) {
    AppArgs args;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--n") && i + 1 < argc) args.instances = (uint32_t)std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--frames") && i + 1 < argc) args.frames = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--immediate")) args.immediate = true;
        else if (!std::strcmp(argv[i], "--validate")) args.validate = true;
        else if (!std::strcmp(argv[i], "--w") && i + 1 < argc) args.width = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--h") && i + 1 < argc) args.height = std::atoi(argv[++i]);
    }
    std::printf("[lemon] instances=%u\n", args.instances);

    if (!SDL_Init(SDL_INIT_VIDEO)) return 1;
    SDL_Window* window = SDL_CreateWindow("Lemon spike 02 - sprites", args.width, args.height,
                                          SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    if (!window) return 1;

    // ---- instance / surface（与 spike-01 相同的引导路径） ---------------------
    uint32_t instApi = VK_API_VERSION_1_0;
    vkEnumerateInstanceVersion(&instApi);
    instApi = std::min(instApi, (uint32_t)VK_API_VERSION_1_3);

    uint32_t sdlExtCount = 0;
    const char* const* sdlExts = SDL_Vulkan_GetInstanceExtensions(&sdlExtCount);
    if (!sdlExts) return 1;
    std::vector<const char*> extensions(sdlExts, sdlExts + sdlExtCount);

    bool portabilityEnum = false;
    {
        uint32_t n = 0;
        vkEnumerateInstanceExtensionProperties(nullptr, &n, nullptr);
        std::vector<VkExtensionProperties> props(n);
        vkEnumerateInstanceExtensionProperties(nullptr, &n, props.data());
        for (auto& p : props)
            if (!std::strcmp(p.extensionName, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
                extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
                portabilityEnum = true;
            }
    }

    std::vector<const char*> layers;
    if (args.validate) {
        uint32_t n = 0;
        vkEnumerateInstanceLayerProperties(&n, nullptr);
        std::vector<VkLayerProperties> props(n);
        vkEnumerateInstanceLayerProperties(&n, props.data());
        for (auto& p : props)
            if (!std::strcmp(p.layerName, "VK_LAYER_KHRONOS_validation")) {
                layers.push_back("VK_LAYER_KHRONOS_validation");
                std::printf("[lemon] validation layer enabled\n");
            }
    }

    VkInstance instance = VK_NULL_HANDLE;
    {
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.pApplicationName = "lemon-spike-sprites";
        app.pEngineName = "Lemon";
        app.apiVersion = instApi;
        VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        ci.pApplicationInfo = &app;
        ci.enabledExtensionCount = (uint32_t)extensions.size();
        ci.ppEnabledExtensionNames = extensions.data();
        ci.enabledLayerCount = (uint32_t)layers.size();
        ci.ppEnabledLayerNames = layers.data();
        if (portabilityEnum) ci.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
        VK_CHECK(vkCreateInstance(&ci, nullptr, &instance));
    }

    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (!SDL_Vulkan_CreateSurface(window, instance, nullptr, &surface)) return 1;

    // ---- physical / logical device ------------------------------------------
    uint32_t devCount = 0;
    vkEnumeratePhysicalDevices(instance, &devCount, nullptr);
    std::vector<VkPhysicalDevice> devices(devCount);
    vkEnumeratePhysicalDevices(instance, &devCount, devices.data());

    VkPhysicalDevice physical = VK_NULL_HANDLE;
    uint32_t graphicsFamily = UINT32_MAX;
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
        bool hasSwap = false, hasPort = false;
        for (auto& e : exts) {
            if (!std::strcmp(e.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) hasSwap = true;
            if (!std::strcmp(e.extensionName, "VK_KHR_portability_subset")) hasPort = true;
        }
        if (!hasSwap) continue;
        bool candDisc = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
        bool curDisc = false;
        if (physical) {
            VkPhysicalDeviceProperties p2;
            vkGetPhysicalDeviceProperties(physical, &p2);
            curDisc = p2.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
        }
        if (!physical || (candDisc && !curDisc)) {
            physical = dev;
            graphicsFamily = family;
            deviceExts = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
            if (hasPort) deviceExts.push_back("VK_KHR_portability_subset");
        }
    }
    if (!physical) return 1;

    VkPhysicalDeviceProperties devProps;
    vkGetPhysicalDeviceProperties(physical, &devProps);
    std::printf("[lemon] device: %s (api %u.%u.%u)\n", devProps.deviceName,
                VK_VERSION_MAJOR(devProps.apiVersion), VK_VERSION_MINOR(devProps.apiVersion),
                VK_VERSION_PATCH(devProps.apiVersion));

    float prio = 1.0f;
    VkDeviceQueueCreateInfo queueCI{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queueCI.queueFamilyIndex = graphicsFamily;
    queueCI.queueCount = 1;
    queueCI.pQueuePriorities = &prio;
    VkDeviceCreateInfo devCI{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    devCI.queueCreateInfoCount = 1;
    devCI.pQueueCreateInfos = &queueCI;
    devCI.enabledExtensionCount = (uint32_t)deviceExts.size();
    devCI.ppEnabledExtensionNames = deviceExts.data();
    VkDevice device = VK_NULL_HANDLE;
    VK_CHECK(vkCreateDevice(physical, &devCI, nullptr, &device));
    VkQueue queue = VK_NULL_HANDLE;
    vkGetDeviceQueue(device, graphicsFamily, 0, &queue);

    VmaAllocator allocator = VK_NULL_HANDLE;
    {
        VmaAllocatorCreateInfo ci{};
        ci.physicalDevice = physical;
        ci.device = device;
        ci.instance = instance;
        ci.vulkanApiVersion = instApi;
        VK_CHECK(vmaCreateAllocator(&ci, &allocator));
    }

    // ---- 几何：单位四边形 + 索引 --------------------------------------------
    auto MakeHostBuffer = [&](VkBufferUsageFlags usage, VkDeviceSize size, VkBuffer* outBuf,
                              VmaAllocation* outAlloc, void** outMapped) {
        VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bci.size = size;
        bci.usage = usage;
        VmaAllocationCreateInfo aci{};
        aci.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
        aci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                    VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VmaAllocationInfo info{};
        VK_CHECK(vmaCreateBuffer(allocator, &bci, &aci, outBuf, outAlloc, &info));
        *outMapped = info.pMappedData;
    };

    VkBuffer quadBuf = VK_NULL_HANDLE, indexBuf = VK_NULL_HANDLE, instanceBuf = VK_NULL_HANDLE;
    VmaAllocation quadAlloc, indexAlloc, instanceAlloc;
    void *quadData = nullptr, *indexData = nullptr, *instanceData = nullptr;
    {
        const float corners[4][2] = {{-0.5f, -0.5f}, {0.5f, -0.5f}, {0.5f, 0.5f}, {-0.5f, 0.5f}};
        const uint16_t indices[6] = {0, 1, 2, 0, 2, 3};
        MakeHostBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, sizeof(corners), &quadBuf, &quadAlloc,
                       &quadData);
        std::memcpy(quadData, corners, sizeof(corners));
        MakeHostBuffer(VK_BUFFER_USAGE_INDEX_BUFFER_BIT, sizeof(indices), &indexBuf, &indexAlloc,
                       &indexData);
        std::memcpy(indexData, indices, sizeof(indices));
        MakeHostBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, sizeof(Instance) * args.instances,
                       &instanceBuf, &instanceAlloc, &instanceData);
    }

    // ---- 实例运动参数（固定种子 → 可复现） -----------------------------------
    std::vector<float> baseAngle(args.instances), orbitR(args.instances), angSpeed(args.instances),
        scaleArr(args.instances);
    std::vector<uint32_t> colorArr(args.instances);
    {
        uint32_t seed = 0x1e0f;  // "lemon"
        auto rnd = [&]() {
            seed = seed * 1664525u + 1013904223u;
            return (seed >> 8) / 16777216.0f;
        };
        const uint32_t palette[] = {
            PackRGBA(255, 214, 10, 255), PackRGBA(255, 160, 20, 255), PackRGBA(190, 215, 40, 255),
            PackRGBA(255, 240, 160, 255), PackRGBA(120, 150, 30, 200),
        };
        for (uint32_t i = 0; i < args.instances; ++i) {
            baseAngle[i] = rnd() * 6.2831853f;
            orbitR[i] = 8.0f + rnd() * 560.0f;
            angSpeed[i] = (0.2f + rnd() * 1.8f) * (rnd() > 0.5f ? 1.0f : -1.0f);
            scaleArr[i] = 4.0f + rnd() * 10.0f;
            colorArr[i] = palette[i % 5];
        }
    }

    // ---- 程序化纹理：柠檬圆点（64x64） --------------------------------------
    VkImage texImage = VK_NULL_HANDLE;
    VmaAllocation texAlloc;
    VkImageView texView = VK_NULL_HANDLE;
    VkSampler texSampler = VK_NULL_HANDLE;
    {
        std::vector<uint8_t> pixels(kTexSize * kTexSize * 4);
        for (uint32_t y = 0; y < kTexSize; ++y)
            for (uint32_t x = 0; x < kTexSize; ++x) {
                float dx = ((float)x + 0.5f) / kTexSize - 0.5f;
                float dy = ((float)y + 0.5f) / kTexSize - 0.5f;
                float d = std::sqrt(dx * dx + dy * dy);
                uint8_t* p = &pixels[(y * kTexSize + x) * 4];
                if (d < 0.46f) {
                    float lit = std::max(0.0f, 1.0f - d / 0.46f);
                    p[0] = (uint8_t)(250 - 40 * (1.0f - lit));
                    p[1] = (uint8_t)(225 - 80 * (1.0f - lit));
                    p[2] = (uint8_t)(30 + 60 * (1.0f - lit));
                    p[3] = 255;
                    if (d > 0.44f) p[3] = (uint8_t)(255 * (0.46f - d) / 0.02f);
                } else {
                    p[0] = p[1] = p[2] = 0;
                    p[3] = 0;
                }
            }

        // staging -> optimal image（一次性命令缓冲）
        VkBuffer staging = VK_NULL_HANDLE;
        VmaAllocation stagingAlloc;
        void* stagingPtr = nullptr;
        MakeHostBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, pixels.size(), &staging, &stagingAlloc,
                       &stagingPtr);
        std::memcpy(stagingPtr, pixels.data(), pixels.size());

        VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ici.imageType = VK_IMAGE_TYPE_2D;
        ici.format = VK_FORMAT_R8G8B8A8_UNORM;
        ici.extent = {kTexSize, kTexSize, 1};
        ici.mipLevels = 1;
        ici.arrayLayers = 1;
        ici.samples = VK_SAMPLE_COUNT_1_BIT;
        ici.tiling = VK_IMAGE_TILING_OPTIMAL;
        ici.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        VmaAllocationCreateInfo iaci{};
        iaci.usage = VMA_MEMORY_USAGE_AUTO;
        VK_CHECK(vmaCreateImage(allocator, &ici, &iaci, &texImage, &texAlloc, nullptr));

        VkCommandPool pool = VK_NULL_HANDLE;
        VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pci.queueFamilyIndex = graphicsFamily;
        VK_CHECK(vkCreateCommandPool(device, &pci, nullptr, &pool));
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        cai.commandPool = pool;
        cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cai.commandBufferCount = 1;
        VK_CHECK(vkAllocateCommandBuffers(device, &cai, &cmd));
        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VK_CHECK(vkBeginCommandBuffer(cmd, &bi));

        auto Barrier = [&](VkImageLayout oldL, VkImageLayout newL) {
            VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.image = texImage;
            b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            b.oldLayout = oldL;
            b.newLayout = newL;
            b.srcAccessMask = oldL == VK_IMAGE_LAYOUT_UNDEFINED ? 0 : VK_ACCESS_TRANSFER_WRITE_BIT;
            b.dstAccessMask = newL == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
                                  ? VK_ACCESS_TRANSFER_WRITE_BIT
                                  : VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 newL == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
                                     ? VK_PIPELINE_STAGE_TRANSFER_BIT
                                     : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &b);
        };
        Barrier(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {kTexSize, kTexSize, 1};
        vkCmdCopyBufferToImage(cmd, staging, texImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        Barrier(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        VK_CHECK(vkEndCommandBuffer(cmd));
        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cmd;
        VK_CHECK(vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE));
        VK_CHECK(vkQueueWaitIdle(queue));
        vkDestroyCommandPool(device, pool, nullptr);
        vmaDestroyBuffer(allocator, staging, stagingAlloc);

        VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vci.image = texImage;
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format = VK_FORMAT_R8G8B8A8_UNORM;
        vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VK_CHECK(vkCreateImageView(device, &vci, nullptr, &texView));

        VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        sci.magFilter = sci.minFilter = VK_FILTER_LINEAR;
        sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sci.addressModeU = sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sci.maxLod = 1.0f;
        VK_CHECK(vkCreateSampler(device, &sci, nullptr, &texSampler));
    }

    // ---- 描述符 ---------------------------------------------------------------
    VkDescriptorPool descPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout descLayout = VK_NULL_HANDLE;
    VkDescriptorSet descSet = VK_NULL_HANDLE;
    {
        VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
        VkDescriptorPoolCreateInfo pci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pci.maxSets = 1;
        pci.poolSizeCount = 1;
        pci.pPoolSizes = &poolSize;
        VK_CHECK(vkCreateDescriptorPool(device, &pci, nullptr, &descPool));

        VkDescriptorSetLayoutBinding binding{};
        binding.binding = 0;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo lci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        lci.bindingCount = 1;
        lci.pBindings = &binding;
        VK_CHECK(vkCreateDescriptorSetLayout(device, &lci, nullptr, &descLayout));

        VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        ai.descriptorPool = descPool;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts = &descLayout;
        VK_CHECK(vkAllocateDescriptorSets(device, &ai, &descSet));

        VkDescriptorImageInfo imageInfo{};
        imageInfo.sampler = texSampler;
        imageInfo.imageView = texView;
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = descSet;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &imageInfo;
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    }

    // ---- 着色器 / 命令 / 同步 -------------------------------------------------
    auto MakeModule = [&](const unsigned int* code, unsigned int count) {
        VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        ci.codeSize = (size_t)count * sizeof(unsigned int);
        ci.pCode = code;
        VkShaderModule m = VK_NULL_HANDLE;
        VK_CHECK(vkCreateShaderModule(device, &ci, nullptr, &m));
        return m;
    };
    VkShaderModule vertModule = MakeModule(lemon_spv_sprite_vert, lemon_spv_sprite_vert_count);
    VkShaderModule fragModule = MakeModule(lemon_spv_sprite_frag, lemon_spv_sprite_frag_count);

    VkCommandPool cmdPool = VK_NULL_HANDLE;
    {
        VkCommandPoolCreateInfo ci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        ci.queueFamilyIndex = graphicsFamily;
        VK_CHECK(vkCreateCommandPool(device, &ci, nullptr, &cmdPool));
    }

    FrameSync frames[kFramesInFlight];
    for (auto& f : frames) {
        VkCommandBufferAllocateInfo ci{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        ci.commandPool = cmdPool;
        ci.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ci.commandBufferCount = 1;
        VK_CHECK(vkAllocateCommandBuffers(device, &ci, &f.cmd));
        VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        VK_CHECK(vkCreateFence(device, &fci, nullptr, &f.inFlight));
    }

    std::vector<VkSemaphore> renderFinished;   // per swapchain image
    VkFence acquireFence = VK_NULL_HANDLE;     // fence-only acquire
    {
        VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        VK_CHECK(vkCreateFence(device, &fci, nullptr, &acquireFence));
    }

    // ---- pipeline layout（push constant: vec2 resolution） --------------------
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    {
        VkPushConstantRange pcRange{VK_SHADER_STAGE_VERTEX_BIT, 0, 8};
        VkPipelineLayoutCreateInfo ci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        ci.setLayoutCount = 1;
        ci.pSetLayouts = &descLayout;
        ci.pushConstantRangeCount = 1;
        ci.pPushConstantRanges = &pcRange;
        VK_CHECK(vkCreatePipelineLayout(device, &ci, nullptr, &pipelineLayout));
    }

    // ---- 交换链对象（可重建） --------------------------------------------------
    struct SwapchainObjects {
        VkSwapchainKHR swapchain = VK_NULL_HANDLE;
        VkFormat format = VK_FORMAT_UNDEFINED;
        VkExtent2D extent{};
        VkRenderPass renderPass = VK_NULL_HANDLE;
        VkPipeline pipeline = VK_NULL_HANDLE;
        std::vector<VkImage> images;
        std::vector<VkImageView> views;
        std::vector<VkFramebuffer> framebuffers;
    } sc;

    auto DestroySwapchainObjects = [&]() {
        for (auto fb : sc.framebuffers) vkDestroyFramebuffer(device, fb, nullptr);
        for (auto v : sc.views) vkDestroyImageView(device, v, nullptr);
        for (auto s : renderFinished) vkDestroySemaphore(device, s, nullptr);
        renderFinished.clear();
        if (sc.pipeline) vkDestroyPipeline(device, sc.pipeline, nullptr);
        if (sc.renderPass) vkDestroyRenderPass(device, sc.renderPass, nullptr);
        if (sc.swapchain) vkDestroySwapchainKHR(device, sc.swapchain, nullptr);
        sc = {};
    };

    bool needRecreate = true;
    VkPresentModeKHR chosenPresent = VK_PRESENT_MODE_FIFO_KHR;

    auto CreateSwapchainObjects = [&]() {
        VkSurfaceCapabilitiesKHR caps;
        VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &caps));
        int w = 0, h = 0;
        SDL_GetWindowSizeInPixels(window, &w, &h);
        VkExtent2D extent{};
        if (caps.currentExtent.width == UINT32_MAX) {
            extent.width = std::clamp((uint32_t)w, caps.minImageExtent.width, caps.maxImageExtent.width);
            extent.height = std::clamp((uint32_t)h, caps.minImageExtent.height, caps.maxImageExtent.height);
        } else {
            extent = caps.currentExtent;
        }
        if (extent.width == 0 || extent.height == 0) return false;

        VkSurfaceFormatKHR format = {};
        {
            uint32_t n = 0;
            VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &n, nullptr));
            std::vector<VkSurfaceFormatKHR> fmts(n);
            VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &n, fmts.data()));
            format = fmts[0];
            for (auto& f : fmts)
                if (f.format == VK_FORMAT_B8G8R8A8_SRGB &&
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
            if (args.immediate) {
                chosenPresent = has(VK_PRESENT_MODE_IMMEDIATE_KHR) ? VK_PRESENT_MODE_IMMEDIATE_KHR
                                   : (has(VK_PRESENT_MODE_MAILBOX_KHR) ? VK_PRESENT_MODE_MAILBOX_KHR
                                                                        : VK_PRESENT_MODE_FIFO_KHR);
            } else {
                chosenPresent =
                    has(VK_PRESENT_MODE_MAILBOX_KHR) ? VK_PRESENT_MODE_MAILBOX_KHR : VK_PRESENT_MODE_FIFO_KHR;
            }
        }

        uint32_t imageCount = std::clamp<uint32_t>(3, caps.minImageCount,
                                                   caps.maxImageCount ? caps.maxImageCount : 8);
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
        ci.presentMode = chosenPresent;
        ci.clipped = VK_TRUE;
        VK_CHECK(vkCreateSwapchainKHR(device, &ci, nullptr, &sc.swapchain));

        uint32_t n = 0;
        VK_CHECK(vkGetSwapchainImagesKHR(device, sc.swapchain, &n, nullptr));
        sc.images.resize(n);
        VK_CHECK(vkGetSwapchainImagesKHR(device, sc.swapchain, &n, sc.images.data()));
        sc.format = format.format;
        sc.extent = extent;

        sc.views.resize(n);
        for (uint32_t i = 0; i < n; ++i) {
            VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            vci.image = sc.images[i];
            vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
            vci.format = sc.format;
            vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            VK_CHECK(vkCreateImageView(device, &vci, nullptr, &sc.views[i]));
        }

        renderFinished.resize(n);
        for (uint32_t i = 0; i < n; ++i) {
            VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
            VK_CHECK(vkCreateSemaphore(device, &sci, nullptr, &renderFinished[i]));
        }

        VkAttachmentDescription color{};
        color.format = sc.format;
        color.samples = VK_SAMPLE_COUNT_1_BIT;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorRef;
        VkRenderPassCreateInfo rpci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        rpci.attachmentCount = 1;
        rpci.pAttachments = &color;
        rpci.subpassCount = 1;
        rpci.pSubpasses = &subpass;
        VK_CHECK(vkCreateRenderPass(device, &rpci, nullptr, &sc.renderPass));

        VkPipelineShaderStageCreateInfo stages[2]{
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
             VK_SHADER_STAGE_VERTEX_BIT, vertModule, "main", nullptr},
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
             VK_SHADER_STAGE_FRAGMENT_BIT, fragModule, "main", nullptr}};

        // binding0: 四边形角点（per-vertex）；binding1: 实例数据（per-instance）
        VkVertexInputBindingDescription bindings[2]{
            {0, sizeof(float) * 2, VK_VERTEX_INPUT_RATE_VERTEX},
            {1, sizeof(Instance), VK_VERTEX_INPUT_RATE_INSTANCE}};
        VkVertexInputAttributeDescription attrs[5]{
            {0, 0, VK_FORMAT_R32G32_SFLOAT, 0},                  // aCorner
            {1, 1, VK_FORMAT_R32G32_SFLOAT, offsetof(Instance, pos)},
            {2, 1, VK_FORMAT_R32_SFLOAT, offsetof(Instance, rot)},
            {3, 1, VK_FORMAT_R32_SFLOAT, offsetof(Instance, scale)},
            {4, 1, VK_FORMAT_R8G8B8A8_UINT, offsetof(Instance, color)}};  // shader 侧 in uint + unpackUnorm4x8
        VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        vi.vertexBindingDescriptionCount = 2;
        vi.pVertexBindingDescriptions = bindings;
        vi.vertexAttributeDescriptionCount = 5;
        vi.pVertexAttributeDescriptions = attrs;

        VkPipelineInputAssemblyStateCreateInfo ia{
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo vs{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        vs.viewportCount = 1;
        vs.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        rs.polygonMode = VK_POLYGON_MODE_FILL;
        rs.cullMode = VK_CULL_MODE_NONE;
        rs.lineWidth = 1.0f;
        VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState blendAtt{};
        blendAtt.blendEnable = VK_TRUE;                       // alpha 混合（真实负载）
        blendAtt.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        blendAtt.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blendAtt.colorBlendOp = VK_BLEND_OP_ADD;
        blendAtt.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blendAtt.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blendAtt.alphaBlendOp = VK_BLEND_OP_ADD;
        blendAtt.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                  VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        cb.attachmentCount = 1;
        cb.pAttachments = &blendAtt;

        VkDynamicState dynStates[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dyn{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dyn.dynamicStateCount = 2;
        dyn.pDynamicStates = dynStates;

        VkGraphicsPipelineCreateInfo pci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        pci.stageCount = 2;
        pci.pStages = stages;
        pci.pVertexInputState = &vi;
        pci.pInputAssemblyState = &ia;
        pci.pViewportState = &vs;
        pci.pRasterizationState = &rs;
        pci.pMultisampleState = &ms;
        pci.pColorBlendState = &cb;
        pci.pDynamicState = &dyn;
        pci.layout = pipelineLayout;
        pci.renderPass = sc.renderPass;
        pci.subpass = 0;
        VkPipeline pipe = VK_NULL_HANDLE;
        VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pci, nullptr, &pipe));
        sc.pipeline = pipe;

        sc.framebuffers.resize(n);
        for (uint32_t i = 0; i < n; ++i) {
            VkFramebufferCreateInfo fci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
            fci.renderPass = sc.renderPass;
            fci.attachmentCount = 1;
            fci.pAttachments = &sc.views[i];
            fci.width = sc.extent.width;
            fci.height = sc.extent.height;
            fci.layers = 1;
            VK_CHECK(vkCreateFramebuffer(device, &fci, nullptr, &sc.framebuffers[i]));
        }
        return true;
    };

    // ---- 主循环 -----------------------------------------------------------------
    const Uint64 perfFreq = SDL_GetPerformanceFrequency();
    auto NowMs = [&]() { return (double)SDL_GetPerformanceCounter() * 1000.0 / (double)perfFreq; };

    Instance* inst = (Instance*)instanceData;
    double t = 0.0;
    Uint64 frameNo = 0;
    double frameMin = 1e9, frameMax = 0, frameSum = 0;
    double updMin = 1e9, updMax = 0, updSum = 0;
    double prevFrameMs = NowMs();
    bool running = true;

    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_EVENT_QUIT ||
                (ev.type == SDL_EVENT_KEY_DOWN && ev.key.key == SDLK_ESCAPE))
                running = false;
            if (ev.type == SDL_EVENT_WINDOW_RESIZED ||
                ev.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
                needRecreate = true;
        }
        if (!running) break;

        if (needRecreate) {
            vkDeviceWaitIdle(device);
            DestroySwapchainObjects();
            needRecreate = false;
            if (!CreateSwapchainObjects()) continue;
        }

        // ---- CPU 更新实例数据（全量；测的就是这个 + 渲染的组合） --------------
        double t0 = NowMs();
        const float cx = sc.extent.width * 0.5f, cy = sc.extent.height * 0.5f;
        for (uint32_t i = 0; i < args.instances; ++i) {
            float a = baseAngle[i] + (float)t * angSpeed[i];
            inst[i].pos[0] = cx + std::cos(a) * orbitR[i];
            inst[i].pos[1] = cy + std::sin(a) * orbitR[i] * 0.62f;
            inst[i].rot = a * 2.0f;
            inst[i].scale = scaleArr[i];
            inst[i].color = colorArr[i];
        }
        double updateMs = NowMs() - t0;
        t += 1.0 / 60.0;

        const uint32_t fi = frameNo % kFramesInFlight;
        FrameSync& fs = frames[fi];
        VK_CHECK(vkWaitForFences(device, 1, &fs.inFlight, VK_TRUE, UINT64_MAX));
        vkResetFences(device, 1, &fs.inFlight);

        uint32_t imageIndex = 0;
        VK_CHECK(vkResetFences(device, 1, &acquireFence));
        VkResult acquired = vkAcquireNextImageKHR(device, sc.swapchain, UINT64_MAX, VK_NULL_HANDLE,
                                                  acquireFence, &imageIndex);
        if (acquired == VK_ERROR_OUT_OF_DATE_KHR) { needRecreate = true; continue; }
        VK_CHECK(acquired == VK_SUBOPTIMAL_KHR ? VK_SUCCESS : acquired);
        VK_CHECK(vkWaitForFences(device, 1, &acquireFence, VK_TRUE, UINT64_MAX));

        VK_CHECK(vkResetCommandBuffer(fs.cmd, 0));
        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VK_CHECK(vkBeginCommandBuffer(fs.cmd, &bi));

        VkClearValue clear{{{0.06f, 0.07f, 0.1f, 1.0f}}};
        VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        rp.renderPass = sc.renderPass;
        rp.framebuffer = sc.framebuffers[imageIndex];
        rp.renderArea.extent = sc.extent;
        rp.clearValueCount = 1;
        rp.pClearValues = &clear;
        vkCmdBeginRenderPass(fs.cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(fs.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, sc.pipeline);

        VkViewport vp{0, 0, (float)sc.extent.width, (float)sc.extent.height, 0.0f, 1.0f};
        VkRect2D scissor{{0, 0}, sc.extent};
        vkCmdSetViewport(fs.cmd, 0, 1, &vp);
        vkCmdSetScissor(fs.cmd, 0, 1, &scissor);

        float resolution[2] = {(float)sc.extent.width, (float)sc.extent.height};
        vkCmdPushConstants(fs.cmd, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, 8, resolution);

        VkDeviceSize offset = 0;
        (void)offset;
        VkBuffer vertexBuffers[2] = {quadBuf, instanceBuf};
        VkDeviceSize offsets[2] = {0, 0};
        vkCmdBindVertexBuffers2(fs.cmd, 0, 2, vertexBuffers, offsets, nullptr, nullptr);
        vkCmdBindIndexBuffer(fs.cmd, indexBuf, 0, VK_INDEX_TYPE_UINT16);
        vkCmdBindDescriptorSets(fs.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1,
                                &descSet, 0, nullptr);
        vkCmdDrawIndexed(fs.cmd, 6, args.instances, 0, 0, 0);   // 一次 draw call

        vkCmdEndRenderPass(fs.cmd);
        VK_CHECK(vkEndCommandBuffer(fs.cmd));

        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        si.commandBufferCount = 1;
        si.pCommandBuffers = &fs.cmd;
        si.signalSemaphoreCount = 1;
        si.pSignalSemaphores = &renderFinished[imageIndex];
        VK_CHECK(vkQueueSubmit(queue, 1, &si, fs.inFlight));

        VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        pi.waitSemaphoreCount = 1;
        pi.pWaitSemaphores = &renderFinished[imageIndex];
        pi.swapchainCount = 1;
        pi.pSwapchains = &sc.swapchain;
        pi.pImageIndices = &imageIndex;
        VkResult presented = vkQueuePresentKHR(queue, &pi);
        if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR) needRecreate = true;
        else VK_CHECK(presented);

        // 统计：帧时间（present 间隔）与 CPU 更新耗时（跳过首帧管线编译尖峰）
        double nowMs = NowMs();
        if (frameNo > 0) {
            double frameMs = nowMs - prevFrameMs;
            frameMin = std::min(frameMin, frameMs);
            frameMax = std::max(frameMax, frameMs);
            frameSum += frameMs;
            updMin = std::min(updMin, updateMs);
            updMax = std::max(updMax, updateMs);
            updSum += updateMs;
        }
        prevFrameMs = nowMs;
        ++frameNo;

        if (args.frames > 0 && (int)frameNo >= args.frames) running = false;
    }

    vkDeviceWaitIdle(device);

    if (frameNo > 1) {
        const double n = (double)frameNo - 1;
        std::printf(
            "[lemon] instances=%u frames=%llu frame avg=%.2fms (%.1f fps) min=%.2fms max=%.2fms | "
            "cpuUpdate avg=%.3fms max=%.3fms | present=%s drawCalls=1\n",
            args.instances, (unsigned long long)frameNo, frameSum / n, 1000.0 * n / frameSum,
            frameMin, frameMax, updSum / n, updMax, PresentModeName(chosenPresent));
    }

    // ---- 清理 ---------------------------------------------------------------------
    DestroySwapchainObjects();
    vkDestroyFence(device, acquireFence, nullptr);
    for (auto& f : frames) vkDestroyFence(device, f.inFlight, nullptr);
    vkDestroyCommandPool(device, cmdPool, nullptr);
    vkDestroyShaderModule(device, vertModule, nullptr);
    vkDestroyShaderModule(device, fragModule, nullptr);
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    vkDestroyDescriptorPool(device, descPool, nullptr);
    vkDestroyDescriptorSetLayout(device, descLayout, nullptr);
    vkDestroySampler(device, texSampler, nullptr);
    vkDestroyImageView(device, texView, nullptr);
    vmaDestroyImage(allocator, texImage, texAlloc);
    vmaDestroyBuffer(allocator, quadBuf, quadAlloc);
    vmaDestroyBuffer(allocator, indexBuf, indexAlloc);
    vmaDestroyBuffer(allocator, instanceBuf, instanceAlloc);
    vmaDestroyAllocator(allocator);
    vkDestroyDevice(device, nullptr);
    vkDestroySurfaceKHR(instance, surface, nullptr);
    vkDestroyInstance(instance, nullptr);
    SDL_DestroyWindow(window);
    SDL_Quit();
    std::printf("[lemon] spike-02 exit OK\n");
    return 0;
}
