// Lemon 引擎 — 游戏 UI 子系统实现（M6a 批③a，ADR-014）
// RmlUi 类型只准出现在本 .cpp（头文件零泄漏，同 Vulkan 纪律形状）。
#include "Ui/UiSubsystem.h"

#include <chrono>
#include <cstring>
#include <string>
#include <unordered_map>

#include <RmlUi/Core.h>

#include "Core/Log.h"
#include "Renderer/RmlUiBackend.h"

namespace lemon::ui {
namespace {

struct LemonSystemInterface final : Rml::SystemInterface {
    std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();

    double GetElapsedTime() override {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    }

    bool LogMessage(Rml::Log::Type type, const Rml::String& message) override {
        switch (type) {
        case Rml::Log::LT_ERROR:
        case Rml::Log::LT_ASSERT:
            LEMON_ERROR("[rmlui] %s", message.c_str());
            break;
        case Rml::Log::LT_WARNING:
            LEMON_WARN("[rmlui] %s", message.c_str());
            break;
        default:
            LEMON_LOG("[rmlui] %s", message.c_str());
            break;
        }
        return true; // 截断 RmlUi 默认 stdout 输出（引擎 Console 为唯一汇聚）
    }
};

// ③a 开发机字体链（spike-04 同款降级链；③b 换 Noto Sans CJK 随引擎/模板带，
// ADR-008 D2——正式版不依赖系统字体）
struct FontCandidate {
    const char* path;
    const char* family;
};
constexpr FontCandidate kFontChain[] = {
    {"/System/Library/Fonts/Hiragino Sans GB.ttc", "Hiragino Sans GB"},
    {"/System/Library/Fonts/STHeiti Medium.ttc", "Heiti SC"},
    {"/System/Library/Fonts/Songti.ttc", "Songti SC"},
};

} // namespace

struct UiSubsystem::Impl {
    std::unique_ptr<renderer::RmlUiBackend> backend = std::make_unique<renderer::RmlUiBackend>();
    LemonSystemInterface sys;
    Rml::Context* ctx = nullptr;
    rhi::Device* device = nullptr;
    rhi::Device::RecreateCallbackId recreateCb = 0;
    uint32_t ctxW = 0, ctxH = 0;
    std::string fontFamily;

    struct Doc {
        std::string source;              // 设备丢失重载底稿
        Rml::ElementDocument* doc = nullptr;
        bool shown = false;
    };
    std::unordered_map<std::string, Doc> docs;

