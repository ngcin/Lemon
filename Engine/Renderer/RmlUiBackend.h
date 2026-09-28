// Lemon 引擎 — RmlUi Vulkan 呈现后端（M6a 批③a，ADR-014 D2/D3）
// 纪律：本头零 Vulkan/RmlUi 类型；实现全在 RmlUiBackend.cpp（Renderer .cpp 豁免区，
// Vulkan 经 RHI 内部桥 + NativeCommandBuffer——均为 Renderer 兄弟后端专用通道）。
// 帧协议（由 Engine/Ui 的 UiSubsystem 驱动；gameRT 动态渲染块内、sprite Record 之后）：
//   BeginFrame(cl, rtW, rtH) → [Rml::Context::Render() 逐批回调本后端] → EndFrame()
// 着色器：rmlui.vert / rmlui_color.frag / rmlui_texture.frag（构建期嵌入）。
#pragma once

#include <memory>

#include "Renderer/RHI.h"

namespace lemon::renderer {

class RmlUiBackend {
public:
    RmlUiBackend();                        // 定义在 cpp（Pimpl：内联 =default 会在
    ~RmlUiBackend();                       // 未 Shutdown = 断言（拥有者显式收尾，SpriteBatcher 同契约）
    RmlUiBackend(const RmlUiBackend&) = delete;
    RmlUiBackend& operator=(const RmlUiBackend&) = delete;

    /// 建管线/几何池/描述符池 + 设备丢失回调（RGBA8Unorm = gameRT 格式）
    void Init(rhi::Device& device, rhi::Format colorFormat);
    /// 反注册回调 + WaitIdle 后销毁全部 Vulkan 态（必须在 Device 存活期调用）
    void Shutdown();

    /// 帧首：存命令缓冲/尺寸、算投影、钉全幅 viewport+scissor（spike 改造⑧）
    void BeginFrame(rhi::CommandList& cl, uint32_t rtW, uint32_t rtH);
    /// 帧尾：几何池显式 flush（spike 改造⑫；一致内存上 no-op）
    void EndFrame();

    /// Rml::RenderInterface 实现裸指针（void* = Rml::RenderInterface*；仅 Engine/Ui
    /// 的 .cpp 转型后交 Rml::SetRenderInterface——RmlUi 类型不出本模块 .cpp 的边界）
    void* RenderInterfacePtr();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace lemon::renderer
