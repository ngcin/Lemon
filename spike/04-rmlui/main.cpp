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
//
// ── T7 文本输入微 spike（M6a 批③a，ADR-014 D4，③c 开工门槛）──────────────
// 判据：① 中文提交零乱码（IME 真实事件序 TEXT_EDITING→TEXT_INPUT 注入，
//        GetValue() UTF-8 逐字节比对）；② IME 候选窗贴光标（<input> 聚焦 →
//        RmlUi WidgetTextInput 自动调 SystemInterface::ActivateKeyboard(光标
//        绝对坐标, 行高) → SystemInterface_SDL 转 SDL_SetTextInputArea——断言
//        光标坐标落在输入框内 + 聚焦/失焦激活对）；③ 事件不串（KEY_DOWN 不
//        直接插字 / RETURN 的 '\n' 被单行 input 吞 / 方向键只移光标 / 退格恰好
//        删一个字 / 点按钮失焦出 change）。结论决定 D4：过 → 提交制文本输入
//        直接吃 RmlUi 自带 <input>；不过 → M8 自定义元素兜底。
#include <cstdio>
#include <cstring>

#include <SDL3/SDL.h>

#include <RmlUi/Core.h>
#include <RmlUi/Core/StringUtilities.h>
#include <RmlUi_Backend.h>
#include <RmlUi_Platform_SDL.h>

namespace {

// 日志转 stdout（spike 可见性；引擎接入时接 LEMON_LOG）。
// T7：继承 SystemInterface_SDL——ActivateKeyboard/DeactivateKeyboard 落 SDL
// （SetTextInputArea/StartTextInput/StopTextInput = 候选窗贴光标通路），
// 原裸 SystemInterface 的这两个是 no-op。
int g_activateCount = 0;
int g_deactivateCount = 0;
Rml::Vector2f g_lastCaret(0, 0);
float g_lastLineHeight = 0.0f;

class SpikeSystem final : public SystemInterface_SDL {
public:
    SpikeSystem(SDL_Window* window) : SystemInterface_SDL(window) {}

    bool LogMessage(Rml::Log::Type type, const Rml::String& message) override {
        static const char* kNames[] = {"always", "error", "assert", "warning",
                                       "info",   "debug", "trace"};
        std::printf("[rmlui][%s] %s\n", kNames[(int)type], message.c_str());
        return true; // 不再走默认输出
    }

    void ActivateKeyboard(Rml::Vector2f caret_position, float line_height) override {
        ++g_activateCount;
        g_lastCaret = caret_position;
        g_lastLineHeight = line_height;
        std::printf("[spike] ActivateKeyboard caret@(%g,%g) line=%g（→ SDL_SetTextInputArea 候选窗锚点）\n",
                    (double)caret_position.x, (double)caret_position.y, (double)line_height);
        SystemInterface_SDL::ActivateKeyboard(caret_position, line_height);
    }

