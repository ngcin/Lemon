// Lemon 编辑器 — EditorApp 游戏 UI 桥（ADR-014 RmlUi 资产通道：双击装载 /
// 贴图与文档解析器 / 项目字体 / Play 输入喂入 + IME 锚点 / 场景声明装载与对账）。
// 批④ 2026-09-30 机械外迁：前五函数自 EditorApp.cpp 尾部、MountSceneUiDocuments/
// ReconcileUiDocuments 自 EditorAppScripts.cpp（成员函数跨 TU 定义，类定义零改动，
// 代码逐行原样）。唯一等值变换：FeedGameUiInput 的 g_tplSmoke.pointerHold 直读改
// SmokeTplPointerHold() 访问器（g_tplSmoke 批④ 单 TU 化，见 EditorAppSmokeTpl.cpp）。
#include "App/EditorApp.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <sstream>
#include <vector>

#include <nlohmann/json.hpp>

#include "stb_image_write.h"
#include <SDL3/SDL.h>

#include "App/ImGuiBackend.h"
#include "Assets/AssetDatabase.h"
#include "Assets/ClipEdit.h" // M6a 批② T3：smoke-anim clip 编辑链（面板数据面同款）
#include "Assets/ControllerEdit.h" // T3d：smoke-anim graph 链（controller 数据面）
#include "Assets/ProjectWizard.h"
#include "Interaction/ViewportRenderer.h"
#include "Templates/VsTemplateGen.h"
#include "Tooling/Icons.h"
#include "Tooling/Theme.h"
#include "Tooling/ThumbCache.h"
#include "Components/BehaviorComponents.h"
#include "Components/CoreComponents.h"
#include "Components/GameplayComponents.h"
#include "Components/RenderComponents.h"
#include "Components/UiComponents.h"
#include <unistd.h> // getpid（bench-survivor tempdir）
#include "ECS/Hierarchy.h"
#include "Core/Log.h"
#include "EditorContext.h"
#include "Panels/BuiltInPanels.h"
#include "Platform/Window.h"
#include "Renderer/RHI.h"
#include "Ui/UiSubsystem.h" // 批③a（ADR-014）：游戏 UI 层（RmlUi）
#include "Scripting/ScriptHost.h"
#include "Serialization/SceneArchive.h"
#include "imgui.h"
#include "imgui_internal.h" // DockBuilder（docking 分支布局编程 API）+ FindWindowByName
#include "misc/cpp/imgui_stdlib.h" // InputText(std::string*) 重载（Layout 命名等）
#include "Tooling/TestHooks.h"
#include "App/RecentProjects.h"

