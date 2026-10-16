// Lemon 编辑器 — AnimController 面板（M8 批⑨：.controller 状态机工作台）
// ADR-013 登记的 v1 编辑面补全——"JSON 手写 + Inspector 编辑"的后者从未建过，
// 本批补齐；可视化图（节点/连线）维持"游戏阻塞触发"挂起不动。
// 布局 = 三张表：左列（参数黑板 ≤8 + 状态词表）+ 右区（选中状态出边表 +
// 入边只读行）。编辑期校验是本批的灵魂：
//   * 结构 lint（不可达状态）——引擎 ControllerStructuralWarnings 同源，每帧直算；
//   * 跨资产黄字（exitTime×Loop 脚枪）——exitTime 出边状态在项目 .ani 集中绑定
//     的 clip 若非 Once 模式，段末边沿永不触发（Systems.cpp exitTime 只认非 loop
//     收尾边沿的登记边角）。运行时语义不动，编辑面把作者错误拦在写盘前；
//   * 保存门禁 = ParseControllerJson 同一判据（悬空引用/空条件/超 8 参数/
//     重名）+ roundtrip 预验（AnimationPanel TrySave 同款数据丢失防线）。
// 快照语义：Play 态只读横幅；保存后下次 Enter Play 生效。
#define IMGUI_DEFINE_MATH_OPERATORS
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <unordered_map>

#include "App/EditorApp.h"
#include "Assets/AssetDatabase.h"
#include "Core/Log.h"
#include "EditorContext.h"
#include "Localization/Localization.h"
#include "Panels/BuiltInPanels.h"
#include "Tooling/TestHooks.h"
#include "Tooling/Theme.h"
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"

