// Lemon 编辑器 — EditorApp 项目与 Play 管线（项目打开管线/Play 入口守卫与
// Stop 清场/新建项目与恢复模态；M4.5 §3.7 起）。
// 批② 2026-09-29 自 EditorApp.cpp 机械拆分：成员函数跨 TU 定义，类定义
// App/EditorApp.h 零改动，代码逐行原样。
// 批④ 2026-09-30：UIDocument 装载与对账两函数外迁 App/EditorAppUiBridge.cpp。
// 批④-2 2026-09-30：脚本编译/热重载链八函数外迁 App/EditorAppScriptReload.cpp。
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

// ------------------------------------------------ 项目管线（M4.5）----
bool EditorApp::OpenProjectPipeline(const std::string& projectRoot) {
    // project.lemon 存在性守卫（2026-09-22 测试报告 BUG-1）：此前 --project 对任意
    // 目录静默"收养"——建 Assets/Prefabs/manifest 半成品且零告警（打错的相对路径
    // 曾在仓库里落垃圾目录）。UI picker 路径本有校验；此守卫统一覆盖所有入口
    // （向导/最近菜单入口此刻 project.lemon 必在——新建即写、菜单侧已灰显校验）。
    {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(
                std::filesystem::path(projectRoot) / "project.lemon", ec)) {
            LEMON_ERROR("打开项目失败：%s 下没有 project.lemon（应选项目根目录）",
                        projectRoot.c_str());
            return false;
        }
    }
    // project.lemon 内容最小校验（测试报告 BUG-2）：内容当前无消费者（存在性 =
    // 项目标记），坏档静默无视会让用户误以为项目完好——json 可解析 + name 字段。
    // 坏 = 红字但不阻断（Assets/ 场景可能完好，重建工程文件由用户决定）。
    {
        std::ifstream pf(projectRoot + "/project.lemon", std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(pf)),
                         std::istreambuf_iterator<char>());
        bool plOk = false;
        try {
            const nlohmann::json j = nlohmann::json::parse(text);
            plOk = j.contains("name") && j.at("name").is_string();
        } catch (const std::exception&) {
        }
        if (!plOk)
            LEMON_ERROR("project.lemon 损坏或缺少 name 字段：%s——项目按目录继续打开，"
                        "建议重建工程文件", projectRoot.c_str());
    }
    // 会话内切换支持（M4.6）：Start 对已运行 watcher 是 no-op，必须先停旧根
    watcher_.Stop();
    scriptWatcher_.Stop();
    // [M5 批④后修②] 换项目 = 图集注册表复位到内置页。此前基号随"本会话先前
    // 打开过的项目"累计漂移（demo/svr-test 实测：作第二个项目打开 → 全体
    // spriteId 后移上个项目的精灵数 31 → 场景烘焙引用悬空、玩家/怪物全不渲染；
    // 自动重开上个项目的新建向导流是稳定触发路径）。与设备重建回调同配方：
    // Reset + Build 复原内置页（spriteId 1..N 恒定）→ 下方按 DB 记账号接续导入。
    // 首次打开 = 幂等重建（同号）；既有项目的 id 稳定性仍由 manifest 记账保证。
    viewport_->Assets().Registry().Reset();
    viewport_->Assets().Build(*device_);
    viewport_->RebindProceduralIcons();
    gpuAssets_.ClearPages();
    thumbcache::Clear(); // 换项目 = 旧路径缩略图全失效（T3-UX4；页纹理借项同清）
    // spriteId 基址 = 程序化图集登记后首个可用号（恒定；跨会话稳定由 manifest 记账）
    const uint32_t spriteIdBase = viewport_->Assets().Registry().SpriteCount() + 1;
    if (!ctx_.Assets().OpenProject(projectRoot, spriteIdBase)) return false;
    gpuAssets_.Init(*device_, ui_.get(), &viewport_->Assets().Registry(),
                    ctx_.Assets(), /*firstSlot=*/3); // 0=调色板 1=字体页 2=图标形状页(M4.7b)
    thumbcache::Init(device_.get(), ui_.get(), &ctx_.Assets(), &gpuAssets_); // T3-UX4 选择器缩略图
    if (gameUi_) { // 批③b UI 资产通道：切项目 = 旧文档全卸（旧贴图引用随旧图集死）
        gameUi_->UnloadAllDocuments();
        gameUi_->SetTextureResolver(
            [this](const std::string& s, rhi::Texture& t, uint32_t& w, uint32_t& h) {
                return ResolveUiTexture(s, t, w, h);
            });
        LoadProjectFonts();
    }
    {
        // 按 DB 记账号升序导入（与设备重建回调同约定）：bindless 槽位分配确定性，
        // 与文件系统扫描序无关（2026-09-21：扫描序曾致注册表号与记账交叉）
        std::vector<const AssetEntry*> imps;
        for (const AssetEntry& e : ctx_.Assets().Entries())
            if (!e.missing && e.type == AssetType::Sprite) imps.push_back(&e);
        std::sort(imps.begin(), imps.end(),
                  [](const AssetEntry* a, const AssetEntry* b) {
                      return a->spriteId < b->spriteId;
                  });
        for (const AssetEntry* e : imps) gpuAssets_.ImportSprite(*e);
    }
    // 设备丢失重建（"editor-viewport" 先 Reset+重建程序化页 → 此处按 DB 记账号接续）；
    // 只注册一次——会话内切项目重复注册会叠加回调（RebuildAll 被调两遍）
    if (!assetGpuCbRegistered_) {
        assetGpuCbRegistered_ = true;
        device_->AddRecreateCallback("asset-gpu", [this](rhi::Device& d) {
            gpuAssets_.RebuildAll(d);
        });
    }
    watcher_.Start(ctx_.Assets().AssetsRoot());
    scriptWatcher_.Start(ctx_.Assets().ProjectRoot() + "/Game"); // 热重载触发源（§3.7）
    // 源码基线化（M4.6）：开项目时已存在的 .cs 不算"变更"——否则 lastHandledCsWrite_
    // 从 0 起步，首次 watcher 事件（dotnet build 写 obj 触发）必引发一次无谓换装
    // （每次泄漏一个旧域；用户实测闪退链的第一环就是它）
    ScriptSourceChanged();
    LEMON_LOG("资产管线就绪：项目 %s", projectRoot.c_str());

    // 项目自带 Game/ 工程且未显式 --script → 编译 + 装配脚本宿主（向导零配置体验）
    std::string csproj, dll;
    if (launch_->script.empty() && FindGameProject(csproj, dll)) {
        double buildMs = 0.0;
        std::string buildOut;
        if (ProjectWizard::BuildGameProject(csproj, ctx_.Assets().ProjectRoot() + "/.lemon/bin",
                                            &buildMs, &buildOut) == 0) {
            InitScriptHostFrom(dll);
            LEMON_LOG("Game/ 编译 %.0fms → %s", buildMs, dll.c_str());
        } else {
            LEMON_ERROR("Game/ 编译失败（项目仍可编辑，无脚本）：dotnet build %s", csproj.c_str());
            LogCompileErrors(buildOut); // M4.6 §5-6：启动期编译错误同样红字可读
        }
    } else if (launch_->script.empty()) {
        // 新项目无 Game/：清旧宿主（顺序 = 先摘 ctx 再毁宿主，指针永不悬空），
        // 旧项目脚本类型不得跨项目残留
        ctx_.SetScriptHost(nullptr);
        host_.reset();
    }
    // 记最近项目用 DB 侧 root_（已绝对化）——入参可能是向导手敲的相对路径。
    // 注入/冒烟会话不记（2026-09-22 测试报告复验时发现：smoke-ui 不带 --smoke
    // 标志，回归曾把 ${TMP}/ui 推成首条；trap 删目录后成死条目并挤掉真实项目）
    const bool injectionSession = launch_->smoke || launch_->smokeUi || launch_->smokeDrag ||
                                  launch_->smokeUirml || launch_->finalTest ||
                                  !launch_->smokeClose.empty();
    if (!injectionSession)
        PushRecentProject(ctx_.Assets().ProjectRoot(), recentProjects_);
    ctx_.LoadRecentScenes(); // M4.8-b：项目内最近场景随项目装载
    return true;
}

