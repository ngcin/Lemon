// Lemon 编辑器 — Animation 面板（M6a 批② T3 + T3b；05 §7 内容编辑器三件套之一）
// .clip 帧动画的读-改-写 + 三条生产通道（T3b 用户实测反馈驱动）：
//   A 文件夹多单图一键建动画（右键文件夹/向导；整图引用 = 未切片 cell0 本体号）
//   B 单图切片（meta importer，Unity 式归属 + 面板内快捷入口 SetGridSlice）
//   C 网格拖框选区间 → 追加/替换帧（Godot SpriteFrames 式交互）
// 帧列表 = 横排胶片带（拖拽重排 / Ctrl 多选删 / 单选帧编辑行）；LoopMode 三模式
//（Once/Loop/PingPong）。播放预览 = 编辑器时钟推进（非确定无妨，不进模拟）。
// EnterPlay 快照语义 = 保存后下次 Enter Play 生效；Play 中只读（X 关面板同步
// PanelRegistry——Window 菜单可重开）。帧可解析判据与运行时 BuildPlayClipCache
// 同源：切片表 cell 界内 / 未切片 cell 0 且本体号非零（T3b-1 整图引用）。
#define IMGUI_DEFINE_MATH_OPERATORS // ImVec2 ± 运算（须先于一切 imgui.h 包含点）
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "App/EditorApp.h"
#include "Assets/AssetDatabase.h"
#include "Core/Log.h"
#include "EditorContext.h"
#include "Panels/BuiltInPanels.h"
#include "Tooling/Theme.h"
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"

namespace lemon::editor {
namespace {
constexpr float kPreviewEdge = 96.0f; // 播放预览帧边长
constexpr float kStripEdge = 56.0f;   // 胶片带帧缩略图边长（T3b-7）
constexpr float kRowPreview = 28.0f;  // 选中帧编辑行缩略图边长

/// 帧引用可解析？（判据与 BuildPlayClipCache 同源——面板拦在保存前）
bool FrameResolvable(const AssetDatabase& db, const ClipFrame& f, const AssetEntry*& out) {
    out = db.FindByGuid(f.sheetGuid);
    if (!out || out->missing || out->type != AssetType::Sprite) return false;
    if (out->Sliced()) return f.cell < out->sliceCount;
    return f.cell == 0 && out->spriteId != 0; // T3b-1：整图引用（一帧一图）
}

/// LoopMode 名表（Animator2D.loop 同值域；与 ComponentCatalog kLoopModeNames 对应）
const char* const kLoopNames[] = {"Once", "Loop", "PingPong"};

ImU32 WithAlpha(const ImVec4& c, float a) {
    const ImU32 u = ImGui::GetColorU32(c);
    return (u & 0x00FFFFFFu) | (ImU32)((int)(a * 255.0f) << 24);
}
} // namespace

// ---------------------------------------------------------------- 装载 ----

void AnimationPanel::LoadFrom(const AssetDatabase& db, const AssetEntry& e) {
    loadedGuid_ = e.guid;
    loadedHash_ = e.hash;
    previewing_ = false;
    previewFrame_ = 0;
    selFrame_ = -1;
    selSet_.clear();
    dirty_ = false;
    std::ifstream f(db.AbsolutePath(e), std::ios::binary);
    if (f) {
        std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        edit_ = ParseClipJson(text);
    } else {
        edit_ = ClipData{};
        edit_.error = "读档失败：" + e.relPath;
    }
    if (edit_.name.empty()) edit_.name = std::filesystem::path(e.relPath).stem().string();
    fpsI_ = edit_.ok ? std::clamp((int)edit_.fps, 1, 60) : 8;
}

// ---------------------------------------------------------------- 预览 ----

void AnimationPanel::DrawCellImage(EditorApp& app, const AssetEntry* sheet, uint32_t cell,
                                   float edge) {
    // 切片号 → 页缩略图 UV（行优先换算）；未切片 = 全幅（T3b-1 整图引用）
    const bool sliced = sheet && sheet->Sliced();
    const bool valid = sheet && !sheet->missing && sheet->type == AssetType::Sprite &&
                       (sliced ? cell < sheet->sliceCount : cell == 0);
    void* tex = valid ? app.AssetGpu().Thumbnail(sheet->guid) : nullptr;
    uint32_t w = 0, h = 0;
    if (tex && valid && app.AssetGpu().PageInfo(sheet->guid, w, h) && w && h) {
        float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
        uint32_t cw = w, ch = h;
        if (sliced) {
            const uint32_t cols = sheet->gridCols ? sheet->gridCols : 1;
            const uint32_t rows = sheet->gridRows ? sheet->gridRows : 1;
            cw = sheet->cellW ? sheet->cellW : w / cols;
            ch = sheet->cellH ? sheet->cellH : h / rows;
            const uint32_t cx = cell % cols, cy = cell / cols;
            u0 = (float)(cx * cw) / (float)w;
            v0 = (float)(cy * ch) / (float)h;
            u1 = (float)((cx + 1) * cw) / (float)w;
            v1 = (float)((cy + 1) * ch) / (float)h;
        }
        ImGui::Image(tex, ImVec2(edge, edge * (float)ch / (float)cw), ImVec2(u0, v0),
                     ImVec2(u1, v1));
        return;
    }
    ImGui::PushStyleColor(ImGuiCol_Button, theme::kPlayStop);
    ImGui::Button(valid ? "?" : "×", ImVec2(edge, edge));
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", valid ? "页未导入（缩略图缺失）"
                                      : "帧悬空：sheet 缺失/未切片且 cell≠0/cell 越界");
}

