// Lemon 编辑器 — Hierarchy 面板（M4-Editor-Plan §2.2）
// 数据源：Scene 实体遍历 + Hierarchy 父子链（ECS/Hierarchy.h 维护）。
// 验收点：拖拽成环被拒（SetParent 引擎侧拒绝 + Console 告警）；Play 中切数据源 M4.3。
#include <cstdio>
#include <cstring>

#include "App/EditorApp.h"
#include "Components/CoreComponents.h"
#include "Core/Log.h"
#include "ECS/Hierarchy.h"
#include "EditorContext.h"
#include "Panels/BuiltInPanels.h"
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"

namespace lemon::editor {
namespace {

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

} // namespace

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
    if (!ImGui::Begin(Name(), nullptr, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }
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
    ImGui::BeginChild("tree", ImVec2(0, 0), ImGuiChildFlags_None,
                      ImGuiWindowFlags_AlwaysVerticalScrollbar);

    scene.Each([&](ecs::Entity e) {
        const ecs::Hierarchy* h = scene.TryGet<ecs::Hierarchy>(e);
        if (h && !h->parent.IsNull() && scene.Alive(h->parent)) return; // 非根
        if (PassFilter(scene, e, filter_.c_str()) || SubtreeMatches(scene, e, filter_.c_str()))
            DrawNode(app, e, scene.Has<ecs::Hierarchy>(e));
    });

    // 空区右键 / 拖放摘根
    if (ImGui::BeginPopupContextWindow("hierarchy_bg")) {
        if (ImGui::MenuItem("创建空实体")) ctx.Select(ctx.CreateEntity("Empty"), false);
        if (ImGui::MenuItem("创建精灵")) ctx.Select(ctx.CreateSpriteEntity("Sprite"), false);
        ImGui::EndPopup();
    }
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
}

bool HierarchyPanel::PassFilter(ecs::Scene& s, ecs::Entity e, const char* filter) {
    if (!filter[0]) return true;
    const ecs::Meta* m = s.TryGet<ecs::Meta>(e);
    return m && std::strstr(m->tag, filter) != nullptr;
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

void HierarchyPanel::DrawNode(EditorApp& app, ecs::Entity e, bool hasHierarchy) {
    EditorContext& ctx = app.Ctx();
    ecs::Scene& scene = ctx.ActiveScene();

    const ecs::Hierarchy* h = hasHierarchy ? scene.TryGet<ecs::Hierarchy>(e) : nullptr;
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

    const bool nodeOpen =
        ImGui::TreeNodeEx((void*)(uintptr_t)e.id, flags, "%s", DisplayName(scene, e));
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
    if (ImGui::BeginDragDropTarget()) { // 拖到我身上 = 挂为我的子（成环被引擎拒绝）
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("LemonEntity")) {
            ecs::Entity dropped{};
            std::memcpy(&dropped, p->Data, sizeof(dropped));
            const std::string before = ctx.SnapshotSceneJson();
            if (SceneSetParent(scene, dropped, e)) {
                ctx.dirty = true;
                if (!ctx.Playing()) ctx.PushStructuralUndo("挂接父子", before);
            }
        }
        ImGui::EndDragDropTarget();
    }
    if (ImGui::BeginPopupContextItem("node_ctx")) {
        ctx.Select(e, false);
        if (ImGui::MenuItem("重命名 (F2)")) StartRename(scene, e);
        if (ImGui::MenuItem("复制 (Ctrl+D)")) {
            ecs::Entity copy = ctx.DuplicateEntity(e);
            if (!copy.IsNull()) ctx.Select(copy, false);
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
