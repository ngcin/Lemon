// Lemon 编辑器 — Hierarchy 面板（M4.md §2.2）
// 数据源：Scene 实体遍历 + Hierarchy 父子链（ECS/Hierarchy.h 维护）。
// 验收点：拖拽成环被拒（SetParent 引擎侧拒绝 + Console 告警）；Play 中切数据源 M4.3。
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "App/EditorApp.h"
#include "Components/CoreComponents.h"
#include "Components/UiComponents.h"
#include "Core/Log.h"
#include "ECS/Hierarchy.h"
#include "ECS/SceneMembership.h" // 批⑩：Play 态场景组分组依据（kSceneFlagDontDestroyOnLoad/句柄）
#include "Components/RenderComponents.h"
#include "Interaction/ViewportRenderer.h"
#include "Scripting/ScriptBox.h"
#include "Tooling/TestHooks.h"
#include "Tooling/Theme.h"
#include "EditorContext.h"
#include "Localization/Localization.h"
#include "Panels/BuiltInPanels.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "misc/cpp/imgui_stdlib.h"

namespace lemon::editor {
namespace {

// 性能批②探针：LEMON_BENCH_UI_PROBE=1 时累计本面板整帧耗时（静态一次性读 env）
UiPanelProbe g_hierProbe;
const bool g_hierProbeOn = std::getenv("LEMON_BENCH_UI_PROBE") != nullptr;

const char* DisplayName(ecs::Scene& s, ecs::Entity e) {
    static char buf[40];
    const ecs::Meta* m = s.TryGet<ecs::Meta>(e);
    if (m && m->tag[0]) {
        std::snprintf(buf, sizeof(buf), "%s", m->tag);
        return buf;
    }
    std::snprintf(buf, sizeof(buf), "Entity_%llu", (unsigned long long)(e.id & 0xFFFFFFFFull));
    return buf;
}

// C3 手测：搜索忽略大小写（ASCII tolower 后子串匹配；非 ASCII 字节段原样比较，
// 中文等不受影响）。AssetBrowser 过滤器同款语义。
const char* StrIStr(const char* hay, const char* needle) {
    if (!needle[0]) return hay;
    for (const char* h = hay; *h; ++h) {
        const char* a = h;
        const char* b = needle;
        while (*a && *b &&
               std::tolower((unsigned char)*a) == std::tolower((unsigned char)*b)) {
            ++a;
            ++b;
        }
        if (!*b) return h;
    }
    return nullptr;
}

} // namespace

UiPanelProbe HierarchyPanelProbe() { return g_hierProbe; }

void HierarchyPanel::StartRename(ecs::Scene& s, ecs::Entity e) {
    renaming_ = e;
    renameFocus_ = true;
    if (const ecs::Meta* m = s.TryGet<ecs::Meta>(e)) renameBuf_ = m->tag;
    else renameBuf_.clear();
}

void HierarchyPanel::CommitRename(EditorApp& app, ecs::Entity e, bool apply) {
    EditorContext& ctx = app.Ctx();
    if (apply && !renameBuf_.empty()) {
        ecs::Scene& s = ctx.ActiveScene();
        if (ecs::Meta* m = s.TryGet<ecs::Meta>(e)) {
            // tag[24] 截断保护 + 变更才落（避免空操作进 Undo/置脏）
            char next[24] = {};
            std::snprintf(next, sizeof(next), "%s", renameBuf_.c_str());
            if (std::strcmp(next, m->tag) != 0) {
                const ecs::ComponentMeta* cm = ecs::ComponentRegistry::Instance().Find("Meta");
                std::vector<uint8_t> before =
                    cm ? ctx.SnapshotComponent(e, cm->id) : std::vector<uint8_t>{};
                std::memcpy(m->tag, next, sizeof(next));
                ctx.dirty = true;
                if (!ctx.Playing() && cm)
                    ctx.PushPropertyUndo("重命名实体", m->guid, cm->id, std::move(before),
                                         ctx.SnapshotComponent(e, cm->id));
            }
        }
    }
    renaming_ = ecs::Entity::Null();
}

void HierarchyPanel::OnGui(EditorApp& app) {
    using lemon::editor::loc::tr;
    const auto probeT0 = std::chrono::steady_clock::now();
    const auto probeAccum = [&]() {
        if (g_hierProbeOn) {
            g_hierProbe.totalMs += std::chrono::duration<double, std::milli>(
                                       std::chrono::steady_clock::now() - probeT0)
                                       .count();
            ++g_hierProbe.frames;
        }
    };
    // 窗口标题本地化；### 后段（=Name()）保窗口身份/停靠/ini 不随语言变
    const std::string title = std::string(tr("panel.hierarchy")) + "###" + Name();
    bool winOpen = true;
    if (!ImGui::Begin(title.c_str(), &winOpen,
                      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar)) {
        ImGui::End();
        if (!winOpen) app.ClosePanel(Name()); // × 关闭（T3b-8）
        probeAccum(); // 折叠帧也计（折叠时几乎为零——本身即归因信号）
        return;
    }
    if (!winOpen) app.ClosePanel(Name());
    EditorContext& ctx = app.Ctx();
    ecs::Scene& scene = ctx.ActiveScene();

    // 重命名态自愈：实体没了（删除/Undo/切场景）→ 静默退出编辑
    if (!renaming_.IsNull() && !scene.Alive(renaming_)) renaming_ = ecs::Entity::Null();
    // F2：主选中进入重命名（§4-7；输入框聚焦时 EditorApp 快捷层已屏蔽）
    if (renaming_.IsNull() && ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) &&
        ImGui::IsKeyPressed(ImGuiKey_F2, false)) {
        if (!ctx.Primary().IsNull()) StartRename(scene, ctx.Primary());
    }

