// Lemon 编辑器 — AnimController 面板（M8 批⑨ 三表工作台 / 批⑩a 画布化）
// ADR-013 D6 定形"图管结构 + 检查器管细节"：左画布（节点=状态/边=transition，
// 自绘 ImDrawList，拖动/平移/缩放/适配）+ 右检查器列（b9 三表保留：参数/
// 状态词表/选中出边）。布局随 .controller 顶层可选 editor.nodes 持久化
// （旧档缺省 = 确定性自动布局，运行时/旧档零感知；批⑩b 接连线/检查器细节/
// 撤销重做，本批不动）。编辑期校验是本批灵魂：
//   * 结构 lint（不可达状态）——引擎 ControllerStructuralWarnings 同源，每帧直算；
//     画布灰化用 ControllerReachability（同一 BFS，杜绝两套）。
//   * 跨资产黄字（exitTime×Loop 脚枪）——exitTime 出边状态在项目 .ani 集中绑定
//     的 clip 若非 Once 模式，段末边沿永不触发（Systems.cpp exitTime 只认非 loop
//     收尾边沿的登记边角）。运行时语义不动，编辑面把作者错误拦在写盘前；
//   * 保存门禁 = ParseControllerJson 同一判据（悬空引用/空条件/超 8 参数/
//     重名）+ roundtrip 预验（AnimationPanel TrySave 同款数据丢失防线）。
// 快照语义：Play 态只读横幅；保存后下次 Enter Play 生效。
#define IMGUI_DEFINE_MATH_OPERATORS
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <unordered_map>

#include "App/EditorApp.h"
#include "Assets/AssetDatabase.h"
#include "Components/RenderComponents.h" // AnimGraph/Animator2D（Play 当前态高亮）
#include "Core/Log.h"
#include "ECS/ClipTable.h"
#include "ECS/Scene.h"
#include "EditorContext.h"
#include "Localization/Localization.h"
#include "Panels/BuiltInPanels.h"
#include "Tooling/TestHooks.h"
#include "Tooling/Theme.h"
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"

