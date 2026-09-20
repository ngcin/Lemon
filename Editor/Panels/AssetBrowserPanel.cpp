// Lemon 编辑器 — AssetBrowser 面板（M4-Editor-Plan §2.2/§5 M4.4）
// 数据源：AssetDatabase（GUID/.meta/manifest）+ AssetGpuCache 缩略图。
// 交互：目录树 + 缩略图网格；拖拽 sprite 进 SceneView = 建实体、进 Inspector
// sprite 槽 = 设引用；拖 prefab 进 SceneView/Hierarchy = 实例化；右键导入/
// 重命名（guid 随 .meta 走 → 引用不断）/删除（墓碑 + 体检红字）；双击 = 建实体。
#include <cstdio>
#include <cstring>

#include "App/EditorApp.h"
#include "Assets/AssetDatabase.h"
#include "Core/Log.h"
#include "EditorContext.h"
#include "Interaction/ViewportRenderer.h"
#include "Panels/BuiltInPanels.h"
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"

namespace lemon::editor {

namespace {
constexpr float kCell = 92.0f; // 网格单元（含名）

uint8_t KindOf(AssetType t) {
    switch (t) {
        case AssetType::Sprite: return 0;
        case AssetType::Prefab: return 1;
        case AssetType::Script: return 2;
        default: return 3;
    }
}
} // namespace

void AssetBrowserPanel::OnGui(EditorApp& app) {
    if (!ImGui::Begin(Name(), nullptr, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }
    EditorContext& ctx = app.Ctx();
    AssetDatabase& db = ctx.Assets();

    // 工具行：目录下拉 + 搜索 + 重扫 + 计数
    if (ImGui::BeginCombo("##dir", currentDir_.empty() ? "Assets/" : currentDir_.c_str())) {
        for (const std::string& d : db.Directories())
            if (ImGui::Selectable(d.empty() ? "Assets/" : d.c_str(), d == currentDir_))
                currentDir_ = d;
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::PushItemWidth(160);
    ImGui::InputTextWithHint("##filter", "搜索资产…", &filter_);
    ImGui::PopItemWidth();
    ImGui::SameLine();
    if (ImGui::Button("重扫")) app.RescanAssets();
    ImGui::SameLine();
    ImGui::TextDisabled("%u 资产 / 体检红字 %u", db.SpriteAssetCount(), db.HealthIssues());
    ImGui::Separator();

    ImGui::BeginChild("grid", ImVec2(0, 0), ImGuiChildFlags_None,
                      ImGuiWindowFlags_AlwaysVerticalScrollbar);
    const float avail = ImGui::GetContentRegionAvail().x;
    const int cols = std::max(1, (int)(avail / kCell));
    int col = 0;
    for (const AssetEntry* e : db.EntriesInDir(currentDir_)) {
        if (!filter_.empty() && !strstr(e->FileName().c_str(), filter_.c_str())) continue;
        if (col++ > 0) ImGui::SameLine();
        DrawItem(app, *e);
        if (col >= cols) col = 0;
    }

    // 空区右键：导入
    if (ImGui::BeginPopupContextWindow("assets_bg")) {
        if (ImGui::MenuItem("导入文件…")) app.MenuImportAsset();
        ImGui::EndPopup();
    }
    // 拖 prefab 进空区 = 实例化到编辑相机中心
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* pay = ImGui::AcceptDragDropPayload("LemonAsset")) {
            AssetDragPayload d{};
            std::memcpy(&d, pay->Data, sizeof(d));
            if (d.kind == 1) {
                const std::string before = ctx.SnapshotSceneJson();
                ecs::Entity root = ctx.InstantiatePrefabAsset(d.guid,
                                                              app.Viewport().SceneCam().center);
                if (!root.IsNull()) {
                    ctx.Select(root, false);
                    if (!ctx.Playing()) ctx.PushStructuralUndo("Prefab 实例化", before);
                }
            }
        }
        ImGui::EndDragDropTarget();
    }
    ImGui::EndChild();

    // 重命名模态（InputText 走 IME；提交 = db.Rename → .meta 随行 → 引用不断）
    if (renamingGuid_) ImGui::OpenPopup("重命名资产");
    if (ImGui::BeginPopupModal("重命名资产", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("新路径（相对 Assets/；.meta 随行 → 场景引用不断）");
        ImGui::InputText("##newpath", &renameBuf_);
        if (ImGui::Button("确定", ImVec2(120, 0)) || ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
            if (AssetEntry* target = ctx.Assets().FindByGuid(renamingGuid_))
                ctx.Assets().Rename(*target, renameBuf_);
            renamingGuid_ = 0;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("取消", ImVec2(120, 0)) ||
            ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            renamingGuid_ = 0;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::End();
}

void AssetBrowserPanel::DrawItem(EditorApp& app, const AssetEntry& e) {
    EditorContext& ctx = app.Ctx();
    const bool isSprite = e.type == AssetType::Sprite;

    ImGui::PushID((int)e.guid);
    ImGui::BeginGroup();
    // 缩略图 / 类型图标（sprite = 纹理页；其余 = 调色板色块 uv 子区）
    const ImVec2 size(72, 72);
    void* tex = isSprite ? app.AssetGpu().Thumbnail(e.guid) : app.Viewport().PaletteIconTex();
    ImVec2 uv0(0, 0), uv1(1, 1);
    if (!isSprite && tex) { // 调色板第 kind 格
        const float cell = 1.0f / (float)ProceduralAtlas::kPaletteSprites;
        uv0 = ImVec2(cell * KindOf(e.type), 0.0f);
        uv1 = ImVec2(cell * (KindOf(e.type) + 1), 1.0f);
    }
    if (tex) ImGui::ImageButton("##thumb", tex, size, uv0, uv1);
    else ImGui::Button("?", size);
    const bool hovered = ImGui::IsItemHovered();
    const bool doubleClicked = hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);

    // 悬浮提示：类型 / guid / 导入状态
    if (hovered) {
        uint32_t w = 0, h = 0;
        const bool imported = app.AssetGpu().PageInfo(e.guid, w, h);
        std::string dims = isSprite && imported
                               ? "已导入 " + std::to_string(w) + "x" + std::to_string(h)
                               : (isSprite ? "未导入" : AssetTypeName(e.type));
        ImGui::SetTooltip("%s\n%s  guid %s\n%s", e.FileName().c_str(), AssetTypeName(e.type),
                          AssetDatabase::GuidToHex(e.guid).c_str(), dims.c_str());
    }

    // 拖拽源：sprite/prefab/script 统一载荷（目标按 kind 分派）
    if (ImGui::BeginDragDropSource()) {
        AssetDragPayload d{e.guid, e.spriteId, KindOf(e.type)};
        ImGui::SetDragDropPayload("LemonAsset", &d, sizeof(d));
        ImGui::TextUnformatted(e.FileName().c_str());
        ImGui::EndDragDropSource();
    }

    // 双击 sprite = 建实体（与拖入 SceneView 同一通路）
    if (doubleClicked && isSprite) {
        const std::string before = ctx.SnapshotSceneJson();
        ecs::Entity ne = ctx.CreateSpriteEntityFromAsset(e.FileName().c_str(), e.guid,
                                                         app.Viewport().SceneCam().center);
        if (!ne.IsNull()) {
            ctx.Select(ne, false);
            if (!ctx.Playing()) ctx.PushStructuralUndo("资产建实体", before);
        }
    }

    // 文件名（截断 12 字符）+ 右键菜单
    char shortName[16];
    std::snprintf(shortName, sizeof(shortName), "%.12s%s", e.FileName().c_str(),
                  e.FileName().size() > 12 ? "…" : "");
    ImGui::TextWrapped("%s", shortName);
    if (ImGui::BeginPopupContextItem("asset_ctx")) {
        if (ImGui::MenuItem("重命名…")) {
            renamingGuid_ = e.guid;
            renameBuf_ = e.relPath;
        }
        if (ImGui::MenuItem("复制 GUID"))
            ImGui::SetClipboardText(AssetDatabase::GuidToHex(e.guid).c_str());
        ImGui::Separator();
        if (ImGui::MenuItem("删除（转墓碑）")) {
            if (AssetEntry* target = ctx.Assets().FindByGuid(e.guid))
                ctx.Assets().Remove(*target);
        }
        ImGui::EndPopup();
    }
    ImGui::EndGroup();
    ImGui::PopID();
}

} // namespace lemon::editor
