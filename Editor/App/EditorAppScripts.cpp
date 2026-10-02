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
#include "Assets/ProjectWizard.h"
#include "Interaction/ViewportRenderer.h"
#include "Tooling/ThumbCache.h"
#include "Core/Log.h"
#include "EditorContext.h"
#include "Renderer/RHI.h"
#include "Ui/UiSubsystem.h" // 批③a（ADR-014）：游戏 UI 层（RmlUi）
#include "Scripting/ScriptHost.h"
#include "imgui.h"
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

// M6c 批②：World::SetAudioBackend 的 guid→clipId 解析壳（ctx = EditorApp*）
namespace {
uint32_t ResolveAudioClipThunk(uint64_t guid, void* ctx) {
    return static_cast<const EditorApp*>(ctx)->AudioClipOfGuid(guid);
}
} // namespace

// Play World 音频后端装配（交互侧 TryEnterPlay 与程序化 --play 双挂点——竖切批
// MountPlayAudio 同款双点纪律；ctx = this，成员地址稳定）
void EditorApp::WirePlayAudioBackend() {
    ctx_.ActiveWorld().SetAudioBackend(&audio_, ResolveAudioClipThunk, this);
}

bool EditorApp::TryEnterPlay() {
    if (PlayBlockedByScripts()) {
        playBlockedOpen_ = true;
        LEMON_WARN("已阻止进入 Play：Game/ 编译失败（脚本宿主未装配）。"
                   "错误见 Console 红字；修复保存后自动重编译装配");
        return false;
    }
    if (!ctx_.EnterPlay()) return false;
    paused_ = false; // review 2026-10-02 #28：会话边界复位——sim 冻结态不跨 Play
                     // 残留（音频侧 MountPlayAudio 复位后两侧不再错位成
                     //「画面冻结、BGM 照响」；工具栏暂停钮态随新会话归零）
    MountSceneUiDocuments(); // 批③d 前置（通道 A）：场景声明装载 + EnterPlay 归位
    MountPlayAudio();        // M6c 竖切批：烤制/装载音频资产（guid→clip）
    WirePlayAudioBackend();  // M6c 批②：命令表提交引擎 + guid 解析（AudioSystem #20 消费）
    return true;
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
// 扫全部 Audio 资产：缺烤/源新于产物 → 现烤（.lemon/baked/audio/<guidHex>.baked，
// LBA1 = 48k PCM16）→ 装载注册 → guid→clipId。失败红字跳过（无声不炸 Play）；
// 重进 Play 全量重装（ResetClips 防注册表跨局累积——id 只增不减）。
namespace {
namespace fs = std::filesystem;
// 烤制产物路径（.lemon/baked/audio/<guidHex>.baked；bakeDir 由调用方保证存在）
std::string BakedPathFor(const std::string& root, uint64_t guid) {
    char hex[17];
    std::snprintf(hex, sizeof(hex), "%016llx", (unsigned long long)guid);
    return root + "/.lemon/baked/audio/" + hex + ".baked";
}
// 缺烤/源新于产物（mtime；后台线程与 EnterPlay 兜底共用同一判定）。
// review 2026-10-02 #8：.meta（importer 段：loop/preload）新于产物同样算 stale
//——loop 冻结在 .baked 头里，不重烤则热改永不生效于已烤 clip
bool BakeStale(const std::string& src, const std::string& dst) {
    std::error_code ec;
    if (!fs::exists(dst, ec)) return true;
    const auto dstT = fs::last_write_time(dst, ec);
    if (ec) return true;
    const auto srcT = fs::last_write_time(src, ec);
    if (!ec && srcT > dstT) return true;
    const auto metaT = fs::last_write_time(src + ".meta", ec);
    return !ec && metaT > dstT;
}
} // namespace

uint32_t EditorApp::MountPlayAudio() {
    audio_.StopAll();
    if (previewVoice_ != 0) { // 试听声部随 Play 重开终止（clip 表将重建）
        previewVoice_ = 0;
        previewGuid_ = 0;
    }
    audio_.ResetClips();
    // 会话起点归位（2026-10-01 真人验收发现）：引擎 pausedAll 跨会话残留——上局
    // 游戏暂停中 StopPlay 再 Play，新 BGM 起播即挂起变哑。新 World 的 AudioChannel
    // 意图恒 false（游戏要起始暂停会显式再 SetPaused），此处对齐引擎侧。
    audio_.SetPaused(false);
    audioClips_.clear();
    const std::string root = ctx_.Assets().ProjectRoot();
    if (root.empty()) return 0; // 无项目 = 零资产零装载
    std::error_code ec;
    fs::create_directories(fs::path(root) / ".lemon" / "baked" / "audio", ec);
    uint32_t ok = 0, failed = 0, streamed = 0;
    for (const AssetEntry& e : ctx_.Assets().Entries()) {
        if (e.type != AssetType::Audio || e.missing) continue;
        bool isStream = false;
        if (EnsureClipLoaded(e, &isStream)) {
            ++ok;
            if (isStream) ++streamed; // 批①b：>1MiB 未 preload = 流式（RAM 常驻证据）
        } else {
            ++failed;
        }
    }
    if (ok)
        LEMON_LOG("进 Play 音频装载：%u 成功（流式 %u）%s", ok, streamed,
                  failed ? "" : "，全部就绪");
    else if (failed)
        LEMON_WARN("进 Play 音频装载：0 成功 / %u 失败（详见上方红字）", failed);
    return ok;
}

// 按需装载单 clip：缺烤/陈旧现烤（同步；通常已被后台烤制预热）→ 装载注册 →
// guid→clipId。Edit 态试听与 EnterPlay 兜底共用此口。批①b：payload > 1MiB 且
// 未显式 preload → 流式注册（RAM 常驻 < 阈值；句柄/环随声部开闭）。
bool EditorApp::EnsureClipLoaded(const AssetEntry& e, bool* outStreamed) {
    if (outStreamed) *outStreamed = false;
    if (const auto it = audioClips_.find(e.guid); it != audioClips_.end()) return true;
    const std::string root = ctx_.Assets().ProjectRoot();
    const std::string src = ctx_.Assets().AbsolutePath(e);
    const std::string dst = BakedPathFor(root, e.guid);
    if (BakeStale(src, dst)) {
        const auto t0 = std::chrono::steady_clock::now();
        if (!audio::BakeAudioFile(src.c_str(), dst.c_str(), e.audioLoopStart, e.audioLoopEnd))
            return false;
        // 批①b：同步烤超 100ms 红字（后台预热未命中——首播顿挫面，量级证据）
        const double ms = std::chrono::duration<double, std::milli>(
                              std::chrono::steady_clock::now() - t0).count();
        if (ms > 100.0)
            LEMON_WARN("audio: 同步烤制耗时 %.0fms（后台预热未命中）：%s", ms, src.c_str());
    }
    audio::BakedClipInfo info;
    if (!audio::PeekBakedClip(dst.c_str(), info)) return false;
    if (info.payloadBytes > audio::kStreamThresholdBytes && !e.audioPreload) {
        const uint32_t streamId = audio_.RegisterStreamClip(dst.c_str());
        if (streamId == 0) return false;
        audioClips_[e.guid] = streamId;
        if (outStreamed) *outStreamed = true;
        return true;
    }
    std::vector<int16_t> pcm;
    if (!audio::LoadBakedClip(dst.c_str(), pcm, info)) return false;
    const uint32_t clipId = audio_.RegisterClip(std::move(pcm), info.channels,
                                                info.frameCount, info.loopStart, info.loopEnd);
    if (clipId == 0) return false;
    audioClips_[e.guid] = clipId;
    return true;
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
                                     BakedPathFor(root, e.guid), e.audioLoopStart,
                                     e.audioLoopEnd);
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
                if (BakeStale(src, dst)) // 入队到执行间可能已被兜底烤过
                    audio::BakeAudioFile(src.c_str(), dst.c_str(), loopS, loopE);
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
    const auto it = audioClips_.find(guid);
    return it != audioClips_.end() ? it->second : 0;
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
