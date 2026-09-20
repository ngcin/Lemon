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
    dirMode_ = false;
    std::error_code ec;
    dir_ = std::filesystem::exists(defaultDir, ec) ? std::filesystem::path(defaultDir)
                                                   : std::filesystem::current_path(ec);
    open_ = true;
    opening_ = true;
    firstFrame_ = true;
    selected_.clear();
    Refresh();
}

void FilePicker::OpenDir(const char* title, const std::string& defaultDir) {
    Open(title, defaultDir, "", nullptr);
    dirMode_ = true;
    Refresh(); // 目录模式只列目录（重刷一次）
}

void FilePicker::Refresh() {
    entries_.clear();
    std::error_code ec;
    for (auto it = std::filesystem::directory_iterator(dir_, ec);
         it != std::filesystem::directory_iterator(); ++it) {
        const std::filesystem::path& p = it->path();
        std::string name = p.filename().string();
        if (name.empty() || name[0] == '.') continue; // 隐藏文件
        const bool isDir = it->is_directory(ec);
        if (dirMode_ && !isDir) continue; // 目录模式：只列目录
        if (!isDir && !requireExt_.empty() && p.extension().string() != requireExt_)
            continue; // 保存模式列表里也过滤显示同类文件
        entries_.push_back({name, isDir});
    }
    std::sort(entries_.begin(), entries_.end(), [](const Entry& a, const Entry& b) {
        if (a.isDir != b.isDir) return a.isDir; // 目录在前
        return a.name < b.name;
    });
}

PickerResult FilePicker::Draw() {
    if (!open_) return {};
    // 模态化（M4.6 §4-6）：选择器开着主 UI 不可点（先选后改背后场景的竞态修复）
    if (opening_) {
        ImGui::OpenPopup(title_.c_str());
        opening_ = false;
    }
    PickerResult out;
    ImGui::SetNextWindowSize(ImVec2(640, 420), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal(title_.c_str(), &open_)) {
        if (!open_) out = {PickerAction::Cancel, ""}; // 右上角 X = 取消（恰好一次）
        return out;
    }

    // 路径行：父目录 + 当前路径（目录模式加"选择此目录/新建目录"）
    if (ImGui::Button("↑ 上级")) {
        auto parent = dir_.parent_path();
        if (parent != dir_) {
            dir_ = parent;
            selected_.clear();
            Refresh();
        }
    }
    ImGui::SameLine();
    if (dirMode_) {
        if (ImGui::Button("选择此目录")) {
            out = {PickerAction::Open, dir_.string()};
            open_ = false;
        }
        ImGui::SameLine();
        if (ImGui::Button("＋ 新建目录")) {
            // 就地建 NewFolder（重名自动加序号）并进入——向导选父目录时顺手归类
            for (int i = 1;; ++i) {
                const std::string nm = i == 1 ? "NewFolder" : "NewFolder" + std::to_string(i);
                const std::filesystem::path np = dir_ / nm;
                std::error_code ec;
                if (std::filesystem::create_directory(np, ec)) {
                    dir_ = np;
                    selected_.clear();
                    Refresh();
                    break;
                }
                if (ec) {
                    LEMON_WARN("新建目录失败：%s", ec.message().c_str());
                    break;
                }
            }
        }
        ImGui::SameLine();
    }
    ImGui::TextUnformatted(dir_.string().c_str());

    ImGui::Separator();
    ImGui::BeginChild("list", ImVec2(0, dirMode_ ? 0.0f : -ImGui::GetFrameHeightWithSpacing() * 2.2f),
                      ImGuiChildFlags_None, ImGuiWindowFlags_AlwaysVerticalScrollbar);
    for (const Entry& e : entries_) {
        ImGuiSelectableFlags flags = ImGuiSelectableFlags_AllowDoubleClick;
        const bool wasSel = selected_ == dir_ / e.name;
        if (ImGui::Selectable(e.isDir ? (std::string("[D] ") + e.name).c_str()
                                      : e.name.c_str(),
                              wasSel, flags)) {
            selected_ = dir_ / e.name;
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

    // 文件名 + 确认行（目录模式无文件名概念，列表即全部）
    if (!dirMode_) {
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
    }
    if (ImGui::Button("取消")) {
        out = {PickerAction::Cancel, ""};
        open_ = false;
    }

    firstFrame_ = false;
    if (!open_) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
    return out;
}

} // namespace lemon::editor
