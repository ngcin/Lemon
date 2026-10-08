#include "Tooling/FilePicker.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

#include "Core/Log.h"
#include "Localization/Localization.h"
#include "Tooling/ThumbCache.h"
#include "Tooling/TestHooks.h"
#include "Tooling/Theme.h"
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"

namespace lemon::editor {

void FilePicker::Open(const char* title, const std::string& defaultDir,
                      const std::string& defaultName, const char* requireExt) {
    title_ = title;
    fileName_ = defaultName;
    requireExt_ = requireExt ? requireExt : "";
    dirMode_ = false;
    multi_ = false; // Open/OpenDir 重置多选态（与 OpenMulti 互斥）
    filterExts_.clear();
    multiSel_.clear();
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

void FilePicker::OpenMulti(const char* title, const std::string& defaultDir,
                           std::vector<std::string> filterExts) {
    Open(title, defaultDir, "", nullptr);
    multi_ = true;
    view_ = View::Icons; // 图片多选默认缩略图网格（T3-UX4；可切回列表）
    filterExts_ = std::move(filterExts);
    Refresh(); // 按扩展名白名单重刷
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
        if (!isDir && !filterExts_.empty()) { // 多选模式：扩展名白名单（小写比较）
            std::string ext = p.extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(),
                           [](unsigned char c) { return (char)std::tolower(c); });
            bool ok = false;
            for (const std::string& want : filterExts_)
                if (ext == want) ok = true;
            if (!ok) continue;
        }
        if (!isDir && !requireExt_.empty() && p.extension().string() != requireExt_)
            continue; // 保存模式列表里也过滤显示同类文件
        entries_.push_back({name, isDir});
    }
    std::sort(entries_.begin(), entries_.end(), [](const Entry& a, const Entry& b) {
        if (a.isDir != b.isDir) return a.isDir; // 目录在前
        return a.name < b.name;
    });
    anchorIdx_ = -1; // 换目录 = 旧锚点作废（Shift 范围不跨目录）
    pathInput_ = dir_.string(); // 手输框随当前目录同步（M4.6 §5-7）
}

