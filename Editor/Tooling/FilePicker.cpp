#include "Tooling/FilePicker.h"

#include <algorithm>

#include "Core/Log.h"
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"

namespace lemon::editor {

void FilePicker::Open(const char* title, const std::string& defaultDir,
                      const std::string& defaultName, const char* requireExt) {
    title_ = title;
    fileName_ = defaultName;
    requireExt_ = requireExt ? requireExt : "";
    std::error_code ec;
    dir_ = std::filesystem::exists(defaultDir, ec) ? std::filesystem::path(defaultDir)
                                                   : std::filesystem::current_path(ec);
    open_ = true;
    firstFrame_ = true;
    Refresh();
}

void FilePicker::Refresh() {
    entries_.clear();
    std::error_code ec;
    for (auto it = std::filesystem::directory_iterator(dir_, ec);
         it != std::filesystem::directory_iterator(); ++it) {
        const std::filesystem::path& p = it->path();
        std::string name = p.filename().string();
        if (name.empty() || name[0] == '.') continue; // 隐藏文件
        if (!it->is_directory(ec) && !requireExt_.empty() &&
            p.extension().string() != requireExt_)
            continue; // 保存模式列表里也过滤显示同类文件
        entries_.push_back({name, it->is_directory(ec)});
    }
    std::sort(entries_.begin(), entries_.end(), [](const Entry& a, const Entry& b) {
        if (a.isDir != b.isDir) return a.isDir; // 目录在前
        return a.name < b.name;
    });
}

PickerResult FilePicker::Draw() {
    if (!open_) return {};
    PickerResult out;
    ImGui::SetNextWindowSize(ImVec2(640, 420), ImGuiCond_Appearing);
    if (!ImGui::Begin(title_.c_str(), &open_, ImGuiWindowFlags_NoDocking)) {
        ImGui::End();
        if (!open_) out = {PickerAction::Cancel, ""};
        return out;
    }

    // 路径行：父目录按钮 + 当前路径 + 新建目录
    if (ImGui::Button("↑ 上级")) {
        std::error_code ec;
        auto parent = dir_.parent_path();
        if (parent != dir_) {
            dir_ = parent;
            Refresh();
        }
        (void)ec;
    }
    ImGui::SameLine();
    ImGui::TextUnformatted(dir_.string().c_str());

    ImGui::Separator();
    ImGui::BeginChild("list", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 2.2f));
    for (const Entry& e : entries_) {
        ImGuiSelectableFlags flags = ImGuiSelectableFlags_AllowDoubleClick;
        const bool wasSel = selected_ == dir_ / e.name;
        if (ImGui::Selectable(e.isDir ? (std::string("[D] ") + e.name).c_str()
                                      : e.name.c_str(),
                              wasSel, flags)) {
            selected_ = dir_ / e.name;
            if (!e.isDir) fileName_ = e.name;
            if (ImGui::IsMouseDoubleClicked(0)) {
                if (e.isDir) {
                    dir_ /= e.name;
                    selected_.clear();
                    Refresh();
                } else {
                    out = {PickerAction::Open, (dir_ / e.name).string()};
                    open_ = false;
                }
            }
        }
    }
    ImGui::EndChild();

    // 文件名 + 确认行
    ImGui::SetNextItemWidth(-160);
    if (firstFrame_) ImGui::SetKeyboardFocusHere();
    ImGui::InputText("##name", &fileName_);
    ImGui::SameLine();
    if (ImGui::Button("确认") && !fileName_.empty()) {
        std::string name = fileName_;
        if (!requireExt_.empty() && std::filesystem::path(name).extension().string() != requireExt_)
            name += requireExt_;
        out = {PickerAction::Open, (dir_ / name).string()};
        if (!requireExt_.empty()) out.action = PickerAction::Save;
        open_ = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("取消")) {
        out = {PickerAction::Cancel, ""};
        open_ = false;
    }

    firstFrame_ = false;
    ImGui::End();
    return out;
}

} // namespace lemon::editor
