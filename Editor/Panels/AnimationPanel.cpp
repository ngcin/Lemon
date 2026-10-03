// Lemon 编辑器 — Animation 面板（M6a 批② T3/T3b/T3c + 工作台 v3 交互重设计；
// 05 §7 内容编辑器三件套之一）
// v3（用户三轮实测反馈收敛，Godot SpriteFrames 参照——截图 docs/animation/）：
//   主从布局——左列段清单（集名 + [新动作/改名/复制/移除] 工具条 + 搜索 +
//   inline 新建输入：**建段只输入名字**（如 idle），选帧回右区做——旧版大表单
//   把选图塞在下方、与段编辑割离，是主痛点）+ 右区（帧操作/播放工具条 ·
//   大预览 fit≤512 · 胶片带——底部属性行 T3-UX8 退役）。
//   帧操作键盘化：←/→ 移选 · Ctrl+←/→ 换序 · Del 删 · Ctrl+D 复制 · Space 播放
//   （窗口持焦点经 CapturesGlobalKeys 仲裁，EditorApp 跳过实体级同名键——防
//   "删帧顺手删实体"双触发）。拖 Assets 精灵：入胶片带格 = 换图 / 入带尾或
//   预览 = 加帧 / 入 sheet 槽 = 换 sheet。从精灵表对话框 v2 = 全选/清空 +
//   框选(行优先)/点选(按点击序 = Godot As Selected) + Ctrl+滚轮缩放 + 动态
//   按钮文案；v2.1（三图优化点③）右栏「选择」区带已选计数——选择操作处
//   即时反馈；sheet combo 项带缩略图（纯路径列表翻找是旧痛点）。
// .anim 三条生产通道（T3b：文件夹多单图/网格切片/框选建帧）+ 集容器（T3c：
// .override = Unity .controller 壳，段身份 = 文件 GUID，集=显式成员关系）数据模型
// 不变。LoopMode 三模式（Once/Loop/PingPong）。播放预览 = 编辑器时钟推进（非
// 确定无妨，不进模拟）；暂停态预览跟随选中帧。EnterPlay 快照语义 = 保存后
// 下次 Enter Play 生效；Play 中只读（X 关面板同步 PanelRegistry——Window 菜单
// 可重开）。帧可解析判据与运行时 BuildPlayClipCache 同源。
#define IMGUI_DEFINE_MATH_OPERATORS // ImVec2 ± 运算（须先于一切 imgui.h 包含点）
#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <unordered_set>

#include "App/EditorApp.h"
#include "Assets/AssetDatabase.h"
#include "Core/Log.h"
#include "EditorContext.h"
#include "Panels/BuiltInPanels.h"
#include "Tooling/Icons.h"
#include "Tooling/TestHooks.h"
#include "Tooling/Theme.h"
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"

namespace lemon::editor {
namespace {
constexpr float kStripEdgeMin = 40.0f, kStripEdgeMax = 128.0f; // 胶片带缩放域
constexpr float kSegListWidth = 230.0f;   // 左列宽（v3：容纳搜索框）

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

/// 集内动画文件的落位目录（v3.3 设计修复，用户实测驱动）：集所在目录下的
/// <集文件名去后缀>/ 子文件夹——跨集同名动画（每个角色都有 run/idle 是常态）
/// 不再共用一个文件。旧设计（裸名落集目录）下两个集的 run 撞同一个
/// Assets/run.anim：后保存的覆盖先保存的；删除过的还会经墓碑复活把两个集
/// 接到同一 guid——"编辑条目丢失（重扫无效）"悬空的来源。集成员关系语义
/// 仍 = .override 显式引用（T3c 否决"目录=作用域"的决策不变——文件夹只是存放位）。
std::string SetClipDir(const AssetEntry& setEntry) {
    std::string d = setEntry.Dir();
    if (d.empty()) d = "Assets";
    const std::string stem = std::filesystem::path(setEntry.relPath).stem().string();
    return d + "/" + stem;
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

// 切片号/整图 → 页缩略图采样参数（DrawCellImage 与左列行小图共用）。成功时
// tex 非空 + uv 区间 + aspect = 单元高宽比（ch/cw）；失败返回 false（画占位）。
bool CellImageParams(EditorApp& app, const AssetEntry* sheet, uint32_t cell,
                     void*& tex, ImVec2& uv0, ImVec2& uv1, float& aspect) {
    // 切片号 → 页缩略图 UV（行优先换算）；未切片 = 全幅（T3b-1 整图引用）
    const bool sliced = sheet && sheet->Sliced();
    const bool valid = sheet && !sheet->missing && sheet->type == AssetType::Sprite &&
                       (sliced ? cell < sheet->sliceCount : cell == 0);
    tex = valid ? app.AssetGpu().Thumbnail(sheet->guid) : nullptr;
    uint32_t w = 0, h = 0;
    if (!tex || !valid || !app.AssetGpu().PageInfo(sheet->guid, w, h) || !w || !h)
        return false;
    uint32_t cw = w, ch = h;
    if (sliced) {
        const uint32_t cols = sheet->gridCols ? sheet->gridCols : 1;
        const uint32_t rows = sheet->gridRows ? sheet->gridRows : 1;
        cw = sheet->cellW ? sheet->cellW : w / cols;
        ch = sheet->cellH ? sheet->cellH : h / rows;
        const uint32_t cx = cell % cols, cy = cell / cols;
        uv0 = ImVec2((float)(cx * cw) / (float)w, (float)(cy * ch) / (float)h);
        uv1 = ImVec2((float)((cx + 1) * cw) / (float)w, (float)((cy + 1) * ch) / (float)h);
    } else {
        uv0 = ImVec2(0.0f, 0.0f);
        uv1 = ImVec2(1.0f, 1.0f);
    }
    aspect = (float)ch / (float)cw;
    return true;
}

void AnimationPanel::DrawCellImage(EditorApp& app, const AssetEntry* sheet, uint32_t cell,
                                   float edge, const char* idStr) {
    void* tex = nullptr;
    ImVec2 uv0, uv1;
    float aspect = 1.0f;
    const bool ok = CellImageParams(app, sheet, cell, tex, uv0, uv1, aspect);
    if (idStr) {
        // T3-UX9b 交互帧格：InvisibleButton + drawlist 直贴。裸 ImGui::Image 无
        // ID——拖源（重排）永假（无 ID 项需 SourceAllowNullID）、拖靶/右键只认
        // 图矩形（胶片带拖拽三通道自出生即死的根因，用户实测报告）。
        ImGui::InvisibleButton(idStr, ImVec2(edge, edge * aspect));
        const ImVec2 rmin = ImGui::GetItemRectMin(), rmax = ImGui::GetItemRectMax();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (ok) {
            dl->AddImage(tex, rmin, rmax, uv0, uv1);
            return;
        }
        const bool valid = sheet && !sheet->missing && sheet->type == AssetType::Sprite &&
                           (sheet->Sliced() ? cell < sheet->sliceCount : cell == 0);
        dl->AddRectFilled(rmin, rmax, ImGui::GetColorU32(theme::kBgMid));
        dl->AddRect(rmin, rmax, ImGui::GetColorU32(theme::kTextDim));
        const char* glyph = valid ? "?" : "×";
        const ImVec2 ts = ImGui::CalcTextSize(glyph);
        dl->AddText(ImVec2((rmin.x + rmax.x - ts.x) * 0.5f, (rmin.y + rmax.y - ts.y) * 0.5f),
                    ImGui::GetColorU32(theme::kTextDim), glyph);
        return;
    }
    if (ok) {
        ImGui::Image(tex, ImVec2(edge, edge * aspect), uv0, uv1);
        return;
    }
    const bool valid = sheet && !sheet->missing && sheet->type == AssetType::Sprite &&
                       (sheet->Sliced() ? cell < sheet->sliceCount : cell == 0);
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
    // M7a 批① M22 防御位：外部路径（未来事件编辑面/批量操作）漏清越界事件时，
    // 保存前兜底清——不清则下方 roundtrip 预验必拒，保存链自锁且 UI 无修复入口
    const size_t droppedEvents = SanitizeClipEvents(edit_);
    const std::string json = ClipToJson(edit_);
    const std::string abs = app.Ctx().Assets().AbsolutePath(e);
    if (json.empty()) {
        saveMsg_ = "× 写盘失败（序列化产物为空）：" + abs;
        saveOk_ = false;
        return false;
    }
    // 落盘前 roundtrip 预验（review 2026-10-02 #5）：坏 JSON 直接拒写——序列化器
    // 缺陷不再能覆写原档（数据丢失防线；正常路径 Parse(ClipToJson(x)) 恒过）
    if (ClipData back = ParseClipJson(json); !back.ok || back.name != edit_.name ||
        back.frames != edit_.frames) {
        saveMsg_ = "× 保存中止：序列化 roundtrip 校验失败（名字含非法字符？）";
        saveOk_ = false;
        return false;
    }
    // 注意：Rescan 会重建 entries_——调用方在 TrySave 后不得再用本引用
    if (!WriteFileAtomic(abs, json + "\n")) {
        saveMsg_ = "× 写盘失败（磁盘满/权限？）：" + abs;
        saveOk_ = false;
        return false;
    }
    app.RescanAssets();
    dirty_ = false;
    saveOk_ = true;
    saveMsg_ = droppedEvents > 0
                  ? "√ 已保存（顺带移除 " + std::to_string(droppedEvents) +
                        " 个越界帧事件）——Enter Play 后生效（进行中的局用旧快照）"
                  : "√ 已保存——Enter Play 后生效（进行中的局用旧快照）";
    return true;
}

bool AnimationPanel::TryCreateClip(EditorApp& app, const ClipData& c, const std::string& dir,
                                   std::string& err) {
    // 新建落盘共用（向导三通道 + 集内建动画）：dir 相对项目根（"Assets" = 根）；
    // 名字校验 + 撞路径拒（DB + 磁盘双查——只查 DB 时磁盘孤儿文件会被 POSIX
    // rename 静默覆盖 = 数据丢失）+ 建父目录（v3.3 集子文件夹落位）+ 原子写 +
    // Rescan 反查 guid → SetTarget（guid 由 .meta 补齐派发）
    AssetDatabase& db = app.Ctx().Assets();
    std::string why; // D7：校验硬化单源（拒引号/控制字符/超长）
    if (!ValidateAssetName(c.name, &why)) {
        err = "名字非法：" + why;
        return false;
    }
    std::string d = dir.empty() ? "Assets" : dir;
    while (d.size() > 1 && d.back() == '/') d.pop_back();
    const std::string rel = d == "Assets" ? "Assets/" + c.name + ".anim"
                                          : d + "/" + c.name + ".anim";
    // 墓碑（已删除）不挡重建：写文件 + Rescan = 墓碑复活（06 §2 语义——同名
    // 复活继承旧 guid，旧场景引用随复活重新生效，属预期收益非副作用）
    if (const AssetEntry* exist = db.FindByPath(rel); exist && !exist->missing) {
        err = "已存在：" + rel;
        return false;
    }
    const std::string abs = db.ProjectRoot() + "/" + rel;
    std::error_code ecd;
    if (std::filesystem::exists(abs, ecd)) { // 磁盘孤儿（DB 不知情）——拒，防静默覆盖
        err = "磁盘上已存在：" + rel;
        return false;
    }
    std::filesystem::create_directories(std::filesystem::path(abs).parent_path(), ecd);
    if (!WriteFileAtomic(abs, ClipToJson(c) + "\n")) {
        err = "写盘失败：" + abs;
        return false;
    }
    app.RescanAssets();
    if (const AssetEntry* e = db.FindByPath(rel)) SetTarget(e->guid);
    return true;
}

// ------------------------------------------------------------ 帧操作原语 ----

void AnimationPanel::InsertFrameAfter(int idx, const ClipFrame& f) {
    // v3 插入语义：idx ∈ [0, n-1] = 插到 idx 后；**idx = -1 = 插到最前**
    // （「在之前插入副本」首帧位依赖此）；其他越界（< -1 或 >= n）= 末尾追加
    // → 末尾追加请传 (int)edit_.frames.size()。2026-09-27 帧序事故根因：旧注释
    // 称"-1 = 末尾追加"而实现是头插，多图逐张"追加"经此整表反序（svr-test
    // Monster03 四 clip 实测 014..000）——契约以本注释为准，勿再改回。
    if (idx < -1 || idx >= (int)edit_.frames.size()) idx = (int)edit_.frames.size() - 1;
    edit_.frames.insert(edit_.frames.begin() + idx + 1, f);
    selFrame_ = idx + 1;
    selSet_.clear();
    selSet_.push_back(selFrame_);
    selAnchor_ = selFrame_;
    previewFrame_ = selFrame_;
    previewing_ = false;
    dirty_ = true;
    saveMsg_.clear();
}

void AnimationPanel::DuplicateSelectedFrames() {
    // 多选 = 各自插后（升序处理索引稳定）；选中新集 = 各副本位（ord[k]+1+k）
    if (selSet_.empty() && selFrame_ < 0) return;
    std::vector<int> ord = selSet_;
    if (ord.empty()) ord.push_back(selFrame_);
    std::sort(ord.begin(), ord.end());
    for (int k = (int)ord.size() - 1; k >= 0; --k) { // 从尾起插，前段索引不漂移
        const int i = ord[k];
        if (i < 0 || i >= (int)edit_.frames.size()) continue;
        edit_.frames.insert(edit_.frames.begin() + i + 1, edit_.frames[(size_t)i]);
    }
    std::vector<int> dup;
    for (size_t k = 0; k < ord.size(); ++k)
        dup.push_back(ord[k] + 1 + (int)k);
    selSet_ = dup;
    selFrame_ = dup.empty() ? -1 : dup.back();
    selAnchor_ = selFrame_;
    previewFrame_ = selFrame_;
    previewing_ = false;
    dirty_ = true;
    saveMsg_.clear();
}

void AnimationPanel::DeleteSelectedFrames() {
    // selSet_ 优先（多选批量），空则删 selFrame_ 单帧；降序删防索引漂移
    std::vector<int> ord = selSet_;
    if (ord.empty() && selFrame_ >= 0) ord.push_back(selFrame_);
    if (ord.empty()) return;
    std::sort(ord.rbegin(), ord.rend());
    const int firstDel = ord.back(); // 最小被删位 = 删后选中回落位
    for (int idx : ord)
        if (idx >= 0 && idx < (int)edit_.frames.size())
            edit_.frames.erase(edit_.frames.begin() + idx);
    // M7a 批① M22：删帧后清越界帧事件——事件无 UI 编辑入口（作者面 = 手写/表
    // 驱动），留着必被 TrySave 的 roundtrip 预验拒绝（解析侧硬拒）= 保存链自锁
    saveMsg_.clear(); // 旧消息不滞留（"√ 已保存"挂在 dirty 态上误导）
    if (const size_t dropped = SanitizeClipEvents(edit_); dropped > 0)
        saveMsg_ = "已移除 " + std::to_string(dropped) + " 个越界帧事件（删帧所致）";
    selSet_.clear();
    selAnchor_ = -1;
    selFrame_ = edit_.frames.empty() ? -1 : std::min(firstDel, (int)edit_.frames.size() - 1);
    previewFrame_ = std::max(selFrame_, 0);
    previewing_ = false;
    dirty_ = true;
}

void AnimationPanel::HandleKeys(bool ro) {
    // v3 键盘帧操作。守卫链：只读态 → 左列输入中 → WantTextInput（IME 冒烟
    // 项）→ 弹窗全关 → 本面板持焦点（keysFocused_；EditorApp 据此跳过实体级
    // 同名键，防双触发——见 BuildShortcuts / CapturesGlobalKeys）。
    if (ro) return;
    if (segNewActive_ || segEditIdx_ >= 0) return;
    if (ImGui::GetIO().WantTextInput) return;
    if (wizOpen_ || pickOpen_ || setCreateOpen_ || pickFlow_ != 0 || clipPickOpen_) return;
    if (!keysFocused_) return;
    const int n = (int)edit_.frames.size();
    if (n == 0) return;
    const bool ctrl = ImGui::GetIO().KeyCtrl;
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false) && selFrame_ > 0) {
        std::swap(edit_.frames[(size_t)selFrame_ - 1], edit_.frames[(size_t)selFrame_]);
        --selFrame_;
        selSet_.clear();
        selAnchor_ = selFrame_;
        previewFrame_ = selFrame_;
        dirty_ = true;
        saveMsg_.clear();
    }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_RightArrow, false) && selFrame_ >= 0 &&
        selFrame_ < n - 1) {
        std::swap(edit_.frames[(size_t)selFrame_], edit_.frames[(size_t)selFrame_ + 1]);
        ++selFrame_;
        selSet_.clear();
        selAnchor_ = selFrame_;
        previewFrame_ = selFrame_;
        dirty_ = true;
        saveMsg_.clear();
    }
    if (!ctrl && ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false)) {
        selFrame_ = selFrame_ <= 0 ? 0 : selFrame_ - 1;
        selSet_.clear();
        selSet_.push_back(selFrame_);
        selAnchor_ = selFrame_;
        previewFrame_ = selFrame_;
        previewing_ = false;
    }
    if (!ctrl && ImGui::IsKeyPressed(ImGuiKey_RightArrow, false)) {
        selFrame_ = selFrame_ < 0 ? 0 : std::min(selFrame_ + 1, n - 1);
        selSet_.clear();
        selSet_.push_back(selFrame_);
        selAnchor_ = selFrame_;
        previewFrame_ = selFrame_;
        previewing_ = false;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) ||
        ImGui::IsKeyPressed(ImGuiKey_Backspace, false))
        DeleteSelectedFrames();
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_D)) DuplicateSelectedFrames();
    if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) {
        previewing_ = !previewing_;
        previewT0_ = ImGui::GetTime();
    }
}

