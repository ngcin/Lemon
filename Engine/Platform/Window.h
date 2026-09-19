// Lemon 引擎 — 平台窗口层（SDL3 封装，Pimpl 隐藏 SDL 类型）
// M1 作用域：窗口 + 事件泵 + 按键状态 + resize 标志；输入抽象在 M2 扩展。
#pragma once

#include <memory>

namespace lemon {

struct WindowDesc {
    const char* title = "Lemon";
    int width = 1280, height = 720;
    bool resizable = true;
};

// 与 SDL_Scancode 数值一致（HID 用法页，跨版本稳定）；只列引擎用到的
enum class Key : int {
    A = 4, D = 7, F = 9, L = 15, M = 16, P = 19, S = 22, V = 25,
    Return = 40, Escape = 41, Space = 44,
    F1 = 58, F2 = 59, F3 = 60,
    Right = 79, Left = 80, Down = 81, Up = 82,
};

class Window {
public:
    static std::unique_ptr<Window> Create(const WindowDesc& desc);
    ~Window();

    /// 原始事件观察者（const void* = SDL_Event*）：Pump 循环内每事件回调一次，
    /// 只读不消费（键状态跟踪不受影响）。编辑器 UI 层（IME 文本输入等）专用。
    using EventObserver = void (*)(const void* sdlEvent, void* userData);
    void SetEventObserver(EventObserver fn, void* userData = nullptr);

    /// 泵事件；返回 false = 收到退出请求（窗口关闭）
    bool PollEvents();
    /// 读取并清除"需要重建交换链"标志（resize/像素尺寸变化）
    bool TakeResized();
    /// 程序化改窗口尺寸（测试用：压测 resize 重建路径）
    void RequestResize(int w, int h);
    void* NativeHandle() const;
    void GetPixelSize(int& w, int& h) const;
    bool IsKeyDown(Key k) const;

private:
    Window() = default;
    struct Impl;
    std::unique_ptr<Impl> m;
};

} // namespace lemon
