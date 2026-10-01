// Lemon 编辑器 — 应用壳（M4.md §3.1：EditorEntry/主循环/帧节奏/布局持久化）
// 主循环 = anim-smoke 全链基线外层套 ImGui 帧节奏（M4.0 壳 + UI；World/场景 M4.1 起）。
#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "Audio/AudioEngine.h"
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
namespace ui {
class UiSubsystem;
}
namespace ecs {
struct InputState;
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
    bool smoke = false;      // 退出前自检断言 + 汇总打印（M4.md §6 #13 提案）
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
    bool smokeAnim = false;  // --smoke-anim：切片+clip+Animator 帧映射链冒烟（M5 批③；
                             // 隐含 --smoke --play：程序化 4 帧表必验，yami 表在场即验）
    bool benchSurvivor = false; // --bench-survivor：M5 压测基线（临时项目 + 1 万怪刷怪
                                // 场景 + Immediate 呈现 + 帧时统计；08 §3 判据 ≥45fps）
    bool benchScene = false;    // --bench-scene：任意 --project/--scene 场景跑同款测量
                                //（Immediate + 帧八段 + 逐系统分解；不播种无判据，只报数
                                //——用户压测场景的瓶颈检测器，2026-09-25 随 Battle 场加）
    std::string genVsTemplate;  // --gen-vs-template <dir>：M5 批④ 开发工具——生成
                                // vs-survivor 模板项目文件后退出（不开窗；跑一次入库）
    bool smokeTemplate = false; // --smoke-template：M5 批④ 模板链冒烟（向导复制 →
                                // build → Play → HUD/波次/击杀/升级/卡片断言）
    bool smokeGuid = false;     // --smoke-guid：M6a 批⓪ T5 sprite 引用稳定性链冒烟
    bool smokeAudio = false;    // --smoke-audio：M6c 音频全链冒烟（批① 资产链 +
                                // 批③ playOnStart 逻辑声部断言；须配 --project——
                                // 空目录夹具自播种；真项目 Game/ 会装配运行脚本，
                                // ExitPlay 兜底回写 .lemon/saves/ 三档）
    bool smokeUirml = false;    // --smoke-uirml：M6b 批③a RmlUi 呈现地基冒烟（隐含
                                // --play；gameRT 像素断言 + 独立裁决链，不并 editor-smoke 门）
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
    audio::AudioEngine& Audio() { return audio_; } // M6c 竖切批：混音面板/调音消费
    /// M6c 批②：guid→clipId 只读查表（World::SetAudioBackend 解析壳消费）
    uint32_t AudioClipOfGuid(uint64_t guid) const;
    /// M6c 批②：Play World 音频后端装配（TryEnterPlay 与 --play 双挂点）
    void WirePlayAudioBackend();
    /// M6c 竖切批：guid 查表 + Play（文件内音频钩子消费；未装载 clip = 0）
    uint32_t AudioPlayByGuid(uint64_t guid, int32_t group, float volume, float pan,
                             int32_t loop);
    /// M6c 批①：Edit 态试听切换（同 guid 再点 = 停；换 guid 顶停旧曲）
    void TogglePreviewAudio(uint64_t guid);
    /// M6c 批①：按需装载单 clip（试听/兜底用；缺烤/陈旧现烤——同步路径）。
    /// 批①b：outStreamed 出参 = 本次是否走流式（装载日志/验收证据）。
    bool EnsureClipLoaded(const AssetEntry& e, bool* outStreamed = nullptr);
    /// M6c 批①：后台烤制入队（Rescan 增量 / 开项目预热；线程惰性起）
    void EnqueueAudioBake(const AssetEntry& e);
    /// 开项目一次性预热全部音频（批①：导入期烤制——EnterPlay 命中缓存）
    void WarmAudioBakes();
    void StopAudioBaker(); // 幂等（Run 尾 + 析构双保险——早退路径靠析构收线程）
    /// M6c 批①：--smoke-audio 资产链冒烟（导入→meta→后台烤→Peek→试听；须 --project）
    bool RunSmokeAudioChain();
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
    /// 批③c（M7）：GameView 画布矩形上报（GameViewPanel 每帧；ImGui 屏幕点 + RT
    /// 像素——鼠标→画布坐标换算与 IME 候选窗锚点换算的底座）
    void SetGameViewCanvas(float x, float y, float w, float h, uint32_t rtW, uint32_t rtH,
                           bool hovered) {
        gvCanvasX_ = x; gvCanvasY_ = y; gvCanvasW_ = w; gvCanvasH_ = h;
        gvRtW_ = rtW; gvRtH_ = rtH;
        gvCanvasValid_ = w >= 1.0f && h >= 1.0f && rtW > 0 && rtH > 0;
        gvCanvasHovered_ = hovered && gvCanvasValid_;
    }
    const EditorLaunch& Launch() const { return *launch_; }

