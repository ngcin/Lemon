// Lemon 编辑器 — Profiler 面板（M4.md §2.2）
// 数据源与 --stats/F3 同源：SystemPipeline::Profiles + RHI LastFrameTiming +
// ScriptHost::GcAllocated（M4.3 脚本域接入后显示；GC 红字口径 = 每帧托管分配 > 0）。
#include <cstdio>
#include <cstring>
#include <string>

#include "App/EditorApp.h"
#include "ECS/SystemPipeline.h"
#include "EditorContext.h"
#include "Localization/Localization.h"
#include "Panels/BuiltInPanels.h"
#include "Renderer/RHI.h"
#include "Scripting/ScriptHost.h"
#include "Tooling/Theme.h"
#include "imgui.h"

namespace lemon::editor {

void ProfilerPanel::OnGui(EditorApp& app) {
    using lemon::editor::loc::tr;
    bool winOpen = true;
    // 标题 = 本地化显示名 + ###稳定 ID（窗口身份/停靠/ini 持久化不随语言变）
    const std::string title = std::string(tr("panel.profiler")) + "###" + Name();
    if (!ImGui::Begin(title.c_str(), &winOpen, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        if (!winOpen) app.ClosePanel(Name()); // × 关闭（T3b-8）
        return;
    }
    if (!winOpen) app.ClosePanel(Name());
    EditorContext& ctx = app.Ctx();

    // 帧时间曲线（ImGui 自带 PlotLines；编辑器帧全量 ms）
    frameMs_.push_back(1000.0f / std::max(1.0f, ImGui::GetIO().Framerate));
    if (frameMs_.size() > 240) frameMs_.erase(frameMs_.begin());
    if (frameMs_.size() > 2) {
        float sum = 0, mx = 0;
        for (float v : frameMs_) { sum += v; mx = std::max(mx, v); }
        char avgBuf[16], maxBuf[16];
        std::snprintf(avgBuf, sizeof(avgBuf), "%.2f", sum / frameMs_.size());
        std::snprintf(maxBuf, sizeof(maxBuf), "%.2f", mx);
        const std::string overlay =
            loc::trFmt("prof.plot_overlay_fmt", {avgBuf, maxBuf});
        ImGui::PlotLines("##frames", frameMs_.data(), (int)frameMs_.size(), 0,
                         overlay.c_str(), 0.0f, mx * 1.3f + 1.0f, ImVec2(-1, 60));
    }

    // GPU 时间戳（EnableTimestamps 于启动开启）
    const rhi::FrameTiming gpu = app.Device().LastFrameTiming();
    {
        char fpsBuf[16];
        std::snprintf(fpsBuf, sizeof(fpsBuf), "%.0f", ImGui::GetIO().Framerate);
        if (showGpu_) { // #92：原死控件（只写不读）——接线 = 门控 GPU 时间行
            char gpuBuf[16];
            std::snprintf(gpuBuf, sizeof(gpuBuf), "%.2f", gpu.valid ? gpu.gpuMs : -1.0f);
            ImGui::TextUnformatted(
                loc::trFmt("prof.timing_gpu_fmt", {gpuBuf, fpsBuf}).c_str());
        } else {
            ImGui::TextUnformatted(loc::trFmt("prof.timing_cpu_fmt", {fpsBuf}).c_str());
        }
    }

    ImGui::Separator();
    // 系统表（与 --stats 同一数据源；执行序 = 注册序）
    // #91：BeginTable 失败不得早退跳过 ImGui::End()（窗口栈失衡——当前 flags 下
    // 不可达，加滚动 flag 即触发，ImGui 契约地雷）
    if (ImGui::BeginTable("sys", 4,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
                              ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn(tr("prof.col_system"));
        ImGui::TableSetupColumn(tr("prof.col_last"), ImGuiTableColumnFlags_WidthFixed, 70);
        ImGui::TableSetupColumn(tr("prof.col_max"), ImGuiTableColumnFlags_WidthFixed, 70);
        ImGui::TableSetupColumn(tr("prof.col_runs"), ImGuiTableColumnFlags_WidthFixed, 80);
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
    }

    ImGui::Spacing();
    // C# GC 纪律（§6 #7）：每帧托管分配 = GcAllocated 差分；> 0 红字（04 §5 热路径零分配）
    if (ctx.Scripts()) {
        const uint64_t gcNow = ctx.Scripts()->GcAllocated();
        if (gcPrev_ != 0) { // 首帧只立基线不显示
            const int64_t perFrame = gcNow > gcPrev_ ? (int64_t)(gcNow - gcPrev_) : 0;
            if (perFrame > 0) ImGui::PushStyleColor(ImGuiCol_Text, theme::kTextError);
            ImGui::TextUnformatted(
                (loc::trFmt("prof.gc_per_frame_fmt", {std::to_string((long long)perFrame)}) +
                 (perFrame > 0 ? tr("prof.gc_hot") : tr("prof.gc_zero")))
                    .c_str());
            if (perFrame > 0) ImGui::PopStyleColor();
        }
        gcPrev_ = gcNow;
        // 热重载换装/泄漏常驻显示（§3.7：A 线已知限制对用户可见）
        const int reloads = ctx.Scripts()->HotReloadCount();
        const int leaks = ctx.Scripts()->HotReloadLeakCount();
        if (leaks > 0) ImGui::PushStyleColor(ImGuiCol_Text, theme::kTextError);
        ImGui::TextUnformatted(
            loc::trFmt("prof.hot_reload_fmt",
                       {std::to_string(reloads), std::to_string(leaks),
                        std::to_string(leaks * 100)})
                .c_str());
        if (leaks > 0) ImGui::PopStyleColor();
    } else {
        ImGui::TextDisabled("%s", tr("prof.gc_no_host"));
    }
    if (ImGui::Button(tr("prof.reset_peaks"))) ctx.ActiveWorld().Pipeline().ResetProfiles();
    ImGui::SameLine();
    ImGui::Checkbox(tr("prof.gpu_col"), &showGpu_);
    ImGui::End();
}

} // namespace lemon::editor
