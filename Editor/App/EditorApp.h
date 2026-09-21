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
class SceneViewPanel;
class AssetBrowserPanel;

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
    bool noReopen = false;   // --no-reopen：跳过"自动重开上次项目"（M4.6 §4-4）
    std::string smokeClose;  // --smoke-close clean|dirty：关闭状态机交互冒烟（M4.6 §4-9）
    bool smokeDrag = false;  // --smoke-drag：视口拖拽注入冒烟（M4.7c 交互回归）
    bool smokeUi = false;    // --smoke-ui：真人会话注入冒烟（快捷键/Undo/保存/Play/重命名/
                             // 挂父子/目录导航/命名布局；M4.7d 收尾轮）
};

/// 工具标识（Q 选择 / W 移动 / E 旋转 / R 缩放；Godot 式 Select 模式 = 8 向手柄）
enum class EditTool : uint8_t { Select = 0, Move, Rotate, Scale };

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
    bool GridVisible() const { return gridVisible_; } // 网格显示（纯视觉）
    bool SnapEnabled() const { return snapEnabled_; } // 拖拽吸附（默认关；Ctrl 临时取反）
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
    /// 会话内打开项目（选 project.lemon → 全管线切换 + 新建场景）
    void MenuOpenProject();
    /// 切项目落地（管线 + 新场景；Play/脏场景守卫在调用方）——选择器/最近项目共用
    bool OpenProjectInSession(const std::string& root);
    double LastHotReloadMs() const { return hotReloadMs_; }
    int HotReloadCount() const;

private:
    void BuildUI();          // 菜单栏/工具栏/dockspace/状态栏/面板/模态
    void BuildMenuBar();
    void BuildToolbar();
    void BuildStatusBar();
    // ---- Layout 下拉（M4.7d：命名布局快照 .lemon/editor/layouts/*.ini）----
    void BuildLayoutDropdown();          // 工具栏右段：切换/保存/更新/删除
    bool SaveLayoutIni(const std::string& name); // 当前布局 → 命名 ini
    bool LoadLayoutIni(const std::string& name); // 命名 ini → 应用（帧内安全点）
    std::vector<std::string> ListSavedLayouts() const;
    void BuildShortcuts();   // Ctrl+S/O/D、Delete（输入框聚焦时屏蔽）
    void BuildPickersAndModals();
    void BuildNoProjectCard(); // 无项目引导（M4.6 §4-1：中央卡 + 两按钮直达）
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
    /// Play 中游戏相机跟随（M4.7 手测修复）：tag "Camera"（显式相机实体）>
    /// tag "Player" > 首个挂脚本实体；进 Play 首帧吸附、之后阻尼跟随；退出回默认位。
    void UpdateGameCameraFollow(float dt);

    // ---- LEMON_PLAY_DIAG=1：Play 相机手感诊断（手测第九轮）----
    /// 逐帧打印墙钟帧耗时/相机中心/跟随目标/gameRT 尺寸；f60-120 自动注入 D 键
    /// （走→停复现）。用于把"抖动"归因到 帧节奏/RT 重建/相机数学/写回时序 之一。
    bool playDiag_ = false;
    std::chrono::steady_clock::time_point playDiagPrev_{};
    Vec2 playDiagTarget_{};
    bool playDiagHasTarget_ = false;
    bool playDiagPlayingSeen_ = false;

    // ---- M4.6b（§5 日常编辑效率）----
    void CopySelection();    // §5-1：选中子树（多根）→ 内部剪贴板（树 JSON + 根位置）
    void PasteClipboard();   // §5-1：粘贴（结构完整；根 +24/+24 相对偏移；进 Undo）
    void ImportDroppedFile(const std::string& absPath); // §5-3：OS drop → 当前资产目录
    void QueueScriptRebuild(const char* reason);        // §5-5：编译排队（先画一帧"编译中…"）
    void LogCompileErrors(const std::string& dotnetOutput); // §5-6：CSxxxx 红字

    enum class PickerMode { Open, Save, Import, OpenProject, WizardDir };
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

    // 编译状态（M4.6 §5-5）：排队 → 先画一帧"编译中…" → 下帧真构建（阻塞现状不动）
    bool compileQueued_ = false;
    std::string compileQueuedReason_;
    double lastBuildMs_ = -1.0;      // 最近一次成功 编译+换装 耗时（状态栏回显）

    // 新建脚本模态（M4.6 §5-4）
    bool newScriptOpen_ = false;
    char newScriptName_[64] = {};

    // 实体剪贴板（M4.6 §5-1）：树 JSON 多根 + 各根原位置（粘贴相对偏移用）
    std::vector<std::string> entityClip_;
    std::vector<Vec2> entityClipRootPos_;

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
    bool gameFollowActive_ = false; // 游戏相机跟随已吸附（UpdateGameCameraFollow）
    EditTool tool_ = EditTool::Move;
    // 网格显示与吸附解耦（手测第五轮）：旧 gridSnap_ 一flag两用——想看网格就被迫
    // 吃 8px/15°/0.25 全套吸附台阶（= "8 向拖动不丝滑"主因）。Godot/Unity 语义：
    // 网格纯视觉默认开；吸附独立开关默认关，按住 Ctrl 拖拽临时取反。
    bool gridVisible_ = true;
    bool snapEnabled_ = false;
    // Layout 下拉状态（M4.7d）
    std::string activeLayout_;    // 当前命名布局（空 = 默认，imgui.ini 直管）
    std::string pendingLayout_;   // 待应用（与 forceDefaultLayout_ 同点帧内加载）
    bool layoutSaveOpen_ = false; // 保存命名模态请求
    std::string layoutNameBuf_;
    bool exitRequested_ = false;
    bool forceExit_ = false;       // 确认模态放行退出
    bool quitConfirmOpen_ = false; // 本帧打开模态
    bool quitConfirmArmed_ = false; // 模态已打开（防重复弹）
    bool escHeld_ = false;         // ESC 边沿检测（Play 中 = Stop）
    bool assetGpuCbRegistered_ = false; // 设备重建回调只注册一次（会话内切项目防叠加）
    std::vector<std::string> recentProjects_; // 最近项目（~/.lemon/recent.json；M4.6）
    std::string curTitle_;         // 窗口标题缓存（变更才调 SDL）
    bool smokeCloseArmedEver_ = false; // --smoke-close dirty：确认框出现过
    ConfirmContext confirmContext_ = ConfirmContext::Exit;
    bool aboutOpen_ = false;
    uint32_t smokeSeeded_ = 0;     // 冒烟播种实体数（退出时守恒断言）
    SceneViewPanel* scenePanel_ = nullptr; // --smoke-drag 注入定位（按名取，非所有权）
    AssetBrowserPanel* assetPanel_ = nullptr; // --smoke-ui 注入定位（按名取，非所有权）
    bool noProjectCardDismissed_ = false;  // 无项目中央卡已关（会话内；卡会截走视口点击）
    bool forceDefaultLayout_ = false;      // smoke-drag：下帧 BuildUI 强制默认布局
};

} // namespace lemon::editor
