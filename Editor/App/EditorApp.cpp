// Lemon 编辑器 — 应用壳实现（M4-Editor-Plan §3.1；主循环承 anim-smoke 全链基线）
// M4.0：壳 + 默认布局 + DPI/字体 + smoke；M4.1：EditorContext/场景 IO/快捷键/关闭确认。
#include "App/EditorApp.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>

#include "stb_image_write.h"

#include "App/ImGuiBackend.h"
#include "Assets/AssetDatabase.h"
#include "Assets/ProjectWizard.h"
#include "Interaction/ViewportRenderer.h"
#include "Components/BehaviorComponents.h"
#include "Components/CoreComponents.h"
#include "Components/GameplayComponents.h"
#include "Components/RenderComponents.h"
#include "ECS/Hierarchy.h"
#include "Core/Log.h"
#include "EditorContext.h"
#include "Platform/Window.h"
#include "Renderer/RHI.h"
#include "Scripting/ScriptHost.h"
#include "imgui.h"
#include "imgui_internal.h" // DockBuilder（docking 分支布局编程 API）

namespace lemon::editor {

EditorApp::EditorApp() = default;
EditorApp::~EditorApp() = default;

namespace {
// C# native 资产钩子（M4.4 #8；进程一份——EditorApp 即进程单例）
EditorApp* g_app = nullptr;
uint32_t HookSpriteOf(const char* hex) {
    return g_app ? g_app->Ctx().SpriteIdOfGuidHex(hex) : 0;
}
uint64_t HookInstantiate(const char* hex, float x, float y) {
    if (!g_app) return 0;
    ecs::Entity e = g_app->Ctx().InstantiatePrefabAsset(AssetDatabase::HexToGuid(hex),
                                                        Vec2{x, y});
    return e.IsNull() ? 0 : e.id;
}
} // namespace

void EditorApp::SetupDefaultLayout() {
    // Unity 式默认布局（§2.1 线框）：左 Hierarchy 20% / 右 Inspector 25% /
    // 中央上 Scene|Game 标签页 / 中央下 Console|Assets 标签页（30% 高）
    ImGuiID dock = ImGui::GetID("LemonDockSpace");
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::DockBuilderRemoveNode(dock);
    ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dock, vp->WorkSize);
    ImGuiID mainId = dock, leftId = 0, rightId = 0, bottomId = 0;
    ImGui::DockBuilderSplitNode(mainId, ImGuiDir_Left, 0.20f, &leftId, &mainId);
    ImGui::DockBuilderSplitNode(mainId, ImGuiDir_Right, 0.25f, &rightId, &mainId);
    ImGui::DockBuilderSplitNode(mainId, ImGuiDir_Down, 0.30f, &bottomId, &mainId);
    ImGui::DockBuilderDockWindow("Hierarchy", leftId);
    ImGui::DockBuilderDockWindow("Inspector", rightId);
    ImGui::DockBuilderDockWindow("Scene", mainId);
    ImGui::DockBuilderDockWindow("Game", mainId);      // 同区域 = 标签页
    ImGui::DockBuilderDockWindow("Console", bottomId);
    ImGui::DockBuilderDockWindow("Assets", bottomId);  // 同区域 = 标签页
    ImGui::DockBuilderFinish(dock);
    LEMON_LOG("editor: default layout built");
}

void EditorApp::BuildMenuBar() {
    if (!ImGui::BeginMenuBar()) return;
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("新建场景", nullptr, false, !playing_)) MenuNewScene();
        if (ImGui::MenuItem("打开场景...", "Ctrl+O", false, !playing_)) MenuOpenScene();
        ImGui::Separator();
        char saveLabel[96];
        std::snprintf(saveLabel, sizeof(saveLabel), "保存场景 %s", ctx_.ScenePath().empty() ? "" : "(Ctrl+S)");
        if (ImGui::MenuItem(saveLabel, ctx_.ScenePath().empty() ? nullptr : "Ctrl+S", false,
                            !playing_))
            MenuSaveScene();
        if (ImGui::MenuItem("另存为...", nullptr, false, !playing_)) MenuSaveSceneAs();
        ImGui::Separator();
        if (ImGui::MenuItem("新建项目...", nullptr, false, !playing_)) MenuNewProject();
        if (ImGui::MenuItem("打开项目...", nullptr, false, !playing_)) {
            pickerMode_ = PickerMode::Import; // 复用目录浏览起点（选 .lemon 上级）
            LEMON_LOG("打开项目：用 --project <dir> 启动，或新建项目向导（M4 单项目会话）");
        }
        ImGui::Separator();
        if (ImGui::MenuItem("退出", nullptr, false, true)) RequestExit();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
        ImGui::MenuItem("Undo", "Ctrl+Z", false, false); // M4.2 属性轨
        ImGui::MenuItem("Redo", "Ctrl+Y", false, false);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Assets")) {
        if (ImGui::MenuItem("导入文件...", nullptr, false, true)) MenuImportAsset();
        if (ImGui::MenuItem("重扫资产库", nullptr, false, true)) RescanAssets();
        ImGui::Separator();
        if (ImGui::MenuItem("重新编译脚本（热重载）", nullptr, false, host_ != nullptr))
            MenuRebuildScripts();
        ImGui::Separator();
        ImGui::TextDisabled("项目：%s", ctx_.Assets().ProjectRoot().c_str());
        ImGui::TextDisabled("资产 %u（sprite %u）｜体检红字 %u",
                            (uint32_t)ctx_.Assets().Entries().size(),
                            ctx_.Assets().SpriteAssetCount(), ctx_.Assets().HealthIssues());
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("GameObject")) {
        if (ImGui::MenuItem("创建空实体")) ctx_.Select(ctx_.CreateEntity("Empty"), false);
        if (ImGui::MenuItem("创建精灵")) ctx_.Select(ctx_.CreateSpriteEntity("Sprite"), false);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Window")) {
        for (auto& e : panels_.Entries()) ImGui::MenuItem(e.panel->Name(), nullptr, &e.open);
        ImGui::Separator();
        ImGui::MenuItem("Dear ImGui Demo", nullptr, &launchCopy_.demoWindow);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")) {
        ImGui::MenuItem("About", nullptr, &aboutOpen_);
        ImGui::EndMenu();
    }
    ImGui::EndMenuBar();
}

void EditorApp::BuildToolbar() {
    const bool playing = ctx_.Playing();
    ImGui::PushStyleColor(ImGuiCol_Button,
                          playing ? ImVec4(0.50f, 0.32f, 0.06f, 1.0f) : ImVec4(0.22f, 0.26f, 0.20f, 1.0f));
    if (ImGui::Button(playing ? "Stop" : "Play")) {
        if (playing) {
            if (!ctx_.ExitPlay()) LEMON_WARN("Stop 后快照校验失败（编辑场景已按快照重建）");
        } else {
            ctx_.EnterPlay();
        }
    }
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::BeginDisabled(!playing);
    if (ImGui::Button(paused_ ? "Resume" : "Pause")) paused_ = !paused_;
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!playing);
    if (ImGui::Button("单步")) singleStep_ = true;
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    struct ToolBtn { const char* label; const char* tip; EditTool tool; };
    static const ToolBtn kTools[] = {{"Move (W)", "移动工具", EditTool::Move},
                                     {"Rotate (E)", "旋转工具", EditTool::Rotate},
                                     {"Scale (R)", "四角缩放工具", EditTool::Scale}};
    for (const auto& t : kTools) {
        if (t.tool == tool_) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.42f, 0.38f, 0.10f, 1.0f));
        if (ImGui::Button(t.label)) tool_ = t.tool;
        if (t.tool == tool_) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", t.tip);
        ImGui::SameLine();
    }
    ImGui::Checkbox("Grid Snap", &gridSnap_); // 生效于 M4.2 Gizmo
}