// ---------------------------------------------------------------- 胶片带 ----

void AnimationPanel::DrawFilmstrip(EditorApp& app, bool ro, int playFrame) {
    // v3 胶片带：单击选中即预览跟随 · Ctrl 切换/Shift 范围多选 · 拖拽重排 ·
    // 拖 Assets 精灵入格 = 换图 · 右键插入/删除 · 播放帧游标高亮（playFrame
    // >= 0 时）。ro 时整块由调用方 BeginDisabled（播放传输在工具条，仍可用）。
    (void)ro;
    AssetDatabase& db = app.Ctx().Assets();
    const int n = (int)edit_.frames.size();
    if (n == 0) {
        ImGui::TextDisabled("（无帧：「+ 添加帧」四通道，或把 Assets 图/.anim 拖进上方预览）");
        return;
    }
    const float avail = ImGui::GetContentRegionAvail().x;
    const ImGuiStyle& st = ImGui::GetStyle();
    // T3-UX9a：每格真实占宽 = 图宽 + 行距（帧格是 InvisibleButton 直贴，无
    // FramePadding——旧公式多算 2×FramePadding/格 → 列数低估 → 提前换行 +
    // 右侧累计空档，用户截图取证）
    const float step = stripEdge_ + st.ItemSpacing.x;
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
        DrawCellImage(app, ok ? sheet : nullptr, edit_.frames[(size_t)i].cell, stripEdge_,
                      "##cell"); // T3-UX9b：交互帧格（拖源/拖靶/右键/点击挂得上）
        stripCellItem_ = (unsigned)ImGui::GetItemID(); // 冒烟：帧格必须是有 ID 交互件
        const ImVec2 rmin = ImGui::GetItemRectMin(), rmax = ImGui::GetItemRectMax();
        if (selected) // 选中描边（当前帧 = 亮色加粗，多选 = 暗色细框）
            ImGui::GetWindowDrawList()->AddRect(
                rmin - ImVec2(1, 1), rmax + ImVec2(1, 1),
                ImGui::GetColorU32(i == selFrame_ ? theme::kAccent : theme::kAccentDim),
                0.0f, 0, i == selFrame_ ? 2.5f : 1.5f);
        if (playFrame == i) // 播放帧游标（外圈亮色细框，播放暂停即隐）
            ImGui::GetWindowDrawList()->AddRect(rmin - ImVec2(2.5f, 2.5f),
                                                rmax + ImVec2(2.5f, 2.5f),
                                                ImGui::GetColorU32(theme::kTextBright), 0.0f, 0,
                                                1.5f);
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            selFrame_ = i;
            if (ImGui::GetIO().KeyCtrl) { // Ctrl = 多选切换
                auto it = std::find(selSet_.begin(), selSet_.end(), i);
                if (it != selSet_.end())
                    selSet_.erase(it);
                else
                    selSet_.push_back(i);
            } else if (ImGui::GetIO().KeyShift && selAnchor_ >= 0 && selAnchor_ < n) {
                const int lo = std::min(selAnchor_, i), hi = std::max(selAnchor_, i);
                selSet_.assign((size_t)(hi - lo + 1), 0);
                for (int k = lo; k <= hi; ++k) selSet_[(size_t)(k - lo)] = k;
            } else {
                selSet_.clear();
                selSet_.push_back(i);
                selAnchor_ = i;
            }
            previewFrame_ = i; // v3：选中即预览（取代旧"双击跳帧"）
            previewing_ = false;
        }
        // 右键帧菜单：插入/删除（删除主入口之一——属性行退役后此处在位）
        if (ImGui::BeginPopupContextItem("##framectx")) {
            if (ImGui::MenuItem("在之前插入副本")) InsertFrameAfter(i - 1, edit_.frames[(size_t)i]);
            if (ImGui::MenuItem("在之后插入副本")) InsertFrameAfter(i, edit_.frames[(size_t)i]);
            ImGui::Separator();
            if (ImGui::MenuItem("删除帧 (Del)")) {
                selFrame_ = i;
                selSet_.clear();
                selSet_.push_back(i);
                DeleteSelectedFrames();
            }
            ImGui::EndPopup();
        }
        // 拖拽源（重排）：拖到目标帧前插入
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
                    // #36：统一"插到目标帧前"——原 src<i 分支 insert(i) 实为插到
                    // 目标之后（拖到相邻下一帧表现为两帧交换）且选中位按回退算，
                    // 注释/实现/高亮三者矛盾；erase 后目标帧在 i-1，前插 = i-1
                    const int dst = src < i ? i - 1 : i;
                    edit_.frames.insert(edit_.frames.begin() + dst, f);
                    selFrame_ = dst;
                    selSet_.clear();
                    dirty_ = true;
                    saveMsg_.clear();
                }
            }
            // v3：拖 Assets 精灵入格 = 换图（整图 cell0；切片表用「从精灵表…」）
            if (const ImGuiPayload* pay2 = ImGui::AcceptDragDropPayload("LemonAsset")) {
                AssetDragPayload d{};
                std::memcpy(&d, pay2->Data, sizeof(d));
                if (d.kind == 0) {
                    edit_.frames[(size_t)i] = {d.guid, 0};
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
    // 带尾放置区：拖 Assets 精灵 = 追加帧（v3 主选图通道——免 combo 翻找）
    ImGui::PushID("dropend");
    if (col > 0) ImGui::SameLine();
    ImGui::InvisibleButton("##z", ImVec2(stripEdge_, stripEdge_));
    if (ImGui::IsItemHovered()) {
        const ImVec2 bmin = ImGui::GetItemRectMin(), bmax = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddRectFilled(bmin, bmax, WithAlpha(theme::kAccentDim, 0.25f));
        ImGui::GetWindowDrawList()->AddRect(bmin, bmax, ImGui::GetColorU32(theme::kAccentDim));
        ImGui::SetTooltip("拖 Assets 精灵到此追加帧");
    }
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* pay = ImGui::AcceptDragDropPayload("LemonAsset")) {
            AssetDragPayload d{};
            std::memcpy(&d, pay->Data, sizeof(d));
            if (d.kind == 0)
                InsertFrameAfter((int)edit_.frames.size(), {d.guid, 0}); // 末尾追加
            else if (d.kind == 4) AppendClipFrames(app, d.guid); // .anim = 复制帧表
        }
        ImGui::EndDragDropTarget();
    }
    ImGui::PopID();
}

// ------------------------------------------------ 从精灵表加帧（内嵌体）----

// ------------------------------------------- 从精灵表添加帧（v3.1 两段式）----

void AnimationPanel::StartSheetPick(EditorApp& app) {
    // 第一段：文件选择器选精灵图（v3.1 用户实测反馈——原 combo 列全部资产翻找
    // 太麻烦）。起始目录 = 当前帧 sheet 所在目录（无则项目 Assets/）。
    // （T3-UX7：createMode 流 3 随创建向导退役——本通道只剩加帧。）
    pickFlow_ = 1;
    AssetDatabase& db = app.Ctx().Assets();
    std::string dir = db.ProjectRoot() + "/Assets";
    if (!edit_.frames.empty()) {
        if (const AssetEntry* t = db.FindByGuid(edit_.frames.back().sheetGuid)) {
            std::error_code ec;
            std::filesystem::path p = db.AbsolutePath(*t);
            if (std::filesystem::is_directory(p.parent_path(), ec))
                dir = p.parent_path().string();
        }
    }
    picker_.SetQuickDirs({{"项目 Assets", db.ProjectRoot() + "/Assets"}});
    picker_.OpenMulti("选择精灵图", dir, {".png", ".jpg", ".jpeg", ".bmp"});
}

