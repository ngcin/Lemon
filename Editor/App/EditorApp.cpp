// Lemon 编辑器 — 应用壳实现（M4.md §3.1；主循环承 anim-smoke 全链基线）
// M4.0：壳 + 默认布局 + DPI/字体 + smoke；M4.1：EditorContext/场景 IO/快捷键/关闭确认。
// 2026-09-30 批④：UI 桥七函数外迁 App/EditorAppUiBridge.cpp（机械搬移零行为
// 变化；Run 渲染段 capReq 块收口 SmokeTplCapture 挂点，g_tplSmoke 单 TU 化）
// 2026-09-29 批②：UI 骨架/编辑动作/脚本管线拆出 EditorAppChrome/Actions/Scripts.cpp（机械拆分零行为变化）
// 2026-09-29 批③a：冒烟播种族外迁 App/EditorAppSmoke.{h,cpp}（机械搬移零行为变化；kSmokePngGuid 死常量随迁删除）
// 2026-09-29 批①：vs-survivor 模板生成器外迁 Templates/VsTemplateGen.{h,cpp}（机械搬移零行为变化）
#include "App/EditorApp.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <vector>

#include "stb_image_write.h"

// 批③c：UI 桥钩子目标（gameUi_ 生命周期镜像——Init 成功置位 / Shutdown 清空；
// UiHooks 是 C 函数指针，不能捕 this）
static ::lemon::ui::UiSubsystem* s_gameUiForHooks = nullptr;

#include <SDL3/SDL.h>

#include "App/ImGuiBackend.h"
#include "Assets/AssetDatabase.h"
#include "Interaction/ViewportRenderer.h"
#include "Templates/VsTemplateGen.h"
#include "App/RecentProjects.h"
#include "App/EditorAppSmoke.h"
#include "Components/CoreComponents.h"
#include "Components/RenderComponents.h"
#include "ECS/Hierarchy.h"
#include "Core/Log.h"
#include "EditorContext.h"
#include "Panels/BuiltInPanels.h"
#include "Platform/Window.h"
#include "Renderer/RHI.h"
#include "Ui/UiSubsystem.h" // 批③a（ADR-014）：游戏 UI 层（RmlUi）
#include "Scripting/ScriptHost.h"
#include "imgui.h"
#include "imgui_internal.h" // DockBuilder（docking 分支布局编程 API）+ FindWindowByName

namespace lemon::editor {

EditorApp::EditorApp() = default;
EditorApp::~EditorApp()
{
    StopAudioBaker(); // 批①：幂等——早退路径（--gen-vs-template 等）的线程收口兜底
}

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
// M5 批④：C# Save.Flush → 编辑器域落盘（项目 .lemon/saves/；无项目 = no-op）；
// M6a 批② T5：三档全落（空档跳过语义保留——Count 0 不写文件）
void HookSaveFlush(ecs::World& w) {
    if (!g_app) return;
    for (uint8_t ch = 0; ch < ecs::kSaveChannelCount; ++ch)
        g_app->Ctx().WriteSaveFile(ch, w.Saves(ch));
}
// M6c 批②：音频播放路径退役四桥（HookAudio*）——C# 命令走 g_world->Audio()
// staging + AudioSystem #20 提交（ADR-015 M3）；guid→clipId 解析壳在
// EditorAppScripts.cpp（TryEnterPlay 同 TU 注入 World::SetAudioBackend）。

// ImGui 错误汇（1.92 内部回调口；DockBuilder 同源引用 imgui_internal）：ID 冲突/
// 空标签等程序员错误在这里现形——冒烟断言清零（M4.5 修复 Inspector ##v 撞号后
// 加的程序化防线：这类错只在交互时弹窗，无头冒烟原本测不到）。
int g_imguiErrorCount = 0;
void ImGuiErrorSink(ImGuiContext*, void* user_data, const char* msg) {
    ++*static_cast<int*>(user_data);
    LEMON_WARN("ImGui 错误：%s", msg);
}

// 网格线特征 = "比视口底色略亮的灰系"（α70 网格与 α110 主轴在 (23,26,33) 底上
// 混出约 (49,54,58)~(78,90,97) 的灰带；UI 面板底 (35,38,46) 与亮灰文字均在带外）
int CountGridishPixels(const std::vector<uint8_t>& px, uint32_t w, uint32_t h) {
    int n = 0;
    for (size_t i = 0; i < (size_t)w * h; ++i) {
        const uint8_t* p = &px[i * 4];
        const int r = p[0], g = p[1], b = p[2];
        if (g >= 44 && g <= 104 && std::abs(r - g) <= 14 && std::abs(g - b) <= 14) ++n;
    }
    return n;
}

} // namespace

// ---- M4.7-P0 冒烟像素断言辅助：overlay 渲染可见性（数像素不数包）----
int CountPixelsNear(const std::vector<uint8_t>& px, uint32_t w, uint32_t h, int r, int g,
                    int b, int tol) {
    int n = 0;
    for (size_t i = 0; i < (size_t)w * h; ++i) {
        const uint8_t* p = &px[i * 4];
        if (std::abs((int)p[0] - r) <= tol && std::abs((int)p[1] - g) <= tol &&
            std::abs((int)p[2] - b) <= tol)
            ++n;
    }
    return n;
}

