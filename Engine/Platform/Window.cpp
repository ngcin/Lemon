#include "Platform/Window.h"

#include <cstdlib>
#include <cstring>
#include <utility>

#include <SDL3/SDL.h>

#include "Core/Log.h"

namespace lemon {

struct Window::Impl {
    SDL_Window* window = nullptr;
    bool resized = false;
    bool keys[512] = {}; // 下标 = SDL_Scancode
    EventObserver observer = nullptr;
    void* observerData = nullptr;
    std::vector<std::string> drops; // SDL drop 文件（M4.6 §5-3；PollEvents 攒、TakeDroppedFiles 排水）
};

std::unique_ptr<Window> Window::Create(const WindowDesc& desc) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        LEMON_ASSERT(false, "SDL_Init(VIDEO) failed: %s", SDL_GetError());
        return nullptr;
    }
    // 自动化/回归跑窗口不抢前台焦点：显示时不激活（SDL3 hint）。smoke 输入是
    // 合成事件注入，不依赖窗口激活；手动使用不设此变量 = 默认行为不变
    if (std::getenv("LEMON_NO_ACTIVATE"))
        SDL_SetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN, "0");
    auto w = std::unique_ptr<Window>(new Window());
    w->m = std::make_unique<Impl>();
    w->m->window = SDL_CreateWindow(desc.title, desc.width, desc.height,
                                    SDL_WINDOW_VULKAN |
                                        (desc.resizable ? SDL_WINDOW_RESIZABLE : 0));
    if (!w->m->window) {
        LEMON_ASSERT(false, "SDL_CreateWindow failed: %s", SDL_GetError());
        return nullptr;
    }
    return w;
}

Window::~Window() {
    if (m && m->window) {
        SDL_DestroyWindow(m->window);
        SDL_Quit();
    }
}

bool Window::PollEvents() {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (m->observer) m->observer((const void*)&ev, m->observerData);
        switch (ev.type) {
            case SDL_EVENT_QUIT:
                return false;
            case SDL_EVENT_WINDOW_RESIZED:
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                m->resized = true;
                break;
            case SDL_EVENT_WINDOW_FOCUS_LOST:
            case SDL_EVENT_WINDOW_MINIMIZED:
                // 失焦/最小化瞬间按住的键不会再收到 KEY_UP（去了新焦点应用），
                // 不清 = 卡键（Play 态角色持续单向移动）
                std::memset(m->keys, 0, sizeof(m->keys));
                break;
            case SDL_EVENT_KEY_DOWN: {
                int sc = (int)ev.key.scancode;
                if (sc >= 0 && sc < 512) m->keys[sc] = true;
                break;
            }
            case SDL_EVENT_KEY_UP: {
                int sc = (int)ev.key.scancode;
                if (sc >= 0 && sc < 512) m->keys[sc] = false;
                break;
            }
            case SDL_EVENT_DROP_FILE: // OS 拖入窗口（路径在事件回调外失效 → 即拷即存）
                // drop.data 归应用所有，SDL3 约定必须 SDL_free（空串也占分配；
                // data 是 const char*，所有权转移需去 const 转 void*）
                if (ev.drop.data) {
                    if (ev.drop.data[0]) m->drops.emplace_back(ev.drop.data);
                    SDL_free((void*)ev.drop.data);
                }
                break;
            default:
                break;
        }
    }
    return true;
}

void Window::SetEventObserver(EventObserver fn, void* userData) {
    m->observer = fn;
    m->observerData = userData;
}

bool Window::TakeResized() {
    bool r = m->resized;
    m->resized = false;
    return r;
}

void Window::RequestResize(int w, int h) { SDL_SetWindowSize(m->window, w, h); }

void Window::SetTitle(const char* title) { SDL_SetWindowTitle(m->window, title); }

std::vector<std::string> Window::TakeDroppedFiles() {
    std::vector<std::string> out = std::move(m->drops);
    m->drops.clear();
    return out;
}

void* Window::NativeHandle() const { return (void*)m->window; }

void Window::GetPixelSize(int& w, int& h) const { SDL_GetWindowSizeInPixels(m->window, &w, &h); }

bool Window::IsKeyDown(Key k) const {
    int sc = (int)k;
    return sc >= 0 && sc < 512 && m->keys[sc];
}

} // namespace lemon
