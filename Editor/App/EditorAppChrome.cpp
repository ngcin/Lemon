// Lemon 编辑器 — EditorApp UI 骨架（菜单栏/工具栏/状态栏/布局下拉/快捷键/
// 选择器与模态；M4 §2/§3）。批② 2026-09-29 自 EditorApp.cpp 机械拆分：
// 成员函数跨 TU 定义，类定义 App/EditorApp.h 零改动，代码逐行原样。
#include "App/EditorApp.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

#include "App/ImGuiBackend.h"
#include "Assets/AssetDatabase.h"
#include "Assets/ProjectWizard.h"
#include "Tooling/Icons.h"
#include "Tooling/Theme.h"
#include "Components/CoreComponents.h"
#include "Core/Log.h"
#include "EditorContext.h"
#include "Platform/Window.h"
#include "Scripting/ScriptHost.h"
#include "imgui.h"
#include "imgui_internal.h" // DockBuilder（docking 分支布局编程 API）+ FindWindowByName
#include "misc/cpp/imgui_stdlib.h" // InputText(std::string*) 重载（Layout 命名等）
#include "Tooling/TestHooks.h"
#include "App/RecentProjects.h"

namespace lemon::editor {

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
    // Animation（M6a 批② T3）：按需窗口（OpenByDefault=false），停靠请求照样
    // 预挂——首次打开落位而非随机浮窗（DockBuilder 队列对未建窗口有效）；
    // 实际停靠位 = bottomId（见下方 T3b 注记）
    // Profiler 补进 bottom 区（BUG-3：此前未停靠 → 首启以浮窗随机遮挡
    // Hierarchy）；先于 Console/Assets 停靠 = 不抢当前标签，默认隐藏页
    ImGui::DockBuilderDockWindow("Profiler", bottomId);
    ImGui::DockBuilderDockWindow("Console", bottomId);
    ImGui::DockBuilderDockWindow("Assets", bottomId);  // 同区域 = 标签页
    // T3b：Animation 落底部区（Unity 同款——宽矮窗配横排胶片带；不开在中央区，
    // 否则打开即抢 Scene 标签页 → 视口像素断言/编辑视图被盖）
    ImGui::DockBuilderDockWindow("Animation", bottomId);
    ImGui::DockBuilderFinish(dock);
    LEMON_LOG("editor: default layout built");
}

