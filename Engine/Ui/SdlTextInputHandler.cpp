// Lemon 引擎 — SDL IME 预编辑 handler 实现（M6b 批③c）
// RmlUi 类型只准出现在本 .cpp。逻辑 = spike-04 T7 验证过的官方 SDL 平台层
// TextInputMethodEditor_SDL（backends/RmlUi_Platform_SDL.cpp:524-569，2026-09-28
// 三判据实证：中文提交零乱码/候选窗贴光标/事件不串）。
#include "Ui/SdlTextInputHandler.h"

#include <RmlUi/Core.h>
#include <RmlUi/Core/StringUtilities.h>
#include <RmlUi/Core/TextInputHandler.h>
#include <RmlUi/Core/TextInputContext.h>

namespace lemon::ui {

namespace {

// spike T7 同款：预编辑串经 SetText 原位替换选中区 + SetCompositionRange 标记
// 组合中；空串编辑事件 = 提交边界（CommitComposition），随后 SDL 另发 TEXT_INPUT
// 提交串（ProcessTextInput 路径）——两路叠加值恰一份。
class SdlImeHandler final : public Rml::TextInputHandler {
public:
    void OnActivate(Rml::TextInputContext* input_context) override { ctx_ = input_context; }
    void OnDeactivate(Rml::TextInputContext* input_context) override {
        if (ctx_ == input_context) ctx_ = nullptr;
    }
    void OnDestroy(Rml::TextInputContext* input_context) override {
        if (ctx_ == input_context) ctx_ = nullptr;
    }

    void HandleEdit(const char* utf8, int evStart, int evLength) {
        if (ctx_ == nullptr) return;

        auto str = Rml::String(utf8);
        auto length = static_cast<int>(Rml::StringUtilities::LengthUTF8(str));

        bool composing = start_ != end_;
        if (!composing) ctx_->GetSelectionRange(start_, end_);

        if (composing || length > 0) ctx_->SetText(str, start_, end_);

        end_ = start_ + length;
        ctx_->SetCompositionRange(start_, end_);

        if (length > 0 && evStart >= 0 && evLength >= 0)
            ctx_->SetSelectionRange(start_ + evStart, start_ + evStart + evLength);
        else if (composing)
            ctx_->SetCursorPosition(end_);

        // 提交边界：SDL 先发空串编辑事件、再发 TEXT_INPUT 提交串
        if (composing && length == 0) ctx_->CommitComposition(Rml::StringView());
    }

private:
    Rml::TextInputContext* ctx_ = nullptr;
    int start_ = 0, end_ = 0;
};

} // namespace

struct SdlTextInputHandler::Impl {
    SdlImeHandler handler;
};

SdlTextInputHandler::SdlTextInputHandler() = default;
SdlTextInputHandler::~SdlTextInputHandler() = default;

void* SdlTextInputHandler::HandlerPtr() { return &impl_->handler; }

void SdlTextInputHandler::HandleTextEditing(const char* utf8, int start, int length) {
    if (impl_) impl_->handler.HandleEdit(utf8, start, length);
}

} // namespace lemon::ui
