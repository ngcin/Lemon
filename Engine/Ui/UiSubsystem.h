// Lemon 引擎 — 游戏 UI 子系统（M6a 批③a 呈现地基 / 批③b 字体与资产通道，ADR-014 D3）
// 职责：RmlUi 上下文生命周期 + 字体注册 + 内存/文件文档加载/显隐/热重载 +
// 贴图解析器门面 + 帧 Update/Render 驱动。
// 纪律：本头零 RmlUi 类型（Pimpl 全在 UiSubsystem.cpp——RmlUi 类型不出 .cpp）。
// 演进：③c C# API（UI.Apply(ops) + UiEvent 队列 + 契约响亮失败 + 输入路由 M7）。
#pragma once

#include <functional>
#include <memory>
#include <string>

#include "Renderer/RHI.h"

namespace lemon::ui {

/// 贴图桥解析器（批③b，ADR-014 M6 资产源）：source = RmlUi JoinPath 解析后的
/// 路径；命中则回填引擎纹理句柄 + 尺寸（外部纹理——后端只持描述符集，image 归
/// 图集/调用方）。安装方 = 编辑器（路径 → AssetDatabase → AtlasRegistry）。
using UiTextureResolver =
    std::function<bool(const std::string& source, rhi::Texture& tex, uint32_t& w, uint32_t& h)>;

class UiSubsystem {
public:
    UiSubsystem(); // 定义在 cpp（Pimpl：内联 =default 会实例化 ~unique_ptr<Impl> 于
                   // Impl 不完整的 TU——构造异常路径需析构已构成员）
    ~UiSubsystem(); // 未 Shutdown = 断言（拥有者显式收尾，SpriteBatcher 同契约）
    UiSubsystem(const UiSubsystem&) = delete;
    UiSubsystem& operator=(const UiSubsystem&) = delete;

    /// 建后端 + RmlUi 初始化 + context("game") + 系统字体链兜底；false = 不可用
    /// （字体链全败等——UI 缺席不阻断编辑器/游戏，红字告警）
    bool Init(rhi::Device& device, rhi::Format rtFormat);
    void Shutdown(); // Rml::Shutdown → backend.Shutdown（序：Rml 收尾期间仍会回调后端）

    /// 注册字体（批③b：引擎 Noto / 项目字体）。family = 调用方所知族名（报告/冒烟
    /// 断言用；RmlUi 按文件内名匹配——传错名渲染不受影响但报告失真）。fallback=true
    /// 时成功装载将更新 LoadedFontFamily（引擎正字优先）。
    bool LoadFontFace(const char* absPath, const char* family, bool fallback);

    /// 安装贴图桥解析器（须在文档加载前；文档重载即按新解析器重解）。
    void SetTextureResolver(UiTextureResolver fn);

    /// 内存文档（③a 冒烟通道）。同名覆盖旧文档。
    bool LoadDocumentFromMemory(const char* name, const char* rmlText);
    /// 文件文档（批③b 资产通道）：absPath 为文档底稿——相对 href/src 按其目录解析
    /// （SystemInterface::JoinPath 覆写）。name 惯例 = 资产 relPath（热重载对账键）。
    bool LoadDocumentFromFile(const char* name, const char* absPath);
    /// 热重载（批③b）：重读底稿（file = 重读盘，内存 = 原文本）；shown 态保持。
    /// 全量重载前清 RmlUi 样式表缓存（Factory 按路径缓存 <link> 解析结果——不清
    /// 则重载后样式仍旧档，实测 2026-09-28）。
    bool ReloadDocument(const char* name);
    /// 样式热重载（.rcss 变更）：逐文档 ReloadStyleSheet——保 DOM/元素状态（③c
    /// 数据重灌的底座）+ 清缓存重建样式；比全量重载轻。
    void ReloadStyleSheets();
    /// 全量重载（设备丢失路径：几何/纹理均需重编译）。
    void ReloadAllDocuments();
    /// 卸载单文档（资产删除路径）；全部卸载（切项目）。
    bool UnloadDocument(const char* name);
    void UnloadAllDocuments();

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
