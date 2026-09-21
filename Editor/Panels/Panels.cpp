// Lemon 编辑器 — 面板注册中心（编译期注册表唯一登记点；新增面板在此追加）
#include <cstdio>
#include <memory>

#include "imgui.h"

#include "App/EditorApp.h"
#include "Core/Log.h"
#include "EditorContext.h"
#include "Panels/BuiltInPanels.h"
#include "Panels/Panel.h"
#include "Tooling/Theme.h"

namespace lemon::editor {
namespace {

// -------------------------------------------------------------- Console ----
// 实装：引擎日志环 + 分级过滤/清空/自动滚动（M4-Editor-Plan §2.2；M4.3 补脚本异常源标记）
// M4.7a：徽标计数常显（Info/Warn/Error 行数随过滤勾标注出，非零警示级着色）
class ConsolePanel final : public IEditorPanel {
public:
    const char* Name() const override { return "Console"; }
    void OnGui(EditorApp& app) override {
        if (!ImGui::Begin(Name(), nullptr, ImGuiWindowFlags_NoCollapse)) {
            ImGui::End();
            return;
        }
        std::vector<EditorLogLine> snap = app.Log().Snapshot();
        // 分级计数（徽标原料；环容量级遍历，每帧成本可忽略）
        uint32_t n[3] = {0, 0, 0};
        for (const auto& l : snap) ++n[(int)l.level];
        const uint32_t nWarn = n[(int)LogLevel::Warn], nErr = n[(int)LogLevel::Error];

        if (ImGui::Button("Clear")) app.Log().Clear();
        ImGui::SameLine();
        CountedFilter("Info", (int)LogLevel::Info, n[(int)LogLevel::Info], theme::kTextDim);
        ImGui::SameLine();
        CountedFilter("Warn", (int)LogLevel::Warn, nWarn,
                      nWarn ? theme::kTextWarn : theme::kTextDim);
        ImGui::SameLine();
        CountedFilter("Error", (int)LogLevel::Error, nErr,
                      nErr ? theme::kTextError : theme::kTextDim);
        ImGui::SameLine();
        ImGui::Checkbox("Auto-scroll", &autoScroll_);
        ImGui::SameLine();
        ImGui::Checkbox("Collapse", &collapse_); // M4.7d：连续重复行折叠 ×N
        ImGui::Separator();

        ImGui::BeginChild("lines", ImVec2(0, 0), ImGuiChildFlags_None,
                          ImGuiWindowFlags_HorizontalScrollbar);
        if (!collapse_) {
            for (const auto& l : snap) {
                if (!(filter_ & (1u << (int)l.level))) continue;
                const ImVec4 c = LineColor(l.level);
                ImGui::PushStyleColor(ImGuiCol_Text, c);
                ImGui::TextUnformatted(l.text.c_str());
                ImGui::PopStyleColor();
            }
        } else {
            // 折叠：流内连续同文同级并组（Unity Collapse 语义）——刷怪/重复告警
            // 类噪音一行 + ×N 徽标；过滤只作用于组首（整组同级，等效全组）
            for (size_t i = 0; i < snap.size(); ++i) {
                size_t j = i;
                while (j + 1 < snap.size() && snap[j + 1].level == snap[i].level &&
                       snap[j + 1].text == snap[i].text)
                    ++j;
                const uint32_t n = (uint32_t)(j - i + 1);
                if (filter_ & (1u << (int)snap[i].level)) {
                    ImGui::PushStyleColor(ImGuiCol_Text, LineColor(snap[i].level));
                    ImGui::TextUnformatted(snap[i].text.c_str());
                    ImGui::PopStyleColor();
                    if (n > 1) {
                        ImGui::SameLine();
                        ImGui::TextDisabled("×%u", n);
                    }
                }
                i = j;
            }
        }
        if (autoScroll_ && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f)
            ImGui::SetScrollHereY(1.0f);
        ImGui::EndChild();
        ImGui::End();
    }

private:
    static ImVec4 LineColor(LogLevel lv) {
        return lv == LogLevel::Error ? theme::kTextError
               : lv == LogLevel::Warn ? theme::kTextWarn
                                      : theme::kText;
    }

    /// 勾选 + 徽标计数（"Warn 12"；非零警示级着色，零/Info 走次级灰）
    void CountedFilter(const char* label, int levelIdx, uint32_t count,
                       const ImVec4& countColor) {
        char buf[40];
        std::snprintf(buf, sizeof(buf), "%s %u", label, count);
        ImGui::PushStyleColor(ImGuiCol_Text, countColor);
        ImGui::CheckboxFlags(buf, &filter_, 1u << levelIdx);
        ImGui::PopStyleColor();
    }

    uint32_t filter_ = 0b111;
    bool autoScroll_ = true;
    bool collapse_ = true;
};

} // namespace

std::vector<std::unique_ptr<IEditorPanel>> CreateAllPanels() {
    std::vector<std::unique_ptr<IEditorPanel>> out;
    out.push_back(std::make_unique<HierarchyPanel>());
    out.push_back(std::make_unique<InspectorPanel>());
    out.push_back(std::make_unique<SceneViewPanel>());
    out.push_back(std::make_unique<GameViewPanel>());
    out.push_back(std::make_unique<AssetBrowserPanel>());
    out.push_back(std::make_unique<ConsolePanel>());
    out.push_back(std::make_unique<ProfilerPanel>());
    return out;
}

} // namespace lemon::editor
