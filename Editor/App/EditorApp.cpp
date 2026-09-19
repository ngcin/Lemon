// Lemon 编辑器 — 应用壳实现（M4-Editor-Plan §3.1；主循环承 anim-smoke 全链基线）
// M4.0：壳 + 默认布局 + DPI/字体 + smoke；M4.1：EditorContext/场景 IO/快捷键/关闭确认。
#include "App/EditorApp.h"

#include <cstdio>
#include <cstring>
#include <filesystem>

#include "stb_image_write.h"

#include "App/ImGuiBackend.h"
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
#include "imgui.h"
#include "imgui_internal.h" // DockBuilder（docking 分支布局编程 API）

namespace lemon::editor {

EditorApp::EditorApp() = default;
EditorApp::~EditorApp() = default;

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
        if (ImGui::MenuItem("退出", nullptr, false, true)) RequestExit();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
        ImGui::MenuItem("Undo", "Ctrl+Z", false, false); // M4.2 属性轨
        ImGui::MenuItem("Redo", "Ctrl+Y", false, false);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Assets")) {
        ImGui::MenuItem("(导入器 M4.4)", nullptr, false, false);
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
    ImGui::TextDisabled("| DPI %.1fx | %dx%d px | %.0f fps | 选中 %zu | 中文渲染正常",
                        ui_->DisplayScale(), pw, ph, ImGui::GetIO().Framerate,
                        ctx_.Selection().size());
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
    // 文件选择器（打开/另存共用；动作一次性返回）
    if (PickerResult r = picker_.Draw(); r.action != PickerAction::None) {
        if (r.action == PickerAction::Open || r.action == PickerAction::Save) {
            if (pickerMode_ == PickerMode::Open) {
                if (!ctx_.OpenScene(r.path)) LEMON_WARN("打开失败：%s", r.path.c_str());
            } else {
                if (ctx_.SaveScene(r.path)) LEMON_LOG("已另存为：%s", r.path.c_str());
            }
        }
    }

    // 退出确认（dirty 场景）：保存 / 丢弃 / 取消
    if (quitConfirmOpen_) {
        ImGui::OpenPopup("未保存更改");
        quitConfirmOpen_ = false;
        quitConfirmArmed_ = true;
    }
    if (ImGui::BeginPopupModal("未保存更改", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
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

    // 启动场景：--scene 指定则打开；冒烟模式播种示例实体（面板有内容可验收）
    if (!launch.openScene.empty()) {
        if (!ctx_.OpenScene(launch.openScene)) return 1;
        if (launch.smoke) smokeSeeded_ = ctx_.ActiveScene().AliveCount(); // 守恒断言基数 = 载入数
    } else if (launch.smoke) {
        SeedSmokeScene();
    } else {
        ctx_.NewScene();
        LEMON_LOG("编辑器就绪（新建场景；Ctrl+O 打开 .scene）");
    }

    // --play：Play 往返验收（§6 #4/#5）：进 Play → 中段编辑落 Play World → Stop 逐字节断言
    if (launch.playTest && launch.smoke) {
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
    while (running) {
        if (!window_->PollEvents() || window_->IsKeyDown(Key::Escape)) {
            if (!exitRequested_) exitRequested_ = true;
        }
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
        if (firstFrameMs < 0.0)
            firstFrameMs = std::chrono::duration<double, std::milli>(
                               std::chrono::steady_clock::now() - tStart)
                               .count();
        ++frame;
    }

    bool playVerified = true;
    double playEnterMs = 0, playExitMs = 0;
    if (ctx_.Playing()) { // --play：跑满帧数后 Stop（恢复编辑世界）
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
        if (!drew || !cjkOk || errCount > 0 || !sceneOk || !playOk) {
            std::printf("[lemon] editor-smoke FAIL\n");
            exitCode = 1;
        } else {
            std::printf("[lemon] editor-smoke PASS\n");
        }
    } else {
        std::printf("[lemon] editor exit: frames=%llu cold-start=%.0fms\n",
                    (unsigned long long)frame, firstFrameMs);
    }

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
    smokeSeeded_ = (uint32_t)s.AliveCount();
    ctx_.Select(root, false); // Inspector 有主选中
}

} // namespace lemon::editor
