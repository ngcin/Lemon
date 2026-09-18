// ---------------------------------------------------------------------------
// Lemon M0-W1 spike：SDL3 + Vulkan + VMA + 交换链 + 三角形
// 目的：验证本机（macOS + MoltenVK / Windows 原生）完整 Vulkan 帧循环跑通。
// 用法：lemon-spike-triangle [--frames N] [--immediate] [--w W] [--h H]
//   --frames N   跑 N 帧后自动退出并打印统计（冒烟/压测用）
//   --immediate  优先 IMMEDIATE 呈现模式（不锁垂直同步，测极限帧率）
// 退出码：0 成功；2 Vulkan 错误。
// 设计依据：docs/EngineDesign/02-Rendering-Vulkan.md（RHI 雏形）
// ---------------------------------------------------------------------------

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
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

// 由 cmake/CompileShaders.cmake 生成的嵌入式 SPIR-V
extern const unsigned int lemon_spv_tri_vert[];
extern const unsigned int lemon_spv_tri_vert_count;
extern const unsigned int lemon_spv_tri_frag[];
extern const unsigned int lemon_spv_tri_frag_count;

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

struct AppArgs {
    int  frames = 0;       // 0 = 一直跑直到关窗
    bool immediate = false;
    bool validate = false;
    int  width = 1280, height = 720;
};

struct FrameSync {
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFence inFlight = VK_NULL_HANDLE;  // 每帧在途一份（CPU 侧节流）
};

struct Vertex {
    float pos[2];
    float color[3];
};

AppArgs ParseArgs(int argc, char** argv) {
    AppArgs a;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--frames") && i + 1 < argc) a.frames = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--immediate")) a.immediate = true;
        else if (!std::strcmp(argv[i], "--validate")) a.validate = true;
        else if (!std::strcmp(argv[i], "--w") && i + 1 < argc) a.width = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--h") && i + 1 < argc) a.height = std::atoi(argv[++i]);
    }
    return a;
}

const char* PresentModeName(VkPresentModeKHR m) {
    switch (m) {
        case VK_PRESENT_MODE_FIFO_KHR: return "FIFO";
        case VK_PRESENT_MODE_MAILBOX_KHR: return "MAILBOX";
        case VK_PRESENT_MODE_IMMEDIATE_KHR: return "IMMEDIATE";
        default: return "OTHER";
    }
}

} // namespace