PickerResult FilePicker::Draw() {
    using lemon::editor::loc::tr;
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
    if (ImGui::Button(tr("fp.up"))) {
        auto parent = dir_.parent_path();
        if (parent != dir_) {
            dir_ = parent;
            selected_.clear();
            Refresh();
        }
    }
    ImGui::SameLine();
    if (dirMode_) {
        if (ImGui::Button(tr("fp.select_this_dir"))) {
            out = {PickerAction::Open, dir_.string()};
            open_ = false;
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("fp.new_folder"))) {
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
    // 快捷目录钮 + 路径手输（M4.6 §5-7）：绝对路径回车直达（目录 = 进入；文件 = 进父目录并选中）
    for (const auto& [label, path] : quickDirs_) {
        std::error_code ecq;
        if (!path.empty() && std::filesystem::is_directory(path, ecq) &&
            ImGui::Button(label.c_str())) {
            dir_ = std::filesystem::path(path);
            selected_.clear();
            Refresh();
        }
        ImGui::SameLine();
    }
    // 视图切换（T3-UX4）：列表 ↔ 缩略图网格（激活侧高亮；参考 docs/Animation/
    // select multi images from file system.png——Godot 式缩略图网格选图）
    {
        auto toggle = [this](const char* label, View v) {
            const bool active = view_ == v;
            if (active) ImGui::PushStyleColor(ImGuiCol_Button,
                                              ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            if (ImGui::Button(label, ImVec2(ImGui::CalcTextSize(label).x + 16.0f, 0)))
                view_ = v;
            if (active) ImGui::PopStyleColor();
            ImGui::SameLine();
        };
        toggle(tr("fp.view_list"), View::List);
        toggle(tr("fp.view_thumbs"), View::Icons);
    }
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputTextWithHint("##path", tr("fp.path_hint"), &pathInput_,
                                 ImGuiInputTextFlags_EnterReturnsTrue)) {
        std::error_code ecp;
        std::filesystem::path p(pathInput_);
        if (!pathInput_.empty() && std::filesystem::is_directory(p, ecp)) {
            dir_ = p;
            selected_.clear();
            Refresh();
        } else if (!pathInput_.empty() && std::filesystem::is_regular_file(p, ecp)) {
            dir_ = p.parent_path();
            if (!dirMode_) fileName_ = p.filename().string(); // 文件名进下方名字框
            selected_ = p;
            Refresh();
            pathInput_ = dir_.string(); // 框回显父目录
        } else if (!pathInput_.empty()) {
            LEMON_WARN("路径不存在：%s", pathInput_.c_str());
            pathInput_ = dir_.string();
        }
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", tr("fp.path_tooltip"));

    // Ctrl/Cmd+A 全选文件（T3-UX4 系统式选择语义；路径输入框聚焦时让位文本编辑。
    // mac 侧编辑器把 Ctrl 和弦映射为 Cmd（ImGuiBackend 约定）→ 双修饰都认）
    if (multi_ && !ImGui::GetIO().WantTextInput &&
        (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_A) ||
         ImGui::IsKeyChordPressed(ImGuiMod_Super | ImGuiKey_A))) {
        multiSel_.clear();
        anchorIdx_ = -1;
        for (size_t k = 0; k < entries_.size(); ++k)
            if (!entries_[k].isDir) {
                if (anchorIdx_ < 0) anchorIdx_ = (int)k; // 后续 Shift 从首个文件连
                multiSel_.push_back(dir_ / entries_[k].name);
            }
    }

    ImGui::Separator();
    thumbcache::Tick(); // 缩略图逐帧解码预算（未初始化 = 空转）
    ImGui::BeginChild("list", ImVec2(0, dirMode_ ? 0.0f : -ImGui::GetFrameHeightWithSpacing() * 2.2f),
                      ImGuiChildFlags_None, ImGuiWindowFlags_AlwaysVerticalScrollbar);
    if (view_ == View::Icons)
        DrawIconEntries(out);
    else
        DrawListEntries(out);
    ImGui::EndChild();

    // 文件名 + 确认行（目录/多选模式无文件名概念，列表即全部）
    if (!dirMode_ && !multi_) {
        ImGui::SetNextItemWidth(-160);
        if (firstFrame_) ImGui::SetKeyboardFocusHere();
        ImGui::InputText("##name", &fileName_);
        ImGui::SameLine();
        if (ImGui::Button(tr("common.confirm")) && !fileName_.empty()) {
            std::string name = fileName_;
            if (!requireExt_.empty() && std::filesystem::path(name).extension().string() != requireExt_)
                name += requireExt_;
            out = {PickerAction::Open, (dir_ / name).string()};
            if (!requireExt_.empty()) out.action = PickerAction::Save;
            open_ = false;
        }
        ImGui::SameLine();
    }
    if (multi_) {
        ImGui::TextDisabled(
            "%s",
            lemon::editor::loc::trFmt("fp.selected_fmt", {std::to_string(multiSel_.size())})
                .c_str());
        ImGui::SameLine();
        ImGui::BeginDisabled(multiSel_.empty());
        if (ImGui::Button(tr("fp.open"))) ConfirmMulti(out);
        testhooks::Stash("picker.open", ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        ImGui::EndDisabled();
        ImGui::SameLine();
    }
    if (ImGui::Button(tr("common.cancel"))) {
        out = {PickerAction::Cancel, ""};
        open_ = false;
    }
    testhooks::Stash("picker.cancel", ImGui::GetItemRectMin(), ImGui::GetItemRectMax());

    firstFrame_ = false;
    if (!open_) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
    return out;
}

// ---------------------------------------------------- T3-UX4 选择语义 ----

void FilePicker::ApplyMultiClick(size_t idx, bool ctrl, bool shift) {
    // 系统式三态（Finder/Explorer 同款）：
    //   单击 = 清余选己（记锚）；Ctrl = 加/移单项（记锚）；
    //   Shift = 锚点到此处范围连选（替换；锚不动——连续 Shift = 换端连选）。
    // 目录不可选（跨目录累选无意义）；范围内的目录条目跳过。
    if (!multi_ || idx >= entries_.size() || entries_[idx].isDir) return;
    const std::filesystem::path full = dir_ / entries_[idx].name;
    if (shift && anchorIdx_ >= 0) {
        multiSel_.clear();
        const size_t lo = std::min(idx, (size_t)anchorIdx_);
        const size_t hi = std::max(idx, (size_t)anchorIdx_);
        for (size_t k = lo; k <= hi; ++k)
            if (!entries_[k].isDir) multiSel_.push_back(dir_ / entries_[k].name);
    } else if (ctrl) {
        auto it = std::find(multiSel_.begin(), multiSel_.end(), full);
        if (it != multiSel_.end())
            multiSel_.erase(it);
        else
            multiSel_.push_back(full);
        anchorIdx_ = (int)idx;
    } else {
        multiSel_.clear();
        multiSel_.push_back(full);
        anchorIdx_ = (int)idx;
    }
}

std::vector<std::filesystem::path> FilePicker::OrderedMultiSel() const {
    std::vector<std::filesystem::path> out;
    for (const Entry& e : entries_) {
        if (e.isDir) continue;
        const std::filesystem::path full = dir_ / e.name;
        if (std::find(multiSel_.begin(), multiSel_.end(), full) != multiSel_.end())
            out.push_back(full);
    }
    return out;
}

void FilePicker::ConfirmMulti(PickerResult& out) {
    const std::vector<std::filesystem::path> sel = OrderedMultiSel();
    if (sel.empty()) return;
    out.action = PickerAction::Open;
    out.path = sel.front().string();
    for (auto& p : sel) out.paths.push_back(p.string());
    open_ = false;
}

void FilePicker::NavigateTo(const std::filesystem::path& sub) {
    dir_ = sub;
    selected_.clear();
    multiSel_.clear();
    Refresh(); // 内含 anchorIdx_ = -1
}

void FilePicker::DrawListEntries(PickerResult& out) {
    using lemon::editor::loc::tr;
    const std::string dirPrefix = tr("fp.dir_prefix"); // 目录条目前缀（列表视图）
    for (size_t i = 0; i < entries_.size(); ++i) {
        const Entry& e = entries_[i];
        ImGuiSelectableFlags flags = ImGuiSelectableFlags_AllowDoubleClick;
        const std::filesystem::path full = dir_ / e.name;
        if (multi_) {
            const bool inSel = std::find(multiSel_.begin(), multiSel_.end(), full) !=
                               multiSel_.end();
            if (ImGui::Selectable(e.isDir ? (dirPrefix + e.name).c_str() : e.name.c_str(),
                                  inSel, flags)) {
                if (e.isDir) {
                    // 单击仅高亮；双击进入（T3-UX4：网格误点不清选集——列表同口径）
                    if (ImGui::IsMouseDoubleClicked(0)) NavigateTo(full);
                    else selected_ = full;
                } else {
                    ApplyMultiClick(i, ImGui::GetIO().KeyCtrl, ImGui::GetIO().KeyShift);
                    if (ImGui::IsMouseDoubleClicked(0)) ConfirmMulti(out);
                }
            }
            continue;
        }
        const bool wasSel = selected_ == full;
        if (ImGui::Selectable(e.isDir ? (dirPrefix + e.name).c_str() : e.name.c_str(),
                              wasSel, flags)) {
            selected_ = full;
            if (ImGui::IsMouseDoubleClicked(0)) {
                if (e.isDir) NavigateTo(full);
                else {
                    out = {PickerAction::Open, full.string()};
                    open_ = false;
                }
            }
        }
    }
}

void FilePicker::DrawIconEntries(PickerResult& out) {
    using lemon::editor::loc::tr;
    // 缩略图网格（参考 Godot 文件对话框）：等宽瓦片按可用宽折行；瓦片 =
    // 隐形按钮（点击/双击走真实管线）+ drawlist 手绘（底板/图/名/选中描边）。
    const float tileW = 104.0f;
    const float imgH = 72.0f;
    const float pad = 6.0f;
    const float lineH = ImGui::GetTextLineHeight();
    const float tileH = imgH + lineH + lineH * 0.9f + 10.0f;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const int cols =
        std::max(1, (int)(ImGui::GetContentRegionAvail().x / (tileW + spacing)));
    int col = 0;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (size_t i = 0; i < entries_.size(); ++i) {
        const Entry& e = entries_[i];
        if (col > 0) ImGui::SameLine();
        if (++col >= cols) col = 0;
        ImGui::PushID((int)i);
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        const ImVec2 p1{p0.x + tileW, p0.y + tileH};
        const std::filesystem::path full = dir_ / e.name;
        const bool inMulti = multi_ && std::find(multiSel_.begin(), multiSel_.end(),
                                                 full) != multiSel_.end();
        const bool sel = multi_ ? inMulti : selected_ == full;
        if (ImGui::InvisibleButton("tile", ImVec2(tileW, tileH))) {
            if (e.isDir) { // 单击高亮 / 双击进入（与列表同口径）
                if (ImGui::IsMouseDoubleClicked(0)) NavigateTo(full);
                else selected_ = full;
            } else if (multi_) {
                ApplyMultiClick(i, ImGui::GetIO().KeyCtrl, ImGui::GetIO().KeyShift);
                if (ImGui::IsMouseDoubleClicked(0)) ConfirmMulti(out);
            } else {
                selected_ = full;
                if (ImGui::IsMouseDoubleClicked(0)) {
                    out = {PickerAction::Open, full.string()};
                    open_ = false;
                }
            }
        }
        const bool hovered = ImGui::IsItemHovered();

        // 底板 + 选中描边
        dl->AddRectFilled(p0, p1,
                          ImGui::GetColorU32(ImGuiCol_FrameBg, hovered ? 0.9f : 0.5f),
                          6.0f);
        if (sel) {
            // 主选 = 主题亮蓝（ButtonActive 在本主题 = 背景灰，用作描边不可见——
            // T3-UX4 截图目检发现：11 项全选却无任何视觉反馈）
            dl->AddRectFilled(p0, p1, ImGui::ColorConvertFloat4ToU32(
                                         ImVec4(theme::kAccent.x, theme::kAccent.y,
                                                theme::kAccent.z, 0.18f)),
                              6.0f);
            dl->AddRect(p0, p1, ImGui::ColorConvertFloat4ToU32(theme::kAccent), 6.0f, 0,
                        2.0f);
        }

        // 图区（等比 fit 居中；目录/非图占位）
        const ImVec2 ib0{p0.x + pad, p0.y + pad - 1.0f};
        const ImVec2 ib1{p1.x - pad, p0.y + pad + imgH};
        int iw = 0, ih = 0;
        if (e.isDir) {
            dl->AddRectFilled(ib0, ib1, ImGui::GetColorU32(ImGuiCol_Tab, 0.4f), 4.0f);
            const char* dirLabel = tr("fp.dir_tile");
            const ImVec2 ts = ImGui::CalcTextSize(dirLabel);
            dl->AddText(ImVec2((ib0.x + ib1.x - ts.x) * 0.5f, (ib0.y + ib1.y - ts.y) * 0.5f),
                        ImGui::GetColorU32(ImGuiCol_Text, 0.65f), dirLabel);
        } else if (void* tex = thumbcache::Get(full.string(), &iw, &ih); tex) {
            const float s = std::min((ib1.x - ib0.x) / (float)iw,
                                     (ib1.y - ib0.y) / (float)ih);
            const float dw = iw * s, dh = ih * s;
            const ImVec2 c{(ib0.x + ib1.x - dw) * 0.5f, (ib0.y + ib1.y - dh) * 0.5f};
            dl->AddImage((ImTextureID)tex, c, ImVec2{c.x + dw, c.y + dh});
        } else {
            dl->AddRectFilled(ib0, ib1, ImGui::GetColorU32(ImGuiCol_FrameBg), 4.0f);
            std::string ext = std::filesystem::path(e.name).extension().string();
            for (char& c : ext) c = (char)std::toupper((unsigned char)c);
            if (ext.empty()) ext = "?";
            if (ext.size() > 5) ext.resize(5);
            const ImVec2 ts = ImGui::CalcTextSize(ext.c_str());
            dl->AddText(ImVec2((ib0.x + ib1.x - ts.x) * 0.5f, (ib0.y + ib1.y - ts.y) * 0.5f),
                        ImGui::GetColorU32(ImGuiCol_Text, 0.45f), ext.c_str());
        }

        // 名字（居中省略号）+ 次行原图尺寸（选图时有感）
        std::string shown = e.name;
        if (ImGui::CalcTextSize(shown.c_str()).x > tileW - pad * 2.0f) {
            while (!shown.empty() &&
                   ImGui::CalcTextSize((shown + "…").c_str()).x > tileW - pad * 2.0f)
                shown.pop_back();
            shown += "…";
        }
        const ImVec2 ns = ImGui::CalcTextSize(shown.c_str());
        dl->AddText(ImVec2(p0.x + (tileW - ns.x) * 0.5f, p0.y + pad + imgH + 3.0f),
                    ImGui::GetColorU32(ImGuiCol_Text), shown.c_str());
        if (!e.isDir && iw > 0 && ih > 0) {
            char dim[32];
            std::snprintf(dim, sizeof(dim), "%d×%d", iw, ih);
            const ImVec2 ds = ImGui::CalcTextSize(dim);
            dl->AddText(ImVec2(p0.x + (tileW - ds.x) * 0.5f, p0.y + pad + imgH + lineH + 5.0f),
                        ImGui::GetColorU32(ImGuiCol_Text, 0.5f), dim);
        }
        testhooks::Stash((std::string("picker.tile") + std::to_string(i)).c_str(), p0, p1);
        ImGui::PopID();
    }
}

} // namespace lemon::editor