void EditorApp::BuildMenuBar() {
    if (!ImGui::BeginMenuBar()) return;
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("新建场景", nullptr, false, !playing_)) MenuNewScene();
        if (ImGui::MenuItem("打开场景...", "Ctrl+O", false, !playing_)) MenuOpenScene();
        // M4.8-b：最近场景子菜单（文件名 + 当前标记；tooltip 全路径）
        if (ImGui::BeginMenu("最近场景", !ctx_.RecentScenes().empty() && !playing_)) {
            namespace fsr = std::filesystem;
            const std::vector<std::string>& recents = ctx_.RecentScenes();
            for (size_t i = 0; i < recents.size(); ++i) {
                const std::string& p = recents[i];
                ImGui::PushID((int)i); // 索引 ID：档内重复路径曾致同 label ID 冲突
                std::error_code ec;
                const bool usable = fsr::is_regular_file(p, ec); // 文件被删 → 灰显可辨
                const bool cur = p == ctx_.ScenePath();
                const std::string label =
                    fsr::path(p).filename().string() + (cur ? "（当前）" : "");
                if (ImGui::MenuItem(label.c_str(), nullptr, cur, usable && !cur))
                    MenuOpenRecentScene(p); // 按值收，切断对 recents 元素的引用
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", p.c_str());
                ImGui::PopID();
            }
            ImGui::EndMenu();
        }
        ImGui::Separator();
        char saveLabel[96];
        std::snprintf(saveLabel, sizeof(saveLabel), "保存场景 %s", ctx_.ScenePath().empty() ? "" : "(Ctrl+S)");
        if (ImGui::MenuItem(saveLabel, ctx_.ScenePath().empty() ? nullptr : "Ctrl+S", false,
                            !playing_))
            MenuSaveScene();
        if (ImGui::MenuItem("另存为...", nullptr, false, !playing_)) MenuSaveSceneAs();
        ImGui::Separator();
        if (ImGui::MenuItem("新建项目...", nullptr, false, !ctx_.Playing())) MenuNewProject();
        if (ImGui::MenuItem("打开项目...", nullptr, false, !ctx_.Playing())) MenuOpenProject();
        if (ImGui::BeginMenu("最近打开", !recentProjects_.empty() && !ctx_.Playing())) {
            namespace fsr = std::filesystem;
            for (const std::string& p : recentProjects_) {
                ImGui::PushID(p.c_str());
                std::error_code ec;
                // 可点性 = 根下有 project.lemon（目录被删/手滑改名 → 灰显可辨）
                const bool usable =
                    fsr::is_regular_file(fsr::path(p) / "project.lemon", ec);
                if (ImGui::MenuItem(fsr::path(p).filename().c_str(), p.c_str(), false,
                                    usable && !ctx_.dirty)) {
                    if (OpenProjectInSession(p)) LEMON_LOG("已打开最近项目：%s", p.c_str());
                }
                ImGui::PopID();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("清除列表")) {
                recentProjects_.clear();
                SaveRecentProjects(recentProjects_);
            }
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("退出", nullptr, false, true)) RequestExit();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
        // M4.6 §4-8：接线真实可用性（快捷键 M4.2 起已通；Play 中禁用同快捷键）
        if (ImGui::MenuItem("Undo", "Ctrl+Z", false, !ctx_.Playing() && ctx_.Undo().CanUndo()))
            ctx_.Undo().Undo();
        if (ImGui::MenuItem("Redo", "Ctrl+Y", false, !ctx_.Playing() && ctx_.Undo().CanRedo()))
            ctx_.Undo().Redo();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Assets")) {
        if (ImGui::MenuItem("导入文件...", nullptr, false, true)) MenuImportAsset();
        if (ImGui::MenuItem("重扫资产库", nullptr, false, true)) RescanAssets();
        if (ImGui::MenuItem("清理孤儿 .meta…", nullptr, false, true)) MenuSweepOrphanMetas();
        ImGui::Separator();
        { // 新建脚本（M4.6 §5-4）：模板 .cs → Game/ + 注册行 → 热重载排队
            std::string csproj, dll;
            const bool can =
                !ctx_.Assets().ProjectRoot().empty() && FindGameProject(csproj, dll);
            if (ImGui::MenuItem("新建脚本...", nullptr, false, can)) newScriptOpen_ = true;
            if (!can && ImGui::IsItemHovered())
                ImGui::SetTooltip("需要已打开项目且 Game/ 有脚本工程");
        }
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
        // C1：创建三入口（+创建/空区右键/本菜单）都进结构轨——此前本菜单与
        // 空区右键漏推快照，创建后 Ctrl+Z 报"栈空"
        if (ImGui::MenuItem("创建空实体")) {
            const std::string before = ctx_.SnapshotSceneJson();
            ecs::Entity ne = ctx_.CreateEntity("Empty");
            ctx_.Select(ne, false);
            if (!ctx_.Playing()) ctx_.PushStructuralUndo("创建实体", before);
        }
        if (ImGui::MenuItem("创建精灵")) {
            const std::string before = ctx_.SnapshotSceneJson();
            ecs::Entity ne = ctx_.CreateSpriteEntity("Sprite");
            ctx_.Select(ne, false);
            if (!ctx_.Playing()) ctx_.PushStructuralUndo("创建实体", before);
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Window")) {
        for (auto& e : panels_.Entries()) ImGui::MenuItem(e.panel->Name(), nullptr, &e.open);
        ImGui::Separator();
        // M6c 批③：Audio Mixer 按需工具窗（不进面板注册表——05 §3 冻结旁路形态）
        ImGui::MenuItem("Audio Mixer", nullptr, &audioMixerOpen_);
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
    // M4.7a/b 三段式图标工具栏：左 = Q/W/E/R + 网格显示/吸附｜中 = Play/Pause/单步
    // （居中）｜右 = 预留（Layout 下拉 M5）。图标 = 形状页（零新依赖），居中按按钮实宽精算。
    const bool playing = ctx_.Playing();
    const float x0 = ImGui::GetCursorPosX();
    const float avail = ImGui::GetContentRegionAvail().x;

    // ---- 左段：Q 选择/W 移动/E 旋转/R 缩放 工具组 + 网格吸附（图标 toggle）----
    struct ToolBtn { IconKind icon; const char* id; const char* tip; EditTool tool; };
    static const ToolBtn kTools[] = {
        {IconKind::Cursor, "##toolSelect", "选择（Q）：8 向手柄调整大小 / 拖动移动", EditTool::Select},
        {IconKind::Move, "##toolMove", "移动 (W)", EditTool::Move},
        {IconKind::Rotate, "##toolRotate", "旋转 (E)", EditTool::Rotate},
        {IconKind::Scale, "##toolScale", "四角缩放 (R)", EditTool::Scale}};
    for (const auto& t : kTools) {
        if (ui::IconButton(*this, t.icon, t.id, t.tool == tool_)) tool_ = t.tool;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", t.tip);
        ImGui::SameLine();
    }
    // 网格显示（纯视觉）与拖拽吸附（独立开关，默认关）——Godot/Unity 语义
    if (ui::IconButton(*this, IconKind::Grid, "##gridVisible", gridVisible_))
        gridVisible_ = !gridVisible_;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("网格显示");
    ImGui::SameLine();
    if (ui::IconButton(*this, IconKind::Magnet, "##snap", snapEnabled_))
        snapEnabled_ = !snapEnabled_;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("拖拽吸附（平移 8px / 旋转 15° / 缩放 0.25 档；按住 Ctrl 拖拽临时取反）");
    ImGui::SameLine();

    // ---- 中段：Play/Pause/单步（水平居中 ±2px）----
    const ImGuiStyle& st = ImGui::GetStyle();
    const float btnW = 18.0f + 8.0f + st.FramePadding.x * 2.0f; // IconButton 实宽
    const float centerW = btnW * 3.0f + st.ItemSpacing.x * 2.0f;
    const float afterLeft = ImGui::GetCursorPosX() - st.ItemSpacing.x; // SameLine 补偿
    const float centerTarget = x0 + (avail - centerW) * 0.5f;
    if (centerTarget > afterLeft + st.ItemSpacing.x)
        ImGui::SetCursorPosX(centerTarget);

    // 决议 D3：编辑态 Play 灰蓝/播放态 Stop 红调（图标底色承载态色）
    ImGui::PushStyleColor(ImGuiCol_Button,
                          playing ? theme::kPlayStop : theme::kAccentDim);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                          playing ? theme::kPlayStop : theme::kAccentDim);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                          playing ? theme::kPlayStop : theme::kAccentDim);
    if (ui::IconButton(*this, playing ? IconKind::Stop : IconKind::Play, "##play", false)) {
        if (playing) {
            if (!StopPlay()) LEMON_WARN("Stop 后快照校验失败（编辑场景已按快照重建）");
        } else if (TryEnterPlay()) {
            tabFocusPending_ = 1;
        }
    }
    ImGui::PopStyleColor(3);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", playing ? "Stop（恢复编辑场景）" : "Play（进入沙盒）");
    ImGui::SameLine();
    ImGui::BeginDisabled(!playing);
    if (ui::IconButton(*this, IconKind::Pause, "##pause", paused_)) {
        paused_ = !paused_;
        // 2026-10-01 真人验收反馈：编辑器暂停此前只冻 sim、音频照响（"点暂停还有
        // 声"）。现联动 ADR-015 M4 挂起语义（循环/BGM 挂起、Ui 组免疫）；恢复时按
        // 游戏最后 staged 意图回设（游戏自身暂停屏在场则保持挂起，不越权解挂）。
        if (paused_)
            audio_.SetPaused(true);
        else
            audio_.SetPaused(ctx_.ActiveWorld().Audio().pausedStaged());
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", "暂停/继续（仅 Play 态；音频同步挂起）");
    ImGui::SameLine();
    if (ui::IconButton(*this, IconKind::Step, "##step", false)) singleStep_ = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", "单步一帧（仅 Play 态；音频保持挂起）");
    ImGui::EndDisabled();

    // ---- 右段：Layout 下拉（M4.7d；右对齐；窄工具栏时让位不与中段重叠）----
    ImGui::SameLine();
    constexpr float kLayoutW = 150.0f;
    const float rightX = x0 + avail - kLayoutW;
    if (ImGui::GetCursorPosX() < rightX) {
        ImGui::SetCursorPosX(rightX);
        BuildLayoutDropdown();
    }
}

