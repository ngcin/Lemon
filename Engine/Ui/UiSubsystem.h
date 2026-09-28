// Lemon 引擎 — 游戏 UI 子系统（M6a 批③a 最小版，ADR-014 D3）
// 职责：RmlUi 上下文生命周期 + 内存文档加载/显隐 + 帧 Update/Render 驱动。
// 纪律：本头零 RmlUi 类型（Pimpl 全在 UiSubsystem.cpp——RmlUi 类型不出 .cpp）。
// 演进：③b 资产通道（.rml/.rcss GUID + Noto 字体随引擎）→ ③c C# API（UI.Apply(ops)
// + UiEvent 队列 + 契约响亮失败 + 输入路由 M7）——本批仅为呈现地基与冒烟通道。
#pragma once

#include <memory>

#include "Renderer/RHI.h"

namespace lemon::ui {

class UiSubsystem {
public:
    UiSubsystem(); // 定义在 cpp（Pimpl：内联 =default 会实例化 ~unique_ptr<Impl> 于
                   // Impl 不完整的 TU——构造异常路径需析构已构成员）
    ~UiSubsystem(); // 未 Shutdown = 断言（拥有者显式收尾，SpriteBatcher 同契约）
    UiSubsystem(const UiSubsystem&) = delete;
    UiSubsystem& operator=(const UiSubsystem&) = delete;

    /// 建后端 + RmlUi 初始化 + context("game") + 系统字体链；false = 不可用
    /// （字体链全败等——UI 缺席不阻断编辑器/游戏，红字告警）
    bool Init(rhi::Device& device, rhi::Format rtFormat);
    void Shutdown(); // Rml::Shutdown → backend.Shutdown（序：Rml 收尾期间仍会回调后端）

    /// 内存文档（③a 冒烟通道；③b 起走 .rml 资产加载）。同名覆盖旧文档。
    bool LoadDocumentFromMemory(const char* name, const char* rmlText);
    bool ShowDocument(const char* name, bool show);
    bool HasDocument(const char* name) const;

    /// 帧逻辑步（Play 中、World 步进后调用——context Update）
    void Update();
    /// 帧呈现（gameRT 动态渲染块内、sprite 之后：尺寸变化 → SetDimensions，随后
    /// backend.BeginFrame → ctx Render → backend.EndFrame）
    void Render(rhi::CommandList& cl, uint32_t rtW, uint32_t rtH);

    /// 冒烟探针：实际载入的字体族名（空 = 未初始化）
    const char* LoadedFontFamily() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace lemon::ui
