// Lemon 编辑器 — AssetBrowser 面板（M4.md §2.2/§5 M4.4）
// 数据源：AssetDatabase（GUID/.meta/manifest）+ AssetGpuCache 缩略图。
// 交互：目录树 + 缩略图网格；拖拽 sprite 进 SceneView = 建实体、进 Inspector
// sprite 槽 = 设引用；拖 prefab 进 SceneView/Hierarchy = 实例化；右键导入/
// 重命名（guid 随 .meta 走 → 引用不断）/删除（墓碑 + 体检红字）；双击 = 建实体。
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "App/EditorApp.h"
#include "Assets/AssetDatabase.h"
#include "Assets/Csv.h"
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
constexpr float kThumb = 72.0f; // 缩略图边长（网格节距按主题实测，见 OnGui）

uint8_t KindOf(AssetType t) {
    switch (t) {
        case AssetType::Sprite: return 0;
        case AssetType::Prefab: return 1;
        case AssetType::Script: return 2;
        case AssetType::Clip: return 4;  // M5 批③（3 保留 generic）
        case AssetType::Table: return 5; // M6a 批②（暂无拖拽消费者）
        default: return 3;
    }
}

// C3 同类：过滤忽略大小写（与 Hierarchy 搜索同语义）
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

void AssetBrowserPanel::OnGui(EditorApp& app) {
    bool winOpen = true;
    if (!ImGui::Begin(Name(), &winOpen, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        if (!winOpen) app.ClosePanel(Name()); // × 关闭（T3b-8）
        return;
    }
    if (!winOpen) app.ClosePanel(Name());
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

    // 类型过滤（T3b-9）："找资产靠过滤/搜索，不靠目录纪律"（约定不强制配套）
    {
        static const char* kLabels[6] = {"全部", "图", "动画", "Prefab", "表", "脚本"};
        for (int i = 0; i < 6; ++i) {
            if (i) ImGui::SameLine();
            if (typeFilter_ == i) ImGui::PushStyleColor(ImGuiCol_Button, theme::kAccentDim);
            if (ImGui::SmallButton(kLabels[i])) typeFilter_ = i;
            if (typeFilter_ == i) ImGui::PopStyleColor();
        }
    }

    // .tab 表格区（M6a 批② T1）：选中 Table 条目时网格下方长出。空间预留用
    // 上一帧的 tableOpen_（头部本帧才提交，取简——首帧/开合切换一帧跳变可接受）。
    // T1 反馈批：预留按面板高比例化（最小 12 行）——内嵌区是快速预览，大编辑面
    // 走「放大编辑」浮动窗。
    const AssetEntry* tableSel = db.FindByGuid(selectedGuid_);
    const bool showTable =
        tableSel && !tableSel->missing && tableSel->type == AssetType::Table && tableOpen_;
    float reserve = 0.0f;
    if (showTable) {
        const float panelH = ImGui::GetContentRegionAvail().y;
        reserve = std::clamp(panelH * 0.42f,
                             ImGui::GetTextLineHeightWithSpacing() * 12.0f + 60.0f, 520.0f);
    }

    ImGui::BeginChild("grid", ImVec2(0, -reserve), ImGuiChildFlags_None,
                      ImGuiWindowFlags_AlwaysVerticalScrollbar);
    const float avail = ImGui::GetContentRegionAvail().x;
    // 列距按主题实测而非固定值：主题度量随 displayScale 缩放（FramePadding.x =
    // ItemSpacing.x = 8k → 节距 72+24k，k≥1 时超旧硬码 92）→ 旧公式列数偏多，
    // 每行末列溢出右缘被裁——子窗口只保留竖向滚动条，溢出部分无横向滚动可达
    // （用户实测"素材一多显示不完整"）。配套 DrawItem/文件夹名 PushTextWrapPos
    // 钉宽 → 单元格宽确定，本公式精确成立。
    const ImGuiStyle& st = ImGui::GetStyle();
    const float cellW = kThumb + st.FramePadding.x * 2.0f;
    const int cols =
        std::max(1, (int)((avail + st.ItemSpacing.x) / (cellW + st.ItemSpacing.x)));
    int col = 0;

    // 子目录文件夹单元格（M4.7d 补全：面包屑只向上，向下进子目录靠这里——
    // 旧目录下拉删除后曾出现"子目录进不去"的导航缺口，smoke-ui 真人链路抓到）。
    // 单击进入；currentDir_ 语义 = "Assets/相对路径"（同 Directories()/面包屑）。
    // M4.7 手测修复：currentDir_=""（根）= Assets/ 本身——此前 EntriesInDir("") 列
    // 的是项目根散文件（浏览器里只剩 project.lemon，导入进 Assets/ 的素材因
    // "Assets" 这层没有文件夹单元格而不可见）；根级目录（06 §1 Prefabs/）也补
    // 单元格——Prefab 化落点此前在浏览器任何层都进不去。
    {
        const std::string prefix = currentDir_.empty() ? "Assets/" : currentDir_ + "/";
        for (const std::string& d : db.Directories()) {
            bool direct =
                d.size() > prefix.size() && d.compare(0, prefix.size(), prefix) == 0 &&
                d.find('/', prefix.size()) == std::string::npos; // 当前目录直下子目录
            if (!direct && currentDir_.empty() && d != "Assets" &&
                d.find('/') == std::string::npos)
                direct = true; // 根级目录（Assets 之外：Prefabs/ 等）
            if (!direct) continue;
            const bool outsideAssets =
                currentDir_.empty() && d.compare(0, 6, "Assets") != 0;
            const std::string rest = outsideAssets ? d : d.substr(prefix.size());
            if (!filter_.empty() && !StrIStr(rest.c_str(), filter_.c_str())) continue;
            if (col++ > 0) ImGui::SameLine();
            ImGui::PushID(d.c_str());
            ImGui::BeginGroup();
            void* tex = app.Viewport().IconTex();
            float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
            if (tex) app.Viewport().Assets().IconUV(IconKind::AssetGeneric, u0, v0, u1, v1);
            const ImVec2 size(kThumb, kThumb);
            if (tex)
                ImGui::ImageButton("##dir", tex, size, ImVec2(u0, v0), ImVec2(u1, v1),
                                   theme::kBgMid, theme::kAccent);
            else
                ImGui::Button("/", size);
            const bool iconHover = ImGui::IsItemHovered();
            if (iconHover) ImGui::SetTooltip("文件夹（单击进入）\n%s", d.c_str());
            // 右键：从此文件夹创建动画（T3b-5 通道 A——多单图文件夹一键建 clip）
            if (ImGui::BeginPopupContextItem("folder_ctx")) {
                if (ImGui::MenuItem("从此文件夹创建动画…"))
                    app.OpenAnimationCreateFromFolder(d);
                ImGui::EndPopup();
            }
            char dirName[16];
            std::snprintf(dirName, sizeof(dirName), "%.12s%s", rest.c_str(),
                          rest.size() > 12 ? "…" : "");
            // 名字钉到按钮宽（TextWrapped 相对窗口右缘 → 行中单元格宽随位置漂移）
            ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + cellW);
            ImGui::TextUnformatted(dirName);
            ImGui::PopTextWrapPos();
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

    // 条目网格：根（""）= Assets/ 直下；子目录 = currentDir_ 直下（同上面包屑语义）
    const std::string entryDir = currentDir_.empty() ? "Assets" : currentDir_;
    const auto passType = [this](AssetType t) { // 类型过滤（T3b-9）
        switch (typeFilter_) {
            case 1: return t == AssetType::Sprite;
            case 2: return t == AssetType::Clip;
            case 3: return t == AssetType::Prefab;
            case 4: return t == AssetType::Table;
            case 5: return t == AssetType::Script;
            default: return true;
        }
    };
    for (const AssetEntry* e : db.EntriesInDir(entryDir)) {
        if (!passType(e->type)) continue;
        if (!filter_.empty() && !StrIStr(e->FileName().c_str(), filter_.c_str())) continue;
        if (col++ > 0) ImGui::SameLine();
        DrawItem(app, *e);
        if (col >= cols) col = 0;
    }

    // 空区右键：导入。NoOpenOverItems 同 HierarchyPanel（右键资产条目时禁开本
    // 菜单——与 asset_ctx 同帧双触发会被本菜单盖掉，条目右键永远只见"导入"）
    if (ImGui::BeginPopupContextWindow("assets_bg", ImGuiPopupFlags_NoOpenOverItems)) {
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

    // 内嵌表格区（选中 .tab 时；写在 EndChild 之后 = 面板底部独立滚动区）
    if (tableSel && !tableSel->missing && tableSel->type == AssetType::Table)
        DrawTableArea(app, *tableSel);

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

    // 浮动表格编辑器（主面板窗口作用域之外；按需工具窗）
    if (tableWinOpen_) DrawTableEditorWindow(app);
}

void AssetBrowserPanel::DrawItem(EditorApp& app, const AssetEntry& e) {
    EditorContext& ctx = app.Ctx();
    const bool isSprite = e.type == AssetType::Sprite;

    ImGui::PushID((int)e.guid);
    ImGui::BeginGroup();
    // 缩略图 / 类型图标：sprite = 纹理页缩略；其余 = 形状页类型图标（M4.7b，
    // 替代旧调色板色块——prefab 蓝调 / script 墨绿 / generic 次级灰，ImageButton tint 染色）
    const ImVec2 size(kThumb, kThumb);
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
               : e.type == AssetType::Table ? theme::kTextWarn // 琥珀 = 数据表（M6a 批②）
                                            : theme::kTextDim;
    }
    const bool selected = e.guid == selectedGuid_; // 单击选中（M6a 批②：表格区锚点）
    if (tex)
        ImGui::ImageButton("##thumb", tex, size, uv0, uv1,
                           selected ? theme::kAccentDim : theme::kBgMid, tint);
    else ImGui::Button("?", size);
    const bool hovered = ImGui::IsItemHovered();
    const bool doubleClicked = hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) selectedGuid_ = e.guid;

    // 右键菜单绑缩略图（72×72 大目标）：绑在下方 12 字符文件名上时几乎点不中——
    // NoOpenOverItems 修复后右键缩略图不再弹空白菜单，成了"无菜单"（G4–G6 手测）
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

    // 悬浮提示：类型 / guid / 导入状态
    if (hovered) {
        uint32_t w = 0, h = 0;
        const bool imported = app.AssetGpu().PageInfo(e.guid, w, h);
        std::string dims = isSprite && imported
                               ? "已导入 " + std::to_string(w) + "x" + std::to_string(h)
                               : (isSprite ? "未导入" : AssetTypeName(e.type));
        if (e.type == AssetType::Table) dims += "\n双击：放大编辑";
        if (e.type == AssetType::Clip)  dims += "\n双击：动画编辑"; // M6a 批② T3
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

    // 双击 .tab = 浮动放大编辑器（T1 反馈批：内嵌区可操作面不足）
    if (doubleClicked && e.type == AssetType::Table) {
        tableWinOpen_ = true;
        tableWinGuid_ = e.guid;
    }

    // 双击 .clip = Animation 面板（M6a 批② T3）——首个"资产 → 专用编辑面板"
    // 通道（EditorApp 汇聚：FindEntry 置 open + SetTarget）
    if (doubleClicked && e.type == AssetType::Clip) app.OpenAnimationEditor(e.guid);

    // 文件名（截断 12 字符；钉到按钮宽换行——单元格宽确定，网格列距公式才精确；
    // 右键菜单已上移绑缩略图）
    char shortName[16];
    std::snprintf(shortName, sizeof(shortName), "%.12s%s", e.FileName().c_str(),
                  e.FileName().size() > 12 ? "…" : "");
    const ImGuiStyle& st = ImGui::GetStyle();
    ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + kThumb + st.FramePadding.x * 2.0f);
    ImGui::TextUnformatted(shortName);
    ImGui::PopTextWrapPos();
    ImGui::EndGroup();
    ImGui::PopID();
}

void AssetBrowserPanel::RenderTableGrid(EditorApp& app, const AssetEntry& e) {
    // 内嵌区/浮动窗共用的表格渲染（M6a 批② T1）。缓存按 guid+hash 键（两处
    // 同时显示不同表时交替重读，小文件可接受）；写回 = WriteFileAtomic + 主动
    // Rescan（FileWatcher 防抖已修 F-15，同帧主动扫描去重）；EnterPlay 快照语义
    // ——改动下次 Enter Play 生效（提示行交代）。
    if (tableGuid_ != e.guid || tableHash_ != e.hash) {
        tableGuid_ = e.guid;
        tableHash_ = e.hash;
        editRow_ = editCol_ = -1;
        std::ifstream f(app.Ctx().Assets().AbsolutePath(e), std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        table_ = ParseTableJson(text);
    }
    if (!table_.ok) {
        ImGui::TextColored(theme::kTextError, "表解析失败：%s", table_.error.c_str());
        return;
    }

    const int rows = (int)table_.rows.size();
    const int cols = (int)table_.Cols();
    ImGui::TextDisabled("%s：%d 行 × %d 列（首行 = 列头）· 双击单元格编辑，Enter 提交 · "
                        "改动 Enter Play 后生效",
                        e.FileName().c_str(), rows, cols);
    if (app.Ctx().Playing())
        ImGui::TextDisabled("Play 进行中：当前局的表快照不变，改动下一局生效");
    if (!tableError_.empty()) {
        ImGui::TextColored(theme::kTextError, "%s", tableError_.c_str());
        tableError_.clear();
    }

    // ScrollX + SizingFixedFit：列宽 = 内容宽，超窗横向滚动（T1 反馈批——19 列
    // 表在内嵌区/窄窗横向不可达）；行号列 + ScrollFreeze(1,1) 双向钉住（宽表
    // 横滚时行号/列头恒可见）。
    // 反馈批②（2026-09-26 用户五点）：
    //   #3 行高——CellPadding.y 加厚（行随内容+padding 长高，整格天然可点）；
    //   #4 表头固定——外高恒占余量（-2px 防缘溢出），滚动恒归表格内部；表格
    //       一旦高过所在窗口，滚动升格为窗口级、冻结失效（列头随窗滚走的根因）；
    //   #1/#2/#5 Excel 语义——整格热区（Selectable SpanAvailWidth）双击进入编辑；
    //       Enter 或点别处（失焦）= 保存退出；Esc = 弃改。
    const ImGuiTableFlags flags =
        ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg |
        ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedFit;
    const float gridH = std::max(ImGui::GetContentRegionAvail().y - 2.0f,
                                 ImGui::GetTextLineHeightWithSpacing() * 2.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding,
                        ImVec2(6.0f, ImGui::GetStyle().CellPadding.y * 2.5f));
    if (!ImGui::BeginTable("cells", cols + 1, flags, ImVec2(0.0f, gridH))) {
        ImGui::PopStyleVar();
        return;
    }
    ImGui::TableSetupScrollFreeze(1, 1); // 行号列 + 列头行钉住
    ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed,
                            ImGui::GetTextLineHeight() * 2.4f);
    for (int c = 0; c < cols; ++c)
        ImGui::TableSetupColumn(table_.rows[0][c].c_str());
    ImGui::TableHeadersRow();

    // 编辑提交即写盘 + Rescan → entries_ 重建，本帧不得再触碰 e（选中条目下帧
    // 按 guid 重找）。committed 置位后跳出双循环。提交失败（超限/写盘）也退出
    // 编辑态：红字已交代原因，重双击再改（半激活无焦点的僵尸编辑态更绕）。
    bool committed = false;
    std::string* editCell = nullptr;
    if (editRow_ >= 1 && editRow_ < rows && editCol_ >= 0 && editCol_ < cols)
        editCell = &table_.rows[editRow_][editCol_];
    auto tryCommit = [&]() {
        const std::string prev = *editCell;
        *editCell = editBuf_;
        TableData chk = NormalizeTable(table_.rows); // 单格长度上限在此裁决
        if (!chk.ok) {
            *editCell = prev;
            tableError_ = "提交被拒：" + chk.error;
        } else {
            const std::filesystem::path p(app.Ctx().Assets().AbsolutePath(e));
            const std::string json = TableToJson(p.stem().string(), chk.rows);
            if (!json.empty() && WriteFileAtomic(p.string(), json + "\n")) {
                tableError_.clear();
                app.RescanAssets();
                tableGuid_ = 0; // 缓存失效：下帧按新 hash 重读
                committed = true;
            } else {
                *editCell = prev;
                tableError_ = "写盘失败（磁盘满/权限？）：" + p.string();
            }
        }
        editRow_ = editCol_ = -1;
    };
    for (int r = 1; r < rows && !committed; ++r) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0); // 行号列（数据行自 1 起）
        ImGui::TextDisabled("%d", r);
        for (int c = 0; c < cols && !committed; ++c) {
            ImGui::TableSetColumnIndex(c + 1);
            std::string& cell = table_.rows[r][c];
            ImGui::PushID(r * (int)kTableMaxCols + c);
            if (editRow_ == r && editCol_ == c) {
                if (editJustStarted_) { // 首帧抢焦点（IME 中文输入前提）
                    ImGui::SetKeyboardFocusHere();
                    editJustStarted_ = false;
                }
                const bool done = ImGui::InputText(
                    "##cell", &editBuf_,
                    ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
                if (done || ImGui::IsItemDeactivated())
                    tryCommit(); // Enter / 点别处失焦 = 保存（Excel 语义）
                else if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
                    editRow_ = editCol_ = -1; // Esc = 弃改
            } else {
                // 整格热区：文字项只盖文本宽，宽列空白处双击无响应（反馈 #1 根因）
                ImGui::Selectable(cell.c_str(), false, ImGuiSelectableFlags_SpanAvailWidth);
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    editRow_ = r;
                    editCol_ = c;
                    editBuf_ = cell;
                    editJustStarted_ = true;
                }
            }
            ImGui::PopID();
        }
    }
    ImGui::EndTable();
    ImGui::PopStyleVar();
}

