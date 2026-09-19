// Lemon 编辑器 — Profiler 面板（M4-Editor-Plan §2.2）
// 数据源与 --stats/F3 同源：SystemPipeline::Profiles + RHI LastFrameTiming +
// ScriptHost::GcAllocated（M4.3 脚本域接入后显示；GC 红字口径 = 每帧托管分配 > 0）。
#include <cstring>

#include "App/EditorApp.h"
#include "ECS/SystemPipeline.h"
#include "EditorContext.h"
#include "Panels/BuiltInPanels.h"
#include "Renderer/RHI.h"
#include "imgui.h"

namespace lemon::editor {

void ProfilerPanel::OnGui(EditorApp& app) {
    if (!ImGui::Begin(Name(), nullptr, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }
    EditorContext& ctx = app.Ctx();

    // 帧时间曲线（ImGui 自带 PlotLines；编辑器帧全量 ms）
    frameMs_.push_back(1000.0f / std::max(1.0f, ImGui::GetIO().Framerate));
    if (frameMs_.size() > 240) frameMs_.erase(frameMs_.begin());
    if (frameMs_.size() > 2) {
        float sum = 0, mx = 0;
        for (float v : frameMs_) { sum += v; mx = std::max(mx, v); }
        char overlay[64];
        std::snprintf(overlay, sizeof(overlay), "avg %.2fms  max %.2fms", sum / frameMs_.size(), mx);
        ImGui::PlotLines("##frames", frameMs_.data(), (int)frameMs_.size(), 0, overlay, 0.0f,
                         mx * 1.3f + 1.0f, ImVec2(-1, 60));
    }

    // GPU 时间戳（EnableTimestamps 于启动开启）
    const rhi::FrameTiming gpu = app.Device().LastFrameTiming();
    ImGui::Text("GPU %.2f ms   CPU(fps) %.0f", gpu.valid ? gpu.gpuMs : -1.0f,
                ImGui::GetIO().Framerate);

    ImGui::Separator();
    // 系统表（与 --stats 同一数据源；执行序 = 注册序）
    if (!ImGui::BeginTable("sys", 4,
                           ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
                               ImGuiTableFlags_SizingStretchProp))
        return;
    ImGui::TableSetupColumn("system");
    ImGui::TableSetupColumn("last", ImGuiTableColumnFlags_WidthFixed, 70);
    ImGui::TableSetupColumn("max", ImGuiTableColumnFlags_WidthFixed, 70);
    ImGui::TableSetupColumn("runs", ImGuiTableColumnFlags_WidthFixed, 80);
    ImGui::TableHeadersRow();
    for (const ecs::SystemProfile& p : ctx.World().Pipeline().Profiles()) {
        if (p.runs == 0) continue;
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(p.name);
        ImGui::TableNextColumn();
        ImGui::Text("%.3f", p.lastMs);
        ImGui::TableNextColumn();
        ImGui::Text("%.3f", p.maxMs);
        ImGui::TableNextColumn();
        ImGui::Text("%llu", (unsigned long long)p.runs);
    }
    ImGui::EndTable();

    ImGui::Spacing();
    ImGui::TextDisabled("GC：M4.3 脚本域接入后显示（红字口径 = 热路径每帧托管分配 > 0）");
    if (ImGui::Button("Reset Peaks")) ctx.World().Pipeline().ResetProfiles();
    ImGui::SameLine();
    ImGui::Checkbox("GPU 列", &showGpu_);
    ImGui::End();
}

} // namespace lemon::editor
