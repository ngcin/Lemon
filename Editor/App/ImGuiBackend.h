// Lemon 编辑器 — ImGui 后端胶水（M4-Editor-Plan §3.1/§3.2）
// 职责：ImGui context 生命周期、SDL3 事件桥接、Vulkan 后端初始化（经 RHI interop
// 豁免口）、DPI 缩放（style.ScaleAllDimensions + 字体按密度重栅格化）、CJK 字体加载。
//
// 纪律（本目录唯一例外 TU）：
//   * ImGuiBackend.cpp 是 Editor/ 内唯一允许包含 Vulkan 头的编译单元（后端 glue，
//     纯粘合零引擎逻辑）；对应引擎侧豁免口 = rhi::VulkanInteropHandles /
//     rhi::CommandList::NativeCommandBuffer（07 例外登记，2026-09-19）。
//   * lemon-engine 不出现任何 ImGui 引用（tests/imgui_isolation.cmake 编译期断言）。
#pragma once

#include <memory>
#include <string>

namespace lemon {
class Window;
namespace rhi {
class Device;
class CommandList;
}
}

namespace lemon::editor {

class ImGuiBackend {
public:
    ImGuiBackend();
    ~ImGuiBackend();

    /// iniDir：布局持久化目录（如 ".lemon/editor"），内部创建；布局文件 imgui.ini。
    bool Init(Window& window, rhi::Device& device, const char* iniDir);
    void Shutdown();

    /// 每帧开头：DPI 自检（变化则重建字体/缩放 style）+ 三段 NewFrame。
    /// 事件泵由 Window 事件观察者在 Init 时挂好，无需手工喂。
    void BeginFrame(Window& window);

    /// 帧录制段：ImGui::Render + 在"当前渲染块内"录制 draw data。
    /// 必须在 cl.BeginPass() 与 cl.EndPass() 之间调用（动态渲染约定）。
    void Render(rhi::CommandList& cl);

    /// 当前显示缩放（= SDL 窗口像素密度；1.0 = 标准 DPI，2.0 = Retina）
    float DisplayScale() const;
    /// CJK 字体是否加载成功（冒烟断言项：失败 = 回退 ProggyClean，中文显示为问号）
    bool CjkFontLoaded() const;

    /// 视口纹理注册（M4.2）：引擎纹理 → ImTextureID（ImGui Image 显示）。
    /// 纹理重建（resize）时 Unregister 旧的再注册新的。返回 nullptr = 失败。
    void* RegisterViewportTexture(uint32_t rhiTextureId);
    void UnregisterViewportTexture(void* imguiTexId);

private:
    struct Impl;
    std::unique_ptr<Impl> m;
};

} // namespace lemon::editor