int main(int argc, char** argv) {
    const AppArgs args = ParseArgs(argc, argv);

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "[lemon] SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow("Lemon spike 01 - triangle", args.width, args.height,
                                          SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    if (!window) {
        std::fprintf(stderr, "[lemon] SDL_CreateWindow failed: %s\n", SDL_GetError());
        return 1;
    }

    // ---- instance -----------------------------------------------------------
    uint32_t instApi = VK_API_VERSION_1_0;
    vkEnumerateInstanceVersion(&instApi);
    instApi = std::min(instApi, (uint32_t)VK_API_VERSION_1_3);  // 声明上限 1.3，MoltenVK 报 1.2 也无妨
    std::printf("[lemon] instance api %u.%u.%u\n", VK_VERSION_MAJOR(instApi), VK_VERSION_MINOR(instApi),
                VK_VERSION_PATCH(instApi));

    uint32_t sdlExtCount = 0;
    const char* const* sdlExts = SDL_Vulkan_GetInstanceExtensions(&sdlExtCount);
    if (!sdlExts) {
        std::fprintf(stderr, "[lemon] SDL_Vulkan_GetInstanceExtensions failed: %s\n", SDL_GetError());
        return 1;
    }
    std::vector<const char*> extensions(sdlExts, sdlExts + sdlExtCount);

    // MoltenVK 是 portability 实现：枚举它需要该扩展 + flag
    bool portabilityEnum = false;
    {
        uint32_t n = 0;
        vkEnumerateInstanceExtensionProperties(nullptr, &n, nullptr);
        std::vector<VkExtensionProperties> props(n);
        vkEnumerateInstanceExtensionProperties(nullptr, &n, props.data());
        for (auto& p : props) {
            if (!std::strcmp(p.extensionName, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
                extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
                portabilityEnum = true;
            }
        }
    }

    VkInstance instance = VK_NULL_HANDLE;
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
        if (layers.empty()) std::printf("[lemon] validation layer NOT found, running without\n");
    }
    {
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.pApplicationName = "lemon-spike-triangle";
        app.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
        app.pEngineName = "Lemon";
        app.engineVersion = VK_MAKE_VERSION(0, 1, 0);
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
    if (!SDL_Vulkan_CreateSurface(window, instance, nullptr, &surface)) {
        std::fprintf(stderr, "[lemon] SDL_Vulkan_CreateSurface failed: %s\n", SDL_GetError());
        return 1;
    }
    // ---- physical device ----------------------------------------------------
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
        bool hasSwapchain = false, hasPortabilitySubset = false;
        for (auto& e : exts) {
            if (!std::strcmp(e.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) hasSwapchain = true;
            if (!std::strcmp(e.extensionName, "VK_KHR_portability_subset")) hasPortabilitySubset = true;
        }
        if (!hasSwapchain) continue;

        // 离散卡优先；同为离散/集成时取第一个满足条件的
        bool candidateIsDiscrete = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
        bool currentIsDiscrete =
            physical != VK_NULL_HANDLE &&
            [&] {
                VkPhysicalDeviceProperties p2;
                vkGetPhysicalDeviceProperties(physical, &p2);
                return p2.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
            }();
        if (physical == VK_NULL_HANDLE || (candidateIsDiscrete && !currentIsDiscrete)) {
            physical = dev;
            graphicsFamily = family;
            deviceExts = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
            if (hasPortabilitySubset) deviceExts.push_back("VK_KHR_portability_subset");
        }
    }
    if (physical == VK_NULL_HANDLE) {
        std::fprintf(stderr, "[lemon] no usable Vulkan device (graphics+present+swapchain)\n");
        return 1;
    }

    VkPhysicalDeviceProperties devProps;
    vkGetPhysicalDeviceProperties(physical, &devProps);
    const char* devTypeName = "other";
    if (devProps.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) devTypeName = "discrete";
    else if (devProps.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) devTypeName = "integrated";
    std::printf("[lemon] device: %s (%s, api %u.%u.%u, driver %u.%u.%u)\n", devProps.deviceName,
                devTypeName, VK_VERSION_MAJOR(devProps.apiVersion), VK_VERSION_MINOR(devProps.apiVersion),
                VK_VERSION_PATCH(devProps.apiVersion), VK_VERSION_MAJOR(devProps.driverVersion),
                VK_VERSION_MINOR(devProps.driverVersion), VK_VERSION_PATCH(devProps.driverVersion));

    // ---- logical device + VMA ----------------------------------------------
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

    // ---- 顶点缓冲（VMA 宿主可见 + mapped） -----------------------------------
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VmaAllocation vertexAlloc = VK_NULL_HANDLE;
    void* vertexData = nullptr;
    {
        const Vertex verts[3] = {
            {{-0.6f, -0.6f}, {1.0f, 0.35f, 0.1f}},   // Lemon 橙
            {{0.6f, -0.6f}, {0.98f, 0.85f, 0.05f}},  // Lemon 黄
            {{0.0f, 0.6f}, {0.95f, 0.98f, 0.75f}},
        };
        VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bci.size = sizeof(verts);
        bci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        VmaAllocationCreateInfo aci{};
        aci.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
        aci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                    VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VmaAllocationInfo allocInfo{};
        VK_CHECK(vmaCreateBuffer(allocator, &bci, &aci, &vertexBuffer, &vertexAlloc, &allocInfo));
        vertexData = allocInfo.pMappedData;
        std::memcpy(vertexData, verts, sizeof(verts));
    }

    // ---- 着色器模块 ----------------------------------------------------------
    auto MakeModule = [&](const unsigned int* code, unsigned int count) {
        VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        ci.codeSize = (size_t)count * sizeof(unsigned int);
        ci.pCode = code;
        if (getenv("LEMON_DEBUG_SPV"))
            std::fprintf(stderr, "[lemon] module: count=%u codeSize=%zu first=%08x magic-ok=%d\n",
                         count, ci.codeSize, code ? code[0] : 0, code && code[0] == 0x07230203);
        VkShaderModule m = VK_NULL_HANDLE;
        VkResult r = vkCreateShaderModule(device, &ci, nullptr, &m);
        if (r != VK_SUCCESS)
            std::fprintf(stderr, "[lemon] vkCreateShaderModule -> %d (count=%u size=%zu magic=%08x)\n",
                         (int)r, count, ci.codeSize, code ? code[0] : 0);
        VK_CHECK(r);
        return m;
    };
    VkShaderModule vertModule = MakeModule(lemon_spv_tri_vert, lemon_spv_tri_vert_count);
    VkShaderModule fragModule = MakeModule(lemon_spv_tri_frag, lemon_spv_tri_frag_count);

    // ---- 命令池 / 同步对象 ---------------------------------------------------
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

    // present 信号量按"交换链图像"持有（图像被再次 acquire 前，其上一次 present 必已完成，
    // 信号量必已清零）。acquire 走 fence-only 模式：acquire 不用信号量，CPU 等 fence 后再
    // 提交，从根上消除 acquire 信号量复用竞态（VUID-vkQueueSubmit-pSignalSemaphores-00067）。
    std::vector<VkSemaphore> renderFinished;   // submit -> present
    VkFence acquireFence = VK_NULL_HANDLE;     // 每帧 acquire 同步（串行使用，单枚即可）
    {
        VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        VK_CHECK(vkCreateFence(device, &fci, nullptr, &acquireFence));
    }

    // ---- 交换链相关对象（可整体重建） -----------------------------------------
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

    auto CreateSwapchainObjects = [&](VkSwapchainKHR old) {
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
        if (extent.width == 0 || extent.height == 0) return false;  // 最小化，稍后再试

        // 格式：优先 B8G8R8A8_SRGB + SRGB_NONLINEAR
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

        // 呈现模式
        {
            uint32_t n = 0;
            VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(physical, surface, &n, nullptr));
            std::vector<VkPresentModeKHR> modes(n);
            VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(physical, surface, &n, modes.data()));
            auto has = [&](VkPresentModeKHR m) {
                return std::find(modes.begin(), modes.end(), m) != modes.end();
            };
            std::string all;
            for (auto m : modes) { all += PresentModeName(m); all += " "; }
            if (args.immediate) {
                if (has(VK_PRESENT_MODE_IMMEDIATE_KHR)) chosenPresent = VK_PRESENT_MODE_IMMEDIATE_KHR;
                else if (has(VK_PRESENT_MODE_MAILBOX_KHR)) chosenPresent = VK_PRESENT_MODE_MAILBOX_KHR;
                else chosenPresent = VK_PRESENT_MODE_FIFO_KHR;
            } else {
                chosenPresent = has(VK_PRESENT_MODE_MAILBOX_KHR) ? VK_PRESENT_MODE_MAILBOX_KHR
                                                                 : VK_PRESENT_MODE_FIFO_KHR;
            }
            std::printf("[lemon] present modes available: %s -> using %s\n", all.c_str(),
                        PresentModeName(chosenPresent));
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
        ci.oldSwapchain = old;
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

        // render pass
        VkAttachmentDescription color{};
        color.format = sc.format;
        color.samples = VK_SAMPLE_COUNT_1_BIT;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
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

        // pipeline（动态 viewport/scissor）
        VkPipelineShaderStageCreateInfo stages[2]{
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
             VK_SHADER_STAGE_VERTEX_BIT, vertModule, "main", nullptr},
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
             VK_SHADER_STAGE_FRAGMENT_BIT, fragModule, "main", nullptr}};

        VkVertexInputBindingDescription binding{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
        VkVertexInputAttributeDescription attrs[2]{
            {0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, pos)},
            {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color)}};
        VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        vi.vertexBindingDescriptionCount = 1;
        vi.pVertexBindingDescriptions = &binding;
        vi.vertexAttributeDescriptionCount = 2;
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
        pci.layout = VK_NULL_HANDLE;  // 无描述符，layout 可为空？——必须提供，见下
        pci.renderPass = sc.renderPass;
        pci.subpass = 0;
        VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        VkPipelineLayout layout = VK_NULL_HANDLE;
        VK_CHECK(vkCreatePipelineLayout(device, &plci, nullptr, &layout));
        pci.layout = layout;

        VkPipeline pipeline = VK_NULL_HANDLE;
        VkResult pr = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pci, nullptr, &pipeline);
        vkDestroyPipelineLayout(device, layout, nullptr);  // pipeline 持有自己的引用
        VK_CHECK(pr);
        sc.pipeline = pipeline;

        // framebuffers
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

    // ---- 主循环 ---------------------------------------------------------------
    const Uint64 perfFreq = SDL_GetPerformanceFrequency();
    Uint64 prev = SDL_GetPerformanceCounter();
    uint64_t frameNo = 0;
    double minMs = 1e9, maxMs = 0, sumMs = 0;
    bool running = true;
    bool swapchainOk = false;

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
            swapchainOk = CreateSwapchainObjects(VK_NULL_HANDLE);
            needRecreate = false;
            if (!swapchainOk) continue;  // 窗口最小化（extent 为 0），下轮再试
        }

        const uint32_t fi = frameNo % kFramesInFlight;
        FrameSync& fs = frames[fi];
        VK_CHECK(vkWaitForFences(device, 1, &fs.inFlight, VK_TRUE, UINT64_MAX));
        vkResetFences(device, 1, &fs.inFlight);

        uint32_t imageIndex = 0;
        VK_CHECK(vkResetFences(device, 1, &acquireFence));
        VkResult acquired =
            vkAcquireNextImageKHR(device, sc.swapchain, UINT64_MAX, VK_NULL_HANDLE, acquireFence, &imageIndex);
        if (acquired == VK_ERROR_OUT_OF_DATE_KHR) { needRecreate = true; continue; }
        VK_CHECK(acquired == VK_SUBOPTIMAL_KHR ? VK_SUCCESS : acquired);
        // fence-only acquire：CPU 等 image 就绪后再提交（提交无等待信号量）
        VK_CHECK(vkWaitForFences(device, 1, &acquireFence, VK_TRUE, UINT64_MAX));

        VK_CHECK(vkResetCommandBuffer(fs.cmd, 0));
        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VK_CHECK(vkBeginCommandBuffer(fs.cmd, &bi));

        VkClearValue clear{{{0.07f, 0.07f, 0.09f, 1.0f}}};
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

        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(fs.cmd, 0, 1, &vertexBuffer, &offset);
        vkCmdDraw(fs.cmd, 3, 1, 0, 0);
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

        // 帧统计
        Uint64 now = SDL_GetPerformanceCounter();
        double ms = (double)(now - prev) * 1000.0 / (double)perfFreq;
        prev = now;
        ++frameNo;
        minMs = std::min(minMs, ms);
        maxMs = std::max(maxMs, ms);
        sumMs += ms;

        if (args.frames > 0 && (int)frameNo >= args.frames) running = false;
    }

    vkDeviceWaitIdle(device);

    if (frameNo > 0) {
        std::printf("[lemon] frames=%llu avg=%.2fms (%.1f fps) min=%.2fms max=%.2fms present=%s\n",
                    (unsigned long long)frameNo, sumMs / frameNo, 1000.0 * frameNo / sumMs, minMs, maxMs,
                    PresentModeName(chosenPresent));
    }

    // ---- 清理 -----------------------------------------------------------------
    DestroySwapchainObjects();
    vkDestroyFence(device, acquireFence, nullptr);
    for (auto& f : frames) vkDestroyFence(device, f.inFlight, nullptr);
    vkDestroyCommandPool(device, cmdPool, nullptr);
    vkDestroyShaderModule(device, vertModule, nullptr);
    vkDestroyShaderModule(device, fragModule, nullptr);
    vmaDestroyBuffer(allocator, vertexBuffer, vertexAlloc);
    vmaDestroyAllocator(allocator);
    vkDestroyDevice(device, nullptr);
    vkDestroySurfaceKHR(instance, surface, nullptr);
    vkDestroyInstance(instance, nullptr);
    SDL_DestroyWindow(window);
    SDL_Quit();
    std::printf("[lemon] spike-01 exit OK\n");
    return 0;
}
