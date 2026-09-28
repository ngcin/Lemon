// Lemon 引擎 — 游戏 UI 子系统实现（M6a 批③a/③b，ADR-014）
// RmlUi 类型只准出现在本 .cpp（头文件零泄漏，同 Vulkan 纪律形状）。
#include "Ui/UiSubsystem.h"

#include <chrono>
#include <cstring>
#include <filesystem>
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

    // 批③b：POSIX 路径语义。上游默认把前导 '/' 剥掉按 cwd 解（RmlUi 的"根 = 数据
    // 根"约定）——本引擎以绝对路径装载文档，必须直通绝对路径；相对路径按文档目录
    // 拼接 + lexically_normal 吃 ".."。<link href> 与贴图 src 两路均经此口
    // （XMLNodeHandlerHead / RenderManager 摸底 2026-09-28）。
    void JoinPath(Rml::String& translated_path, const Rml::String& document_path,
                  const Rml::String& path) override {
        namespace fs = std::filesystem;
        if (!path.empty() && (path[0] == '/' || path.find(':') != Rml::String::npos)) {
            translated_path = path; // 绝对（POSIX / Windows 盘符）直通
            return;
        }
        if (document_path.empty()) {
            translated_path = path;
            return;
        }
        const fs::path joined = fs::path(document_path.c_str()).parent_path() / path.c_str();
        translated_path = joined.lexically_normal().generic_string();
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
    bool reloadDocsNextUpdate = false; // 设备丢失 → 延迟到 Update 重载（见 Init 注记）

    struct Doc {
        std::string sourceText;         // 内存底稿（③a；两者互斥，file 优先）
        std::string sourcePath;         // 文件底稿（③b 资产通道；重读盘即热重载）
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

    // 底稿 → 文档实例（file 优先；均空 = 坏条目防御返回 nullptr）。设备丢失与
    // 热重载共用的唯一重载路径——shown 态由调用方恢复。
    Rml::ElementDocument* ReloadDoc(Doc& d, const std::string& name) {
        UnloadDoc(d);
        Rml::ElementDocument* doc = nullptr;
        if (!d.sourcePath.empty()) doc = ctx->LoadDocument(Rml::String(d.sourcePath));
        else doc = ctx->LoadDocumentFromMemory(Rml::String(d.sourceText), Rml::String(name));
        d.doc = doc;
        return doc;
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
    // （文档重载 → RmlUi 重编译几何——池是新的，旧几何句柄已随设备死）。
    // 批③b 修正：重载延迟到下一帧 Update——asset-gpu（图集页重建）的回调注册在
    // 本层之后（OpenProjectPipeline 时刻），立即重载会让贴图桥解析到未重建的死纹理。
    i.recreateCb = device.AddRecreateCallback("ui-subsystem", [this](rhi::Device&) {
        Rml::ReleaseFontResources();
        impl_->reloadDocsNextUpdate = true;
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

// ---------------------------------------------------------------- 字体 ----
bool UiSubsystem::LoadFontFace(const char* absPath, const char* family, bool fallback) {
    if (!impl_) return false;
    if (!Rml::LoadFontFace(Rml::String(absPath), fallback)) {
        LEMON_WARN("ui-subsystem: 字体装载失败（fallback=%d）：%s", (int)fallback, absPath);
        return false;
    }
    if (fallback && family && family[0]) {
        impl_->fontFamily = family; // 引擎正字/最近装载的兜底字（冒烟探针读它）
    }
    return true;
}

void UiSubsystem::SetTextureResolver(UiTextureResolver fn) {
    if (impl_) impl_->backend->SetTextureResolver(std::move(fn));
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
    entry.sourceText = rmlText;
    entry.sourcePath.clear(); // 底稿形态互斥（file → memory 覆盖）
    entry.doc = doc;
    entry.shown = false;
    return true;
}

bool UiSubsystem::LoadDocumentFromFile(const char* name, const char* absPath) {
    if (!impl_ || !impl_->ctx) return false;
    Impl& i = *impl_;
    auto it = i.docs.find(name);
    if (it != i.docs.end()) i.UnloadDoc(it->second);
    // RmlUi 默认 FileInterface（fopen 绝对路径）；文档 source URL = absPath →
    // 相对 href/src 经 JoinPath 按文档目录解析（打包期自定义 IO 归 M8）
    Rml::ElementDocument* doc = i.ctx->LoadDocument(Rml::String(absPath));
    if (!doc) {
        LEMON_ERROR("ui-subsystem: LoadDocument('%s') 解析/读盘失败", absPath);
        return false;
    }
    auto& entry = i.docs[name];
    entry.sourceText.clear();
    entry.sourcePath = absPath;
    entry.doc = doc;
    entry.shown = false;
    return true;
}

bool UiSubsystem::ReloadDocument(const char* name) {
    if (!impl_) return false;
    auto it = impl_->docs.find(name);
    if (it == impl_->docs.end()) return false;
    // <link> 样式表按路径缓存（Factory）——重载前先清，否则新文档仍配旧样式
    Rml::Factory::ClearStyleSheetCache();
    const bool shown = it->second.shown;
    Rml::ElementDocument* doc = impl_->ReloadDoc(it->second, name);
    if (doc && shown) doc->Show();
    it->second.shown = shown;
    LEMON_LOG("ui-subsystem: 文档热重载 %s（%s）", name,
              it->second.sourcePath.empty() ? "内存底稿" : it->second.sourcePath.c_str());
    return doc != nullptr;
}

void UiSubsystem::ReloadStyleSheets() {
    if (!impl_) return;
    // 逐文档 ReloadStyleSheet：内部清缓存 + 重解析文档头样式并原位替换——DOM 与
    // 元素状态保持（对齐 ADR-014 M2 "DocumentReloaded → C# 重灌" 的轻量前半段）
    for (auto& [name, d] : impl_->docs) {
        if (!d.doc) continue;
        d.doc->ReloadStyleSheet();
    }
    LEMON_LOG("ui-subsystem: 样式热重载（%zu 文档，DOM/状态保持）", impl_->docs.size());
}

void UiSubsystem::ReloadAllDocuments() {
    if (!impl_) return;
    for (auto& [name, d] : impl_->docs) {
        const bool shown = d.shown;
        Rml::ElementDocument* doc = impl_->ReloadDoc(d, name);
        if (doc && shown) doc->Show();
        d.shown = shown;
    }
}

bool UiSubsystem::UnloadDocument(const char* name) {
    if (!impl_) return false;
    auto it = impl_->docs.find(name);
    if (it == impl_->docs.end()) return false;
    impl_->UnloadDoc(it->second);
    impl_->docs.erase(it);
    return true;
}

void UiSubsystem::UnloadAllDocuments() {
    if (!impl_) return;
    for (auto& [name, d] : impl_->docs) impl_->UnloadDoc(d);
    impl_->docs.clear();
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
    if (!impl_ || !impl_->ctx) return;
    if (impl_->reloadDocsNextUpdate) { // 设备丢失翌帧：图集/字体均已重建（见 Init 注记）
        impl_->reloadDocsNextUpdate = false;
        ReloadAllDocuments();
    }
    impl_->ctx->Update();
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