    // 工具行：搜索 + 创建
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x - 118);
    ImGui::InputTextWithHint("##search", tr("hier.search_hint"), &filter_);
    ImGui::PopItemWidth();
    ImGui::SameLine();
    if (ImGui::Button(tr("hier.create"))) ImGui::OpenPopup("create_entity");
    if (ImGui::BeginPopup("create_entity")) {
        if (ImGui::MenuItem(tr("hier.create_empty"))) {
            const std::string before = ctx.SnapshotSceneJson();
            ecs::Entity ne = ctx.CreateEntity("Empty");
            ctx.Select(ne, false);
            if (!ctx.Playing()) ctx.PushStructuralUndo("创建实体", before);
        }
        if (ImGui::MenuItem(tr("hier.create_sprite"))) {
            const std::string before = ctx.SnapshotSceneJson();
            ecs::Entity ne = ctx.CreateSpriteEntity("Sprite");
            ctx.Select(ne, false);
            if (!ctx.Playing()) ctx.PushStructuralUndo("创建实体", before);
        }
        if (ImGui::MenuItem(tr("hier.create_ui_doc"))) // 批③d 前置 T4
            uiDocPickOpen_ = true; // 先选 .rml 再建实体（取消 = 不建）
        ImGui::EndPopup();
    }

    // 批③d 前置 T4：UI Document 创建弹窗——列项目内 .rml 资产，选中即建实体挂
    // UIDocument（guid 真源）；取消/空列表 = 不建实体（防"空 UI 实体"堆积）。
    // 弹窗 ID = 本地化标题（OpenPopup/BeginPopupModal 同帧同字符串共用）
    const char* uiPickId = tr("hier.ui_pick_title");
    if (uiDocPickOpen_) {
        uiDocPickOpen_ = false;
        ImGui::OpenPopup(uiPickId);
    }
    if (ImGui::BeginPopupModal(uiPickId, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(tr("hier.ui_pick_body"));
        ImGui::Separator();
        bool any = false;
        for (const auto& en : ctx.Assets().Entries()) {
            if (en.type != AssetType::Rml || en.missing) continue;
            any = true;
            if (ImGui::Selectable(en.relPath.c_str())) {
                const std::string before = ctx.SnapshotSceneJson();
                ecs::Entity ne = ctx.CreateEntity("UIDocument");
                ecs::UIDocument& ud = ctx.ActiveScene().Emplace<ecs::UIDocument>(ne);
                ud.sourceAssetGuid = en.guid;
                ctx.Select(ne, false);
                ctx.dirty = true;
                if (!ctx.Playing()) ctx.PushStructuralUndo("创建 UI Document", before);
                LEMON_LOG("创建 UIDocument 实体（%s）——进 Play 声明式装载",
                          en.relPath.c_str());
                ImGui::CloseCurrentPopup();
            }
        }
        if (!any)
            ImGui::TextDisabled("%s", tr("hier.ui_pick_empty"));
        ImGui::Separator();
        if (ImGui::Button(tr("common.cancel"))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::Separator();
    // 滚动条按需出现（曾挂 AlwaysVerticalScrollbar：空列表也常驻——手测反馈移除）
    ImGui::BeginChild("tree", ImVec2(0, 0), ImGuiChildFlags_None);

    if (ctx.Playing() && filter_.empty()) {
        // 批⑩：Play 态场景组显示（编辑态/过滤态路径逐字节不变——R3）
        DrawPlaySceneGroups(app);
    } else if (filter_.empty()) {
        // 性能批②：根一次收集（O(N) TryGet，~0.05ms@万级）。全部为叶（平铺海）且
        // 超阈值 → ImGuiListClipper 只提交可见行——万级全画曾是 ui 段 87% 占比。
        // 带父子结构的场景走原递归（clipper 行号假设每根恰好一行）。
        constexpr size_t kClipThreshold = 256;
        rootCache_.clear();
        bool anyChildren = false;
        scene.Each([&](ecs::Entity e) {
            const ecs::Hierarchy* h = scene.TryGet<ecs::Hierarchy>(e);
            if (h && !h->parent.IsNull() && scene.Alive(h->parent)) return; // 非根
            rootCache_.push_back(e);
            if (h && !h->firstChild.IsNull() && scene.Alive(h->firstChild))
                anyChildren = true;
        });
        if (!anyChildren && rootCache_.size() > kClipThreshold) {
            ImGuiListClipper clip;
            clip.Begin((int)rootCache_.size());
            while (clip.Step())
                for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i)
                    DrawNodeRow(app, rootCache_[(size_t)i]);
        } else {
            for (ecs::Entity e : rootCache_) DrawNode(app, e, scene.Has<ecs::Hierarchy>(e));
        }
    } else {
        scene.Each([&](ecs::Entity e) {
            const ecs::Hierarchy* h = scene.TryGet<ecs::Hierarchy>(e);
            if (h && !h->parent.IsNull() && scene.Alive(h->parent)) return; // 非根
            if (PassFilter(scene, e, filter_.c_str()) || SubtreeMatches(scene, e, filter_.c_str()))
                DrawNode(app, e, scene.Has<ecs::Hierarchy>(e));
        });
    }

    // 空区右键 / 拖放摘根。NoOpenOverItems：右键行条目时禁开本菜单——否则与
    // BeginPopupContextItem(node_ctx) 同帧双触发，后开者关掉前者，行右键永远
    // 只见"创建"两项（C5/C6 手测"没有摘根/删除"的真因；无渲染 imgui 最小程序
    // 复现：不带 flag 时 node_ctx=0/hierarchy_bg=1，带 flag 反转）
    if (ImGui::BeginPopupContextWindow("hierarchy_bg", ImGuiPopupFlags_NoOpenOverItems)) {
        // C1：空区创建进 Undo（对齐"+ 创建"弹窗三行快照写法——此前漏推，
        // 创建后 Ctrl+Z 报"栈空"）
        if (ImGui::MenuItem(tr("hier.ctx_create_empty"))) {
            const std::string before = ctx.SnapshotSceneJson();
            ecs::Entity ne = ctx.CreateEntity("Empty");
            ctx.Select(ne, false);
            if (!ctx.Playing()) ctx.PushStructuralUndo("创建实体", before);
        }
        if (ImGui::MenuItem(tr("hier.ctx_create_sprite"))) {
            const std::string before = ctx.SnapshotSceneJson();
            ecs::Entity ne = ctx.CreateSpriteEntity("Sprite");
            ctx.Select(ne, false);
            if (!ctx.Playing()) ctx.PushStructuralUndo("创建实体", before);
        }
        if (ImGui::MenuItem(tr("hier.ctx_create_ui_doc"))) // 批③d 前置 T4（空区右键同 "+ 创建"）
            uiDocPickOpen_ = true;
        ImGui::EndPopup();
    }
    // C2：点空白清选（干净左击 = 非拖拽收尾、不落在任何条目上；与 SceneView
    // 点空白清选同语义）。拖拽位移阈值 3px：行拖挂接的释放不算点击。
    if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered() &&
        ImGui::IsMouseReleased(ImGuiMouseButton_Left) &&
        ImGui::GetIO().MouseDragMaxDistanceSqr[0] < 9.0f)
        ctx.ClearSelection();
    // 空白区显式拖放目标（Dummy 占满剩余区）：窗口级 BeginDragDropTarget 实际
    // 测的是 LastItemData（= 最后一行树节点）——树不满一屏时拖到真空白处无
    // 任何 target，子节点拖不出来（单根场景实抓）。Dummy 以 id=0 提交
    //（ItemAdd(bb, 0)）：HoveredRect 照置 → BeginDragDropTarget 走
    // GetIDFromRectangle 回退命中空白区；HoveredId 只记 id≠0 的 item，
    // 故 IsAnyItemHovered 不受影响（C2 点空白清选 / NoOpenOverItems 空区
    // 右键菜单照旧）。仍放在 C2 之后：防御性顺序，语义零耦合。
    const ImVec2 blank = ImGui::GetContentRegionAvail();
    if (blank.y > 0.0f) {
        ImGui::Dummy(blank);
        if (ImGui::BeginDragDropTarget()) { // 拖到空白 = 摘根
            if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("LemonEntity")) {
                ecs::Entity dropped{};
                std::memcpy(&dropped, p->Data, sizeof(dropped));
                const std::string before = ctx.SnapshotSceneJson();
                if (SceneSetParent(scene, dropped, ecs::Entity::Null())) {
                    ctx.dirty = true;
                    if (!ctx.Playing()) ctx.PushStructuralUndo("脱离父级", before);
                }
            }
            ImGui::EndDragDropTarget();
        }
    }
    ImGui::EndChild();
    ImGui::End();
    probeAccum();
}