namespace lemon::editor {
namespace {
constexpr float kNodeW = 160.0f;  // 节点矩形（图空间）
constexpr float kNodeH = 56.0f;
constexpr float kNodeRad = 6.0f;  // 圆角（图空间）

const char* KindLabel(int kind) {
    return kind == 1 ? "bool" : kind == 2 ? "trigger" : "float";
}
const char* OpLabel(int op) {
    static const char* kNames[] = {"<", "<=", ">", ">=", "==", "trigger"};
    return kNames[op >= 0 && op <= 5 ? op : 2];
}
/// 参数 kind 收窄算子域（UI 面收窄；schema 侧只认字符串、无 kind 交叉校验）：
/// trigger 固定 op=5；bool 只 ==（值 0/1）；float 的 trigger 无意义 → 回落 >
int OpForKind(int op, int kind) {
    if (kind == 2) return 5;
    if (kind == 1) return 4;
    return op == 5 ? 2 : op;
}
} // namespace

// ---------------------------------------------------------------- 装载 ----

void AnimControllerPanel::LoadFrom(const AssetDatabase& db, const AssetEntry& e) {
    loadedGuid_ = e.guid;
    loadedHash_ = e.hash;
    dirty_ = false;
    selState_ = -1;
    selEdge_ = -1;
    edgeDragFrom_ = -1;
    dragNode_ = -1;
    renameBuf_.clear();
    undoStack_.Clear(); // 新目标 = 新会话（跨档撤销无意义）
    std::ifstream f(db.AbsolutePath(e), std::ios::binary);
    if (f) {
        std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        edit_ = assets::ParseControllerJson(text);
    } else {
        edit_ = assets::ControllerData{};
        using lemon::editor::loc::trFmt;
        edit_.error = trFmt("animctrl.err.load_failed_fmt", {e.relPath});
    }
    if (edit_.ok && edit_.name.empty())
        edit_.name = std::filesystem::path(e.relPath).stem().string();
    EnsureLayout(); // 旧档无 editor 块 = 内存自动布局（不改 dirty；保存时随档写出）
    canvasFitPending_ = true;
    warnings_.clear();
    lintDirty_ = true;
}

int AnimControllerPanel::StateIndex(const std::string& name) const {
    for (size_t i = 0; i < edit_.states.size(); ++i)
        if (edit_.states[i] == name) return (int)i;
    return -1;
}

// ---------------------------------------------------- 布局（editor.nodes）----

int AnimControllerPanel::LayoutIndexOf(const std::string& state) const {
    for (size_t i = 0; i < edit_.layout.size(); ++i)
        if (edit_.layout[i].state == state) return (int)i;
    return -1;
}

void AnimControllerPanel::EnsureLayout() {
    if (!edit_.ok || edit_.states.empty()) return;
    bool missing = false;
    for (const std::string& s : edit_.states)
        if (LayoutIndexOf(s) < 0) {
            missing = true;
            break;
        }
    if (missing) {
        const std::vector<assets::ControllerNodePos> autoPos =
            assets::ControllerAutoLayoutNodes(edit_);
        for (const assets::ControllerNodePos& np : autoPos)
            if (LayoutIndexOf(np.state) < 0) edit_.layout.push_back(np);
    }
    NormalizeLayout();
}

void AnimControllerPanel::NormalizeLayout() {
    if (edit_.layout.empty()) return;
    std::vector<assets::ControllerNodePos> out;
    out.reserve(edit_.states.size());
    for (const std::string& s : edit_.states)
        if (const int li = LayoutIndexOf(s); li >= 0)
            out.push_back(edit_.layout[(size_t)li]);
    edit_.layout = std::move(out);
}

bool AnimControllerPanel::SetNodePosition(const std::string& state, int x, int y) {
    if (!edit_.ok) return false;
    EnsureLayout();
    const int li = LayoutIndexOf(state);
    if (li < 0) return false;
    const int nx = assets::ControllerSnapGrid(x), ny = assets::ControllerSnapGrid(y);
    if (edit_.layout[(size_t)li].x == nx && edit_.layout[(size_t)li].y == ny) return false;
    edit_.layout[(size_t)li].x = nx;
    edit_.layout[(size_t)li].y = ny;
    dirty_ = true; // 布局不碰结构——不动 lintDirty_（跨资产扫描免重算）
    return true;
}

bool AnimControllerPanel::NodePosForTest(const std::string& state, int& x, int& y) const {
    const int li = LayoutIndexOf(state);
    if (li < 0) return false;
    x = edit_.layout[(size_t)li].x;
    y = edit_.layout[(size_t)li].y;
    return true;
}

bool AnimControllerPanel::StateHandlePosForTest(int stateIdx, float& x, float& y) const {
    if (stateIdx < 0 || stateIdx >= (int)edit_.states.size()) return false;
    const int li = LayoutIndexOf(edit_.states[(size_t)stateIdx]);
    if (li < 0) return false;
    // 与 DrawCanvas 的 handlePos 同式（节点右侧中点；用最近一次绘制的画布原点）
    const assets::ControllerNodePos& np = edit_.layout[(size_t)li];
    x = lastCanvasP0X_ + ((float)np.x + kNodeW) * canvasZoom_ + canvasPanX_;
    y = lastCanvasP0Y_ + ((float)np.y + kNodeH * 0.5f) * canvasZoom_ + canvasPanY_;
    return true;
}

void AnimControllerPanel::EnsureLoaded(EditorApp& app) {
    const AssetEntry* e = app.Ctx().Assets().FindByGuid(targetGuid_);
    if (e && !e->missing && e->type == AssetType::Controller &&
        (loadedGuid_ != e->guid || loadedHash_ != e->hash))
        LoadFrom(app.Ctx().Assets(), *e);
    // 跨资产 lint 同步重算（OnGui 每帧也会做——此处保证无头路径（冒烟/未来
    // 命令行）不开窗也持有最新告警）
    if (edit_.ok && lintDirty_) {
        RecomputeLint(app);
        lintDirty_ = false;
    }
}

// ---------------------------------------------------- 跨资产 lint（黄字）----

void AnimControllerPanel::RecomputeLint(EditorApp& app) {
    // exitTime×Loop 脚枪扫描。词表约定 = 状态名即集段名（ADR-013 D4——集绑定
    // 表的键就是状态名）：对每条 exitTime 出边的源状态，扫项目全部 .ani 集的
    // 同名段，其绑定 clip 非 Once（段末边沿不触发）→ 黄字点名（状态/集/模式）。
    // clip 解析按 (guid,hash) 缓存——Rescan 后 hash 变即失效，其余零 IO；
    // 只在结构变更拍重算（MarkDirty），不是每帧。
    using lemon::editor::loc::tr;
    using lemon::editor::loc::trFmt;
    warnings_.clear();
    if (!edit_.ok) return;
    std::vector<std::string> exitStates;
    for (const assets::ControllerTransitionEdit& t : edit_.transitions)
        if (t.exitTime &&
            std::find(exitStates.begin(), exitStates.end(), t.from) == exitStates.end())
            exitStates.push_back(t.from);
    if (exitStates.empty()) return;
    AssetDatabase& db = app.Ctx().Assets();
    std::unordered_map<uint64_t, std::pair<uint64_t, int>> clipLoop; // guid → {hash, loopMode}
    auto loopOf = [&](const AssetEntry& clip) -> int {
        if (auto it = clipLoop.find(clip.guid);
            it != clipLoop.end() && it->second.first == clip.hash)
            return it->second.second;
        int mode = -1; // -1 = 坏档（另有红字面，不重复报）
        if (std::ifstream f(db.AbsolutePath(clip), std::ios::binary); f) {
            std::string text((std::istreambuf_iterator<char>(f)),
                             std::istreambuf_iterator<char>());
            if (assets::ClipData c = assets::ParseClipJson(text); c.ok) mode = c.loopMode;
        }
        clipLoop[clip.guid] = {clip.hash, mode};
        return mode;
    };
    for (const AssetEntry& se : db.Entries()) {
        if (se.type != AssetType::AnimSet || se.missing) continue;
        std::ifstream f(db.AbsolutePath(se), std::ios::binary);
        if (!f) continue;
        std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        const assets::AnimSetData set = assets::ParseAnimSetJson(text);
        if (!set.ok) continue;
        for (const assets::AnimSetSeg& sg : set.segments) {
            if (std::find(exitStates.begin(), exitStates.end(), sg.name) == exitStates.end())
                continue;
            const AssetEntry* clip = db.FindByGuid(sg.clipGuid);
            if (!clip || clip->missing || clip->type != AssetType::Clip) continue;
            const int mode = loopOf(*clip);
            if (mode == 1 || mode == 2)
                warnings_.push_back(trFmt(
                    "animctrl.warn.exit_loop_fmt",
                    {sg.name,
                     set.name.empty() ? std::filesystem::path(se.relPath).stem().string()
                                      : set.name,
                     mode == 1 ? tr("anim.loop") : tr("anim.loop.pingpong")}));
        }
    }
}

// ---------------------------------------------------------------- 保存 ----

bool AnimControllerPanel::TrySave(EditorApp& app, const AssetEntry& e) {
    using lemon::editor::loc::tr;
    using lemon::editor::loc::trFmt;
    if (!edit_.ok) {
        saveMsg_ = trFmt("animctrl.err.save_bad_fmt", {edit_.error});
        saveOk_ = false;
        return false;
    }
    // entry 显式化（缺省语义 = states[0]，落盘前钉死——roundtrip 等值的前提）
    if (edit_.entry.empty() && !edit_.states.empty()) edit_.entry = edit_.states[0];
    NormalizeLayout(); // 悬空布局键清洗 + states 序（roundtrip 等值前提）
    const std::string abs = app.Ctx().Assets().AbsolutePath(e);
    // 门禁 = 序列化 → ParseControllerJson（与运行时 BuildPlayControllerCache 同一
    // 判据：悬空引用/空条件/超 8 参数/重名）→ roundtrip 等值（序列化器缺陷不
    // 覆写原档，AnimationPanel 同款数据丢失防线）
    const std::string json = assets::ControllerToJson(edit_);
    if (json.empty()) {
        saveMsg_ = trFmt("anim.err.serialize_empty_fmt", {abs});
        saveOk_ = false;
        return false;
    }
    const assets::ControllerData back = assets::ParseControllerJson(json);
    if (!back.ok) {
        saveMsg_ = trFmt("animctrl.err.gate_fmt", {back.error});
        saveOk_ = false;
        return false;
    }
    if (back.name != edit_.name || back.entry != edit_.entry || back.states != edit_.states ||
        back.params != edit_.params || back.transitions != edit_.transitions ||
        back.layout != edit_.layout) {
        saveMsg_ = tr("animctrl.err.roundtrip");
        saveOk_ = false;
        return false;
    }
    // 注意：Rescan 会重建 entries_——调用方在 TrySave 后不得再用本引用
    if (!WriteFileAtomic(abs, json + "\n")) {
        saveMsg_ = trFmt("anim.err.write_failed_fmt", {abs});
        saveOk_ = false;
        return false;
    }
    app.RescanAssets();
    // review B3：保存回读零副作用——落盘内容 = edit_ 序列化（roundtrip 已证等值），
    // loadedHash_ 直接同步新档 hash → 下一帧 OnGui 缓存键命中不触发 LoadFrom
    //（撤销栈/画布相机/选择全保留；换档/外部改动路径照旧重读）
    if (const AssetEntry* e2 = app.Ctx().Assets().FindByGuid(targetGuid_))
        loadedHash_ = e2->hash;
    dirty_ = false;
    lintDirty_ = true;
    saveOk_ = true;
    saveMsg_ = tr("anim.err.saved_hint");
    return true;
}

bool AnimControllerPanel::SaveForTest(EditorApp& app) {
    const AssetEntry* e = app.Ctx().Assets().FindByGuid(targetGuid_);
    return e && !e->missing && e->type == AssetType::Controller && TrySave(app, *e);
}

bool AnimControllerPanel::TryCreate(EditorApp& app, const std::string& dir,
                                    const std::string& name, std::string& err) {
    // 新建落盘（AnimationPanel TryCreateSet 同款口径）：名字校验 + 撞路双查
    //（DB + 磁盘——磁盘孤儿会被 POSIX rename 静默覆盖）+ 建父目录 + 原子写 +
    // Rescan → SetTarget。起步档 = 单状态 Idle（entry 同名）。
    AssetDatabase& db = app.Ctx().Assets();
    using lemon::editor::loc::trFmt;
    std::string why;
    if (!assets::ValidateAssetName(name, &why)) {
        err = trFmt("anim.set.err_set_name_fmt", {why});
        return false;
    }
    std::string d = dir.empty() ? "Assets/Controllers" : dir; // ADR-013 D3 目录约定
    while (d.size() > 1 && d.back() == '/') d.pop_back();
    const std::string rel = d + "/" + name + ".controller";
    if (const AssetEntry* exist = db.FindByPath(rel); exist && !exist->missing) {
        err = trFmt("anim.err.exists_fmt", {rel});
        return false;
    }
    const std::string abs = db.ProjectRoot() + "/" + rel;
    if (std::filesystem::exists(abs)) {
        err = trFmt("anim.err.exists_fmt", {rel});
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(abs).parent_path(), ec);
    assets::ControllerData c;
    c.ok = true;
    c.name = name;
    c.states = {"Idle"};
    c.entry = "Idle";
    if (!WriteFileAtomic(abs, assets::ControllerToJson(c) + "\n")) {
        err = trFmt("anim.err.write_plain_fmt", {abs});
        return false;
    }
    app.RescanAssets();
    const AssetEntry* ne = db.FindByPath(rel);
    if (!ne || ne->missing) {
        err = trFmt("anim.set.err_rescan_missing_fmt", {rel});
        return false;
    }
    SetTarget(ne->guid);
    createOpen_ = false;
    createErr_.clear();
    return true;
}

void AnimControllerPanel::AddState(const std::string& name) {
    if (!edit_.ok || name.empty() || StateIndex(name) >= 0) return;
    PushUndo();
    edit_.states.push_back(name);
    EnsureLayout(); // 新状态补自动布局位（画布立即可见）
    SelectState((int)edit_.states.size() - 1);
    MarkDirty();
}

// ------------------------------------------------- 结构变更（批⑩b）----
// 撤销语义 = 变更前压快照（PushUndo）；拖动类手势在会话起拍、松手有实际位移
// 才 Push（"松手拍一次"）——所有变更走这里的单一入口。

void AnimControllerPanel::SelectState(int idx) {
    selState_ = idx;
    selEdge_ = -1;
    renameBuf_ = (idx >= 0 && idx < (int)edit_.states.size()) ? edit_.states[(size_t)idx] : "";
    renameFocus_ = false;
}

void AnimControllerPanel::SelectEdge(int idx) {
    selEdge_ = idx;
    // 节点选保留（源状态方位感）——检查器按"边优先"渲染
}

void AnimControllerPanel::CommitStateRename(int idx, const std::string& newName) {
    if (idx < 0 || idx >= (int)edit_.states.size() || newName.empty() ||
        newName == edit_.states[(size_t)idx] || StateIndex(newName) >= 0)
        return; // 空/重名/同名 = 静默保旧（段改名 CommitSegRename 同款不扰邻语义）
    PushUndo();
    const std::string old = edit_.states[(size_t)idx];
    edit_.states[(size_t)idx] = newName;
    if (edit_.entry == old) edit_.entry = newName;
    if (const int li = LayoutIndexOf(old); li >= 0) edit_.layout[(size_t)li].state = newName;
    for (auto& t : edit_.transitions) { // 引用传播（from/to）
        if (t.from == old) t.from = newName;
        if (t.to == old) t.to = newName;
    }
    renameBuf_ = newName;
    MarkDirty();
}

void AnimControllerPanel::DeleteState(int idx) {
    if (idx < 0 || idx >= (int)edit_.states.size()) return;
    PushUndo();
    const std::string gone = edit_.states[(size_t)idx];
    std::vector<assets::ControllerTransitionEdit> keep;
    for (auto& t : edit_.transitions)
        if (t.from != gone && t.to != gone) keep.push_back(std::move(t));
    edit_.transitions = std::move(keep);
    edit_.states.erase(edit_.states.begin() + (long)idx);
    if (edit_.entry == gone)
        edit_.entry = edit_.states.empty() ? std::string() : edit_.states[0];
    if (const int li = LayoutIndexOf(gone); li >= 0)
        edit_.layout.erase(edit_.layout.begin() + li);
    if (selState_ == idx) {
        SelectState(-1);
    } else if (selState_ > idx) {
        --selState_;
        renameBuf_ = edit_.states[(size_t)selState_];
    }
    selEdge_ = -1;
    MarkDirty();
}

int AnimControllerPanel::AddTransitionByIndex(int fromIdx, int toIdx) {
    if (!edit_.ok || fromIdx < 0 || fromIdx >= (int)edit_.states.size() || toIdx < 0 ||
        toIdx >= (int)edit_.states.size())
        return -1;
    PushUndo();
    assets::ControllerTransitionEdit t;
    t.from = edit_.states[(size_t)fromIdx];
    t.to = edit_.states[(size_t)toIdx];
    // 起步即合法（b9 规则）：有参数 → 默认一条首参条件；无参数 → exitTime
    if (!edit_.params.empty()) {
        const assets::ControllerParamEdit& p = edit_.params.front();
        t.conds.push_back({p.name, OpForKind(2, p.kind), 0.0f});
    } else {
        t.exitTime = true;
    }
    edit_.transitions.push_back(std::move(t));
    SelectState(fromIdx);
    SelectEdge((int)edit_.transitions.size() - 1); // 新边即选 → 检查器转边详情
    MarkDirty();
    return (int)edit_.transitions.size() - 1;
}

bool AnimControllerPanel::DeleteTransition(int idx) {
    if (idx < 0 || idx >= (int)edit_.transitions.size()) return false;
    PushUndo();
    edit_.transitions.erase(edit_.transitions.begin() + (long)idx);
    if (selEdge_ == idx)
        selEdge_ = -1;
    else if (selEdge_ > idx)
        --selEdge_;
    MarkDirty();
    return true;
}

bool AnimControllerPanel::UndoLocal() {
    if (!edit_.ok) return false;
    assets::ControllerData out;
    if (!undoStack_.Undo(edit_, out)) return false;
    edit_ = std::move(out);
    AfterHistoryRestore();
    return true;
}

bool AnimControllerPanel::RedoLocal() {
    if (!edit_.ok) return false;
    assets::ControllerData out;
    if (!undoStack_.Redo(edit_, out)) return false;
    edit_ = std::move(out);
    AfterHistoryRestore();
    return true;
}

void AnimControllerPanel::AfterHistoryRestore() {
    if (selState_ >= (int)edit_.states.size()) selState_ = -1;
    if (selEdge_ >= (int)edit_.transitions.size()) selEdge_ = -1;
    edgeDragFrom_ = -1;
    dragNode_ = -1;
    renameBuf_ =
        (selState_ >= 0 && selState_ < (int)edit_.states.size()) ? edit_.states[(size_t)selState_] : "";
    EnsureLayout(); // 快照可能早于某次自动布局（幂等；restore 本就 dirty）
    dirty_ = true;  // 撤销回到盘上态也不回退 dirty（简化——b9 dirty 语义 = 内存≠盘上）
    lintDirty_ = true;
}

std::string AnimControllerPanel::EdgeSummary(const assets::ControllerTransitionEdit& t) const {
    using lemon::editor::loc::tr;
    std::string s;
    if (t.exitTime) s = tr("animctrl.trans.exit_time");
    for (size_t i = 0; i < t.conds.size(); ++i) {
        if (i >= 2) {
            s += " +" + std::to_string(t.conds.size() - 2);
            break;
        }
        const assets::ControllerCondEdit& c = t.conds[i];
        if (!s.empty()) s += " & ";
        char buf[96];
        if (c.op == 5) // trigger：无值
            std::snprintf(buf, sizeof(buf), "%s", c.param.c_str());
        else if (c.op == 4) // bool ==
            std::snprintf(buf, sizeof(buf), "%s==%s", c.param.c_str(), c.value != 0.0f ? "1" : "0");
        else
            std::snprintf(buf, sizeof(buf), "%s%s%g", c.param.c_str(), OpLabel(c.op), c.value);
        s += buf;
    }
    return s;
}

void AnimControllerPanel::HandleKeys(bool ro) {
    if (ro || !keysFocused_) return;
    if (ImGui::GetIO().WantTextInput) return;
    if (dragNode_ >= 0 || edgeDragFrom_ >= 0) return; // 手势会话中不吃功能键
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Z)) {
        if (ImGui::GetIO().KeyShift)
            RedoLocal();
        else
            UndoLocal();
        return;
    }
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Y)) {
        RedoLocal();
        return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
        if (selEdge_ >= 0)
            DeleteTransition(selEdge_);
        else if (selState_ >= 0)
            DeleteState(selState_);
    }
}

