// Lemon 引擎 — RmlUi Vulkan 呈现后端（M6b 批③a，ADR-014 D2/D3）
// 纪律：本头零 Vulkan/RmlUi 类型；实现全在 RmlUiBackend.cpp（Renderer .cpp 豁免区，
// Vulkan 经 RHI 内部桥 + NativeCommandBuffer——均为 Renderer 兄弟后端专用通道）。
// 帧协议（由 Engine/Ui 的 UiSubsystem 驱动；gameRT 动态渲染块内、sprite Record 之后）：
//   BeginFrame(cl, rtW, rtH) → [Rml::Context::Render() 逐批回调本后端] → EndFrame()
// 着色器：rmlui.vert / rmlui_color.frag / rmlui_texture.frag（构建期嵌入）。
#pragma once

#include <functional>
#include <memory>
#include <string>

#include "Renderer/RHI.h"

namespace lemon::renderer {

/// 贴图桥解析器（批③b，ADR-014 M6 资产源）：source = RmlUi JoinPath 解析后的路径。
/// 命中则回填引擎纹理句柄 + 尺寸——后端按"外部纹理"包装（只持描述符集借用其
/// view；image 生命周期归调用方/图集，释放只还描述符集）。
using UiTextureResolver =
    std::function<bool(const std::string& source, rhi::Texture& tex, uint32_t& w, uint32_t& h)>;

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

    /// 贴图桥解析器（批③b）：LoadTexture 先问它，未命中走 ③a 告警语义。
    void SetTextureResolver(UiTextureResolver fn);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace lemon::renderer
