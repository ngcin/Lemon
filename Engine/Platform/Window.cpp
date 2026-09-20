#include "Platform/Window.h"

#include <cstring>

#include <SDL3/SDL.h>

#include "Core/Log.h"

namespace lemon {

struct Window::Impl {
    SDL_Window* window = nullptr;
    bool resized = false;
    bool keys[512] = {}; // 下标 = SDL_Scancode
    EventObserver observer = nullptr;
    void* observerData = nullptr;
};

std::unique_ptr<Window> Window::Create(const WindowDesc& desc) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        LEMON_ASSERT(false, "SDL_Init(VIDEO) failed: %s", SDL_GetError());
        return nullptr;
    }
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

void* Window::NativeHandle() const { return (void*)m->window; }

void Window::GetPixelSize(int& w, int& h) const { SDL_GetWindowSizeInPixels(m->window, &w, &h); }

bool Window::IsKeyDown(Key k) const {
    int sc = (int)k;
    return sc >= 0 && sc < 512 && m->keys[sc];
}

} // namespace lemon
