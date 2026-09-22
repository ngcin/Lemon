// M5 ADR-008 spike — RmlUi 官方 SDL3+Vulkan 后端整体接入验证
// 目的：在 Lemon 技术栈（SDL3 窗口 + Vulkan/MoltenVK + brew FreeType）上验证
// RmlUi 6.3 官方后端能否直接跑通：文档渲染 / 中文字体 / 事件回调三判据。
// 注：spike 刻意用官方裸后端（自带 swapchain）——正式接入（若 spike 过）将把
// RmlUi_Renderer_VK 的思路适配到引擎 RHI（合批/bindless），见 ADR-008。
//
// 自动化验收（可无人值守跑）：stdout 行缓冲；frame 90 合成鼠标事件点按钮
// （坐标链：SDL 点 ×pixelDensity → ctx px ÷dpRatio → dp——窗口创建已乘
// contentScale，两因子相消，故 SDL 事件坐标 = RmlUi dp 坐标）；frame 900
// 自动退出并打印 VERDICT。人工验收：窗口应显示中文标题 + 可点按钮。
#include <cstdio>
#include <cstring>

#include <SDL3/SDL.h>

#include <RmlUi/Core.h>
#include <RmlUi_Backend.h>

namespace {

// 日志转 stdout（spike 可见性；引擎接入时接 LEMON_LOG）
class SpikeSystem final : public Rml::SystemInterface {
public:
    bool LogMessage(Rml::Log::Type type, const Rml::String& message) override {
        static const char* kNames[] = {"always", "error", "assert", "warning",
                                       "info",   "debug", "trace"};
        std::printf("[rmlui][%s] %s\n", kNames[(int)type], message.c_str());
        return true; // 不再走默认输出
    }
};

// 判据③：按钮事件回调（监听器生命周期 RmlUi 托管——detach 时释放）
int g_clickCount = 0;

class ClickLog final : public Rml::EventListener {
public:
    void ProcessEvent(Rml::Event& event) override {
        ++g_clickCount;
        Rml::Element* el = event.GetCurrentElement();
        std::printf("[spike] CLICK '%s'（事件通路 OK）\n",
                    el ? el->GetId().c_str() : "?");
    }
};

// 点击诊断：挂在 body 上——任何到达 RmlUi 的点击都留痕（区分"事件没进来"
// 与"进来了但命中错元素/坐标"）
class AnyClickProbe final : public Rml::EventListener {
public:
    void ProcessEvent(Rml::Event& event) override {
        Rml::Element* target = event.GetTargetElement();
        std::printf("[spike] click-probe target='%s' tag=%s\n",
                    target ? target->GetId().c_str() : "?",
                    target ? target->GetTagName().c_str() : "?");
    }
};

void PushSyntheticClick(float x, float y) {
    SDL_Event ev;
    SDL_zero(ev);
    ev.type = SDL_EVENT_MOUSE_MOTION;
    ev.motion.timestamp = SDL_GetTicksNS();
    ev.motion.x = x; ev.motion.y = y;
    SDL_PushEvent(&ev);
    SDL_zero(ev);
    ev.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    ev.button.timestamp = SDL_GetTicksNS();
    ev.button.button = SDL_BUTTON_LEFT;
    ev.button.clicks = 1;
    ev.button.down = true;
    ev.button.x = x; ev.button.y = y;
    SDL_PushEvent(&ev);
    SDL_zero(ev);
    ev.type = SDL_EVENT_MOUSE_BUTTON_UP;
    ev.button.timestamp = SDL_GetTicksNS();
    ev.button.button = SDL_BUTTON_LEFT;
    ev.button.clicks = 1;
    ev.button.down = false;
    ev.button.x = x; ev.button.y = y;
    SDL_PushEvent(&ev);
}

} // namespace