// ---------------------------------------------------------------- 保存 ----

bool AnimationPanel::TrySave(EditorApp& app, const AssetEntry& e) {
    // 校验三关：可解析态 / 非空帧表 / 全帧可解析（判据同 BuildPlayClipCache——
    // 运行时对坏 clip 是整条跳过红字，编辑器拦在写盘前保住既有资产可用性）
    if (!edit_.ok) {
        saveMsg_ = "× 坏档不可保存：" + edit_.error;
        saveOk_ = false;
        return false;
    }
    if (edit_.frames.empty()) {
        saveMsg_ = "× 无帧：至少加一帧再保存（空帧 clip 进 Play 会被跳过）";
        saveOk_ = false;
        return false;
    }
    for (size_t i = 0; i < edit_.frames.size(); ++i) {
        const AssetEntry* sheet = nullptr;
        if (!FrameResolvable(app.Ctx().Assets(), edit_.frames[i], sheet)) {
            saveMsg_ = "× 帧 " + std::to_string(i) +
                       " 悬空（sheet 缺失/未切片且 cell≠0/cell 越界）";
            saveOk_ = false;
            return false;
        }
    }
    edit_.fps = (float)fpsI_;
    const std::string json = ClipToJson(edit_);
    const std::string abs = app.Ctx().Assets().AbsolutePath(e);
    // 注意：Rescan 会重建 entries_——调用方在 TrySave 后不得再用本引用
    if (json.empty() || !WriteFileAtomic(abs, json + "\n")) {
        saveMsg_ = "× 写盘失败（磁盘满/权限？）：" + abs;
        saveOk_ = false;
        return false;
    }
    app.RescanAssets();
    dirty_ = false;
    saveOk_ = true;
    saveMsg_ = "√ 已保存——Enter Play 后生效（进行中的局用旧快照）";
    return true;
}

bool AnimationPanel::TryCreateClip(EditorApp& app, const ClipData& c, const std::string& dir,
                                   std::string& err) {
    // 新建落盘共用（向导三通道）：dir 相对项目根（"Assets" = 根）；名字校验 +
    // 撞路径拒 + 原子写 + Rescan 反查 guid → SetTarget（guid 由 .meta 补齐派发）
    AssetDatabase& db = app.Ctx().Assets();
    if (c.name.empty() || c.name.find('/') != std::string::npos ||
        c.name.find('\\') != std::string::npos || c.name.find("..") != std::string::npos) {
        err = "名字非法（空/路径分隔/..）";
        return false;
    }
    std::string d = dir.empty() ? "Assets" : dir;
    while (d.size() > 1 && d.back() == '/') d.pop_back();
    const std::string rel = d == "Assets" ? "Assets/" + c.name + ".clip"
                                          : d + "/" + c.name + ".clip";
    if (db.FindByPath(rel)) {
        err = "已存在：" + rel;
        return false;
    }
    const std::string abs = db.ProjectRoot() + "/" + rel;
    if (!WriteFileAtomic(abs, ClipToJson(c) + "\n")) {
        err = "写盘失败：" + abs;
        return false;
    }
    app.RescanAssets();
    if (const AssetEntry* e = db.FindByPath(rel)) SetTarget(e->guid);
    return true;
}

// -------------------------------------------------------------- 胶片带 ----

