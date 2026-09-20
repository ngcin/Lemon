// Lemon 编辑器 — 应用壳（M4-Editor-Plan §3.1：EditorEntry/主循环/帧节奏/布局持久化）
// 主循环 = anim-smoke 全链基线外层套 ImGui 帧节奏（M4.0 壳 + UI；World/场景 M4.1 起）。
#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "Assets/AssetGpuCache.h"
#include "Assets/FileWatcher.h"
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
namespace scripting {
class ScriptHost;
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
    std::string projectDir;  // --project（M4.4 生效：AssetDatabase 根；空 = cwd 当项目）
    std::string openScene;   // --scene：启动即打开的 .scene（冒烟/CLI 用）
    std::string saveScene;   // --save-scene：场景就绪后保存并退出（CLI roundtrip 验收）
    bool playTest = false;   // --play：冒烟内进/出 Play 往返（逐字节断言 + 计时，验收 #4/#5）
    std::string script;      // --script：用户脚本程序集（C# 装配通路；空 = 无脚本宿主）
    bool finalTest = false;  // --final：M4.5 终验（向导建项目→判据场景→Play/热重载量化）
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
    AssetGpuCache& AssetGpu() { return gpuAssets_; }
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

    // ---- 资产动作（M4.4）----
    void MenuImportAsset();  // 文件选择器（复制进 Assets/ + 导入）
    void RescanAssets();     // FileWatcher/手动重扫 → DB + GPU 增量导入

    // ---- 项目/脚本动作（M4.5）----
    /// 打开项目全管线（DB/GPU 导入/watcher/Game 编译 + 脚本宿主）；向导与 --project 共用
    bool OpenProjectPipeline(const std::string& projectRoot);
    /// 装配脚本宿主（dll 绝对路径；失败清 host 并告警）。--script 与项目 Game/ 共用
    bool InitScriptHostFrom(const std::string& dllAbs);
    /// 编译 Game/ + 整域换装 + 双世界重装配 + 计时/泄漏红字（M4-Editor-Plan §3.7）
    bool TryHotReloadScripts(const char* reason);
    /// 手动触发（菜单"重新编译脚本"）
    void MenuRebuildScripts();
    /// 新建项目向导模态（名字 + 父目录 → blank 模板 → 直接打开）
    void MenuNewProject();
    double LastHotReloadMs() const { return hotReloadMs_; }
    int HotReloadCount() const;

private:
    void BuildUI();          // 菜单栏/工具栏/dockspace/状态栏/面板/模态
    void BuildMenuBar();
    void BuildToolbar();
    void BuildStatusBar();
    void BuildShortcuts();   // Ctrl+S/O/D、Delete（输入框聚焦时屏蔽）
    void BuildPickersAndModals();
    void SetupDefaultLayout();
    void SeedSmokeScene();   // 冒烟播种：父子链 + 常用组件（面板验收有内容）
    void SeedSmokeProject(); // 冒烟播种：临时项目 + 预置 PNG（固定 guid，M4.4 资产链验收）
    /// M4.5 终验判据场景（向导项目上零代码搭"走地图+刷怪"）：地图/角色/刷怪器
    void SeedJudgementScene(uint64_t spawnGuid);
    /// 换装判定辅助：Game/*.csproj 路径 + 输出 dll（项目根/.lemon/bin/<名>.dll）
    bool FindGameProject(std::string& csproj, std::string& dll);
    /// Game/ 源码变更检测（FileWatcher 置脏后过滤 .cs，排除 obj/bin）
    bool ScriptSourceChanged();
    /// 崩溃恢复提示模态（启动检测 autosave 新于盘档 → 恢复/忽略）
    void DrawRecoveryModal();

    enum class PickerMode { Open, Save, Import };
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
    std::unique_ptr<scripting::ScriptHost> host_; // C# 宿主（--script/项目 Game；null = 无）
    AssetGpuCache gpuAssets_;
    FileWatcher watcher_;
    FileWatcher scriptWatcher_;      // Game/ 源码（M4.5 热重载触发）
    std::vector<std::unique_ptr<IEditorPanel>> ownedPanels_;
    PanelRegistry panels_;

    // 热重载状态（§3.7）
    double hotReloadMs_ = 0.0;       // 最近一次 编译+换装+重装配 总耗时（≤2s 判定）
    int64_t lastHandledCsWrite_ = 0; // 上次已处理的 .cs 新写时间戳（去重/防抖）
    double reloadDebounceUntil_ = 0.0;
    bool imguiIdConflictSeen_ = false; // 悬停扫掠命中过 ID 冲突（一次性计数）

    // 向导/恢复模态状态
    char wizName_[64] = {};
    char wizParent_[512] = ".";
    bool wizOpen_ = false;
    std::string recoveryPath_;       // 非空 = 检测到可恢复快照（DrawRecoveryModal 消费）
    bool recoveryAnswered_ = false;  // 冒烟终验：已自动答复
    uint64_t wizardSpawnGuid_ = 0;   // 终验：种子资产 guid（判据场景装配用）

    // 终验计量（--final）
    double finalPlayMinFps_ = 1e9;
    double finalPlayReloadMs_ = 0.0;    // Play 中换装耗时（§6 #2 双态口径）
    uint32_t finalAliveAtReload_ = 0;
    uint64_t finalReloadFrame_ = ~0ull; // 换装帧（fps 统计剔除窗口）
    bool finalPlayReloadOk_ = false, finalEditReloadOk_ = false;
    int finalStateBagTotal_ = -1;

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