void AnimationPanel::HandlePickerResult(EditorApp& app) {
    // 文件选择器结果路由。**本函数可能 ImportFile/RescanAssets——调用后 OnGui
    // 里的条目指针须全部 guid 重查**（调用点安排在 target 解析之前）。
    if (pickFlow_ == 0) return;
    PickerResult r = picker_.Draw();
    if (r.action == PickerAction::None) return;
    const int flow = pickFlow_;
    pickFlow_ = 0;
    if (r.action != PickerAction::Open || r.paths.empty()) return;
    AssetDatabase& db = app.Ctx().Assets();

    // 路径 → sprite 资产：项目内 → FindByPath（未登记 → Rescan 收编——手放进
    // 目录未扫的图）；项目外 → ImportFile 落到集目录/浏览器当前目录。
    const auto resolveSprite = [&](const std::string& abs, const AssetEntry*& out) -> bool {
        out = nullptr;
        std::error_code ec;
        std::filesystem::path p(abs);
        if (!std::filesystem::is_regular_file(p, ec)) {
            saveMsg_ = "× 文件不存在：" + abs;
            saveOk_ = false;
            return false;
        }
        const std::string root = db.ProjectRoot();
        if (abs.rfind(root + "/", 0) == 0) { // 项目内
            const std::string rel = abs.substr(root.size() + 1); // "Assets/..."
            if (const AssetEntry* e = db.FindByPath(rel); e && !e->missing) {
                out = e;
                return true;
            }
            app.RescanAssets(); // 未登记（新拷入的图）——重扫收编
            if (const AssetEntry* e = db.FindByPath(rel); e && !e->missing) {
                out = e;
                return true;
            }
        }
        // 项目外（或收编失败）→ 导入。落点 = 集目录（集模式）/ 浏览器当前目录
        std::string destDir = app.AssetBrowserDir();
        if (setGuid_ != 0)
            if (const AssetEntry* se = db.FindByGuid(setGuid_)) destDir = se->Dir();
        std::string sub = destDir.size() > 7 && destDir.rfind("Assets", 0) == 0
                              ? destDir.substr(7) // "Assets/xx" → "xx"
                              : std::string();
        while (!sub.empty() && (sub.front() == '/')) sub.erase(sub.begin());
        while (!sub.empty() && (sub.back() == '/')) sub.pop_back();
        const std::string relDest = sub.empty() ? p.filename().string() : sub + "/" + p.filename().string();
        out = db.ImportFile(abs, relDest); // 内部 Rescan + FindByPath
        if (!out) {
            saveMsg_ = "× 导入失败：" + abs;
            saveOk_ = false;
        }
        return out != nullptr;
    };

    if (flow == 1) { // 精灵表单图 → 第二段选帧对话框（流 3 随创建向导退役）
        const AssetEntry* e = nullptr;
        if (!resolveSprite(r.paths.front(), e)) return;
        if (e->type != AssetType::Sprite) {
            saveMsg_ = "× 不是图片资产：" + e->relPath;
            saveOk_ = false;
            return;
        }
        pickGuid_ = e->guid;
        if (e->Sliced()) { // 网格参数就位 = meta 现值（可改——添加时重写）
            pickInput_ = 0;
            pickCols_ = e->gridCols;
            pickRows_ = e->gridRows;
        }
        pickHasRect_ = false;
        pickSelCells_.clear();
        pickCountLast_ = 0; // 参数条计数读上一帧缓存——开窗即 0，不吃上一次会话残值
        pickZoom_ = 1.0f;
        pickOpen_ = true; // 边沿触发：第二段模态（选帧对话框）
        pickPending_ = true;
    } else if (flow == 2) { // 多图整图入帧（按选择顺序 = 显示序文件名升序）
        size_t added = 0;
        for (const std::string& abs : r.paths) {
            const AssetEntry* e = nullptr;
            if (resolveSprite(abs, e) && e->type == AssetType::Sprite) {
                InsertFrameAfter((int)edit_.frames.size(), {e->guid, 0}); // 末尾追加
                ++added;
            }
        }
        if (!added) {
            saveMsg_ = "× 没有可导入的图片";
            saveOk_ = false;
        }
    }
}

void AnimationPanel::OpenSheetPickForTest(EditorApp& app, uint64_t guid) {
    // 冒烟语义直调（三图优化点③）：复刻 HandlePickerResult 流 1 的第二段就位
    // （跳过文件选择器——第一段已有 pick(click/shift/all) 独立回归位）。此后
    // 全选/清空/取消走真实模态点击（TestHooks 矩形 + hold/release 隔帧注入）。
    const AssetEntry* e = app.Ctx().Assets().FindByGuid(guid);
    if (!e || e->missing || e->type != AssetType::Sprite) return;
    pickFlow_ = 0;
    pickGuid_ = e->guid;
    if (e->Sliced()) { // 与真实流同款：网格参数就位 = meta 现值
        pickInput_ = 0;
        pickCols_ = e->gridCols;
        pickRows_ = e->gridRows;
    }
    pickHasRect_ = false;
    pickSelCells_.clear();
    pickCountLast_ = 0; // 与真实流同款：开窗计数即 0
    pickZoom_ = 1.0f;
    pickOpen_ = true;
    pickPending_ = true;
}