bool EditorApp::PlayBlockedByScripts() {
    if (host_) return false;
    std::string csproj, dll;
    return FindGameProject(csproj, dll); // 带 Game/ 工程而无宿主 = 启动期编译/装配失败
}

bool EditorApp::TryEnterPlay() {
    if (PlayBlockedByScripts()) {
        playBlockedOpen_ = true;
        LEMON_WARN("已阻止进入 Play：Game/ 编译失败（脚本宿主未装配）。"
                   "错误见 Console 红字；修复保存后自动重编译装配");
        return false;
    }
    if (!ctx_.EnterPlay()) return false;
    MountSceneUiDocuments(); // 批③d 前置（通道 A）：场景声明装载 + EnterPlay 归位
    return true;
}

bool EditorApp::StopPlay() {
    if (!ctx_.ExitPlay()) return false;
    // 形态 D 清场（2026-09-29）：Unity「Stop = 运行时态归零」同构——Scene/CSharp
    // 来源文档 Hide（装载保留免 IO）；Edit 双击预览豁免（③b 跨 Play 保持）。
    // EnterPlay 侧 ResetDynamicDocuments 仍是兜底不变量（双防线，幂等共存）
    if (gameUi_) {
        const uint32_t hidden = gameUi_->HideNonEditDocuments();
        if (hidden)
            LEMON_LOG("退 Play UI 清场：隐藏 %u 个游戏文档（装载保留）", hidden);
    }
    tabFocusPending_ = -1;
    return true;
}

void EditorApp::MenuNewProject() { wizOpen_ = true; }

void EditorApp::DrawRecoveryModal() {
    if (recoveryPath_.empty()) return;
    if (!ImGui::IsPopupOpen("崩溃恢复") && !recoveryAnswered_) ImGui::OpenPopup("崩溃恢复");
    if (!ImGui::BeginPopupModal("崩溃恢复", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::Text("检测到较新的自动备份：\n%s", recoveryPath_.c_str());
    ImGui::TextUnformatted(
        "（上次会话可能未正常保存。恢复 = 打开备份内容并保持未保存状态；\n"
        "忽略 = 本次不处理，下次启动仍会提示；忽略并删除 = 丢弃备份，不再提示）");
    ImGui::Separator();
    if (ImGui::Button("恢复", ImVec2(120, 0))) {
        if (ctx_.OpenSceneRecovery(recoveryPath_))
            LEMON_LOG("崩溃恢复：已载入备份（Ctrl+S 落盘）");
        else
            LEMON_WARN("崩溃恢复失败：备份解析失败");
        recoveryPath_.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("忽略", ImVec2(120, 0))) { // 只关本会话弹窗，文件保留（热修④）
        recoveryPath_.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("忽略并删除", ImVec2(120, 0))) { // 丢弃备份——否则 untitled 永弹
        if (!ctx_.DiscardAutosave(recoveryPath_))
            LEMON_WARN("丢弃自动备份失败（文件已在/权限？）——下次启动可能仍会提示");
        recoveryPath_.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

} // namespace lemon::editor