namespace lemon::editor {

// 批③b（ADR-014）：双击 .rml → 装载到游戏 UI。文档名 = relPath（RescanAssets
// 热重载对账键）；Show 无条件置位（渲染层只在 Play 中被调用——非 Play 装载即
// 备好，进 Play 即显）。③c C# 装载通道落地前的手动通道。
void EditorApp::LoadUiDocument(uint64_t guid) {
    if (!gameUi_) {
        LEMON_WARN("UI 装载失败：游戏 UI 层不可用（初始化失败/字体缺失，见启动红字）");
        return;
    }
    const AssetEntry* e = ctx_.Assets().FindByGuid(guid);
    if (!e || e->missing || e->type != AssetType::Rml) return;
    const std::string abs = ctx_.Assets().AbsolutePath(*e);
    if (!gameUi_->LoadDocumentFromFile(e->relPath.c_str(), abs.c_str(),
                                       lemon::ui::UiDocOrigin::Edit)) return;
    gameUi_->ShowDocument(e->relPath.c_str(), true);
    LEMON_LOG("UI 文档已装载%s：%s（改动落盘经 watcher/重扫热重载）",
              ctx_.Playing() ? "" : "（进 Play 后 GameView 显示）", e->relPath.c_str());
}

// 批③c（M7/ADR-014 D2）：游戏 UI 输入喂入（Play 段、gameUi_->Update() 前每帧）——
//   鼠标：画布矩形内（ImGui 屏幕点 → 画布 RT 像素）逐帧 SetPointer + 点击边沿；
//   键盘：gameViewFocused_ 时 ImGui key 态差分（边沿）转发（RmlUi 焦点导航/文本编辑）；
//   IME 锚点：窗口点 = 画布原点 + caret×(画布点/RT 像素)——ActivateKeyboard 消费。
//   游戏侧让出门在 ApplyInput（uiHoldsInput）——本函数只喂 UI 不夺编辑器事件。
void EditorApp::FeedGameUiInput() {
    if (!gameUi_) return;
    bool inside = false;
    float px = 0, py = 0;
    if (gvCanvasValid_ && gvCanvasHovered_) {
        const ImVec2 mp = ImGui::GetMousePos();
        inside = mp.x >= gvCanvasX_ && mp.x <= gvCanvasX_ + gvCanvasW_ &&
                 mp.y >= gvCanvasY_ && mp.y <= gvCanvasY_ + gvCanvasH_;
        if (inside) {
            px = (mp.x - gvCanvasX_) * (float)gvRtW_ / gvCanvasW_;
            py = (mp.y - gvCanvasY_) * (float)gvRtH_ / gvCanvasH_;
        }
    }
    // 批③d-1：模板卡片直灌点击窗——指针保持（Update 建悬停、down/up 两帧间不被
    // 真实鼠标复位覆盖；证据块状态机置位，见 smoke-template 段注记）
    if (!SmokeTplPointerHold()) gameUi_->SetPointer((int)px, (int)py, inside);
    // review P2：画布无效（GameView 关闭/未上报）时 W/H 与 RT 均为残 0——0/0 = NaN
    // 会灌进 SDL_SetTextInputArea。守卫 + 恒等中性（锚点 = 光标本位）。
    if (gvCanvasValid_ && gvRtW_ > 0 && gvRtH_ > 0 && gvCanvasW_ > 0.0f && gvCanvasH_ > 0.0f)
        gameUi_->SetImeRectTransform(gvCanvasX_, gvCanvasY_, gvCanvasW_ / (float)gvRtW_,
                                     gvCanvasH_ / (float)gvRtH_);
    else
        gameUi_->SetImeRectTransform(0.0f, 0.0f, 1.0f, 1.0f);
    if (inside) {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) gameUi_->ProcessMouseButton(0, true);
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
            gameUi_->ProcessMouseButton(0, false);
    }
    if (gameViewFocused_) {
        // ImGuiKey → UiKey（波1 集合：字母/数字/方向/编辑键；差分出边沿）
        using UK = ::lemon::ui::UiKey;
        static const struct { int ik; UK uk; } kMap[] = {
            {ImGuiKey_A, UK::A}, {ImGuiKey_B, UK::B}, {ImGuiKey_C, UK::C},
            {ImGuiKey_D, UK::D}, {ImGuiKey_E, UK::E}, {ImGuiKey_F, UK::F},
            {ImGuiKey_G, UK::G}, {ImGuiKey_H, UK::H}, {ImGuiKey_I, UK::I},
            {ImGuiKey_J, UK::J}, {ImGuiKey_K, UK::K}, {ImGuiKey_L, UK::L},
            {ImGuiKey_M, UK::M}, {ImGuiKey_N, UK::N}, {ImGuiKey_O, UK::O},
            {ImGuiKey_P, UK::P}, {ImGuiKey_Q, UK::Q}, {ImGuiKey_R, UK::R},
            {ImGuiKey_S, UK::S}, {ImGuiKey_T, UK::T}, {ImGuiKey_U, UK::U},
            {ImGuiKey_V, UK::V}, {ImGuiKey_W, UK::W}, {ImGuiKey_X, UK::X},
            {ImGuiKey_Y, UK::Y}, {ImGuiKey_Z, UK::Z},
            {ImGuiKey_0, UK::Num0}, {ImGuiKey_1, UK::Num1}, {ImGuiKey_2, UK::Num2},
            {ImGuiKey_3, UK::Num3}, {ImGuiKey_4, UK::Num4}, {ImGuiKey_5, UK::Num5},
            {ImGuiKey_6, UK::Num6}, {ImGuiKey_7, UK::Num7}, {ImGuiKey_8, UK::Num8},
            {ImGuiKey_9, UK::Num9},
            {ImGuiKey_UpArrow, UK::Up}, {ImGuiKey_DownArrow, UK::Down},
            {ImGuiKey_LeftArrow, UK::Left}, {ImGuiKey_RightArrow, UK::Right},
            {ImGuiKey_Backspace, UK::Backspace}, {ImGuiKey_Enter, UK::Return},
            {ImGuiKey_Escape, UK::Escape}, {ImGuiKey_Space, UK::Space},
            {ImGuiKey_Home, UK::Home}, {ImGuiKey_End, UK::End},
            {ImGuiKey_Delete, UK::Delete}, {ImGuiKey_Tab, UK::Tab},
        };
        for (const auto& m : kMap) {
            const bool down = ImGui::IsKeyDown((ImGuiKey)m.ik);
            const int idx = (int)m.uk;
            if (down != gvKeyWasDown_[idx]) gameUi_->ProcessKey(m.uk, down);
            gvKeyWasDown_[idx] = down;
        }
    }
}