void AnimationPanel::DrawSheetPicker(EditorApp& app) {
    // 第二段：选帧对话框 v3.2（Godot Select Frames 式，截图 docs/animation/）。
    // 上下结构（2026-09-28 用户定案：长条精灵表横向是稀缺项，原右参数栏 252px
    // 挤宽）：顶部参数条两行（分割/选择/视图横排，改动即清选区——格变了选集无
    // 意义）+ 图区吃满全宽（网格实时重绘 + InvisibleButton 覆盖捕获——修复"图
    // 上拖动变成拖走整个对话框"）+ 底行动态按钮（左侧空档放 meta 状态提示）。
    // **添加时**才写 .meta：SetGridSlice → Rescan（连号块分配）→ guid 重查。
    // 1×1 且未切片 = 整图引用（不写 meta）。
    if (!pickOpen_) return;
    if (pickPending_) {
        ImGui::OpenPopup("从精灵表添加帧");
        pickPending_ = false;
    }
    ImGui::SetNextWindowSize(ImVec2(780.0f, 600.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("从精灵表添加帧", nullptr, ImGuiWindowFlags_NoSavedSettings)) {
        pickOpen_ = false; // 被外力关——复位，下次入口重开（防僵尸开态）
        return;
    }
    AssetDatabase& db = app.Ctx().Assets();
    AssetEntry* sh = db.FindByGuid(pickGuid_); // 可变版：添加时 SetGridSlice
    void* tex = sh ? app.AssetGpu().Thumbnail(sh->guid) : nullptr;
    uint32_t w = 0, h = 0;
    const bool imgOk = sh && !sh->missing && sh->type == AssetType::Sprite && tex &&
                       app.AssetGpu().PageInfo(sh->guid, w, h) && w && h;
    if (!imgOk) {
        ImGui::TextColored(theme::kTextError, "图片不可用（页未导入——Assets 重扫后重试）");
        if (ImGui::Button("取消")) {
            pickOpen_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
        return;
    }

    // 参数 → 网格派生（实时；钳到图片尺寸防 0 尺寸格）
    const auto deriveGrid = [&]() {
        uint32_t cols, rows, cw, ch;
        if (pickInput_ == 0) {
            cols = (uint32_t)std::clamp(pickCols_, 1, (int)w);
            rows = (uint32_t)std::clamp(pickRows_, 1, (int)h);
            cw = w / cols;
            ch = h / rows;
        } else {
            cw = (uint32_t)std::clamp(pickCellW_, 1, (int)w);
            ch = (uint32_t)std::clamp(pickCellH_, 1, (int)h);
            cols = w / cw;
            rows = h / ch;
        }
        return std::make_tuple(cols, rows, cw, ch);
    };
    auto [cols, rows, cw, ch] = deriveGrid();

    // ---- 上：参数条两行横排。上下结构（2026-09-28 用户定案）：长条精灵表横向
    // 是稀缺项，右栏 252px 挤宽 → 参数上移横条、图区吃满全宽 ----
    const float footH = ImGui::GetFrameHeightWithSpacing();
    const float paramH = ImGui::GetFrameHeight() * 2.0f + ImGui::GetStyle().ItemSpacing.y +
                         ImGui::GetStyle().WindowPadding.y * 2.0f + 2.0f;
    ImGui::BeginChild("##pickparams", ImVec2(0.0f, paramH), ImGuiChildFlags_Borders);
    bool changed = false;
    if (ImGui::RadioButton("按块数", &pickInput_, 0)) changed = true;
    ImGui::SameLine();
    if (ImGui::RadioButton("按像素", &pickInput_, 1)) changed = true;
    ImGui::SameLine();
    if (pickInput_ == 0) {
        ImGui::SetNextItemWidth(110.0f);
        if (ImGui::DragInt("横向块数", &pickCols_, 1.0f, 1, 512)) changed = true;
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110.0f);
        if (ImGui::DragInt("纵向块数", &pickRows_, 1.0f, 1, 512)) changed = true;
    } else {
        ImGui::SetNextItemWidth(110.0f);
        if (ImGui::DragInt("cell 宽", &pickCellW_, 1.0f, 1, 4096)) changed = true;
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110.0f);
        if (ImGui::DragInt("cell 高", &pickCellH_, 1.0f, 1, 4096)) changed = true;
    }
    if (changed) {
        pickHasRect_ = false;
        pickSelCells_.clear();
    }
    auto [c2, r2, w2, h2] = deriveGrid();
    ImGui::SameLine();
    ImGui::TextDisabled("cell %u × %u · 共 %u × %u 块", w2, h2, c2, r2);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("添加时写入图片 .meta（全项目生效）");
    // 行 2：选择（主操作，Godot 参照常规钮）+ 模式 + 视图
    if (ImGui::Button("全选##pickall")) {
        if (pickOrderMode_ == 0) {
            pickHasRect_ = true;
            pickR0_ = pickC0_ = 0;
            pickR1_ = (int)rows - 1;
            pickC1_ = (int)cols - 1;
        } else {
            pickSelCells_.clear();
            pickSelCells_.reserve((size_t)rows * cols);
            for (uint32_t r = 0; r < rows; ++r)
                for (uint32_t c = 0; c < cols; ++c) pickSelCells_.push_back(r * cols + c);
        }
    }
    testhooks::Stash("sheetpick.all", ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    ImGui::SameLine();
    if (ImGui::Button("清空##picknone")) {
        pickHasRect_ = false;
        pickSelCells_.clear();
    }
    testhooks::Stash("sheetpick.none", ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    ImGui::SameLine();
    // 计数画在选择操作处（三图优化点③）；本子窗先于图区画，读上一帧的
    // pickCountLast_（图区子窗末尾更新，探针同值——滞后一帧无感）
    ImGui::Text("已选 %d 帧", pickCountLast_);
    testhooks::Stash("sheetpick.count", ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    ImGui::SameLine();
    ImGui::SetNextItemWidth(132.0f);
    if (ImGui::Combo("##pickorder", &pickOrderMode_, "框选（行优先）\0点选（按点击序）\0")) {
        pickHasRect_ = false;
        pickSelCells_.clear();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("框选 = 拖矩形按行优先入帧\n点选 = 逐格点选，按点击顺序入帧（Godot As Selected）");
    ImGui::SameLine();
    ImGui::TextDisabled("│");
    ImGui::SameLine();
    if (ImGui::SmallButton("-##pickzoomout")) pickZoom_ = std::max(0.25f, pickZoom_ / 1.25f);
    ImGui::SameLine();
    ImGui::TextDisabled("%d%%", (int)(pickZoom_ * 100.0f));
    ImGui::SameLine();
    if (ImGui::SmallButton("+##pickzoomin")) pickZoom_ = std::min(8.0f, pickZoom_ * 1.25f);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("图区 Ctrl+滚轮缩放");
    ImGui::EndChild();

    // ---- 下：图区（余量全吃；缩放可滚；InvisibleButton 覆盖捕获交互）----
    ImGui::BeginChild("##pickarea", ImVec2(0.0f, -footH), ImGuiChildFlags_Borders);
    if (ImGui::IsWindowHovered() && ImGui::GetIO().KeyCtrl &&
        ImGui::GetIO().MouseWheel != 0.0f)
        pickZoom_ = std::clamp(pickZoom_ * (1.0f + ImGui::GetIO().MouseWheel * 0.1f), 0.25f,
                               8.0f);
    const float availW = ImGui::GetContentRegionAvail().x;
    const float availH = ImGui::GetContentRegionAvail().y;
    const float fitW = std::max(availW, 160.0f);
    const float fitH = std::max(availH, 120.0f);
    float dispW = fitW * pickZoom_;
    const float dispH0 = dispW * (float)h / (float)w;
    if (dispH0 > fitH * pickZoom_) dispW = fitH * pickZoom_ * (float)w / (float)h; // 双向 fit
    const float dispH = dispW * (float)h / (float)w;
    const ImVec2 pos = ImGui::GetCursorPos();
    ImGui::Image(tex, ImVec2(dispW, dispH));
    ImGui::SetCursorPos(pos); // 回退同位叠不可见命中层——Image 不拦截拖动，裸图
                             // 上拖 = 移动模态窗口（用户实测框选坏因），按钮层吃住
    ImGui::InvisibleButton("##pickhit", ImVec2(dispW, dispH));
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
    if (pickOrderMode_ == 0) { // 拖框选
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
    } else if (hov && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) { // 点选序
        const auto [r, c] = cellAt(mouse);
        const uint32_t cell = (uint32_t)r * cols + (uint32_t)c;
        auto it = std::find(pickSelCells_.begin(), pickSelCells_.end(), cell);
        if (it != pickSelCells_.end())
            pickSelCells_.erase(it);
        else
            pickSelCells_.push_back(cell);
    }
    int ra = pickR0_, rb = pickR1_, ca = pickC0_, cb = pickC1_;
    if (ra > rb) std::swap(ra, rb);
    if (ca > cb) std::swap(ca, cb);
    const int cnt = pickOrderMode_ == 0
                        ? (pickHasRect_ ? (rb - ra + 1) * (cb - ca + 1) : 0)
                        : (int)pickSelCells_.size();
    pickCountLast_ = cnt; // 冒烟探针缓存（右栏计数/动态按钮文案用的就是这个值）
    if (pickOrderMode_ == 0 && pickHasRect_) {
        const ImVec2 q0 = pmin + ImVec2(dispW * (float)ca / (float)cols,
                                        dispH * (float)ra / (float)rows);
        const ImVec2 q1 = pmin + ImVec2(dispW * (float)(cb + 1) / (float)cols,
                                        dispH * (float)(rb + 1) / (float)rows);
        dl->AddRectFilled(q0, q1, WithAlpha(theme::kAccent, 0.30f));
        dl->AddRect(q0, q1, ImGui::GetColorU32(theme::kAccent), 0.0f, 0, 2.0f);
    }
    if (pickOrderMode_ == 1) { // 点选高亮 + 序号角标（入帧顺序）
        for (size_t k = 0; k < pickSelCells_.size(); ++k) {
            const uint32_t cell = pickSelCells_[k];
            const float cx = (float)(cell % cols), cy = (float)(cell / cols);
            const ImVec2 q0 = pmin + ImVec2(dispW * cx / (float)cols, dispH * cy / (float)rows);
            const ImVec2 q1 =
                pmin + ImVec2(dispW * (cx + 1) / (float)cols, dispH * (cy + 1) / (float)rows);
            dl->AddRectFilled(q0, q1, WithAlpha(theme::kAccent, 0.30f));
            dl->AddRect(q0, q1, ImGui::GetColorU32(theme::kAccent), 0.0f, 0, 2.0f);
            char no[8];
            std::snprintf(no, sizeof(no), "%zu", k + 1);
            dl->AddText(ImVec2(q0.x + 2.0f, q0.y + 1.0f), ImGui::GetColorU32(theme::kTextBright),
                        no);
        }
    }
    ImGui::EndChild();

    // ---- 底行：取消/产出（计数本体在参数条「选择」处；底行动态按钮文案仍带
    // 计数——按钮即反馈，Godot Add N Frame(s) 同款）----
    const auto buildFrames = [&]() {
        std::vector<ClipFrame> out;
        if (pickOrderMode_ == 0) {
            for (int r = ra; r <= rb; ++r)
                for (int c = ca; c <= cb; ++c)
                    out.push_back({pickGuid_, (uint32_t)(r * (int)cols + c)});
        } else {
            for (uint32_t cell : pickSelCells_) out.push_back({pickGuid_, cell});
        }
        return out;
    };
    // meta 状态提示放底行左侧空档（上下结构批自右栏移来；悬停 cell 信息另有
    // 同文 tooltip）——末行固定高（-footH 预留），SameLine 防提示顶出第二行
    ImGui::TextDisabled("添加时写入图片 .meta（全项目生效）");
    ImGui::SameLine();
    // 底行三钮（取消/添加/替换为）：右对齐按整行宽让位（按两钮算 = 整行右溢出窗）
    const float bw = 150.0f;
    const float rowW = bw * 3.0f + ImGui::GetStyle().ItemSpacing.x * 2.0f;
    const float rightX = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - rowW;
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), rightX));
    if (ImGui::Button("取消", ImVec2(bw, 0)) ||
        (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !ImGui::IsAnyItemActive())) {
        pickOpen_ = false;
        ImGui::CloseCurrentPopup();
    }
    testhooks::Stash("sheetpick.cancel", ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    ImGui::SameLine();
    ImGui::BeginDisabled(cnt <= 0);
    char b1[64], b2[64];
    std::snprintf(b1, sizeof(b1), "添加 %d 帧", cnt);
    std::snprintf(b2, sizeof(b2), "替换为 %d 帧", cnt);
    if (ImGui::Button(b1, ImVec2(bw, 0))) {
        std::vector<ClipFrame> add = buildFrames();
        // 网格与 meta 不一致（或未切片）且非 1×1 → 写 .meta + Rescan（连号块分
        // 配）→ guid 重查（Rescan 重建 entries_——sh 指针此后失效）
        const bool needMeta =
            cols * rows > 1 && (!sh->Sliced() || sh->gridCols != cols || sh->gridRows != rows ||
                                sh->cellW != cw || sh->cellH != ch);
        if (needMeta) {
            if (!db.SetGridSlice(*sh, cw, ch, cols, rows)) {
                ImGui::EndDisabled();
                ImGui::TextColored(theme::kTextError, "写 .meta 失败（权限/磁盘？）");
                ImGui::EndPopup();
                return;
            }
            app.RescanAssets();
        }
        edit_.frames.insert(edit_.frames.end(), add.begin(), add.end());
        dirty_ = true;
        saveMsg_.clear();
        pickOpen_ = false;
        ImGui::CloseCurrentPopup();
    }
    {
        ImGui::SameLine();
        if (ImGui::Button(b2, ImVec2(bw, 0))) {
            edit_.frames = buildFrames();
            const bool needMeta =
                cols * rows > 1 && (!sh->Sliced() || sh->gridCols != cols ||
                                    sh->gridRows != rows || sh->cellW != cw || sh->cellH != ch);
            if (needMeta) {
                if (!db.SetGridSlice(*sh, cw, ch, cols, rows)) {
                    ImGui::EndDisabled();
                    ImGui::TextColored(theme::kTextError, "写 .meta 失败（权限/磁盘？）");
                    ImGui::EndPopup();
                    return;
                }
                app.RescanAssets();
            }
            selFrame_ = -1;
            selSet_.clear();
            selAnchor_ = -1;
            dirty_ = true;
            saveMsg_.clear();
            pickOpen_ = false;
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::EndDisabled();
    // 越窗锁探针：替换为 = 底行最右钮，与模态窗矩形同帧登记（smoke f80 断言右缘
    // 在窗内——cnt=0 时按钮禁用但照常渲染）
    testhooks::Stash("sheetpick.win", ImGui::GetWindowPos(),
                     ImGui::GetWindowPos() + ImGui::GetWindowSize());
    testhooks::Stash("sheetpick.replace", ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    ImGui::EndPopup();
}

void AnimationPanel::AppendClipFrames(EditorApp& app, uint64_t clipGuid) {
    // 读 clip 资产帧表整段追加（从 .anim 复制 / 拖 .anim 资产两入口共用）
    AssetDatabase& db = app.Ctx().Assets();
    const AssetEntry* ce = db.FindByGuid(clipGuid);
    if (!ce || ce->missing || ce->type != AssetType::Clip) {
        saveMsg_ = "× 动画剪辑条目悬空";
        saveOk_ = false;
        return;
    }
    ClipData cd;
    {
        std::ifstream f(db.AbsolutePath(*ce), std::ios::binary);
        if (f) {
            std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            cd = ParseClipJson(text);
        }
        if (!cd.ok) {
            saveMsg_ = "× 源剪辑解析失败：" + cd.error;
            saveOk_ = false;
            return;
        }
    }
    for (const ClipFrame& fr : cd.frames) edit_.frames.push_back(fr);
    selFrame_ = (int)edit_.frames.size() - 1;
    selSet_.clear();
    selAnchor_ = selFrame_;
    previewFrame_ = std::max(selFrame_, 0);
    dirty_ = true;
    saveMsg_.clear();
}

void AnimationPanel::DrawClipPickModal(EditorApp& app) {
    // 从 .anim 复制帧（v3.1 加帧通道）：列表弹窗（排除当前编辑目标）。边沿触发。
    if (!clipPickOpen_) return;
    if (clipPickPending_) {
        ImGui::OpenPopup("从动画剪辑复制帧");
        clipPickPending_ = false;
    }
    ImGui::SetNextWindowSize(ImVec2(480.0f, 400.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("从动画剪辑复制帧", nullptr, ImGuiWindowFlags_NoSavedSettings)) {
        clipPickOpen_ = false;
        return;
    }
    AssetDatabase& db = app.Ctx().Assets();
    ImGui::TextDisabled("选中一条剪辑，其帧表整段追加到当前动画末尾");
    ImGui::BeginChild("##cliplist", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()),
                      ImGuiChildFlags_Borders);
    for (const auto& e : db.Entries()) {
        if (e.missing || e.type != AssetType::Clip || e.guid == targetGuid_) continue;
        ImGui::PushID(e.relPath.c_str());
        if (ImGui::Selectable(e.relPath.c_str())) {
            AppendClipFrames(app, e.guid);
            clipPickOpen_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
    if (ImGui::Button("取消") ||
        (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !ImGui::IsAnyItemActive())) {
        clipPickOpen_ = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

// ---------------------------------------------------- T3c 动画集工作台 ----
// 目标 = .override 集容器（Unity AnimatorController 壳 / Godot SpriteFrames 单入口，
// 决策记录见批文件）：集存段引用（一段一 .anim，段身份 = 文件 GUID），左列段清
// 单，段编辑复用上方全部 clip 机制（targetGuid_ 指向当前段）。按名播放作用域 =
// 集成员关系（非目录约定——资源搬家/特殊组织不破语义）。

void AnimationPanel::ResetEditingState() {
    // 2026-09-27 热修②：右键"新建动画集/动画剪辑"弹出面板显示历史内容——面板
    // 对象常驻，setGuid_/targetGuid_ 残留则弹窗背后照渲染上一个集。清后走空态
    // 提示行；创建成功路径由 TryCreateClip(SetTarget)/OpenSet 重新置位。
    setGuid_ = 0;
    setLoadedGuid_ = 0;
    segGuid_ = 0;
    targetGuid_ = 0;
    segRowCache_.clear(); // 行元信息随集走——残留只会指向已删/他集的段
    // M7a 批① M23：inline 改名态随集清——此前 segEditIdx_/segEditBuf_/segFilter_
    // 跨集残留，集 B 的第 N 行命中旧 idx 时预填集 A 的名字，回车即把 B 的 .anim
    // 改名写盘（用户实测串写路径）。
    segEditIdx_ = -1;
    segEditBuf_.clear();
    segFilter_.clear();
    // M25：集面脏标随集清（新集 LoadSetFrom 会重置 setLoadedName_）
    setDirty_ = false;
}

void AnimationPanel::StartImageFilePick(EditorApp& app) {
    // T3-UX4 抽取（菜单项与冒烟注入共用）：多选图片 → 整图入帧
    pickFlow_ = 2;
    AssetDatabase& db = app.Ctx().Assets();
    picker_.SetQuickDirs({{"项目 Assets", db.ProjectRoot() + "/Assets"}});
    picker_.OpenMulti("选择图片（多选）", db.ProjectRoot() + "/Assets",
                      {".png", ".jpg", ".jpeg", ".bmp"});
}

void AnimationPanel::StartCreateSet(const std::string& relDir, const std::string& destDir) {
    ResetEditingState(); // 新建 = 从零开始，不带上一个集的显示
    setCreateOpen_ = true;
    setCreatePending_ = true; // 边沿触发（向导同款）
    setCreateDir_ = destDir.empty() ? "Assets" : destDir;
    setCreateSrc_ = relDir.empty() || relDir == "Assets" ? "" : relDir;
    setCreateWithSeg_ = !setCreateSrc_.empty();
    const size_t slash = setCreateDir_.find_last_of('/');
    setCreateName_ = setCreateDir_.empty() || setCreateDir_ == "Assets"
                         ? "player"
                         : setCreateDir_.substr(slash + 1);
    if (setCreateName_.empty()) setCreateName_ = "player";
    setErr_.clear();
}

void AnimationPanel::LoadSetFrom(const AssetDatabase& db, const AssetEntry& e) {
    setLoadedGuid_ = e.guid;
    setLoadedHash_ = e.hash;
    segRowCache_.clear(); // 换集：段行缓存全量重建（键 guid 跨集不撞，纯防陈旧）
    std::ifstream f(db.AbsolutePath(e), std::ios::binary);
    if (f) {
        std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        setEdit_ = ParseAnimSetJson(text);
    } else {
        setEdit_ = AnimSetData{};
        setEdit_.error = "读档失败：" + e.relPath;
    }
    if (setEdit_.name.empty())
        setEdit_.name = std::filesystem::path(e.relPath).stem().string();
    setLoadedName_ = setEdit_.name; // M25：集名改动判基准（Enter/失焦即存的变值探测）
    setMsg_.clear();
    // 保持动画选中：外部改档后引用还在 → 保留；没了 → v3.2 自动选首个动画
    //（双击开集即见帧——旧版回落空提示行，配合窄窗被误读成"没编辑"）
    bool keep = false;
    for (const AnimSetSeg& sg : setEdit_.segments)
        if (sg.clipGuid == segGuid_) keep = true;
    if (!keep) {
        if (!setEdit_.segments.empty()) {
            segGuid_ = setEdit_.segments.front().clipGuid;
            targetGuid_ = segGuid_;
        } else {
            segGuid_ = 0;
            targetGuid_ = 0;
        }
    }
}

bool AnimationPanel::TrySaveSet(EditorApp& app, const AssetEntry& setEntry) {
    // 校验：段名非空集内唯一（运行时对重名取路径序先者——编辑器拦在写盘前）+ 段
    // 引用可解析。Rescan 会重建 entries_——调用方此后不得再用 setEntry 引用。
    AssetDatabase& db = app.Ctx().Assets();
    if (!setEdit_.ok) {
        setMsg_ = "× 坏档不可保存：" + setEdit_.error;
        setOk_ = false;
        return false;
    }
    std::unordered_set<std::string> seen;
    for (size_t i = 0; i < setEdit_.segments.size(); ++i) {
        const AnimSetSeg& sg = setEdit_.segments[i];
        if (sg.name.empty()) {
            setMsg_ = "× 动画 " + std::to_string(i) + " 名字为空";
            setOk_ = false;
            return false;
        }
        if (!seen.insert(sg.name).second) {
            setMsg_ = "× 重名动画「" + sg.name + "」（按名解析会歧义）";
            setOk_ = false;
            return false;
        }
        const AssetEntry* c = db.FindByGuid(sg.clipGuid);
        if (!c || c->missing || c->type != AssetType::Clip) {
            setMsg_ = "× 动画「" + sg.name + "」引用悬空（clip 缺失/非 clip）";
            setOk_ = false;
            return false;
        }
    }
    const std::string json = AnimSetToJson(setEdit_);
    const std::string abs = db.AbsolutePath(setEntry);
    if (json.empty()) {
        setMsg_ = "× 写盘失败（序列化产物为空）：" + abs;
        setOk_ = false;
        return false;
    }
    // 落盘前 roundtrip 预验（review 2026-10-02 #5，TrySave 同款防线）
    if (AnimSetData back = ParseAnimSetJson(json); !back.ok || back.segments != setEdit_.segments) {
        setMsg_ = "× 保存中止：序列化 roundtrip 校验失败（段名含非法字符/过长？）";
        setOk_ = false;
        return false;
    }
    if (!WriteFileAtomic(abs, json + "\n")) {
        setMsg_ = "× 写盘失败（磁盘满/权限？）：" + abs;
        setOk_ = false;
        return false;
    }
    app.RescanAssets();
    setOk_ = true;
    setDirty_ = false; // M25：集面脏标随保存清除（clip 面 dirty_ 由 TrySave 管）
    setMsg_ = "√ 集已保存——Enter Play 后生效（按名播放走 Play 时刻快照）";
    return true;
}

bool AnimationPanel::TryCreateSet(EditorApp& app, const std::string& dir,
                                  const std::string& name, std::vector<AnimSetSeg> segs,
                                  std::string& err) {
    // 新建集落盘（TryCreateClip 同款口径）：名字校验 + 撞路拒 + 墓碑复活 + 原子
    // 写 + Rescan → OpenSet。
    AssetDatabase& db = app.Ctx().Assets();
    std::string why; // D7：校验硬化单源
    if (!ValidateAssetName(name, &why)) {
        err = "集名非法：" + why;
        return false;
    }
    std::string d = dir.empty() ? "Assets" : dir;
    while (d.size() > 1 && d.back() == '/') d.pop_back();
    const std::string rel = d == "Assets" ? "Assets/" + name + ".override" : d + "/" + name + ".override";
    if (const AssetEntry* exist = db.FindByPath(rel); exist && !exist->missing) {
        err = "已存在：" + rel;
        return false;
    }
    AnimSetData sd;
    sd.ok = true;
    sd.name = name;
    sd.segments = std::move(segs);
    const std::string abs = db.ProjectRoot() + "/" + rel;
    if (!WriteFileAtomic(abs, AnimSetToJson(sd) + "\n")) {
        err = "写盘失败：" + abs;
        return false;
    }
    app.RescanAssets();
    // 热修③（2026-09-27 用户实测：创建后面板空态无入口）：落盘成功却查无此档
    // = 内部不一致。旧版静默跳过 OpenSet 仍返回 true → 模态正常关闭、setGuid_
    // 未置位 → 面板空态且无任何入口。宁可红字留模态（文件已在盘上，重试会报
    // "已存在"——那本身就是需要人看的异常态）。
    const AssetEntry* ne = db.FindByPath(rel);
    if (!ne || ne->missing) {
        err = "创建成功但重扫未收录（内部不一致，请报障）：" + rel;
        return false;
    }
    OpenSet(ne->guid);
    return true;
}

const AnimationPanel::SegRowInfo& AnimationPanel::LoadSegRow(EditorApp& app,
                                                             const AssetEntry& clip) {
    // 命中判据 = 段文件内容哈希（Rescan 每次重算）——面板保存/外部改动自动失效，
    // 未变时零 IO（一集几十段 × 每帧，解析开销只在变更帧付一次）
    SegRowInfo& r = segRowCache_[clip.guid];
    if (r.hash != 0 && r.hash == clip.hash) return r;
    r = SegRowInfo{};
    r.hash = clip.hash;
    std::ifstream f(app.Ctx().Assets().AbsolutePath(clip), std::ios::binary);
    if (!f) return r;
    std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (ClipData c = ParseClipJson(text); c.ok) {
        r.ok = true;
        r.frames = (int)c.frames.size();
        r.fps = c.fps > 0.0f ? c.fps : 8.0f;
        if (!c.frames.empty()) r.first = c.frames.front();
    }
    return r;
}

void AnimationPanel::DrawSegRowThumb(EditorApp& app, const AssetEntry* sheet, uint32_t cell,
                                     float posX, float posY, float edge) {
    // drawlist 直染（不走 ImGui::Image）：Image 也是 item，盖在 Selectable 上会抢
    // HoveredId——左列双击改名/右键菜单的 IsItemHovered 判定就断了（胶片带同款
    // 结论：交互件 + 覆盖内容 = 内容必须走 drawlist）
    const ImVec2 pos{posX, posY};
    void* tex = nullptr;
    ImVec2 uv0, uv1;
    float aspect = 1.0f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (CellImageParams(app, sheet, cell, tex, uv0, uv1, aspect)) {
        // 等比缩进 edge 框（长边贴边、短边居中）——竖长帧不撑破行高
        float w = edge, h = edge * aspect;
        if (h > edge) {
            w = edge / aspect;
            h = edge;
        }
        const ImVec2 c{pos.x + edge * 0.5f, pos.y + edge * 0.5f};
        dl->AddImage(tex, ImVec2(c.x - w * 0.5f, c.y - h * 0.5f),
                     ImVec2(c.x + w * 0.5f, c.y + h * 0.5f), uv0, uv1);
        return;
    }
    // 占位灰框（悬空/坏档/未导入——具体原因在名字后缀与元信息行）
    dl->AddRectFilled(pos, ImVec2(pos.x + edge, pos.y + edge),
                      ImGui::ColorConvertFloat4ToU32(ImVec4(0.24f, 0.24f, 0.25f, 1.0f)),
                      4.0f);
}

int AnimationPanel::SegRowFramesForTest(size_t idx) const {
    // 缓存由左列绘制侧填充——返回 -1 即"没画过/没解析"，冒烟据此断言左列真的
    // 走到了行渲染（比探针坐标更贴语义：帧数来自 .anim 解析结果）
    if (idx >= setEdit_.segments.size()) return -1;
    const auto it = segRowCache_.find(setEdit_.segments[idx].clipGuid);
    if (it == segRowCache_.end() || it->second.hash == 0) return -1;
    return it->second.frames;
}

void AnimationPanel::DrawLeftColumn(EditorApp& app, float width, float height,
                                    bool sameLineAfter) {
    // v3.2 左列（Godot Animations 列，图标工具条）：集名行（保存 = 右区「保存」
    // 统一落盘）+ 图标行（新建/改名/复制/移除——Delete 图标 = 移除出集，删文件
    // 留右键菜单）+ 搜索 + 动画清单（双击/右键 inline 改名 + 右键菜单）+ 底部
    // inline 新建输入（Enter 建 animation 后保持开 = 连续建）。width/height <0 =
    // 该向填满（宽窗竖列 / 窄窗顶部横条）。内部操作可能 Rescan 重建 entries_——
    // 全程只持 guid，操作后重查。
    AssetDatabase& db = app.Ctx().Assets();
    const uint64_t setGuid = setGuid_;
    const bool ro = app.Ctx().Playing();
    ImGui::BeginChild("##setlist", ImVec2(width, height), ImGuiChildFlags_Borders);

    // 集名（窄输入；动画 = 一段一 .anim，集存引用）。M7a 批① D6：Play 中只读
    //（BeginDisabled 包输入本体）；M25：Enter/失焦且值变 → 立即落盘——集名是
    // 集面唯一"滞留内存"的改动（结构性操作都即时存），即存后无提示丢失窗口
    // 只剩打字中的一瞬（外部重载竞争，登记可接受）。
    ImGui::SetNextItemWidth(-1.0f);
    if (ro) ImGui::BeginDisabled();
    const bool setNameEnter = ImGui::InputTextWithHint(
        "##setname", "集名", &setEdit_.name, ImGuiInputTextFlags_EnterReturnsTrue);
    if (ro) ImGui::EndDisabled();
    if (setEdit_.name != setLoadedName_) setDirty_ = true; // 打字中即置脏
    // 触发判据用「名字相对基准变了」而非 setDirty_——后者也被结构性操作置位，
    // 失焦重触发会替别的失败路径意外重存；名字差值才是本输入的责任面
    if ((setNameEnter || ImGui::IsItemDeactivated()) && setEdit_.name != setLoadedName_) {
        if (AssetEntry* se = db.FindByGuid(setGuid); se && TrySaveSet(app, *se))
            setLoadedName_ = setEdit_.name; // 成功才推进基准（失败保持差值 = 可重试）
    }

    // 图标工具条（作用于当前选中动画；v3.1 文字按钮 → 图标，Godot 同款密度）
    int selIdx = -1;
    for (size_t i = 0; i < setEdit_.segments.size(); ++i)
        if (setEdit_.segments[i].clipGuid == segGuid_) selIdx = (int)i;
    if (ro) ImGui::BeginDisabled();
    if (ui::IconButton(app, IconKind::Add, "##segadd", false)) { // 只输入名字，帧回右区加
        segNewActive_ = true;
        segNewFocus_ = true;
        segNewName_.clear();
        setErr_.clear();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("新建动画（输入名字如 idle → Enter；输入保持开，可连续建）\n建后帧在右侧添加（+ 添加帧 / 拖图）");
    ImGui::SameLine();
    if (selIdx < 0) ImGui::BeginDisabled();
    if (ui::IconButton(app, IconKind::Rename, "##segren", false)) {
        segEditIdx_ = selIdx;
        segEditBuf_ = setEdit_.segments[(size_t)selIdx].name;
        segEditFocus_ = true;
        setErr_.clear();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("重命名（改名动画文件；GUID 随 .meta 不断）");
    if (selIdx < 0) ImGui::EndDisabled();
    ImGui::SameLine();
    if (selIdx < 0) ImGui::BeginDisabled();
    if (ui::IconButton(app, IconKind::Duplicate, "##segdup", false))
        DuplicateSegment(app, (size_t)selIdx);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("复制动画（读源 .anim → 新文件入集）");
    if (selIdx < 0) ImGui::EndDisabled();
    ImGui::SameLine();
    if (selIdx < 0) ImGui::BeginDisabled();
    if (ui::IconButton(app, IconKind::Delete, "##segrem", false))
        RemoveSegmentAt(app, (size_t)selIdx); // M24：失败回滚收口在助手内
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("从集移除（保留动画文件）——删除文件走右键菜单");
    if (selIdx < 0) ImGui::EndDisabled();
    if (ro) ImGui::EndDisabled();

    // 搜索（大小写不敏感子串；建段时自动清——新动画要立刻可见）
    const auto nameContains = [](const std::string& hay, const std::string& needle) {
        if (needle.empty()) return true;
        const auto lo = [](unsigned char c) { return (char)std::tolower(c); };
        std::string h = hay, nd = needle;
        std::transform(h.begin(), h.end(), h.begin(), lo);
        std::transform(nd.begin(), nd.end(), nd.begin(), lo);
        return h.find(nd) != std::string::npos;
    };
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##segfilter", "搜索动画…", &segFilter_);

    // 段清单（inline 改名行 / Selectable + 右键菜单）
    for (size_t i = 0; i < setEdit_.segments.size(); ++i) {
        const AnimSetSeg& sg = setEdit_.segments[i];
        if (!nameContains(sg.name, segFilter_)) continue;
        ImGui::PushID((int)i);
        const AssetEntry* c = db.FindByGuid(sg.clipGuid);
        const bool dangling = !c || c->missing || c->type != AssetType::Clip;
        char lab[128];
        std::snprintf(lab, sizeof(lab), "%s%s", sg.name.c_str(), dangling ? "（悬空）" : "");
        if (segEditIdx_ == (int)i) {
            // inline 改名（Hierarchy 同款：Enter 提交 / Esc 取消；提交校验在
            // CommitSegRename——失败保持输入开改完再 Enter）
            if (segEditFocus_) {
                ImGui::SetKeyboardFocusHere();
                segEditFocus_ = false;
            }
            ImGui::SetNextItemWidth(-14.0f);
            if (ImGui::InputText("##segrename", &segEditBuf_,
                                 ImGuiInputTextFlags_EnterReturnsTrue |
                                     ImGuiInputTextFlags_AutoSelectAll))
                CommitSegRename(app, (int)i);
            if (ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
                segEditIdx_ = -1;
                setErr_.clear();
            }
        } else {
            // 三图优化点①：行 = 首帧小图 + 名 + 帧数/时长元信息（纯名字行要逐个
            // 点开才知道内容——参考 Godot Animations 列带缩略图的可读性）。行交互
            // 归 Selectable（点选/双击改名/右键菜单），内容 drawlist 直染——Image/
            // Text 都是 item，盖上去会抢 HoveredId 断双击与右键。元信息读已存盘
            // 内容：保存触发 Rescan → 内容哈希变 → 下帧自动重读（编辑中未保存的
            // 改动不反映，与 Godot 口径一致）。
            const SegRowInfo* ri = nullptr;
            if (!dangling) ri = &LoadSegRow(app, *c);
            const float lh = ImGui::GetTextLineHeight();
            const float rowH = lh * 2.0f + 7.0f;
            const float thumb = lh * 2.0f - 3.0f;
            if (ImGui::Selectable("##segrow", segGuid_ == sg.clipGuid, 0,
                                  ImVec2(0.0f, rowH))) {
                segGuid_ = sg.clipGuid;
                targetGuid_ = sg.clipGuid; // 右区复用 clip 编辑机制
            }
            if (!ro && ImGui::IsItemHovered() &&
                ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) { // D6：双击入口守卫
                segEditIdx_ = (int)i; // 双击 = inline 改名
                segEditBuf_ = sg.name;
                segEditFocus_ = true;
                setErr_.clear();
            }
            const ImVec2 p0 = ImGui::GetItemRectMin();
            const float textX = p0.x + 6.0f + thumb + 7.0f;
            const bool hasFirst = ri && ri->ok && ri->frames > 0;
            DrawSegRowThumb(app, hasFirst ? db.FindByGuid(ri->first.sheetGuid) : nullptr,
                            hasFirst ? ri->first.cell : 0, p0.x + 6.0f,
                            p0.y + (rowH - thumb) * 0.5f, thumb);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddText(ImVec2(textX, p0.y + 3.0f), ImGui::GetColorU32(ImGuiCol_Text), lab);
            char meta[64];
            if (dangling)
                std::snprintf(meta, sizeof(meta), "%s", "文件丢失");
            else if (!ri->ok)
                std::snprintf(meta, sizeof(meta), "%s", "坏档（右区看详情）");
            else if (ri->frames == 0)
                std::snprintf(meta, sizeof(meta), "%s", "0 帧（右区加帧）");
            else
                std::snprintf(meta, sizeof(meta), "%d 帧 · %.2fs", ri->frames,
                              (float)ri->frames / ri->fps);
            dl->AddText(ImVec2(textX, p0.y + 5.0f + lh),
                        ImGui::GetColorU32(ImGuiCol_TextDisabled), meta);
            if (ImGui::BeginPopupContextItem("##segctx")) {
                if (ro) ImGui::BeginDisabled();
                if (ImGui::MenuItem("重命名（改名动画文件）")) {
                    segEditIdx_ = (int)i;
                    segEditBuf_ = sg.name;
                    segEditFocus_ = true;
                    setErr_.clear();
                }
                if (ImGui::MenuItem("从集移除（保留动画文件）"))
                    RemoveSegmentAt(app, i); // M24：失败回滚收口在助手内
                if (ImGui::MenuItem("删除动画文件（进墓碑）"))
                    DeleteSegmentFile(app, i); // M24 重排：先存集后删文件
                if (ro) ImGui::EndDisabled();
                ImGui::EndPopup();
            }
        }
        ImGui::PopID();
    }
    if (setEdit_.segments.empty()) ImGui::TextDisabled("（空集：＋ 新建动画起步）");

    // inline 新建输入（底部）：Enter = QuickCreateSegment（成功后输入保持开）
    if (segNewActive_) {
        ImGui::Separator();
        if (segNewFocus_) {
            ImGui::SetKeyboardFocusHere();
            segNewFocus_ = false;
        }
        ImGui::SetNextItemWidth(-30.0f);
        if (ImGui::InputText("##segnew", &segNewName_, ImGuiInputTextFlags_EnterReturnsTrue))
            QuickCreateSegment(app);
        if (ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            segNewActive_ = false;
            setErr_.clear();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("×##segnewx")) {
            segNewActive_ = false;
            setErr_.clear();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("收起新建输入");
    }
    if (!setErr_.empty())
        ImGui::TextColored(theme::kTextError, "%s", setErr_.c_str());
    else if (!setMsg_.empty())
        ImGui::TextColored(setOk_ ? theme::kTextOk : theme::kTextError, "%s", setMsg_.c_str());
    else if (!ro)
        ImGui::TextDisabled("动画 = 一段一 .anim；运行时按所在集解析名称");
    ImGui::EndChild();
    if (sameLineAfter) ImGui::SameLine(); // 宽窗并排；窄窗堆叠 = 不回行
}

void AnimationPanel::QuickCreateSegment(EditorApp& app) {
    // v3 建段 = 只输入名字：空 .anim 落盘（TryCreateClip 不要求 ≥1 帧——保存侧
    // 才校验）+ 入集 + 选中；输入保持开、名清空 = 连续建段。fps/循环用默认，
    // 在右区工具条随手改（旧版大表单把参数+选图前置 = 割离操作的主痛点）。
    if (app.Ctx().Playing()) { // D6 commit 双判（＋按钮已随工具条禁用）
        setErr_ = "Play 中不可新建动画（沙盒只读）——退出 Play 后再试";
        segNewActive_ = false;
        return;
    }
    AssetDatabase& db = app.Ctx().Assets();
    std::string name = segNewName_;
    while (!name.empty() && (name.front() == ' ' || name.front() == '\t'))
        name.erase(name.begin());
    while (!name.empty() && (name.back() == ' ' || name.back() == '\t')) name.pop_back();
    std::string why; // D7：校验硬化单源（旧校验只拦空/路径段/..）
    if (!ValidateAssetName(name, &why)) {
        setErr_ = "名字非法：" + why;
        return;
    }
    for (const AnimSetSeg& sg : setEdit_.segments)
        if (sg.name == name) {
            setErr_ = "集内已有同名动画「" + name + "」";
            return;
        }
    const AssetEntry* se = db.FindByGuid(setGuid_);
    if (!se) {
        setErr_ = "集条目丢失（重扫后重试）";
        return;
    }
    ClipData cd;
    cd.ok = true;
    cd.name = name;
    cd.fps = 8.0f;
    cd.loopMode = 1;
    std::string err;
    if (!TryCreateClip(app, cd, SetClipDir(*se), err)) {
        setErr_ = err; // 集文件夹内撞名/写盘失败（跨集同名由子文件夹隔离）
        return;
    }
    // TryCreateClip 已 Rescan + SetTarget(新段)——集条目重查
    const uint64_t newSeg = targetGuid_;
    if (AssetEntry* se2 = db.FindByGuid(setGuid_)) {
        setEdit_.segments.push_back({name, newSeg});
        setDirty_ = true;
        if (TrySaveSet(app, *se2)) {
            segGuid_ = newSeg;
            selFrame_ = -1; // 新段空帧——清旧段选中态
            selSet_.clear();
            selAnchor_ = -1;
            previewFrame_ = 0;
            previewing_ = false;
            segNewName_.clear(); // 输入保持开 = 连续建段
            segNewFocus_ = true;
            segFilter_.clear(); // 新段要立刻可见
            setErr_.clear();
        } else {
            // M24：集保存失败回滚已 push 的段行（.anim 文件保留——它是合法独立
            // 资产，浏览器可删；集档不收 = 下次重载不见，状态一致）
            setEdit_.segments.pop_back();
            setErr_ = "× 已建动画文件但入集保存失败（" +
                      (setMsg_.size() > 1 ? setMsg_.substr(setMsg_.find(' ') + 1)
                                          : "写盘失败") +
                      "）——修因后重试（文件在资产浏览器可见）";
        }
    } else {
        setErr_ = "集条目丢失（重扫后重试）";
    }
}

// ---- M7a 批① M24：集面结构性操作收口------------------------------------------
// 此前六处 TrySaveSet 返回值被吞：行已抹/文件已删而 .override 落盘失败 = 盘上
// 集与编辑态分叉（报告 M24 的"最重路径"= 先删文件后存集，失败即悬空引用）。
// 统一语义：内存先改 → TrySaveSet → 失败回滚内存态 + 红字；不可逆步（删文件）
// 排最后且仅在集保存成功后做。
void AnimationPanel::RemoveSegmentAt(EditorApp& app, size_t i) {
    AssetDatabase& db = app.Ctx().Assets();
    if (i >= setEdit_.segments.size()) return;
    const uint64_t setGuid = setGuid_;
    const AnimSetSeg removed = setEdit_.segments[i]; // 拷值——erase 后原引用失效
    setEdit_.segments.erase(setEdit_.segments.begin() + (long)i);
    if (segEditIdx_ == (int)i) segEditIdx_ = -1;
    else if (segEditIdx_ > (int)i) --segEditIdx_;
    if (segGuid_ == removed.clipGuid) { // 选中动画已移除 → 右区回落提示行
        segGuid_ = 0;
        targetGuid_ = 0;
    }
    setDirty_ = true; // M25：集面改动置脏（成功保存在 TrySaveSet 内清除）
    AssetEntry* se = db.FindByGuid(setGuid);
    if (se && TrySaveSet(app, *se)) {
        setErr_.clear();
        return;
    }
    // 回滚：文件未动 = 行可无损插回原位。取因只在 TrySaveSet 真跑过时读 setMsg_
    //（se 缺席时它是上一操作的陈旧消息）
    const std::string reason =
        se ? (setMsg_.size() > 1 ? setMsg_.substr(setMsg_.find(' ') + 1) : "写盘失败")
           : std::string("集条目丢失（重扫后重试）");
    setEdit_.segments.insert(setEdit_.segments.begin() + (long)i, removed);
    segEditIdx_ = -1; // 索引修回易错，直接收起 inline 改名（保守复位）
    setErr_ = "× 集保存失败，已还原本次移除（" + reason + "）——修因后重试";
}

void AnimationPanel::DeleteSegmentFile(EditorApp& app, size_t i) {
    // M24 重排：先从集面移除并存盘（引用面先断），成功后才删文件。旧序 =
    // db.Remove 先删盘上 .anim → TrySaveSet 失败被吞 → .override 仍列已删 clip
    //（下次重载才以"（悬空）"暴露，且无回滚）。
    AssetDatabase& db = app.Ctx().Assets();
    if (i >= setEdit_.segments.size()) return;
    const uint64_t setGuid = setGuid_;
    const AnimSetSeg removed = setEdit_.segments[i]; // 拷值（旧代码 erase 后读 sg = UB）
    setEdit_.segments.erase(setEdit_.segments.begin() + (long)i);
    if (segEditIdx_ == (int)i) segEditIdx_ = -1;
    else if (segEditIdx_ > (int)i) --segEditIdx_;
    if (segGuid_ == removed.clipGuid) {
        segGuid_ = 0;
        targetGuid_ = 0;
    }
    setDirty_ = true;
    AssetEntry* se = db.FindByGuid(setGuid);
    if (se && TrySaveSet(app, *se)) {
        // TrySaveSet 内已 Rescan——条目重查后才动不可逆步
        if (AssetEntry* ce = db.FindByGuid(removed.clipGuid)) db.Remove(*ce);
        setErr_.clear();
        return;
    }
    const std::string reason =
        se ? (setMsg_.size() > 1 ? setMsg_.substr(setMsg_.find(' ') + 1) : "写盘失败")
           : std::string("集条目丢失（重扫后重试）");
    setEdit_.segments.insert(setEdit_.segments.begin() + (long)i, removed);
    segEditIdx_ = -1;
    setErr_ = "× 集保存失败，动画文件未删（" + reason + "）——修因后重试";
}

void AnimationPanel::CommitSegRename(EditorApp& app, int idx) {
    // inline 改名提交：校验（合法/集内唯一）→ db.Rename 段文件 → 集段名同步 →
    // TrySaveSet。失败保持输入开（setErr_ 提示，改完再 Enter）。Rename 原位改 +
    // 排序——此后只用 guid。
    if (app.Ctx().Playing()) { // D6 commit 双判（入口三处已禁，此处防注入/竞态）
        setErr_ = "Play 中不可改名（沙盒只读）——退出 Play 后再试";
        segEditIdx_ = -1;
        return;
    }
    AssetDatabase& db = app.Ctx().Assets();
    const std::string& name = segEditBuf_;
    std::string why; // D7：校验硬化单源（拒引号/控制字符/超长——旧校验只拦路径段）
    if (!ValidateAssetName(name, &why)) {
        setErr_ = "名字非法：" + why;
        return;
    }
    if (idx < 0 || idx >= (int)setEdit_.segments.size()) {
        segEditIdx_ = -1;
        return;
    }
    for (size_t k = 0; k < setEdit_.segments.size(); ++k)
        if (k != (size_t)idx && setEdit_.segments[k].name == name) {
            setErr_ = "集内已有同名动画「" + name + "」";
            return;
        }
    const uint64_t clipGuid = setEdit_.segments[(size_t)idx].clipGuid;
    if (const AssetEntry* ce = db.FindByGuid(clipGuid)) {
        // v3.3：改名目标 = 集子文件夹（跨集同名隔离；源在旧位置也一并归位）
        const std::string oldRel = ce->relPath; // Rename 会改条目——先存回滚路径
        std::string newRel;
        if (const AssetEntry* se = db.FindByGuid(setGuid_))
            newRel = SetClipDir(*se) + "/" + name + ".anim";
        else {
            const std::string dir = ce->Dir();
            newRel = (dir.empty() ? "Assets" : dir) + "/" + name + ".anim";
        }
        if (AssetEntry* ceMut = db.FindByGuid(clipGuid); db.Rename(*ceMut, newRel)) {
            setDirty_ = true;
            AssetEntry* se = db.FindByGuid(setGuid_);
            if (se && TrySaveSet(app, *se)) {
                setEdit_.segments[(size_t)idx].name = name; // M24：集保存成功才同步段名
                segEditIdx_ = -1;
                setErr_.clear();
                return;
            }
            // 取因只在 TrySaveSet 真跑过时读 setMsg_（se 缺席时它是陈旧消息）
            const std::string reason =
                se ? (setMsg_.size() > 1 ? setMsg_.substr(setMsg_.find(' ') + 1)
                                         : "写盘失败")
                   : std::string("集条目丢失（重扫后重试）");
            // M24：集保存失败回滚——文件名改回 + 段名未动 + 输入保持开
            if (AssetEntry* ce2 = db.FindByGuid(clipGuid);
                ce2 && db.Rename(*ce2, oldRel)) {
                setErr_ = "× 集保存失败，文件名已还原（" + reason + "）——修因后再提交";
            } else {
                setErr_ = "× 集保存失败且文件名回滚也失败（" + newRel +
                          "）——盘上文件名与集段名已分叉，请手动对齐后重试";
            }
        } else {
            setErr_ = "重命名失败（目标已存在/IO）";
        }
    } else {
        setErr_ = "段文件条目丢失";
    }
}

void AnimationPanel::DuplicateSegment(EditorApp& app, size_t idx) {
    // 复制段：读源 .anim → 撞名后缀 -copy/-2.. 落盘 → 入集 + 选中（相似动作
    // atk/atk2 的量产通道——Godot duplicate 同款）
    if (app.Ctx().Playing()) { // D6 commit 双判（工具条按钮已随 ro 禁用）
        setErr_ = "Play 中不可复制动画（沙盒只读）——退出 Play 后再试";
        return;
    }
    AssetDatabase& db = app.Ctx().Assets();
    if (idx >= setEdit_.segments.size()) return;
    const AnimSetSeg& src = setEdit_.segments[idx];
    const AssetEntry* ce = db.FindByGuid(src.clipGuid);
    if (!ce || ce->missing || ce->type != AssetType::Clip) {
        setErr_ = "源段条目悬空，不可复制";
        return;
    }
    ClipData cd;
    {
        std::ifstream f(db.AbsolutePath(*ce), std::ios::binary);
        if (f) {
            std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            cd = ParseClipJson(text);
        }
        if (!cd.ok) {
            setErr_ = "源段解析失败：" + cd.error;
            return;
        }
    }
    // 撞名探测：集内段名唯一 + 集子文件夹内不撞路（v3.3 落位；TryCreateClip 同款口径）
    const AssetEntry* se = db.FindByGuid(setGuid_);
    if (!se) {
        setErr_ = "集条目丢失（重扫后重试）";
        return;
    }
    const std::string clipDir = SetClipDir(*se);
    const auto relOf = [](std::string dir, const std::string& n) {
        if (dir.empty()) dir = "Assets";
        while (dir.size() > 1 && dir.back() == '/') dir.pop_back();
        return dir == "Assets" ? "Assets/" + n + ".anim" : dir + "/" + n + ".anim";
    };
    std::string name = src.name + "-copy";
    for (int n = 2; n < 1000; ++n) {
        bool clash = false;
        for (const AnimSetSeg& sg : setEdit_.segments)
            if (sg.name == name) clash = true;
        if (const AssetEntry* ex = db.FindByPath(relOf(clipDir, name)); ex && !ex->missing)
            clash = true;
        std::error_code ecx;
        if (std::filesystem::exists(db.ProjectRoot() + "/" + relOf(clipDir, name), ecx))
            clash = true; // 磁盘孤儿同拒
        if (!clash) break;
        name = src.name + "-" + std::to_string(n);
    }
    cd.name = name;
    std::string err;
    if (!TryCreateClip(app, cd, clipDir, err)) {
        setErr_ = err;
        return;
    }
    const uint64_t newSeg = targetGuid_;
    if (AssetEntry* se2 = db.FindByGuid(setGuid_)) {
        setEdit_.segments.push_back({name, newSeg});
        setDirty_ = true;
        if (TrySaveSet(app, *se2)) {
            segGuid_ = newSeg;
            segFilter_.clear();
            setErr_.clear();
        } else {
            // M24：集保存失败回滚段行（副本文件保留——合法独立资产，同 QuickCreate）
            setEdit_.segments.pop_back();
            setErr_ = "× 已写副本文件但入集保存失败（" +
                      (setMsg_.size() > 1 ? setMsg_.substr(setMsg_.find(' ') + 1)
                                          : "写盘失败") +
                      "）——修因后重试";
        }
    }
}

void AnimationPanel::DrawSetModals(EditorApp& app) {
    // 新建集模态。**边沿触发**（T3b 修正批：每帧 OpenPopup 破坏弹窗栈序）；
    // Begin 失败 = 复位僵尸开态。Rescan 后条目引用失效——全程只用 guid。
    //（v3：段重命名改左列 inline 输入——弹窗只留新建集。）
    AssetDatabase& db = app.Ctx().Assets();

    if (setCreateOpen_) {
        testhooks::Stash("animset.queued", ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        if (setCreatePending_) {
            ImGui::OpenPopup("新建动画集");
            setCreatePending_ = false;
        }
        ImGui::SetNextWindowSize(ImVec2(480.0f, 0.0f), ImGuiCond_Appearing);
        if (!ImGui::BeginPopupModal("新建动画集", nullptr, ImGuiWindowFlags_NoSavedSettings)) {
            // 外力关 ≠ 弃单（2026-09-27 热修③，用户实测：右键新建动画集开面板同瞬
            // 排队，首开窗口停靠换代把弹窗一帧杀掉 → 空面板无入口）。用户侧关闭
            //（创建/取消/Escape）都先行清 setCreateOpen_，落到这里的失败必是外力
            //——重排队下帧重开，而非复位弃单。
            setCreatePending_ = true;
        } else {
            ImGui::InputText("集名", &setCreateName_);
            ImGui::TextDisabled("落点：%s/", setCreateDir_.c_str());
            if (!setCreateSrc_.empty()) {
                size_t n = 0;
                for (const AssetEntry* e : db.EntriesInDir(setCreateSrc_))
                    if (e->type == AssetType::Sprite && !e->missing) ++n;
                ImGui::Checkbox("并从源目录图片建首段", &setCreateWithSeg_);
                ImGui::SameLine();
                ImGui::TextDisabled("%s（%zu 张）", setCreateSrc_.c_str(), n);
            }
            if (!setErr_.empty()) ImGui::TextColored(theme::kTextError, "%s", setErr_.c_str());
            if (ImGui::Button("创建", ImVec2(120, 0))) {
                std::vector<AnimSetSeg> segs;
                bool go = true;
                if (setCreateWithSeg_ && !setCreateSrc_.empty()) {
                    // 首段 = 源目录全部图片按文件名序（一帧一图）；段/文件名 = 目录叶名
                    ClipData cd;
                    cd.ok = true;
                    const size_t slash = setCreateSrc_.find_last_of('/');
                    cd.name =
                        slash == std::string::npos ? setCreateSrc_ : setCreateSrc_.substr(slash + 1);
                    cd.fps = 8.0f;
                    cd.loopMode = 1;
                    for (const AssetEntry* e : db.EntriesInDir(setCreateSrc_))
                        if (e->type == AssetType::Sprite && !e->missing)
                            cd.frames.push_back({e->guid, 0});
                    if (cd.frames.empty()) {
                        setErr_ = "源目录没有图片";
                        go = false;
                    } else {
                        std::string err;
                        // v3.3：首段落集子文件夹（集尚未落盘——目录按 <落点>/<集名> 推算）
                        const std::string firstDir = setCreateDir_ + "/" + setCreateName_;
                        if (!TryCreateClip(app, cd, firstDir, err)) {
                            setErr_ = err;
                            go = false;
                        } else {
                            segs.push_back({cd.name, targetGuid_}); // SetTarget 已指新段
                        }
                    }
                }
                if (go && TryCreateSet(app, setCreateDir_, setCreateName_, std::move(segs),
                                       setErr_)) {
                    // OpenSet 已置 setGuid_；首段已由 TryCreateClip 选中
                    if (targetGuid_ == 0) segGuid_ = 0;
                    setCreateOpen_ = false;
                    ImGui::CloseCurrentPopup();
                }
            }
            // 热修③注入位：smoke-anim 经 TestHooks 真实点击"创建"走完整模态链
            testhooks::Stash("animset.create", ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
            ImGui::SameLine();
            if (ImGui::Button("取消", ImVec2(80, 0)) ||
                (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !ImGui::IsAnyItemActive())) {
                setCreateOpen_ = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }
}

// ------------------------------------------------------------ 新建向导 ----

void AnimationPanel::StartCreateFromFolder(const std::string& relDir,
                                           const std::string& destDir) {
    // 唯一创建入口（T3-UX7 向导退役重设计）：文件夹右键 → 极简创建框。
    ResetEditingState(); // 同新建集口径：弹窗背后不残留旧集/旧剪辑
    wizOpen_ = true;
    wizPending_ = true;
    wizDir_ = relDir.empty() || relDir == "Assets" ? "Assets" : relDir;
    const size_t slash = wizDir_.find_last_of('/');
    wizName_ = slash == std::string::npos ? wizDir_ : wizDir_.substr(slash + 1);
    if (wizName_.empty() || wizName_ == "Assets") wizName_ = "new-clip";
    wizSaveDir_ = destDir.empty() ? "Assets" : destDir; // 保存位置 = 浏览器当前目录
    wizErr_.clear();
}

void AnimationPanel::DrawFolderCreate(EditorApp& app) {
    // 极简文件夹创建框（T3-UX7，用户实测"三 tab 向导配置杂乱"驱动重设计）：
    // 单页无 tab——源文件夹右键时上下文已定（换源 = 去浏览器点别的文件夹），
    // 保存位置固定 = 浏览器当前目录（Unity 同款无位置字段，只读行提示——
    // "落点"一词退役），创建后自动进面板可改名/调参。原向导的精灵表/空白
    // 创建通道随之退役：集内 inline 新建段 + 加帧四通道覆盖（先起名后选帧，
    // 顺序更顺）；独立 clip 仅此一入口。wizPending_ = 边沿触发（同新建集）。
    if (!wizOpen_) return;
    if (wizPending_) {
        ImGui::OpenPopup("从文件夹创建动画");
        wizPending_ = false;
    }
    ImGui::SetNextWindowSize(ImVec2(460.0f, 0.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("从文件夹创建动画", nullptr,
                                ImGuiWindowFlags_NoSavedSettings |
                                    ImGuiWindowFlags_AlwaysAutoResize)) {
        wizPending_ = true; // 外力关（首开停靠换代杀弹窗）→ 重排队（新建集同口径）
        return;
    }
    std::vector<const AssetEntry*> imgs;
    for (const AssetEntry* e : app.Ctx().Assets().EntriesInDir(
             wizDir_.empty() ? "Assets" : wizDir_))
        if (e->type == AssetType::Sprite && !e->missing) imgs.push_back(e);
    ImGui::TextDisabled("%s · %zu 张图（文件名序逐张入帧）", wizDir_.c_str(), imgs.size());
    const float avail = ImGui::GetContentRegionAvail().x;
    const float step = 40.0f + ImGui::GetStyle().ItemSpacing.x;
    const int fcols = std::max(1, (int)(avail / step));
    int fcol = 0;
    for (size_t i = 0; i < imgs.size() && i < 12; ++i) {
        if (fcol++ > 0) ImGui::SameLine();
        DrawCellImage(app, imgs[i], 0, 40.0f);
        if (fcol >= fcols) fcol = 0;
    }
    if (imgs.empty()) ImGui::TextColored(theme::kTextError, "该文件夹没有图片");
    ImGui::TextDisabled("保存位置：%s", wizSaveDir_.c_str());
    ImGui::InputText("名称", &wizName_);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(72);
    ImGui::DragInt("fps##wiz", &wizFps_, 1.0f, 1, 60);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(110);
    ImGui::Combo("循环##wiz", &wizLoop_, kLoopNames, 3);
    if (!wizErr_.empty()) ImGui::TextColored(theme::kTextError, "%s", wizErr_.c_str());

    if (ImGui::Button("创建", ImVec2(140, 0)) ||
        ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
        ClipData cd;
        cd.ok = true;
        cd.name = wizName_;
        cd.fps = (float)wizFps_;
        cd.loopMode = wizLoop_;
        if (imgs.empty()) {
            wizErr_ = "文件夹无图片";
        } else {
            for (const AssetEntry* e : imgs) cd.frames.push_back({e->guid, 0});
            if (TryCreateClip(app, cd, wizSaveDir_, wizErr_)) {
                wizOpen_ = false;
                ImGui::CloseCurrentPopup();
            }
        }
    }
    testhooks::Stash("clipcreate.ok", ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    ImGui::SameLine();
    if (ImGui::Button("取消") ||
        (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !ImGui::IsAnyItemActive())) {
        wizOpen_ = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}


// ------------------------------------------------------------ 统一保存 ----

bool AnimationPanel::SaveAll(EditorApp& app) {
    // v3 统一保存：段 TrySave 成功 → 集模式连存集（集名等集面改动也在此落盘
    // ——旧版"保存集/保存"两个按钮分居两处，易漏存一半）。Rescan 重建
    // entries_——全程 guid 重查（TrySave/TrySaveSet 内各自 Rescan）。
    AssetDatabase& db = app.Ctx().Assets();
    const AssetEntry* t = db.FindByGuid(targetGuid_);
    if (!t || t->missing || t->type != AssetType::Clip) {
        // 悬空主因：动画文件被删（墓碑——重扫救不回）或被旧版跨集同名覆盖。
        // 左列该动画标「（悬空）」——右键从集移除后重建（v3.3 起重建落集子文件夹，
        // 不再与别的集撞文件）。
        saveMsg_ = "× 动画文件已丢失（重扫无效）：左列该动画会标「悬空」——"
                   "右键「从集移除」后重新建（新文件落集子文件夹，不再跨集相撞）";
        saveOk_ = false;
        return false;
    }
    if (!TrySave(app, *t)) return false; // TrySave 内 Rescan——此后重查
    if (setGuid_ != 0) {
        if (AssetEntry* se = db.FindByGuid(setGuid_)) {
            if (!TrySaveSet(app, *se)) {
                saveMsg_ = setMsg_; // 集失败状态也上面板（预览条右缘，旧版只在集头行）
                saveOk_ = setOk_;
                return false;
            }
            saveMsg_ = "√ 段+集已保存——Enter Play 后生效（进行中的局用旧快照）";
        }
    }
    return true;
}

// ------------------------------------------------------------ 右区三层 ----

void AnimationPanel::DrawFrameToolbar(EditorApp& app, bool ro) {
    // v3.1 工具条：加帧四通道下拉（空帧/从精灵表/多图/从 .anim）+ 帧操作 | 播放
    // 传输（Play 态照常可用）| fps/循环/裸 clip 名称 | 保存（集模式 = 段+集连存）
    if (ro) ImGui::BeginDisabled();
    if (ImGui::Button("+ 添加帧")) ImGui::OpenPopup("##addframe");
    if (ImGui::BeginPopup("##addframe")) {
        if (ImGui::MenuItem("空帧（插到选中后）")) {
            ClipFrame nf{};
            if (selFrame_ >= 0 && selFrame_ < (int)edit_.frames.size())
                nf = edit_.frames[(size_t)selFrame_];
            else if (!edit_.frames.empty())
                nf = edit_.frames.back();
            else {
                for (const auto& e : app.Ctx().Assets().Entries()) // 空表起步：首个精灵
                    if (e.type == AssetType::Sprite && !e.missing && e.spriteId != 0) {
                        nf.sheetGuid = e.guid;
                        break;
                    }
            }
            InsertFrameAfter(selFrame_ >= 0 ? selFrame_ : (int)edit_.frames.size(),
                             nf); // 有选中 = 插其后；无选中 = 末尾追加（-1 是头插，勿传）
        }
        if (ImGui::MenuItem("从精灵表…（选图 → 分割 → 选帧）")) StartSheetPick(app);
        if (ImGui::MenuItem("从图片文件…（多选，整图入帧）")) StartImageFilePick(app);
        if (ImGui::MenuItem("从动画剪辑 (.anim) 复制…")) {
            clipPickOpen_ = true;
            clipPickPending_ = true; // 边沿触发
        }
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("保存")) SaveAll(app); // v3.2 提前——窄窗工具条溢出时保命键先活
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("直写资产文件（Enter Play 后生效；不走场景 undo）%s",
                          setGuid_ != 0 ? "——集模式连存动画+集" : "");
    ImGui::SameLine();
    if (ImGui::Button("复制帧")) DuplicateSelectedFrames();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("复制选中帧 (Ctrl+D)");
    ImGui::SameLine();
    if (ImGui::Button("删除帧")) DeleteSelectedFrames();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("删除选中帧 (Del/Backspace)");
    if (ro) ImGui::EndDisabled();

    ImGui::SameLine();
    if (ro) ImGui::BeginDisabled();
    ImGui::SetNextItemWidth(52);
    if (ImGui::DragInt("fps", &fpsI_, 1.0f, 1, 60)) {
        dirty_ = true;
        saveMsg_.clear();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(88);
    if (ImGui::Combo("循环", &edit_.loopMode, kLoopNames, 3)) {
        dirty_ = true;
        saveMsg_.clear();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Once 播完钳末帧 / Loop 回绕 / PingPong 往返（0..n-1..0）\n"
                          "档面 = 创建默认；实体 Inspector 的 LoopMode = 运行时权威");
    if (setGuid_ == 0) { // 裸 clip：名称就地编辑（集模式名称走左列改名——文件+集双写）
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110);
        if (ImGui::InputText("名称", &edit_.name)) {
            dirty_ = true;
            saveMsg_.clear();
        }
    }
    if (ro) ImGui::EndDisabled();
}

void AnimationPanel::DrawPreview(EditorApp& app, bool ro, int shown) {
    // v3.1 预览条（修"右侧编辑区空白"：预览从大块 child 改为紧凑一行，帧网格
    // 主体化吃剩余空间）：传输图标 + 当前帧图 + 信息两行 + 状态。拖 Assets
    // 图 = 加帧（sprite）/ 复制帧表（.anim）。
    AssetDatabase& db = app.Ctx().Assets();
    const int n = (int)edit_.frames.size();
    const AssetEntry* sheet = nullptr;
    if (n) FrameResolvable(db, edit_.frames[(size_t)shown], sheet);

    if (ui::IconButton(app, IconKind::Stop, "##animrew", false)) {
        previewFrame_ = 0;
        previewing_ = false;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("回起点");
    ImGui::SameLine();
    if (ui::IconButton(app, IconKind::Step, "##animprev", false, ImVec2(18, 18),
                       /*flipX=*/true)) { // 镜像同形 glyph——与「下一帧」区分方向
        if (n) previewFrame_ = (previewFrame_ + n - 1) % n;
        previewing_ = false;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("上一帧");
    ImGui::SameLine();
    if (ui::IconButton(app, IconKind::Step, "##animnext", false)) {
        if (n) previewFrame_ = (previewFrame_ + 1) % n;
        previewing_ = false;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("下一帧");
    ImGui::SameLine();
    if (ui::IconButton(app, previewing_ ? IconKind::Pause : IconKind::Play, "##animplay",
                       previewing_)) {
        previewing_ = !previewing_;
        previewT0_ = ImGui::GetTime();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("播放/暂停 (Space)");
    ImGui::SameLine();

    // 当前帧图（高 ≤72、宽 ≤160，等比；空帧 = 占位）
    float aspect = 1.0f;
    uint32_t pw = 0, ph = 0;
    if (sheet && app.AssetGpu().PageInfo(sheet->guid, pw, ph) && pw && ph) {
        uint32_t cwp = pw, chp = ph;
        if (sheet->Sliced()) {
            const uint32_t cols = sheet->gridCols ? sheet->gridCols : 1;
            const uint32_t rows = sheet->gridRows ? sheet->gridRows : 1;
            cwp = sheet->cellW ? sheet->cellW : pw / cols;
            chp = sheet->cellH ? sheet->cellH : ph / rows;
        }
        aspect = (float)cwp / (float)chp;
    }
    const float edge = std::clamp(72.0f * aspect, 28.0f, 160.0f);
    DrawCellImage(app, sheet, n ? edit_.frames[(size_t)shown].cell : 0, edge,
                  "##pvimg"); // T3-UX9b：交互化——拖入加帧目标挂真实交互件
    if (!ro && ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* pay = ImGui::AcceptDragDropPayload("LemonAsset")) {
            AssetDragPayload d{};
            std::memcpy(&d, pay->Data, sizeof(d));
            if (d.kind == 0)
                InsertFrameAfter((int)edit_.frames.size(), {d.guid, 0}); // sprite = 末尾加帧
            else if (d.kind == 4)
                AppendClipFrames(app, d.guid); // .anim = 复制帧表
        }
        ImGui::EndDragDropTarget();
    }
    if (ImGui::IsItemHovered() && !ro)
        ImGui::SetTooltip("拖 Assets 精灵图 = 加帧；拖 .anim = 复制其帧表");
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::TextDisabled("帧 %d / %d · %.3fs/帧 · 总长 %.2fs", n ? shown + 1 : 0, n,
                        1.0f / std::max(fpsI_, 1), (float)n / std::max(fpsI_, 1));
    if (!saveMsg_.empty())
        ImGui::TextColored(saveOk_ ? theme::kTextOk : theme::kTextError, "%s", saveMsg_.c_str());
    else if (dirty_)
        ImGui::TextColored(theme::kTextWarn, "有未保存改动");
    else
        ImGui::TextDisabled("%s", edit_.loopMode == 2   ? "PingPong 往返"
                                 : edit_.loopMode == 1 ? "循环"
                                                       : "单次（播完钳末帧）");
    ImGui::EndGroup();
}

void AnimationPanel::DrawRightArea(EditorApp& app, bool ro) {
    // v3.2 右区（宿主 = ##right child）：ro 横幅 → 工具条 → 预览条 → 帧网格
    // （属性行 T3-UX8 退役）。时钟推进在内（shown 供预览与播放游标共用）。
    if (ro)
        ImGui::TextColored(theme::kTextWarn,
                           "Play 进行中：面板只读（当前局用进 Play 时刻快照，改动下一局生效）");

    DrawFrameToolbar(app, ro);

    // 播放时钟推进（纯预览，不进模拟/回放；PingPong 映射与 AnimatorSystem 同源）
    const int n = (int)edit_.frames.size();
    int shown = previewFrame_;
    if (previewing_ && n > 0) {
        const uint64_t idx = (uint64_t)((ImGui::GetTime() - previewT0_) * (double)fpsI_);
        if (edit_.loopMode == 2 && n > 1) { // PingPong
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
    DrawPreview(app, ro, shown);

    // 帧网格（主体，flex 高度 = 剩余全取——属性行已退役，底部不再预留一行）
    if (ImGui::SmallButton("-##zoomout")) stripEdge_ = std::max(kStripEdgeMin, stripEdge_ - 8.0f);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("缩小帧缩略图");
    ImGui::SameLine();
    if (ImGui::SmallButton("+##zoomin")) stripEdge_ = std::min(kStripEdgeMax, stripEdge_ + 8.0f);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("放大帧缩略图（网格 Ctrl+滚轮）");
    ImGui::SameLine();
    ImGui::TextDisabled("帧列表（单击选中 · Ctrl/Shift+点击多选 · ←/→ 移动选择 · "
                        "拖拽重排 · 拖图入格换图/入尾加帧 · Del 删 · Ctrl+D 复制）");
    ImGui::BeginChild("strip", ImVec2(0, 0), ImGuiChildFlags_Borders);
    if (ImGui::IsWindowHovered() && ImGui::GetIO().KeyCtrl &&
        ImGui::GetIO().MouseWheel != 0.0f)
        stripEdge_ = std::clamp(stripEdge_ + ImGui::GetIO().MouseWheel * 6.0f, kStripEdgeMin,
                                kStripEdgeMax);
    if (ro) ImGui::BeginDisabled();
    DrawFilmstrip(app, ro, previewing_ ? shown : -1);
    if (ro) ImGui::EndDisabled();
    ImGui::EndChild();
}

void AnimationPanel::OnGui(EditorApp& app) {
    bool winOpen = true;
    keysFocused_ = false; // 先清（Begin 失败/窗口不绘制时不吃键——防陈旧 true 卡全局）
    if (!ImGui::Begin(Name(), &winOpen, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        testhooks::Stash("animpanel.skip", ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        if (!winOpen) app.ClosePanel(Name()); // × 关面板（T3b-8）
        return;
    }
    testhooks::Stash("animpanel.begin", ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    if (!winOpen) app.ClosePanel(Name());
    keysFocused_ = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows); // 键捕获仲裁

    EditorContext& ctx = app.Ctx();
    AssetDatabase& db = ctx.Assets();
    const bool ro = ctx.Playing(); // 快照语义：进行中的局不吃改档 → 面板只读

    // ---- 模态层（v3.1：目标行已删——切换/打开资产走 AssetBrowser 双击，创建
    // 走浏览器右键；面板 = 纯编辑器）。HandlePickerResult 可能 ImportFile/
    // RescanAssets——此后 OnGui 条目一律 guid 重查，无陈旧指针 ----
    DrawFolderCreate(app);
    DrawSheetPicker(app);
    DrawSetModals(app);
    DrawClipPickModal(app);
    HandlePickerResult(app);

    const AssetEntry* target = db.FindByGuid(targetGuid_);
    const AssetEntry* setEntry = db.FindByGuid(setGuid_);
    if (setEntry && (setEntry->missing || setEntry->type != AssetType::AnimSet))
        setEntry = nullptr;
    if (setGuid_ != 0 && !setEntry) {
        setGuid_ = 0; // 集被删 → 回传统模式
        setLoadedGuid_ = 0;
    }

    // ---- 窄窗自适应（v3.2，修"右侧空白不可编辑"）：可用宽 ≥480 = 左右并排
    // （列宽 = min(230, 30% 可用宽)）；更窄 = 上下堆叠（列变顶部 38% 横条）。
    // 右区一律进 ##right child——内容超宽出滚动条而非裁切丢失 ----
    const float availW = ImGui::GetContentRegionAvail().x;
    const bool wide = availW >= 480.0f;
    if (setEntry) {
        if (setLoadedGuid_ != setGuid_ || setLoadedHash_ != setEntry->hash)
            LoadSetFrom(db, *setEntry);
        if (!setEdit_.ok) {
            ImGui::TextColored(theme::kTextError, "动画集解析失败：%s", setEdit_.error.c_str());
            ImGui::TextDisabled("外部修复 JSON 后点 Assets「重扫」或重选目标");
            ImGui::End();
            return;
        }
        if (wide)
            DrawLeftColumn(app, std::min(kSegListWidth, availW * 0.30f), 0.0f,
                           /*sameLineAfter=*/true);
        else
            DrawLeftColumn(app, 0.0f, ImGui::GetContentRegionAvail().y * 0.38f,
                           /*sameLineAfter=*/false);
        setEntry = db.FindByGuid(setGuid_); // 左列操作可能 Rescan——重查
        target = db.FindByGuid(segGuid_);   // 右区编辑目标 = 选中动画
    } else {
        target = db.FindByGuid(targetGuid_); // 传统模式（T3b 原路径）
    }

    if (!target || target->missing || target->type != AssetType::Clip) {
        ImGui::TextDisabled(setEntry ? "左侧选择动画，或点 ＋ 新建"
                                     : "双击 Assets 中的动画集（.override）或动画剪辑（.anim）打开");
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

    HandleKeys(ro);
    ImGui::BeginChild("##right", ImVec2(0, 0), ImGuiChildFlags_None);
    DrawRightArea(app, ro);
    ImGui::EndChild();
    ImGui::End();
}

} // namespace lemon::editor