    void DeactivateKeyboard() override {
        ++g_deactivateCount;
        std::printf("[spike] DeactivateKeyboard（→ SDL_StopTextInput）\n");
        SystemInterface_SDL::DeactivateKeyboard();
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

// T7 判据③：#name 失焦/回车提交时的 change 事件（D4 提交制的事件面）
int g_changeCount = 0;

class ChangeLog final : public Rml::EventListener {
public:
    void ProcessEvent(Rml::Event& event) override {
        ++g_changeCount;
        Rml::Element* el = event.GetCurrentElement();
        Rml::String v;
        if (el) v = static_cast<Rml::ElementFormControl*>(el)->GetValue();
        std::printf("[spike] CHANGE #name value='%s'（提交语义 OK）\n", v.c_str());
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

// T7：合成按键（down/up 成对；text 字符只能走 TEXT_INPUT 通道——KEY_DOWN
// 不带字是 SDL 的事件纪律，判据③正是要证 RmlUi 同守此纪律）
void PushKey(SDL_Keycode key) {
    SDL_Event ev;
    SDL_zero(ev);
    ev.type = SDL_EVENT_KEY_DOWN;
    ev.key.timestamp = SDL_GetTicksNS();
    ev.key.key = key;
    ev.key.down = true;
    ev.key.repeat = false;
    SDL_PushEvent(&ev);
    SDL_zero(ev);
    ev.type = SDL_EVENT_KEY_UP;
    ev.key.timestamp = SDL_GetTicksNS();
    ev.key.key = key;
    ev.key.down = false;
    ev.key.repeat = false;
    SDL_PushEvent(&ev);
}

// T7：合成文本输入（SDL3 text 成员是 const char*——传字面量静态存储，
// 事件下轮 pump 才消费，生命周期安全）
void PushTextInput(const char* utf8) {
    SDL_Event ev;
    SDL_zero(ev);
    ev.type = SDL_EVENT_TEXT_INPUT;
    ev.text.timestamp = SDL_GetTicksNS();
    ev.text.text = utf8;
    SDL_PushEvent(&ev);
}

// T7：合成 IME 预编辑事件（真实输入法的事件序：EDITING(预编辑串) →
// EDITING(空串=结束) → INPUT(提交串)；TextInputMethodEditor_SDL::HandleEdit
// 在后端事件循环里消费）
void PushTextEditing(const char* utf8, int start, int length) {
    SDL_Event ev;
    SDL_zero(ev);
    ev.type = SDL_EVENT_TEXT_EDITING;
    ev.edit.timestamp = SDL_GetTicksNS();
    ev.edit.text = utf8;
    ev.edit.start = start;
    ev.edit.length = length;
    SDL_PushEvent(&ev);
}

Rml::String InputValue(Rml::ElementDocument* doc) {
    Rml::Element* el = doc->GetElementById("name");
    if (!el) return "<null>";
    return static_cast<Rml::ElementFormControl*>(el)->GetValue();
}

// T7 断言助手：UTF-8 逐字节比对（判据①"零乱码"的机器化——乱码必然字节不等）
bool CheckInputValue(Rml::ElementDocument* doc, const char* expect, const char* what) {
    const Rml::String v = InputValue(doc);
    const bool ok = (v == expect);
    std::printf("[spike] CHECK %-12s value='%s'(%d字) expect='%s' => %s\n",
                what, v.c_str(), (int)Rml::StringUtilities::LengthUTF8(v), expect,
                ok ? "OK" : "FAIL");
    return ok;
}

} // namespace

int main() {
    setvbuf(stdout, nullptr, _IOLBF, 0); // 后台跑时逐行落盘

    if (!Backend::Initialize("lemon spike-04 rmlui (SDL_VK)", 1280, 720, true)) {
        std::printf("[spike] backend init FAIL\n");
        return 1;
    }
    SpikeSystem sys(Backend::GetWindow());
    Rml::SetSystemInterface(&sys);
    Rml::SetRenderInterface(Backend::GetRenderInterface());
    if (!Rml::Initialise()) {
        std::printf("[spike] Rml::Initialise FAIL\n");
        return 1;
    }

    // T7：IME 预编辑 handler 注册（后端事件循环已喂 TEXT_EDITING，注册后
    // OnActivate/OnDeactivate 才接得上；handler 生命周期随 BackendData，
    // Rml::Shutdown 先于 Backend::Shutdown ✓）
    Rml::Context* ctx = Rml::CreateContext("main", Rml::Vector2i(1280, 720), nullptr,
                                           Backend::GetTextInputHandler());
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

    // 判据①：文档渲染（data/test.rml：深色底 + 中文标题 + 按钮 + T7 输入框）
    Rml::ElementDocument* doc = ctx->LoadDocument("data/test.rml");
    if (!doc) {
        std::printf("[spike] LoadDocument FAIL（data/test.rml）\n");
        return 1;
    }
    doc->Show();
    if (Rml::Element* btn = doc->GetElementById("start"))
        btn->AddEventListener(Rml::EventId::Click, new ClickLog());
    if (Rml::Element* name = doc->GetElementById("name"))
        name->AddEventListener(Rml::EventId::Change, new ChangeLog());
    if (Rml::Element* body = doc->GetFirstChild())
        body->AddEventListener(Rml::EventId::Click, new AnyClickProbe());

    std::printf("[spike] 初始化 OK（font=%d）——验收：窗口应显示中文标题 + 输入框 + 可点按钮\n",
                fontOK ? 1 : 0);

    // 帧循环（官方样例同构；120 帧打一次心跳防"黑屏假活"）。
    // T7 时序：事件注入在 frame N 的帧尾 hook 推入队列，下一轮 ProcessEvents
    // 消费后同步落进 RmlUi 状态 → frame N+k 的断言 hook 读到终态。
    constexpr int kClickFrame = 90;       // 布局稳定后合成点击按钮（判据③）
    constexpr int kShotFrame = 600;       // 交换链回读截图（判据①像素级验证）
    constexpr int kMaxFrames = 900;       // ~15s@60fps 自动退出（无人值守）
    constexpr int kInputClickFrame = 150; // T7：点输入框聚焦
    constexpr int kFocusCheckFrame = 160; // T7：断言焦点在 #name
    constexpr int kImeCommitFrame = 180;  // T7：IME 真实事件序提交"柠檬"
    constexpr int kPlainCommitFrame = 200;// T7：纯 TEXT_INPUT 提交"骑士"
    constexpr int kCheckCommitFrame = 210;// T7：值 == "柠檬骑士"
    constexpr int kKeyNFrame = 220;       // T7：裸 KEY_DOWN('N')（无 TEXT_INPUT）
    constexpr int kCheckKeyNFrame = 230;  // T7：不得插字
    constexpr int kBackspaceFrame = 240;  // T7：退格删"士"
    constexpr int kCheckBackspaceFrame = 250;
    constexpr int kReturnFrame = 260;     // T7：RETURN（后端转 '\n' 文本输入）
    constexpr int kCheckReturnFrame = 270;// T7：单行 input 不得出换行
    constexpr int kArrowFrame = 280;      // T7：方向键只移光标
    constexpr int kCheckArrowFrame = 290;
    constexpr int kBlurFrame = 300;       // T7：点 start 按钮 → 失焦出 change
    constexpr int kCheckBlurFrame = 310;  // T7：change + DeactivateKeyboard 成对
    bool focusOK = false, commitOK = false, keyNoInsertOK = false;
    bool backspaceOK = false, returnOK = false, arrowOK = false, blurOK = false;
    bool imeActivateOK = false, imeCaretOK = false, imeDeactivateOK = false;
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
        if (frame == kInputClickFrame) {
            if (Rml::Element* el = doc->GetElementById("name")) {
                const Rml::Vector2f tl = el->GetAbsoluteOffset(Rml::BoxArea::Border);
                const Rml::Vector2f sz = el->GetBox().GetSize(Rml::BoxArea::Border);
                const Rml::Vector2f pos(tl.x + sz.x / 2, tl.y + sz.y / 2);
                const float density = Backend::GetWindowPixelDensity();
                std::printf("[spike] T7 合成点击 #name 输入框 ctx@(%g,%g) %gx%g "
                            "density=%g\n", pos.x, pos.y, sz.x, sz.y, density);
                PushSyntheticClick(pos.x / density, pos.y / density);
            } else {
                std::printf("[spike] T7 WARN #name 不在 DOM\n");
            }
        }
        if (frame == kFocusCheckFrame) {
            Rml::Element* focus = ctx->GetFocusElement();
            const Rml::String id = focus ? focus->GetId() : Rml::String("<null>");
            focusOK = (id == "name");
            std::printf("[spike] T7 焦点检查 focus='%s' => %s\n", id.c_str(),
                        focusOK ? "OK" : "FAIL");
        }
        if (frame == kImeCommitFrame) {
            // 真实 IME 提交事件序：预编辑进（柠檬）→ 预编辑清（空串=提交边界）→
            // 提交文本（TEXT_INPUT）。HandleEdit 走 SetText/CommitComposition，
            // TEXT_INPUT 走 ProcessTextInput——两路叠加后值恰为一份"柠檬"。
            std::printf("[spike] T7 注入 IME 提交事件序（柠檬）\n");
            PushTextEditing("柠檬", 0, 6);
            PushTextEditing("", 0, 0);
            PushTextInput("柠檬");
        }
        if (frame == kPlainCommitFrame) {
            std::printf("[spike] T7 注入纯 TEXT_INPUT（骑士）\n");
            PushTextInput("骑士");
        }
        if (frame == kCheckCommitFrame)
            commitOK = CheckInputValue(doc, "柠檬骑士", "commit");
        if (frame == kKeyNFrame) {
            std::printf("[spike] T7 注入裸 KEY_DOWN N（无 TEXT_INPUT——不得插字）\n");
            PushKey(SDLK_N);
        }
        if (frame == kCheckKeyNFrame)
            keyNoInsertOK = CheckInputValue(doc, "柠檬骑士", "key-noins");
        if (frame == kBackspaceFrame) {
            std::printf("[spike] T7 注入 BACKSPACE（应恰删一字）\n");
            PushKey(SDLK_BACKSPACE);
        }
        if (frame == kCheckBackspaceFrame)
            backspaceOK = CheckInputValue(doc, "柠檬骑", "backspace");
        if (frame == kReturnFrame) {
            std::printf("[spike] T7 注入 RETURN（平台层转 ProcessTextInput('\\n')，"
                        "单行 input 应吞掉）\n");
            PushKey(SDLK_RETURN);
        }
        if (frame == kCheckReturnFrame)
            returnOK = CheckInputValue(doc, "柠檬骑", "return-nl");
        if (frame == kArrowFrame) {
            std::printf("[spike] T7 注入 ←/→（只移光标不动值）\n");
            PushKey(SDLK_LEFT);
            PushKey(SDLK_RIGHT);
        }
        if (frame == kCheckArrowFrame)
            arrowOK = CheckInputValue(doc, "柠檬骑", "arrow");
        if (frame == kBlurFrame) {
            if (Rml::Element* btn = doc->GetElementById("start")) {
                const Rml::Vector2f tl = btn->GetAbsoluteOffset(Rml::BoxArea::Border);
                const Rml::Vector2f sz = btn->GetBox().GetSize(Rml::BoxArea::Border);
                const float density = Backend::GetWindowPixelDensity();
                std::printf("[spike] T7 合成点击 start（#name 失焦 → change + "
                            "DeactivateKeyboard）\n");
                PushSyntheticClick((tl.x + sz.x / 2) / density,
                                   (tl.y + sz.y / 2) / density);
            }
        }
        if (frame == kCheckBlurFrame) {
            blurOK = (g_changeCount >= 1);
            std::printf("[spike] T7 失焦检查 change=%d deactivate=%d => %s\n",
                        g_changeCount, g_deactivateCount, blurOK ? "OK" : "FAIL");
            // 判据②收口：激活对（focus 进）+ 光标锚在输入框内 + 失活对（blur 出）
            imeActivateOK = (g_activateCount >= 1);
            imeDeactivateOK = (g_deactivateCount >= 1);
            if (Rml::Element* el = doc->GetElementById("name")) {
                const Rml::Vector2f tl = el->GetAbsoluteOffset(Rml::BoxArea::Border);
                const Rml::Vector2f sz = el->GetBox().GetSize(Rml::BoxArea::Border);
                imeCaretOK = g_lastCaret.x >= tl.x - 2 && g_lastCaret.x <= tl.x + sz.x + 2 &&
                             g_lastCaret.y >= tl.y - g_lastLineHeight &&
                             g_lastCaret.y <= tl.y + sz.y + g_lastLineHeight;
                std::printf("[spike] T7 候选窗锚点检查 caret@(%g,%g) 框@(%.0f,%.0f "
                            "%gx%g) => %s\n", (double)g_lastCaret.x,
                            (double)g_lastCaret.y, tl.x, tl.y, sz.x, sz.y,
                            imeCaretOK ? "OK" : "FAIL");
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
    const bool textOK = focusOK && commitOK;      // 判据①：中文提交零乱码
    const bool imeOK = imeActivateOK && imeCaretOK && imeDeactivateOK; // 判据②
    const bool routeOK = keyNoInsertOK && backspaceOK && returnOK && arrowOK && blurOK; // 判据③
    const bool pass = docOK && fontOK && clickOK && textOK && imeOK && routeOK;
    std::printf("[spike] T7 终值 value='%s'\n", InputValue(doc).c_str());
    std::printf("[spike] VERDICT doc=%s font=%s click=%d text=%s(ime=%s route=%s) "
                "(frames=%d) => %s\n",
                docOK ? "OK" : "FAIL", fontOK ? "OK" : "FAIL", g_clickCount,
                textOK ? "OK" : "FAIL", imeOK ? "OK" : "FAIL",
                routeOK ? "OK" : "FAIL", frame, pass ? "PASS" : "FAIL");
    Rml::Shutdown();
    Backend::Shutdown();
    return pass ? 0 : 1;
}