void AnimationPanel::DrawFilmstrip(EditorApp& app) {
    // T3b-7：横排 wrap 缩略图带——帧号角标 / 单击选中 / Ctrl 多选 / 拖拽重排
    //（竖排行对 18+ 帧素材不可用——用户实测反馈）
    AssetDatabase& db = app.Ctx().Assets();
    const int n = (int)edit_.frames.size();
    if (n == 0) {
        ImGui::TextDisabled("（无帧：「+ 加帧」/「从精灵表…」框选 / 从文件夹创建）");
        return;
    }
    const float avail = ImGui::GetContentRegionAvail().x;
    const ImGuiStyle& st = ImGui::GetStyle();
    const float step = kStripEdge + st.FramePadding.x * 2.0f + st.ItemSpacing.x;
    const int cols = std::max(1, (int)((avail + st.ItemSpacing.x) / step));
    int col = 0;
    for (int i = 0; i < n; ++i) {
        ImGui::PushID(i);
        if (col++ > 0) ImGui::SameLine();
        ImGui::BeginGroup();
        const AssetEntry* sheet = nullptr;
        const bool ok = FrameResolvable(db, edit_.frames[(size_t)i], sheet);
        const bool selected =
            i == selFrame_ || std::find(selSet_.begin(), selSet_.end(), i) != selSet_.end();
        DrawCellImage(app, ok ? sheet : nullptr, edit_.frames[(size_t)i].cell, kStripEdge);
        const ImVec2 rmin = ImGui::GetItemRectMin(), rmax = ImGui::GetItemRectMax();
        if (selected) // 选中描边（当前帧 = 亮色加粗，多选 = 暗色细框）
            ImGui::GetWindowDrawList()->AddRect(
                rmin - ImVec2(1, 1), rmax + ImVec2(1, 1),
                ImGui::GetColorU32(i == selFrame_ ? theme::kAccent : theme::kAccentDim),
                0.0f, 0, i == selFrame_ ? 2.5f : 1.5f);
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            previewFrame_ = i; // 双击 = 预览跳帧
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            selFrame_ = i;
            if (ImGui::GetIO().KeyCtrl) { // Ctrl = 多选切换
                auto it = std::find(selSet_.begin(), selSet_.end(), i);
                if (it != selSet_.end())
                    selSet_.erase(it);
                else
                    selSet_.push_back(i);
            } else {
                selSet_.clear();
                selSet_.push_back(i);
            }
        }
        // 拖拽重排：拖到目标帧前插入（src<dst 插入位回退一格）
        if (ImGui::BeginDragDropSource()) {
            ImGui::SetDragDropPayload("LemonAnimFrame", &i, sizeof(int));
            ImGui::Text("帧 %d", i);
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* pay = ImGui::AcceptDragDropPayload("LemonAnimFrame")) {
                const int src = *(const int*)pay->Data;
                if (src != i) {
                    const ClipFrame f = edit_.frames[(size_t)src];
                    edit_.frames.erase(edit_.frames.begin() + src);
                    edit_.frames.insert(edit_.frames.begin() + i, f);
                    selFrame_ = src < i ? i - 1 : i;
                    selSet_.clear();
                    dirty_ = true;
                    saveMsg_.clear();
                }
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::TextDisabled("%02d", i);
        ImGui::EndGroup();
        ImGui::PopID();
        if (col >= cols) col = 0;
    }
}

void AnimationPanel::DrawSelectedFrameRow(EditorApp& app) {
    // T3b-7：选中帧编辑行（胶片带下方）——sheet 槽（全部 sprite，未切片标"整图"）
    // + cell（整图锁 0）+ 删除。拖 Assets 精灵进 combo = 换 sheet。
    AssetDatabase& db = app.Ctx().Assets();
    if (selFrame_ < 0 || selFrame_ >= (int)edit_.frames.size()) {
        ImGui::TextDisabled("单击胶片带帧以编辑（Ctrl 多选 / 拖拽重排）");
        return;
    }
    ClipFrame& fr = edit_.frames[(size_t)selFrame_];
    const AssetEntry* sheet = nullptr;
    const bool ok = FrameResolvable(db, fr, sheet);

    ImGui::TextDisabled("帧 %d/%d", selFrame_ + 1, (int)edit_.frames.size());
    ImGui::SameLine();
    DrawCellImage(app, ok ? sheet : nullptr, fr.cell, kRowPreview);
    ImGui::SameLine();

    char lab[176];
    if (sheet && !sheet->missing) {
        if (sheet->Sliced())
            std::snprintf(lab, sizeof(lab), "%s（%u 帧）", sheet->relPath.c_str(),
                          sheet->sliceCount);
        else
            std::snprintf(lab, sizeof(lab), "%s（整图）", sheet->relPath.c_str());
    } else if (fr.sheetGuid != 0) {
        std::snprintf(lab, sizeof(lab), "悬空 %016llx", (unsigned long long)fr.sheetGuid);
    } else {
        std::snprintf(lab, sizeof(lab), "(无 sheet)");
    }
    ImGui::SetNextItemWidth(220);
    if (ImGui::BeginCombo("##sheet", lab)) {
        for (const auto& e : db.Entries()) {
            if (e.type != AssetType::Sprite || e.missing) continue;
            char item[192];
            std::snprintf(item, sizeof(item), "%s%s%s", e.guid == fr.sheetGuid ? "√ " : "",
                          e.relPath.c_str(), e.Sliced() ? "" : "（整图）");
            if (ImGui::Selectable(item, e.guid == fr.sheetGuid)) {
                fr.sheetGuid = e.guid;
                fr.cell = e.Sliced() ? std::min(fr.cell, e.sliceCount - 1) : 0;
                dirty_ = true;
                saveMsg_.clear();
            }
        }
        ImGui::EndCombo();
    }
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* pay = ImGui::AcceptDragDropPayload("LemonAsset")) {
            AssetDragPayload d{};
            std::memcpy(&d, pay->Data, sizeof(d));
            if (d.kind == 0) { // sprite（AssetBrowser KindOf）
                fr.sheetGuid = d.guid;
                fr.cell = 0;
                dirty_ = true;
                saveMsg_.clear();
            }
        }
        ImGui::EndDragDropTarget();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(72);
    int cell = (int)fr.cell;
    const int cellMax = sheet && sheet->Sliced() ? (int)sheet->sliceCount - 1 : 0;
    if (ImGui::DragInt("##cell", &cell, 1.0f, 0, cellMax)) {
        fr.cell = (uint32_t)std::clamp(cell, 0, cellMax);
        dirty_ = true;
        saveMsg_.clear();
    }
    if (ImGui::IsItemHovered() && cellMax == 0)
        ImGui::SetTooltip("整图 sheet：cell 锁 0（一帧一图）");
    ImGui::SameLine();
    if (ImGui::SmallButton("删除此帧")) {
        edit_.frames.erase(edit_.frames.begin() + selFrame_);
        if (selFrame_ >= (int)edit_.frames.size()) selFrame_ = (int)edit_.frames.size() - 1;
        selSet_.clear();
        dirty_ = true;
        saveMsg_.clear();
    }
    ImGui::SameLine();
    if (selSet_.size() > 1 && ImGui::SmallButton("删除选中")) {
        std::vector<int> ord = selSet_;
        std::sort(ord.rbegin(), ord.rend()); // 降序删防索引漂移
        for (int idx : ord)
            if (idx >= 0 && idx < (int)edit_.frames.size())
                edit_.frames.erase(edit_.frames.begin() + idx);
        selFrame_ = -1;
        selSet_.clear();
        dirty_ = true;
        saveMsg_.clear();
    }
}

