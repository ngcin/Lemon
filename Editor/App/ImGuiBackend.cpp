// Lemon 编辑器 — ImGui 后端胶水实现。
// 本文件是 Editor/ 内唯一含 Vulkan 头的 TU（见 ImGuiBackend.h 纪律注释）：
// 只做 Vulkan 句柄的取得与传递，不创建/销毁任何引擎资源。
#include "App/ImGuiBackend.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

#include <SDL3/SDL.h>
#include <vulkan/vulkan.h>

#include "imgui.h"
#include "backends/imgui_impl_sdl3.h"
#include "backends/imgui_impl_vulkan.h"

#include "Core/Log.h"
#include "Tooling/Theme.h"
#include "Platform/Window.h"
#include "Renderer/RHI.h"

namespace lemon::editor {

// ------------------------------------------------------------- 字体来源 ----
// 运行时加载系统 CJK 字体（TrueType 轮廓；stb_truetype 不支持 CFF）。
// 内嵌 OFL 字体子集是打包项（05 §10 / M4-Editor-Plan §2.3），开发期用系统字体
// 满足冒烟红线；此处按平台给出候选列表，取首个可加载者。
static const char* kCjkFontCandidates[] = {
#if defined(__APPLE__)
    "/System/Library/Fonts/STHeiti Light.ttc",   // 华文黑体（TrueType，Apple 系统）
    "/System/Library/Fonts/STHeiti Medium.ttc",
    "/System/Library/Fonts/Supplemental/Songti.ttc",
    "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
#elif defined(_WIN32)
    "C:\\Windows\\Fonts\\msyh.ttc",              // 微软雅黑（TrueType）
    "C:\\Windows\\Fonts\\simhei.ttf",
#else
    "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc",
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc", // 可能 CFF，加载失败自动跳过
#endif
};

static bool ReadFileBytes(const char* path, std::vector<char>& out) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return false;
    std::streampos size = f.tellg();
    if (size <= 0 || size > 64 * 1024 * 1024) return false;
    f.seekg(0, std::ios::beg);
    out.resize((size_t)size);
    f.read(out.data(), size);
    return f.good() || f.eof();
}

static VkFormat ToVkFormat(rhi::Format f) { // 引擎侧 ToVk 在 RHI.cpp 内部，此处复制最小映射
    switch (f) {
        case rhi::Format::RGBA8Unorm: return VK_FORMAT_R8G8B8A8_UNORM;
        case rhi::Format::BGRA8UnormSrgb: return VK_FORMAT_B8G8R8A8_SRGB;
        case rhi::Format::RGBA8UnormSrgb: return VK_FORMAT_R8G8B8A8_SRGB;
        case rhi::Format::RGBA16Float: return VK_FORMAT_R16G16B16A16_SFLOAT;
        default: return VK_FORMAT_UNDEFINED;
    }
}

struct ImGuiBackend::Impl {
    Window* window = nullptr;
    rhi::Device* device = nullptr;
    std::vector<char> fontData;   // 字体字节（FontDataOwnedByAtlas=false，自行持有）
    bool cjkLoaded = false;
    float scale = 1.0f;
    std::string iniFilename;      // io.IniFilename 指向本串，须长寿

    void SetupStyle(float scale) {
        // M4.7a：主题单点（Tooling/Theme——Unity 深色系；本函数不再自写色值）
        theme::ApplyTheme(scale);
    }

