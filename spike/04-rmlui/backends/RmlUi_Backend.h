#pragma once

#include <RmlUi/Core/Input.h>
#include <RmlUi/Core/RenderInterface.h>
#include <RmlUi/Core/SystemInterface.h>
#include <RmlUi/Core/Types.h>

struct SDL_Window;
namespace Rml { class TextInputHandler; }

using KeyDownCallback = bool (*)(Rml::Context* context, Rml::Input::KeyIdentifier key, int key_modifier, float native_dp_ratio, bool priority);

/**
    This interface serves as a basic abstraction over the various backends included with RmlUi. It is mainly intended as an example to get something
    simple up and running, and provides just enough functionality for the included samples.

    This interface may be used directly for simple applications and testing. However, for anything more advanced we recommend to use the backend as a
    starting point and copy relevant parts into the main loop of your application. On the other hand, the underlying platform and renderer used by the
    backend are intended to be re-usable as is.
 */
namespace Backend {

// Initializes the backend, including the custom system and render interfaces, and opens a window for rendering the RmlUi context.
bool Initialize(const char* window_name, int width, int height, bool allow_resize);
// Closes the window and release all resources owned by the backend, including the system and render interfaces.
void Shutdown();

// Returns a pointer to the custom system interface which should be provided to RmlUi.
Rml::SystemInterface* GetSystemInterface();
// Returns a pointer to the custom render interface which should be provided to RmlUi.
Rml::RenderInterface* GetRenderInterface();

// Polls and processes events from the current platform, and applies any relevant events to the provided RmlUi context and the key down callback.
// @return False to indicate that the application should be closed.
bool ProcessEvents(Rml::Context* context, KeyDownCallback key_down_callback = nullptr, bool power_save = false);
// Request application closure during the next event processing call.
void RequestExit();

// [Lemon spike 改造⑤] Window pixel density (pixels per point) — synthetic input
// coordinates must be divided by this (SDL point coords get multiplied by it
// before entering the RmlUi context).
float GetWindowPixelDensity();

// [Lemon spike 改造⑦] Read the next presented swapchain image to a BMP file
// (spike acceptance only — pixel-level verification without screen-recording
// permission; delete when porting onto the engine RHI).
void RequestScreenshot(const Rml::String& path);

// [Lemon spike 改造⑬] T7 文本输入微 spike：暴露窗口句柄——自定义 SystemInterface
// 需继承 SystemInterface_SDL（ActivateKeyboard → SDL_SetTextInputArea/StartTextInput
// 的候选窗贴光标通路），其构造要求窗口。
SDL_Window* GetWindow();

// [Lemon spike 改造⑭] T7 文本输入微 spike：暴露 IME 预编辑 handler——事件循环
// （ProcessEvents 的 SDL_EVENT_TEXT_EDITING 分支）已调其 HandleEdit，但从未注册
// 给 RmlUi 上下文；调用方经 Rml::CreateContext 第 4 参接上。
Rml::TextInputHandler* GetTextInputHandler();

// Prepares the render state to accept rendering commands from RmlUi, call before rendering the RmlUi context.
void BeginFrame();
// Presents the rendered frame to the screen, call after rendering the RmlUi context.
void PresentFrame();

} // namespace Backend
