// Lemon 编辑器 — 面板注册中心（编译期注册表唯一登记点；新增面板在此追加）
#include <memory>

#include "imgui.h"

#include "App/EditorApp.h"
#include "Core/Log.h"
#include "EditorContext.h"
#include "Panels/BuiltInPanels.h"
#include "Panels/Panel.h"

namespace lemon::editor {
namespace {

// -------------------------------------------------------------- Console ----
// 实装：引擎日志环 + 分级过滤/清空/自动滚动（M4-Editor-Plan §2.2；M4.3 补脚本异常源标记）
class ConsolePanel final : public IEditorPanel {
public:
    const char* Name() const override { return "Console"; }
    void OnGui(EditorApp& app) override {
        if (!ImGui::Begin(Name(), nullptr, ImGuiWindowFlags_NoCollapse)) {
            ImGui::End();
            return;
        }
        if (ImGui::Button("Clear")) app.Log().Clear();
        ImGui::SameLine();
        ImGui::CheckboxFlags("Info", &filter_, 1u << (int)LogLevel::Info);
        ImGui::SameLine();
        ImGui::CheckboxFlags("Warn", &filter_, 1u << (int)LogLevel::Warn);
        ImGui::SameLine();
        ImGui::CheckboxFlags("Error", &filter_, 1u << (int)LogLevel::Error);
        ImGui::SameLine();
        ImGui::Checkbox("Auto-scroll", &autoScroll_);
        ImGui::Separator();

        std::vector<EditorLogLine> snap = app.Log().Snapshot();
        ImGui::BeginChild("lines", ImVec2(0, 0), ImGuiChildFlags_None,
                          ImGuiWindowFlags_HorizontalScrollbar);
        for (const auto& l : snap) {
            if (!(filter_ & (1u << (int)l.level))) continue;
            ImVec4 c = l.level == LogLevel::Error
                           ? ImVec4(0.95f, 0.35f, 0.35f, 1.0f)
                           : l.level == LogLevel::Warn
                                 ? ImVec4(0.95f, 0.80f, 0.35f, 1.0f)
                                 : ImVec4(0.80f, 0.85f, 0.80f, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, c);
            ImGui::TextUnformatted(l.text.c_str());
            ImGui::PopStyleColor();
        }
        if (autoScroll_ && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f)
            ImGui::SetScrollHereY(1.0f);
        ImGui::EndChild();
        ImGui::End();
    }

private:
    uint32_t filter_ = 0b111;
    bool autoScroll_ = true;
};

// ------------------------------------------------------------- 占位面板 ----
// Scene/Game → M4.2；Assets → M4.4。窗口名是默认布局 DockBuilder 的落位键，先行占位。
class PlaceholderPanel final : public IEditorPanel {
public:
    PlaceholderPanel(const char* name, const char* stage) : name_(name), stage_(stage) {}
    const char* Name() const override { return name_; }
    void OnGui(EditorApp&) override {
        if (!ImGui::Begin(name_, nullptr, ImGuiWindowFlags_NoCollapse)) {
            ImGui::End();
            return;
        }
        ImGui::TextUnformatted(stage_);
        ImGui::TextUnformatted("（M4-Editor-Plan §5 子阶段填充中）");
        ImGui::End();
    }

private:
    const char* name_;
    const char* stage_;
};

} // namespace

std::vector<std::unique_ptr<IEditorPanel>> CreateAllPanels() {
    std::vector<std::unique_ptr<IEditorPanel>> out;
    out.push_back(std::make_unique<HierarchyPanel>());
    out.push_back(std::make_unique<InspectorPanel>());
    out.push_back(std::make_unique<SceneViewPanel>());
    out.push_back(std::make_unique<GameViewPanel>());
    out.push_back(std::make_unique<PlaceholderPanel>("Assets", "资产浏览器（M4.4：GUID/.meta/manifest）"));
    out.push_back(std::make_unique<ConsolePanel>());
    out.push_back(std::make_unique<ProfilerPanel>());
    return out;
}

} // namespace lemon::editor