// 批⑩：Play 态场景组渲染（SceneMembership.h「编辑器按位分组显示归批⑩」预留位
// 兑现）。Unity 式分组：DDOL 根置顶合成组（位在根——批⑦ D1 根位式，根无祖先 =
// 位检查即 lineage）→ 已装载档案组按记录序（装载序；isLoaded=false 不显示 =
// 异步 staging 期新档天然隐藏）→ 未指派兜底组（防御显示，运行时装载路径不应
// 出现）。未指派根显示归并活动组（Play 中编辑器新建实体无打标——Unity 心智
// 模型；仅显示面语义，生命周期不变：下次 Single 换场同被清）。组内全叶 >256
// 走 clipper（与编辑态同阈同性质——bench-survivor 万级平铺 Play 态受益者保形）。
void HierarchyPanel::DrawPlaySceneGroups(EditorApp& app) {
    using lemon::editor::loc::tr;
    using lemon::editor::loc::trFmt;
    EditorContext& ctx = app.Ctx();
    ecs::Scene& scene = ctx.ActiveScene();
    ecs::World& world = ctx.ActiveWorld();

    // 根收集与编辑态同判据（父缺失/父亡 = 根）
    rootCache_.clear();
    scene.Each([&](ecs::Entity e) {
        const ecs::Hierarchy* h = scene.TryGet<ecs::Hierarchy>(e);
        if (h && !h->parent.IsNull() && scene.Alive(h->parent)) return; // 非根
        rootCache_.push_back(e);
    });

    const uint32_t recN = world.SceneRecordCount();
    // 句柄 → 记录索引：记录数 = 场景数（个位量级），线性扫即可（发号单调但不
    // 与 vector 索引显式绑定，不偷懒假设 handle == index+1）
    const auto recIndexOf = [&](uint32_t handle) -> int {
        for (uint32_t i = 0; i < recN; ++i)
            if (const ecs::World::SceneRecord* r = world.SceneRecordAt(i);
                r && r->handle == handle)
                return (int)i;
        return -1;
    };
    // review F1（2026-10-09）：仅**已装载**记录入组——句柄指向未装载记录 = 异常
    // 态（Single 清场同帧销毁非 DDOL 实体、staging 实体在独立 registry，合法
    // 流程到不了），真出现时落兜底组可见，不静默隐没
    const auto loadedIdx = [&](uint32_t handle) -> int {
        const int i = recIndexOf(handle);
        if (i < 0) return -1;
        const ecs::World::SceneRecord* r = world.SceneRecordAt((uint32_t)i);
        return r && r->isLoaded ? i : -1;
    };
    const uint32_t activeHandle = world.ActiveSceneHandle();
    const int activeIdx = activeHandle != ecs::kSceneHandleUnassigned
                              ? loadedIdx(activeHandle)
                              : -1;

    std::vector<uint32_t> ddol, unassigned;
    std::vector<std::vector<uint32_t>> groups(recN);
    std::vector<char> groupHasChildren(recN, 0);
    bool ddolHasChildren = false, unassignedHasChildren = false;
    const auto rootHasKids = [&](ecs::Entity e) {
        const ecs::Hierarchy* h = scene.TryGet<ecs::Hierarchy>(e);
        return h && !h->firstChild.IsNull() && scene.Alive(h->firstChild);
    };
    for (uint32_t i = 0; i < rootCache_.size(); ++i) {
        const ecs::Entity e = rootCache_[i];
        const ecs::SceneMembership* m = scene.TryGet<ecs::SceneMembership>(e);
        const bool kids = rootHasKids(e);
        if (m && (m->flags & ecs::kSceneFlagDontDestroyOnLoad)) {
            ddol.push_back(i);
            ddolHasChildren |= kids;
            continue;
        }
        const uint32_t h = m ? m->scene : ecs::kSceneHandleUnassigned;
        int gi = h != ecs::kSceneHandleUnassigned ? loadedIdx(h) : -1;
        if (gi < 0 && h == ecs::kSceneHandleUnassigned) gi = activeIdx; // 无句柄 → 活动组
        if (gi >= 0) {
            groups[(size_t)gi].push_back(i);
            groupHasChildren[(size_t)gi] = groupHasChildren[(size_t)gi] || kids;
        } else {
            unassigned.push_back(i); // 未知/未装载句柄且无活动档案可归并——兜底组
            unassignedHasChildren |= kids;
        }
    }

    constexpr size_t kClipThreshold = 256; // 性②：与编辑态平铺同阈
    const auto drawGroup = [&](const char* label, bool accent,
                               const std::vector<uint32_t>& idx, bool hasChildren) {
        // 组头：活动场 accent 着色，其余微降不透明度（层级弱于实体行）
        ImGui::PushStyleColor(ImGuiCol_Text,
                              accent ? theme::kAccent
                                     : ImVec4(theme::kText.x, theme::kText.y,
                                              theme::kText.z, 0.72f));
        ImGui::TextUnformatted(label);
        ImGui::PopStyleColor();
        ImGui::Separator();
        if (idx.empty()) return;
        if (!hasChildren && idx.size() > kClipThreshold) {
            ImGuiListClipper clip;
            clip.Begin((int)idx.size());
            while (clip.Step())
                for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i)
                    DrawNodeRow(app, rootCache_[idx[(size_t)i]]);
        } else {
            for (uint32_t i : idx)
                DrawNode(app, rootCache_[i], scene.Has<ecs::Hierarchy>(rootCache_[i]));
        }
    };

    if (!ddol.empty())
        drawGroup(tr("hier.group_ddol"), false, ddol, ddolHasChildren);
    for (uint32_t i = 0; i < recN; ++i) {
        const ecs::World::SceneRecord* r = world.SceneRecordAt(i);
        if (!r || !r->isLoaded) continue;
        drawGroup(trFmt("hier.scene_header", {r->name, std::to_string(groups[i].size())})
                      .c_str(),
                  r->handle == activeHandle, groups[i], groupHasChildren[i] != 0);
    }
    if (!unassigned.empty())
        drawGroup(tr("hier.group_unassigned"), false, unassigned, unassignedHasChildren);
}