// ---------------------------------------------------------------- 表单 ----

void AnimControllerPanel::StartCreate(const std::string& relDir) {
    // 创建入口统一清编辑态（AnimationPanel ResetEditingState 同款纪律：面板
    // 常驻 PanelRegistry，新建表单背后不得照渲染上次打开的 controller）
    targetGuid_ = 0;
    loadedGuid_ = 0;
    edit_ = assets::ControllerData{};
    warnings_.clear();
    selState_ = -1;
    selEdge_ = -1;
    edgeDragFrom_ = -1;
    renameBuf_.clear();
    undoStack_.Clear();
    createDir_ = relDir;
    while (createDir_.size() > 1 && createDir_.back() == '/') createDir_.pop_back();
    createOpen_ = true;
}

void AnimControllerPanel::DrawCreateForm(EditorApp& app, bool ro) {
    using lemon::editor::loc::tr;
    ImGui::TextUnformatted(tr("animctrl.create.title"));
    ImGui::TextDisabled("%s",
                        (createDir_.empty() ? std::string("Assets/Controllers") : createDir_)
                            .c_str());
    if (ro) {
        ImGui::TextColored(theme::kTextWarn, "%s", tr("animctrl.ro_banner"));
        return;
    }
    ImGui::InputText("##ctrlname", &createName_);
    ImGui::SameLine();
    if (ImGui::Button(tr("anim.create.button"))) TryCreate(app, createDir_, createName_, createErr_);
    if (!createErr_.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::kTextError);
        ImGui::PushTextWrapPos(0.0f); // 2026-10-12 教训：错误行必须折行，窄窗不裁
        ImGui::TextUnformatted(createErr_.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }
}

