// Lemon 引擎 — SDL IME 预编辑 handler（M6b 批③c，ADR-014 D4/M7）
// Rml::TextInputHandler 派生：SDL_TEXT_EDITING（预编辑串）→ TextInputContext 的
// SetText/SetCompositionRange/CommitComposition——spike-04 T7 同款逻辑的引擎落位
// （spike backends/RmlUi_Platform_SDL.cpp TextInputMethodEditor_SDL，2026-09-28 实证）。
// 头文件零 RmlUi 类型（Impl 藏 .cpp）。
#pragma once

#include <memory>

namespace lemon::ui {

class SdlTextInputHandler {
public:
    SdlTextInputHandler(); // 定义在 cpp（Pimpl 五件套纪律：声明在头、定义在 cpp）
    ~SdlTextInputHandler();
    SdlTextInputHandler(const SdlTextInputHandler&) = delete;
    SdlTextInputHandler& operator=(const SdlTextInputHandler&) = delete;

    /// Rml::TextInputHandler*（供 Rml::CreateContext 第 4 参注册；生命周期随本对象，
    /// 须活到 Rml::Shutdown——UiSubsystem::Impl 成员序保证）
    void* HandlerPtr();

    /// SDL_TextEditing 事件消费（EditorApp 的 SDL tap 转发；utf8 = 预编辑串）
    void HandleTextEditing(const char* utf8, int start, int length);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace lemon::ui