bool HierarchyPanel::PassFilter(ecs::Scene& s, ecs::Entity e, const char* filter) {
    if (!filter[0]) return true;
    const ecs::Meta* m = s.TryGet<ecs::Meta>(e);
    return m && StrIStr(m->tag, filter) != nullptr; // C3：忽略大小写
}

bool HierarchyPanel::SubtreeMatches(ecs::Scene& s, ecs::Entity e, const char* filter) {
    // #35：递归全子树（原只看直接子节点——A→B→C→D 搜 D 名时 SubtreeMatches(A)
    // 只验 B，根被过滤掉后匹配实体永不可见）。深度帽 64 防坏档环链（SubtreeSizeOf
    // 同款），命中即短路。
    if (!filter[0]) return true;
    return SubtreeMatchesRec(s, e, filter, 0);
}

bool HierarchyPanel::SubtreeMatchesRec(ecs::Scene& s, ecs::Entity e, const char* filter,
                                       int depth) {
    if (depth > 64) return false; // 环链防线（正常树高远低于此）
    const ecs::Hierarchy* h = s.TryGet<ecs::Hierarchy>(e);
    if (!h) return false;
    for (ecs::Entity c = h->firstChild; !c.IsNull() && s.Alive(c);) {
        if (PassFilter(s, c, filter)) return true;
        if (SubtreeMatchesRec(s, c, filter, depth + 1)) return true;
        const ecs::Hierarchy* ch = s.TryGet<ecs::Hierarchy>(c);
        c = ch && !ch->next.IsNull() ? ch->next : ecs::Entity::Null();
    }
    return false;
}