int EditorApp::Run(const EditorLaunch& launch) {
    launchCopy_ = launch;
    launch_ = &launchCopy_;
    // 批③b：冒烟钩子（热重载中点 100/140）与末帧捕获都锚定帧号——参数门禁对齐
    // smoke-template 先例（③a 的"缺省 180"只写 launchCopy_ 而主循环判 launch.frames，
    // 无 --frames 实际 = 无限跑 + 零捕获；显式要求根除该歧义）
    if (launchCopy_.smokeUirml && launch.frames < 470) {
        LEMON_ERROR("--smoke-uirml 需要 --frames N（N>=470：两段热重载中点 100/140 + "
                    "watcher 驱动删除逐出余量（500ms 轮询）+ 形态 D 两段 405-430 + "
                    "终局画面稳定余量）");
        return 2;
    }
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
    sd.present = (launch.benchSurvivor || launch.benchScene)
                     ? rhi::PresentModePref::Immediate // 压测口径：禁 vsync（否则帧时被 60Hz 钉住测不出真实档位）
                     : rhi::PresentModePref::Fifo;     // 编辑器 vsync（05 §9）
    if (!device_->CreateSwapchain(sd)) return 1;
    device_->EnableTimestamps(); // Profiler 面板 GPU 列（02 §3.5）

    ui_ = std::make_unique<ImGuiBackend>();
    // M4.8-c：注入/冒烟模式不做布局持久化（cwd 共享 ini 的状态污染 = smoke-drag
    // 间歇失败的根因）；正常会话布局持久化照旧
    const bool persistLayout = !(launch.smoke || launch.smokeUi || launch.smokeDrag ||
                                 launch.smokeAnim || launch.playTest || launch.finalTest ||
                                 !launch.smokeClose.empty());
    if (!ui_->Init(*window_, *device_, ".lemon/editor", persistLayout)) return 1;
    // ImGui 程序员错误（ID 冲突等）进编辑器日志 + 冒烟清零断言（见 anon-ns 注记）
    ImGui::GetCurrentContext()->ErrorCallback = ImGuiErrorSink;
    ImGui::GetCurrentContext()->ErrorCallbackUserData = &g_imguiErrorCount;

    viewport_ = std::make_unique<ViewportRenderer>();
    viewport_->Init(*device_, *ui_);

    // 批③a（ADR-014）：游戏 UI 层（RmlUi over RHI）。初始化失败不阻断编辑器
    // （红字 + 层不挂——UI 缺席是可运行的降级态）；Play 中由 ViewportRenderer
    // 在 gameRT 动态渲染块内叠画（sprite 之后、EndPass 之前）
    gameUi_ = std::make_unique<::lemon::ui::UiSubsystem>();
    if (gameUi_->Init(*device_, rhi::Format::RGBA8Unorm, window_->NativeHandle())) {
        viewport_->SetGameUiLayer(
            [this](rhi::CommandList& cl, uint32_t w, uint32_t h) { gameUi_->Render(cl, w, h); });
        // 批③c（M7/ADR-014 D2）：文本输入事件 tap（ImGui 先吃、游戏 UI 后喂——
        // GameView 聚焦 + Play 中才转发；候选窗锚点 = ActivateKeyboard→SetTextInputArea，
        // T7 实证核心自带光标随移随发）
        ui_->SetSdlEventTap([this](const void* raw) {
            const SDL_Event* e = (const SDL_Event*)raw;
            if (!gameUi_ || !ctx_.Playing() || !gameViewFocused_) return;
            if (e->type == SDL_EVENT_TEXT_INPUT)
                gameUi_->ProcessTextInput(e->text.text);
            else if (e->type == SDL_EVENT_TEXT_EDITING)
                gameUi_->ProcessTextEditing(e->edit.text, e->edit.start, e->edit.length);
        });
        // 批③b：引擎正字 Noto Sans SC（OFL，随仓库 Engine/Ui/Fonts；LEMON_TEMPLATE_DIR
        // 同款编译期路径）。fallback=true——RCSS 未命中的族名也有字可渲（③a 发现
        // RCSS 无逗号回退列表）。装载失败红字不阻断（Init 内系统链仍兜底）
#ifdef LEMON_ENGINE_FONT_DIR
        gameUi_->LoadFontFace(LEMON_ENGINE_FONT_DIR "/NotoSansSC-Regular.otf",
                              "Noto Sans SC", /*fallback=*/true);
#else
        LEMON_WARN("ui-subsystem: 未定义 LEMON_ENGINE_FONT_DIR——引擎 Noto 未装载，"
                   "沿用系统字体链");
#endif
        // --smoke-uirml 播种延后到项目打开后（批③b 起文档/样式/贴图均来自夹具资产）
        // 批③c（M2/M3）：UI 桥钩子装配——ScriptHost TickBatch 尾拉 C# ops 直转
        // ApplyOps（当帧可见）；#16 头抽干事件直转 DrainEvents。静态指针镜像
        // gameUi_ 生命周期（钩子是 C 函数指针，Shutdown 时清空）
        s_gameUiForHooks = gameUi_.get();
        scripting::SetUiHooks(
            {[](const ::lemon::ui::UiOpC* ops, uint32_t n, const char* arena, uint32_t bytes) {
                 if (s_gameUiForHooks) s_gameUiForHooks->ApplyOps(ops, n, arena, bytes);
             },
             [](::lemon::ui::UiEventC* dst, uint32_t cap) -> uint32_t {
                 return s_gameUiForHooks ? s_gameUiForHooks->DrainEvents(dst, cap) : 0;
             }});
        // 批③d 前置（通道 B）：文档解析器——C# UI.Show 未装载文档名 → 项目内
        // .rml 资产现载。闭包经 ctx_ 活引用，切项目免重装（贴图 resolver 同理）
        gameUi_->SetDocumentResolver(
            [this](const std::string& rel, std::string& abs) {
                return ResolveUiDocument(rel, abs);
            });
        // 批③d-1（B1 dp 坐标系）：L2 皮设计基准 = 720dp 高画布（ratio = gameRT 高/
        // 720，只缩 dp 单位——px 文档零影响）。项目级覆写登记 ③d-2（工程档）。
        gameUi_->SetDpReferenceHeight(720);
    } else {
        gameUi_.reset();
    }

    // M6c 竖切批（ADR-015 M4）：音频引擎——gameUi_ 段后装配（同为"失败红字不
    // 阻断"降级服务）。批② 起 C# 命令通道 = World.AudioChannel（SetAudioHooks
    // 退役；Play World 后端注入在 TryEnterPlay，试听走引擎直呼不变）
    audio_.Init();

    // M5 批④：--gen-vs-template <dir>（开发工具：产出模板项目文件后退出——
    // 不进渲染主循环；产物入库 Templates/vs-survivor 随仓库管理）。须在 viewport
    // 就绪后执行：spriteIdBase 用与 OpenProjectPipeline 同一规则
    // （程序化图集 SpriteCount()+1）——模板场景的数字 spriteId 才与真实打开
    // 路径一致（manifest 丢失/gitignore 下 fresh 扫描仍可复现）。
    if (!launch.genVsTemplate.empty()) {
        const uint32_t genBase = viewport_->Assets().Registry().SpriteCount() + 1;
        const bool ok = GenerateVsTemplate(ctx_, genBase, launch.genVsTemplate);
        std::printf("[gen-vs-template] %s → %s（base %u）\n", ok ? "OK" : "FAILED",
                    launch.genVsTemplate.c_str(), genBase);
        return ok ? 0 : 1;
    }

    // M6a 批⓪ T5：--smoke-guid（sprite 引用稳定性链——无头跑完即退，不进主循环）
    if (launch.smokeGuid) {
        const uint32_t base = viewport_->Assets().Registry().SpriteCount() + 1;
        const bool ok = vs_template::RunGuidSmokeChain(base);
        std::printf("[smoke-guid] %s\n", ok ? "OK" : "FAILED");
        return ok ? 0 : 1;
    }

    ownedPanels_ = CreateAllPanels();
    for (auto& p : ownedPanels_) panels_.Add(p.get());
    for (auto& e : panels_.Entries()) // --smoke-drag 注入定位（按名取 Scene 面板）
    {
        if (std::strcmp(e.panel->Name(), "Scene") == 0)
            scenePanel_ = static_cast<SceneViewPanel*>(e.panel);
        if (std::strcmp(e.panel->Name(), "Assets") == 0)
            assetPanel_ = static_cast<AssetBrowserPanel*>(e.panel);
    }

    // ---- M4.4 资产链 / M4.5 项目向导与终验 ----
    if ((launch.smoke || launch.smokeUi) && !launch.projectDir.empty() && !launch.finalTest)
        SeedSmokeProject();
    // 终验第一步：向导建项目（blank 模板；目录必须不存在 → --project 传父目录，
    // 项目名固定 lemon-final，保证可重复跑）
    // --final 向导播种——外迁 EditorAppFinal.cpp（批③c-6：挂点原位）
    if (!FinalSeedProject()) return 1;
    // --smoke-template（M5 批④）：向导复制 vs-survivor 到 tempdir（幂等清残留）→
    // 走标准 OpenProjectPipeline（Game/ 编译 + 脚本宿主 + watcher）→ 开 Main.scene
    // --smoke-template 向导复制播种——外迁 EditorAppSmokeTpl.cpp（批③c-4：挂点原位）
    if (!SmokeTplSeedProject()) return 1;
    // 批③b：--smoke-uirml 资产夹具（temp 项目：.rml + .rcss + 贴图，标准管线打开）
    // ——文档/样式/贴图/热重载四通道全走真实资产路径（③a 的内存文档退役）
    if (launch.smokeUirml) SeedSmokeUiRmlProject();
    // M6c 批③ review 修：--smoke-audio 须显式 --project，裸跑 fail-fast。两个
    // hazard：①自动重开上次项目并在其上跑链（真项目副作用面——Game/ 脚本装配
    // 运行 + EnterPlay 载入/ExitPlay 兜底回写 .lemon/saves/ 三档）；②无最近项目
    // 则链路不执行、无提示开窗常驻。守卫在自动重开块之前（原义 projectDir）。
    if (launch.smokeAudio && launch.projectDir.empty()) {
        LEMON_ERROR("--smoke-audio 须配 --project（空目录 = 夹具自播种；真项目注意："
                    "Game/ 脚本会装配运行，ExitPlay 兜底回写 .lemon/saves/ 三档）");
        return 1;
    }
    // M6c 批③：--smoke-audio 夹具（temp 项目 + smoke-tone.wav；真项目零播种）
    if (launch.smokeAudio && !SeedSmokeAudioProject()) return 1; // #37：播种失败 fail-fast
    // 最近项目（M4.6 §4-4）：--project 缺省时自动重开上次（--no-reopen 跳过；
    // 冒烟/终验不适用——确定性优先）。菜单最近列表同源本 vector。
    recentProjects_ = LoadRecentProjects();
    if (launch_->projectDir.empty() && !launch.noReopen && !launch.smoke &&
        !launch.finalTest && !recentProjects_.empty()) {
        const std::string& last = recentProjects_.front();
        std::error_code ec;
        if (std::filesystem::is_regular_file(std::filesystem::path(last) / "project.lemon",
                                             ec)) {
            launchCopy_ = *launch_;
            launchCopy_.projectDir = last;
            launch_ = &launchCopy_;
            LEMON_LOG("自动重开上次项目：%s（--no-reopen 跳过）", last.c_str());
        } else { // BUG-1 连带（测试报告）：半成品目录此前静默跳过零日志
            LEMON_WARN("自动重开跳过：%s 下没有 project.lemon"
                       "（File → 最近打开 可清除该条目）", last.c_str());
        }
    }
    if (!launch_->projectDir.empty()) {
        if (!OpenProjectPipeline(launch_->projectDir)) return 1;
        // 批③b：夹具文档装载（项目已开 → 贴图桥 resolver/字体已就位）
        if (launch.smokeUirml && gameUi_) SeedSmokeUiDocument();
        // 批①：音频导入期预热（后台烤制——EnterPlay 命中缓存，消除 230ms 同步顿）
        WarmAudioBakes();
        // 批①：--smoke-audio 资产链冒烟（真项目资产：导入→meta→后台烤→Peek→试听）
        if (launch.smokeAudio) {
            const bool ok = RunSmokeAudioChain();
            std::printf("[smoke-audio] %s\n", ok ? "OK" : "FAILED");
            return ok ? 0 : 1;
        }
    }

    // ---- 脚本宿主（--script <dll> 显式指定；项目 Game/ 已在管线内装配）----
    if (!launch.script.empty()) InitScriptHostFrom(launch.script);
    g_app = this;
    scripting::SetEditorAssetHooks({HookSpriteOf, HookInstantiate});
    scripting::SetScriptIoHooks({HookSaveFlush}); // M5 批④：存档 IO（编辑器域）
    playDiag_ = std::getenv("LEMON_PLAY_DIAG") != nullptr; // 相机手感诊断开关
    if (playDiag_) std::printf("[playdiag] init on\n");

    // 启动场景：--scene 指定则打开；向导项目开 Main.scene；冒烟播种示例实体
    if (!launch.openScene.empty()) {
        if (!ctx_.OpenScene(launch.openScene)) return 1;
        if (launch.smoke) smokeSeeded_ = ctx_.ActiveScene().AliveCount(); // 守恒断言基数 = 载入数
    } else if (launch.finalTest) {
        // --final 场景开+判据播种——外迁 EditorAppFinal.cpp（批③c-6：守卫留原位）
        if (!FinalSeedScene()) return 1;
    } else if (launch.smokeTemplate) {
        // M5 批④：模板链冒烟——Main.scene（玩家/导演已由生成器播种）；进 Play 前
        // 预置存档 = EnterPlay 载入路径的机械验证。M6a 批② T5 双载体：meta.sav
        // （vs.best=123，新名——模板 Chan.Meta 读点）+ game.sav（旧名——slot 档
        // 惰性迁移链载体；出 Play 断言迁移落新名）
        // --smoke-template 场景+预置存档播种——外迁 EditorAppSmokeTpl.cpp（批③c-4）
        if (!SmokeTplSeedScene()) return 1;
    } else if (launch.smoke || launch.smokeDrag || launch.smokeUi) {
        SeedSmokeScene();
        // 冒烟不吃 ini 布局漂移账（同 smoke-drag 语义）：断言依赖 Scene 面板被绘制
        // （grid/选框/手柄 = 面板侧推送），上次会话若把 GameView 切成活动标签，
        // Scene 沉入后台标签 = overlay 三要素全零误报（etest 排查实抓，2026-09-21）
        forceDefaultLayout_ = true;
    } else {
        ctx_.NewScene();
        LEMON_LOG("编辑器就绪（新建场景；Ctrl+O 打开 .scene）");
    }

    // 启动恢复检测（§3.8）：场景打开后 autosave 新于盘档 → 提示（交互模态/终验自动恢复）。
    // 只在交互会话做（热修④）：冒烟/终验/压测等 --frames 有限会话跳过——恢复模态
    // 每帧重开抢占模态栈，曾把 animset.create 等注入链整链憋死（"环境抖动"真身）
    if (!launch.finalTest && launch.frames <= 0 && !ctx_.Assets().ProjectRoot().empty())
        recoveryPath_ = ctx_.DetectAutosaveRecovery();

    // --smoke-anim 预循环播种——外迁 EditorAppSmoke.cpp（批③c-3：挂点原位）
    SmokeAnimSeed();

    // --play：Play 往返验收（§6 #4/#5）：进 Play → 中段编辑落 Play World → Stop 逐字节断言
    // --final 同样进 Play（终验 §6 #2/#6：Play 中热重载 + fps）
    if ((launch.playTest || launch.finalTest) && launch.smoke) {
        // 程序化守卫（2026-09-22 测试报告 BUG-3）：与交互侧 TryEnterPlay 同判据
        // （PlayBlockedByScripts）——此前直调 EnterPlay，坏档项目 --play 静默无脚本
        // 运行。无头路径不弹模态：红字 + 退出码 1。--script 显式供装属既定语义。
        if (PlayBlockedByScripts()) {
            LEMON_ERROR("已阻止进入 Play：Game/ 编译失败（脚本宿主未装配）——"
                        "修复编译错误后重跑（本次 exit 1）");
            return 1;
        }
        if (!ctx_.EnterPlay()) return 1;
        MountSceneUiDocuments(); // 批③d 前置（通道 A）：--play/--final 程序化路径
        MountPlayAudio();        // M6c 竖切批：烤制/装载音频资产（guid→clip）
        WirePlayAudioBackend();  // M6c 批②：程序化路径同款后端注入（漏 = 无头 --play 全哑）
    }
    // 批③a（ADR-014）：--smoke-uirml 独立进 Play——playTest 的"进/出往返"语义与
    // smoke 门绑定（上方块），本模式只需"Play 中持续渲染 UI"一态（Stop 由循环后
    // --play 收尾块统一处理，见下方 ctx_.ExitPlay）
    if (launchCopy_.smokeUirml && !ctx_.Playing()) {
        // --smoke-uirml 独立进 Play——外迁 EditorAppSmokeUirml.cpp（批③c-5：守卫留原位）
        if (!SmokeUirmlEnterPlay()) return 1;
    }
    // M6a 批② T3：编辑后的 clip 进 Play 快照生效（BuildPlayClipCache 吃到 13fps×2 帧
    // ——验收②"保存 → 重进 Play 帧率/帧数生效"的程序化侧）。T3b-1：整图引用
    // 单帧档同断言（未切片 smoke.png cell0 → 本体号 1 帧）。
    // --smoke-anim Play 前置——外迁 EditorAppSmoke.cpp（批③c-3：挂点原位）
    SmokeAnimPlaySetup();
    // --bench-survivor（M5 清障③）：播种压测场景（tempdir 项目 + 1 万怪 Spawner）并进
    // Play。无 Game/（tempdir）——无脚本属合法形态，不走 PlayBlockedByScripts 守卫。
    if (launch.benchSurvivor) {
        if (!SeedBenchSurvivorScene(ctx_)) {
            LEMON_ERROR("bench-survivor 播种失败（临时项目/prefab 导出）");
            return 1;
        }
        if (!ctx_.EnterPlay()) return 1;
        MountSceneUiDocuments(); // 批③d 前置：基准护栏活证——无 UIDocument 装载恒 0
    }
    // --bench-scene（2026-09-25 工具化）：--project/--scene 已开，进 Play 跑同款测量
    //（Immediate + 帧八段 + 逐系统分解）。不播种、无场景特定判据——RESULT 只报数，
    // 退出码恒 0：用户压测场景（如 svr-test Battle 场）的瓶颈检测器。
    if (launch.benchScene) {
        if (launch.openScene.empty()) {
            LEMON_ERROR("--bench-scene 需要 --scene（绝对路径）+ --project");
            return 2;
        }
        if (!ctx_.EnterPlay()) return 1;
        MountSceneUiDocuments(); // 批③d 前置：基准护栏活证（用户压测场景零装载）
    }
    // M5 批④ --smoke-template：EnterPlay 已由上方 playTest 块完成（模板含 Game/、
    // 编译成功才走到这——PlayBlockedByScripts 守卫先行）。此处挂事件计数 sink。
    // --smoke-template Play 前置——外迁 EditorAppSmokeTpl.cpp（批③c-4：挂点原位）
    SmokeTplPlaySetup();

    // --save-scene：场景就绪即保存退出（CLI roundtrip 验收：save → --scene 重开）
    if (!launch.saveScene.empty()) {
        if (!ctx_.SaveScene(launch.saveScene)) return 1;
        std::printf("[lemon] editor: scene saved to %s (%u entities)\n",
                    launch.saveScene.c_str(), ctx_.ActiveScene().AliveCount());
        return 0;
    }

    // --smoke-close 看门狗：帧上限 = 失效时的兜底退出（否则挂死）；跑满 = FAIL
    if (!launch.smokeClose.empty() && launch.frames <= 0) {
        LEMON_ERROR("--smoke-close 需要 --frames N（看门狗）");
        return 2;
    }
    // --smoke-drag 看门狗（同上；84 帧 = 移动 3-23 + 旋转 24-41 + resize 43-56 +
    //  缩放 58-63 + 判定余量）
    if (launch.smokeDrag && launch.frames < 84) {
        LEMON_ERROR("--smoke-drag 需要 --frames N（N>=84 看门狗）");
        return 2;
    }
    if (launch.smokeUi && launch.frames < 160) {
        LEMON_ERROR("--smoke-ui 需要 --frames N（N>=160 看门狗）");
        return 2;
    }
    if (launch.smokeAnim && launch.frames < 120) {
        LEMON_ERROR("--smoke-anim 需要 --frames N（N>=120：帧映射 24 tick + 批① 切段链"
                    "（frame 60 起 + hit 段 10 tick 回切）+ fx 余量）");
        return 2;
    }
    if (launch.benchSurvivor && launch.frames < 600) {
        LEMON_ERROR("--bench-survivor 需要 --frames N（N>=600：怪海涨满 ~240 帧预热 + "
                    "测量窗 ≥360）");
        return 2;
    }
    if (launch.benchScene && launch.frames < 600) {
        LEMON_ERROR("--bench-scene 需要 --frames N（N>=600：预热 240 + 测量窗 ≥360）");
        return 2;
    }
    if (launch.smokeTemplate && launch.frames < 3000) {
        LEMON_ERROR("--smoke-template 需要 --frames N（N>=3000：波1 t=5s + 击杀攒满"
                    "首升 XP + 卡片链 + 2100 帧起站桩死亡→对话框→复活链 + 余量）");
        return 2;
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
        const auto benchT0 = std::chrono::steady_clock::now();
        using BenchClock = std::chrono::steady_clock;
        // 段界（epoch = 未走到该点，如非 Play 帧；累计与全帧同门不读 epoch）
        BenchClock::time_point bPump{}, bSim{}, bUi0{}, bUi1{}, bAcq{}, bScene{}, bUiDraw{},
            bPresent{};
        // 窗口关闭按钮 → 请求退出（消费在下方统一裁决：干净场景直接退，脏场景确认）
        if (!window_->PollEvents()) {
            if (!exitRequested_) exitRequested_ = true;
        }
        // ESC 边沿：Play 中 = Stop（编辑器惯例）。Edit 态 ESC 不再触发退出——
        // 误按一下就整体退出对编辑器太危险（原 anim-smoke 骨架遗留行为，M4.6 移除）
        const bool esc = window_->IsKeyDown(Key::Escape);
        if (esc && !escHeld_ && ctx_.Playing()) {
            if (!StopPlay()) LEMON_WARN("Stop 后快照校验失败（编辑场景已按快照重建）");
        }
        escHeld_ = esc;
        // 外部拖拽导入（M4.6 §5-3）：OS drop 文件 → 当前资产目录（无项目 = 可操作红字）
        for (const std::string& f : window_->TakeDroppedFiles()) ImportDroppedFile(f);
        // 编译队列执行（M4.6 §5-5）：排队发生在上帧 → 上帧状态栏已画"编译中…"，
        // 本帧才真正阻塞构建（dotnet 1–2s 期间屏幕留提示帧）
        if (compileQueued_) {
            compileQueued_ = false;
            TryHotReloadScripts(compileQueuedReason_.c_str());
        }
        // --smoke-close（M4.6 §4-9）：关闭状态机交互冒烟注入
        //   clean：干净场景下请求退出 → 应"不弹确认且立即退出"（b7094a9 修复回归线）
        //   dirty：置脏 → 请求退出 → 应弹确认（armed）→ 模拟"丢弃并退出"（forceExit）
        if (launch.smokeClose == "clean") {
            if (frame == 30) exitRequested_ = true;
        } else if (launch.smokeClose == "dirty") {
            if (frame == 30) ctx_.CreateSpriteEntity("close-probe"); // CreateEntity 置脏
            if (frame == 45) exitRequested_ = true;
            if (frame == 60 && quitConfirmArmed_) forceExit_ = true; // = 点"丢弃并退出"
        }
        // 资产热替换（M4.4）：watcher 置脏 → 重扫 + 增量导入（改文件落盘即时可见）
        if (watcher_.Running() && watcher_.ConsumeDirty()) RescanAssets();
        // C# 热重载（M4.5 §3.7）：Game/ 源写 → 防抖 0.4s（编辑器连续保存不打断）→ 编译+换装
        // （M4.6 §5-5：改走编译队列——先画一帧"编译中…"再阻塞）
        // F-15（2026-09-24）：防抖窗内取走的脏事件转 pending（DebounceGate）——原实现
        // 直接清标志，窗内第二次保存不再触发编译（吞事件）
        if (scriptWatcher_.Running() && scriptWatcher_.ConsumeDirty() && !launch.finalTest)
            reloadGate_.OnDirty(ImGui::GetTime());
        if (reloadGate_.Due(ImGui::GetTime())) {
            if (ScriptSourceChanged()) QueueScriptRebuild("源码变更");
        }
        // 自动备份（§3.8）：5 分钟节拍，dirty 且非 Play 才写
        ctx_.TickAutosave(ImGui::GetTime() - autosaveClock0);
        if (launch.frames > 0 && (int)frame >= launch.frames) running = false;
        if (forceExit_) running = false;
        // 退出裁决：干净场景立即退出；脏场景弹一次确认（M4.6 修复——原先干净场景下
        // exitRequested_ 无任何消费路径，点关闭按钮毫无反应，直到场景变脏那帧才弹框）
        if (exitRequested_) {
            if (!ctx_.dirty) {
                running = false;
            } else if (!quitConfirmArmed_) {
                quitConfirmOpen_ = true; // 退出前确认（一次）
                confirmContext_ = ConfirmContext::Exit; // 上一次 SceneOp 不残留
                exitRequested_ = false;
            } else {
                // 确认框已开（本条或 SceneOp 的）：吸收重复退出请求。原样残留会越过
                // 非退出分支（SceneOp 保存/丢弃后 dirty 即清）——下一帧走到"干净直接
                // 退"，用户只是开了个场景，编辑器却无提示退出
                exitRequested_ = false;
            }
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
        // 批③b：UI 文档/样式热重载两段——中点①改 .rml（标题色金→绿）、②改 .rcss
        // （正文色灰→蓝），落盘后直接 RescanAssets（确定性触发；watcher 亦会到但
        // 二次重扫 hash 未变 = no-op）。终态像素断言见 Run 尾裁决段
        // --smoke-uirml 帧链——外迁 EditorAppSmokeUirml.cpp（批③c-5：挂点原位）
        SmokeUirmlFrame(frame);
        // 终验（§6 #1/#2/#6/#7）：Play 中热重载——改 .cs 落盘 → 编译换装 → 新逻辑 +
        // StateBag 续跑（刷怪窗口 40→70；续跑总刷怪 66 = 换装前 16 + 换装后 50）
        // --final Play 中热重载播种——外迁 EditorAppFinal.cpp（批③c-6：挂点原位）
        FinalFrame(frame);
        if (ctx_.Playing()) {
            if (playDiag_ && !playDiagPlayingSeen_) { // 诊断探针：Playing 分支首帧
                playDiagPlayingSeen_ = true;
                std::printf("[playdiag] playing branch at f=%llu\n",
                            (unsigned long long)frame);
            }
            // 输入路由：GameView 聚焦且非文本输入 → 语义子集（WASD/箭头/空格）进 Play World
            ecs::InputState in;
            // 批③c（M7 让出）：RmlUi 文本控件持有键盘（WantsKeyboard）或模态文档
            // 打开（AnyModalShown）= 游戏输入让出——"菜单打开时脚本让出输入"规则化
            const bool uiHoldsInput =
                gameUi_ && (gameUi_->WantsKeyboard() || gameUi_->AnyModalShown());
            if (gameViewFocused_ && !ImGui::GetIO().WantTextInput && !uiHoldsInput) {
                float ax = 0, ay = 0;
                if (ImGui::IsKeyDown(ImGuiKey_A) || ImGui::IsKeyDown(ImGuiKey_LeftArrow)) ax -= 1.0f;
                if (ImGui::IsKeyDown(ImGuiKey_D) || ImGui::IsKeyDown(ImGuiKey_RightArrow)) ax += 1.0f;
                if (ImGui::IsKeyDown(ImGuiKey_W) || ImGui::IsKeyDown(ImGuiKey_UpArrow)) ay -= 1.0f;
                if (ImGui::IsKeyDown(ImGuiKey_S) || ImGui::IsKeyDown(ImGuiKey_DownArrow)) ay += 1.0f;
                in.ax = ax;
                in.ay = ay;
                if (ImGui::IsKeyDown(ImGuiKey_Space)) in.buttons |= 1u << 4; // bit4 attack
                if (ImGui::IsKeyDown(ImGuiKey_R)) in.buttons |= 1u << 5;     // bit5 confirm（M5 批④：模板重开/确认）
                // 批③d-2 bit6 pause：Esc/P（Esc 与 ImGui 弹窗争键面 = 各面板自身
                // widget 活动态，GameView 聚焦门控下互斥；P 别名保底）
                if (ImGui::IsKeyDown(ImGuiKey_Escape) || ImGui::IsKeyDown(ImGuiKey_P))
                    in.buttons |= 1u << 6;
            }
            if (playDiag_ && frame >= 60 && frame < 120)
                in.ax = 1.0f; // 诊断注入：D 键右走（自动化无真人点击，不经聚焦门）
            // --smoke-template 转向注入——外迁 EditorAppSmokeTpl.cpp（批③c-4：挂点原位）
            SmokeTplSteer(frame, in);
            ctx_.ActiveWorld().ApplyInput(in);
            bPump = BenchClock::now(); // 段界：pump（轮询/watcher/自动备份）结束 = sim 开始
            audio_.Tick(ImGui::GetIO().DeltaTime); // M6c 竖切批：声部回收 + 静音模式游标推进
            // 固定步长累加器（2026-09-29 复审 2a/2b）：模拟速率此前 = 渲染帧率
            //（vsync Fifo 下 144Hz 屏跑 2.4 倍速）。交互 Play 改墙钟进账 → N × 1/60
            // 出账（追帧上限 5 步，停顿后不快进），余账/步长 = 渲染插值 alpha
            //（此前 ViewportRenderer 传字面量 1.0 = 恒取 cur，插值机制空转）。
            // 自动化链（smoke*/bench*/--frames/--final/playDiag）保持每渲染帧恰
            // 一步 + alpha=1——帧号 = tick 号，像素断言/回放口径逐位不变。
            const bool playPaced = !(launch.frames > 0 || launch.smoke || launch.smokeDrag ||
                                     launch.smokeUi || launch.smokeAnim || launch.smokeGuid ||
                                     launch.smokeUirml || launch.smokeTemplate ||
                                     launch.benchSurvivor || launch.benchScene ||
                                     launch.finalTest || playDiag_);
            constexpr float kPlayFixedDt = 1.0f / 60.0f;
            constexpr float kPlayMaxAcc = kPlayFixedDt * 5.0f; // 追帧上限（死亡螺旋钳）
            constexpr int kPlayMaxSteps = 5;
            // M6c 批②：音频监听器每帧推给 Play World（活动相机位 + gameRT 视口半宽；
            // 上一帧跟随值——一帧延迟口径 = FeedGameUiInput 先例，空间化无感）
            {
                const Camera2D& gc = viewport_->GameCam();
                const uint32_t rtW = viewport_->RenderTargetWidth(1),
                              rtH = viewport_->RenderTargetHeight(1);
                audio::AudioListener l;
                l.center = gc.center;
                l.halfWidth = rtH > 0 ? gc.HalfWidth((float)rtW / (float)rtH) : gc.halfHeight;
                ctx_.ActiveWorld().SetAudioListener(l);
            }
            int playSteps = 0;
            if (!playPaced) {
                ctx_.TickPlay(paused_ && !singleStep_ ? 0.0f : kPlayFixedDt);
                playAlpha_ = 1.0f;
            } else if (paused_ && !singleStep_) {
                ctx_.TickPlay(0.0f); // 暂停：Essential（销毁提交）照跑（原语义）
                playAlpha_ = 1.0f;
            } else {
                playAcc_ += ImGui::GetIO().DeltaTime;
                if (playAcc_ > kPlayMaxAcc) playAcc_ = kPlayMaxAcc;
                const bool stepping = singleStep_;
                if (stepping) {
                    ctx_.TickPlay(kPlayFixedDt);
                    playAcc_ = 0.0f;
                    playSteps = 1;
                } else {
                    while (playAcc_ >= kPlayFixedDt && playSteps < kPlayMaxSteps) {
                        ctx_.TickPlay(kPlayFixedDt);
                        playAcc_ -= kPlayFixedDt;
                        ++playSteps;
                    }
                }
                if (playSteps == 0) ctx_.TickPlay(0.0f); // 空转帧：Essential 照跑（暂停同款）
                // 单步 = 直接呈现步后状态（alpha=0 会"慢一拍"——渲染步前 prev）；
                // 常规帧 = 余账比例（prev→cur 插值）
                playAlpha_ = stepping ? 1.0f : playAcc_ / kPlayFixedDt;
            }
            bSim = BenchClock::now();  // 段界：sim（世界步进，含追帧多步）结束 = glue 开始
            singleStep_ = false;
            UpdateGameCameraFollow();
            FeedGameUiInput(); // 批③c（M7）：鼠标/键盘喂入 + IME 锚点换算（Update 前）
            if (gameUi_) gameUi_->Update(); // 批③a：UI 帧逻辑（World 步进后、渲染前）
            // 批③c review P2：画布/焦点是"上一帧面板绘制上报、本帧头部消费"的一帧
            // 延迟数据——消费后即失效。GameView 关闭（OnGui 不再跑）时残值原本会
            // 一直喂鼠标/键盘/IME 到错误位置；面板若仍开着，帧尾 BuildUI 会重新上报。
            gvCanvasValid_ = false;
            gvCanvasHovered_ = false;
            gameViewFocused_ = false;
            if (playDiag_ && frame >= 2 && frame < 220) {
                // 逐帧：墙钟帧耗时（pacing）/ 相机中心 / 跟随目标 / gameRT 尺寸（重建
                // 翻转即 churn）。f60-120 走、121+ 停——抖动段应能在 dt 或 cam 序列现形
                const auto nowD = std::chrono::steady_clock::now();
                const float wallMs =
                    playDiagPrev_.time_since_epoch().count() == 0
                        ? 0.0f
                        : std::chrono::duration<float, std::milli>(nowD - playDiagPrev_).count();
                playDiagPrev_ = nowD;
                if (playDiagHasTarget_) {
                    const Camera2D& gc = viewport_->GameCam();
                    std::printf("[playdiag] f=%llu dt=%.2f cam=(%.3f,%.3f) tgt=(%.3f,%.3f) "
                                "rt=%ux%u\n",
                                (unsigned long long)frame, wallMs, gc.center.x, gc.center.y,
                                playDiagTarget_.x, playDiagTarget_.y,
                                viewport_->RenderTargetWidth(1),
                                viewport_->RenderTargetHeight(1));
                }
            }
        } else {
            playAcc_ = 0.0f;   // 出 Play 清账（复审 2a）
            playAlpha_ = 1.0f; // 编辑态渲染恒取 cur（无插值）
            ctx_.TickEditor(1.0f / 60.0f); // Essential（销毁提交）+ 空 FixedTick
            UpdateGameCameraFollow();  // 非 Play：退出跟随时回默认位
        }

        // 冒烟悬停扫掠（M4.5）：逐帧走窗口网格 → 会话内所有可见控件至少被悬停
        // 一次——ImGui 的 ID 冲突检查挂 HoveredId 路径，不悬停就永远测不到。
        // 经 SetMouseOverride 注入（SDL 后端每帧轮询真实鼠标，普通事件会被盖掉；
        // 覆盖口在轮询后、NewFrame 排水前生效）。
        if (launch.smoke &&
            !(launchCopy_.smokeUirml && (frame == 60 || frame == 61))) {
            // 批③c 注：smoke-uirml 帧 60/61 是 UI 点击注入帧——扫掠让位（否则覆写
            // 注入位置，RmlUi 收不到画布坐标的按下/释放）
            ImGuiIO& io = ImGui::GetIO();
            if (io.DisplaySize.x > 1.0f && io.DisplaySize.y > 1.0f) {
                constexpr uint64_t kCols = 40, kRows = 15;
                const uint64_t idx = frame % (kCols * kRows);
                ui_->SetMouseOverride(
                    (float)(idx % kCols) / (float)(kCols - 1) * io.DisplaySize.x,
                    (float)(idx / kCols) / (float)(kRows - 1) * io.DisplaySize.y);
            }
        }
        // 冒烟末帧：强制主选中 = 首个精灵实体（overlay 像素断言的选框/手柄原料。
        // --play 换世界 / --scene 重开路径下既有选区可能指向失效实体，需确定性供给）
        if (launch.smoke && launch.frames > 0 && (int)frame == launch.frames - 1) {
            for (auto [ent, tf, sr] :
                 ctx_.ActiveScene().View<ecs::Transform2D, ecs::SpriteRenderer>().each()) {
                (void)tf;
                if (sr.flags & 0x4) { // SpriteRenderer.flags bit2 = enabled
                    ctx_.Select(ecs::Scene::FromEntt(ent), false);
                    break;
                }
            }
            if (launch.smokeTemplate) // 玩家（16×32 hero：环像素稳定过阈；池序首灵
                // 可能是 16×16 子弹，AA 后 <20px 阈值误报）+ 相机对焦（风筝后玩家
                // 大概率在视口外——环被裁 = sel 误报 0）
                ctx_.ActiveScene().Each([&](ecs::Entity e) {
                    if (const ecs::Meta* m = ctx_.ActiveScene().TryGet<ecs::Meta>(e);
                        m && std::strcmp(m->tag, "Player") == 0) {
                        ctx_.Select(e, false);
                        Camera2D& cam = viewport_->SceneCam();
                        cam.zoom = 1.0f;
                        cam.halfHeight = 360.0f;
                        cam.center = ctx_.ActiveScene().Get<ecs::Transform2D>(e).pos;
                    }
                });
        }

        // --smoke-drag（M4.7c 交互回归）——注入状态机外迁 EditorAppSmoke.cpp
        //（批③c-1：帧号锚定/执行时序逐位不变，挂点原位）
        SmokeDragFrame(frame);


        // ---- 热修③（2026-09-27 用户实测：创建后面板空态无入口）真实模态点击
        // 全链复现：播种块 OpenAnimationCreateSet("") 已排好模态（名 player、无
        // 源目录 = 纯空集），此处注入点击"创建"（TestHooks 矩形 + 真实 ImGui
        // 管线，smoke-ui 同款 hold/release 隔帧模式）→ TryCreateSet 落盘
        // Assets/player.override → Rescan → OpenSet；f45 断言 setGuid_ 已切到
        // 新集（空集工作台 = 左列可见，"＋ 新建"入口在位）。
        // --smoke-anim 帧链——外迁 EditorAppSmoke.cpp（批③c-3：挂点原位）
        SmokeAnimFrame(frame);

        // --smoke-ui（M4.7d 收尾轮）——注入状态机外迁 EditorAppSmoke.cpp
        //（批③c-2：帧号锚定/执行时序逐位不变，挂点原位——须在 ui_->BeginFrame 前）
        SmokeUiFrame(frame);


        bUi0 = BenchClock::now(); // 段界：glue（相机跟随/冒烟注入选中）结束 = ImGui 开始
        ui_->BeginFrame(*window_);
        BuildUI();
        if (quitConfirmArmed_) smokeCloseArmedEver_ = true; // dirty 模式断言原料
        // 标题栏（M4.6 §4-5）：<场景>[●] — <项目> — Lemon（变更才调 SDL）
        {
            const std::string& root = ctx_.Assets().ProjectRoot();
            const std::string title =
                ctx_.SceneName() + (ctx_.dirty ? " ●" : "") + " — " +
                (root.empty() ? std::string("未打开项目")
                              : std::filesystem::path(root).filename().string()) +
                " — Lemon";
            if (title != curTitle_) {
                curTitle_ = title;
                window_->SetTitle(title.c_str());
            }
        }
        bUi1 = BenchClock::now(); // 段界：ImGui（BeginFrame+BuildUI+标题）结束

        rhi::AcquireResult acq = device_->AcquireNextImage();
        if (acq.deviceLost || acq.needsRecreate) {
            // OUT_OF_DATE 重建成功也必须跳过本帧（评审 D2）：旧 imageIndex 属于已销毁
            // 的链——落下去 = present 未 acquire 的图像 + 旧下标取新信号量表。下一帧
            // AcquireNextImage 会取新索引。ui_->BeginFrame 已开 ImGui 帧，跳帧前须
            // EndFrame 收掉（否则下一轮 NewFrame 撞未关帧断言）
            if (!acq.deviceLost) device_->RecreateSwapchain();
            ui_->SkipFrame();
            continue;
        }
        rhi::CommandList& cl = device_->BeginFrame();
        bAcq = BenchClock::now(); // 段界：acquire+BeginFrame 结束 = 场景渲染开始
        const uint32_t w = device_->SwapchainWidth(), h = device_->SwapchainHeight();
        viewport_->Render(cl, ctx_, playAlpha_); // 双视口离屏（BuildUI 已定 RT 尺寸/注入 overlay；alpha = 复审 2b 插值系数）
        bScene = BenchClock::now(); // 段界：场景 RT（ExtractScene + 双视口绘制）结束

        const float clear[4] = {0.055f, 0.06f, 0.08f, 1.0f};
        cl.BeginPass(device_->SwapchainFormat(), w, h, clear);
        cl.SetViewportScissor(w, h);
        ui_->Render(cl);
        cl.EndPass();
        bUiDraw = BenchClock::now(); // 段界：ImGui 渲染编码（swapchain pass）结束

        // ID 冲突信号轮询（M4.5）：冲突提示由 ImGui 直接画 tooltip、不走
        // ErrorCallback——帧末读 DebugDrawIdConflictsId（悬停扫掠命中 >1 同 ID 项
        // 时非零）。配合扫掠 = 无头冒烟可真实抓到这类交互期错误。
        if (launch.smoke && !imguiIdConflictSeen_ &&
            ImGui::GetCurrentContext()->DebugDrawIdConflictsId != 0) {
            imguiIdConflictSeen_ = true;
            ++g_imguiErrorCount;
            LEMON_WARN("ImGui：可见控件 ID 冲突（悬停扫掠命中；循环内控件需 PushID 或 ##xx 唯一化）");
        }

        const bool wantCapture =
            !launch.screenshot.empty() || launch.smoke || launchCopy_.smokeUirml;
        // 批③d 前置 T5：层序断言两段中点捕获（171/191 钩子取回数色——见帧钩子段）；
        // 真人验收②二轮：243/347 = 僵尸渲染防线捕获（删文档后活画面像素清零断言源）；
        // 形态 D：411 = 删声明实体后第四局活画面（残留显示断言源）
        const bool midUirmlCapture = launchCopy_.smokeUirml &&
                                     (frame == 170 || frame == 190 || frame == 252 ||
                                      frame == 347 || frame == 411);
        const bool lastFrame =
            (launch.frames > 0 && (int)frame == launch.frames - 1 && wantCapture) ||
            midUirmlCapture;
        if (lastFrame) {
            cl.DebugRecordCapture();
            // 场景 RT 回读（冒烟像素断言源：线性空间、无 UI 合成/sRGB 干扰）；
            // 批③a smoke-uirml 改读 gameRT（RmlUi 面板像素断言源，同线性空间）
            if (launchCopy_.smokeUirml) {
                if (viewport_->GameRenderTarget().IsValid())
                    cl.DebugRecordTextureCapture(viewport_->GameRenderTarget());
            } else if (launch.smoke && viewport_->SceneRenderTarget().IsValid()) {
                cl.DebugRecordTextureCapture(viewport_->SceneRenderTarget());
            }
        }
        // 批③d-1：smoke-template 层序三拍捕获（动态时点——证据块状态机置请求位）
        // ——外迁 EditorAppSmokeTpl.cpp（批④：挂点原位、时序逐位不变）
        SmokeTplCapture(cl);

        bool needRe = false, lost = false;
        device_->EndFrameAndPresent(needRe, lost);
        bPresent = BenchClock::now(); // 段界：present（提交+呈现+可能的先前帧围栏等待）
        if (lost || needRe) {
            if (lost || !device_->RecreateSwapchain()) continue;
        }
        viewport_->AdvanceFrame();
        // 终验 fps 统计（§6 #6：判据场景 Play ≥45fps；预热 60 帧与换装窗口 90 帧剔除
        // ——dotnet build 同步阻塞主线程属换装耗时，不计帧率口径）
        // --final fps 采样——外迁 EditorAppFinal.cpp（批③c-6：挂点原位）
        FinalSample(frame);
        // M5 批③ smoke-anim 证据采样：clip 命中表 → curFrame 推进 + spriteId 落切片区间
        // --smoke-anim 帧采样——外迁 EditorAppSmoke.cpp（批③c-3：挂点原位）
        SmokeAnimSample(frame);
        // M5 批④ smoke-template 证据采样：HUD 四要素行齐 / 存档载入（best=123 回显）/
        // 波次行 / 三选一卡片链（出现 → 注入选择（模拟数字键 1）→ 消费后隐藏）
        // --smoke-template 帧采样——外迁 EditorAppSmokeTpl.cpp（批③c-4：挂点原位）
        SmokeTplSample(frame);
        if (firstFrameMs < 0.0)
            firstFrameMs = std::chrono::duration<double, std::milli>(
                               std::chrono::steady_clock::now() - tColdStart)
                               .count();
        // --bench 帧段累计——外迁 EditorAppBench.cpp（批③c-6：挂点原位）
        BenchSample(frame, benchT0, bPump, bSim, bUi0, bUi1, bAcq, bScene,
                    bUiDraw, bPresent);
        ++frame;
    }

    bool playVerified = true;
    double playEnterMs = 0, playExitMs = 0;
    uint32_t playAliveAtStop = 0;
    // --bench 停跑证据采集——外迁 EditorAppBench.cpp（批③c-6：挂点原位）
    playAliveAtStop = BenchCaptureStop();
    if (ctx_.Playing()) { // --play：跑满帧数后 Stop（恢复编辑世界）
        playAliveAtStop = ctx_.ActiveScene().AliveCount();
        playEnterMs = ctx_.LastEnterPlayMs();
        playVerified = StopPlay();
        playExitMs = ctx_.LastExitPlayMs();
    }

    // ---- 冒烟自检（§6 #13：退出码即判据）----
    int exitCode = 0;
    // --smoke-drag 裁决（M4.7c 交互回归）——外迁 EditorAppSmoke.cpp（批③c-1）
    if (launch.smokeDrag && !SmokeDragVerdict()) exitCode = 1;
    // --smoke-ui 裁决（裁决行已在帧 154 打印；此处只定退出码）
    // --smoke-ui 裁决（裁决行已在帧 154 打印；此处只定退出码）——外迁（批③c-2）
    if (launch.smokeUi && !SmokeUiVerdict()) exitCode = 1;
    // --bench-survivor 裁决（M5 清障③；08 §3 判据：编辑器内 1 万怪 ≥45fps）
    // --bench 裁决——外迁 EditorAppBench.cpp（批③c-6）
    if (!BenchVerdict(frame, playAliveAtStop)) exitCode = 1;
    // --smoke-close 裁决（M4.6 §4-9）：独立于 --smoke——专用最小跑（无项目/无播种）
    if (!launch.smokeClose.empty()) {
        const bool exitedEarly = launch.frames > 0 && frame < (uint64_t)launch.frames;
        bool ok = false;
        if (launch.smokeClose == "clean") {
            ok = exitedEarly && !smokeCloseArmedEver_; // 干净场景：不弹确认、立即退出
            std::printf("[lemon] smoke-close clean: exitedEarly=%d confirmShown=%d => %s\n",
                        exitedEarly ? 1 : 0, smokeCloseArmedEver_ ? 1 : 0, ok ? "OK" : "FAIL");
        } else if (launch.smokeClose == "dirty") {
            ok = exitedEarly && smokeCloseArmedEver_; // 脏场景：先弹确认再丢弃退出
            std::printf("[lemon] smoke-close dirty: confirmShown=%d exitedEarly=%d => %s\n",
                        smokeCloseArmedEver_ ? 1 : 0, exitedEarly ? 1 : 0, ok ? "OK" : "FAIL");
        } else { // 未知值（测试报告观察 2）：此前无诊断静默 exit 1——值校验已在
            // EditorEntry 拒启，此处兜底防未来新增入口漏校验
            std::printf("[lemon] smoke-close: 未知值 '%s'（应为 clean|dirty）=> FAIL\n",
                        launch.smokeClose.c_str());
        }
        if (!ok) exitCode = 1;
    }
    // 帧末截屏回读（--screenshot 落盘 + 冒烟像素断言共用一次回读）
    std::vector<uint8_t> capturePx;
    uint32_t captureW = 0, captureH = 0;
    bool screenshotOk = true; // 观察项（测试报告观察 2）：写失败并入冒烟汇总谓词
    const bool haveCapture = device_->DebugFetchCapture(capturePx, captureW, captureH);
    if (!launch.screenshot.empty()) {
        if (haveCapture) {
            std::error_code ec;
            if (auto p = std::filesystem::path(launch.screenshot).parent_path(); !p.empty())
                std::filesystem::create_directories(p, ec);
            int ok = stbi_write_png(launch.screenshot.c_str(), (int)captureW, (int)captureH, 4,
                                    capturePx.data(), (int)captureW * 4);
            std::printf("[lemon] editor-smoke screenshot: %s %ux%u => %s\n",
                        launch.screenshot.c_str(), captureW, captureH, ok ? "written" : "FAILED");
            screenshotOk = ok != 0;
            exitCode |= ok ? 0 : 1;
        } else {
            std::printf("[lemon] editor-smoke screenshot: capture FAILED\n");
            screenshotOk = false;
            exitCode = 1;
        }
    }
    if (launch.smoke) {
        const bool drew = ImGui::GetCurrentContext() && ImGui::GetDrawData() &&
                          ImGui::GetDrawData()->TotalVtxCount > 0;
        bool cjkOk = false;
        if (ImFont* f = ImGui::GetFont()) cjkOk = f->IsLoaded() && f->IsGlyphInFont(0x4E2D);
        const uint64_t errCount = LogCountOf(LogLevel::Error);
        if (g_imguiErrorCount > 0)
            std::printf("[lemon] editor-smoke imgui-errors=%d（ID 冲突/空标签等）=> FAIL\n",
                        g_imguiErrorCount);
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
        // M4.4 脚本链验收：--script + --play → SpawnerBehaviour 每帧刷怪（36 只）。
        // 批③d-2：模板模式豁免——菜单先行使 Stop 时点（回菜单态）无 run 实体，
        // spawn 证明由 smoke-template 流程链（重开重挂/mobs/gems 峰值）承接
        bool scriptOk = true;
        if (host_ && launch.playTest && !launch.finalTest && !launch.smokeTemplate &&
            smokeSpawnScript_) {
            scriptOk = playAliveAtStop > smokeSeeded_ + 10;
            std::printf("[lemon] editor-smoke script-spawn: playAlive=%u seeded=%u => %s\n",
                        playAliveAtStop, smokeSeeded_, scriptOk ? "OK" : "FAIL");
        }
        // M5 批③动画链验收：切片记账 + clip 建表 + 帧映射推进 + spriteId 落切片区间。
        // 程序化 4 帧表必验；yami hero-walk 素材在场（Samples 拷入项目）即连带验真链。
        // M6a 批①扩：切段链（Play(hit)+Queue(walk) → hit 在场 → 收尾回 walk）+ fx 通道。
        // --smoke-anim 裁决——外迁 EditorAppSmoke.cpp（批③c-3）
        if (launch.smokeAnim && !SmokeAnimVerdict()) exitCode = 1;
        // M5 批④模板链验收：向导复制 → build → Play 全链在跑（能到这 = 前两环已过）；
        // 断言 HUD 四要素 / 存档载入回显 / 波次 / 击杀 / 升级卡片出现-选择-隐藏。
        // 批③d-1：HUD/卡片断言已随迁文档面（hud(doc)=TryGetElementText / cards 容器
        // 计数 + 合成点击 + IsDocumentShown）；layer = 层序三拍（scrim 压暗复原）；
        // uidoc = 通道 A 装载恰 2（HUD+cards 场景声明）。
        // --smoke-template 裁决——外迁 EditorAppSmokeTpl.cpp（批③c-4）
        if (launch.smokeTemplate && !SmokeTplVerdict()) exitCode = 1;
        // ---- M4.5 终验（§6 #1/#2/#3/#6/#7 全量化）----
        bool finalOk = true;
        // --final 验收——外迁 EditorAppFinal.cpp（批③c-6）
        finalOk = FinalVerdict(playAliveAtStop, playEnterMs, playExitMs,
                                playVerified, firstFrameMs);
        // M4.7-P0 冒烟防线：overlay 渲染可见性像素断言（扫场景 RT——线性空间原值，
        // 无 UI 合成与 sRGB 编码干扰）。此前"推入正常但绘制侧全灭"的缺陷穿透了
        // 全部自动化（都只数包不数像素）。四要素特征色：网格（灰系淡带）/主选框
        // （亮青，α255 原值）/Gizmo 手柄（黄，α255 原值）/标签墨（α230 混底 ≈219,233,184）。
        // 容差避开调色板近似色（青 80,220,220 / 黄 250,220,60 / 白 255 三者距离均超带）；
        // 网格阈值取 8000：棋盘精灵暗格 (60,60,60) 同在灰带（≤4096px）不足以假阳。
        bool overlayOk = true;
        {
            std::vector<uint8_t> rt;
            uint32_t rw = 0, rh = 0;
            if (device_->DebugFetchTextureCapture(rt, rw, rh)) {
                auto rgb = [](uint32_t c, int i) { return (int)((c >> (i * 8)) & 0xFF); };
                const int selN = CountPixelsNear(
                    rt, rw, rh, rgb(overlay::kPrimaryColor, 0), rgb(overlay::kPrimaryColor, 1),
                    rgb(overlay::kPrimaryColor, 2), 10);
                const int handleN = CountPixelsNear(
                    rt, rw, rh, rgb(overlay::kHandleColor, 0), rgb(overlay::kHandleColor, 1),
                    rgb(overlay::kHandleColor, 2), 12);
                const int labelN = CountPixelsNear(rt, rw, rh, 219, 233, 184, 22);
                const int gridN = CountGridishPixels(rt, rw, rh);
                overlayOk = selN >= 20 && handleN >= 20 && labelN >= 20 && gridN >= 8000;
                std::printf("[lemon] editor-smoke overlay-visible: grid=%d(≥8000) sel=%d(≥20) "
                            "handle=%d(≥20) label=%d(≥20) => %s\n",
                            gridN, selN, handleN, labelN, overlayOk ? "OK" : "FAIL");

            } else {
                overlayOk = false; // 场景 RT 回读失败 = 断言原料缺失，按失败计
                std::printf("[lemon] editor-smoke overlay-visible: 场景 RT 回读缺失 => FAIL\n");
            }
        }
        if (!drew || !cjkOk || errCount > 0 || !sceneOk || !playOk || !assetsOk || !scriptOk ||
            !finalOk || !overlayOk || !screenshotOk || g_imguiErrorCount > 0) {
            std::printf("[lemon] editor-smoke FAIL\n");
            exitCode = 1;
        } else {
            std::printf("[lemon] editor-smoke PASS\n");
        }
    } else {
        std::printf("[lemon] editor exit: frames=%llu cold-start=%.0fms\n",
                    (unsigned long long)frame, firstFrameMs);
    }

    // 批③b（ADR-014）：UI 资产通道冒烟——③a 像素断言（面板/标题位置）+ 本批四通道：
    // 字体（font 必须是引擎正字 Noto Sans SC）/ 文档（夹具 .rml 经文件通道装载）/
    // 贴图桥（<img> → 图集页 → #d04080 像素过阈）/ 热重载（终态 = 中点两段改写后的
    // 绿标题 + 蓝正文，旧色残留 < 5 证明确实重载）。独立裁决链（读 gameRT）
    // --smoke-uirml 裁决——外迁 EditorAppSmokeUirml.cpp（批③c-5）
    if (launchCopy_.smokeUirml && !SmokeUirmlVerdict()) exitCode = 1;

    watcher_.Stop();          // 先停 watcher 线程（此后无资产重扫）
    scriptWatcher_.Stop();    // 与 Game/ 源监视同批收尾
    host_.reset();            // C# 宿主卸载（无脚本时为空操作）
    device_->WaitIdle(); // ImGui 后端资源（描述符池/采样器）可能被在途帧引用，先等闲
    ui_->Shutdown();
    StopAudioBaker(); // 批①：后台烤制线程先于 AudioEngine 成员析构收口
    SetLogSink(nullptr, nullptr);
    device_->SavePipelineCache();
    if (gameUi_) { // 批③a：UI 子系统先于 viewport/device 收尾（Rml 收尾仍回调后端 + WaitIdle + 反注册）
        s_gameUiForHooks = nullptr; // 批③c：桥钩子目标先清（防收尾期 TickBatch 悬垂）
        scripting::SetUiHooks({nullptr, nullptr});
        gameUi_->Shutdown();
        gameUi_.reset();
    }
    viewport_.reset(); // 视口（含合批器）须先于设备拆毁：SpriteBatcher 析构反注册
                       // 设备丢失回调（M9），设备已亡 = 解引用死指针（实测 SIGSEGV）
    device_.reset();
    window_.reset();
    return exitCode;
}

namespace {
// 标签大小写不敏感比较（Meta.tag 固定 24B，无终止符风险由调用方保证）
bool TagEquals(const char* tag, const char* want) {
    if (!tag) return false;
    while (*tag && *want) {
        if (std::tolower((unsigned char)*tag) != std::tolower((unsigned char)*want)) return false;
        ++tag;
        ++want;
    }
    return *tag == *want;
}
} // namespace

void EditorApp::UpdateGameCameraFollow() {
    // M4.7 手测修复：Play 中游戏相机钉死 (640,360)，玩家 WASD 走出视野后"消失"。
    // 目标优先级（M4.md §2.2 GameView"场景中 Camera 实体"的标签化落地）：
    //   ① tag "Camera"——显式相机位实体（进阶：也可作空场景的固定取景）；
    //   ② tag "Player"——默认跟随玩家；
    //   ③ 首个挂脚本实体——blank 模板默认名"Sprite"+InputMover 的兜底。
    // 首帧吸附（不从旧位滑过去），之后 Camera2D::Follow 指数阻尼（02 §3.5）。
    // M4.3 后若 C# 相机门面落地，脚本驱动可覆盖（编辑器跟随仅兜底语义）。
    Camera2D& cam = viewport_->GameCam();
    if (!ctx_.Playing()) {
        if (gameFollowActive_) { // 退出 Play → 编辑态默认位（ViewportRenderer 口径）
            cam.center = {640, 360};
            gameFollowActive_ = false;
        }
        return;
    }
    ecs::Scene& s = ctx_.ActiveScene();
    ecs::Entity camEnt = ecs::Entity::Null(), playerEnt = ecs::Entity::Null(),
                scriptedEnt = ecs::Entity::Null();
    s.Each([&](ecs::Entity e) {
        const bool hasTf = s.Has<ecs::Transform2D>(e);
        if (!hasTf) return;
        const ecs::Meta* m = s.TryGet<ecs::Meta>(e);
        const char* tag = m ? m->tag : nullptr;
        if (camEnt.IsNull() && TagEquals(tag, "Camera")) camEnt = e;
        if (playerEnt.IsNull() && TagEquals(tag, "Player")) playerEnt = e;
        if (scriptedEnt.IsNull() && s.Has<scripting::ScriptBox>(e)) scriptedEnt = e;
    });
    const ecs::Entity target = !camEnt.IsNull()     ? camEnt
                               : !playerEnt.IsNull() ? playerEnt
                                                     : scriptedEnt;
    if (target.IsNull()) return; // 无目标：保持现位
    ecs::WorldTransform2D wt{};
    Vec2 pos = s.Get<ecs::Transform2D>(target).pos; // 父链异常兜底本地位
    if (ecs::ComputeWorldTransform(s, target, wt)) pos = wt.pos;
    playDiagTarget_ = pos;      // LEMON_PLAY_DIAG 回传（帧循环节奏诊断）
    playDiagHasTarget_ = true;
    if (!gameFollowActive_) {
        gameFollowActive_ = true;
        const char* tag = "脚本实体";
        if (!camEnt.IsNull()) tag = "Camera";
        else if (!playerEnt.IsNull()) tag = "Player";
        LEMON_LOG("游戏相机跟随：%s", tag);
    }
    // 手测第十轮：刚性跟随（center = 目标，零滞后）。阻尼版（rate 5/9 两轮实测）
    // 的稳态滞后在走/停切换时反演成 ~28px 往返滑移 + 亚像素爬行 = 「抖动」观感；
    // LEMON_PLAY_DIAG 数据证明帧节奏/RT/收敛曲线本身全平顺，锅在滞后动态。
    // 电影感阻尼留给 C# 相机门面（M4.3 规划）按需启用 Camera2D::Follow。
    cam.center = pos;
}

} // namespace lemon::editor