int main() {
    setvbuf(stdout, nullptr, _IOLBF, 0); // 后台跑时逐行落盘

    if (!Backend::Initialize("lemon spike-04 rmlui (SDL_VK)", 1280, 720, true)) {
        std::printf("[spike] backend init FAIL\n");
        return 1;
    }
    SpikeSystem sys;
    Rml::SetSystemInterface(&sys);
    Rml::SetRenderInterface(Backend::GetRenderInterface());
    if (!Rml::Initialise()) {
        std::printf("[spike] Rml::Initialise FAIL\n");
        return 1;
    }

    Rml::Context* ctx = Rml::CreateContext("main", Rml::Vector2i(1280, 720));
    if (!ctx) {
        std::printf("[spike] CreateContext FAIL\n");
        return 1;
    }

    // 判据②：中文字体。本机（macOS 15）PingFang.ttc 已挪进 cryptex 且不可直读，
    // 按 sans 优先级降级：Hiragino Sans GB → STHeiti Medium → Songti（宋体兜底）。
    // RCSS 侧用家族回退列表（见 data/test.rml）与这里的加载序对应。
    const char* kFontCandidates[] = {
        "/System/Library/Fonts/Hiragino Sans GB.ttc",
        "/System/Library/Fonts/STHeiti Medium.ttc",
        "/System/Library/Fonts/Supplemental/Songti.ttc",
    };
    bool fontOK = false;
    for (const char* path : kFontCandidates)
        if (Rml::LoadFontFace(path)) { fontOK = true; break; }

    // 判据①：文档渲染（data/test.rml：深色底 + 中文标题 + 按钮）
    Rml::ElementDocument* doc = ctx->LoadDocument("data/test.rml");
    if (!doc) {
        std::printf("[spike] LoadDocument FAIL（data/test.rml）\n");
        return 1;
    }
    doc->Show();
    if (Rml::Element* btn = doc->GetElementById("start"))
        btn->AddEventListener(Rml::EventId::Click, new ClickLog());
    if (Rml::Element* body = doc->GetFirstChild())
        body->AddEventListener(Rml::EventId::Click, new AnyClickProbe());

    std::printf("[spike] 初始化 OK（font=%d）——验收：窗口应显示中文标题 + 可点按钮\n",
                fontOK ? 1 : 0);

    // 帧循环（官方样例同构；120 帧打一次心跳防"黑屏假活"）
    constexpr int kClickFrame = 90;   // 布局稳定后合成点击（判据③）
    constexpr int kShotFrame = 600;   // 交换链回读截图（判据①像素级验证）
    constexpr int kMaxFrames = 900;   // ~15s@60fps 自动退出（无人值守）
    bool running = true;
    int frame = 0;
    while (running) {
        running = Backend::ProcessEvents(ctx);
        Backend::BeginFrame();
        ctx->Update();
        ctx->Render();
        Backend::PresentFrame();
        ++frame;
        if (frame == kClickFrame) {
            std::printf("[spike] ctx=%gx%g\n", (double)ctx->GetDimensions().x,
                        (double)ctx->GetDimensions().y);
            auto dump = [](const char* name, Rml::Element* el) {
                if (!el) { std::printf("[spike]   %-6s = null\n", name); return; }
                const Rml::Vector2f tl = el->GetAbsoluteOffset(Rml::BoxArea::Border);
                const Rml::Vector2f sz = el->GetBox().GetSize(Rml::BoxArea::Border);
                std::printf("[spike]   %-6s @(%.0f,%.0f) %gx%g\n", name,
                            tl.x, tl.y, sz.x, sz.y);
            };
            std::printf("[spike] ctx=%gx%g ratio=%g\n", (double)ctx->GetDimensions().x,
                        (double)ctx->GetDimensions().y,
                        (double)ctx->GetDensityIndependentPixelRatio());
            Rml::ElementList h1s;
            doc->GetElementsByTagName(h1s, "h1");
            dump("root", ctx->GetRootElement());
            dump("doc", doc);
            dump("h1", h1s.empty() ? nullptr : h1s[0]);
            if (Rml::Element* btn = doc->GetElementById("start")) {
                // 按钮中心（上下文像素坐标）。InputEventHandler 会把 SDL 点坐标
                // ×pixel_density 送入上下文 → 合成事件要除回密度（spike 实测
                // 密度 2，直接推上下文坐标会放大 2 倍点偏——见改造⑤注释）
                const Rml::Vector2f tl = btn->GetAbsoluteOffset(Rml::BoxArea::Border);
                const Rml::Vector2f sz = btn->GetBox().GetSize(Rml::BoxArea::Border);
                const Rml::Vector2f pos(tl.x + sz.x / 2, tl.y + sz.y / 2);
                const float density = Backend::GetWindowPixelDensity();
                std::printf("[spike] 合成点击 start 按钮 ctx@(%g,%g) size %gx%g "
                            "density=%g sdl@(%g,%g)\n",
                            pos.x, pos.y, sz.x, sz.y, density, pos.x / density,
                            pos.y / density);
                if (Rml::Element* hit = ctx->GetElementAtPoint(pos))
                    std::printf("[spike]   ElementAtPoint -> %s#%s\n",
                                hit->GetTagName().c_str(), hit->GetId().c_str());
                PushSyntheticClick(pos.x / density, pos.y / density);
            } else {
                std::printf("[spike] WARN start 按钮不在 DOM——跳过合成点击\n");
            }
        }
        if (frame == kShotFrame)
            Backend::RequestScreenshot("spike-screenshot.bmp");
        if (frame % 120 == 0)
            std::printf("[spike] running frame=%d clicks=%d\n", frame, g_clickCount);
        if (frame >= kMaxFrames) {
            std::printf("[spike] 到达帧上限 %d，自动退出\n", kMaxFrames);
            Backend::RequestExit();
        }
    }

    const bool docOK = frame > 100; // 持续渲染超过 100 帧且无致命错误
    const bool clickOK = g_clickCount > 0;
    std::printf("[spike] VERDICT doc=%s font=%s click=%d(frames=%d) => %s\n",
                docOK ? "OK" : "FAIL", fontOK ? "OK" : "FAIL", g_clickCount, frame,
                (docOK && fontOK && clickOK) ? "PASS" : "FAIL");
    Rml::Shutdown();
    Backend::Shutdown();
    return 0;
}