bool HierarchyPanel::DrawNodeRow(EditorApp& app, ecs::Entity e) {
    using lemon::editor::loc::tr;
    EditorContext& ctx = app.Ctx();
    ecs::Scene& scene = ctx.ActiveScene();

    const ecs::Hierarchy* h = scene.TryGet<ecs::Hierarchy>(e);
    ecs::Entity first = h && !h->firstChild.IsNull() && scene.Alive(h->firstChild)
                            ? h->firstChild
                            : ecs::Entity::Null();
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                               ImGuiTreeNodeFlags_OpenOnDoubleClick |
                               ImGuiTreeNodeFlags_SpanAvailWidth;
    if (first.IsNull()) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    if (ctx.IsSelected(e)) flags |= ImGuiTreeNodeFlags_Selected;
    if (!first.IsNull() && openedOnce_.insert(e.id).second)
        flags |= ImGuiTreeNodeFlags_DefaultOpen; // 首见节点默认展开

    // M4.7b 行前类型图标：Prefab 实例 > 带脚本 > 精灵 > 空实体（标签留两空格让位）
    const ecs::Meta* meta = scene.TryGet<ecs::Meta>(e);
    const IconKind icon = meta && meta->prefabId    ? IconKind::AssetPrefab
                          : scene.Has<scripting::ScriptBox>(e) ? IconKind::Script
                          : scene.Has<ecs::SpriteRenderer>(e) ? IconKind::Sprite
                                                              : IconKind::Entity;
    const bool nodeOpen =
        ImGui::TreeNodeEx((void*)(uintptr_t)e.id, flags, "  %s", DisplayName(scene, e));
    // --smoke-ui 定位：行矩形（键 "hier.<tag>"；未命名实体不登记）
    if (meta && meta->tag[0])
        testhooks::Stash((std::string("hier.") + meta->tag).c_str(), ImGui::GetItemRectMin(),
                         ImGui::GetItemRectMax());
    { // 图标叠画在行首箭头右侧（白形状染 prefab 蓝 / 其余主题文本色）
        void* tex = app.Viewport().IconTex();
        if (tex) {
            float u0, v0, u1, v1;
            app.Viewport().Assets().IconUV(icon, u0, v0, u1, v1);
            const ImVec2 p = ImGui::GetItemRectMin();
            const float s = ImGui::GetTextLineHeight() * 0.9f;
            const float ox = p.x + ImGui::GetStyle().FramePadding.x + s * 1.15f;
            ImGui::GetWindowDrawList()->AddImage(
                (ImTextureID)tex, ImVec2(ox, p.y + (ImGui::GetTextLineHeight() - s) * 0.5f),
                ImVec2(ox + s, p.y + (ImGui::GetTextLineHeight() + s) * 0.5f), ImVec2(u0, v0),
                ImVec2(u1, v1),
                ImGui::ColorConvertFloat4ToU32(icon == IconKind::AssetPrefab ? theme::kAccent
                                                            : theme::kText));
        }
    }
    // 批⑩：DDOL 徽标——membership 位在根（批⑦ D1 根位式），行尾右对齐小徽
    //（DDOL 组头已置顶归组，行徽 = 逐行可视确认；过滤态平铺同样生效）
    if (const ecs::SceneMembership* sm = scene.TryGet<ecs::SceneMembership>(e);
        sm && (sm->flags & ecs::kSceneFlagDontDestroyOnLoad)) {
        const char* tag = tr("hier.ddol_badge");
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p0 = ImGui::GetItemRectMin(), p1 = ImGui::GetItemRectMax();
        const ImVec2 ts = ImGui::CalcTextSize(tag);
        const float padX = ImGui::GetStyle().FramePadding.x;
        const float lh = ImGui::GetTextLineHeight();
        const float cy = (p0.y + p1.y) * 0.5f;
        const float bw = ts.x + padX, bh = lh * 0.95f;
        dl->AddRectFilled(
            ImVec2(p1.x - padX - bw, cy - bh * 0.5f), ImVec2(p1.x - padX, cy + bh * 0.5f),
            ImGui::ColorConvertFloat4ToU32(ImVec4(theme::kAccent.x, theme::kAccent.y,
                                                  theme::kAccent.z, 0.22f)),
            3.0f);
        dl->AddText(ImVec2(p1.x - padX - bw + padX * 0.5f, cy - lh * 0.5f),
                    ImGui::ColorConvertFloat4ToU32(theme::kAccent), tag);
    }
    if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        ctx.Select(e, ImGui::GetIO().KeyCtrl); // Ctrl 点选 = 增删选；主选中 = 末位
    // 叶子节点双击 = 重命名（§4-7；父节点双击已被 展开/收起 占用——其用 F2/右键）
    if (first.IsNull() && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
        StartRename(scene, e);
    // 重命名行（节点下一行内联输入：Enter 提交 / ESC 取消 / 点别处 = 提交）
    if (!renaming_.IsNull() && e == renaming_) {
        if (renameFocus_) {
            ImGui::SetKeyboardFocusHere();
            renameFocus_ = false;
        }
        ImGui::SetNextItemWidth(-1);
        const bool committed = ImGui::InputText(
            "##rename", &renameBuf_, ImGuiInputTextFlags_EnterReturnsTrue |
                                        ImGuiInputTextFlags_AutoSelectAll);
        if (ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            CommitRename(app, e, false);
        else if (committed || ImGui::IsItemDeactivated())
            CommitRename(app, e, true);
    }
    if (ImGui::BeginDragDropSource()) {
        ImGui::SetDragDropPayload("LemonEntity", &e, sizeof(e));
        ImGui::TextUnformatted(DisplayName(scene, e));
        ImGui::EndDragDropSource();
    }
    static const bool dbg_dnd = std::getenv("LEMON_SMOKE_UI_DEBUG") != nullptr;
    if (dbg_dnd && (GImGui->DragDropActive ||
                    (GImGui->FrameCount >= 90 && GImGui->FrameCount <= 97))) {
        ImGuiContext& g = *GImGui;
        const ImRect& rb = g.LastItemData.Rect;
        std::printf(
            "[dnd] f=%u row=%llx tag='%s' hovRect=%d skip=%d rect=(%.0f,%.0f)-(%.0f,%.0f) "
            "mouse=(%.0f,%.0f) hovWin='%s' hovUnder='%s' prev=%llx cur=%llx own=%d "
            "srcId=%llx lastId=%llx rootEq=%d dtype='%s' dfc=%d ren=%d scroll=%.0f\n",
            g.FrameCount, (unsigned long long)(e.id & 0xFFFFFFFF),
            meta && meta->tag[0] ? meta->tag : "?",
            (g.LastItemData.StatusFlags & ImGuiItemStatusFlags_HoveredRect) ? 1 : 0,
            g.CurrentWindow->SkipItems ? 1 : 0,
            rb.Min.x, rb.Min.y, rb.Max.x, rb.Max.y,
            g.IO.MousePos.x, g.IO.MousePos.y,
            g.HoveredWindow ? g.HoveredWindow->Name : "-",
            g.HoveredWindowUnderMovingWindow ? g.HoveredWindowUnderMovingWindow->Name : "-",
            (unsigned long long)g.DragDropAcceptIdPrev,
            (unsigned long long)g.DragDropAcceptIdCurr,
            g.IO.MouseDownOwned[0] ? 1 : 0,
            (unsigned long long)g.DragDropPayload.SourceId,
            (unsigned long long)g.LastItemData.ID,
            g.HoveredWindowUnderMovingWindow &&
                    g.CurrentWindow->RootWindowDockTree ==
                        g.HoveredWindowUnderMovingWindow->RootWindowDockTree
                ? 1 : 0,
            g.DragDropPayload.DataType,
            g.DragDropPayload.DataFrameCount,
            renaming_ == e ? 1 : 0,
            g.CurrentWindow->Scroll.y);
    }
    if (ImGui::BeginDragDropTarget()) { // 拖到我身上 = 挂为我的子（成环被引擎拒绝）
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("LemonEntity")) {
            ecs::Entity dropped{};
            std::memcpy(&dropped, p->Data, sizeof(dropped));
            const std::string before = ctx.SnapshotSceneJson();
            if (std::getenv("LEMON_SMOKE_UI_DEBUG"))
                std::printf("[smoke-ui dbg] drop: dropped=%llx onto=%llx\n",
                            (unsigned long long)(dropped.id & 0xFFFFFFFF),
                            (unsigned long long)(e.id & 0xFFFFFFFF));
            if (SceneSetParent(scene, dropped, e)) {
                ctx.dirty = true;
                if (!ctx.Playing()) ctx.PushStructuralUndo("挂接父子", before);
            }
        }
        ImGui::EndDragDropTarget();
    }
    // 弹窗 ID 按节点隔离：node_ctx 曾全节点共享一个 ID——NoOpenOverItems 修复
    // 让菜单首次真正可见时暴露：每个节点的调用点都向同一弹窗提交内容 → 菜单
    // 按实体数整份重复 + 同名条目 conflicting ID 报错框（Inspector 字段级/
    // AssetBrowser DrawItem 同样靠 PushID 保唯一，本面板此前漏）
    ImGui::PushID((const void*)(uintptr_t)e.id);
    if (ImGui::BeginPopupContextItem("node_ctx")) {
        using lemon::editor::loc::tr;
        ctx.Select(e, false);
        if (ImGui::MenuItem(tr("hier.rename_f2"))) StartRename(scene, e);
        if (ImGui::MenuItem(tr("hier.duplicate_ctrl_d"))) {
            // C1 同类：右键"复制"此前漏推结构轨（Ctrl+D 键路有——不对称）
            const std::string before = ctx.SnapshotSceneJson();
            ecs::Entity copy = ctx.DuplicateEntity(e);
            if (!copy.IsNull()) {
                ctx.Select(copy, false);
                if (!ctx.Playing()) ctx.PushStructuralUndo("复制实体", before);
            }
        }
        if (ImGui::MenuItem(tr("hier.detach"))) {
            const std::string before = ctx.SnapshotSceneJson();
            if (SceneDetach(scene, e)) {
                ctx.dirty = true;
                if (!ctx.Playing()) ctx.PushStructuralUndo("脱离父级", before);
            }
        }
        ImGui::Separator();
        if (ImGui::MenuItem(tr("hier.make_prefab"), nullptr, false, !ctx.Playing())) {
            const std::string before = ctx.SnapshotSceneJson();
            if (ctx.MakePrefabFrom(e)) ctx.PushStructuralUndo("Prefab 化", before);
        }
        ImGui::Separator();
        if (ImGui::MenuItem(tr("hier.delete_del"))) {
            const std::string before = ctx.SnapshotSceneJson();
            ctx.DestroyEntityTree(e);
            if (!ctx.Playing()) ctx.PushStructuralUndo("删除实体", before);
        }
        ImGui::EndPopup();
    }
    ImGui::PopID();

    return nodeOpen;
}

void HierarchyPanel::DrawNode(EditorApp& app, ecs::Entity e, bool /*hasHierarchy*/) {
    // 行体内自行 TryGet<Hierarchy>（拆分后本参数仅保留调用点签名不变）
    EditorContext& ctx = app.Ctx();
    ecs::Scene& scene = ctx.ActiveScene();

    // TreePop 配对依据 = TreeNodeEx 提交时"有孩子"，不能事后重查：行内拖放会
    // 当场改父子（摘走独子 → 已 push 却不 pop；拖入叶子 → 未 push 却 pop），
    // 两者都触发 ImGui "Missing TreePop()" 栈断言（missingTresPop.scene 实抓）
    const ecs::Hierarchy* h0 = scene.TryGet<ecs::Hierarchy>(e);
    const bool hadChildren =
        h0 && !h0->firstChild.IsNull() && scene.Alive(h0->firstChild);

    const bool nodeOpen = DrawNodeRow(app, e);
    const ecs::Hierarchy* h = scene.TryGet<ecs::Hierarchy>(e);
    const ecs::Entity first = h && !h->firstChild.IsNull() && scene.Alive(h->firstChild)
                                  ? h->firstChild
                                  : ecs::Entity::Null();
    if (nodeOpen && hadChildren) {
        for (ecs::Entity c = first; !c.IsNull();) {
            const ecs::Hierarchy* ch = scene.TryGet<ecs::Hierarchy>(c);
            ecs::Entity next = ch && !ch->next.IsNull() && scene.Alive(ch->next) ? ch->next
                                                                                 : ecs::Entity::Null();
            if (PassFilter(scene, c, filter_.c_str()) ||
                SubtreeMatches(scene, c, filter_.c_str()))
                DrawNode(app, c, true);
            c = next;
        }
        ImGui::TreePop();
    }
}

} // namespace lemon::editor
