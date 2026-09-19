// Lemon 编辑器 — 应用壳（M4-Editor-Plan §3.1：EditorEntry/主循环/帧节奏/布局持久化）
// 主循环 = anim-smoke 全链基线外层套 ImGui 帧节奏（M4.0 壳 + UI；World/场景 M4.1 起）。
#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "EditorContext.h"
#include "Panels/Panel.h"
#include "Tooling/EditorLog.h"
#include "Tooling/FilePicker.h"

namespace lemon {
class Window;
namespace rhi {
class Device;
class CommandList;
}
}

namespace lemon::editor {

class ImGuiBackend;
class ViewportRenderer;

struct EditorLaunch {
    int frames = 0;          // 0 = 无限；>0 = 跑 N 帧退出（冒烟）
    bool validate = false;   // Vulkan 验证层（编辑器改动默认开，AGENTS 纪律）
    bool smoke = false;      // 退出前自检断言 + 汇总打印（M4-Editor-Plan §6 #13 提案）
    bool demoWindow = false; // 叠加 Dear ImGui Demo（冒烟画面丰富度/手动排障）
    std::string screenshot;  // 非空 = 末帧截屏写 PNG（stb_image_write）
    std::string projectDir;  // --project（M4.1 起生效；M4.0 仅显示）
    std::string openScene;   // --scene：启动即打开的 .scene（冒烟/CLI 用）
    std::string saveScene;   // --save-scene：场景就绪后保存并退出（CLI roundtrip 验收）
    bool playTest = false;   // --play：冒烟内进/出 Play 往返（逐字节断言 + 计时，验收 #4/#5）
};

/// 工具标识（工具栏 W/E/R 三态 + 后续模式栈的地基）
enum class EditTool : uint8_t { Move, Rotate, Scale };

class EditorApp {
public:
    EditorApp();
    ~EditorApp();
    int Run(const EditorLaunch& launch); // 返回进程退出码（smoke 断言失败 = 1）

    // ---- 面板可读的共享状态（EditorContext = §3.3 状态模型）----
    EditorContext& Ctx() { return ctx_; }
    EditorLogRing& Log() { return log_; }
    ImGuiBackend& Ui() { return *ui_; }
    rhi::Device& Device() { return *device_; }
    ViewportRenderer& Viewport() { return *viewport_; }
    bool Playing() const { return playing_; } // Play 沙盒 M4.3 接入
    EditTool Tool() const { return tool_; }
    bool GridSnap() const { return gridSnap_; }
    void RequestExit() { exitRequested_ = true; }
    void SetGameViewFocused(bool f) { gameViewFocused_ = f; }
    const EditorLaunch& Launch() const { return *launch_; }

    // ---- 场景 IO 动作（菜单/快捷键共用；File 状态机内聚于此）----
    void MenuNewScene();
    void MenuOpenScene();
    void MenuSaveScene();    // 无路径 → 转 SaveAs
    void MenuSaveSceneAs();
    bool ConfirmUnsaved();   // dirty 时弹确认框；返回 false = 用户取消

private:
    void BuildUI();          // 菜单栏/工具栏/dockspace/状态栏/面板/模态
    void BuildMenuBar();
    void BuildToolbar();
    void BuildStatusBar();
    void BuildShortcuts();   // Ctrl+S/O/D、Delete（输入框聚焦时屏蔽）
    void BuildPickersAndModals();
    void SetupDefaultLayout();
    void SeedSmokeScene();   // 冒烟播种：父子链 + 常用组件（面板验收有内容）

    enum class PickerMode { Open, Save };
    enum class ConfirmContext { Exit, SceneOp };

    EditorLaunch launchCopy_;
    EditorLaunch* launch_ = nullptr;
    EditorContext ctx_;
    EditorLogRing log_;
    FilePicker picker_;
    PickerMode pickerMode_ = PickerMode::Open;
    std::unique_ptr<Window> window_;
    std::unique_ptr<rhi::Device> device_;
    std::unique_ptr<ImGuiBackend> ui_;
    std::unique_ptr<class ViewportRenderer> viewport_;
    std::vector<std::unique_ptr<IEditorPanel>> ownedPanels_;
    PanelRegistry panels_;

    // 退出/未保存确认状态机
    bool playing_ = false; // 冗余显示态（真值 = ctx_.Playing()）
    bool paused_ = false;
    bool singleStep_ = false;
    bool gameViewFocused_ = false; // GameView 输入门控（§3.6）
    EditTool tool_ = EditTool::Move;
    bool gridSnap_ = true;
    bool exitRequested_ = false;
    bool forceExit_ = false;       // 确认模态放行退出
    bool quitConfirmOpen_ = false; // 本帧打开模态
    bool quitConfirmArmed_ = false; // 模态已打开（防重复弹）
    ConfirmContext confirmContext_ = ConfirmContext::Exit;
    bool aboutOpen_ = false;
    uint32_t smokeSeeded_ = 0;     // 冒烟播种实体数（退出时守恒断言）
};

} // namespace lemon::editor