    // 脏场景确认后的续操作（M4.2 欠账 2026-09-21 补齐：此前 SceneOp 复用退出模态，
    // 按钮"保存并退出/丢弃并退出"硬编码 forceExit_ —— 打开/新建场景、切项目直接把
    // 整个编辑器关了）
    enum class PendingSceneOp { None, OpenScene, NewScene, OpenProject, RecentScene };
    // ---- 场景 IO 动作（菜单/快捷键共用；File 状态机内聚于此）----
    void MenuNewScene();
    void MenuOpenScene();
    /// M4.8-b：最近场景一键切回（脏场景确认后继续走 pendingScenePath_）
    /// 按值收：菜单调用点直传 RecentScenes() 元素引用，OpenScene→RecordRecentScene 会改该 vector
    void MenuOpenRecentScene(std::string path);
    void MenuSaveScene();    // 无路径 → 转 SaveAs
    void MenuSaveSceneAs();
    bool ConfirmUnsaved(PendingSceneOp after); // 脏场景确认（SceneOp 分流：确认后做 after）   // dirty 时弹确认框；返回 false = 用户取消

    // ---- 资产动作（M4.4）----
    void MenuImportAsset();  // 文件选择器（复制进 Assets/ + 导入）
    void RescanAssets();     // FileWatcher/手动重扫 → DB + GPU 增量导入
    /// 双击 .anim/.override 资产 → 打开 Animation 面板（M6a 批② T3——首个"资产 →
    /// 专用编辑面板"通道）。T3c 集归并：.anim 若已属某 .override 集 → 开集工作台并
    /// 选中该段；否则裸 clip 传统模式；.override 直接开集。
    void OpenAnimationEditor(uint64_t guid);
    /// 文件夹右键「从此文件夹创建动画…」（T3-UX7：唯一创建入口）——开极简
    /// 创建框；保存位置 = AssetBrowser 当前浏览目录（只读展示）
    void OpenAnimationCreateFromFolder(const std::string& relDir);
    /// 空白区右键"新建动画集…"（M6a 批② T3c）：开新建集弹窗（源目录非空 =
    /// 显示"并建首段"勾选）；落点 = AssetBrowser 当前浏览目录
    void OpenAnimationCreateSet(const std::string& relDir);
    /// 双击 .rml → 装载到游戏 UI（M6b 批③b，ADR-014）：文档名 = relPath（热重载
    /// 对账键）；Play 中 GameView 即显，非 Play 提示。③c C# 装载通道落地前的手动通道
    void LoadUiDocument(uint64_t guid);
    /// 面板 × 关闭按钮的落点（T3b-8）：同步 PanelRegistry 开关（Window 菜单可重开）
    void ClosePanel(const char* name);
    /// AssetBrowser 当前浏览目录（"" = 根 → 归一 "Assets"；向导落点默认值用）
    const char* AssetBrowserDir() const;