// -------------------------------------------------------- 从精灵表加帧 ----

void AnimationPanel::DrawSheetPicker(EditorApp& app) {
    // T3b-4：选图 →（未切片则配网格 → Apply 写 meta）→ 网格预览拖框选 →
    // 追加/替换帧（编辑态，pickCreate_=false）或选定区间（向导创建，=true）。
    // 注意：Apply 切片会 Rescan 重建 entries_——本帧早退，下帧按新状态重画。
    if (!pickOpen_) return;
    AssetDatabase& db = app.Ctx().Assets();
    const char* title = pickCreate_ ? "从精灵表选定帧" : "从精灵表加帧";
    ImGui::OpenPopup(title);
    ImGui::SetNextWindowSize(ImVec2(620.0f, 600.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_NoSavedSettings)) return;
    if (ImGui::Button("关闭")) {
        pickOpen_ = false;
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    // ---- sheet 选择（全部 sprite；未切片可就地配切片）----
    const AssetEntry* sh = db.FindByGuid(pickGuid_);
    char lab[176];
    if (sh && !sh->missing && sh->type == AssetType::Sprite)
        std::snprintf(lab, sizeof(lab), "%s%s", sh->relPath.c_str(),
                      sh->Sliced() ? "" : "（未切片）");
    else
        std::snprintf(lab, sizeof(lab), "(选择图片)");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(300);
    if (ImGui::BeginCombo("##pickimg", lab)) {
        for (const auto& e : db.Entries()) {
            if (e.type != AssetType::Sprite || e.missing) continue;
            char item[192];
            std::snprintf(item, sizeof(item), "%s%s%s", e.guid == pickGuid_ ? "√ " : "",
                          e.relPath.c_str(), e.Sliced() ? "" : "（未切片）");
            if (ImGui::Selectable(item, e.guid == pickGuid_)) {
                pickGuid_ = e.guid;
                pickHasRect_ = false;
            }
        }
        ImGui::EndCombo();
    }
    if (!sh || sh->missing || sh->type != AssetType::Sprite) {
        ImGui::Spacing();
        ImGui::TextDisabled("选择一张图片开始");
        ImGui::EndPopup();
        return;
    }

    // ---- 未切片：网格配置（写 .meta importer 段，全项目生效）或整图作 1 帧 ----
    if (!sh->Sliced()) {
        uint32_t w = 0, h = 0;
        const bool dims = app.AssetGpu().PageInfo(sh->guid, w, h) && w && h;
        ImGui::SeparatorText("切片配置（写入图片 .meta —— 全项目生效）");
        ImGui::RadioButton("按格数（列 × 行）", &pickInput_, 0);
        ImGui::SameLine();
        ImGui::RadioButton("按像素（cell 尺寸）", &pickInput_, 1);
        if (pickInput_ == 0) {
            ImGui::DragInt("列数", &pickCols_, 1.0f, 1, 256);
            ImGui::SameLine();
            ImGui::DragInt("行数", &pickRows_, 1.0f, 1, 256);
            if (dims)
                ImGui::TextDisabled("cell ≈ %u × %u（按图片尺寸推算）",
                                    w / std::max(1, pickCols_), h / std::max(1, pickRows_));
        } else {
            ImGui::DragInt("cell 宽", &pickCellW_, 1.0f, 1, 4096);
            ImGui::SameLine();
            ImGui::DragInt("cell 高", &pickCellH_, 1.0f, 1, 4096);
            if (dims)
                ImGui::TextDisabled("格数 ≈ %d × %d（按图片尺寸推算）",
                                    (int)(w / std::max(1, pickCellW_)),
                                    (int)(h / std::max(1, pickCellH_)));
        }
        if (ImGui::Button("应用切片")) {
            if (!dims) {
                ImGui::TextColored(theme::kTextError, "页未导入：无法推算尺寸（重扫后重试）");
            } else {
                uint32_t cw = (uint32_t)pickCellW_, ch = (uint32_t)pickCellH_;
                uint32_t cc = (uint32_t)pickCols_, rr = (uint32_t)pickRows_;
                if (pickInput_ == 0) {
                    cw = w / std::max(1, pickCols_);
                    ch = h / std::max(1, pickRows_);
                } else {
                    cc = w / std::max(1, pickCellW_);
                    rr = h / std::max(1, pickCellH_);
                }
                if (AssetEntry* m = db.FindByGuid(pickGuid_)) { // 可变版
                    if (db.SetGridSlice(*m, cw, ch, cc, rr)) {
                        app.RescanAssets();
                        pickHasRect_ = false;
                        ImGui::EndPopup(); // Rescan 重建 entries_——本帧早退
                        return;
                    }
                    ImGui::TextColored(theme::kTextError, "写 .meta 失败（权限/磁盘？）");
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("整图作 1 帧（不切片）")) {
            if (pickCreate_) {
                wizFrames_.assign(1, {pickGuid_, 0});
                wizPicked_ = true;
            } else {
                edit_.frames.push_back({pickGuid_, 0});
                dirty_ = true;
                saveMsg_.clear();
            }
            pickOpen_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
        return;
    }

    // ---- 已切片：网格预览 + 拖框选（Godot SpriteFrames 式）----
    void* tex = app.AssetGpu().Thumbnail(sh->guid);
    uint32_t w = 0, h = 0;
    if (!tex || !app.AssetGpu().PageInfo(sh->guid, w, h) || !w || !h) {
        ImGui::TextColored(theme::kTextError, "页未导入（缩略图缺失）");
        ImGui::EndPopup();
        return;
    }
    const uint32_t cols = sh->gridCols ? sh->gridCols : 1;
    const uint32_t rows = sh->gridRows ? sh->gridRows : 1;
    const float dispW = std::min(ImGui::GetContentRegionAvail().x, 520.0f);
    const float dispH = dispW * (float)h / (float)w;
    ImGui::Image(tex, ImVec2(dispW, dispH));
    const ImVec2 pmin = ImGui::GetItemRectMin();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 gridCol = WithAlpha(theme::kTextDim, 0.45f);
    for (uint32_t c = 0; c <= cols; ++c) {
        const float x = pmin.x + dispW * (float)c / (float)cols;
        dl->AddLine(ImVec2(x, pmin.y), ImVec2(x, pmin.y + dispH), gridCol);
    }
    for (uint32_t r = 0; r <= rows; ++r) {
        const float y = pmin.y + dispH * (float)r / (float)rows;
        dl->AddLine(ImVec2(pmin.x, y), ImVec2(pmin.x + dispW, y), gridCol);
    }
    // 鼠标 → 格（图像外钳到边界格）
    const auto cellAt = [&](const ImVec2& m) {
        const int r = std::clamp((int)((m.y - pmin.y) * (float)rows / dispH), 0, (int)rows - 1);
        const int c = std::clamp((int)((m.x - pmin.x) * (float)cols / dispW), 0, (int)cols - 1);
        return std::make_pair(r, c);
    };
    const bool hov = ImGui::IsItemHovered();
    const ImVec2& mouse = ImGui::GetIO().MousePos;
    if (hov && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        pickDrag_ = true;
        pickHasRect_ = true;
        const auto [r0, c0] = cellAt(mouse);
        pickR0_ = pickR1_ = r0;
        pickC0_ = pickC1_ = c0;
    }
    if (pickDrag_) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            const auto [r1, c1] = cellAt(mouse);
            pickR1_ = r1;
            pickC1_ = c1;
        } else {
            pickDrag_ = false;
        }
    }
    int ra = pickR0_, rb = pickR1_, ca = pickC0_, cb = pickC1_;
    if (ra > rb) std::swap(ra, rb);
    if (ca > cb) std::swap(ca, cb);
    if (pickHasRect_) {
        const ImVec2 q0 = pmin + ImVec2(dispW * (float)ca / (float)cols,
                                        dispH * (float)ra / (float)rows);
        const ImVec2 q1 = pmin + ImVec2(dispW * (float)(cb + 1) / (float)cols,
                                        dispH * (float)(rb + 1) / (float)rows);
        dl->AddRectFilled(q0, q1, WithAlpha(theme::kAccent, 0.30f));
        dl->AddRect(q0, q1, ImGui::GetColorU32(theme::kAccent), 0.0f, 0, 2.0f);
    }
    const int cnt = (rb - ra + 1) * (cb - ca + 1);
    ImGui::Text("已选 %d 帧（%d 列 × %d 行区间，行优先）", cnt, cb - ca + 1, rb - ra + 1);
    if (cnt <= 0) {
        ImGui::EndPopup();
        return;
    }
    const auto buildFrames = [&]() {
        std::vector<ClipFrame> out;
        for (int r = ra; r <= rb; ++r)
            for (int c = ca; c <= cb; ++c)
                out.push_back({pickGuid_, (uint32_t)(r * (int)cols + c)});
        return out;
    };
    if (pickCreate_) {
        if (ImGui::Button("选定区间")) {
            wizFrames_ = buildFrames();
            wizPicked_ = true;
            pickOpen_ = false;
            ImGui::CloseCurrentPopup();
        }
    } else {
        if (ImGui::Button("追加帧")) {
            std::vector<ClipFrame> add = buildFrames();
            edit_.frames.insert(edit_.frames.end(), add.begin(), add.end());
            dirty_ = true;
            saveMsg_.clear();
            pickOpen_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("替换帧表")) {
            edit_.frames = buildFrames();
            selFrame_ = -1;
            selSet_.clear();
            dirty_ = true;
            saveMsg_.clear();
            pickOpen_ = false;
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::EndPopup();
}

// ------------------------------------------------------------ 新建向导 ----

void AnimationPanel::StartCreateFromFolder(const std::string& relDir,
                                           const std::string& destDir) {
    wizOpen_ = true;
    wizTab_ = 0;
    wizDir_ = relDir.empty() || relDir == "Assets" ? "Assets" : relDir;
    const size_t slash = wizDir_.find_last_of('/');
    wizName_ = slash == std::string::npos ? wizDir_ : wizDir_.substr(slash + 1);
    if (wizName_.empty() || wizName_ == "Assets") wizName_ = "new-clip";
    wizPath_ = destDir.empty() ? "Assets" : destDir;
    wizErr_.clear();
    wizPicked_ = false;
    wizFrames_.clear();
}

void AnimationPanel::DrawWizard(EditorApp& app) {
    // T3b-5/6：三通道向导——从文件夹（多单图）/ 从精灵表（框选）/ 空白。
    // 落点默认 = AssetBrowser 当前目录（决策 4：Unity 式跟随浏览位置）。
    if (!wizOpen_) return;
    AssetDatabase& db = app.Ctx().Assets();
    ImGui::OpenPopup("新建动画");
    ImGui::SetNextWindowSize(ImVec2(640.0f, 540.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("新建动画", nullptr, ImGuiWindowFlags_NoSavedSettings)) return;
    if (ImGui::Button("取消") ||
        (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !ImGui::IsAnyItemActive())) {
        wizOpen_ = false;
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    // 文件夹页素材收集（tab 0 也要给"创建"按钮用）
    std::vector<const AssetEntry*> imgs;
    if (wizTab_ == 0)
        for (const AssetEntry* e : db.EntriesInDir(wizDir_.empty() ? "Assets" : wizDir_))
            if (e->type == AssetType::Sprite && !e->missing) imgs.push_back(e);

    if (ImGui::BeginTabBar("##wizsrc")) {
        if (ImGui::BeginTabItem("从文件夹")) {
            wizTab_ = 0;
            ImGui::TextDisabled("文件夹直下的图片按文件名序逐张入帧（一帧一图，整图引用）");
            char dlab[160];
            std::snprintf(dlab, sizeof(dlab), "%s", wizDir_.empty() ? "Assets" : wizDir_.c_str());
            ImGui::SetNextItemWidth(280);
            if (ImGui::BeginCombo("##wizdir", dlab)) {
                for (const std::string& d : db.Directories()) {
                    if (d.empty()) continue; // 项目根散文件目录不参与
                    if (ImGui::Selectable(d.c_str(), d == wizDir_)) {
                        wizDir_ = d;
                        const size_t s = d.find_last_of('/');
                        wizName_ = s == std::string::npos ? d : d.substr(s + 1);
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::Text("将入帧 %zu 张（文件名序）", imgs.size());
            // 前 12 张预览带
            const float avail = ImGui::GetContentRegionAvail().x;
            const float step = 40.0f + ImGui::GetStyle().ItemSpacing.x;
            const int cols = std::max(1, (int)(avail / step));
            int col = 0;
            for (size_t i = 0; i < imgs.size() && i < 12; ++i) {
                if (col++ > 0) ImGui::SameLine();
                DrawCellImage(app, imgs[i], 0, 40.0f);
                if (col >= cols) col = 0;
            }
            if (imgs.empty())
                ImGui::TextColored(theme::kTextError, "该文件夹没有图片");
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("从精灵表")) {
            wizTab_ = 1;
            ImGui::TextDisabled("在精灵表上拖框选帧区间（未切片可就地配切片）");
            if (ImGui::Button("选择帧区间…")) {
                pickOpen_ = true;
                pickCreate_ = true;
                pickHasRect_ = false;
                if (pickGuid_ == 0)
                    for (const auto& e : db.Entries())
                        if (e.type == AssetType::Sprite && !e.missing && e.Sliced()) {
                            pickGuid_ = e.guid;
                            break;
                        }
            }
            if (wizPicked_) {
                ImGui::SameLine();
                ImGui::TextDisabled("已选 %zu 帧", wizFrames_.size());
                ImGui::SameLine();
                if (ImGui::SmallButton("清除")) {
                    wizPicked_ = false;
                    wizFrames_.clear();
                }
            } else {
                ImGui::SameLine();
                ImGui::TextDisabled("（未选定）");
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("空白")) {
            wizTab_ = 2;
            ImGui::TextDisabled("建空 clip 后在面板手动加帧（保存需 ≥1 帧）");
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::SeparatorText("动画参数");
    ImGui::InputText("名称", &wizName_);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(72);
    ImGui::DragInt("fps##wiz", &wizFps_, 1.0f, 1, 60);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(110);
    ImGui::Combo("循环##wiz", &wizLoop_, kLoopNames, 3);
    ImGui::InputTextWithHint("落点目录", "Assets/…（默认 = 资产浏览器当前目录）", &wizPath_);
    if (!wizErr_.empty()) ImGui::TextColored(theme::kTextError, "%s", wizErr_.c_str());

    if (ImGui::Button("创建", ImVec2(140, 0)) ||
        ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
        ClipData cd;
        cd.ok = true;
        cd.name = wizName_;
        cd.fps = (float)wizFps_;
        cd.loopMode = wizLoop_;
        bool go = true;
        if (wizTab_ == 0) {
            if (imgs.empty()) {
                wizErr_ = "文件夹无图片";
                go = false;
            }
            for (const AssetEntry* e : imgs) cd.frames.push_back({e->guid, 0});
        } else if (wizTab_ == 1) {
            if (!wizPicked_ || wizFrames_.empty()) {
                wizErr_ = "先「选择帧区间…」";
                go = false;
            } else
                cd.frames = wizFrames_;
        }
        if (go && TryCreateClip(app, cd, wizPath_, wizErr_)) {
            wizOpen_ = false;
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::EndPopup();
}

// ---------------------------------------------------------------- 主面 ----

void AnimationPanel::OnGui(EditorApp& app) {
    bool winOpen = true;
    if (!ImGui::Begin(Name(), &winOpen, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        if (!winOpen) app.ClosePanel(Name()); // × 关面板（T3b-8）
        return;
    }
    if (!winOpen) app.ClosePanel(Name());

    EditorContext& ctx = app.Ctx();
    AssetDatabase& db = ctx.Assets();
    const bool ro = ctx.Playing(); // 快照语义：进行中的局不吃改档 → 面板只读

    // ---- 目标行：clip 下拉 + 新建向导 ----
    const AssetEntry* target = db.FindByGuid(targetGuid_);
    char label[128];
    if (target && !target->missing && target->type == AssetType::Clip)
        std::snprintf(label, sizeof(label), "%s", target->relPath.c_str());
    else
        std::snprintf(label, sizeof(label), "(选择 .clip)");
    ImGui::SetNextItemWidth(240);
    if (ImGui::BeginCombo("##cliptarget", label)) {
        for (const auto& e : db.Entries()) {
            if (e.type != AssetType::Clip || e.missing) continue;
            char item[192];
            std::snprintf(item, sizeof(item), "%s%s", e.guid == targetGuid_ ? "√ " : "",
                          e.relPath.c_str());
            if (ImGui::Selectable(item, e.guid == targetGuid_)) SetTarget(e.guid);
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ro) ImGui::BeginDisabled();
    if (ImGui::Button("新建…")) {
        wizOpen_ = true;
        wizTab_ = 2;
        wizPath_ = app.AssetBrowserDir();
        if (wizName_.empty()) wizName_ = "new-clip";
        wizErr_.clear();
        wizPicked_ = false;
        wizFrames_.clear();
        if (wizDir_.empty()) wizDir_ = "Assets";
    }
    if (ro) ImGui::EndDisabled();
    DrawWizard(app);
    DrawSheetPicker(app);

    target = db.FindByGuid(targetGuid_); // 新建可能刚换目标
    if (!target || target->missing || target->type != AssetType::Clip) {
        ImGui::TextDisabled("未选目标：下拉选择，或双击 Assets 中的 .clip 资产");
        ImGui::End();
        return;
    }
    if (loadedGuid_ != target->guid || loadedHash_ != target->hash) LoadFrom(db, *target);

    if (!edit_.ok) {
        ImGui::TextColored(theme::kTextError, "clip 解析失败：%s", edit_.error.c_str());
        ImGui::TextDisabled("外部修复 JSON 后点 Assets「重扫」或重选目标");
        ImGui::End();
        return;
    }
    if (ro)
        ImGui::TextColored(theme::kTextWarn,
                           "Play 进行中：面板只读（当前局用进 Play 时刻快照，改动下一局生效）");

    // ---- 头部：name / fps / 循环模式 ----
    if (ro) ImGui::BeginDisabled();
    if (ImGui::InputText("名称", &edit_.name)) {
        dirty_ = true;
        saveMsg_.clear();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(64);
    if (ImGui::DragInt("fps", &fpsI_, 1.0f, 1, 60)) {
        dirty_ = true;
        saveMsg_.clear();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(110);
    if (ImGui::Combo("循环", &edit_.loopMode, kLoopNames, 3)) {
        dirty_ = true;
        saveMsg_.clear();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Once 播完钳末帧 / Loop 回绕 / PingPong 往返（0..n-1..0）\n"
                          "档面 = 创建默认；实体 Inspector 的 LoopMode = 运行时权威");
    if (ro) ImGui::EndDisabled();

    // ---- 播放预览（只读可视化，Play 态照常可用）----
    const int n = (int)edit_.frames.size();
    if (ImGui::Button(previewing_ ? "暂停" : "播放")) {
        previewing_ = !previewing_;
        previewT0_ = ImGui::GetTime();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("回起点")) {
        previewFrame_ = 0;
        previewing_ = false;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("上一帧")) {
        if (n) previewFrame_ = (previewFrame_ + n - 1) % n;
        previewing_ = false;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("下一帧")) {
        if (n) previewFrame_ = (previewFrame_ + 1) % n;
        previewing_ = false;
    }
    int shown = previewFrame_;
    if (previewing_ && n > 0) {
        const uint64_t idx = (uint64_t)((ImGui::GetTime() - previewT0_) * (double)fpsI_);
        if (edit_.loopMode == 2 && n > 1) { // PingPong（AnimatorSystem 同映射）
            const uint64_t period = 2 * (uint64_t)(n - 1);
            const uint64_t pos = idx % period;
            shown = (int)(pos < (uint64_t)n ? pos : period - pos);
        } else if (edit_.loopMode == 1) {
            shown = (int)(idx % (uint64_t)n);
        } else {
            shown = (int)std::min(idx, (uint64_t)(n - 1));
        }
        previewFrame_ = shown;
    }
    shown = n ? std::clamp(shown, 0, n - 1) : 0;

    const AssetEntry* shownSheet = nullptr;
    if (n) FrameResolvable(db, edit_.frames[(size_t)shown], shownSheet);
    DrawCellImage(app, shownSheet, n ? edit_.frames[(size_t)shown].cell : 0, kPreviewEdge);
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::Text("帧 %d / %d", shown + (n ? 1 : 0), n);
    ImGui::TextDisabled("%.3fs/帧 · %s", 1.0f / std::max(fpsI_, 1),
                        edit_.loopMode == 2   ? "PingPong 往返"
                        : edit_.loopMode == 1 ? "循环"
                                              : "单次（播完钳末帧）");
    if (dirty_) ImGui::TextColored(theme::kTextWarn, "有未保存改动");
    ImGui::EndGroup();

    // ---- 胶片带 + 选中帧编辑行（T3b-7）----
    ImGui::Separator();
    ImGui::TextDisabled("帧胶片带（单击选中 / Ctrl 多选 / 拖拽重排 / 双击预览跳帧）");
    ImGui::BeginChild("strip", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 3.0f),
                      ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_HorizontalScrollbar);
    DrawFilmstrip(app);
    ImGui::EndChild();
    if (!ro) ImGui::BeginDisabled();
    DrawSelectedFrameRow(app);
    if (!ro) ImGui::EndDisabled();

    // ---- 底部：加帧 / 从精灵表 / 保存（保存 = 本帧对 target 的最后一次解引用——
    // TrySave 内 Rescan 重建 entries_，此后不得再用）----
    if (ro) ImGui::BeginDisabled();
    if (ImGui::Button("+ 加帧")) {
        ClipFrame nf{};
        if (!edit_.frames.empty()) {
            nf = edit_.frames.back(); // 承接上行 sheet/cell
        } else {
            for (const auto& e : db.Entries()) // 空表起步：首个精灵（整图）
                if (e.type == AssetType::Sprite && !e.missing && e.spriteId != 0) {
                    nf.sheetGuid = e.guid;
                    break;
                }
        }
        edit_.frames.push_back(nf);
        dirty_ = true;
        saveMsg_.clear();
    }
    ImGui::SameLine();
    if (ImGui::Button("从精灵表…")) {
        pickOpen_ = true;
        pickCreate_ = false;
        pickHasRect_ = false;
        if (pickGuid_ == 0 && !edit_.frames.empty()) pickGuid_ = edit_.frames.back().sheetGuid;
    }
    ImGui::SameLine();
    if (ImGui::Button("保存")) TrySave(app, *target);
    if (ro) ImGui::EndDisabled();
    if (!saveMsg_.empty())
        ImGui::TextColored(saveOk_ ? theme::kTextOk : theme::kTextError, "%s",
                           saveMsg_.c_str());
    else
        ImGui::TextDisabled("保存 = 直写资产文件（Enter Play 后生效；不走场景 undo）");
    ImGui::End();
}

} // namespace lemon::editor
