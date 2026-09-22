// Lemon 编辑器 — Profiler 面板（M4-Editor-Plan §2.2）
// 数据源与 --stats/F3 同源：SystemPipeline::Profiles + RHI LastFrameTiming +
// ScriptHost::GcAllocated（M4.3 脚本域接入后显示；GC 红字口径 = 每帧托管分配 > 0）。
#include <cstring>

#include "App/EditorApp.h"
#include "ECS/SystemPipeline.h"
#include "EditorContext.h"
#include "Panels/BuiltInPanels.h"
#include "Renderer/RHI.h"
#include "Scripting/ScriptHost.h"
#include "Tooling/Theme.h"
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
    // ActiveWorld：Play 中 = Play World（真正在 Step 的世界），否则 = 编辑世界
    // （BUG-2：读 World() 恒为不 Step 的编辑世界，Play 时系统表恒空）
    for (const ecs::SystemProfile& p : ctx.ActiveWorld().Pipeline().Profiles()) {
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
    // C# GC 纪律（§6 #7）：每帧托管分配 = GcAllocated 差分；> 0 红字（04 §5 热路径零分配）
    if (ctx.Scripts()) {
        const uint64_t gcNow = ctx.Scripts()->GcAllocated();
        if (gcPrev_ != 0) { // 首帧只立基线不显示
            const int64_t perFrame = gcNow > gcPrev_ ? (int64_t)(gcNow - gcPrev_) : 0;
            if (perFrame > 0) ImGui::PushStyleColor(ImGuiCol_Text, theme::kTextError);
            ImGui::Text("C# GC 分配/帧：%lld B%s", (long long)perFrame,
                        perFrame > 0 ? "  ⚠ 热路径分配（红字口径 04 §5）" : "（零分配 ✔）");
            if (perFrame > 0) ImGui::PopStyleColor();
        }
        gcPrev_ = gcNow;
        // 热重载换装/泄漏常驻显示（§3.7：A 线已知限制对用户可见）
        const int reloads = ctx.Scripts()->HotReloadCount();
        const int leaks = ctx.Scripts()->HotReloadLeakCount();
        if (leaks > 0) ImGui::PushStyleColor(ImGuiCol_Text, theme::kTextError);
        ImGui::Text("热重载：%d 次｜旧域未回收 %d 次（~%d KB，ADR-010 A 线已知限制）", reloads,
                    leaks, leaks * 100);
        if (leaks > 0) ImGui::PopStyleColor();
    } else {
        ImGui::TextDisabled("GC：无脚本宿主（--script / 项目 Game/）");
    }
    if (ImGui::Button("Reset Peaks")) ctx.ActiveWorld().Pipeline().ResetProfiles();
    ImGui::SameLine();
    ImGui::Checkbox("GPU 列", &showGpu_);
    ImGui::End();
}

} // namespace lemon::editor