    // ---- 项目/脚本动作（M4.5）----
    /// 打开项目全管线（DB/GPU 导入/watcher/Game 编译 + 脚本宿主）；向导与 --project 共用
    bool OpenProjectPipeline(const std::string& projectRoot);
    /// 装配脚本宿主（dll 绝对路径；失败清 host 并告警）。--script 与项目 Game/ 共用
    bool InitScriptHostFrom(const std::string& dllAbs);
    /// 编译 Game/ + 整域换装 + 双世界重装配 + 计时/泄漏红字（M4.md §3.7）
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
    /// Play 阻断判据（2026-09-22 测试报告 BUG-3 补程序化侧）：项目带 Game/ 工程
    /// 而宿主未装配（启动期编译失败）。交互侧 TryEnterPlay 弹模态；--play/--final
    /// 程序化侧红字退出——静默无脚本运行是难排查的隐性 bug（对齐 Unity/Godot）。
    bool PlayBlockedByScripts();
    /// Play 入口守卫（2026-09-22）：项目带 Game/ 而宿主未装配（启动期编译失败）
    /// → 阻止进 Play 弹模态（对齐 Unity/Godot——静默降级 = "游戏在跑脚本没生效"
    /// 的隐性 bug）；修错保存经 watcher 自动首装解除。无 Game/ 会话直通。
    bool TryEnterPlay();
    /// Stop 出口聚合（2026-09-29 形态 D 收口）：ctx_.ExitPlay() 成功后做 UI 清场
    /// （HideNonEditDocuments——非 Edit 来源文档 Hide 装载保留，Edit 双击预览豁免）
    /// + 翻页标志复位。所有 Stop 路径（交互按钮 ×3 / smoke 程序化 / --play 收尾）
    /// 统一走此口——ExitPlay 本体在 EditorContext（无 gameUi_ 依赖，分层不动）。
    bool StopPlay();
    void BuildPickersAndModals();
    /// Audio Mixer 按需工具窗（M6c 批③；05 §3 冻结旁路形态——不进面板注册表，
    /// Window 菜单进入）。Master/三组音量 + voice 计数 + 节流/微扰全局调参面；
    /// 无持久化（设置屏音量档 = 批④）。
    void DrawAudioMixerWindow();
    /// 场景选择器起始目录：当前场景父目录 → 项目 Scenes/ → 项目根 → CWD（无项目）
    std::string PickerStartDir();
    void BuildNoProjectCard(); // 无项目引导（M4.6 §4-1：中央卡 + 两按钮直达）
    void SetupDefaultLayout();
    void SeedSmokeScene();   // 冒烟播种：父子链 + 常用组件（面板验收有内容）
    void SeedSmokeProject(); // 冒烟播种：临时项目 + 预置 PNG（固定 guid，M4.4 资产链验收）
    void SeedSmokeUiRmlProject(); // 批③b：--smoke-uirml 资产夹具（temp 项目 + .rml/.rcss/贴图）
    void SeedSmokeAudioProject(); // 批③：--smoke-audio 资产夹具（temp 项目 + WAV；
                                  // 目标已是真项目则零播种——夹具纪律，smoke-anim 同款）
    void SeedSmokeUiDocument(); // 批③b：--smoke-uirml 文档装载（从夹具资产走 LoadDocumentFromFile）
    /// 批③b 贴图桥解析器（安装给 gameUi_）：RmlUi JoinPath 后的路径 → 项目精灵
    /// 资产 → 图集页纹理 + 尺寸（未命中 = false → ③a 告警语义）
    bool ResolveUiTexture(const std::string& source, rhi::Texture& tex, uint32_t& w, uint32_t& h);
    /// 批③d 前置（通道 B）文档解析器（安装给 gameUi_）：C# UI.Show 的 relPath →
    /// 项目 .rml 资产绝对路径（未命中 = false → ApplyOps 响亮失败维持）
    bool ResolveUiDocument(const std::string& relPath, std::string& absPath);
    /// 批③d 前置（通道 A + §3 归位）：EnterPlay 成功后、首帧 TickPlay 前调用——
    /// 扫 playWorld 的 UIDocument：GUID 解析（missing/非 Rml = 红字响亮 + 计数，
    /// 绝不静默空屏）→ 同 GUID 去重装载（WARN）→ 按声明态 showOnStart/modal 归位；
    /// 随后未声明且 stale 的文档 Hide + 清 stale。装载钩只认 EnterPlay 扫描——
    /// 运行时动态加 UIDocument 不生效（批文件 §5 登记）。返回装载成功数。
    uint32_t MountSceneUiDocuments();
    /// M6c 竖切批（ADR-015）：EnterPlay 成功后调用——扫全部 Audio 资产：缺烤/源
    /// 新于产物 → 烤制（.lemon/baked/audio/&lt;guidHex&gt;.baked）→ 装载注册 →
    /// guid→clipId 表。烤制/解码失败红字跳过（游戏无声不炸 Play）。返回装载成功数。
    uint32_t MountPlayAudio();
    /// 状态对账（2026-09-29 根因收口）：文件装载文档 ↔ 资产库健康度——relPath 不再
    /// 是健康 .rml 资产（墓碑/非 Rml/查无）即逐出。事件驱动逐出依赖 cs.removed 恰好
    /// 经过 RescanAssets 处理面，裸 Rescan() 调用方（ImportFile/MakePrefabFrom）会立
    /// 墓碑但吃掉事件 → 逐出永不发生（僵尸渲染，机器复现 pix=8176）。对账挂两处：
    /// RescanAssets 尾（watcher 每拍自愈）+ MountSceneUiDocuments 头（进 Play 不变量）
    void ReconcileUiDocuments();
    /// 批③c（M7）：鼠标/键盘/文本输入喂入游戏 UI + InputState 让出门（Play 段、
    /// gameUi_->Update() 前每帧）
    void FeedGameUiInput();
    void LoadProjectFonts(); // 批③b：Assets/ 下字体文件 → RmlUi fallback 注册
    /// M4.5 终验判据场景（向导项目上零代码搭"走地图+刷怪"）：地图/角色/刷怪器
    void SeedJudgementScene(uint64_t spawnGuid);
    /// --smoke-drag 帧注入状态机（批③c-1 自 Run 外迁 EditorAppSmoke.cpp；
    /// Run 主循环原位调用——帧号锚定/执行时序逐位不变）
    void SmokeDragFrame(uint64_t frame);
    /// --smoke-drag 末帧裁决（分段计数 + 断言行打印；false = 退出码 1）
    bool SmokeDragVerdict();
    /// --smoke-ui 帧注入状态机（批③c-2 自 Run 外迁 EditorAppSmoke.cpp；
    /// Run 原位调用且须在 ui_->BeginFrame 前——注入先于 ImGui 帧消费）
    void SmokeUiFrame(uint64_t frame);
    /// --smoke-ui 末帧裁决位（裁决行已在帧 154 块内打印；此处只回退出码）
    bool SmokeUiVerdict();
    /// --smoke-anim 族五挂点（批③c-3 自 Run 外迁 EditorAppSmoke.cpp；挂点原位
    /// 逐位不变）：预循环播种 / EnterPlay 后快照断言 / 帧链 / 帧采样 / 末帧裁决
    void SmokeAnimSeed();
    void SmokeAnimPlaySetup();
    void SmokeAnimFrame(uint64_t frame);
    void SmokeAnimSample(uint64_t frame);
    bool SmokeAnimVerdict();
    /// --smoke-template 族六挂点（批③c-4 自 Run 外迁 EditorAppSmokeTpl.cpp；挂点
    /// 原位逐位不变）：向导复制播种 / 场景+预置存档播种（else-if 守卫留原位）/
    /// 事件 sink 装配 / 循环内转向注入（ApplyInput 前）/ 帧采样 / 末帧裁决
    bool SmokeTplSeedProject();
    bool SmokeTplSeedScene();
    void SmokeTplPlaySetup();
    void SmokeTplSteer(uint64_t frame, ecs::InputState& in);
    void SmokeTplSample(uint64_t frame);
    bool SmokeTplVerdict();
    /// 批④ 收口（g_tplSmoke 单 TU 化——结构体退回 EditorAppSmokeTpl.cpp）：Run
    /// 渲染段层序捕获块挂点化（挂点原位、时序逐位不变）+ FeedGameUiInput 的
    /// 指针保持窗读点（随 UI 桥外迁后经此访问器读）
    void SmokeTplCapture(rhi::CommandList& cl);
    bool SmokeTplPointerHold() const;
    /// --smoke-uirml 族三挂点（批③c-5 自 Run 外迁 EditorAppSmokeUirml.cpp；挂点
    /// 原位逐位不变）：独立进 Play（守卫留原位）/ 帧链 / 末帧裁决
    bool SmokeUirmlEnterPlay();
    void SmokeUirmlFrame(uint64_t frame);
    bool SmokeUirmlVerdict();
    /// --bench-survivor/--bench-scene 族三挂点（批③c-6 自 Run 外迁 EditorAppBench.cpp；
    /// 挂点原位逐位不变）：帧段累计（段界戳 Run 帧局部、十参数直传——结构体化
    /// 会跨帧残留）/ 停跑证据采集（返回 playAliveAtStop 等价增量）/ 末帧裁决
    void BenchSample(uint64_t frame, std::chrono::steady_clock::time_point benchT0,
                     std::chrono::steady_clock::time_point bPump,
                     std::chrono::steady_clock::time_point bSim,
                     std::chrono::steady_clock::time_point bUi0,
                     std::chrono::steady_clock::time_point bUi1,
                     std::chrono::steady_clock::time_point bAcq,
                     std::chrono::steady_clock::time_point bScene,
                     std::chrono::steady_clock::time_point bUiDraw,
                     std::chrono::steady_clock::time_point bPresent);
    uint32_t BenchCaptureStop();
    bool BenchVerdict(uint64_t frame, uint32_t playAliveAtStop);
    /// --final 终验族五挂点（批③c-6 自 Run 外迁 EditorAppFinal.cpp；挂点原位
    /// 逐位不变）：向导播种 / 场景开+判据播种（守卫留原位）/ Play 中热重载播种 /
    /// fps 采样 / 末帧验收（play 往返四量以参数过桥——Run 跨族共享态）
    bool FinalSeedProject();
    bool FinalSeedScene();
    void FinalFrame(uint64_t frame);
    void FinalSample(uint64_t frame);
    bool FinalVerdict(uint32_t playAliveAtStop, double playEnterMs, double playExitMs,
                      bool playVerified, double firstFrameMs);
    /// 换装判定辅助：Game/*.csproj 路径 + 输出 dll（项目根/.lemon/bin/<名>.dll）
    bool FindGameProject(std::string& csproj, std::string& dll);
    /// Game/ 源码变更检测（FileWatcher 置脏后过滤 .cs，排除 obj/bin）
    bool ScriptSourceChanged();
    /// 崩溃恢复提示模态（启动检测 autosave 新于盘档 → 恢复/忽略）
    void DrawRecoveryModal();
    /// Play 中游戏相机跟随（M4.7 手测修复）：tag "Camera"（显式相机实体）>
    /// tag "Player" > 首个挂脚本实体；进 Play 首帧吸附、之后阻尼跟随；退出回默认位。
    void UpdateGameCameraFollow();

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
    std::unique_ptr<::lemon::ui::UiSubsystem> gameUi_; // 游戏 UI 层（批③a ADR-014；null = 初始化失败降级）
    audio::AudioEngine audio_; // M6c 竖切批：设备/混音（静音降级一等公民，ADR-015 M4）
    std::unordered_map<uint64_t, uint32_t> audioClips_; // 资产 GUID → clipId（装载产物）
    // M6c 批①：试听声部（Edit 态可响——EnsureClipLoaded 按需现烤现载）+ 后台烤制
    //（Rescan/开项目增量 → 工作线程；EnterPlay 只兜缺漏——消除竖切批 230ms 同步顿）
    uint64_t previewGuid_ = 0;
    uint32_t previewVoice_ = 0;
    std::thread audioBakeThread_;
    std::mutex audioBakeMtx_;
    std::condition_variable audioBakeCv_;
    std::deque<std::tuple<uint64_t, std::string, std::string, float, float>> audioBakeQueue_;
    bool audioBakeStop_ = false;
    std::atomic<int> audioBakePending_{0};
    std::unique_ptr<scripting::ScriptHost> host_; // C# 宿主（--script/项目 Game；null = 无）
    AssetGpuCache gpuAssets_;
    FileWatcher watcher_;
    FileWatcher scriptWatcher_;      // Game/ 源码（M4.5 热重载触发）
    std::vector<std::unique_ptr<IEditorPanel>> ownedPanels_;
    PanelRegistry panels_;

