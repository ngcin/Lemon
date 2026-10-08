// Lemon 编辑器 — EditorApp 项目与 Play 管线（项目打开管线/Play 入口守卫与
// Stop 清场/新建项目与恢复模态；M4.5 §3.7 起）。
// 批② 2026-09-29 自 EditorApp.cpp 机械拆分：成员函数跨 TU 定义，类定义
// App/EditorApp.h 零改动，代码逐行原样。
// 批④ 2026-09-30：UIDocument 装载与对账两函数外迁 App/EditorAppUiBridge.cpp。
// 批④-2 2026-09-30：脚本编译/热重载链八函数外迁 App/EditorAppScriptReload.cpp。
#include "App/EditorApp.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

#include <nlohmann/json.hpp>

#include "Audio/BakedClip.h" // M6c 竖切批：LBA1 烤制/装载
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include "Assets/AssetDatabase.h"
#include "Assets/FontBake.h" // M7c 批①：字体离线烘焙（后台 worker 消费）
#include "Assets/ProjectFile.h" // M7c 批①：LoadProjectFile（fxFont 装载；批⑦ ResolveScene）
#include "Assets/ProjectWizard.h"
#include "Assets/SpriteRefs.h" // 批⑦：换场 afterBuild 的 play 世界 guid 归一
#include "Serialization/SceneArchive.h" // 批⑦：HookResolveScene 读档名
#include "Interaction/ViewportRenderer.h"
#include "Localization/Localization.h"
#include "Tooling/ThumbCache.h"
#include "Core/Log.h"
#include "EditorContext.h"
#include "Renderer/RHI.h"
#include "Ui/UiSubsystem.h" // 批③a（ADR-014）：游戏 UI 层（RmlUi）
#include "Scripting/ScriptHost.h"
#include "imgui.h"
#include "App/RecentProjects.h"