void AnimControllerPanel::DrawParamsTable(EditorApp& app) {
    using lemon::editor::loc::tr;
    (void)app;
    if (!ImGui::CollapsingHeader(tr("animctrl.params.header"))) return; // 默认折叠（批⑩b）
    ImGui::TextDisabled("%s", tr("animctrl.params.hint"));
    for (size_t i = 0; i < edit_.params.size(); ++i) {
        assets::ControllerParamEdit& p = edit_.params[i];
        ImGui::PushID((int)i);
        ImGui::InputText("##pname", &p.name);
        if (ImGui::IsItemActivated()) {
            PushUndo();                 // 编辑会话起拍快照（撤销 = 改名前）
            paramRenameOld_ = p.name;   // 改名传播源快照（InputText 就地改写前）
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            // 改名传播到条件引用（防悬空——old 在 InputText 就地改写前快照）
            for (auto& t : edit_.transitions)
                for (auto& c : t.conds)
                    if (c.param == paramRenameOld_) c.param = p.name;
            MarkDirty();
        }
        ImGui::SameLine();
        if (ImGui::BeginCombo("##pkind", KindLabel(p.kind))) {
            for (int ki : {0, 1, 2})
                if (ImGui::Selectable(KindLabel(ki), ki == p.kind) && ki != p.kind) {
                    PushUndo();
                    p.kind = ki;
                    // kind 收窄 → 同名参数的既有条件算子跟随（float→trigger 尤甚）
                    for (auto& t : edit_.transitions)
                        for (auto& c : t.conds)
                            if (c.param == p.name) c.op = OpForKind(c.op, ki);
                    MarkDirty();
                }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (p.kind == 0) {
            if (ImGui::DragFloat("##pdef", &p.def, 0.01f, 0.0f, 0.0f, "%.2f")) MarkDirty();
            if (ImGui::IsItemActivated()) PushUndo(); // 拖动按会话起拍（连续帧只压一次）
        } else {
            ImGui::TextDisabled("%s", tr("animctrl.params.def_none"));
        }
        ImGui::SameLine();
        if (ImGui::Button("×")) { // 删参数 → 引用它的条件一并清（防悬空）
            PushUndo();
            const std::string gone = p.name;
            for (auto& t : edit_.transitions) {
                std::vector<assets::ControllerCondEdit> keep;
                for (auto& c : t.conds)
                    if (c.param != gone) keep.push_back(c);
                t.conds = std::move(keep);
            }
            edit_.params.erase(edit_.params.begin() + (long)i);
            MarkDirty();
            ImGui::PopID();
            return; // 迭代器失效——本帧收手
        }
        ImGui::PopID();
    }
    // ≤8 拦（AnimParams 黑板上限；解析器超限拒绝，UI 从源头不造坏档）
    const bool full = edit_.params.size() >= 8;
    ImGui::BeginDisabled(full);
    if (ImGui::Button(tr("animctrl.params.add"))) {
        PushUndo();
        edit_.params.push_back(
            {"param" + std::to_string(edit_.params.size() + 1), 0, 0.0f});
        MarkDirty();
    }
    ImGui::EndDisabled();
    if (full && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("animctrl.params.full_tooltip"));
}

// ------------------------------------------------- 检查器（批⑩b）----
// 选择驱动（ADR-013 D6："图管结构 + 检查器管细节"）：选中边 → 条件详情 +
// 求值序；选中节点 → 名称/入口/出边汇总；未选中 → 参数表（默认折叠）。

void AnimControllerPanel::DrawNodeInspector(EditorApp& app) {
    using lemon::editor::loc::tr;
    (void)app;
    const std::string& name = edit_.states[(size_t)selState_];
    ImGui::TextUnformatted(tr("animctrl.node.header"));
    testhooks::Stash("animctrl.insp.node", ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##nodename", &renameBuf_); // 改名缓冲（SelectState 时同步）
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        CommitStateRename(selState_, renameBuf_);
        renameBuf_ = edit_.states[(size_t)selState_]; // 拒收（空/重名）也回落显示
    }
    const std::string entryName =
        edit_.entry.empty() ? (edit_.states.empty() ? std::string() : edit_.states[0])
                            : edit_.entry;
    const bool isEntry = name == entryName;
    ImGui::BeginDisabled(isEntry);
    if (ImGui::Button(tr("animctrl.node.entry_btn"))) {
        PushUndo();
        edit_.entry = name;
        MarkDirty();
    }
    ImGui::EndDisabled();
    if (isEntry) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", tr("animctrl.node.entry_is"));
    }

    // 出边汇总（行点击 → 边检查器；同 from 求值序在边详情面显示）
    ImGui::Spacing();
    ImGui::TextDisabled("%s", tr("animctrl.node.out_header"));
    for (size_t i = 0; i < edit_.transitions.size(); ++i) {
        const assets::ControllerTransitionEdit& t = edit_.transitions[i];
        if (t.from != name) continue;
        ImGui::PushID(("oe" + std::to_string(i)).c_str());
        const std::string label = "→ " + t.to + "  ·  " + EdgeSummary(t);
        if (ImGui::Selectable(label.c_str(), (int)i == selEdge_)) SelectEdge((int)i);
        ImGui::PopID();
    }
    ImGui::BeginDisabled(edit_.states.size() < 2);
    if (ImGui::Button(tr("animctrl.trans.add"))) { // 起步条件 b9 规则（同画布拖线）
        int to = 0;
        for (size_t j = 0; j < edit_.states.size(); ++j)
            if ((int)j != selState_) {
                to = (int)j;
                break;
            }
        AddTransitionByIndex(selState_, to);
    }
    ImGui::EndDisabled();
    if (edit_.states.size() < 2 && ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", tr("animctrl.trans.need_two"));
    ImGui::Spacing();
    if (ImGui::Button(tr("animctrl.node.delete_btn"))) {
        DeleteState(selState_);
        return; // name 引用已失效——本帧收手
    }
}

void AnimControllerPanel::DrawEdgeInspector(EditorApp& app) {
    using lemon::editor::loc::tr;
    using lemon::editor::loc::trFmt;
    (void)app;
    assets::ControllerTransitionEdit& t = edit_.transitions[(size_t)selEdge_];
    ImGui::Text("%s → %s", t.from.c_str(), t.to.c_str());
    testhooks::Stash("animctrl.insp.edge", ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    int order = 0, total = 0; // 同 from 文件序 = 运行时评估序（首条命中即切）
    for (size_t i = 0; i < edit_.transitions.size(); ++i)
        if (edit_.transitions[i].from == t.from) {
            ++total;
            if ((int)i == selEdge_) order = total;
        }
    ImGui::TextDisabled(
        "%s", trFmt("animctrl.edge.order_fmt", {std::to_string(order), std::to_string(total)}).c_str());
    ImGui::SetNextItemWidth(160);
    if (ImGui::BeginCombo(tr("animctrl.edge.to"), t.to.c_str())) {
        for (size_t j = 0; j < edit_.states.size(); ++j)
            if (ImGui::Selectable(edit_.states[j].c_str(), edit_.states[j] == t.to) &&
                edit_.states[j] != t.to) {
                PushUndo();
                t.to = edit_.states[j];
                MarkDirty();
            }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    {
        bool et = t.exitTime; // 本地承接（PushUndo 前快照 = 切换前态）
        if (ImGui::Checkbox(tr("animctrl.trans.exit_time"), &et)) {
            PushUndo();
            t.exitTime = et;
            MarkDirty();
        }
    }
    for (size_t j = 0; j < t.conds.size(); ++j)
        if (DrawCondRow(t, j)) return; // 行删——迭代器失效，本帧收手
    ImGui::Indent(24.0f);
    ImGui::BeginDisabled(edit_.params.empty());
    if (ImGui::SmallButton(tr("animctrl.trans.add_cond"))) {
        PushUndo();
        const assets::ControllerParamEdit& p = edit_.params.front();
        t.conds.push_back({p.name, OpForKind(2, p.kind), 0.0f});
        MarkDirty();
    }
    ImGui::EndDisabled();
    ImGui::Unindent(24.0f);
    if (t.conds.empty() && !t.exitTime) { // 解析器拒的形态——行内红字即时提示
        ImGui::TextColored(theme::kTextError, "%s", tr("animctrl.trans.empty_cond"));
    }
    ImGui::Spacing();
    if (ImGui::Button(tr("animctrl.edge.delete_btn"))) {
        DeleteTransition(selEdge_);
        return; // t 引用已失效——本帧收手
    }
}

bool AnimControllerPanel::DrawCondRow(assets::ControllerTransitionEdit& t, size_t j) {
    using lemon::editor::loc::tr;
    assets::ControllerCondEdit& c = t.conds[j];
    ImGui::PushID((int)j);
    ImGui::Indent(24.0f);
    int pi = -1;
    for (size_t k = 0; k < edit_.params.size(); ++k)
        if (edit_.params[k].name == c.param) pi = (int)k;
    if (pi < 0 && !edit_.params.empty()) {
        c.param = edit_.params[0].name; // 引用被删回落（保存门禁兜底）
        pi = 0;
        MarkDirty();
    }
    if (edit_.params.empty()) {
        ImGui::TextDisabled("%s", tr("animctrl.trans.no_params"));
    } else {
        if (ImGui::BeginCombo("##cparam", c.param.c_str())) {
            for (const auto& p : edit_.params)
                if (ImGui::Selectable(p.name.c_str(), p.name == c.param) && p.name != c.param) {
                    PushUndo();
                    c.param = p.name;
                    c.op = OpForKind(c.op, p.kind);
                    MarkDirty();
                }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        const int kind = edit_.params[(size_t)pi].kind;
        if (kind == 2) {
            ImGui::TextUnformatted("trigger");
        } else if (kind == 1) {
            if (ImGui::BeginCombo("##cop", "==")) {
                if (ImGui::Selectable("==") && c.op != 4) {
                    PushUndo();
                    c.op = 4;
                    MarkDirty();
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            bool b = c.value != 0.0f;
            if (ImGui::Checkbox("##cval", &b)) {
                PushUndo();
                c.value = b ? 1.0f : 0.0f;
                MarkDirty();
            }
        } else {
            if (ImGui::BeginCombo("##cop", OpLabel(c.op))) {
                for (int op = 0; op <= 4; ++op)
                    if (ImGui::Selectable(OpLabel(op), c.op == op) && op != c.op) {
                        PushUndo();
                        c.op = op;
                        MarkDirty();
                    }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            if (ImGui::DragFloat("##cval", &c.value, 0.01f, 0.0f, 0.0f, "%.2f")) MarkDirty();
            if (ImGui::IsItemActivated()) PushUndo(); // 拖动会话起拍（连续帧只压一次）
        }
    }
    ImGui::SameLine();
    bool deleted = false;
    if (ImGui::SmallButton("×")) {
        PushUndo();
        t.conds.erase(t.conds.begin() + (long)j);
        MarkDirty();
        deleted = true;
    }
    ImGui::Unindent(24.0f);
    ImGui::PopID();
    return deleted;
}

// ---------------------------------------------------------------- 画布 ----

void AnimControllerPanel::FitCanvas(float availW, float availH) {
    if (edit_.layout.empty()) {
        canvasZoom_ = 1.0f;
        canvasPanX_ = canvasPanY_ = 0.0f;
        return;
    }
    float minX = 1.0e9f, minY = 1.0e9f, maxX = -1.0e9f, maxY = -1.0e9f;
    for (const assets::ControllerNodePos& np : edit_.layout) {
        minX = std::min(minX, (float)np.x);
        minY = std::min(minY, (float)np.y);
        maxX = std::max(maxX, (float)np.x + kNodeW);
        maxY = std::max(maxY, (float)np.y + kNodeH);
    }
    const float pad = 40.0f;
    const float w = std::max(maxX - minX + pad * 2.0f, 1.0f);
    const float h = std::max(maxY - minY + pad * 2.0f, 1.0f);
    canvasZoom_ = std::clamp(std::min(availW / w, availH / h), 0.25f, 2.0f);
    canvasPanX_ = (availW - (minX + maxX) * canvasZoom_) * 0.5f;
    canvasPanY_ = (availH - (minY + maxY) * canvasZoom_) * 0.5f;
}

void AnimControllerPanel::DrawCanvas(EditorApp& app, bool ro) {
    using lemon::editor::loc::tr;
    EnsureLayout();
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    lastCanvasP0X_ = p0.x;
    lastCanvasP0Y_ = p0.y;
    ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.x < 64.0f) avail.x = 64.0f;
    if (avail.y < 64.0f) avail.y = 64.0f;
    ImGui::InvisibleButton("##ctrlcanvas", avail,
                           ImGuiButtonFlags_MouseButtonLeft |
                               ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();
    testhooks::Stash("animctrl.canvas", p0, ImVec2(p0.x + avail.x, p0.y + avail.y));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(p0, ImVec2(p0.x + avail.x, p0.y + avail.y), true);

    const ImGuiIO& io = ImGui::GetIO();
    if (canvasFitPending_) {
        FitCanvas(avail.x, avail.y);
        canvasFitPending_ = false;
    }
    // 相机：screen = p0 + graph * zoom + pan
    auto toScreen = [&](float gx, float gy) {
        return ImVec2(p0.x + gx * canvasZoom_ + canvasPanX_,
                      p0.y + gy * canvasZoom_ + canvasPanY_);
    };
    auto toGraph = [&](const ImVec2& sp) {
        return ImVec2((sp.x - p0.x - canvasPanX_) / canvasZoom_,
                      (sp.y - p0.y - canvasPanY_) / canvasZoom_);
    };
    auto nodeRectGraph = [&](int li) {
        const assets::ControllerNodePos& np = edit_.layout[(size_t)li];
        return ImVec4{(float)np.x, (float)np.y, (float)np.x + kNodeW, (float)np.y + kNodeH};
    };
    const ImVec2 gm = toGraph(io.MousePos);
    const float fs = std::max(13.0f * canvasZoom_, 8.0f);
    const std::string entryName =
        edit_.entry.empty() ? (edit_.states.empty() ? std::string() : edit_.states[0])
                            : edit_.entry;

    // ---- 命中工具（绘制与命中同源）----
    auto hitNodeLi = [&](const ImVec2& p) -> int {
        for (size_t li = 0; li < edit_.layout.size(); ++li) {
            const ImVec4 r = nodeRectGraph((int)li);
            if (p.x >= r.x && p.x <= r.z && p.y >= r.y && p.y <= r.w) return (int)li;
        }
        return -1;
    };
    auto handlePos = [&](int li) {
        const ImVec4 r = nodeRectGraph(li);
        return toScreen(r.z, (r.y + r.w) * 0.5f); // 节点右侧中点把手
    };
    struct EdgePath {
        bool ok = false, loop = false;
        ImVec2 a, b, c1, c2, mid, loopC;
        float loopR = 0.0f;
    };
    auto edgePath = [&](size_t ti) {
        EdgePath ep;
        const assets::ControllerTransitionEdit& t = edit_.transitions[ti];
        const int fi = LayoutIndexOf(t.from), tj = LayoutIndexOf(t.to);
        if (fi < 0 || tj < 0) return ep;
        const ImVec4 fr = nodeRectGraph(fi), trr = nodeRectGraph(tj);
        int dup = 0; // 同 from/to 多条边垂直偏移（绘制/命中同源）
        for (size_t j = 0; j < ti; ++j)
            if (edit_.transitions[j].from == t.from && edit_.transitions[j].to == t.to) ++dup;
        if (fi == tj) { // 自环（trigger 重启等合法形态）：右侧环线
            ep.ok = true;
            ep.loop = true;
            ep.loopC = toScreen(fr.z, (fr.y + fr.w) * 0.5f);
            ep.loopR = (24.0f + (float)dup * 16.0f) * canvasZoom_;
            ep.mid = ImVec2(ep.loopC.x + ep.loopR, ep.loopC.y);
            return ep;
        }
        ep.ok = true;
        ep.a = toScreen((fr.x + fr.z) * 0.5f, (fr.y + fr.w) * 0.5f);
        ep.b = toScreen((trr.x + trr.z) * 0.5f, (trr.y + trr.w) * 0.5f);
        const float off = 24.0f * canvasZoom_;
        const float offY = (float)dup * 16.0f * canvasZoom_;
        ep.c1 = ImVec2(ep.a.x + (ep.b.x >= ep.a.x ? off : -off), ep.a.y + offY);
        ep.c2 = ImVec2(ep.b.x - (ep.b.x >= ep.a.x ? off : -off), ep.b.y + offY);
        ep.mid = ImVec2((ep.a.x + 3.0f * ep.c1.x + 3.0f * ep.c2.x + ep.b.x) * 0.125f,
                        (ep.a.y + 3.0f * ep.c1.y + 3.0f * ep.c2.y + ep.b.y) * 0.125f);
        return ep;
    };
    auto hitEdge = [&](const ImVec2& mouse) -> int {
        int best = -1;
        float bestD = 6.0f; // 屏幕像素阈值
        for (size_t ti = 0; ti < edit_.transitions.size(); ++ti) {
            const EdgePath ep = edgePath(ti);
            if (!ep.ok) continue;
            if (ep.loop) {
                const float d = std::abs(
                    std::hypot(mouse.x - ep.loopC.x, mouse.y - ep.loopC.y) - ep.loopR);
                if (d < bestD) {
                    bestD = d;
                    best = (int)ti;
                }
                continue;
            }
            for (int k = 0; k < 24; ++k) { // 曲线采样命中（24 段折线）
                auto bez = [&](float tt) {
                    const float u = 1.0f - tt;
                    return ImVec2(
                        u * u * u * ep.a.x + 3 * u * u * tt * ep.c1.x +
                            3 * u * tt * tt * ep.c2.x + tt * tt * tt * ep.b.x,
                        u * u * u * ep.a.y + 3 * u * u * tt * ep.c1.y +
                            3 * u * tt * tt * ep.c2.y + tt * tt * tt * ep.b.y);
                };
                const ImVec2 q0 = bez((float)k / 24.0f), q1 = bez((float)(k + 1) / 24.0f);
                const float vx = q1.x - q0.x, vy = q1.y - q0.y;
                const float wx = mouse.x - q0.x, wy = mouse.y - q0.y;
                const float len2 = vx * vx + vy * vy;
                const float tt =
                    len2 > 1.0e-6f ? std::clamp((wx * vx + wy * vy) / len2, 0.0f, 1.0f) : 0.0f;
                const float dx = wx - vx * tt, dy = wy - vy * tt;
                const float d = std::sqrt(dx * dx + dy * dy);
                if (d < bestD) {
                    bestD = d;
                    best = (int)ti;
                }
            }
        }
        return best;
    };

    // ---- 相机操作：滚轮缩放（锚定光标）/ F 适配 / 中键或 空格+左键 平移 ----
    // review B4：相机键不吃文本焦点（检查器输入框打字时悬停画布的 f/空格不误触）
    const bool textFocus = io.WantTextInput;
    if (!textFocus && hovered && io.MouseWheel != 0.0f) {
        const ImVec2 anchor = toGraph(io.MousePos);
        canvasZoom_ =
            std::clamp(canvasZoom_ * std::pow(1.15f, io.MouseWheel), 0.25f, 2.0f);
        canvasPanX_ = io.MousePos.x - p0.x - anchor.x * canvasZoom_;
        canvasPanY_ = io.MousePos.y - p0.y - anchor.y * canvasZoom_;
    }
    if (!textFocus && hovered && ImGui::IsKeyPressed(ImGuiKey_F)) FitCanvas(avail.x, avail.y);
    const bool spaceDown = !textFocus && ImGui::IsKeyDown(ImGuiKey_Space);
    if (dragNode_ < 0 && ImGui::IsItemActive() &&
        (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) ||
         (spaceDown && ImGui::IsMouseDragging(ImGuiMouseButton_Left)))) {
        canvasPanX_ += io.MouseDelta.x;
        canvasPanY_ += io.MouseDelta.y;
    }

    // ---- 交互（编辑态；Play = 只读 + 当前状态高亮）----
    const int hoveredLi = hovered ? hitNodeLi(gm) : -1;
    if (!ro && edgeDragFrom_ < 0 && hovered && !spaceDown &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        // 拖线建边：节点右侧把手按下即起（优先于节点拖动）
        for (size_t li = 0; li < edit_.layout.size(); ++li) {
            const ImVec2 h = handlePos((int)li);
            const float dx = io.MousePos.x - h.x, dy = io.MousePos.y - h.y;
            if (dx * dx + dy * dy <= 64.0f) { // 8px 半径
                const int si = StateIndex(edit_.layout[li].state);
                if (si >= 0) {
                    edgeDragFrom_ = si;
                    edgeDragHeld_ = true;
                    edgeDragStartX_ = io.MousePos.x; // review B1：松开阈值判"拖 vs 点"
                    edgeDragStartY_ = io.MousePos.y;
                    SelectState(si);
                }
                break;
            }
        }
    }
    if (!ro && dragNode_ < 0 && edgeDragFrom_ < 0 && hovered && !spaceDown &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (hoveredLi >= 0) {
            const int si = StateIndex(edit_.layout[(size_t)hoveredLi].state);
            if (si >= 0) {
                dragNode_ = si;
                SelectState(si);
                dragStartGX_ = gm.x;
                dragStartGY_ = gm.y;
                dragOrigX_ = edit_.layout[(size_t)hoveredLi].x;
                dragOrigY_ = edit_.layout[(size_t)hoveredLi].y;
                preDrag_ = edit_; // 拖动撤销"松手拍一次"会话起拍
                dragMutated_ = false;
            }
        } else if (const int ei = hitEdge(io.MousePos); ei >= 0) {
            SelectEdge(ei); // 边点选（曲线采样命中 + 中点小圆点辅助）
        } else {
            SelectState(-1); // 空点 = 全清（检查器回参数表）
        }
    }
    if (dragNode_ >= 0 && dragNode_ < (int)edit_.states.size() &&
        ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        if (SetNodePosition(edit_.states[(size_t)dragNode_],
                            dragOrigX_ + (int)std::lround(gm.x - dragStartGX_),
                            dragOrigY_ + (int)std::lround(gm.y - dragStartGY_)))
            dragMutated_ = true;
    }
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        if (dragNode_ >= 0 && dragMutated_) undoStack_.Push(preDrag_); // 拖动 = 一步撤销
        dragNode_ = -1;
        dragMutated_ = false;
    }
    // 右键菜单 + 双击改名（编辑态）
    if (!ro && hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
        if (hoveredLi >= 0) {
            const int si = StateIndex(edit_.layout[(size_t)hoveredLi].state);
            if (si >= 0) {
                SelectState(si);
                ImGui::OpenPopup("ctrlctx_node");
            }
        } else {
            SelectState(-1);
            ImGui::OpenPopup("ctrlctx_canvas");
        }
    }
    if (!ro && hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && hoveredLi >= 0) {
        const int si = StateIndex(edit_.layout[(size_t)hoveredLi].state);
        if (si >= 0) {
            SelectState(si);
            renameBuf_ = edit_.states[(size_t)si];
            renameFocus_ = true;
            ImGui::OpenPopup("ctrlrename");
        }
    }
    if (!ro && ImGui::BeginPopup("ctrlctx_node")) {
        if (selState_ >= 0 && selState_ < (int)edit_.states.size()) {
            const std::string& nm = edit_.states[(size_t)selState_];
            const bool isEntry = nm == entryName;
            if (ImGui::MenuItem(tr("animctrl.ctx.set_entry"), nullptr, false, !isEntry)) {
                PushUndo();
                edit_.entry = nm;
                MarkDirty();
            }
            if (ImGui::MenuItem(tr("animctrl.ctx.new_edge"), nullptr, false,
                                edit_.states.size() >= 2)) {
                edgeDragFrom_ = selState_;
                edgeDragHeld_ = false; // 待点模式：下一左键落目标节点（空处取消）
            }
            ImGui::Separator();
            if (ImGui::MenuItem(tr("animctrl.ctx.delete_state"))) DeleteState(selState_);
        }
        ImGui::EndPopup();
    }
    if (!ro && ImGui::BeginPopup("ctrlctx_canvas")) {
        if (ImGui::MenuItem(tr("animctrl.ctx.new_state"))) {
            PushUndo();
            std::string nm = "State";
            for (int k = 2; StateIndex(nm) >= 0; ++k) nm = "State" + std::to_string(k);
            edit_.states.push_back(nm);
            EnsureLayout();
            SetNodePosition(nm, (int)std::lround(gm.x - kNodeW * 0.5f),
                            (int)std::lround(gm.y - kNodeH * 0.5f)); // 落点 = 右键处
            SelectState((int)edit_.states.size() - 1);
            MarkDirty();
        }
        ImGui::EndPopup();
    }
    if (!ro && ImGui::BeginPopup("ctrlrename")) {
        ImGui::TextUnformatted(tr("animctrl.rename.title"));
        if (renameFocus_) {
            ImGui::SetKeyboardFocusHere();
            renameFocus_ = false;
        }
        ImGui::SetNextItemWidth(180);
        if (ImGui::InputText("##rn", &renameBuf_, ImGuiInputTextFlags_EnterReturnsTrue)) {
            if (selState_ >= 0 && selState_ < (int)edit_.states.size())
                CommitStateRename(selState_, renameBuf_);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    // 拖线建边推进/收尾（按住拖 = 松开落点；待点模式 = 下一左键落点）
    if (edgeDragFrom_ >= 0) {
        const int hli = edgeDragFrom_ < (int)edit_.states.size()
                            ? LayoutIndexOf(edit_.states[(size_t)edgeDragFrom_])
                            : -1;
        if (hli < 0) {
            edgeDragFrom_ = -1;
            edgeDragHeld_ = false;
        } else {
            const ImVec2 h = handlePos(hli);
            const int tgtLi = hovered ? hitNodeLi(gm) : -1;
            const int tgtSi = tgtLi >= 0 ? StateIndex(edit_.layout[(size_t)tgtLi].state) : -1;
            dl->AddLine(h, io.MousePos, ImGui::GetColorU32(ImGuiCol_ButtonActive), 2.0f);
            dl->AddCircleFilled(io.MousePos, 4.0f, ImGui::GetColorU32(ImGuiCol_ButtonActive));
            if (edgeDragHeld_) {
                if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                    // review B1：单击未拖动（<6px 阈值，SceneView 拖拽同款）= 误触
                    // 右缘把手——取消会话不落边（未动时 tgt = 源节点自身 → 自环）
                    const float mdx = io.MousePos.x - edgeDragStartX_;
                    const float mdy = io.MousePos.y - edgeDragStartY_;
                    if (mdx * mdx + mdy * mdy >= 36.0f && tgtSi >= 0)
                        AddTransitionByIndex(edgeDragFrom_, tgtSi);
                    edgeDragFrom_ = -1;
                    edgeDragHeld_ = false;
                }
            } else if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                edgeDragFrom_ = -1;
            } else if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                if (tgtSi >= 0) AddTransitionByIndex(edgeDragFrom_, tgtSi);
                edgeDragFrom_ = -1;
            }
        }
    }

    // ---- 绘制：先边后节点（节点盖住线心）；不可达灰化 = 引擎可达性同源 ----
    const std::vector<uint8_t> reach = assets::ControllerReachability(edit_);
    const ImU32 edgeCol = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    const ImU32 edgeSelCol = ImGui::GetColorU32(ImGuiCol_ButtonActive);
    for (size_t ti = 0; ti < edit_.transitions.size(); ++ti) {
        const EdgePath ep = edgePath(ti);
        if (!ep.ok) continue;
        const bool selE = (int)ti == selEdge_;
        const ImU32 col = selE ? edgeSelCol : edgeCol;
        const float w = (selE ? 2.5f : 1.5f) * canvasZoom_;
        const std::string summary = EdgeSummary(edit_.transitions[ti]);
        if (ep.loop) {
            dl->AddCircle(ep.loopC, ep.loopR, col, 0, w);
            dl->AddCircleFilled(ep.mid, selE ? 4.0f : 3.0f, col);
            dl->AddText(ImGui::GetFont(), fs,
                        ImVec2(ep.loopC.x + ep.loopR + 6.0f, ep.loopC.y - fs * 0.5f), col,
                        summary.c_str());
            continue;
        }
        dl->AddBezierCubic(ep.a, ep.c1, ep.c2, ep.b, col, w);
        // 箭头：线从目标节点边框穿出处
        const float dx = ep.b.x - ep.a.x, dy = ep.b.y - ep.a.y;
        const float len = std::sqrt(dx * dx + dy * dy);
        if (len > 1.0e-3f) {
            const ImVec4 trr = nodeRectGraph(LayoutIndexOf(edit_.transitions[ti].to));
            const float hw = (trr.z - trr.x) * 0.5f * canvasZoom_;
            const float hh = (trr.w - trr.y) * 0.5f * canvasZoom_;
            const float s = std::min(hw / std::max(std::abs(dx), 1.0e-4f),
                                     hh / std::max(std::abs(dy), 1.0e-4f));
            const ImVec2 tip{ep.b.x - dx * s, ep.b.y - dy * s};
            const ImVec2 u{dx / len, dy / len};
            const ImVec2 nrm{-u.y, u.x};
            const float as = 9.0f * canvasZoom_;
            dl->AddTriangleFilled(
                tip,
                ImVec2(tip.x - u.x * as + nrm.x * as * 0.5f, tip.y - u.y * as + nrm.y * as * 0.5f),
                ImVec2(tip.x - u.x * as - nrm.x * as * 0.5f, tip.y - u.y * as - nrm.y * as * 0.5f),
                col);
        }
        dl->AddCircleFilled(ep.mid, selE ? 4.0f : 3.0f, col); // 中点小圆点（点选辅助）
        dl->AddText(ImGui::GetFont(), fs, ImVec2(ep.mid.x + 6.0f, ep.mid.y - fs - 2.0f), col,
                    summary.c_str());
    }
    // Play 态当前状态高亮（数据面：AnimGraph.controllerGuid + Animator2D.clipId
    // → 集绑定反推段名 = 状态名，ADR-013 D4 约定）
    std::string playState;
    if (ro) {
        ecs::Scene& ps = app.Ctx().ActiveScene();
        const ecs::ClipTable& clips = app.Ctx().ActiveWorld().Clips();
        for (auto [ent, g, an] : ps.View<ecs::AnimGraph, ecs::Animator2D>().each()) {
            (void)ent;
            if (g.controllerGuid != targetGuid_) continue;
            if (const std::string* n = clips.NameOfClip((uint32_t)g.setGuid, an.clipId))
                playState = *n;
            break;
        }
    }
    for (size_t li = 0; li < edit_.layout.size(); ++li) {
        const int si = StateIndex(edit_.layout[li].state);
        if (si < 0) continue;
        const ImVec4 r = nodeRectGraph((int)li);
        const ImVec2 a = toScreen(r.x, r.y), b = toScreen(r.z, r.w);
        const bool sel = si == selState_;
        const bool reachable = (size_t)si < reach.size() && reach[(size_t)si];
        dl->AddRectFilled(a, b, ImGui::GetColorU32(ImGuiCol_FrameBg, sel ? 1.0f : 0.7f),
                          kNodeRad * canvasZoom_);
        dl->AddRect(a, b,
                    sel ? edgeSelCol : ImGui::GetColorU32(ImGuiCol_Border), kNodeRad * canvasZoom_,
                    0, (sel ? 2.0f : 1.0f) * canvasZoom_);
        dl->AddText(ImGui::GetFont(), fs,
                    ImVec2(a.x + 10.0f * canvasZoom_, (a.y + b.y) * 0.5f - fs * 0.5f),
                    reachable ? ImGui::GetColorU32(ImGuiCol_Text)
                              : ImGui::GetColorU32(ImGuiCol_TextDisabled),
                    edit_.layout[li].state.c_str());
        if (edit_.layout[li].state == entryName)
            dl->AddText(ImGui::GetFont(), fs * 0.8f,
                        ImVec2(b.x - 40.0f * canvasZoom_, a.y + 5.0f * canvasZoom_),
                        ImGui::GetColorU32(ImGuiCol_TextDisabled), tr("animctrl.states.entry"));
        if (!playState.empty() && edit_.layout[li].state == playState) // Play 当前态高亮
            dl->AddRect(ImVec2(a.x - 2.0f, a.y - 2.0f), ImVec2(b.x + 2.0f, b.y + 2.0f),
                        ImGui::GetColorU32(theme::kTextWarn), kNodeRad * canvasZoom_ + 2.0f, 0,
                        3.0f * canvasZoom_);
        if (!ro && (int)li == hoveredLi) // 拖线把手（hover 显现）
            dl->AddCircleFilled(handlePos((int)li), 5.0f, edgeSelCol);
    }
    if (edgeDragFrom_ >= 0) // 建边会话提示（按住拖 = 松开落点；待点 = 点目标/空处取消）
        dl->AddText(ImVec2(p0.x + 8.0f, p0.y + 26.0f), ImGui::GetColorU32(ImGuiCol_Text),
                    tr("animctrl.canvas.edge_drag_hint"));
    dl->AddText(ImVec2(p0.x + 8.0f, p0.y + avail.y - 20.0f),
                ImGui::GetColorU32(ImGuiCol_TextDisabled),
                tr(ro ? "animctrl.canvas.hint_play" : "animctrl.canvas.hint")); // review B15
    dl->PopClipRect();
}

// ---------------------------------------------------------------- OnGui ----

void AnimControllerPanel::OnGui(EditorApp& app) {
    using lemon::editor::loc::tr;
    using lemon::editor::loc::trFmt;
    bool winOpen = true;
    keysFocused_ = false; // 先清（Begin 失败/窗口不绘制时不吃键——防陈旧 true 卡全局）
    const std::string title = std::string(tr("panel.animcontroller")) + "###" + Name();
    if (!ImGui::Begin(title.c_str(), &winOpen, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        // 冒烟探针（animpanel.skip 同款）：Begin 失败 = 窗口被停靠标签页遮住/
        // 折叠——OnGui 仍被拉起（渲染链活着），探针两键都认
        testhooks::Stash("animctrl.skip", ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        if (!winOpen) app.ClosePanel(Name());
        return;
    }
    // 冒烟探针（AnimationPanel animpanel.begin 同款）：Begin 成功 = 全量绘制路径
    testhooks::Stash("animctrl.begin", ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    if (!winOpen) app.ClosePanel(Name());
    keysFocused_ = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows); // 键捕获仲裁（批⑩b）

    EditorContext& ctx = app.Ctx();
    AssetDatabase& db = ctx.Assets();
    const bool ro = ctx.Playing(); // 快照语义：进行中的局不吃改档 → 面板只读

    const AssetEntry* target = db.FindByGuid(targetGuid_);
    if (target && (target->missing || target->type != AssetType::Controller)) target = nullptr;
    if (targetGuid_ != 0 && !target) {
        targetGuid_ = 0; // 资产被删 → 回空态
        loadedGuid_ = 0;
    }

    // ---- 空态：新建表单（浏览器右键/面板按钮进入）+ 打开提示 ----
    if (!target) {
        if (createOpen_) {
            DrawCreateForm(app, ro);
        } else {
            ImGui::TextDisabled("%s", tr("animctrl.hint_open_target"));
            if (!ro && ImGui::Button(tr("animctrl.create.open_btn"))) createOpen_ = true;
        }
        ImGui::End();
        return;
    }
    if (loadedGuid_ != target->guid || loadedHash_ != target->hash) LoadFrom(db, *target);

    if (!edit_.ok) {
        ImGui::TextColored(theme::kTextError, "%s",
                           trFmt("animctrl.parse_failed_fmt", {edit_.error}).c_str());
        ImGui::TextDisabled("%s", tr("anim.fix_json_hint"));
        ImGui::End();
        return;
    }
    if (lintDirty_) {
        RecomputeLint(app);
        lintDirty_ = false;
    }

    // 面板本地编辑键（批⑩b）：Ctrl+Z/Ctrl+Shift+Z/Ctrl+Y/Del——只读态与输入中
    // 自守卫；EditorApp 侧经 CapturesUndoKeys 让位（BuildShortcuts）
    HandleKeys(ro);

    if (ro) {
        ImGui::TextColored(theme::kTextWarn, "%s", tr("animctrl.ro_banner"));
        ImGui::BeginDisabled();
    }

    // ---- 顶栏：名字 / entry / 保存 ----
    ImGui::SetNextItemWidth(180);
    ImGui::InputText(tr("anim.name_label"), &edit_.name);
    if (ImGui::IsItemActivated()) PushUndo(); // 编辑会话起拍快照
    if (ImGui::IsItemDeactivatedAfterEdit()) MarkDirty();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(140);
    {
        const std::string cur = edit_.entry.empty()
                                    ? (edit_.states.empty() ? std::string() : edit_.states[0])
                                    : edit_.entry;
        if (ImGui::BeginCombo(tr("animctrl.entry"), cur.c_str())) {
            for (const std::string& s : edit_.states)
                if (ImGui::Selectable(s.c_str(), s == cur) && s != cur) {
                    PushUndo();
                    edit_.entry = s;
                    MarkDirty();
                }
            ImGui::EndCombo();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("common.save"))) {
        if (const AssetEntry* e2 = db.FindByGuid(targetGuid_)) TrySave(app, *e2);
        // TrySave 内含 Rescan（entries_ 重建）——此后一律 guid 重查
    }
    if (dirty_) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", tr("anim.status.unsaved"));
    }
    if (!saveMsg_.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, saveOk_ ? theme::kTextOk : theme::kTextError);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(saveMsg_.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }

    // ---- 编辑期告警（本批灵魂）：结构 lint 每帧直算 + 跨资产黄字缓存 ----
    for (const std::string& w : assets::ControllerStructuralWarnings(edit_)) {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::kTextWarn);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(w.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }
    for (const std::string& w : warnings_) {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::kTextWarn);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(w.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }
    // review B2：只读禁用域到此收束（顶栏/表单），画布是观察行为不是编辑行为——
    // 留在 disabled 域会让 ImGui 拒绝 hovered/ActiveId → Play 态滚轮/F/平移全灭
    //（Play 当前态高亮看得见但视野锁死）。编辑类操作各自有 !ro 门禁收账。
    if (ro) ImGui::EndDisabled();

    // ---- 画布（左）+ 检查器（右）——窄窗上下堆叠。检查器选择驱动
    // （ADR-013 D6：图管结构 + 检查器管细节）：边选中 → 条件详情 + 求值序；
    // 节点选中 → 名称/入口/出边汇总；未选中 → 参数表（默认折叠）+ 操作提示 ----
    const float availW = ImGui::GetContentRegionAvail().x;
    const float availH = ImGui::GetContentRegionAvail().y;
    auto drawInspector = [&]() {
        if (selEdge_ >= 0 && selEdge_ < (int)edit_.transitions.size()) {
            DrawEdgeInspector(app);
        } else if (selState_ >= 0 && selState_ < (int)edit_.states.size()) {
            DrawNodeInspector(app);
        } else {
            DrawParamsTable(app);
            ImGui::Separator();
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextDisabled("%s", tr("animctrl.insp.hint"));
            ImGui::PopTextWrapPos();
        }
    };
    if (availW >= 640.0f) {
        ImGui::BeginChild("##ctrlcanvas_col", ImVec2(availW * 0.58f, 0), ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoScrollbar);
        DrawCanvas(app, ro);
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("##ctrlinspector");
        if (ro) ImGui::BeginDisabled(); // 检查器 = 编辑面，随只读禁用
        drawInspector();
        if (ro) ImGui::EndDisabled();
        ImGui::EndChild();
    } else {
        ImGui::BeginChild("##ctrlcanvas_col", ImVec2(0, availH * 0.55f), ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoScrollbar);
        DrawCanvas(app, ro);
        ImGui::EndChild();
        ImGui::BeginChild("##ctrlinspector");
        if (ro) ImGui::BeginDisabled();
        drawInspector();
        if (ro) ImGui::EndDisabled();
        ImGui::EndChild();
    }

    ImGui::End();
}

} // namespace lemon::editor