void EditorApp::BuildStatusBar() {
    int pw = 0, ph = 0;
    window_->GetPixelSize(pw, ph);
    ImGui::Text("%s%s", ctx_.SceneName().c_str(), ctx_.dirty ? " ●" : "");
    ImGui::SameLine();
    ImGui::TextDisabled("| DPI %.1fx | %dx%d px | %.0f fps | 选中 %zu | 资产 %u | 中文渲染正常",
                        ui_->DisplayScale(), pw, ph, ImGui::GetIO().Framerate,
                        ctx_.Selection().size(), ctx_.Assets().SpriteAssetCount());
    if (playing_) {
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.65f, 0.15f, 1.0f));
        ImGui::TextUnformatted("| \xe2\x96\xb6 PLAY"); // ▶
        ImGui::PopStyleColor();
    }
}

void EditorApp::BuildShortcuts() {
    // §2.3 键位：输入框聚焦（WantTextInput）时全部屏蔽（IME 冒烟检查项）
    if (ImGui::GetIO().WantTextInput) return;
    if (!picker_.IsOpen()) {
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S) && !playing_) MenuSaveScene();
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_O) && !playing_) MenuOpenScene();
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_D)) {
            ecs::Entity e = ctx_.Primary();
            if (!e.IsNull()) {
                ecs::Entity copy = ctx_.DuplicateEntity(e);
                if (!copy.IsNull()) ctx_.Select(copy, false);
            }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
            for (ecs::Entity e : ctx_.Selection()) ctx_.DestroyEntityTree(e);
            ctx_.ClearSelection();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_W)) tool_ = EditTool::Move;
        if (ImGui::IsKeyPressed(ImGuiKey_E)) tool_ = EditTool::Rotate;
        if (ImGui::IsKeyPressed(ImGuiKey_R)) tool_ = EditTool::Scale;
        if (!ctx_.Playing() && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Z)) {
            if (!ctx_.Undo().Undo()) LEMON_LOG("Undo：栈空");
        }
        if (!ctx_.Playing() && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Y))
            ctx_.Undo().Redo();
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_P)) {
            if (ctx_.Playing()) ctx_.ExitPlay();
            else ctx_.EnterPlay();
        }
    }
}

void EditorApp::BuildUI() {
    BuildShortcuts();

    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##LemonEditor", nullptr,
                 ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                     ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_MenuBar);
    ImGui::PopStyleVar(3);

    BuildMenuBar();

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 4));
    if (ImGui::BeginChild("##Toolbar",
                          ImVec2(0.0f, ImGui::GetFrameHeightWithSpacing() + 4.0f))) {
        BuildToolbar();
        if (playing_) { // Play 橙色横幅（§2.4；沙盒 M4.3 生效）
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.60f, 0.10f, 1.0f));
            ImGui::TextUnformatted("PLAY MODE — 编辑落 Play World，Stop 即丢；GameView 聚焦时键鼠进游戏");
            ImGui::PopStyleColor();
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();

    const float statusBarH = ImGui::GetFrameHeightWithSpacing();
    ImGuiID dock = ImGui::GetID("LemonDockSpace");
    if (ImGui::DockBuilderGetNode(dock) == nullptr) SetupDefaultLayout();
    ImGui::DockSpace(dock, ImVec2(0.0f, ImGui::GetContentRegionAvail().y - statusBarH),
                     ImGuiDockNodeFlags_None);

    if (ImGui::BeginChild("##StatusBar", ImVec2(0.0f, statusBarH))) BuildStatusBar();
    ImGui::EndChild();
    ImGui::End();

    for (auto& e : panels_.Entries())
        if (e.open) e.panel->OnGui(*this);

    BuildPickersAndModals();

    if (launchCopy_.demoWindow) ImGui::ShowDemoWindow(&launchCopy_.demoWindow);
    if (aboutOpen_) {
        if (ImGui::Begin("About Lemon Editor", &aboutOpen_, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Lemon Editor — M4（ImGui %s / docking）", IMGUI_VERSION);
            ImGui::TextUnformatted("纯 2D 高性能游戏引擎：C++20 + Vulkan + C# 脚本");
            ImGui::TextUnformatted("规划：docs/EngineDesign/M4-Editor-Plan.md");
        }
        ImGui::End();
    }
}

