// Lemon 编辑器 — AssetBrowser 面板（M4-Editor-Plan §2.2/§5 M4.4）
// 数据源：AssetDatabase（GUID/.meta/manifest）+ AssetGpuCache 缩略图。
// 交互：目录树 + 缩略图网格；拖拽 sprite 进 SceneView = 建实体、进 Inspector
// sprite 槽 = 设引用；拖 prefab 进 SceneView/Hierarchy = 实例化；右键导入/
// 重命名（guid 随 .meta 走 → 引用不断）/删除（墓碑 + 体检红字）；双击 = 建实体。
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "App/EditorApp.h"
#include "Assets/AssetDatabase.h"
#include "Core/Log.h"
#include "EditorContext.h"
#include "Interaction/ViewportRenderer.h"
#include "Panels/BuiltInPanels.h"
#include "Tooling/TestHooks.h"
#include "Tooling/Theme.h"
#include "imgui.h"
#include "imgui_internal.h" // 诊断临时：HoveredWindow
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

    // 面包屑（M4.7d）：Assets/ 逐级可点直达，替代目录下拉——深层目录不用在
    // 全量列表里翻；末段 = 当前目录（灰显不可点）。子目录由网格内文件夹单元格
    // 进入（下方；面包屑只负责向上，v1 曾只有面包屑 → 子目录无法进入）。
    // 语义约定：currentDir_ = ""（根）或 "Assets/相对路径"——与 Directories()/
    // EntriesInDir 的前缀同源（扫描根 = 项目根，v1 曾写 "sub" 风格 → 进子目录
    // 列表恒空，smoke-ui 真人链路抓到）。
    ImGui::TextLink("Assets");
    testhooks::Stash("crumbAssets", ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) currentDir_.clear();
    {
        std::string rest = currentDir_;
        if (rest.compare(0, 7, "Assets/") == 0) rest = rest.substr(7);
        else if (rest == "Assets") rest.clear();
        std::string rel; // 相对 Assets/ 的已消费前缀
        std::string target; // currentDir_ 语义目标（"Assets/..."）
        while (!rest.empty()) {
            const size_t pos = rest.find('/');
            const std::string seg = pos == std::string::npos ? rest : rest.substr(0, pos);
            rest = pos == std::string::npos ? "" : rest.substr(pos + 1);
            if (seg.empty()) continue; // 连续/尾随斜杠防御
            rel = rel.empty() ? seg : rel + "/" + seg;
            target = "Assets/" + rel;
            ImGui::SameLine();
            ImGui::TextDisabled("/");
            ImGui::SameLine();
            if (rest.empty()) { // 末段：当前位置
                ImGui::TextUnformatted(seg.c_str());
                break;
            }
            ImGui::PushID(rel.c_str());
            if (ImGui::TextLink(seg.c_str())) currentDir_ = target;
            testhooks::Stash(("crumbAssets." + rel).c_str(), ImGui::GetItemRectMin(),
                             ImGui::GetItemRectMax());
            ImGui::PopID();
        }
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

    // 子目录文件夹单元格（M4.7d 补全：面包屑只向上，向下进子目录靠这里——
    // 旧目录下拉删除后曾出现"子目录进不去"的导航缺口，smoke-ui 真人链路抓到）。
    // 单击进入；currentDir_ 语义 = "Assets/相对路径"（同 Directories()/面包屑）。
    {
        const std::string prefix = currentDir_.empty() ? "Assets/" : currentDir_ + "/";
        for (const std::string& d : db.Directories()) {
            if (d.size() <= prefix.size() || d.compare(0, prefix.size(), prefix) != 0) continue;
            const std::string rest = d.substr(prefix.size());
            if (rest.find('/') != std::string::npos) continue; // 只列直接子目录
            if (!filter_.empty() && rest.find(filter_) == std::string::npos) continue;
            if (col++ > 0) ImGui::SameLine();
            ImGui::PushID(d.c_str());
            ImGui::BeginGroup();
            void* tex = app.Viewport().IconTex();
            float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
            if (tex) app.Viewport().Assets().IconUV(IconKind::AssetGeneric, u0, v0, u1, v1);
            const ImVec2 size(72, 72);
            if (tex)
                ImGui::ImageButton("##dir", tex, size, ImVec2(u0, v0), ImVec2(u1, v1),
                                   theme::kBgMid, theme::kAccent);
            else
                ImGui::Button("/", size);
            const bool iconHover = ImGui::IsItemHovered();
            if (iconHover) ImGui::SetTooltip("文件夹（单击进入）\n%s", d.c_str());
            ImGui::TextWrapped("%.12s%s", rest.c_str(), rest.size() > 12 ? "…" : "");
            const bool labelHover = ImGui::IsItemHovered();
            testhooks::Stash(("assets.folder." + rest).c_str(), ImGui::GetItemRectMin(),
                             ImGui::GetItemRectMax());
            // 图标或名字任一处单击进入（v1 只挂图标 → 点名字无反应，smoke-ui 抓到）
            if (std::getenv("LEMON_SMOKE_UI_DEBUG") && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                ImVec2 rmin = ImGui::GetItemRectMin(), rmax = ImGui::GetItemRectMax();
                ImGuiContext* gc = ImGui::GetCurrentContext();
                std::printf("[smoke-ui dbg] folder='%s' iconH=%d labelH=%d rect=(%.0f,%.0f)-"
                            "(%.0f,%.0f) mouse=(%.0f,%.0f) hoveredWin='%s' curWin='%s'\n",
                            rest.c_str(), iconHover, labelHover, rmin.x, rmin.y, rmax.x, rmax.y,
                            ImGui::GetIO().MousePos.x, ImGui::GetIO().MousePos.y,
                            gc->HoveredWindow ? gc->HoveredWindow->Name : "(null)",
                            ImGui::GetCurrentWindow()->Name);
            }
            if ((iconHover || labelHover) && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                currentDir_ = d; // 单击进入（面包屑返回）
            ImGui::EndGroup();
            ImGui::PopID();
            if (col >= cols) col = 0;
        }
    }

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
    // 缩略图 / 类型图标：sprite = 纹理页缩略；其余 = 形状页类型图标（M4.7b，
    // 替代旧调色板色块——prefab 蓝调 / script 墨绿 / generic 次级灰，ImageButton tint 染色）
    const ImVec2 size(72, 72);
    void* tex = isSprite ? app.AssetGpu().Thumbnail(e.guid) : app.Viewport().IconTex();
    ImVec2 uv0(0, 0), uv1(1, 1);
    ImVec4 tint(1, 1, 1, 1);
    if (!isSprite && tex) {
        float u0, v0, u1, v1;
        const IconKind k = e.type == AssetType::Prefab ? IconKind::AssetPrefab
                          : e.type == AssetType::Script ? IconKind::AssetScript
                                                        : IconKind::AssetGeneric;
        app.Viewport().Assets().IconUV(k, u0, v0, u1, v1);
        uv0 = ImVec2(u0, v0);
        uv1 = ImVec2(u1, v1);
        tint = e.type == AssetType::Prefab ? theme::kAccent
               : e.type == AssetType::Script ? theme::kTextOk
                                             : theme::kTextDim;
    }
    if (tex) ImGui::ImageButton("##thumb", tex, size, uv0, uv1, theme::kBgMid, tint);
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