    void UnloadDoc(Doc& d) {
        if (d.doc) {
            d.doc->Close(); // 从 context 移除并销毁（几何随后走 ReleaseGeometry 延迟回收）
            d.doc = nullptr;
        }
    }
};

// ------------------------------------------------------------- 生命周期 ----
UiSubsystem::UiSubsystem() = default;
UiSubsystem::~UiSubsystem() {
    // Run() 的早退路径（--save-scene/--smoke-guid/看门狗 return 等）不经主收尾段——
    // 成员声明序保证此时 device/viewport 仍存活（gameUi_ 后声明先析构），防御性就地
    // 收尾（红字留痕；主路径仍应显式 Shutdown，序即契约）
    if (impl_) {
        LEMON_WARN("UiSubsystem 析构前未显式 Shutdown（早退路径）——就地收尾");
        Shutdown();
    }
}

bool UiSubsystem::Init(rhi::Device& device, rhi::Format rtFormat) {
    impl_ = std::make_unique<Impl>();
    Impl& i = *impl_;
    i.device = &device;
    i.backend->Init(device, rtFormat);
    Rml::SetSystemInterface(&i.sys);
    Rml::SetRenderInterface((Rml::RenderInterface*)i.backend->RenderInterfacePtr());
    if (!Rml::Initialise()) {
        LEMON_ERROR("ui-subsystem: Rml::Initialise 失败");
        return false;
    }
    i.ctx = Rml::CreateContext("game", Rml::Vector2i(64, 64));
    if (!i.ctx) {
        LEMON_ERROR("ui-subsystem: CreateContext 失败");
        return false;
    }
    for (const auto& c : kFontChain) {
        if (Rml::LoadFontFace(c.path)) {
            i.fontFamily = c.family;
            break;
        }
    }
    if (i.fontFamily.empty()) {
        LEMON_ERROR("ui-subsystem: 系统字体链全败（Hiragino/STHeiti/Songti）——"
                    "③b 将随引擎带 Noto Sans CJK；本会话 UI 层不可用");
        return false;
    }
    // 设备丢失：后端先重建（注册序在前），本层随后重生字体图集 + 按底稿重载文档
    // （文档重载 → RmlUi 重编译几何——池是新的，旧几何句柄已随设备死）
    i.recreateCb = device.AddRecreateCallback("ui-subsystem", [this](rhi::Device&) {
        Rml::ReleaseFontResources();
        for (auto& [name, d] : impl_->docs) {
            const bool shown = d.shown;
            impl_->UnloadDoc(d);
            Rml::ElementDocument* doc = impl_->ctx->LoadDocumentFromMemory(
                Rml::String(d.source), Rml::String(name));
            d.doc = doc;
            d.shown = shown;
            if (doc && shown) doc->Show();
        }
    });
    return true;
}

void UiSubsystem::Shutdown() {
    if (!impl_) return;
    Impl& i = *impl_;
    if (i.recreateCb) i.device->RemoveRecreateCallback(i.recreateCb);
    // 序：Rml 收尾期间仍会回调 backend（ReleaseGeometry/Texture）——backend 必须活到
    // Rml::Shutdown 之后；context/doc 由 Rml::Shutdown 统一销毁（句柄此后不可再碰）
    Rml::Shutdown();
    i.backend->Shutdown();
    impl_.reset();
}

// ---------------------------------------------------------------- 文档 ----
bool UiSubsystem::LoadDocumentFromMemory(const char* name, const char* rmlText) {
    if (!impl_ || !impl_->ctx) return false;
    Impl& i = *impl_;
    auto it = i.docs.find(name);
    if (it != i.docs.end()) i.UnloadDoc(it->second);
    Rml::ElementDocument* doc = i.ctx->LoadDocumentFromMemory(Rml::String(rmlText),
                                                              Rml::String(name));
    if (!doc) {
        LEMON_ERROR("ui-subsystem: LoadDocumentFromMemory('%s') 解析失败", name);
        return false;
    }
    auto& entry = i.docs[name];
    entry.source = rmlText;
    entry.doc = doc;
    entry.shown = false;
    return true;
}

bool UiSubsystem::ShowDocument(const char* name, bool show) {
    if (!impl_) return false;
    auto it = impl_->docs.find(name);
    if (it == impl_->docs.end() || !it->second.doc) return false;
    if (show) it->second.doc->Show();
    else it->second.doc->Hide();
    it->second.shown = show;
    return true;
}

bool UiSubsystem::HasDocument(const char* name) const {
    return impl_ && impl_->docs.count(name) > 0;
}

// ----------------------------------------------------------------- 帧 ----
void UiSubsystem::Update() {
    if (impl_ && impl_->ctx) impl_->ctx->Update();
}

void UiSubsystem::Render(rhi::CommandList& cl, uint32_t rtW, uint32_t rtH) {
    if (!impl_ || !impl_->ctx) return;
    Impl& i = *impl_;
    if (rtW != i.ctxW || rtH != i.ctxH) {
        i.ctxW = rtW;
        i.ctxH = rtH;
        i.ctx->SetDimensions(Rml::Vector2i((int)rtW, (int)rtH));
    }
    i.backend->BeginFrame(cl, rtW, rtH);
    i.ctx->Render();
    i.backend->EndFrame();
}

const char* UiSubsystem::LoadedFontFamily() const {
    return impl_ ? impl_->fontFamily.c_str() : "";
}

} // namespace lemon::ui