void AssetBrowserPanel::DrawTableArea(EditorApp& app, const AssetEntry& e) {
    // .tab 内嵌表格区（M6a 批② T1 / ADR-012 D1）：快速预览 + 单格微调。可操作
    // 面不足时「放大编辑」/双击资产开浮动窗（不加新面板，05 §3 面板集冻结不破）。
    if (!ImGui::CollapsingHeader("表格视图", ImGuiTreeNodeFlags_DefaultOpen)) {
        tableOpen_ = false;
        return;
    }
    tableOpen_ = true;
    ImGui::SameLine();
    if (ImGui::SmallButton("放大编辑")) {
        tableWinOpen_ = true;
        tableWinGuid_ = e.guid;
    }
    testhooks::Stash("assets.table", ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    RenderTableGrid(app, e);
}

void AssetBrowserPanel::DrawTableEditorWindow(EditorApp& app) {
    // 浮动放大编辑器（T1 反馈批）：双击 .tab /「放大编辑」打开。按需工具窗
    //（不进面板注册表/DockBuilder）；NoSavedSettings = 不落 imgui.ini（冒烟
    // ini 漂移面不扩）。标题 ### 钉 ID——切换目标表窗口不重建。
    AssetDatabase& db = app.Ctx().Assets();
    const AssetEntry* e = db.FindByGuid(tableWinGuid_);
    if (!e || e->missing || e->type != AssetType::Table) {
        tableWinOpen_ = false; // 资产被删/重导入异常 → 自动关窗
        return;
    }
    ImGui::SetNextWindowSize(ImVec2(1040.0f, 560.0f), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing,
                            ImVec2(0.5f, 0.5f));
    char title[96];
    std::snprintf(title, sizeof(title), "%s — 表格编辑器###TableEditor",
                  e->FileName().c_str());
    if (!ImGui::Begin(title, &tableWinOpen_, ImGuiWindowFlags_NoSavedSettings |
                                               ImGuiWindowFlags_NoScrollbar)) {
        ImGui::End();
        return;
    }
    // Esc 关窗（无编辑进行时；编辑中的 Esc 归 InputText 弃改路径）
    if (editRow_ < 0 && ImGui::IsKeyPressed(ImGuiKey_Escape, false) &&
        ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
        tableWinOpen_ = false;
    testhooks::Stash("assets.tableWin", ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    RenderTableGrid(app, *e);
    ImGui::End();
}

} // namespace lemon::editor