// 批③b 贴图桥解析器（RmlUi JoinPath 解析后的绝对路径 → 项目精灵资产 → 图集页）。
// 切片子图由 RmlUi 原生 <img rect="x y w h"> 表达，引擎零机制（M6 波2 扩 GUID/RT 源）
bool EditorApp::ResolveUiTexture(const std::string& source, rhi::Texture& tex, uint32_t& w,
                                 uint32_t& h) {
    namespace fs = std::filesystem;
    const AssetDatabase& db = ctx_.Assets();
    if (db.ProjectRoot().empty()) return false;
    // 批③c（M6 资产源）：GUID 直引协议（img data-field 值 16hex → "guid:<hex>"——
    // JoinPath 见 ':' 直通；图鉴/卡片图标通道，纸面验证 ⓪/① 形态）
    if (source.rfind("guid:", 0) == 0) {
        const AssetEntry* e = db.FindByGuid(AssetDatabase::HexToGuid(source.substr(5).c_str()));
        if (!e || e->missing || e->type != AssetType::Sprite) return false;
        renderer::AtlasRegistry& reg = viewport_->Assets().Registry();
        if (!reg.IsValidSprite(e->spriteId)) return false;
        const renderer::SpriteInfo& si = reg.GetSprite(e->spriteId);
        const rhi::Texture t = reg.AtlasTexture(si.atlasIndex, w, h);
        if (!t.IsValid()) return false;
        tex = t;
        return true;
    }
    std::error_code ec;
    const fs::path rel = fs::relative(fs::path(source), fs::path(db.ProjectRoot()), ec);
    if (ec) return false;
    const std::string relStr = rel.generic_string();
    if (relStr.empty() || relStr == "." || relStr.front() == '.') return false; // 越出项目根
    const AssetEntry* e = db.FindByPath(relStr);
    if (!e || e->missing || e->type != AssetType::Sprite) return false;
    renderer::AtlasRegistry& reg = viewport_->Assets().Registry();
    if (!reg.IsValidSprite(e->spriteId)) return false; // 未导入/空洞
    const renderer::SpriteInfo& si = reg.GetSprite(e->spriteId);
    const rhi::Texture t = reg.AtlasTexture(si.atlasIndex, w, h);
    if (!t.IsValid()) return false;
    tex = t;
    return true;
}

// 批③d 前置（通道 B）：文档解析器（C# UI.Show 的 relPath → 项目 .rml 资产绝对
// 路径）。未开项目/查无/非 Rml 类型/墓碑 = false → ApplyOps 维持响亮失败。
bool EditorApp::ResolveUiDocument(const std::string& relPath, std::string& absPath) {
    const AssetDatabase& db = ctx_.Assets();
    if (db.ProjectRoot().empty()) return false;
    const AssetEntry* e = db.FindByPath(relPath);
    if (!e || e->missing || e->type != AssetType::Rml) return false;
    absPath = db.AbsolutePath(*e);
    return true;
}

// 批③b：项目字体注册（Assets/ 下 otf/ttf/ttc → RmlUi fallback）。RmlUi 无
// UnloadFontFace——切项目旧族名共存（无害）；族名以字体文件为准（报告名传文件名）
void EditorApp::LoadProjectFonts() {
    if (!gameUi_) return;
    namespace fs = std::filesystem;
    uint32_t n = 0;
    for (const AssetEntry& e : ctx_.Assets().Entries()) {
        if (e.missing) continue;
        std::string ext = fs::path(e.relPath).extension().string();
        for (char& c : ext) c = (char)std::tolower((unsigned char)c);
        if (ext != ".otf" && ext != ".ttf" && ext != ".ttc") continue;
        if (gameUi_->LoadFontFace(ctx_.Assets().AbsolutePath(e).c_str(), e.FileName().c_str(),
                                  /*fallback=*/true))
            ++n;
    }
    if (n) LEMON_LOG("项目字体：%u 个已注册为 fallback（RCSS 按 font-family 命中）", n);
}

