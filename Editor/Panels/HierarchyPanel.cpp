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
#include "Core/Log.h"
#include "ECS/Hierarchy.h"
#include "Components/RenderComponents.h"
#include "Interaction/ViewportRenderer.h"
#include "Scripting/ScriptBox.h"
#include "Tooling/TestHooks.h"
#include "Tooling/Theme.h"
#include "EditorContext.h"
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
    const auto probeT0 = std::chrono::steady_clock::now();
    const auto probeAccum = [&]() {
        if (g_hierProbeOn) {
            g_hierProbe.totalMs += std::chrono::duration<double, std::milli>(
                                       std::chrono::steady_clock::now() - probeT0)
                                       .count();
            ++g_hierProbe.frames;
        }
    };
    bool winOpen = true;
    if (!ImGui::Begin(Name(), &winOpen,
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
    ImGui::InputTextWithHint("##search", "搜索实体名…", &filter_);
    ImGui::PopItemWidth();
    ImGui::SameLine();
    if (ImGui::Button("+ 创建")) ImGui::OpenPopup("create_entity");
    if (ImGui::BeginPopup("create_entity")) {
        if (ImGui::MenuItem("空实体 (Empty)")) {
            const std::string before = ctx.SnapshotSceneJson();
            ecs::Entity ne = ctx.CreateEntity("Empty");
            ctx.Select(ne, false);
            if (!ctx.Playing()) ctx.PushStructuralUndo("创建实体", before);
        }
        if (ImGui::MenuItem("精灵 (Sprite)")) {
            const std::string before = ctx.SnapshotSceneJson();
            ecs::Entity ne = ctx.CreateSpriteEntity("Sprite");
            ctx.Select(ne, false);
            if (!ctx.Playing()) ctx.PushStructuralUndo("创建实体", before);
        }
        ImGui::EndPopup();
    }

    ImGui::Separator();
    // 滚动条按需出现（曾挂 AlwaysVerticalScrollbar：空列表也常驻——手测反馈移除）
    ImGui::BeginChild("tree", ImVec2(0, 0), ImGuiChildFlags_None);

    if (filter_.empty()) {
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
        if (ImGui::MenuItem("创建空实体")) {
            const std::string before = ctx.SnapshotSceneJson();
            ecs::Entity ne = ctx.CreateEntity("Empty");
            ctx.Select(ne, false);
            if (!ctx.Playing()) ctx.PushStructuralUndo("创建实体", before);
        }
        if (ImGui::MenuItem("创建精灵")) {
            const std::string before = ctx.SnapshotSceneJson();
            ecs::Entity ne = ctx.CreateSpriteEntity("Sprite");
            ctx.Select(ne, false);
            if (!ctx.Playing()) ctx.PushStructuralUndo("创建实体", before);
        }
        ImGui::EndPopup();
    }
    // C2：点空白清选（干净左击 = 非拖拽收尾、不落在任何条目上；与 SceneView
    // 点空白清选同语义）。拖拽位移阈值 3px：行拖挂接的释放不算点击。
    if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered() &&
        ImGui::IsMouseReleased(ImGuiMouseButton_Left) &&
        ImGui::GetIO().MouseDragMaxDistanceSqr[0] < 9.0f)
        ctx.ClearSelection();
    if (ImGui::BeginDragDropTarget()) { // 拖到空白 = 摘根
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("LemonEntity")) {
            ecs::Entity dropped{};
            std::memcpy(&dropped, p->Data, sizeof(dropped));
            const std::string before = ctx.SnapshotSceneJson();
            if (SceneSetParent(scene, dropped, ecs::Entity::Null())) {
                ctx.dirty = true;
                if (!ctx.Playing()) ctx.PushStructuralUndo("摘根", before);
            }
        }
        ImGui::EndDragDropTarget();
    }
    ImGui::EndChild();
    ImGui::End();
    probeAccum();
}

bool HierarchyPanel::PassFilter(ecs::Scene& s, ecs::Entity e, const char* filter) {
    if (!filter[0]) return true;
    const ecs::Meta* m = s.TryGet<ecs::Meta>(e);
    return m && StrIStr(m->tag, filter) != nullptr; // C3：忽略大小写
}

bool HierarchyPanel::SubtreeMatches(ecs::Scene& s, ecs::Entity e, const char* filter) {
    if (!filter[0]) return true;
    const ecs::Hierarchy* h = s.TryGet<ecs::Hierarchy>(e);
    if (!h) return false;
    for (ecs::Entity c = h->firstChild; !c.IsNull() && s.Alive(c);) {
        if (PassFilter(s, c, filter)) return true;
        const ecs::Hierarchy* ch = s.TryGet<ecs::Hierarchy>(c);
        c = ch && !ch->next.IsNull() ? ch->next : ecs::Entity::Null();
    }
    return false;
}

bool HierarchyPanel::DrawNodeRow(EditorApp& app, ecs::Entity e) {
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
        ctx.Select(e, false);
        if (ImGui::MenuItem("重命名 (F2)")) StartRename(scene, e);
        if (ImGui::MenuItem("复制 (Ctrl+D)")) {
            // C1 同类：右键"复制"此前漏推结构轨（Ctrl+D 键路有——不对称）
            const std::string before = ctx.SnapshotSceneJson();
            ecs::Entity copy = ctx.DuplicateEntity(e);
            if (!copy.IsNull()) {
                ctx.Select(copy, false);
                if (!ctx.Playing()) ctx.PushStructuralUndo("复制实体", before);
            }
        }
        if (ImGui::MenuItem("摘根 (Detach)")) {
            const std::string before = ctx.SnapshotSceneJson();
            if (SceneDetach(scene, e)) {
                ctx.dirty = true;
                if (!ctx.Playing()) ctx.PushStructuralUndo("摘根", before);
            }
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Prefab 化（导出 + 回链）", nullptr, false, !ctx.Playing())) {
            const std::string before = ctx.SnapshotSceneJson();
            if (ctx.MakePrefabFrom(e)) ctx.PushStructuralUndo("Prefab 化", before);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("删除 (Del)")) {
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

    const bool nodeOpen = DrawNodeRow(app, e);
    const ecs::Hierarchy* h = scene.TryGet<ecs::Hierarchy>(e);
    const ecs::Entity first = h && !h->firstChild.IsNull() && scene.Alive(h->firstChild)
                                  ? h->firstChild
                                  : ecs::Entity::Null();
    if (nodeOpen && !first.IsNull()) {
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