namespace lemon::editor {

namespace {
// 批⑦：SceneManager.LoadScene 的编辑器侧寻址（SceneSourceHooks 宿主实现）——
// project.lemon 每次调用现读（换场低频；与 fxFont 装载同款 ad hoc 口径），
// ResolveScene 同一引擎链（路径 > 唯一 stem > false 响亮）
EditorApp* s_sceneSrcApp = nullptr;

bool HookResolveScene(const char* nameOrPath, ecs::SceneSwitchRequest& out) {
    EditorApp* app = s_sceneSrcApp;
    if (!app || !nameOrPath || !*nameOrPath) return false;
    const std::string root = app->Ctx().Assets().ProjectRoot();
    const assets::ProjectFile pf = assets::LoadProjectFile(root);
    const std::string rel = assets::ResolveScene(root, pf, nameOrPath);
    if (rel.empty()) {
        LEMON_ERROR("SceneManager.LoadScene：场景不可解析（路径/唯一名未命中或不唯一）"
                    "'%s'",
                    nameOrPath);
        return false;
    }
    std::ifstream f(std::filesystem::path(root) / rel, std::ios::binary);
    if (!f) {
        LEMON_ERROR("SceneManager.LoadScene：场景文件不可读：%s", rel.c_str());
        return false;
    }
    std::string json((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (!ecs::SceneArchive::SceneDocName(json, out.name)) {
        LEMON_ERROR("SceneManager.LoadScene：场景档解析失败：%s", rel.c_str());
        return false;
    }
    if (out.name.empty()) out.name = std::filesystem::path(rel).stem().string();
    out.path = rel;
    out.jsonText = std::move(json);
    return true;
}
} // namespace

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
    // M7a 批⓪（ADR-016 M8/D6）：顺手解析可选字段 entryScene（入口场景声明，
    // 相对路径）——编辑器侧只回显 + 在场性守卫；入口解析的回退链（entryScene →
    // 唯一 .scene → 多场景缺字段红字）归批② ProjectFile / 批④ lemon-game。
    {
        std::ifstream pf(projectRoot + "/project.lemon", std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(pf)),
                         std::istreambuf_iterator<char>());
        bool plOk = false;
        entryScene_.clear();
        try {
            const nlohmann::json j = nlohmann::json::parse(text);
            plOk = j.contains("name") && j.at("name").is_string();
            if (j.contains("entryScene") && j.at("entryScene").is_string())
                entryScene_ = j.at("entryScene").get<std::string>();
        } catch (const std::exception&) {
        }
        if (!plOk)
            LEMON_ERROR("project.lemon 损坏或缺少 name 字段：%s——项目按目录继续打开，"
                        "建议重建工程文件", projectRoot.c_str());
        if (!entryScene_.empty()) {
            std::error_code fec;
            if (std::filesystem::is_regular_file(
                    std::filesystem::path(projectRoot) / entryScene_, fec)) {
                LEMON_LOG("入口场景声明（entryScene）：%s", entryScene_.c_str());
            } else {
                LEMON_WARN("entryScene 声明的场景不存在：%s/%s——运行时入口将走回退链",
                           projectRoot.c_str(), entryScene_.c_str());
            }
        }
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
    // #95：Reset 覆盖句柄 ≠ 释放——先显式释放旧代活体纹理（须在 Reset 前，
    // 字体页句柄只在 registry 里）
    viewport_->Assets().ReleaseGpu(*device_);
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
                                  launch_->smokeUirml || launch_->smokeAudio ||
                                  launch_->finalTest ||
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

// M6c 批②：World::SetAudioBackend 的 guid→clipId 解析壳已随 AudioMount 下沉引擎
//（M7a 批③；AudioMount::WireBackend 单点）。本文件只剩装载源适配器 + 薄壳。
namespace {
// AssetDatabase → AudioSource 适配器（SpriteRefSource 双实现同款纪律）
class DbAudioSource final : public audio::AudioSource {
public:
    explicit DbAudioSource(const AssetDatabase& db) : db_(db) {}
    std::string ProjectRoot() const override { return db_.ProjectRoot(); }
    void EachAudio(const std::function<void(const audio::AudioItem&)>& fn) const override {
        for (const AssetEntry& e : db_.Entries()) {
            if (e.type != AssetType::Audio || e.missing) continue;
            audio::AudioItem item;
            item.guid = e.guid;
            item.srcAbs = db_.AbsolutePath(e);
            item.loopStart = e.audioLoopStart;
            item.loopEnd = e.audioLoopEnd;
            item.preload = e.audioPreload;
            item.fx.retriggerCdSec = e.audioRetriggerCd;
            item.fx.voiceCap = e.audioVoiceCap;
            item.fx.pitchJitter = e.audioPitchJitter;
            fn(item);
        }
    }

private:
    const AssetDatabase& db_;
};
} // namespace

// Play World 音频后端装配（交互侧 TryEnterPlay 与程序化 --play 双挂点——竖切批
// MountPlayAudio 同款双点纪律；ctx = &audioMount_，成员地址稳定）
void EditorApp::WirePlayAudioBackend() {
    audioMount_.WireBackend(ctx_.ActiveWorld());
}

bool EditorApp::EnterPlayProgrammatic() {
    // #82（review 2026-10-02）：程序化进 Play 装配单源——原 TryEnterPlay 封装与
    // --play/--smoke-uirml/--bench-* 手动拼装并存且步骤面漂移（各漏不同步骤；
    // 封装侧新增任何前置，手动路径静默漏）。守卫由调用方决定（见头注）
    if (!ctx_.EnterPlay()) return false;
    paused_ = false;         // review 2026-10-02 #28：会话边界复位——sim 冻结态不跨
                             // Play 残留（音频侧 MountPlayAudio 复位后两侧不错位）
    MountSceneUiDocuments(); // 批③d 前置（通道 A）：场景声明装载 + EnterPlay 归位
    MountPlayAudio();        // M6c 竖切批：烤制/装载音频资产（guid→clip；无资产 no-op）
    WirePlayAudioBackend();  // M6c 批②：命令表提交引擎 + guid 解析
    LoadFxFontPage();        // M7c 批①：Fx 字体页兜底装载（worker 未烤完时同步补）
    // 批⑦ 前置①兑现：编辑器 Play 世界换场钩子装配（缺则编辑器内 LoadScene =
    // UI origin=Scene 文档不卸 + 新场脚本全哑——sweep/afterBuild 与 lemon-game
    // GameEntry.cpp 同序列）。捕获 this：EditorApp 与 playWorld 同生命周期，
    // ExitPlay 整弃 playWorld（Switcher 随之亡）→ 每次 EnterPlay 重装配
    s_sceneSrcApp = this;
    scripting::SetSceneSourceHooks({HookResolveScene});
    ctx_.ActiveWorld().Switcher().SetHooks({
        .sweep = [this] {
            if (gameUi_) gameUi_->UnloadDocumentsByOrigin(::lemon::ui::UiDocOrigin::Scene);
        },
        .afterBuild = [this](ecs::Scene& s) {
            const assets::SpriteRefStats st = assets::ResolveSpriteRefs(s, ctx_.Assets());
            if (st.danglingGuid)
                LEMON_WARN("编辑器换场：sprite 引用悬空 %u 处（渲染保留旧号）",
                           st.danglingGuid);
            ctx_.ResolvePlayScripts(); // 单 registry：playScene_ 即 s（全组扫描）
            if (gameUi_) {
                MountSceneUiDocuments(); // ActiveScene() = playScene_ = s
                ReconcileUiDocuments();
            }
        },
    });
    return true;
}

bool EditorApp::TryEnterPlay() {
    if (PlayBlockedByScripts()) {
        playBlockedOpen_ = true;
        LEMON_WARN("已阻止进入 Play：Game/ 编译失败（脚本宿主未装配）。"
                   "错误见 Console 红字；修复保存后自动重编译装配");
        return false;
    }
    return EnterPlayProgrammatic();
}

bool EditorApp::StopPlay() {
    audio_.StopAll(); // M6c 竖切批：声部清场（clip 注册表保留——重进 Play 全量重装）
    // review 2026-10-02 #27：试听记账随清场复位——Play 中双击起试听后 Stop 回
    // Edit，残留 previewVoice_ 会让下一次双击同资产命中「同曲再点 = 停」分支
    // 静默空操作（第一次点击无声，需点第二次才播）
    previewVoice_ = 0;
    previewGuid_ = 0;
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

// ---- M6c 竖切批（ADR-015）：进 Play 音频装载 ----
// 本体已下沉 Engine/Audio/AudioMount（M7a 批③ 搬家非复制——烤制/装载/流式分流/
// 会话起点归位全在引擎侧；BakedPath/BakeStale 亦公开供下方后台烤制线程复用）。
// 编辑器侧保留：试听声部清场（编辑器态）+ 源适配。
uint32_t EditorApp::MountPlayAudio() {
    audio_.StopAll();
    if (previewVoice_ != 0) { // 试听声部随 Play 重开终止（clip 表将重建）
        previewVoice_ = 0;
        previewGuid_ = 0;
    }
    const DbAudioSource src(ctx_.Assets());
    return audioMount_.MountAll(src);
}

// 按需装载单 clip（AssetEntry 入参形态的编辑器薄壳——试听/冒烟消费；引擎侧
// AudioMount::EnsureLoaded 逐行同源）
bool EditorApp::EnsureClipLoaded(const AssetEntry& e, bool* outStreamed) {
    audio::AudioItem item;
    item.guid = e.guid;
    item.srcAbs = ctx_.Assets().AbsolutePath(e);
    item.loopStart = e.audioLoopStart;
    item.loopEnd = e.audioLoopEnd;
    item.preload = e.audioPreload;
    item.fx.retriggerCdSec = e.audioRetriggerCd;
    item.fx.voiceCap = e.audioVoiceCap;
    item.fx.pitchJitter = e.audioPitchJitter;
    return audioMount_.EnsureLoaded(item, ctx_.Assets().ProjectRoot(), outStreamed);
}

void EditorApp::TogglePreviewAudio(uint64_t guid) {
    if (previewGuid_ == guid && previewVoice_ != 0) { // 同曲再点 = 停
        audio_.Stop(previewVoice_);
        previewVoice_ = 0;
        previewGuid_ = 0;
        return;
    }
    const AssetEntry* e = ctx_.Assets().FindByGuid(guid);
    if (!e || e->type != AssetType::Audio || e->missing) return;
    if (previewVoice_ != 0) audio_.Stop(previewVoice_); // 换曲顶停旧试听
    if (!EnsureClipLoaded(*e)) {
        LEMON_WARN("试听失败：装载/烤制未过（见上方红字）：%s", e->relPath.c_str());
        return;
    }
    previewVoice_ = AudioPlayByGuid(guid, 1, 0.8f, 0.0f, 0);
    previewGuid_ = previewVoice_ != 0 ? guid : 0;
}

// ---- 后台烤制（批①：Rescan 增量 / 开项目预热 → 工作线程；EnterPlay 只兜缺漏）----

void EditorApp::EnqueueAudioBake(const AssetEntry& e) {
    if (e.type != AssetType::Audio || e.missing) return;
    const std::string root = ctx_.Assets().ProjectRoot();
    if (root.empty()) return;
    // 批③修：新项目首次导入即后台烤（早于任何 EnterPlay）——烤制目录的建目录
    // 时序原归 MountPlayAudio（首进 Play）所有，worker 落盘前无目录 = "产物
    // 不可写"红字。入队侧幂等补建（主线程，无 worker fs 竞争）。
    std::error_code ec;
    std::filesystem::create_directories(
        std::filesystem::path(root) / ".lemon" / "baked" / "audio", ec);
    {
        std::lock_guard<std::mutex> lk(audioBakeMtx_);
        audioBakeQueue_.emplace_back(e.guid, ctx_.Assets().AbsolutePath(e),
                                     audio::AudioMount::BakedPath(root, e.guid),
                                     e.audioLoopStart, e.audioLoopEnd);
    }
    ++audioBakePending_;
    if (!audioBakeThread_.joinable()) // 惰性起（早退路径不落空线程）
        audioBakeThread_ = std::thread([this] {
            for (;;) {
                std::tuple<uint64_t, std::string, std::string, float, float> job;
                {
                    std::unique_lock<std::mutex> lk(audioBakeMtx_);
                    audioBakeCv_.wait(lk, [this] { return audioBakeStop_ || !audioBakeQueue_.empty(); });
                    if (audioBakeQueue_.empty()) break; // stop 且队列空
                    job = std::move(audioBakeQueue_.front());
                    audioBakeQueue_.pop_front();
                }
                const auto& [guid, src, dst, loopS, loopE] = job;
                // 工作线程未捕获异常 = std::terminate 崩整个编辑器（review
                // 2026-10-02 #21）——烤制失败按件隔离，红字后继续吃队列
                try {
                    if (audio::AudioMount::BakeStale(src, dst)) // 入队到执行间可能已被兜底烤过
                        audio::BakeAudioFile(src.c_str(), dst.c_str(), loopS, loopE);
                } catch (const std::exception& ex) {
                    LEMON_ERROR("audio: 后台烤制异常中止（%s）：%s", ex.what(),
                                src.c_str());
                } catch (...) {
                    LEMON_ERROR("audio: 后台烤制未知异常：%s", src.c_str());
                }
                --audioBakePending_;
            }
        });
    audioBakeCv_.notify_one();
}

void EditorApp::WarmAudioBakes() {
    for (const AssetEntry& e : ctx_.Assets().Entries())
        if (e.type == AssetType::Audio && !e.missing) EnqueueAudioBake(e);
}

void EditorApp::StopAudioBaker() {
    {
        std::lock_guard<std::mutex> lk(audioBakeMtx_);
        audioBakeStop_ = true;
    }
    audioBakeCv_.notify_all();
    if (audioBakeThread_.joinable()) audioBakeThread_.join();
}

uint32_t EditorApp::AudioClipOfGuid(uint64_t guid) const {
    return audioMount_.ClipIdOfGuid(guid);
}

// ---- 字体后台烤制（M7c 批①：音频 worker 同款三件套；BakeStale 头对比抓字符集
// 热改——入队到执行间可能已被装载侧兜底烤过，执行前复查省一遍 FreeType）----

void EditorApp::EnqueueFontBake(const AssetEntry& e) {
    if (e.type != AssetType::Font || e.missing) return;
    const std::string root = ctx_.Assets().ProjectRoot();
    if (root.empty()) return;
    std::error_code ec;
    std::filesystem::create_directories(
        std::filesystem::path(root) / ".lemon" / "baked" / "fonts", ec);
    {
        std::lock_guard<std::mutex> lk(fontBakeMtx_);
        fontBakeQueue_.push_back(
            {e.guid, ctx_.Assets().AbsolutePath(e), assets::FontBakedPath(root, e.guid),
             e.FontBake()});
    }
    ++fontBakePending_;
    if (!fontBakeThread_.joinable()) // 惰性起（音频 worker 同款）
        fontBakeThread_ = std::thread([this] {
            for (;;) {
                FontBakeJob job;
                {
                    std::unique_lock<std::mutex> lk(fontBakeMtx_);
                    fontBakeCv_.wait(lk, [this] { return fontBakeStop_ || !fontBakeQueue_.empty(); });
                    if (fontBakeQueue_.empty()) break;
                    job = std::move(fontBakeQueue_.front());
                    fontBakeQueue_.pop_front();
                }
                // 异常隔离（音频 worker #21 同款）：烤制失败红字继续吃队列
                try {
                    if (assets::BakeFontStale(job.src.c_str(), job.dst.c_str(),
                                             job.params.Hash()))
                        if (!assets::BakeFontFile(job.src.c_str(), job.dst.c_str(),
                                                  job.params))
                            LEMON_ERROR("font: 后台烤制失败（见上方红字）：%s",
                                        job.src.c_str());
                } catch (const std::exception& ex) {
                    LEMON_ERROR("font: 后台烤制异常中止（%s）：%s", ex.what(),
                                job.src.c_str());
                } catch (...) {
                    LEMON_ERROR("font: 后台烤制未知异常：%s", job.src.c_str());
                }
                --fontBakePending_;
            }
        });
    fontBakeCv_.notify_one();
}

void EditorApp::WarmFontBakes() {
    for (const AssetEntry& e : ctx_.Assets().Entries())
        if (e.type == AssetType::Font && !e.missing) EnqueueFontBake(e);
}

void EditorApp::StopFontBaker() {
    {
        std::lock_guard<std::mutex> lk(fontBakeMtx_);
        fontBakeStop_ = true;
    }
    fontBakeCv_.notify_all();
    if (fontBakeThread_.joinable()) fontBakeThread_.join();
}

void EditorApp::LoadFxFontPage() {
    if (!viewport_ || !device_) return;
    const std::string root = ctx_.Assets().ProjectRoot();
    if (root.empty()) return;
    const assets::ProjectFile pf = assets::LoadProjectFile(root);
    if (!pf.ok || pf.fxFont == 0) return; // 未配置 = 内置页（零噪声）
    const AssetEntry* e = ctx_.Assets().FindByGuid(pf.fxFont);
    if (!e || e->type != AssetType::Font || e->missing) {
        LEMON_WARN("fxFont 指向的字体资产不存在（guid %016llx）——飘字用内置 5×7 页",
                   (unsigned long long)pf.fxFont);
        return;
    }
    const std::string src = ctx_.Assets().AbsolutePath(*e);
    const std::string dst = assets::FontBakedPath(root, pf.fxFont);
    const assets::FontBakeParams params = e->FontBake();
    if (assets::BakeFontStale(src.c_str(), dst.c_str(), params.Hash())) {
        // worker 在烤（WarmFontBakes 刚入队/Rescan 增量）→ 同步兜底让路：双烤
        // 同一 dst 的 rename 竞争会假红字（首次实跑抓的）；烤成后由下次触发点
        // （Rescan/EnterPlay）装载
        if (fontBakePending_.load() > 0) return;
        if (!assets::BakeFontFile(src.c_str(), dst.c_str(), params))
            return; // 红字已打；内置页降级（后台 worker 烤好后下次触发点重试）
    }
    auto& font = viewport_->Assets().Font();
    font.LoadBaked(*device_, viewport_->Assets().Registry(),
                   renderer::BitmapFont::kDefaultBakedSlot, dst.c_str());
}

uint32_t EditorApp::AudioPlayByGuid(uint64_t guid, int32_t group, float volume, float pan,
                                    int32_t loop) {
    const uint32_t clipId = AudioClipOfGuid(guid);
    if (clipId == 0) return 0; // 未装载（无项目/烤制失败/非音频 guid）
    if (group < 0 || group >= audio::kGroupCount) group = 1; // 越界落 Sfx（防御钳）
    return audio_.Play(clipId, {.volume = volume,
                                .pan = pan,
                                .group = audio::Group(group),
                                .loop = loop != 0});
}

void EditorApp::MenuNewProject() { wizOpen_ = true; }

void EditorApp::DrawRecoveryModal() {
    if (recoveryPath_.empty()) return;
    const char* id = loc::tr("recovery.title");
    if (!ImGui::IsPopupOpen(id) && !recoveryAnswered_) ImGui::OpenPopup(id);
    if (!ImGui::BeginPopupModal(id, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::Text("%s", loc::trFmt("recovery.body_fmt", {recoveryPath_}).c_str());
    ImGui::TextUnformatted(loc::tr("recovery.hint"));
    ImGui::Separator();
    if (ImGui::Button(loc::tr("recovery.restore"), ImVec2(120, 0))) {
        if (ctx_.OpenSceneRecovery(recoveryPath_))
            LEMON_LOG("崩溃恢复：已载入备份（Ctrl+S 落盘）");
        else
            LEMON_WARN("崩溃恢复失败：备份解析失败");
        recoveryPath_.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button(loc::tr("recovery.ignore"), ImVec2(120, 0))) { // 只关本会话弹窗，文件保留（热修④）
        recoveryPath_.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button(loc::tr("recovery.ignore_delete"), ImVec2(120, 0))) { // 丢弃备份——否则 untitled 永弹
        if (!ctx_.DiscardAutosave(recoveryPath_))
            LEMON_WARN("丢弃自动备份失败（文件已在/权限？）——下次启动可能仍会提示");
        recoveryPath_.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

} // namespace lemon::editor