namespace lemon::editor {
namespace {
constexpr float kLeftWidth = 250.0f; // 左列宽（参数 + 状态词表）

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
    renameIdx_ = -1;
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
    warnings_.clear();
    lintDirty_ = true;
}

int AnimControllerPanel::StateIndex(const std::string& name) const {
    for (size_t i = 0; i < edit_.states.size(); ++i)
        if (edit_.states[i] == name) return (int)i;
    return -1;
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
        back.params != edit_.params || back.transitions != edit_.transitions) {
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
    edit_.states.push_back(name);
    selState_ = (int)edit_.states.size() - 1;
    MarkDirty();
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
    if (!ImGui::CollapsingHeader(tr("animctrl.params.header"), ImGuiTreeNodeFlags_DefaultOpen))
        return;
    ImGui::TextDisabled("%s", tr("animctrl.params.hint"));
    for (size_t i = 0; i < edit_.params.size(); ++i) {
        assets::ControllerParamEdit& p = edit_.params[i];
        ImGui::PushID((int)i);
        ImGui::InputText("##pname", &p.name);
        if (ImGui::IsItemActivated()) paramRenameOld_ = p.name; // 编辑会话起拍快照
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
                if (ImGui::Selectable(KindLabel(ki), ki == p.kind)) {
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
        } else {
            ImGui::TextDisabled("%s", tr("animctrl.params.def_none"));
        }
        ImGui::SameLine();
        if (ImGui::Button("×")) { // 删参数 → 引用它的条件一并清（防悬空）
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
        edit_.params.push_back(
            {"param" + std::to_string(edit_.params.size() + 1), 0, 0.0f});
        MarkDirty();
    }
    ImGui::EndDisabled();
    if (full && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("animctrl.params.full_tooltip"));
}

void AnimControllerPanel::DrawStatesList(EditorApp& app) {
    using lemon::editor::loc::tr;
    (void)app;
    if (!ImGui::CollapsingHeader(tr("animctrl.states.header"), ImGuiTreeNodeFlags_DefaultOpen))
        return;
    const std::string entry = edit_.entry.empty()
                                  ? (edit_.states.empty() ? std::string() : edit_.states[0])
                                  : edit_.entry;
    for (size_t i = 0; i < edit_.states.size(); ++i) {
        ImGui::PushID((int)i);
        const bool selected = (int)i == selState_;
        if (ImGui::Selectable("##stsel", selected)) selState_ = (int)i;
        ImGui::SameLine();
        if ((int)i == renameIdx_) {
            // inline 改名（段清单同款交互：双击进入、Enter/失焦提交；空名/重名
            // 静默保旧名——CommitSegRename 同款不扰邻语义）
            if (renameFocus_) {
                ImGui::SetKeyboardFocusHere();
                renameFocus_ = false;
            }
            ImGui::SetNextItemWidth(-30);
            const bool commit = ImGui::InputText("##strename", &renameBuf_,
                                                 ImGuiInputTextFlags_EnterReturnsTrue);
            if (commit || ImGui::IsItemDeactivated()) {
                if (!renameBuf_.empty() &&
                    (renameBuf_ == edit_.states[i] || StateIndex(renameBuf_) < 0)) {
                    const std::string old = edit_.states[i];
                    edit_.states[i] = renameBuf_;
                    if (edit_.entry == old) edit_.entry = renameBuf_;
                    for (auto& t : edit_.transitions) { // 引用传播（from/to）
                        if (t.from == old) t.from = renameBuf_;
                        if (t.to == old) t.to = renameBuf_;
                    }
                    MarkDirty();
                }
                renameIdx_ = -1;
            }
        } else {
            ImGui::TextUnformatted(edit_.states[i].c_str());
            if (edit_.states[i] == entry) {
                ImGui::SameLine();
                ImGui::TextDisabled("«%s", tr("animctrl.states.entry"));
            }
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                renameIdx_ = (int)i;
                renameBuf_ = edit_.states[i];
                renameFocus_ = true;
            }
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("×")) { // 删状态 → 相触及的过渡一并清 + entry 回退
            const std::string gone = edit_.states[i];
            std::vector<assets::ControllerTransitionEdit> keep;
            for (auto& t : edit_.transitions)
                if (t.from != gone && t.to != gone) keep.push_back(t);
            edit_.transitions = std::move(keep);
            edit_.states.erase(edit_.states.begin() + (long)i);
            if (edit_.entry == gone)
                edit_.entry = edit_.states.empty() ? std::string() : edit_.states[0];
            if (selState_ == (int)i) selState_ = -1;
            else if (selState_ > (int)i) --selState_;
            MarkDirty();
            ImGui::PopID();
            return; // 迭代器失效——本帧收手
        }
        ImGui::PopID();
    }
    // inline 新建（Enter/「+」；重名/空名不动——输入保持可改）
    ImGui::SetNextItemWidth(-30);
    if (ImGui::InputTextWithHint("##stnew", tr("animctrl.states.new_hint"), &stNewName_,
                                 ImGuiInputTextFlags_EnterReturnsTrue)) {
        if (!stNewName_.empty() && StateIndex(stNewName_) < 0) {
            AddState(stNewName_);
            stNewName_.clear();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("+") && !stNewName_.empty() && StateIndex(stNewName_) < 0) {
        AddState(stNewName_);
        stNewName_.clear();
    }
}

void AnimControllerPanel::DrawTransitions(EditorApp& app) {
    using lemon::editor::loc::tr;
    (void)app;
    if (selState_ < 0 || selState_ >= (int)edit_.states.size()) {
        ImGui::TextDisabled("%s", tr("animctrl.trans.no_state"));
        return;
    }
    const std::string& from = edit_.states[selState_];
    ImGui::Text("%s: %s", tr("animctrl.trans.out_header"), from.c_str());

    // ---- 出边表（from 固定 = 选中状态；同 from 按文件序首条命中即切 = 运行时
    // 评估序，表序即生效序——增删按显示序落盘）----
    for (size_t i = 0; i < edit_.transitions.size(); ++i) {
        assets::ControllerTransitionEdit& t = edit_.transitions[i];
        if (t.from != from) continue;
        ImGui::PushID(("out" + std::to_string(i)).c_str());
        ImGui::Bullet();
        ImGui::SameLine();
        int to = StateIndex(t.to);
        if (to < 0) to = selState_;
        if (ImGui::BeginCombo("##to", edit_.states[to].c_str())) {
            // 全部状态（含自身——trigger 重启是合法形态）
            for (size_t j = 0; j < edit_.states.size(); ++j)
                if (ImGui::Selectable(edit_.states[j].c_str(), (int)j == to)) {
                    t.to = edit_.states[j];
                    MarkDirty();
                }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::Checkbox(tr("animctrl.trans.exit_time"), &t.exitTime)) MarkDirty();
        ImGui::SameLine();
        if (ImGui::SmallButton("×")) {
            edit_.transitions.erase(edit_.transitions.begin() + (long)i);
            MarkDirty();
            ImGui::PopID();
            return; // 迭代器失效——本帧收手
        }
        // 条件行（AND 语义；trigger 参数固定 op、无值；bool 参数 == 0/1）
        for (size_t j = 0; j < t.conds.size(); ++j) {
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
                        if (ImGui::Selectable(p.name.c_str(), p.name == c.param)) {
                            c.param = p.name;
                            c.op = OpForKind(c.op, p.kind);
                            MarkDirty();
                        }
                    ImGui::EndCombo();
                }
                ImGui::SameLine();
                const int kind = edit_.params[pi].kind;
                if (kind == 2) {
                    ImGui::TextUnformatted("trigger");
                } else if (kind == 1) {
                    if (ImGui::BeginCombo("##cop", "==")) {
                        if (ImGui::Selectable("==")) {
                            c.op = 4;
                            MarkDirty();
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::SameLine();
                    bool b = c.value != 0.0f;
                    if (ImGui::Checkbox("##cval", &b)) {
                        c.value = b ? 1.0f : 0.0f;
                        MarkDirty();
                    }
                } else {
                    if (ImGui::BeginCombo("##cop", OpLabel(c.op))) {
                        for (int op = 0; op <= 4; ++op)
                            if (ImGui::Selectable(OpLabel(op), c.op == op)) {
                                c.op = op;
                                MarkDirty();
                            }
                        ImGui::EndCombo();
                    }
                    ImGui::SameLine();
                    if (ImGui::DragFloat("##cval", &c.value, 0.01f, 0.0f, 0.0f, "%.2f"))
                        MarkDirty();
                }
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("×")) {
                t.conds.erase(t.conds.begin() + (long)j);
                MarkDirty();
                ImGui::Unindent(24.0f);
                ImGui::PopID();
                ImGui::PopID();
                return; // 迭代器失效——本帧收手
            }
            ImGui::Unindent(24.0f);
            ImGui::PopID();
        }
        ImGui::Indent(24.0f);
        ImGui::BeginDisabled(edit_.params.empty());
        if (ImGui::SmallButton(tr("animctrl.trans.add_cond"))) {
            const assets::ControllerParamEdit& p = edit_.params.front();
            t.conds.push_back({p.name, OpForKind(2, p.kind), 0.0f});
            MarkDirty();
        }
        ImGui::EndDisabled();
        ImGui::Unindent(24.0f);
        // 无条件且非 exitTime = 解析器拒的形态——行内红字即时提示
        if (t.conds.empty() && !t.exitTime) {
            ImGui::Indent(24.0f);
            ImGui::TextColored(theme::kTextError, "%s", tr("animctrl.trans.empty_cond"));
            ImGui::Unindent(24.0f);
        }
        ImGui::PopID();
    }
    ImGui::BeginDisabled(edit_.states.size() < 2);
    if (ImGui::Button(tr("animctrl.trans.add"))) {
        // 新出边起步即合法：有参数 → 默认一条首参条件；无参数 → exitTime
        assets::ControllerTransitionEdit t;
        t.from = from;
        for (size_t j = 0; j < edit_.states.size(); ++j)
            if ((int)j != selState_) {
                t.to = edit_.states[j];
                break;
            }
        if (!edit_.params.empty()) {
            const assets::ControllerParamEdit& p = edit_.params.front();
            t.conds.push_back({p.name, OpForKind(2, p.kind), 0.0f});
        } else {
            t.exitTime = true;
        }
        edit_.transitions.push_back(std::move(t));
        MarkDirty();
    }
    ImGui::EndDisabled();
    if (edit_.states.size() < 2 && ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", tr("animctrl.trans.need_two"));

    // ---- 入边只读行（编辑面在各自源状态；这里只做方位感）----
    bool any = false;
    for (const auto& t : edit_.transitions) {
        if (t.to != from) continue;
        if (!any) {
            ImGui::Spacing();
            ImGui::TextDisabled("%s", tr("animctrl.trans.in_header"));
            any = true;
        }
        ImGui::BulletText("%s → %s", t.from.c_str(),
                          t.exitTime ? tr("animctrl.trans.in_exit") : "");
    }
}

// ---------------------------------------------------------------- OnGui ----

void AnimControllerPanel::OnGui(EditorApp& app) {
    using lemon::editor::loc::tr;
    using lemon::editor::loc::trFmt;
    bool winOpen = true;
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

    if (ro) {
        ImGui::TextColored(theme::kTextWarn, "%s", tr("animctrl.ro_banner"));
        ImGui::BeginDisabled();
    }

    // ---- 顶栏：名字 / entry / 保存 ----
    ImGui::SetNextItemWidth(180);
    ImGui::InputText(tr("anim.name_label"), &edit_.name);
    if (ImGui::IsItemDeactivatedAfterEdit()) MarkDirty();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(140);
    {
        const std::string cur = edit_.entry.empty()
                                    ? (edit_.states.empty() ? std::string() : edit_.states[0])
                                    : edit_.entry;
        if (ImGui::BeginCombo(tr("animctrl.entry"), cur.c_str())) {
            for (const std::string& s : edit_.states)
                if (ImGui::Selectable(s.c_str(), s == cur)) {
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

    // ---- 双列：左（参数 + 状态）| 右（出边）——窄窗上下堆叠 ----
    const float availW = ImGui::GetContentRegionAvail().x;
    if (availW >= 520.0f) {
        ImGui::BeginChild("##left", ImVec2(std::min(kLeftWidth, availW * 0.34f), 0),
                          ImGuiChildFlags_None);
        DrawParamsTable(app);
        DrawStatesList(app);
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("##right", ImVec2(0, 0));
        DrawTransitions(app);
        ImGui::EndChild();
    } else {
        ImGui::BeginChild("##left", ImVec2(0, ImGui::GetContentRegionAvail().y * 0.42f));
        DrawParamsTable(app);
        DrawStatesList(app);
        ImGui::EndChild();
        ImGui::BeginChild("##right", ImVec2(0, 0));
        DrawTransitions(app);
        ImGui::EndChild();
    }

    if (ro) ImGui::EndDisabled();
    ImGui::End();
}

} // namespace lemon::editor