    void RebuildFonts(float scale) {
        ImGuiIO& io = ImGui::GetIO();
        io.Fonts->Clear();
        if (!fontData.empty()) {
            ImFontConfig cfg;
            cfg.FontDataOwnedByAtlas = false; // 字节由本 Impl 持有，防双释放
            cfg.FontNo = 0;                   // TTC 首字体
            // 像素密度缩放下按 pointSize*density 栅格化（配合 DisplayFramebufferScale
            // 逐像素对齐，非整倍拉伸）；含 ASCII + 2500 常用简体（05 §10）
            io.Fonts->AddFontFromMemoryTTF(fontData.data(), (int)fontData.size(),
                                           16.0f * scale, &cfg,
                                           io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
            cjkLoaded = true;
        } else {
            io.Fonts->AddFontDefault(); // 回退：中文将显示为 '?'
            cjkLoaded = false;
        }
        // 1.92 纹理后端（RendererHasTextures）：不手工 Build，后端按需烘焙上传
    }

    void ApplyScale(float newScale) {
        scale = newScale;
        SetupStyle(scale);
        ImGui::GetStyle().ScaleAllSizes(scale); // 1.92：尺寸缩放（字体另行重栅格化）
        RebuildFonts(scale);
    }

    static void EventThunk(const void* sdlEvent, void* userData) {
        (void)userData;
        ImGui_ImplSDL3_ProcessEvent((const SDL_Event*)sdlEvent);
    }
};

ImGuiBackend::ImGuiBackend() : m(std::make_unique<Impl>()) {}
ImGuiBackend::~ImGuiBackend() { Shutdown(); }

bool ImGuiBackend::Init(Window& window, rhi::Device& device, const char* iniDir) {
    m->window = &window;
    m->device = &device;

    // 布局持久化目录（.lemon/editor/imgui.ini；M4.0 起，布局持久化 = M4-Editor-Plan §1.1）
    std::error_code ec;
    std::filesystem::create_directories(iniDir, ec);
    m->iniFilename = std::string(iniDir) + "/imgui.ini";

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable; // 单窗口 docking（决议 #7）
    io.IniFilename = m->iniFilename.c_str();

    // CJK 字体：取首个可加载候选（加载结果冒烟断言）
    for (const char* path : kCjkFontCandidates) {
        if (ReadFileBytes(path, m->fontData)) {
            LEMON_LOG("editor font: %s (%zu bytes)", path, m->fontData.size());
            break;
        }
        m->fontData.clear();
    }
    if (m->fontData.empty())
        LEMON_WARN("未找到可用 CJK 系统字体（中文将无法渲染；内嵌 OFL 子集在打包期解决）");

    m->ApplyScale(1.0f); // 首帧 BeginFrame 内按实际像素密度校正

    if (!ImGui_ImplSDL3_InitForVulkan((SDL_Window*)window.NativeHandle())) {
        LEMON_WARN("ImGui_ImplSDL3_InitForVulkan failed");
        return false;
    }
    window.SetEventObserver(&Impl::EventThunk, nullptr);

    rhi::VulkanInteropHandles vi = device.GetVulkanInterop();
    ImGui_ImplVulkan_InitInfo info{};
    info.ApiVersion = vi.apiVersion;
    info.Instance = (VkInstance)vi.instance;
    info.PhysicalDevice = (VkPhysicalDevice)vi.physicalDevice;
    info.Device = (VkDevice)vi.device;
    info.QueueFamily = vi.queueFamily;
    info.Queue = (VkQueue)vi.queue;
    info.DescriptorPoolSize = 64; // 后端自建池（FREE_DESCRIPTOR_SET 位由后端置位）
    info.MinImageCount = vi.swapchainImageCount;
    info.ImageCount = vi.swapchainImageCount;
    info.UseDynamicRendering = true; // 引擎动态渲染（无 renderPass 对象），RHI.cpp 同款约定
    info.MinAllocationSize = 1024 * 1024;
    info.CheckVkResultFn = [](VkResult err) {
        if (err != VK_SUCCESS)
            LEMON_WARN("imgui-vulkan: VkResult %d", (int)err);
    };
    VkFormat colorFmt = ToVkFormat(device.SwapchainFormat());
    VkPipelineRenderingCreateInfo prci{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    prci.colorAttachmentCount = 1;
    prci.pColorAttachmentFormats = &colorFmt;
    info.PipelineInfoMain.PipelineRenderingCreateInfo = prci;
    info.PipelineInfoForViewports.PipelineRenderingCreateInfo = prci; // 多视口未启用，仅占位

    if (!ImGui_ImplVulkan_Init(&info)) {
        LEMON_WARN("ImGui_ImplVulkan_Init failed");
        return false;
    }
    return true;
}

void ImGuiBackend::Shutdown() {
    if (!ImGui::GetCurrentContext()) return;
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    if (m->window) m->window->SetEventObserver(nullptr, nullptr);
    m->window = nullptr;
}

void ImGuiBackend::BeginFrame(Window& window) {
    // DPI 自检：像素密度变化（跨屏拖动）→ style + 字体重建
    float density = SDL_GetWindowPixelDensity((SDL_Window*)window.NativeHandle());
    if (density <= 0.0f) density = 1.0f;
    if (density != m->scale) m->ApplyScale(density);

    ImGui_ImplSDL3_NewFrame();
    if (mouseOverrideSet_) { // 冒烟扫掠：轮询后、排水前注入（顺序靠后生效）
        mouseOverrideSet_ = false;
        ImGuiIO& io = ImGui::GetIO();
        io.AddMousePosEvent(mouseOverrideX_, mouseOverrideY_);
        if (mouseBtnSet_) { // --smoke-drag：左键态跟随位置注入
            mouseBtnSet_ = false;
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, mouseBtnLeft_);
        }
        if (wheelSet_) { // --smoke-drag：滚轮格数注入
            wheelSet_ = false;
            io.AddMouseWheelEvent(0.0f, mouseWheel_);
        }
    }
    if (chordSet_) { // --smoke-drag/ui：本帧 (mods+)key down，下一帧 key(+mods) up
        chordSet_ = false;
        ImGuiIO& io = ImGui::GetIO();
        // macOS：ConfigMacOSXBehaviors（默认开）在事件层交换 Ctrl↔Super——真键盘
        // Cmd 进来被换成 Ctrl；注入侧同理必须发 Super 才能点亮 KeyCtrl，
        // 否则 IsKeyChordPressed(Ctrl|X) 的 KeyMods 精确比对永假（组合键全失效）
        int mods = chordMods_;
#ifdef __APPLE__
        if (mods & (int)ImGuiMod_Ctrl) {
            mods &= ~(int)ImGuiMod_Ctrl;
            mods |= (int)ImGuiMod_Super;
        }
#endif
        if (mods) io.AddKeyEvent((ImGuiKey)mods, true);
        io.AddKeyEvent((ImGuiKey)chordKey_, true);
        chordUpPending_ = true;
    } else if (chordUpPending_) {
        chordUpPending_ = false;
        ImGuiIO& io = ImGui::GetIO();
        io.AddKeyEvent((ImGuiKey)chordKey_, false);
#ifdef __APPLE__
        if (chordMods_ & (int)ImGuiMod_Ctrl) {
            io.AddKeyEvent(ImGuiMod_Super, false);
        } else
#endif
        if (chordMods_) io.AddKeyEvent((ImGuiKey)chordMods_, false);
    }
    if (textSet_) { // --smoke-ui：文本上屏（InputText 活动时逐字符进）
        textSet_ = false;
        if (textIn_) ImGui::GetIO().AddInputCharactersUTF8(textIn_);
        textIn_ = nullptr;
    }
    ImGui_ImplVulkan_NewFrame();
    ImGui::NewFrame();
}

void ImGuiBackend::Render(rhi::CommandList& cl) {
    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(),
                                    (VkCommandBuffer)cl.NativeCommandBuffer());
}

float ImGuiBackend::DisplayScale() const { return m->scale; }
bool ImGuiBackend::CjkFontLoaded() const { return m->cjkLoaded; }

void* ImGuiBackend::RegisterViewportTexture(uint32_t rhiTextureId) {
    void* view = m->device->GetVulkanTextureViewInterop(rhi::Texture{rhiTextureId});
    if (!view) return nullptr;
    // 描述符集句柄 = ImTextureID（1.92 Vulkan 后端纹理通道；SHADER_READ 布局与
    // EndPass 的离屏转换一致）
    return (void*)ImGui_ImplVulkan_AddTexture((VkImageView)view,
                                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void ImGuiBackend::UnregisterViewportTexture(void* imguiTexId) {
    if (imguiTexId) ImGui_ImplVulkan_RemoveTexture((VkDescriptorSet)imguiTexId);
}

} // namespace lemon::editor