// ---- Layout 下拉（M4.7d）：命名布局 = imgui.ini 全量快照另存，一键切换 ----
// 切换延迟一帧到 BuildUI 的布局安全点应用（与 forceDefaultLayout_ 同点，
// DockBuilder/LoadIniSettingsFromMemory 均在帧内 dockspace 构建前调用）。
void EditorApp::BuildLayoutDropdown() {
    const std::vector<std::string> names = ListSavedLayouts();
    const char* preview = activeLayout_.empty() ? "布局：默认" : activeLayout_.c_str();
    ImGui::SetNextItemWidth(150.0f);
    if (ImGui::BeginCombo("##layout", preview)) {
        testhooks::Stash("layout.comboOpen", ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        if (ImGui::Selectable("默认布局", activeLayout_.empty())) {
            activeLayout_.clear();
            forceDefaultLayout_ = true; // 下帧 SetupDefaultLayout（帧内 DockBuilder 点）
        }
        testhooks::Stash("layout.item.default", ImGui::GetItemRectMin(),
                         ImGui::GetItemRectMax());
        for (const std::string& n : names)
            if (ImGui::Selectable(n.c_str(), n == activeLayout_)) {
                activeLayout_ = n;
                pendingLayout_ = n; // 下帧 LoadLayoutIni
            }
        ImGui::Separator();
        if (ImGui::Selectable("保存当前布局…")) {
            layoutNameBuf_ = activeLayout_;
            layoutSaveOpen_ = true;
        }
        testhooks::Stash("layout.item.save", ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        if (!activeLayout_.empty()) {
            char buf[96];
            std::snprintf(buf, sizeof(buf), "更新 \xe2\x80\x9c%s\xe2\x80\x9d",
                          activeLayout_.c_str());
            if (ImGui::Selectable(buf)) SaveLayoutIni(activeLayout_);
            std::snprintf(buf, sizeof(buf), "删除 \xe2\x80\x9c%s\xe2\x80\x9d",
                          activeLayout_.c_str());
            if (ImGui::Selectable(buf)) {
                std::error_code ec;
                std::filesystem::remove(".lemon/editor/layouts/" + activeLayout_ + ".ini", ec);
                activeLayout_.clear();
            }
        }
        ImGui::EndCombo();
    } else {
        testhooks::Stash("layout.combo", ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", "命名布局（保存/切换/删除；默认 = 内置七面板）");

    // 保存命名模态（OpenPopup 需在组合框外的稳定 ID 栈位调用）
    if (layoutSaveOpen_) {
        layoutSaveOpen_ = false;
        ImGui::OpenPopup("保存布局");
    }
    if (ImGui::BeginPopupModal("保存布局", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("布局名：");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(180);
        ImGui::InputText("##name", &layoutNameBuf_);
        testhooks::Stash("layout.nameInput", ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        ImGui::BeginDisabled(layoutNameBuf_.empty());
        if (ImGui::Button("保存", ImVec2(100, 0)) ||
            (ImGui::IsKeyPressed(ImGuiKey_Enter, false) && !layoutNameBuf_.empty())) {
            if (SaveLayoutIni(layoutNameBuf_)) {
                activeLayout_ = layoutNameBuf_;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndDisabled();
        testhooks::Stash("layout.saveBtn", ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        ImGui::SameLine();
        if (ImGui::Button("取消", ImVec2(100, 0)) ||
            ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

bool EditorApp::SaveLayoutIni(const std::string& name) {
    std::error_code ec;
    std::filesystem::create_directories(".lemon/editor/layouts", ec);
    size_t sz = 0;
    const char* ini = ImGui::SaveIniSettingsToMemory(&sz);
    if (!ini || sz == 0) return false;
    std::ofstream f(".lemon/editor/layouts/" + name + ".ini", std::ios::binary);
    if (!f) return false;
    f.write(ini, (std::streamsize)sz);
    LEMON_LOG("布局已保存：%s（%zu B）", name.c_str(), sz);
    return true;
}

bool EditorApp::LoadLayoutIni(const std::string& name) {
    std::ifstream f(".lemon/editor/layouts/" + name + ".ini", std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    const std::string ini = ss.str();
    if (ini.empty()) return false;
    ImGui::LoadIniSettingsFromMemory(ini.c_str(), ini.size());
    return true;
}

std::vector<std::string> EditorApp::ListSavedLayouts() const {
    std::vector<std::string> out;
    std::error_code ec;
    for (const auto& it : std::filesystem::directory_iterator(".lemon/editor/layouts", ec)) {
        if (!it.is_regular_file() || it.path().extension() != ".ini") continue;
        out.push_back(it.path().stem().string());
    }
    std::sort(out.begin(), out.end());
    return out;
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
        ImGui::PushStyleColor(ImGuiCol_Text, theme::kAccent);
        ImGui::TextUnformatted("| \xe2\x96\xb6 PLAY"); // ▶
        ImGui::PopStyleColor();
    }
    if (ctx_.Assets().ProjectRoot().empty()) { // M4.6 §4-1：无项目显式可见
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, theme::kTextError);
        ImGui::TextUnformatted("| 未打开项目（文件 → 新建/打开项目）");
        ImGui::PopStyleColor();
    }
    // 编译状态（M4.6 §5-5）：排队中橙字（构建阻塞期间屏幕留此帧）；完成后回显耗时
    if (compileQueued_) {
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, theme::kTextWarn);
        ImGui::TextUnformatted("| 编译中…（dotnet build）");
        ImGui::PopStyleColor();
    } else if (lastBuildMs_ >= 0.0) {
        ImGui::SameLine();
        ImGui::TextDisabled("| 上次编译 %.0fms", lastBuildMs_);
    }
}

void EditorApp::BuildNoProjectCard() {
    // 无项目引导（M4.6 §4-1，最小横幅形态——决议 R1）：中央卡两按钮直达
    // 新建/打开；有项目/向导开着不出现。用户不再需要知道 --project 的存在。
    // 可关闭（M4.7 修复：卡悬停区会截走其下 Scene 视口的点击/拖拽——视口中心
    // 恰是实体聚集区；关掉即恢复全程可编辑，会话内不再弹出）。
    if (!ctx_.Assets().ProjectRoot().empty() || wizOpen_ || picker_.IsOpen() ||
        noProjectCardDismissed_)
        return;
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.45f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(400, 0), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    const bool open = ImGui::Begin(
        "未打开项目##noproject", nullptr,
        ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar);
    if (open) {
        ImGui::Dummy(ImVec2(0, 6));
        ImGui::TextUnformatted("  尚未打开项目");
        ImGui::SameLine();
        if (ImGui::SmallButton("×##dismiss")) noProjectCardDismissed_ = true;
        ImGui::TextDisabled("  导入资产、脚本编译、场景保存都需要项目目录。");
        ImGui::Dummy(ImVec2(0, 8));
        if (ImGui::Button("新建项目…", ImVec2(-1, 0))) MenuNewProject();
        if (ImGui::Button("打开项目…", ImVec2(-1, 0))) MenuOpenProject();
        ImGui::Dummy(ImVec2(0, 4));
        if (!recentProjects_.empty()) {
            ImGui::TextDisabled("  最近：");
            namespace fsr = std::filesystem;
            for (const std::string& p : recentProjects_) {
                ImGui::PushID(p.c_str());
                std::error_code ec;
                if (fsr::is_regular_file(fsr::path(p) / "project.lemon", ec) && !ctx_.dirty) {
                    if (ImGui::SmallButton(fsr::path(p).filename().c_str())) {
                        if (OpenProjectInSession(p)) LEMON_LOG("已打开最近项目：%s", p.c_str());
                    }
                }
                ImGui::PopID();
            }
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

void EditorApp::BuildShortcuts() {
    // §2.3 键位：输入框聚焦（WantTextInput）时全部屏蔽（IME 冒烟检查项）
    if (ImGui::GetIO().WantTextInput) return;
    // 键仲裁（动画工作台 v3）：持有焦点的面板声明捕获时，实体级 Delete/Ctrl+D
    // 跳过——这两键在 Animation 面板内是帧操作，双触发会顺手删掉场景选中实体
    bool panelCapturesKeys = false;
    for (auto& e : panels_.Entries())
        if (e.open && e.panel->CapturesGlobalKeys()) {
            panelCapturesKeys = true;
            break;
        }
    if (!picker_.IsOpen()) {
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S) && !playing_) MenuSaveScene();
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_O) && !playing_) MenuOpenScene();
        if (!panelCapturesKeys && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_D)) {
            // C8：按"选中子树的根"复制（祖先也在选中集内的跳过，同 CopySelection
            // 过滤——此前只取 Primary 单体，选父子链只得根；DuplicateEntity 已
            // 子树化）。根先收集再复制：迭代 Selection() 中调 Select() 会改选择
            // 集容器（追加触发重分配 = 迭代器失效）。结构轨快照同前
            const std::string before = ctx_.SnapshotSceneJson();
            ecs::Scene& s = ctx_.ActiveScene();
            std::vector<ecs::Entity> roots;
            for (ecs::Entity e : ctx_.Selection()) {
                if (e.IsNull() || !s.Alive(e)) continue;
                bool ancestorSelected = false;
                for (ecs::Entity a = e;;) {
                    const ecs::Hierarchy* h = s.TryGet<ecs::Hierarchy>(a);
                    if (!h || h->parent.IsNull() || !s.Alive(h->parent)) break;
                    a = h->parent;
                    if (ctx_.IsSelected(a)) {
                        ancestorSelected = true;
                        break;
                    }
                }
                if (!ancestorSelected) roots.push_back(e);
            }
            bool any = false;
            for (ecs::Entity e : roots) {
                ecs::Entity copy = ctx_.DuplicateEntity(e);
                if (!copy.IsNull()) {
                    ctx_.Select(copy, any);
                    any = true;
                }
            }
            if (any && !ctx_.Playing()) ctx_.PushStructuralUndo("复制实体", before);
        }
        // M4.6 §5-1：复制/粘贴（Edit 态专属——Undo 结构轨在 Play 禁用）；Ctrl+D 保留
        if (!ctx_.Playing() && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_C))
            CopySelection();
        if (!ctx_.Playing() && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_V))
            PasteClipboard();
        if (!panelCapturesKeys && ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
            // 结构轨：删除前快照（此前漏推——Del 键删完 Ctrl+Z 无效，与右键
            // "删除 (Del)" 菜单不对称；smoke-ui 真人链路抓到）
            const std::string before = ctx_.SnapshotSceneJson();
            // 先拷贝再迭代：DestroyEntityTree 内部 PruneSelection 会换掉
            // selection_ 底层缓冲（局部 keep 交换后析构），range-for 缓存的
            // begin/end 即悬空——多选删除偶发漏删（与 Ctrl+D 路径同型的漏修）
            const std::vector<ecs::Entity> sel = ctx_.Selection();
            bool any = false;
            for (ecs::Entity e : sel) {
                ctx_.DestroyEntityTree(e);
                any = true;
            }
            ctx_.ClearSelection();
            if (any && !ctx_.Playing()) ctx_.PushStructuralUndo("删除实体", before);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Q)) tool_ = EditTool::Select;
        if (ImGui::IsKeyPressed(ImGuiKey_W)) tool_ = EditTool::Move;
        if (ImGui::IsKeyPressed(ImGuiKey_E)) tool_ = EditTool::Rotate;
        if (ImGui::IsKeyPressed(ImGuiKey_R)) tool_ = EditTool::Scale;
        if (!ctx_.Playing() && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Z)) {
            if (!ctx_.Undo().Undo()) LEMON_LOG("Undo：栈空");
        }
        if (!ctx_.Playing() && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Y))
            ctx_.Undo().Redo();
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_P)) {
            if (ctx_.Playing()) {
                StopPlay();
            } else if (TryEnterPlay()) {
                tabFocusPending_ = 1;
            }
        }
    }
}

void EditorApp::BuildUI() {
    playing_ = ctx_.Playing(); // 冗余显示态每帧对齐真值（菜单/快捷键/横幅守卫共用；
                               // 失同步曾致 Play 中 Ctrl+S 把 Play 世界存进编辑场景）
    if (tabFocusPending_ != 0) { // Play 进出自动切 Game/Scene 标签页（Unity 心智；F1 手测）
        // 目标窗口未建（冒烟首帧：默认布局在本帧更晚的 DockBuilderGetNode 分支才
        // 建）→ 标志留到下帧再翻，不清零
        const char* want = tabFocusPending_ > 0 ? "Game" : "Scene";
        ImGuiWindow* w = ImGui::FindWindowByName(want);
        if (w && (!w->DockNode || !w->DockNode->TabBar)) w = nullptr; // tab 栏未建同理等
        if (w) {
            ImGui::SetWindowFocus(want);
            // 批③b：FocusWindow 的 dock tab 选择段被上游注释（imgui #2304——
            // "avoid applying focus immediately before the tabbar is visible"），
            // 无头会话里 SetWindowFocus 只改 nav 不翻标签页（真人点击走交互路径
            // 才翻）。此处补上被注释逻辑的等价操作：显式选中目标 tab
            w->DockNode->TabBar->NextSelectedTabId = w->TabId;
            tabFocusPending_ = 0;
        }
    }
    // 性能批②：仅注入会话登记矩形（smoke-ui 全面板；smoke-anim 只为热修③的
    // 新建集模态点击位——登记面 = 面板侧 Stash 调用点，无面板登记则零成本）
    testhooks::SetEnabled(launchCopy_.smokeUi || launchCopy_.smokeAnim);
    testhooks::ClearAll(); // 注入会话矩形登记每帧重建（防陈旧矩形误导注入）
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
                     ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_MenuBar |
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar(3);

    BuildMenuBar();

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 4));
    if (ImGui::BeginChild("##Toolbar",
                          ImVec2(0.0f, ImGui::GetFrameHeightWithSpacing() + 4.0f))) {
        BuildToolbar();
        if (playing_) { // Play 亮蓝横幅（§2.4；沙盒 M4.3 生效；M4.7a 旧橙改主题蓝）
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, theme::kAccent);
            ImGui::TextUnformatted("PLAY MODE — 编辑落 Play World，Stop 即丢；GameView 聚焦时键鼠进游戏");
            ImGui::PopStyleColor();
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();

    const float statusBarH = ImGui::GetFrameHeightWithSpacing();
    ImGuiID dock = ImGui::GetID("LemonDockSpace");
    if (forceDefaultLayout_) { // smoke-drag：忽略 ini 漂移，强制默认布局（一次）
        forceDefaultLayout_ = false;
        SetupDefaultLayout();
    }
    if (!pendingLayout_.empty()) { // M4.7d Layout 下拉切换：帧内安全点应用
        if (!LoadLayoutIni(pendingLayout_)) {
            LEMON_WARN("布局加载失败：%s（文件缺失？回到默认）", pendingLayout_.c_str());
            activeLayout_.clear();
        }
        pendingLayout_.clear();
    }
    if (ImGui::DockBuilderGetNode(dock) == nullptr) SetupDefaultLayout();
    ImGui::DockSpace(dock, ImVec2(0.0f, ImGui::GetContentRegionAvail().y - statusBarH),
                     ImGuiDockNodeFlags_None);

    if (ImGui::BeginChild("##StatusBar", ImVec2(0.0f, statusBarH),
                          ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
        BuildStatusBar();
    ImGui::EndChild();
    ImGui::End();

    for (auto& e : panels_.Entries())
        if (e.open) e.panel->OnGui(*this);

    BuildNoProjectCard();
    BuildPickersAndModals();
    DrawAudioMixerWindow(); // M6c 批③：按需工具窗（默认关——零默认布局影响）
    DrawOrphanSweepReportWindow(); // 孤儿 .meta 清扫报告（默认关，随菜单动作开）

    if (launchCopy_.demoWindow) ImGui::ShowDemoWindow(&launchCopy_.demoWindow);
    if (aboutOpen_) {
        if (ImGui::Begin("About Lemon Editor", &aboutOpen_, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Lemon Editor — M4（ImGui %s / docking）", IMGUI_VERSION);
            ImGui::TextUnformatted("纯 2D 高性能游戏引擎：C++20 + Vulkan + C# 脚本");
            ImGui::TextUnformatted("规划：docs/Plans/M4/M4.md");
        }
        ImGui::End();
    }
}

// Audio Mixer 按需工具窗（M6c 批③）：Master/三组音量直写引擎（钳界引擎侧已有），
// voice 计数与静音降级态是观测面（静音模式逻辑记账同有效，批⓪ 口径）。节流/微扰
// = 两轮听感热修（2026-10-01）参数的全局调参台——per-资产覆写缓议（批文件裁定），
// 真实调参需求出现时以微批启。无持久化：设置屏音量档归批④（Settings 档）。
void EditorApp::DrawAudioMixerWindow() {
    if (!audioMixerOpen_) return;
    if (!ImGui::Begin("Audio Mixer", &audioMixerOpen_, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }
    namespace ag = lemon::audio;
    static constexpr const char* kGroupNames[ag::kGroupCount] = {"Bgm", "Sfx", "Ui"};

    // 状态行：设备/降级 + 声部占用（试听与 Play 世界同引擎——单实例单真相）
    if (audio_.silent()) {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::kTextWarn);
        ImGui::TextUnformatted("静音模式（无设备/LEMON_AUDIO=off 降级，逻辑声部照常记账）");
        ImGui::PopStyleColor();
    } else {
        ImGui::TextUnformatted("设备输出正常");
    }
    ImGui::SameLine();
    ImGui::TextDisabled("声部 %d/%d", audio_.ActiveVoiceCount(), ag::kMaxVoices);

    float v = audio_.MasterVolume();
    if (ImGui::SliderFloat("Master", &v, 0.0f, 1.0f, "%.2f")) audio_.SetMasterVolume(v);
    for (int i = 0; i < ag::kGroupCount; ++i) {
        const ag::Group g = static_cast<ag::Group>(i);
        float gv = audio_.GroupVolume(g);
        if (ImGui::SliderFloat(kGroupNames[i], &gv, 0.0f, 1.0f, "%.2f"))
            audio_.SetGroupVolume(g, gv);
    }
    if (ImGui::Button("全部停止")) audio_.StopAll(); // 试听/残留声部急停（Play 世界同引擎）
    ImGui::SameLine();
    ImGui::TextDisabled("Edit 试听与 Play 共用本引擎");

    ImGui::Separator();
    ImGui::TextUnformatted("重触发治理（全局默认；per-资产缓议）");
    float cd = audio_.RetriggerCooldown();
    if (ImGui::SliderFloat("同 clip 节流", &cd, 0.0f, 0.2f, "%.3fs"))
        audio_.SetRetriggerCooldown(cd);
    float pj = audio_.PitchJitter();
    if (ImGui::SliderFloat("音高微扰", &pj, 0.0f, 0.1f, "%.3f")) audio_.SetPitchJitter(pj);
    ImGui::End();
}

// 孤儿 .meta 清扫报告（2026-10-01 拍板，Assets 菜单动作的结果面）：清了什么 /
// 留了什么为什么留。不盲清被引用项——那是"只恢复源文件"场景的复链钩子，
// 引用面判据见 AssetDatabase::SweepOrphanMetas。
void EditorApp::DrawOrphanSweepReportWindow() {
    if (!orphanSweepReportOpen_) return;
    if (!ImGui::Begin("孤儿 .meta 清理报告", &orphanSweepReportOpen_,
                      ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }
    ImGui::Text("已清理（源已删且零引用）：%u",
                (uint32_t)orphanSweepResult_.cleaned.size());
    for (const std::string& p : orphanSweepResult_.cleaned) ImGui::BulletText("%s", p.c_str());
    ImGui::Separator();
    if (orphanSweepResult_.keptReferenced.empty()) {
        ImGui::TextDisabled("无被引用残留");
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::kTextWarn);
        ImGui::Text("保留（guid 仍被引用——恢复源文件即可复链，确弃请先清引用）：%u",
                    (uint32_t)orphanSweepResult_.keptReferenced.size());
        ImGui::PopStyleColor();
        for (const std::string& p : orphanSweepResult_.keptReferenced)
            ImGui::BulletText("%s", p.c_str());
    }
    ImGui::Separator();
    if (ImGui::Button("关闭")) orphanSweepReportOpen_ = false;
    ImGui::End();
}

void EditorApp::BuildPickersAndModals() {
    // 快捷目录（M4.6 §5-7）：Home + 当前项目根（打开期间每帧刷新——切项目后随动）
    if (picker_.IsOpen()) {
        std::vector<std::pair<std::string, std::string>> qd;
        if (const char* home = std::getenv("HOME")) qd.push_back({"Home", home});
        const std::string& root = ctx_.Assets().ProjectRoot();
        if (!root.empty()) qd.push_back({"项目", root});
        picker_.SetQuickDirs(std::move(qd));
    }
    // 文件选择器（打开/另存/导入共用；动作一次性返回）
    if (PickerResult r = picker_.Draw(); r.action != PickerAction::None) {
        if (r.action == PickerAction::Open || r.action == PickerAction::Save) {
            if (pickerMode_ == PickerMode::Open) {
                if (!ctx_.OpenScene(r.path)) LEMON_WARN("打开失败：%s", r.path.c_str());
            } else if (pickerMode_ == PickerMode::Save) {
                if (ctx_.SaveScene(r.path)) LEMON_LOG("已另存为：%s", r.path.c_str());
            } else if (pickerMode_ == PickerMode::Import) { // 复制进 Assets/ 根 + 登记导入（M4.4）
                std::filesystem::path src(r.path);
                if (const AssetEntry* e = ctx_.Assets().ImportFile(
                        r.path, src.filename().string())) {
                    if (e->type == AssetType::Sprite) gpuAssets_.ImportSprite(*e);
                    LEMON_LOG("已导入：%s（guid %016llx）", e->relPath.c_str(),
                              (unsigned long long)e->guid);
                }
            } else if (pickerMode_ == PickerMode::OpenProject) {
                // M4.6 §4-2：目录选择模式——选项目根目录，校验 project.lemon 在内
                namespace fs = std::filesystem;
                std::error_code ec;
                if (!fs::is_regular_file(fs::path(r.path) / "project.lemon", ec)) {
                    LEMON_WARN("打开项目失败：%s 下没有 project.lemon（应选项目根目录）",
                               r.path.c_str());
                } else {
                    OpenProjectInSession(r.path);
                }
            } else { // WizardDir：向导父目录浏览（M4.6 §4-3；回填后重开向导模态）
                std::snprintf(wizParent_, sizeof(wizParent_), "%s", r.path.c_str());
                wizOpen_ = true;
            }
        }
    }

    // 退出确认（dirty 场景）：保存 / 丢弃 / 取消
    if (quitConfirmOpen_) {
        ImGui::OpenPopup("未保存更改");
        quitConfirmOpen_ = false;
        quitConfirmArmed_ = true;
    }    if (ImGui::BeginPopupModal("未保存更改", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        // SceneOp（打开/新建场景、切项目前的脏确认）与退出分流：前者保存/丢弃后续做
        // 挂起操作（M4.2 欠账——此前按钮硬编码"并退出"，选了就把整个编辑器关了）
        const bool exiting = confirmContext_ == ConfirmContext::Exit;
        ImGui::Text("场景 %s 有未保存更改。", ctx_.SceneName().c_str());
        ImGui::Separator();
        auto runPending = [&]() {
            const PendingSceneOp op = pendingSceneOp_;
            pendingSceneOp_ = PendingSceneOp::None;
            confirmContext_ = ConfirmContext::Exit;
            switch (op) { // dirty 已清，各入口直通（选择器/新场景）
                case PendingSceneOp::OpenScene: MenuOpenScene(); break;
                case PendingSceneOp::NewScene: MenuNewScene(); break;
                case PendingSceneOp::OpenProject: MenuOpenProject(); break;
                case PendingSceneOp::RecentScene: // M4.8-b：路径已持有，无需选择器
                    if (!pendingScenePath_.empty()) ctx_.OpenScene(pendingScenePath_);
                    pendingScenePath_.clear();
                    break;
                default: break;
            }
        };
        if (ImGui::Button(exiting ? "保存并退出" : "保存", ImVec2(140, 0))) {
            if (ctx_.ScenePath().empty()) {
                // 无路径：走另存为；完成后由用户重触发（与退出路径同款简化环）
                ImGui::CloseCurrentPopup();
                quitConfirmArmed_ = false;
                pendingSceneOp_ = PendingSceneOp::None;
                pendingScenePath_.clear();
                confirmContext_ = ConfirmContext::Exit;
                exitRequested_ = false; // 等另存完成由用户再关（简化环）
                MenuSaveSceneAs();
            } else {
                ctx_.SaveScene();
                ImGui::CloseCurrentPopup();
                quitConfirmArmed_ = false;
                if (exiting) forceExit_ = true;
                else runPending();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(exiting ? "丢弃并退出" : "丢弃", ImVec2(140, 0))) {
            ctx_.dirty = false; // 丢弃 = 放弃未存改动（盘档不动）
            ImGui::CloseCurrentPopup();
            quitConfirmArmed_ = false;
            if (exiting) forceExit_ = true;
            else runPending();
        }
        ImGui::SameLine();
        if (ImGui::Button("取消", ImVec2(140, 0))) {
            ImGui::CloseCurrentPopup();
            quitConfirmArmed_ = false;
            pendingSceneOp_ = PendingSceneOp::None;
            pendingScenePath_.clear();
            confirmContext_ = ConfirmContext::Exit;
            exitRequested_ = false;
        }
        ImGui::EndPopup();
    }

    // Play 阻断（2026-09-22）：Game/ 编译失败（宿主未装配）时阻止进 Play——
    // 对齐 Unity/Godot。修错保存 → watcher 自动首装即解除；模态内亦可一键重试。
    if (playBlockedOpen_) {
        ImGui::OpenPopup("脚本未就绪");
        playBlockedOpen_ = false;
    }
    if (ImGui::BeginPopupModal("脚本未就绪", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(
            "Game/ 脚本编译失败，已阻止进入 Play。\n"
            "（防止\"游戏在跑但脚本没生效\"的隐性 bug）\n"
            "错误详情见 Console 红字；修复保存后将自动重新编译装配。");
        ImGui::Separator();
        if (ImGui::Button("重新编译并进入 Play", ImVec2(210, 0))) {
            ImGui::CloseCurrentPopup();
            if (TryHotReloadScripts("Play 阻断重试")) {
                // review 2026-10-02 #9：统一走 TryEnterPlay——此前直调 ctx_.EnterPlay
                // 漏 MountPlayAudio/WirePlayAudioBackend 双挂点，该路径整段无声
                //（新 World 的 audioSink_ 为 null，C# 音频命令纯记账）
                if (TryEnterPlay())
                    tabFocusPending_ = 1;
            } else if (!host_) {
                playBlockedOpen_ = true; // 仍失败：重开模态（新错误已进 Console）
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("取消", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // M4.5 新建项目向导（blank/vs-survivor 模板；06 §1 布局 + §7 模板）
    if (wizOpen_) ImGui::OpenPopup("新建项目");
    if (ImGui::BeginPopupModal("新建项目", &wizOpen_, ImGuiWindowFlags_AlwaysAutoResize)) {
        static int wizTemplate = 0; // 0 blank / 1 vs-survivor（M5 批④模板整合）
        const char* wizTemplates[] = {"blank（空场景起步）", "vs-survivor（幸存者完整玩法）"};
        ImGui::SetNextItemWidth(320);
        ImGui::Combo("模板", &wizTemplate, wizTemplates, 2);
        if (wizTemplate == 0)
            ImGui::TextUnformatted("blank：Assets/Scenes/Prefabs/Game/Data + 种子资产 +\n"
                                   "可编译脚本工程（零配置直接 Play）");
        else
            ImGui::TextDisabled("%s", "vs-survivor：玩家/波次导演/三选一/HUD/存档全套\n"
                                      "（yami 素材随行，MIT——见模板 README）");
        ImGui::SetNextItemWidth(320);
        ImGui::InputText("项目名", wizName_, sizeof(wizName_));
        ImGui::SetNextItemWidth(320);
        ImGui::InputText("父目录（绝对路径）", wizParent_, sizeof(wizParent_));
        ImGui::SameLine();
        if (ImGui::Button("浏览…")) { // M4.6 §4-3：目录选择器（零手敲路径）
            std::error_code ec;
            std::string start =
                wizParent_[0] && std::filesystem::is_directory(wizParent_, ec)
                    ? std::string(wizParent_)
                    : (std::getenv("HOME") ? std::getenv("HOME") : ".");
            pickerMode_ = PickerMode::WizardDir;
            wizOpen_ = false; // 模态不叠加：关向导开选择器，选定即回填重开
            ImGui::CloseCurrentPopup();
            picker_.OpenDir("选择父目录", start);
        }
        ImGui::Separator();
        ImGui::BeginDisabled(!wizName_[0] || !wizParent_[0]);
        if (ImGui::Button("创建并打开", ImVec2(160, 0))) {
            ProjectDesc d;
            d.parentDir = wizParent_;
            d.name = wizName_;
#ifdef LEMON_SCRIPT_DIR
            d.sdkDir = LEMON_SCRIPT_DIR;
            d.engineVersion = "0.4.0-m4";
            if (wizTemplate == 1) { // M5 批④：模板分支（复制 + 重锚）
                d.templateName = "vs-survivor";
                d.templateDir = std::string(LEMON_TEMPLATE_DIR) + "/vs-survivor";
            }
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

    // 新建脚本（M4.6 §5-4）：类名 → 模板 .cs 落 Game/ + GameMain 注册行 → 热重载排队
    // → watcher 自动接手（新类型编译后即可挂到实体）
    if (newScriptOpen_) ImGui::OpenPopup("新建脚本");
    if (ImGui::BeginPopupModal("新建脚本", &newScriptOpen_, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("模板 .cs 落 Game/，并在 GameMain.cs 自动注册；\n"
                               "创建后自动热重载，新类型立即可挂到实体。");
        ImGui::SetNextItemWidth(280);
        ImGui::InputText("类名", newScriptName_, sizeof(newScriptName_));
        ImGui::BeginDisabled(!newScriptName_[0]);
        if (ImGui::Button("创建并编译", ImVec2(160, 0))) {
            const std::string gameDir = ctx_.Assets().ProjectRoot() + "/Game";
            if (ProjectWizard::AddBehaviourScript(gameDir, newScriptName_)) {
                LEMON_LOG("新脚本已建：Game/%s.cs（注册行已插，热重载排队）", newScriptName_);
                ScriptSourceChanged(); // 吸收基线（编译走队列；watcher 不再二次重编）
                QueueScriptRebuild("新建脚本");
                newScriptOpen_ = false;
                ImGui::CloseCurrentPopup();
            } else {
                LEMON_WARN("新建脚本失败：类名非法或 Game/%s.cs 已存在", newScriptName_);
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("取消", ImVec2(120, 0))) {
            newScriptOpen_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    DrawRecoveryModal();
}

} // namespace lemon::editor