    // 热重载状态（§3.7）
    double hotReloadMs_ = 0.0;       // 最近一次 编译+换装+重装配 总耗时（≤2s 判定）
    int64_t lastHandledCsWrite_ = 0; // 上次已处理的 .cs 新写时间戳（去重/防抖）
    DebounceGate reloadGate_;        // 防抖门（F-15：窗内脏事件转 pending 不再吞）
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
    // 固定步长累加器（2026-09-29 复审 2a/2b）：交互 Play 墙钟进账 → N × 1/60 出账，
    // playAcc_ = 余账、playAlpha_ = 余账/步长 → 渲染插值。自动化链（smoke*/bench*/
    // --frames/playDiag）不走此路径（每渲染帧恰一步 + alpha=1，口径逐位不变）。
    float playAcc_ = 0.0f;
    float playAlpha_ = 1.0f;
    bool gameViewFocused_ = false; // GameView 输入门控（§3.6）
    // 批③c（M7）：GameView 画布（ImGui 屏幕点）+ RT 像素 + 键盘差分态
    float gvCanvasX_ = 0, gvCanvasY_ = 0, gvCanvasW_ = 0, gvCanvasH_ = 0;
    uint32_t gvRtW_ = 0, gvRtH_ = 0;
    bool gvCanvasValid_ = false;
    bool gvCanvasHovered_ = false; // 画布悬停（面板内 IsItemHovered——浮窗遮挡为假）
    bool gvKeyWasDown_[64] = {}; // UiKey 差分（边沿转发 RmlUi；枚举值 < 64）
    char smokeUiEvText_[48] = {}; // 批③c：uiev 回读快照（Play 中的 RtUi 槽——退 Play
                                  // 后 play world 即毁，终帧前捕获）
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
    PendingSceneOp pendingSceneOp_ = PendingSceneOp::None; // SceneOp 确认后要做的场景操作
    std::string pendingScenePath_; // RecentScene 的目标路径（确认模态期间持有）
    int8_t tabFocusPending_ = 0; // Play 进出自动切 Game/Scene 标签页（+1/-1；BuildUI 内消费）
    bool aboutOpen_ = false;
    bool audioMixerOpen_ = false; // Audio Mixer 工具窗开态（M6c 批③；Window 菜单）
    uint32_t smokeSeeded_ = 0;     // 冒烟播种实体数（退出时守恒断言）
    SceneViewPanel* scenePanel_ = nullptr; // --smoke-drag 注入定位（按名取，非所有权）
    AssetBrowserPanel* assetPanel_ = nullptr; // --smoke-ui 注入定位（按名取，非所有权）
    bool noProjectCardDismissed_ = false;  // 无项目中央卡已关（会话内；卡会截走视口点击）
    bool forceDefaultLayout_ = false;      // smoke-drag：下帧 BuildUI 强制默认布局
    bool playBlockedOpen_ = false;         // Play 阻断模态已请求开（脚本未装配）
};

} // namespace lemon::editor