void EditorApp::BuildPickersAndModals() {
    // 文件选择器（打开/另存/导入共用；动作一次性返回）
    if (PickerResult r = picker_.Draw(); r.action != PickerAction::None) {
        if (r.action == PickerAction::Open || r.action == PickerAction::Save) {
            if (pickerMode_ == PickerMode::Open) {
                if (!ctx_.OpenScene(r.path)) LEMON_WARN("打开失败：%s", r.path.c_str());
            } else if (pickerMode_ == PickerMode::Save) {
                if (ctx_.SaveScene(r.path)) LEMON_LOG("已另存为：%s", r.path.c_str());
            } else { // Import：复制进 Assets/ 根 + 登记导入（M4.4）
                std::filesystem::path src(r.path);
                if (const AssetEntry* e = ctx_.Assets().ImportFile(
                        r.path, src.filename().string())) {
                    if (e->type == AssetType::Sprite) gpuAssets_.ImportSprite(*e);
                    LEMON_LOG("已导入：%s（guid %016llx）", e->relPath.c_str(),
                              (unsigned long long)e->guid);
                }
            }
        }
    }

    // 退出确认（dirty 场景）：保存 / 丢弃 / 取消
    if (quitConfirmOpen_) {
        ImGui::OpenPopup("未保存更改");
        quitConfirmOpen_ = false;
        quitConfirmArmed_ = true;
    }    if (ImGui::BeginPopupModal("未保存更改", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("场景 %s 有未保存更改。", ctx_.SceneName().c_str());
        ImGui::Separator();
        if (ImGui::Button("保存并退出", ImVec2(140, 0))) {
            if (ctx_.ScenePath().empty()) {
                // 无路径：走另存为；完成后再退
                ImGui::CloseCurrentPopup();
                MenuSaveSceneAs();
                quitConfirmArmed_ = false;
                exitRequested_ = false; // 等另存完成由用户再关（简化环）
            } else {
                ctx_.SaveScene();
                ImGui::CloseCurrentPopup();
                quitConfirmArmed_ = false;
                forceExit_ = true;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("丢弃并退出", ImVec2(140, 0))) {
            ImGui::CloseCurrentPopup();
            quitConfirmArmed_ = false;
            forceExit_ = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("取消", ImVec2(140, 0))) {
            ImGui::CloseCurrentPopup();
            quitConfirmArmed_ = false;
            exitRequested_ = false;
        }
        ImGui::EndPopup();
    }

    // M4.5 新建项目向导（blank 模板；06 §1 布局 + 零配置 Game/ 编译装配）
    if (wizOpen_) ImGui::OpenPopup("新建项目");
    if (ImGui::BeginPopupModal("新建项目", &wizOpen_, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("blank 模板：Assets/Scenes/Prefabs/Game/Data + 种子资产 + "
                               "可编译脚本工程（零配置直接 Play）");
        ImGui::SetNextItemWidth(320);
        ImGui::InputText("项目名", wizName_, sizeof(wizName_));
        ImGui::SetNextItemWidth(320);
        ImGui::InputText("父目录（绝对路径）", wizParent_, sizeof(wizParent_));
        ImGui::Separator();
        ImGui::BeginDisabled(!wizName_[0] || !wizParent_[0]);
        if (ImGui::Button("创建并打开", ImVec2(160, 0))) {
            ProjectDesc d;
            d.parentDir = wizParent_;
            d.name = wizName_;
#ifdef LEMON_SCRIPT_DIR
            d.sdkDir = LEMON_SCRIPT_DIR;
            d.engineVersion = "0.4.0-m4";
            if (const std::string root = ProjectWizard::Create(d); !root.empty()) {
                ImGui::CloseCurrentPopup();
                wizOpen_ = false;
                if (OpenProjectPipeline(root)) {
                    ctx_.OpenScene(root + "/Scenes/Main.scene");
                    LEMON_LOG("新项目已打开：%s（保存场景后即可 Play）", root.c_str());
                }
            }
#else
            LEMON_WARN("新建项目需要 LEMON_BUILD_SCRIPTING=ON 构建");
#endif
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("取消", ImVec2(120, 0))) {
            wizOpen_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    DrawRecoveryModal();
}

// ---- 场景 IO 动作 ----
void EditorApp::MenuNewScene() {
    if (ctx_.dirty && !ConfirmUnsaved()) return;
    ctx_.NewScene();
    LEMON_LOG("新建场景（untitled）");
}

void EditorApp::MenuOpenScene() {
    if (ctx_.dirty && !ConfirmUnsaved()) return;
    std::error_code ec;
    std::string dir = std::filesystem::path(ctx_.ScenePath()).parent_path().string();
    if (dir.empty()) dir = std::filesystem::current_path(ec).string();
    pickerMode_ = PickerMode::Open;
    picker_.Open("打开场景", dir, "", ".scene");
}

void EditorApp::MenuSaveScene() {
    if (ctx_.ScenePath().empty()) {
        MenuSaveSceneAs();
        return;
    }
    ctx_.SaveScene();
}

void EditorApp::MenuSaveSceneAs() {
    std::error_code ec;
    std::string dir = std::filesystem::path(ctx_.ScenePath()).parent_path().string();
    if (dir.empty()) dir = std::filesystem::current_path(ec).string();
    pickerMode_ = PickerMode::Save;
    picker_.Open("另存场景", dir, ctx_.SceneName(), ".scene");
}

bool EditorApp::ConfirmUnsaved() {
    if (!ctx_.dirty) return true;
    quitConfirmOpen_ = true; // 复用退出确认模态（语义 = 保存/丢弃/取消）
    confirmContext_ = ConfirmContext::SceneOp;
    return false; // 异步：取消则不继续（保存/丢弃后的续操作 M4.2 补齐闭环）
}

// ---------------------------------------------------------------- 资产 ----
void EditorApp::MenuImportAsset() {
    pickerMode_ = PickerMode::Import;
    picker_.Open("导入资产", ctx_.Assets().AssetsRoot(), "", ""); // 任意扩展名
}

void EditorApp::RescanAssets() {
    AssetDatabase& db = ctx_.Assets();
    db.Rescan();
    const AssetDatabase::ChangeSet& cs = db.LastChange();
    for (uint64_t g : cs.added)
        if (const AssetEntry* e = db.FindByGuid(g); e && e->type == AssetType::Sprite)
            gpuAssets_.ImportSprite(*e);
    for (uint64_t g : cs.modified)
        if (const AssetEntry* e = db.FindByGuid(g); e && e->type == AssetType::Sprite)
            gpuAssets_.ImportSprite(*e);
    for (uint64_t g : cs.removed) gpuAssets_.Evict(g); // 幽灵页（号保留；M6 图集回收）
    db.SaveManifest();
    if (!cs.Empty())
        LEMON_LOG("资产重扫：+%zu ~%zu -%zu", cs.added.size(), cs.modified.size(),
                  cs.removed.size());
}

// ------------------------------------------------ 项目/脚本管线（M4.5）----
bool EditorApp::FindGameProject(std::string& csproj, std::string& dll) {
    namespace fs = std::filesystem;
    const std::string& root = ctx_.Assets().ProjectRoot();
    if (root.empty()) return false;
    std::error_code ec;
    for (auto it = fs::directory_iterator(root + "/Game", ec);
         it != fs::directory_iterator(); it.increment(ec)) {
        if (ec || !it->is_regular_file(ec)) continue;
        if (it->path().extension() != ".csproj") continue;
        csproj = it->path().string();
        dll = root + "/.lemon/bin/" +
              it->path().stem().string() + ".dll";
        return true;
    }
    return false;
}

bool EditorApp::OpenProjectPipeline(const std::string& projectRoot) {
    // spriteId 基址 = 程序化图集登记后首个可用号（跨会话稳定由 manifest 记账）
    const uint32_t spriteIdBase = viewport_->Assets().Registry().SpriteCount() + 1;
    if (!ctx_.Assets().OpenProject(projectRoot, spriteIdBase)) return false;
    gpuAssets_.Init(*device_, ui_.get(), &viewport_->Assets().Registry(),
                    ctx_.Assets(), /*firstSlot=*/2); // 0=调色板 1=字体页
    for (const AssetEntry& e : ctx_.Assets().Entries())
        if (!e.missing && e.type == AssetType::Sprite) gpuAssets_.ImportSprite(e);
    // 设备丢失重建（"editor-viewport" 先 Reset+重建程序化页 → 此处按 DB 记账号接续）
    device_->AddRecreateCallback("asset-gpu", [this](rhi::Device& d) {
        gpuAssets_.RebuildAll(d);
    });
    watcher_.Start(ctx_.Assets().AssetsRoot());
    scriptWatcher_.Start(ctx_.Assets().ProjectRoot() + "/Game"); // 热重载触发源（§3.7）
    LEMON_LOG("资产管线就绪：项目 %s", projectRoot.c_str());

    // 项目自带 Game/ 工程且未显式 --script → 编译 + 装配脚本宿主（向导零配置体验）
    std::string csproj, dll;
    if (launch_->script.empty() && FindGameProject(csproj, dll)) {
        double buildMs = 0.0;
        if (ProjectWizard::BuildGameProject(csproj, ctx_.Assets().ProjectRoot() + "/.lemon/bin",
                                            &buildMs) == 0) {
            InitScriptHostFrom(dll);
            LEMON_LOG("Game/ 编译 %.0fms → %s", buildMs, dll.c_str());
        } else {
            LEMON_ERROR("Game/ 编译失败（项目仍可编辑，无脚本）：dotnet build %s", csproj.c_str());
        }
    }
    return true;
}

bool EditorApp::InitScriptHostFrom(const std::string& dllAbs) {
#ifdef LEMON_SCRIPT_DIR
    if (!std::filesystem::exists(dllAbs)) {
        LEMON_WARN("脚本装配失败：程序集不存在 %s", dllAbs.c_str());
        return false;
    }
    host_ = std::make_unique<scripting::ScriptHost>();
    // DomainManager 要求绝对路径（ALC LoadFromAssemblyPath 约束）
    std::error_code eca;
    std::string scriptAbs = std::filesystem::absolute(dllAbs, eca).generic_string();
    if (host_->Initialize(nullptr, LEMON_SCRIPT_DIR "/Lemon.Entry.runtimeconfig.json",
                          LEMON_SCRIPT_DIR "/Lemon.Entry.dll") &&
        host_->LoadUserAssembly(scriptAbs.c_str())) {
        ctx_.SetScriptHost(host_.get());
        LEMON_LOG("脚本宿主就绪：%s（类型 %zu 个）", scriptAbs.c_str(),
                  ctx_.ScriptTypeNames().size());
        return true;
    }
    LEMON_WARN("脚本宿主初始化失败（%s）——无脚本继续", scriptAbs.c_str());
    host_.reset();
#endif
    return false;
}

bool EditorApp::ScriptSourceChanged() {
    // FileWatcher 只报"有变化"；这里过滤出真正需要重编译的源写（.cs/.csproj，
    // 排除 obj/bin 生成物——dotnet build 会改写它们，否则自我触发死循环）
    namespace fs = std::filesystem;
    const std::string gameDir = ctx_.Assets().ProjectRoot() + "/Game";
    std::error_code ec;
    int64_t newest = 0;
    for (auto it = fs::recursive_directory_iterator(gameDir,
                                                    fs::directory_options::skip_permission_denied,
                                                    ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        const fs::directory_entry& de = *it;
        const std::string name = de.path().filename().string();
        if (de.is_directory(ec)) {
            if (name == "obj" || name == "bin" || (!name.empty() && name[0] == '.'))
                it.disable_recursion_pending();
            continue;
        }
        if (name.size() < 3) continue;
        const std::string ext = de.path().extension().string();
        if (ext != ".cs" && ext != ".csproj") continue;
        auto wt = fs::last_write_time(de.path(), ec);
        if (ec) continue;
        const int64_t s = (int64_t)wt.time_since_epoch().count();
        if (s > newest) newest = s;
    }
    if (newest == 0 || newest <= lastHandledCsWrite_) return false;
    lastHandledCsWrite_ = newest;
    return true;
}

bool EditorApp::TryHotReloadScripts(const char* reason) {
    if (!host_) {
        LEMON_WARN("热重载跳过：无脚本宿主（%s）", reason);
        return false;
    }
    std::string csproj, dll;
    const bool hasProject = FindGameProject(csproj, dll);
    // 无 Game/ 工程（--script 直载 dll 形态）：dll 可能已被外部重编——直接换装同一文件
    if (!hasProject) {
        dll = launch_->script;
        if (dll.empty()) return false;
    }
    const auto t0 = std::chrono::steady_clock::now();
    if (hasProject) {
        const int rc = ProjectWizard::BuildGameProject(
            csproj, ctx_.Assets().ProjectRoot() + "/.lemon/bin");
        if (rc != 0) {
            LEMON_ERROR("热重载编译失败（保持旧域运行）：dotnet build 退出码 %d（%s）", rc,
                        reason);
            return false;
        }
    }
    const auto info = host_->HotReloadAssembly(dll.c_str());
    const int reattached = ctx_.RefreshScriptsAfterReload();
    hotReloadMs_ = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0)
                       .count();
    if (!info.ok) {
        LEMON_ERROR("热重载换装失败：新域装载异常（旧域已弃，脚本停摆——修错后再触发）");
        return false;
    }
    LEMON_LOG("热重载完成（%s）：编译+换装+重装配 %.0fms（Play 重装配 %d 实例；类型 %zu 个）",
              reason, hotReloadMs_, reattached, ctx_.ScriptTypeNames().size());
    if (info.leakCount > 0)
        LEMON_WARN("热重载泄漏计数 %d（旧 ALC 未回收——本 runtime 已知限制，ADR-010 A 线；"
                   "每次约百 KB 级，会话内可接受）",
                   info.leakCount);
    return true;
}

void EditorApp::MenuRebuildScripts() { TryHotReloadScripts("手动触发"); }

void EditorApp::MenuNewProject() { wizOpen_ = true; }

void EditorApp::DrawRecoveryModal() {
    if (recoveryPath_.empty()) return;
    if (!ImGui::IsPopupOpen("崩溃恢复") && !recoveryAnswered_) ImGui::OpenPopup("崩溃恢复");
    if (!ImGui::BeginPopupModal("崩溃恢复", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::Text("检测到较新的自动备份：\n%s", recoveryPath_.c_str());
    ImGui::TextUnformatted("（上次会话可能未正常保存。恢复 = 打开备份内容并保持未保存状态）");
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
    if (ImGui::Button("忽略", ImVec2(120, 0))) {
        recoveryPath_.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

int EditorApp::HotReloadCount() const {
    return host_ ? host_->HotReloadCount() : 0;
}


int EditorApp::Run(const EditorLaunch& launch) {
    launchCopy_ = launch;
    launch_ = &launchCopy_;
    const auto tStart = std::chrono::steady_clock::now();

    SetLogSink(&EditorLogRing::SinkThunk, &log_);
    LEMON_LOG("lemon-editor starting (validate=%s smoke=%s frames=%d)",
              launch.validate ? "on" : "off", launch.smoke ? "on" : "off", launch.frames);

    window_ = Window::Create({.title = "Lemon Editor", .width = 1600, .height = 900});
    if (!window_) return 1;

    rhi::DeviceDesc dd;
    dd.appName = "lemon-editor";
    dd.debugLayer = launch.validate; // 验证层第一天就开（AGENTS 纪律）
    dd.pipelineCachePath = ".lemon/editor/pipeline-cache.bin";
    device_ = rhi::Device::Create(dd);
    rhi::SwapchainDesc sd;
    sd.nativeWindow = window_->NativeHandle();
    sd.present = rhi::PresentModePref::Fifo; // 编辑器 vsync（05 §9）
    if (!device_->CreateSwapchain(sd)) return 1;
    device_->EnableTimestamps(); // Profiler 面板 GPU 列（02 §3.5）

    ui_ = std::make_unique<ImGuiBackend>();
    if (!ui_->Init(*window_, *device_, ".lemon/editor")) return 1;

    viewport_ = std::make_unique<ViewportRenderer>();
    viewport_->Init(*device_, *ui_);

    ownedPanels_ = CreateAllPanels();
    for (auto& p : ownedPanels_) panels_.Add(p.get());

    // ---- M4.4 资产链 / M4.5 项目向导与终验 ----
    if (launch.smoke && !launch.projectDir.empty() && !launch.finalTest) SeedSmokeProject();
    if (launch.finalTest) {
        // 终验第一步：向导建项目（blank 模板；目录必须不存在 → --project 传父目录，
        // 项目名固定 lemon-final，保证可重复跑）
#ifndef LEMON_SCRIPT_DIR
        LEMON_ERROR("终验需要 LEMON_BUILD_SCRIPTING=ON 构建");
        return 1;
#else
        namespace fs = std::filesystem;
        std::error_code ec;
        fs::path p(launch.projectDir.empty() ? "/tmp/lemon-m45" : launch.projectDir);
        fs::remove_all(p / "lemon-final", ec); // 幂等：清上次终验残留
        ProjectDesc desc;
        desc.parentDir = p.string();
        desc.name = "lemon-final";
        desc.sdkDir = LEMON_SCRIPT_DIR;
        desc.engineVersion = "0.4.0-m4";
        const std::string root = ProjectWizard::Create(desc, &wizardSpawnGuid_);
        if (root.empty()) {
            LEMON_ERROR("终验失败：项目向导创建失败");
            return 1;
        }
        launchCopy_.projectDir = root;
        launch_ = &launchCopy_;
        LEMON_LOG("final: 向导建项目 OK %s", root.c_str());
#endif
    }
    if (!launch_->projectDir.empty()) {
        if (!OpenProjectPipeline(launch_->projectDir)) return 1;
    }

    // ---- 脚本宿主（--script <dll> 显式指定；项目 Game/ 已在管线内装配）----
    if (!launch.script.empty()) InitScriptHostFrom(launch.script);
    g_app = this;
    scripting::SetEditorAssetHooks({HookSpriteOf, HookInstantiate});

    // 启动场景：--scene 指定则打开；向导项目开 Main.scene；冒烟播种示例实体
    if (!launch.openScene.empty()) {
        if (!ctx_.OpenScene(launch.openScene)) return 1;
        if (launch.smoke) smokeSeeded_ = ctx_.ActiveScene().AliveCount(); // 守恒断言基数 = 载入数
    } else if (launch.finalTest) {
        if (!ctx_.OpenScene(launch_->projectDir + "/Scenes/Main.scene")) return 1;
        SeedJudgementScene(wizardSpawnGuid_);
        smokeSeeded_ = ctx_.ActiveScene().AliveCount();
    } else if (launch.smoke) {
        SeedSmokeScene();
    } else {
        ctx_.NewScene();
        LEMON_LOG("编辑器就绪（新建场景；Ctrl+O 打开 .scene）");
    }

    // 启动恢复检测（§3.8）：场景打开后 autosave 新于盘档 → 提示（交互模态/终验自动恢复）
    if (!launch.finalTest && !ctx_.Assets().ProjectRoot().empty())
        recoveryPath_ = ctx_.DetectAutosaveRecovery();

    // --play：Play 往返验收（§6 #4/#5）：进 Play → 中段编辑落 Play World → Stop 逐字节断言
    // --final 同样进 Play（终验 §6 #2/#6：Play 中热重载 + fps）
    if ((launch.playTest || launch.finalTest) && launch.smoke) {
        if (!ctx_.EnterPlay()) return 1;
    }

    // --save-scene：场景就绪即保存退出（CLI roundtrip 验收：save → --scene 重开）
    if (!launch.saveScene.empty()) {
        if (!ctx_.SaveScene(launch.saveScene)) return 1;
        std::printf("[lemon] editor: scene saved to %s (%u entities)\n",
                    launch.saveScene.c_str(), ctx_.ActiveScene().AliveCount());
        return 0;
    }

    // ---- 主循环（anim-smoke 基线骨架；编辑 Step = Essential）----
    uint64_t frame = 0;
    double firstFrameMs = -1.0;
    bool running = true;
    const double autosaveClock0 = ImGui::GetTime(); // steady 秒（TickAutosave 节拍源）
    // 终验冷启动口径 = 编辑器主循环首帧（向导建项目 + Game 首次编译是创建期工作，
    // 另由 final-wizard/编译日志计量——不混入 §6 #3 判定）
    const auto tColdStart = launch.finalTest ? std::chrono::steady_clock::now() : tStart;
    while (running) {
        if (!window_->PollEvents() || window_->IsKeyDown(Key::Escape)) {
            if (!exitRequested_) exitRequested_ = true;
        }
        // 资产热替换（M4.4）：watcher 置脏 → 重扫 + 增量导入（改文件落盘即时可见）
        if (watcher_.Running() && watcher_.ConsumeDirty()) RescanAssets();
        // C# 热重载（M4.5 §3.7）：Game/ 源写 → 防抖 0.4s（编辑器连续保存不打断）→ 编译+换装
        if (scriptWatcher_.Running() && scriptWatcher_.ConsumeDirty() && !launch.finalTest) {
            if (ImGui::GetTime() >= reloadDebounceUntil_) {
                reloadDebounceUntil_ = ImGui::GetTime() + 0.4;
                if (ScriptSourceChanged()) TryHotReloadScripts("源码变更");
            }
        }
        // 自动备份（§3.8）：5 分钟节拍，dirty 且非 Play 才写
        ctx_.TickAutosave(ImGui::GetTime() - autosaveClock0);
        if (launch.frames > 0 && (int)frame >= launch.frames) running = false;
        if (forceExit_) running = false;
        if (exitRequested_ && ctx_.dirty && !quitConfirmArmed_) {
            quitConfirmOpen_ = true; // 退出前确认（一次）
            exitRequested_ = false;
        }
        if (!running) break;
        if (window_->TakeResized() && !device_->RecreateSwapchain()) continue;

        if (ctx_.Playing() && launch.playTest && frame == (uint64_t)(launch.frames / 2)) {
            // Play 中编辑落 Play World（决议 #5）：挪动主选中 —— Stop 后必须消失（逐字节）
            ecs::Entity p = ctx_.Primary();
            if (!p.IsNull() && ctx_.ActiveScene().Has<ecs::Transform2D>(p))
                ctx_.ActiveScene().Get<ecs::Transform2D>(p).pos = Vec2{1234.0f, 567.0f};
            LEMON_LOG("play-test: Play 中编辑已落 Play World（Stop 即丢）");
        }
        // 资产热替换验收（M4.4 §5）：中点把 PNG 换成 96×48 蓝（尺寸变化 = 页重建路径）
        if (launch.smoke && !launch.projectDir.empty() && !launch.finalTest &&
            frame == (uint64_t)(launch.frames / 2)) {
            std::vector<uint8_t> px(96 * 48 * 4);
            for (int y = 0; y < 48; ++y)
                for (int x = 0; x < 96; ++x) {
                    uint8_t* q = &px[((size_t)y * 96 + x) * 4];
                    q[0] = 60; q[1] = 120; q[2] = 240; q[3] = 255;
                }
            std::error_code ecw;
            std::filesystem::path png =
                std::filesystem::path(launch.projectDir) / "Assets" / "smoke.png";
            stbi_write_png(png.string().c_str(), 96, 48, 4, px.data(), 96 * 4);
            LEMON_LOG("asset-smoke: PNG 落盘改写（96×48 蓝）→ 等 watcher 重导入");
        }
        // 终验（§6 #1/#2/#6/#7）：Play 中热重载——改 .cs 落盘 → 编译换装 → 新逻辑 +
        // StateBag 续跑（刷怪窗口 40→70；续跑总刷怪 66 = 换装前 16 + 换装后 50）
        if (launch.finalTest && frame == 20) {
            namespace fs = std::filesystem;
            const fs::path sp = fs::path(launch_->projectDir) / "Game" / "SpawnerBehaviour.cs";
            std::ifstream in(sp, std::ios::binary);
            std::string src((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            const size_t at = src.find("_tick > 40");
            finalPlayReloadOk_ = at != std::string::npos;
            if (finalPlayReloadOk_) {
                finalAliveAtReload_ = ctx_.ActiveScene().AliveCount();
                src.replace(at, 10, "_tick > 70");
                {
                    std::ofstream out(sp, std::ios::trunc);
                    out << src;
                } // 先落盘再编译（流未 close 就 build 会读到半截文件——实测坑）
                finalReloadFrame_ = frame;
                finalPlayReloadOk_ = TryHotReloadScripts("final-play（Play 中）");
                finalPlayReloadMs_ = hotReloadMs_;
            }
            if (!finalPlayReloadOk_) LEMON_ERROR("final: Play 中热重载失败");
        }
        if (ctx_.Playing()) {
            // 输入路由：GameView 聚焦且非文本输入 → 语义子集（WASD/箭头/空格）进 Play World
            ecs::InputState in;
            if (gameViewFocused_ && !ImGui::GetIO().WantTextInput) {
                float ax = 0, ay = 0;
                if (ImGui::IsKeyDown(ImGuiKey_A) || ImGui::IsKeyDown(ImGuiKey_LeftArrow)) ax -= 1.0f;
                if (ImGui::IsKeyDown(ImGuiKey_D) || ImGui::IsKeyDown(ImGuiKey_RightArrow)) ax += 1.0f;
                if (ImGui::IsKeyDown(ImGuiKey_W) || ImGui::IsKeyDown(ImGuiKey_UpArrow)) ay -= 1.0f;
                if (ImGui::IsKeyDown(ImGuiKey_S) || ImGui::IsKeyDown(ImGuiKey_DownArrow)) ay += 1.0f;
                in.ax = ax;
                in.ay = ay;
                if (ImGui::IsKeyDown(ImGuiKey_Space)) in.buttons |= 1u << 4; // bit4 attack
            }
            ctx_.ActiveWorld().ApplyInput(in);
            ctx_.TickPlay(paused_ && !singleStep_ ? 0.0f : 1.0f / 60.0f); // Pause = dt0（含 Essential 提交）
            singleStep_ = false;
        } else {
            ctx_.TickEditor(1.0f / 60.0f); // Essential（销毁提交）+ 空 FixedTick
        }

        ui_->BeginFrame(*window_);
        BuildUI();

        rhi::AcquireResult acq = device_->AcquireNextImage();
        if (acq.deviceLost || acq.needsRecreate) {
            if (acq.deviceLost || !device_->RecreateSwapchain()) continue;
        }
        rhi::CommandList& cl = device_->BeginFrame();
        const uint32_t w = device_->SwapchainWidth(), h = device_->SwapchainHeight();
        viewport_->Render(cl, ctx_); // 双视口离屏（BuildUI 已定 RT 尺寸/注入 overlay）

        const float clear[4] = {0.055f, 0.06f, 0.08f, 1.0f};
        cl.BeginPass(device_->SwapchainFormat(), w, h, clear);
        cl.SetViewportScissor(w, h);
        ui_->Render(cl);
        cl.EndPass();

        const bool lastFrame =
            launch.frames > 0 && (int)frame == launch.frames - 1 && !launch.screenshot.empty();
        if (lastFrame) cl.DebugRecordCapture();

        bool needRe = false, lost = false;
        device_->EndFrameAndPresent(needRe, lost);
        if (lost || needRe) {
            if (lost || !device_->RecreateSwapchain()) continue;
        }
        viewport_->AdvanceFrame();
        // 终验 fps 统计（§6 #6：判据场景 Play ≥45fps；预热 60 帧与换装窗口 90 帧剔除
        // ——dotnet build 同步阻塞主线程属换装耗时，不计帧率口径）
        if (launch.finalTest && ctx_.Playing() && frame > 60 &&
            frame - finalReloadFrame_ > 90) {
            const float f = ImGui::GetIO().Framerate;
            if (f > 1.0f && f < finalPlayMinFps_) finalPlayMinFps_ = f;
        }
        if (firstFrameMs < 0.0)
            firstFrameMs = std::chrono::duration<double, std::milli>(
                               std::chrono::steady_clock::now() - tColdStart)
                               .count();
        ++frame;
    }

    bool playVerified = true;
    double playEnterMs = 0, playExitMs = 0;
    uint32_t playAliveAtStop = 0;
    if (ctx_.Playing()) { // --play：跑满帧数后 Stop（恢复编辑世界）
        playAliveAtStop = ctx_.ActiveScene().AliveCount();
        playEnterMs = ctx_.LastEnterPlayMs();
        playVerified = ctx_.ExitPlay();
        playExitMs = ctx_.LastExitPlayMs();
    }

    // ---- 冒烟自检（§6 #13：退出码即判据）----
    int exitCode = 0;
    if (!launch.screenshot.empty()) {
        std::vector<uint8_t> px;
        uint32_t sw = 0, sh = 0;
        if (device_->DebugFetchCapture(px, sw, sh)) {
            std::error_code ec;
            if (auto p = std::filesystem::path(launch.screenshot).parent_path(); !p.empty())
                std::filesystem::create_directories(p, ec);
            int ok = stbi_write_png(launch.screenshot.c_str(), (int)sw, (int)sh, 4, px.data(),
                                    (int)sw * 4);
            std::printf("[lemon] editor-smoke screenshot: %s %ux%u => %s\n",
                        launch.screenshot.c_str(), sw, sh, ok ? "written" : "FAILED");
            exitCode |= ok ? 0 : 1;
        } else {
            std::printf("[lemon] editor-smoke screenshot: capture FAILED\n");
            exitCode = 1;
        }
    }
    if (launch.smoke) {
        const bool drew = ImGui::GetCurrentContext() && ImGui::GetDrawData() &&
                          ImGui::GetDrawData()->TotalVtxCount > 0;
        bool cjkOk = false;
        if (ImFont* f = ImGui::GetFont()) cjkOk = f->IsLoaded() && f->IsGlyphInFont(0x4E2D);
        const uint64_t errCount = LogCountOf(LogLevel::Error);
        // 场景健全：实体数守恒（播种数 = 现存数；冒烟中无销毁）+ 视口可见包 > 0
        const uint32_t alive = ctx_.ActiveScene().AliveCount();
        const uint32_t visible = viewport_->LastSceneVisible();
        const bool sceneOk = alive == smokeSeeded_ && smokeSeeded_ > 0 && visible > 0;
        std::printf("[lemon] editor-smoke: frames=%llu cold-start=%.0fms uiVtx=%d "
                    "cjkFont=%s entities=%u/%u viewportVisible=%u errors=%llu\n",
                    (unsigned long long)frame, firstFrameMs,
                    ImGui::GetCurrentContext() && ImGui::GetDrawData()
                        ? ImGui::GetDrawData()->TotalVtxCount
                        : -1,
                    cjkOk ? "OK" : "FAIL", alive, smokeSeeded_, visible,
                    (unsigned long long)errCount);
        bool playOk = true;
        if (launch.playTest) {
            playOk = playVerified && ctx_.LastExitVerified() && playEnterMs < 500.0 &&
                     playExitMs < 300.0;
            std::printf("[lemon] editor-smoke play-roundtrip: enter=%.1fms exit=%.1fms "
                        "byte-exact=%s => %s\n",
                        playEnterMs, playExitMs, ctx_.LastExitVerified() ? "YES" : "NO",
                        playOk ? "OK" : "FAIL");
        }
        // M4.4 资产链验收：固定 guid 资产在库、已导入、热替换生效（96×48 蓝）
        bool assetsOk = true;
        if (!launch.projectDir.empty() && !launch.finalTest) {
            constexpr uint64_t kSmokeGuid = 0x5bd31a7c10e9f2c8ull;
            const AssetEntry* e = ctx_.Assets().FindByGuid(kSmokeGuid);
            uint32_t w = 0, h = 0;
            const bool info = e ? gpuAssets_.PageInfo(kSmokeGuid, w, h) : false;
            const bool thumb = e && gpuAssets_.Thumbnail(kSmokeGuid) != nullptr;
            const bool hotOk = !launch.smoke || (w == 96 && h == 48); // 中点改写后应已重导入
            assetsOk = e && !e->missing && e->spriteId != 0 && info && thumb && hotOk;
            std::printf("[lemon] editor-smoke assets: entry=%s spriteId=%u page=%ux%u "
                        "thumb=%s hotreload=%s => %s\n",
                        e ? "YES" : "NO", e ? e->spriteId : 0, w, h, thumb ? "YES" : "NO",
                        (w == 96 && h == 48) ? "YES" : (launch.playTest ? "NO" : "n/a"),
                        assetsOk ? "OK" : "FAIL");
        }
        // M4.4 脚本链验收：--script + --play → SpawnerBehaviour 每帧刷怪（36 只）
        bool scriptOk = true;
        if (host_ && launch.playTest && !launch.finalTest) {
            scriptOk = playAliveAtStop > smokeSeeded_ + 10;
            std::printf("[lemon] editor-smoke script-spawn: playAlive=%u seeded=%u => %s\n",
                        playAliveAtStop, smokeSeeded_, scriptOk ? "OK" : "FAIL");
        }
        // ---- M4.5 终验（§6 #1/#2/#3/#6/#7 全量化）----
        bool finalOk = true;
        if (launch.finalTest) {
            // StateBag 续跑断言：换装于 tick=20（窗口 40→70）→ 总刷怪 = 16 + 50 = 66
            // （若状态丢失 = 16 + 66 = 82 ≠ 66；若换装失败 = 36）
            finalStateBagTotal_ = (int)playAliveAtStop - (int)smokeSeeded_;
            const bool bagOk = finalStateBagTotal_ == 66;
            // Edit 态热重载一例（§6 #2 双态各一）：加 EditProbeBehaviour → 注册表可见
            {
                namespace fs = std::filesystem;
                const fs::path gp = fs::path(launch_->projectDir) / "Game";
                std::ofstream probe(gp / "EditProbeBehaviour.cs", std::ios::trunc);
                probe << "public sealed class EditProbeBehaviour : Lemon.LemonBehaviour { }\n";
                std::ifstream in(gp / "GameMain.cs", std::ios::binary);
                std::string src((std::istreambuf_iterator<char>(in)),
                                std::istreambuf_iterator<char>());
                // 在既有注册行前插一行（锚点替换——直接动 Configure 签名会破坏大括号配对）
                const std::string anchor = "Lemon.Behaviours.Register<InputMoverBehaviour>();";
                const size_t at = src.find(anchor);
                if (at != std::string::npos)
                    src.insert(at, "Lemon.Behaviours.Register<EditProbeBehaviour>();\n        ");
                std::ofstream out(gp / "GameMain.cs", std::ios::trunc);
                out << src;
            } // 落盘作用域结束（先 close 再编译）
            finalEditReloadOk_ = TryHotReloadScripts("final-edit（Edit 态）");
            bool seen = false;
            for (const std::string& n : ctx_.ScriptTypeNames()) seen |= n == "EditProbeBehaviour";
            finalEditReloadOk_ = finalEditReloadOk_ && seen;
            // 自动备份/崩溃恢复链（§3.8）：dirty → 快照 → 检出 → 恢复（保持 dirty）→ 落盘 → 清
            bool autosaveOk = false;
            {
                ctx_.Select(ctx_.Primary(), false);
                ctx_.dirty = true;
                const bool wrote = ctx_.AutoSaveNow();
                const std::string rec = ctx_.DetectAutosaveRecovery();
                const bool opened = !rec.empty() && ctx_.OpenSceneRecovery(rec);
                const bool keptDirty = ctx_.dirty;
                const bool saved = ctx_.SaveScene();
                const bool cleared = ctx_.DetectAutosaveRecovery().empty();
                autosaveOk = wrote && opened && keptDirty && saved && cleared;
            }
            const int leaks = host_ ? host_->HotReloadLeakCount() : 0;
            const int reloads = HotReloadCount();
            const bool hrOk = finalPlayReloadOk_ && finalEditReloadOk_ &&
                              finalPlayReloadMs_ <= 2000.0 && hotReloadMs_ <= 2000.0 &&
                              leaks > 0 /* A 线已知泄漏（探针口径）*/;
            const bool fpsOk = finalPlayMinFps_ >= 45.0;
            const bool coldOk = firstFrameMs < 2000.0;
            finalOk = bagOk && hrOk && fpsOk && coldOk && autosaveOk && playVerified;
            std::printf("[lemon] final-wizard: dirs/project.lemon/Game/spawn.png => %s\n", "OK");
            std::printf("[lemon] final-judgement: zero-code scene entities=%u visible=%u\n",
                        smokeSeeded_, viewport_->LastSceneVisible());
            std::printf("[lemon] final-hotreload: play=%.0fms edit=%.0fms(≤2000) "
                        "stateBag=%d/66 reloads=%d leaks=%d(A线) => %s\n",
                        finalPlayReloadMs_, hotReloadMs_, finalStateBagTotal_, reloads, leaks,
                        hrOk ? "OK" : "FAIL");
            std::printf("[lemon] final-play: minFps=%.0f(≥45) enter=%.1fms exit=%.1fms "
                        "byte-exact=%s => %s\n",
                        finalPlayMinFps_, playEnterMs, playExitMs,
                        ctx_.LastExitVerified() ? "YES" : "NO", (fpsOk && playVerified) ? "OK" : "FAIL");
            std::printf("[lemon] final-autosave: snapshot+recovery+save-clear => %s\n",
                        autosaveOk ? "OK" : "FAIL");
            std::printf("[lemon] final-coldstart: %.0fms(<2000) => %s\n", firstFrameMs,
                        coldOk ? "OK" : "FAIL");
            if (!finalOk) std::printf("[lemon] final FAIL 项：bag=%d hr=%d fps=%d cold=%d autosave=%d play=%d\n",
                                      bagOk, hrOk, fpsOk, coldOk, autosaveOk, playVerified);
        }
        if (!drew || !cjkOk || errCount > 0 || !sceneOk || !playOk || !assetsOk || !scriptOk ||
            !finalOk) {
            std::printf("[lemon] editor-smoke FAIL\n");
            exitCode = 1;
        } else {
            std::printf("[lemon] editor-smoke PASS\n");
        }
    } else {
        std::printf("[lemon] editor exit: frames=%llu cold-start=%.0fms\n",
                    (unsigned long long)frame, firstFrameMs);
    }

    watcher_.Stop();          // 先停 watcher 线程（此后无资产重扫）
    scriptWatcher_.Stop();    // 与 Game/ 源监视同批收尾
    host_.reset();            // C# 宿主卸载（无脚本时为空操作）
    device_->WaitIdle(); // ImGui 后端资源（描述符池/采样器）可能被在途帧引用，先等闲
    ui_->Shutdown();
    SetLogSink(nullptr, nullptr);
    device_->SavePipelineCache();
    device_.reset();
    window_.reset();
    return exitCode;
}

void EditorApp::SeedSmokeScene() {
    // 冒烟播种：父子层级 + 常用组件 → Hierarchy/Inspector 有内容可验收
    using namespace lemon::ecs;
    Scene& s = ctx_.ActiveScene();
    (void)0;
    ecs::Entity root = ctx_.CreateEntity("Player");
    s.Get<Transform2D>(root).pos = Vec2{640, 360}; // 相机中心
    {
        SpriteRenderer& sr = s.Emplace<SpriteRenderer>(root);
        sr.spriteId = 3;
        sr.flags = 0x4;
        Health& hp = s.Emplace<Health>(root);
        hp.max = hp.cur = 100.0f;
        Stats& st = s.Emplace<Stats>(root);
        st.moveSpeed = 180.0f;
        Chase& ch = s.Emplace<Chase>(root);
        ch.speed = 120.0f;
        ch.aggroRange = 300.0f;
        ch.keepRange = 40.0f;
        ch.targetTeam = 1;
    }
    for (int i = 0; i < 3; ++i) {
        char tag[16];
        std::snprintf(tag, sizeof(tag), "Mob%d", i);
        ecs::Entity mob = ctx_.CreateSpriteEntity(tag, (uint32_t)(5 + i)); // 青/品红/棋盘
        s.Emplace<Collectible>(mob);
        SceneSetParent(s, mob, root); // 父子链（内核 #1）
        s.Get<Transform2D>(mob).pos = Vec2{110.0f * (i + 1), 70.0f * i};
    }
    ecs::Entity trigger = ctx_.CreateEntity("Gate");
    s.Emplace<Trigger2D>(trigger).radius = 128.0f;
    s.Get<Transform2D>(trigger).pos = Vec2{950, 180};
    // M4.4 资产链：导入 PNG → 场景实体（CreateSpriteEntityFromAsset 与拖拽/双击同通路）
    if (!launch_->projectDir.empty()) {
        ecs::Entity fromAsset =
            ctx_.CreateSpriteEntityFromAsset("FromAsset", 0x5bd31a7c10e9f2c8ull, Vec2{420, 200});
        if (!fromAsset.IsNull()) s.Get<Transform2D>(fromAsset).scale = Vec2{0.5f, 0.5f};
    }
    // M4.4 装配通路：--script 时挂 SpawnerBehaviour（Play 中刷怪断言用）
    if (ctx_.Scripts()) ctx_.AttachScript(root, 0, "SpawnerBehaviour");
    smokeSeeded_ = (uint32_t)s.AliveCount();
    ctx_.Select(root, false); // Inspector 有主选中
}

void EditorApp::SeedSmokeProject() {
    // 冒烟项目播种：Assets/smoke.png（64×64 红，四角亮标记）+ 固定 guid .meta
    // （TestScript.SpawnerBehaviour.kSpriteGuid 引用同一常量——资产链端到端可断言）
    std::error_code ec;
    std::filesystem::path root(launchCopy_.projectDir);
    std::filesystem::create_directories(root / "Assets", ec);
    std::filesystem::path png = root / "Assets" / "smoke.png";
    if (!std::filesystem::exists(png, ec)) {
        std::vector<uint8_t> px(64 * 64 * 4);
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x) {
                uint8_t* q = &px[((size_t)y * 64 + x) * 4];
                const bool mark = (x < 8 || x >= 56) && (y < 8 || y >= 56);
                q[0] = mark ? 255 : 200;
                q[1] = mark ? 255 : 40;
                q[2] = 40;
                q[3] = 255;
            }
        stbi_write_png(png.string().c_str(), 64, 64, 4, px.data(), 64 * 4);
    }
    std::filesystem::path meta = png.string() + ".meta";
    if (!std::filesystem::exists(meta, ec)) {
        std::ofstream f(meta, std::ios::trunc);
        f << "{\n  \"guid\": \"5bd31a7c10e9f2c8\",\n  \"type\": \"sprite\",\n  \"hash\": 0,\n"
             "  \"importedAt\": 0\n}\n";
    }
}

void EditorApp::SeedJudgementScene(uint64_t spawnGuid) {
    // 终验判据场景（§6 #1）：纯编辑器操作等价物——"走地图 + 刷怪"零代码。
    // 地图 = 背景精灵手摆（种子资产平铺）；角色 = InputMoverBehaviour（方向键走动）；
    // 刷怪器 = SpawnerBehaviour（种子 sprite 周期 Spawn）。
    using namespace lemon::ecs;
    Scene& s = ctx_.ActiveScene();
    // 地面：spawn.png 4×3 平铺（大比例 = 走地图背景）
    for (int ty = 0; ty < 3; ++ty)
        for (int tx = 0; tx < 4; ++tx) {
            char tag[24];
            std::snprintf(tag, sizeof(tag), "Ground%d_%d", ty, tx);
            ecs::Entity g = ctx_.CreateSpriteEntityFromAsset(tag, spawnGuid,
                                                             Vec2{160.0f + tx * 320.0f,
                                                                  140.0f + ty * 240.0f});
            if (!g.IsNull()) s.Get<Transform2D>(g).scale = Vec2{8.0f, 6.0f};
        }
    // 角色（走地图）
    ecs::Entity player = ctx_.CreateSpriteEntityFromAsset("Player", spawnGuid, Vec2{640, 360});
    if (ctx_.Scripts()) ctx_.AttachScript(player, 0, "InputMoverBehaviour");
    // 刷怪器
    ecs::Entity spawner = ctx_.CreateSpriteEntityFromAsset("Spawner", spawnGuid, Vec2{980, 220});
    if (ctx_.Scripts()) ctx_.AttachScript(spawner, 0, "SpawnerBehaviour");
    if (!player.IsNull()) ctx_.Select(player, false);
}

} // namespace lemon::editor