// 批③d 前置（通道 A）：见 EditorApp.h 注记。无 UIDocument 的场景（bench/replay/
// 基准场）装载调用恒 0——装载点只在此扫描处，不进任何通用路径（基准护栏 §5）。
uint32_t EditorApp::MountSceneUiDocuments() {
    if (!gameUi_) return 0;
    // 进 Play 对账（形态 C 治愈位）：文件装载文档的 relPath 非健康 .rml 资产 → 逐出。
    // "本屏不装载"是画面层不变量——事件被裸 Rescan 吞掉的残留在此清场
    ReconcileUiDocuments();
    AssetDatabase& db = ctx_.Assets();
    uint32_t loaded = 0, missing = 0;
    std::vector<std::string> declared;
    std::vector<uint64_t> seen; // 同 GUID 去重（一屏两实体无意义；文档量小线性足够）
    ctx_.ActiveScene().View<ecs::UIDocument>().each([&](auto raw, ecs::UIDocument& ud) {
        const ecs::Entity e = ecs::Scene::FromEntt(raw);
        if (ud.sourceAssetGuid == 0) return; // 未挂（合法；Inspector 提示）
        if (std::find(seen.begin(), seen.end(), ud.sourceAssetGuid) != seen.end()) {
            LEMON_WARN("UIDocument：实体 %llu 重复挂同一 .rml（guid %016llx）——"
                       "一屏两实体无意义，已去重装载",
                       (unsigned long long)e.id,
                       (unsigned long long)ud.sourceAssetGuid);
            return;
        }
        seen.push_back(ud.sourceAssetGuid);
        const AssetEntry* en = db.FindByGuid(ud.sourceAssetGuid);
        if (!en || en->missing || en->type != AssetType::Rml) {
            ++missing; // 响亮失败：绝不静默空屏（guid-chain 同款 per-entity resolve）
            LEMON_ERROR("UIDocument：资产缺失或非 .rml（guid %016llx，实体 %llu）——"
                        "本屏不装载；修复资产或重挂后重进 Play",
                        (unsigned long long)ud.sourceAssetGuid,
                        (unsigned long long)e.id);
            return;
        }
        if (gameUi_->LoadDocumentFromFile(en->relPath.c_str(),
                                          db.AbsolutePath(*en).c_str(),
                                          lemon::ui::UiDocOrigin::Scene)) {
            ++loaded;
            // 声明态归位：showOnStart=0 = 装载但隐藏（动态屏）；modal 初值入 Doc
            gameUi_->ShowDocument(en->relPath.c_str(), ud.showOnStart != 0,
                                  ud.modal != 0);
            declared.push_back(en->relPath);
        }
    });
    gameUi_->ResetDynamicDocuments(declared); // §3：未声明且 stale → Hide + 清 stale
    if (loaded || missing)
        LEMON_LOG("UIDocument 装载：%u 成功 / %u 缺失（声明态归位 + stale 清场）",
                  loaded, missing);
    return loaded;
}

// 状态对账（2026-09-29 根因收口）：见 EditorApp.h 注记。事件驱动路径保留（首拍即逐
// 出 + 「已卸载」观测日志），对账是兜住"事件被谁吃了"的不变量层——两者幂等共存
void EditorApp::ReconcileUiDocuments() {
    if (!gameUi_) return;
    const AssetDatabase& db = ctx_.Assets();
    for (const std::string& name : gameUi_->FileBackedDocumentNames()) {
        const AssetEntry* e = db.FindByPath(name);
        if (e && !e->missing && e->type == AssetType::Rml) continue;
        if (gameUi_->UnloadDocument(name.c_str()))
            LEMON_LOG("UI 文档对账逐出（资产不健康——事件可能被裸重扫吞噬）：%s",
                      name.c_str());
    }
}

} // namespace lemon::editor
