// Lemon 引擎 — 独立游戏入口 lemon-game（M7a 批④；ADR-016 M2 / ADR-005 同源双入口兑现）
// 编辑器与游戏共用同一套引擎装配件（批②③④ 下沉：AssetIndex/TextureStore/
// PrefabCache/PlayCaches/SaveStore/UiMount/AudioMount/CameraFollow/SceneExtractor/
// RenderExtractSystem/GameFx），本文件只做"宿主"：CLI 解析、装配序列、主循环、
// 退出收尾。不链 editor-core/ImGui（CMake 层不可违）。
//
// 装配序列（ADR-016 M2，对标 anim-smoke 最小循环 + 编辑器 EnterPlay 装配清单）：
//   Window → Device/Swapchain（pipeline cache 显式传 <root>/.lemon/game/，cwd 相对
//   默认不再触发；**设备须先于 ScriptHost**——CoreCLR 重写 DYLD 回退表后验证层
//   dlopen 会失败，--validate 实抓的顺序纪律）→ 程序化页（白精灵 + 位图字体）→
//   AssetIndex → TextureStore → AudioEngine → UiSubsystem（字体链：<root>/Fonts/
//   → 引擎源树回退）→ ScriptHost（entry dll = exe 旁 runtime/ → LEMON_SCRIPT_DIR
//   构建树回退；用户程序集 = <root>/.lemon/bin/Game.dll——构建归编辑器/packager，
//   lemon-game 只消费）→ hooks 三族 + UI 贴图/文档解析器 → World（默认 20 系统 +
//   RenderExtractSystem）→ SceneArchive::Load(entryScene) + GUID 归一 → 四缓存 →
//   存档三通道 → 脚本解析 → UiMount/AudioMount → 主循环。
//
// 主循环（编辑器 Play 路径同款纪律）：InputState（WASD/箭头/空格/R/Esc|P；UI
// WantsKeyboard/AnyModalShown 让出）→ audio tick + 监听器 → 固定步累加器（1/60，
// 追帧上限 5 步；--frames/--smoke 自动化 = 每渲染帧恰一步 + alpha=1）→ 相机跟随
// → UI 喂入/Update → 直渲染（D8 A 案：sprite pass + RmlUi pass 同一动态渲染块内
// 直画 swapchain，spike-04 先例）→ present。
//
// CLI：lemon-game [--project <dir>] [--scene <rel>] [--frames N] [--smoke] [--validate]
//   --project  项目根（含 project.lemon）；缺省 = exe 旁 data/（包形态零参启动）
//   --scene    相对路径覆盖入口场景（缺省 = entryScene → 回退链）
//   --frames   跑 N 帧退出（自动化）
//   --smoke    机器判据：终帧 RESULT 行（回归口径，与编辑器各 smoke 链同款）
//   --validate Vulkan 验证层（开发自测）
#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

#include "Assets/AssetIndex.h"
#include "Assets/PlayCaches.h"
#include "Assets/PrefabCache.h"
#include "Assets/ProjectFile.h"
#include "Assets/SaveStore.h"
#include "Assets/SpriteRefs.h"
#include "Assets/TextureStore.h"
#include "Assets/AtlasStore.h"
#include "Audio/AudioEngine.h"
#include "Audio/AudioMount.h"
#include "Audio/Spatial2D.h"
#include "Components/CoreComponents.h" // Meta（smoke-scene DDOL 定位）
#include "Core/Log.h"
#include "Core/Math.h"
#include "ECS/Scene.h"
#include "ECS/SceneMembership.h" // MarkDontDestroyOnLoadTree/CountSceneGroup（smoke-scene）
#include "ECS/StateHash.h"       // ComputeStateHash（批⑧ async 跳：staging 零可见性断言）
#include "ECS/World.h"
#include "Platform/Window.h"
#include "Renderer/Atlas.h"
#include "Renderer/BitmapFont.h"
#include "Renderer/Camera2D.h"
#include "Renderer/CameraFollow.h"
#include "Renderer/GameFx.h"
#include "Renderer/Renderable.h"
#include "Renderer/RHI.h"
#include "Renderer/SceneExtractor.h"
#include "Renderer/SpriteBatcher.h"
#include "Scripting/ScriptBox.h"
#include "Scripting/ScriptHost.h"
#include "Serialization/SceneArchive.h"
#include "Ui/UiMount.h"
#include "Ui/UiSubsystem.h"

#if defined(__APPLE__)
#include <mach-o/dyld.h> // _NSGetExecutablePath
#elif defined(_WIN32)
#include <windows.h> // GetModuleFileNameA（ExeDir 批⑦：win 分支首次真编）
#endif

namespace fs = std::filesystem;
using namespace lemon;
using namespace lemon::ecs;
using lemon::renderer::SpritePacket;

namespace {

constexpr float kFixedDt = 1.0f / 60.0f;
constexpr float kMaxAcc = kFixedDt * 5.0f; // 追帧上限（死亡螺旋钳；编辑器 Play 同款）
constexpr int kMaxSteps = 5;
constexpr uint32_t kWhiteCellPx = 64;
// review 2026-10-04 #1：uiKeyWasDown[64] 按 (int)UiKey 裸索引——枚举无 Count 哨兵，
// 扩键越过 64 时是静默越界写。编译期锁当前域（Tab=48），扩键即红。
static_assert((int)ui::UiKey::Tab < 64, "uiKeyWasDown 表与新 UiKey 域同步扩容");

// ---- 可执行文件目录（exe 旁 runtime/ 与缺省 data/ 定位；mac/Win 双实现）----
std::string ExeDir() {
    char buf[4096] = {};
#if defined(__APPLE__)
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) != 0) return ".";
    if (char real[4096]; realpath(buf, real)) return fs::path(real).parent_path().string();
    return fs::path(buf).parent_path().string();
#elif defined(_WIN32)
    DWORD n = GetModuleFileNameA(nullptr, buf, (DWORD)sizeof(buf));
    if (n == 0 || n >= sizeof(buf)) return ".";
    return fs::path(std::string(buf, (size_t)n)).parent_path().string();
#else
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return ".";
    return fs::path(std::string(buf, (size_t)n)).parent_path().string();
#endif
}

bool ReadFileText(const fs::path& p, std::string& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    out.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return true;
}

// ---- 输入采集（SDL 事件观察者：键盘/鼠标/文本输入单源表）----
// UiKey 边沿（差分）与 InputState（当帧快照）都吃这张表——lemon-game 无 ImGui
// 中间层，窗口即画布。指针坐标 = 窗口点（Retina 下与 swapchain 像素差 scale，
// 主循环换算）。鼠标按键边沿归主循环差分（观察者只记电平）。
struct InputCollector {
    bool keyDown[SDL_SCANCODE_COUNT] = {};
    bool uiKeyWasDown[64] = {}; // (int)ui::UiKey 索引（枚举值 < 64）
    bool leftDown = false;
    float mx = 0, my = 0; // 窗口点坐标
    bool inside = false;
    std::string textInput; // 本帧 SDL_TEXT_INPUT 累积（UTF-8 片段）
    struct {
        std::string text;
        int start = 0, len = 0;
        bool has = false;
    } editing;

    // SDL_Scancode → UiKey（波1 集合：字母/数字/方向/编辑键；编辑器 FeedGameUiInput
    // 的键位映射同源）
    static int UiKeyOf(SDL_Scancode sc) {
        using UK = ui::UiKey;
        if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z)
            return (int)UK::A + (int)(sc - SDL_SCANCODE_A);
        if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9)
            return (int)UK::Num1 + (int)(sc - SDL_SCANCODE_1);
        if (sc == SDL_SCANCODE_0) return (int)UK::Num0;
        switch (sc) {
            case SDL_SCANCODE_UP: return (int)UK::Up;
            case SDL_SCANCODE_DOWN: return (int)UK::Down;
            case SDL_SCANCODE_LEFT: return (int)UK::Left;
            case SDL_SCANCODE_RIGHT: return (int)UK::Right;
            case SDL_SCANCODE_BACKSPACE: return (int)UK::Backspace;
            case SDL_SCANCODE_RETURN:
            case SDL_SCANCODE_KP_ENTER: return (int)UK::Return;
            case SDL_SCANCODE_ESCAPE: return (int)UK::Escape;
            case SDL_SCANCODE_SPACE: return (int)UK::Space;
            case SDL_SCANCODE_HOME: return (int)UK::Home;
            case SDL_SCANCODE_END: return (int)UK::End;
            case SDL_SCANCODE_DELETE: return (int)UK::Delete;
            case SDL_SCANCODE_TAB: return (int)UK::Tab;
            default: return -1;
        }
    }

    static void OnEvent(const void* raw, void* ud) {
        auto* self = static_cast<InputCollector*>(ud);
        const SDL_Event* e = static_cast<const SDL_Event*>(raw);
        switch (e->type) {
            case SDL_EVENT_MOUSE_MOTION:
                self->mx = e->motion.x;
                self->my = e->motion.y;
                self->inside = true;
                break;
            case SDL_EVENT_WINDOW_MOUSE_ENTER: self->inside = true; break;
            case SDL_EVENT_WINDOW_MOUSE_LEAVE: self->inside = false; break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                self->mx = e->button.x;
                self->my = e->button.y;
                if (e->button.button == SDL_BUTTON_LEFT) self->leftDown = true;
                break;
            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (e->button.button == SDL_BUTTON_LEFT) self->leftDown = false;
                break;
            case SDL_EVENT_KEY_DOWN:
                if (!e->key.repeat) self->keyDown[e->key.scancode] = true;
                break;
            case SDL_EVENT_KEY_UP: self->keyDown[e->key.scancode] = false; break;
            case SDL_EVENT_TEXT_INPUT: self->textInput += e->text.text; break;
            case SDL_EVENT_TEXT_EDITING:
                self->editing.text = e->edit.text;
                self->editing.start = e->edit.start;
                self->editing.len = e->edit.length;
                self->editing.has = true;
                break;
            default: break;
        }
    }
};

// ---- C 函数指针钩子的宿主静态位（ScriptHost.h:132-146——hooks 不装 = C# 侧静默
// 返回 0；编辑器同款"静态指针镜像生命周期"纪律）----
struct GameHost {
    assets::AssetIndex* index = nullptr;
    scripting::ScriptHost* scripts = nullptr;
    ecs::World* world = nullptr;
    ui::UiSubsystem* ui = nullptr;
    std::string projectRoot;
    assets::ProjectFile pf; // 批⑦：HookResolveScene 的 entryScene 声明链
};
GameHost* g_host = nullptr;

uint32_t HookSpriteOf(const char* hex) {
    if (!g_host || !hex) return 0;
    const assets::IndexedEntry* e = g_host->index->FindByGuid(assets::HexToGuid(hex));
    return e ? e->spriteId : 0;
}

uint64_t HookInstantiate(const char* hex, float x, float y) {
    // 编辑器 InstantiatePrefabAsset 的 Play 态等价：guid → json 快照 →
    // InstantiateJson + ResolveTreeScripts（批③d-2 运行时补挂路径）
    if (!g_host || !hex || !g_host->world || !g_host->world->ActiveScene()) return 0;
    const uint64_t guid = assets::HexToGuid(hex);
    const assets::IndexedEntry* entry = g_host->index->FindByGuid(guid);
    if (!entry || entry->type != assets::AssetType::Prefab) {
        LEMON_WARN("Prefab 实例化失败：资产不存在（guid %016llx）",
                   (unsigned long long)guid);
        return 0;
    }
    std::string json;
    if (!ReadFileText(g_host->index->AbsolutePath(*entry), json)) return 0;
    ecs::Scene& s = *g_host->world->ActiveScene();
    const uint32_t aliveBefore = s.AliveCount();
    ecs::Entity root = assets::PrefabCache::InstantiateJson(s, json, guid, Vec2{x, y});
    if (root.IsNull()) return 0;
    if (g_host->scripts)
        assets::PrefabCache::ResolveTreeScripts(*g_host->world, s, root, *g_host->scripts);
    // 批⑦ 前置②兑现：子树打标 active（Instantiate 落点 = active 场景，ADR-017 D1；
    // World::SpawnPrefab 的 StampTreeMembership 同款责任边界）
    ecs::StampTreeMembership(s, root, g_host->world->ActiveSceneHandle());
    LEMON_LOG("Prefab 实例化：%s（%u 实体）", entry->relPath.c_str(),
              s.AliveCount() - aliveBefore);
    return root.id;
}

void HookSaveFlush(ecs::World& w) {
    if (!g_host) return;
    for (uint8_t ch = 0; ch < kSaveChannelCount; ++ch)
        assets::SaveStore::Write(g_host->projectRoot, ch, w.Saves(ch));
}

void HookUiApplyOps(const ui::UiOpC* ops, uint32_t n, const char* arena, uint32_t bytes) {
    if (g_host && g_host->ui) g_host->ui->ApplyOps(ops, n, arena, bytes);
}
uint32_t HookUiDrainEvents(ui::UiEventC* dst, uint32_t cap) {
    return g_host && g_host->ui ? g_host->ui->DrainEvents(dst, cap) : 0;
}

bool HookResolveScene(const char* nameOrPath, ecs::SceneSwitchRequest& out) {
    // 批⑦ D4 寻址宿主侧：路径 > 唯一 stem > 响亮失败（ADR-017 D4；引擎层不碰 IO，
    // SceneSourceHooks 注入纪律）。jsonText 交原文——迁移链在 ValidateParse 内。
    if (!g_host || !nameOrPath) return false;
    const std::string rel =
        assets::ResolveScene(g_host->projectRoot, g_host->pf, nameOrPath);
    if (rel.empty()) {
        LEMON_ERROR("SceneManager.LoadScene：场景不可解析（路径/唯一名未命中或不唯一）"
                    "'%s'",
                    nameOrPath);
        return false;
    }
    std::string json;
    if (!ReadFileText(std::filesystem::path(g_host->projectRoot) / rel, json)) {
        LEMON_ERROR("SceneManager.LoadScene：场景文件不可读：%s", rel.c_str());
        return false;
    }
    if (!ecs::SceneArchive::SceneDocName(json, out.name)) {
        LEMON_ERROR("SceneManager.LoadScene：场景档解析失败：%s", rel.c_str());
        return false;
    }
    if (out.name.empty()) out.name = std::filesystem::path(rel).stem().string();
    out.path = rel;
    out.jsonText = std::move(json);
    return true;
}

// ---- AssetIndex 侧源适配器族（SpriteRefSource 同款纪律：编辑器 AssetDatabase
// 双实现的运行时半边；批③移交清单兑现）----

class IdxPrefabSource final : public assets::PrefabSource {
public:
    explicit IdxPrefabSource(const assets::AssetIndex& idx) : idx_(idx) {}
    void EachPrefab(const std::function<bool(uint64_t guid, const std::string& absPath)>& fn)
        const override {
        for (const assets::IndexedEntry& e : idx_.Entries()) {
            if (e.type != assets::AssetType::Prefab) continue;
            if (!fn(e.guid, idx_.AbsolutePath(e))) return;
        }
    }

private:
    const assets::AssetIndex& idx_;
};

class IdxPlayCacheSource final : public assets::PlayCacheSource {
public:
    explicit IdxPlayCacheSource(const assets::AssetIndex& idx) : idx_(idx) {}
    void Each(assets::AssetType type,
              const std::function<void(uint64_t guid, const std::string& relPath,
                                       const std::string& absPath)>& fn) const override {
        for (const assets::IndexedEntry& e : idx_.Entries()) {
            if (e.type != type) continue; // 条目在场 = 文件健康（无 missing 语义）
            fn(e.guid, e.relPath, idx_.AbsolutePath(e));
        }
    }
    const assets::IndexedEntry* FindSprite(uint64_t guid) const override {
        const assets::IndexedEntry* e = idx_.FindByGuid(guid);
        return e && e->type == assets::AssetType::Sprite ? e : nullptr;
    }
    bool HasClip(uint64_t guid) const override {
        const assets::IndexedEntry* e = idx_.FindByGuid(guid);
        return e && e->type == assets::AssetType::Clip;
    }

private:
    const assets::AssetIndex& idx_;
};

class IdxUiDocSource final : public ui::UiDocSource {
public:
    explicit IdxUiDocSource(const assets::AssetIndex& idx) : idx_(idx) {}
    bool ResolveRml(uint64_t guid, std::string& relPath, std::string& absPath) const override {
        const assets::IndexedEntry* e = idx_.FindByGuid(guid);
        if (!e || e->type != assets::AssetType::Rml) return false;
        relPath = e->relPath;
        absPath = idx_.AbsolutePath(*e);
        return true;
    }
    bool IsHealthyRml(const std::string& relPath) const override {
        const assets::IndexedEntry* e = idx_.FindByPath(relPath);
        return e && e->type == assets::AssetType::Rml;
    }

private:
    const assets::AssetIndex& idx_;
};

class IdxAudioSource final : public audio::AudioSource {
public:
    explicit IdxAudioSource(const assets::AssetIndex& idx) : idx_(idx) {}
    std::string ProjectRoot() const override { return idx_.ProjectRoot(); }
    void EachAudio(const std::function<void(const audio::AudioItem&)>& fn) const override {
        for (const assets::IndexedEntry& e : idx_.Entries()) {
            if (e.type != assets::AssetType::Audio) continue;
            audio::AudioItem item;
            item.guid = e.guid;
            item.srcAbs = idx_.AbsolutePath(e);
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
    const assets::AssetIndex& idx_;
};

class IdxSpriteRefSource final : public assets::SpriteRefSource {
public:
    explicit IdxSpriteRefSource(const assets::AssetIndex& idx) : idx_(idx) {}
    const assets::SpriteEntryView* SpriteByGuid(uint64_t guid) const override {
        const assets::IndexedEntry* e = idx_.FindByGuid(guid);
        if (!e || e->type != assets::AssetType::Sprite) return nullptr;
        return Fill(*e);
    }
    const assets::SpriteEntryView* SpriteByWholeId(uint32_t spriteId) const override {
        const assets::IndexedEntry* e = idx_.FindByWholeSpriteId(spriteId);
        return e ? Fill(*e) : nullptr;
    }
    uint32_t SpriteIdBase() const override { return idx_.SpriteIdBase(); }

private:
    const assets::SpriteEntryView* Fill(const assets::IndexedEntry& e) const {
        view_ = {};
        view_.guid = e.guid;
        view_.spriteId = e.spriteId;
        view_.sliceBase = e.sliceBase;
        view_.sliceCount = e.sliceCount;
        return &view_;
    }
    const assets::AssetIndex& idx_;
    mutable assets::SpriteEntryView view_; // 单槽 scratch（编辑器侧同口径）
};

// ---- --smoke-scene 夹具（M7c 批⑥b 换场编排单跳）------------------------
// 第二场景 = 内嵌最小档（3 实体；cards.rml 声明 showOnStart=1——批⑨ 起模板流程
// 文档 code-mount（菜单态 cards 未挂），每跳 sweep 卸旧 origin=Scene + afterBuild
// 装新声明显式 Show，断言区分度）。guid 取固定段避开模板已用值；DDOL 目标 =
// MainMenu.scene 的 GameFlow 实体（tag Flow，guid 钉死 = kFlowEntityGuid 镜像）。
constexpr const char* kSceneSmokeSceneB = R"({"schemaVersion":2,"name":"SceneB","entities":[
  {"components":{"Meta":{"guid":4700000000000000001,"layer":0,"prefabId":0,"tag":"","team":0},"Transform2D":{"pos":[64.0,64.0],"rot":0.0,"scale":[1.0,1.0]}}},
  {"components":{"Meta":{"guid":4700000000000000002,"layer":0,"prefabId":0,"tag":"","team":0},"Transform2D":{"pos":[128.0,128.0],"rot":0.0,"scale":[1.0,1.0]}}},
  {"components":{"Meta":{"guid":4700000000000000003,"layer":0,"prefabId":0,"tag":"UI_HUD","team":0},"Transform2D":{"pos":[0.0,0.0],"rot":0.0,"scale":[1.0,1.0]},"UIDocument":{"modal":0,"reserved":0,"showOnStart":1,"sourceAssetGuid":9103797948311928834}}}
]})";
// 四跳第 2/4 站（批⑦）：同构异 guid（实体 guid 段 4710/4720 避撞；UIDocument
// 同指 cards.rml——每跳 sweep 卸旧 + afterBuild 声明装载 + 显式 Show，断言稳定）
constexpr const char* kSceneSmokeSceneC = R"({"schemaVersion":2,"name":"SceneC","entities":[
  {"components":{"Meta":{"guid":4710000000000000001,"layer":0,"prefabId":0,"tag":"","team":0},"Transform2D":{"pos":[64.0,64.0],"rot":0.0,"scale":[1.0,1.0]}}},
  {"components":{"Meta":{"guid":4710000000000000002,"layer":0,"prefabId":0,"tag":"","team":0},"Transform2D":{"pos":[128.0,128.0],"rot":0.0,"scale":[1.0,1.0]}}},
  {"components":{"Meta":{"guid":4710000000000000003,"layer":0,"prefabId":0,"tag":"UI_HUD","team":0},"Transform2D":{"pos":[0.0,0.0],"rot":0.0,"scale":[1.0,1.0]},"UIDocument":{"modal":0,"reserved":0,"showOnStart":1,"sourceAssetGuid":9103797948311928834}}}
]})";
constexpr const char* kSceneSmokeSceneD = R"({"schemaVersion":2,"name":"SceneD","entities":[
  {"components":{"Meta":{"guid":4720000000000000001,"layer":0,"prefabId":0,"tag":"","team":0},"Transform2D":{"pos":[64.0,64.0],"rot":0.0,"scale":[1.0,1.0]}}},
  {"components":{"Meta":{"guid":4720000000000000002,"layer":0,"prefabId":0,"tag":"","team":0},"Transform2D":{"pos":[128.0,128.0],"rot":0.0,"scale":[1.0,1.0]}}},
  {"components":{"Meta":{"guid":4720000000000000003,"layer":0,"prefabId":0,"tag":"UI_HUD","team":0},"Transform2D":{"pos":[0.0,0.0],"rot":0.0,"scale":[1.0,1.0]},"UIDocument":{"modal":0,"reserved":0,"showOnStart":1,"sourceAssetGuid":9103797948311928834}}}
]})";
constexpr uint64_t kSceneSmokeDdolGuid = 10963355455248361612ull;

} // namespace

int main(int argc, char** argv) {
    std::string projectArg, sceneArg;
    int frames = 0;
    bool smoke = false, smokeScene = false, validate = false;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--project") && i + 1 < argc) projectArg = argv[++i];
        else if (!std::strcmp(argv[i], "--scene") && i + 1 < argc) sceneArg = argv[++i];
        else if (!std::strcmp(argv[i], "--frames") && i + 1 < argc) frames = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--smoke")) smoke = true;
        else if (!std::strcmp(argv[i], "--smoke-scene")) smokeScene = true;
        else if (!std::strcmp(argv[i], "--validate")) validate = true;
    }
    const std::string exeDir = ExeDir();
    // 交互 = 墙钟累加；自动化 = 每帧恰一步
    const bool paced = frames == 0 && !smoke && !smokeScene;

    // ---- 项目根与入口场景（--project 缺省 = exe 旁 data/，包形态零参启动）----
    std::string root = projectArg.empty() ? (exeDir + "/data") : projectArg;
    {
        std::error_code ec;
        root = fs::absolute(fs::path(root), ec).generic_string();
    }
    const assets::ProjectFile pf = assets::LoadProjectFile(root);
    if (!pf.ok) {
        LEMON_ERROR("lemon-game：project.lemon 不可用（路径 %s——--project <dir> 指定项目根）",
                    root.c_str());
        return 1;
    }
    std::string entryScene = sceneArg.empty() ? assets::ResolveEntryScene(root, pf) : sceneArg;
    if (entryScene.empty()) {
        LEMON_ERROR("lemon-game：入口场景不可解析（entryScene 未声明且场景不唯一/为零——"
                    "编辑器内 project.lemon 写入 entryScene）");
        return 1;
    }
    if (!fs::is_regular_file(fs::path(root) / entryScene)) {
        LEMON_ERROR("lemon-game：入口场景文件缺失：%s/%s", root.c_str(), entryScene.c_str());
        return 1;
    }

    // ---- 窗口 / 设备（pipeline cache 显式传项目内可写位——cwd 相对默认不触发）----
    // 顺序纪律（实测坑）：设备须先于 ScriptHost——CoreCLR 装载会重写
    // DYLD_FALLBACK_LIBRARY_PATH（.NET 自持路径替换 dyld 默认回退表），此后
    // vkCreateInstance 对裸文件名 library_path 的验证层 dlopen 找不到 brew 层
    // （VK_ERROR_LAYER_NOT_PRESENT，--validate 实抓；编辑器同为设备先行故无此症）
    InputCollector input;
    // 包形态 ICD 自举（M7a 批⑤；mac 专属——Windows 侧 Vulkan ICD 由显卡驱动
    // 注册表发现，无注册位问题）：Vulkan loader 的 ICD 发现默认走系统注册位
    // （brew /usr/local/etc/vulkan/icd.d）——干净机无注册位则 libMoltenVK 永不
    // 装载。exe 旁 MoltenVK_icd.json 在场 = packager 产物，显式指包内清单
    // （library_path 相对清单自身解析，随包可搬迁）；不覆写用户显式设置（调试态
    // 注入系统 MoltenVK 仍优先）。须在 Device::Create（vkCreateInstance）前。
#if defined(__APPLE__)
    if (const char* prevIcd = getenv("VK_ICD_FILENAMES"); !prevIcd || !*prevIcd) {
        std::error_code icdEc;
        const fs::path bundledIcd = fs::path(exeDir) / "MoltenVK_icd.json";
        if (fs::is_regular_file(bundledIcd, icdEc))
            setenv("VK_ICD_FILENAMES", bundledIcd.string().c_str(), /*overwrite=*/0);
    }
#endif
    auto window = Window::Create({.title = pf.name.c_str(), .width = 1280, .height = 720});
    if (!window) return 1;
    window->SetEventObserver(&InputCollector::OnEvent, &input);

    rhi::DeviceDesc dd;
    dd.appName = "lemon-game";
    dd.debugLayer = validate;
    const std::string cachePath = (fs::path(root) / ".lemon" / "game" / "pipeline-cache.bin")
                                      .string(); // DeviceDesc 只持指针——串须长于 Create
    dd.pipelineCachePath = cachePath.c_str();
    auto device = rhi::Device::Create(dd);
    if (!device) return 1;
    rhi::SwapchainDesc sd;
    sd.nativeWindow = window->NativeHandle();
    sd.present = paced ? rhi::PresentModePref::Fifo : rhi::PresentModePref::Immediate;
    if (!device->CreateSwapchain(sd)) return 1;

    // ---- C# 宿主（窗口/设备之后、World 装配之前：脚本即游戏，装配失败响亮退出；
    // entry dll 解析链：exe 旁 runtime/（pack 形态，CMake POST_BUILD 暂存）→
    // LEMON_SCRIPT_DIR（构建树回退）——替换编译期宏单源依赖（ADR-016 清障 #6））----
    std::string entryDir = exeDir + "/runtime";
#ifdef LEMON_SCRIPT_DIR
    if (!fs::is_regular_file(fs::path(entryDir) / "Lemon.Entry.dll")) entryDir = LEMON_SCRIPT_DIR;
#endif
    if (!fs::is_regular_file(fs::path(entryDir) / "Lemon.Entry.dll")) {
        LEMON_ERROR("lemon-game：Lemon.Entry 运行时缺失（%s/runtime/ 与构建树均无——"
                    "cmake --build lemon-game 暂存或批⑤ packager 拷入）",
                    exeDir.c_str());
        return 1;
    }
    scripting::ScriptHost host;
    const std::string gameDll = (fs::path(root) / ".lemon" / "bin" / "Game.dll").string();
    if (!fs::is_regular_file(gameDll)) {
        LEMON_ERROR("lemon-game：用户程序集缺失：%s（构建归编辑器/packager——先用编辑器"
                    "打开项目编译 Game/）",
                    gameDll.c_str());
        return 1;
    }
    // dotnetRoot 恒传 entryDir（M7a 批⑤ 包形态：self-contained 平铺 libhostfxr
    // 在场即命中;dev 形态该目录只有托管件——CoreCLRHost 落空后回退 env/brew 链，
    // 行为与批④的 nullptr 等价）
    if (!host.Initialize(entryDir.c_str(),
                         (entryDir + "/Lemon.Entry.runtimeconfig.json").c_str(),
                         (entryDir + "/Lemon.Entry.dll").c_str()) ||
        !host.LoadUserAssembly(gameDll.c_str())) {
        LEMON_ERROR("lemon-game：C# 宿主装配失败（%s）", gameDll.c_str());
        return 1;
    }

    // ---- 程序化页（白精灵槽 0 + 位图字体槽 1）+ 资产纹理（槽 2 起）----
    renderer::AtlasRegistry atlas;
    renderer::BitmapFont font;
    uint32_t whiteSprite = 0;
    auto BuildProceduralPages = [&](rhi::Device& d) {
        std::vector<uint8_t> px((size_t)kWhiteCellPx * kWhiteCellPx * 4, 0xFF);
        rhi::Texture tex = d.CreateTexture(
            {.width = kWhiteCellPx, .height = kWhiteCellPx, .debugName = "gameWhite"});
        d.UploadTexture(tex, px.data(), px.size());
        d.BindTextureToSlot(tex, 0);
        atlas.RegisterAtlas(0, tex, kWhiteCellPx, kWhiteCellPx);
        whiteSprite = atlas.AddSprite(0, 0, 0, kWhiteCellPx, kWhiteCellPx);
        font.Init(d, atlas, 1); // 程序化 ASCII 字体页（fx 飘字渲染）
        d.BindSamplerToSlot(d.CreateSampler({}), 0);
        d.BindSamplerToSlot(d.CreateSampler({.min = rhi::FilterMode::Point,
                                             .mag = rhi::FilterMode::Point}),
                            1);
    };
    BuildProceduralPages(*device);

    assets::AssetIndex index;
    if (!index.Open(root, atlas.SpriteCount() + 1)) return 1;
    assets::TextureStore textures;
    assets::AtlasStore bakedAtlas;
    uint32_t atlasPages = 0;
    const std::string bakedAtlasPath = (fs::path(root) / assets::kBakedAtlasRelPath).string();
    std::error_code atlasEc;
    if (fs::is_regular_file(bakedAtlasPath, atlasEc)) {
        // 包形态（批⑥ LAT1）：sprite 源不入包、链路全走图集 .baked——装载失败 =
        // 包完整性事故，响亮退出不回退（manifest 号账与 LAT1 几何账 packager 同轮）
        bakedAtlas.Init(*device, &atlas, index, /*firstSlot=*/2);
        if (!bakedAtlas.Load(bakedAtlasPath)) return 1;
        atlasPages = bakedAtlas.PageCount();
    } else {
        textures.Init(*device, &atlas, index, /*firstSlot=*/2);
        textures.LoadAll();
    }

    // ---- M7c 批①：Fx 飘字字体页（project.lemon fxFont → 烘焙页；编辑器/
    // packager 烤好 .baked 在场才装载——运行时零 FreeType 零栅格化红线，缺档 =
    // 内置 5×7 页降级红字）----
    if (pf.fxFont != 0) {
        if (index.FindByGuid(pf.fxFont) != nullptr) { // 在场性判存（条目内容不用）
            const std::string dst = assets::FontBakedPath(root, pf.fxFont);
            std::error_code fontEc;
            if (fs::is_regular_file(dst, fontEc))
                font.LoadBaked(*device, atlas, renderer::BitmapFont::kDefaultBakedSlot,
                               dst.c_str());
            else
                LEMON_WARN("lemon-game：fxFont 烘焙产物缺失（编辑器打开项目后台烤制"
                           "后重试）：%s——飘字用内置 5×7 页",
                           dst.c_str());
        } else {
            LEMON_WARN("lemon-game：fxFont 指向的字体资产不存在（guid %016llx）——"
                       "飘字用内置 5×7 页",
                       (unsigned long long)pf.fxFont);
        }
    }

    // ---- 音频（静音降级一等公民：无设备 = 红字 Warn 不阻断）----
    audio::AudioEngine audio;
    audio.Init();
    audio::AudioMount audioMount(audio);

    // ---- 游戏 UI（RmlUi over RHI；直渲染 swapchain → rtFormat = 交换链格式）----
    ui::UiSubsystem ui;
    if (!ui.Init(*device, device->SwapchainFormat(), window->NativeHandle()))
        LEMON_WARN("lemon-game：UiSubsystem 初始化失败（UI 缺席降级运行）");
    ui.SetDpReferenceHeight(720); // ③d-1 dp 坐标系（模板 RCSS 按 720 参考高）

    // ---- 装配：hooks 三族 + UI 解析器/贴图桥 + 字体链 ----
    GameHost hostState;
    g_host = &hostState;
    assets::PrefabCache prefabs;
    hostState.index = &index;
    hostState.scripts = &host;
    hostState.ui = &ui;
    hostState.projectRoot = root;
    hostState.pf = pf;
    scripting::SetEditorAssetHooks({HookSpriteOf, HookInstantiate});
    scripting::SetScriptIoHooks({HookSaveFlush});
    scripting::SetUiHooks({HookUiApplyOps, HookUiDrainEvents});
    scripting::SetSceneSourceHooks({HookResolveScene}); // 批⑦：SceneManager.LoadScene 寻址
    // 贴图桥（批③b 同款两协议）：guid:<16hex> 直引 | 相对路径 → 项目精灵页
    auto ResolveAtlasSprite = [&](uint32_t spriteId, rhi::Texture& tex, uint32_t& w,
                                  uint32_t& h) {
        if (!atlas.IsValidSprite(spriteId)) return false; // 未导入/空洞
        const renderer::SpriteInfo& si = atlas.GetSprite(spriteId);
        const rhi::Texture t = atlas.AtlasTexture(si.atlasIndex, w, h);
        if (!t.IsValid()) return false;
        tex = t;
        return true;
    };
    ui.SetTextureResolver([&](const std::string& source, rhi::Texture& tex, uint32_t& w,
                              uint32_t& h) {
        if (source.rfind("guid:", 0) == 0) {
            const assets::IndexedEntry* e =
                index.FindByGuid(assets::HexToGuid(source.substr(5)));
            if (!e || e->type != assets::AssetType::Sprite) return false;
            return ResolveAtlasSprite(e->spriteId, tex, w, h);
        }
        std::error_code ec;
        const fs::path rel = fs::relative(fs::path(source), fs::path(root), ec);
        if (ec) return false;
        const std::string relStr = rel.generic_string();
        if (relStr.empty() || relStr == "." || relStr.front() == '.') return false; // 越出根
        const assets::IndexedEntry* e = index.FindByPath(relStr);
        if (!e || e->type != assets::AssetType::Sprite) return false;
        return ResolveAtlasSprite(e->spriteId, tex, w, h);
    });
    // 通道 B 文档解析器（C# UI.Show 未装载文档名现载）
    ui.SetDocumentResolver([&](const std::string& relPath, std::string& absPath) {
        const assets::IndexedEntry* e = index.FindByPath(relPath);
        if (!e || e->type != assets::AssetType::Rml) return false;
        absPath = index.AbsolutePath(*e);
        return true;
    });
    // 字体链（ADR-016 清障 #6）：<root>/Fonts/（packager 拷入）→ 引擎源树 Noto 回退；
    // 项目字体（Assets 下 otf/ttf/ttc → fallback，编辑器 LoadProjectFonts 同款）
    {
        const fs::path packFont = fs::path(root) / "Fonts" / "NotoSansSC-Regular.otf";
        std::error_code ec;
        bool mainLoaded = false;
        if (fs::is_regular_file(packFont, ec)) {
            mainLoaded = ui.LoadFontFace(packFont.string().c_str(), "Noto Sans SC",
                                         /*fallback=*/true);
        }
#ifdef LEMON_ENGINE_FONT_DIR
        if (!mainLoaded)
            ui.LoadFontFace(LEMON_ENGINE_FONT_DIR "/NotoSansSC-Regular.otf", "Noto Sans SC",
                            /*fallback=*/true);
#endif
        for (const assets::IndexedEntry& e : index.Entries()) {
            std::string ext = fs::path(e.relPath).extension().string();
            for (char& c : ext) c = (char)std::tolower((unsigned char)c);
            if (ext != ".otf" && ext != ".ttf" && ext != ".ttc") continue;
            ui.LoadFontFace(index.AbsolutePath(e).c_str(), e.FileName().c_str(),
                            /*fallback=*/true);
        }
    }

    // ---- World（默认 20 系统全量管线 + RenderExtractSystem = Extract 阶段管线
    // 驱动形态——编辑器不装（插值时序契约），lemon-game 装配期消费＝批③移交件）----
    ecs::World world;
    world.InstallDefaultSystems();
    renderer::RenderableManager rm;
    world.Pipeline().AddSystem(std::make_unique<renderer::RenderExtractSystem>(atlas, rm));
    world.Pipeline().ResolveOrder();
    ecs::Scene& scene = world.CreateScene("game");
    world.SetActiveScene(&scene);

    // ---- 场景装载 + GUID 归一（spriteId 漂移后按 guid 复原——runtime/编辑器 id
    // 数值不同无害的落点）----
    {
        std::string sceneText;
        if (!ReadFileText(fs::path(root) / entryScene, sceneText) ||
            !ecs::SceneArchive::Load(scene, sceneText)) {
            LEMON_ERROR("lemon-game：入口场景装载失败：%s", entryScene.c_str());
            return 1;
        }
        const IdxSpriteRefSource spriteSrc(index);
        const assets::SpriteRefStats st = assets::ResolveSpriteRefs(scene, spriteSrc);
        if (st.danglingGuid)
            LEMON_WARN("lemon-game：场景 sprite 引用悬空 %u 处（渲染保留旧号）",
                       st.danglingGuid);
        // 批⑥b F2：初始人口建档+打标——"零未打标"从第一帧成立（首次 LoadScene 的
        // 组清场才能收走初始实体；档案名 = "name" 段恢复值 = scene.Name()）。
        // 批⑦：isLoaded=true——入口场景即已装载（C# sceneCount/GetSceneAt 查询面）
        const uint32_t entryHandle =
            world.CreateSceneRecord(scene.Name(), entryScene.c_str());
        if (ecs::World::SceneRecord* r = world.FindSceneRecord(entryHandle))
            r->isLoaded = true;
        ecs::StampSceneMembership(scene, entryHandle);
        world.SetActiveSceneHandle(entryHandle);
    }
    hostState.world = &world;

    // ---- 四缓存 + 存档 + 脚本解析（编辑器 EnterPlay 装配清单同序）----
    {
        const IdxPrefabSource prefabSrc(index);
        prefabs.Build(prefabSrc);
    }
    world.SetSpawnFn([&prefabs](ecs::Scene& s, uint32_t prefabId, Vec2 pos, uint32_t team) {
        return prefabs.Spawn(s, prefabId, pos, team);
    });
    {
        const IdxPlayCacheSource cacheSrc(index);
        assets::BuildClipCache(world, cacheSrc);
        assets::BuildControllerCache(world, cacheSrc);
        assets::BuildTableCache(world, cacheSrc);
    }
    for (uint8_t ch = 0; ch < kSaveChannelCount; ++ch)
        assets::SaveStore::Load(root, ch, world.Saves(ch));
    world.SetScriptBackend(&host);
    host.ResetScriptTime();
    host.ResetPlayDomain();
    // ScriptBox 槽解析（EditorContext::ResolvePlayScripts 同款；批⑥b 起 lambda 化——
    // 入口装配与换场 afterBuild 复用同一序列（F3 镜像））
    auto resolveSceneScripts = [&](ecs::Scene& s) {
        const auto& names = host.BehaviourTypeNames();
        auto typeIdOf = [&](const char* cls) {
            for (size_t i = 0; i < names.size(); ++i)
                if (names[i] == cls) return (int)i;
            return -1;
        };
        s.Each([&](ecs::Entity e) {
            scripting::ScriptBox* sb = s.TryGet<scripting::ScriptBox>(e);
            if (!sb) return;
            for (uint32_t i = 0; i < sb->count; ++i) {
                scripting::ScriptSlot& sl = sb->slots[i];
                if (sl.typeId >= 0) continue;
                const int id = typeIdOf(sl.className);
                if (id < 0) {
                    LEMON_WARN("lemon-game：脚本类型未注册（跳过）'%s'", sl.className);
                    continue;
                }
                host.ResolveSlotBehaviour(world, s, e, i, id);
            }
        });
    };
    resolveSceneScripts(scene);

    // ---- UI/音频挂载（通道 A 声明装载 + 烤制/装载注册 + 后端接线）----
    uint32_t mountedAudio = 0;
    // 声明装载 lambda 化（换场 afterBuild 复用；ReconcileDocuments 随装载头调用
    // —— M6b 根因收口的不变量层口径）。批⑨ 起入口场景零声明文档 = 返回值恒 0
    //（装载链断言移 RESULT 侧 DocumentLoadCount——code-mount 口径），不再记账
    auto mountSceneUi = [&](ecs::Scene& s) -> uint32_t {
        const IdxUiDocSource uiSrc(index);
        const uint32_t n = ui::MountSceneDocuments(ui, s, uiSrc);
        ui::ReconcileDocuments(ui, uiSrc);
        return n;
    };
    mountSceneUi(scene);
    {
        const IdxAudioSource audioSrc(index);
        mountedAudio = audioMount.MountAll(audioSrc);
    }
    audioMount.WireBackend(world);

    // ---- 换场钩子（批⑥b；SceneSwitcher 编排的宿主件段——引擎件（Fx/Audio.Paused）
    // 编排直兑不经此。afterBuild = 入口装载同序列（F3：漏此段 = 首帧渲染与脚本全哑）----
    world.Switcher().SetHooks({
        .sweep = [&ui] { ui.UnloadDocumentsByOrigin(ui::UiDocOrigin::Scene); },
        .afterBuild = [&](ecs::Scene& s) {
            const IdxSpriteRefSource spriteSrc(index);
            const assets::SpriteRefStats st = assets::ResolveSpriteRefs(s, spriteSrc);
            if (st.danglingGuid)
                LEMON_WARN("lemon-game：换场 sprite 引用悬空 %u 处（渲染保留旧号）",
                           st.danglingGuid);
            resolveSceneScripts(s);
            mountSceneUi(s);
        },
    });

    // ---- 设备丢失重建（resize 触发整设备重建：程序化页 → 资产页 → UI 全量重载，
    // 编辑器 "editor-viewport"→"asset-gpu" 两段式同序）----
    device->AddRecreateCallback("game-assets", [&](rhi::Device& d) {
        atlas.Reset();
        BuildProceduralPages(d);
        if (atlasPages) bakedAtlas.RebuildAll(d);
        else textures.RebuildAll(d);
        ui.ReloadAllDocuments();
    });

    // ---- 渲器件（colorFormat = 交换链格式：动态渲染管线唯一格式依赖）----
    renderer::SpriteBatcher batcher;
    batcher.Init(*device, 0, 1, device->SwapchainFormat());
    device->SavePipelineCache();

    renderer::Camera2D gameCam;
    renderer::CameraFollowState camFollow; // 首帧吸附目标（默认位 = ResetCameraFollow 同款）
    gameCam.center = {640.0f, 360.0f};

    // ---- 主循环 ----
    const auto NowS = []() {
        return std::chrono::duration<double>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    };
    uint64_t frame = 0;
    double frameSec = 0, prevS = NowS();
    uint32_t statFrames = 0;
    float acc = 0.0f, alpha = 1.0f;
    bool prevLeft = false;
    bool hudShown = false; // 进局观察位（smoke 点击驱动 → HUD 文档显示）
    bool cardsShown = false; // 动态文档观察位（升级三选一/死亡对话 = cards.rml：
                             // C# UI.Show 动态弹卡链——2026-10-04 真人验收发现回归
                             // 只断言 hud 后补；demo/svr-test 的 RtUi 卡不在此链）
    // --smoke 流程驱动（smoke-template 引擎直灌三帧机同款）：菜单就绪后点
    // 「开始游戏」→ 进局——UI 点击→UiEvent→C# 流程→prefab spawn→渲染提取的
    // 机器面端到端。150 定位（盒中心保持指针，Update 建悬停）→151 down→152 up
    float smokePtrX = 0, smokePtrY = 0;
    bool smokeHold = false;
    // --smoke-scene 换场四跳（批⑦：Main→B→C→B 同名重装→D）。每跳七面断言：
    // 旧组归零至 DDOL 来源基数 / DDOL 系精确清单句柄不变 / 零孤组 / 新组 3 实体
    // / origin=Scene 卸载 + 新声明装载 / Audio.Paused 强制清 / Fx 整场清 / 档案
    // isLoaded 翻转 / 每载一档新句柄。事件序断言归 script-tests TestSceneSdk（验收
    // 分工 D4）。
    struct SceneHopDoc { const char* name; const char* json; };
    const SceneHopDoc sceneHops[4] = {
        {"SceneB", kSceneSmokeSceneB}, {"SceneC", kSceneSmokeSceneC},
        {"SceneB", kSceneSmokeSceneB}, {"SceneD", kSceneSmokeSceneD}};
    int sceneHop = -1; // -1 未发起；0..3 进行中；4 完成
    uint32_t sceneHopPrev = 0, sceneHopEntry = 0, sceneHopHandles[4] = {0, 0, 0, 0};
    bool sceneHopOk[4] = {false, false, false, false};
    // 第 5 跳 async（批⑧）：-1 未发；0 门关观测中；1 已开门待激活；2 终态
    int sceneHopAsync = -1;
    uint32_t sceneAsyncOp = 0;
    bool sceneHopAsyncOk = false;
    bool asyncMonotonic = false, asyncHashStable = true, asyncCapped = true;
    uint64_t asyncHashBaseline = 0;
    float asyncLastProgress = 0.0f;
    int asyncGateHold = 0;
    ecs::Entity sceneSmokeDdol = ecs::Entity::Null();
    std::vector<ecs::Entity> sceneDdolBaseline; // 首跳前 DDOL 系快照（精确清单对照）
    std::vector<SpritePacket> textPackets, fxBarPackets;
    std::chrono::steady_clock::time_point lastFxTime{};
    bool running = true;
    while (running) {
        if (!window->PollEvents()) running = false; // 窗口关闭
        if (frames > 0 && (uint64_t)frame >= (uint64_t)frames) running = false;
        if (!running) break;
        if (window->TakeResized() && !device->RecreateSwapchain()) continue;

        const double t0 = NowS();
        const float dt = paced ? std::clamp((float)(t0 - prevS), 0.0f, 0.25f) : kFixedDt;
        prevS = t0;

        // 输入快照（编辑器 Play 输入路由同款：WASD/箭头 → 轴；Space/R/Esc|P 位键；
        // RmlUi 文本/模态让出——lemon-game 窗口恒聚焦，让出门只剩 UI 侧）
        InputState in;
        const bool uiHoldsInput = ui.WantsKeyboard() || ui.AnyModalShown();
        if (!uiHoldsInput) {
            float ax = 0, ay = 0;
            if (input.keyDown[SDL_SCANCODE_A] || input.keyDown[SDL_SCANCODE_LEFT]) ax -= 1;
            if (input.keyDown[SDL_SCANCODE_D] || input.keyDown[SDL_SCANCODE_RIGHT]) ax += 1;
            if (input.keyDown[SDL_SCANCODE_W] || input.keyDown[SDL_SCANCODE_UP]) ay -= 1;
            if (input.keyDown[SDL_SCANCODE_S] || input.keyDown[SDL_SCANCODE_DOWN]) ay += 1;
            in.ax = ax;
            in.ay = ay;
            if (input.keyDown[SDL_SCANCODE_SPACE]) in.buttons |= 1u << 4;
            if (input.keyDown[SDL_SCANCODE_R]) in.buttons |= 1u << 5;
            if (input.keyDown[SDL_SCANCODE_ESCAPE] || input.keyDown[SDL_SCANCODE_P])
                in.buttons |= 1u << 6;
        }
        world.ApplyInput(in);
        audio.Tick(dt);

        // 固定步进（交互 = 墙钟累加 + 追帧上限；自动化 = 每渲染帧恰一步 alpha=1——
        // 编辑器自动化链同口径；Extract 阶段随 Step 跑（RenderExtractSystem））
        if (!paced) {
            world.Step(kFixedDt);
            alpha = 1.0f;
        } else {
            acc = std::min(acc + dt, kMaxAcc);
            int steps = 0;
            while (acc >= kFixedDt && steps < kMaxSteps) {
                world.Step(kFixedDt);
                acc -= kFixedDt;
                ++steps;
            }
            if (steps == 0) world.Step(0.0f); // 空转帧：Essential 照跑（编辑器同款）
            alpha = acc / kFixedDt;
        }
        // 批⑦：帧尾裸 CommitDestroys 兜底已删（⑥b review 登记项）——Essential
        // #17/#18 双提交点已清空队列，此处恒 no-op；删裸 commit 防"绕通知提交"的
        // 错误先例（OnDestroy 补发在 DestroyCommitSystem 内，F1 红线）

        // 相机跟随（编辑器薄壳同款优先级：Camera tag → Player → 脚本实体）+
        // 音频监听器（一帧延迟口径 = 编辑器同款；空间化无感）
        renderer::UpdateCameraFollow(scene, gameCam, camFollow);
        {
            audio::AudioListener l;
            l.center = gameCam.center;
            l.halfWidth = gameCam.HalfWidth((float)device->SwapchainWidth() /
                                            (float)device->SwapchainHeight());
            world.SetAudioListener(l);
        }

        // UI 输入喂入（指针 = 窗口点 → swapchain 像素换算；键盘/鼠标差分出边沿；
        // IME 锚点 = 直渲染画布即窗口 → 像素→点换算）
        {
            if (smoke && !smokeScene && !smokeHold && frame == 150) {
                float w = 0, h = 0, x = 0, y = 0;
                if (ui.TryGetElementBox("Assets/UI/main.rml", "btn-start", &w, &h, &x, &y) &&
                    w > 1.0f && h > 1.0f) {
                    smokePtrX = x + w * 0.5f;
                    smokePtrY = y + h * 0.5f;
                    smokeHold = true;
                } else {
                    LEMON_WARN("lemon-game：smoke 菜单按钮定位失败（main.rml btn-start）");
                }
            }
            int pw = 1, ph = 1;
            window->GetPixelSize(pw, ph);
            const float sx = pw > 0 ? (float)device->SwapchainWidth() / (float)pw : 1.0f;
            const float sy = ph > 0 ? (float)device->SwapchainHeight() / (float)ph : 1.0f;
            if (smokeHold)
                ui.SetPointer((int)smokePtrX, (int)smokePtrY, true);
            else
                ui.SetPointer((int)(input.mx * sx), (int)(input.my * sy), input.inside);
            ui.SetImeRectTransform(0.0f, 0.0f, sx > 0 ? 1.0f / sx : 1.0f,
                                   sy > 0 ? 1.0f / sy : 1.0f);
            if (smokeHold && (frame == 151 || frame == 152)) { // 注入点击边沿
                ui.ProcessMouseButton(0, frame == 151);
                if (frame == 152) smokeHold = false;
            } else if (input.leftDown != prevLeft) {
                ui.ProcessMouseButton(0, input.leftDown);
            }
            prevLeft = input.leftDown;
            for (int sc = 0; sc < SDL_SCANCODE_COUNT; ++sc) {
                const int uk = InputCollector::UiKeyOf((SDL_Scancode)sc);
                if (uk < 0) continue;
                if (input.keyDown[sc] != input.uiKeyWasDown[uk])
                    ui.ProcessKey((ui::UiKey)uk, input.keyDown[sc]);
                input.uiKeyWasDown[uk] = input.keyDown[sc];
            }
            if (!input.textInput.empty()) {
                ui.ProcessTextInput(input.textInput.c_str());
                input.textInput.clear();
            }
            if (input.editing.has) {
                ui.ProcessTextEditing(input.editing.text.c_str(), input.editing.start,
                                      input.editing.len);
                input.editing = {};
            }
        }
        ui.Update();

        // ---- 渲染（D8 A 案：sprite pass + RmlUi pass 同一动态渲染块直画 swapchain）----
        rhi::AcquireResult acq = device->AcquireNextImage();
        if (acq.deviceLost || acq.needsRecreate) {
            if (!acq.deviceLost) device->RecreateSwapchain();
            continue;
        }
        rhi::CommandList& cl = device->BeginFrame();
        const uint32_t w = device->SwapchainWidth(), h = device->SwapchainHeight();
        const float aspect = (float)w / (float)h;
        const Rect view = gameCam.ViewRect(aspect);
        rm.SetViewport(gameCam.center, (view.max.x - view.min.x) * 0.5f,
                       (view.max.y - view.min.y) * 0.5f, 200.0f);
        auto packets = rm.Extract(atlas, alpha);

        textPackets.clear();
        fxBarPackets.clear();
        {
            const auto now = std::chrono::steady_clock::now();
            const float fxDt = lastFxTime.time_since_epoch().count() == 0
                                   ? 0.0f
                                   : std::clamp(
                                         std::chrono::duration<float>(now - lastFxTime).count(),
                                         0.0f, 0.1f);
            lastFxTime = now;
            renderer::AppendGameFx(world.Fx(), scene, view, whiteSprite, font, atlas,
                                   fxDt, fxBarPackets, textPackets);
        }
        batcher.Bake(atlas, packets, {}, textPackets,
                     std::span<const SpritePacket>(fxBarPackets));

        const float clear[4] = {0.09f, 0.10f, 0.13f, 1.0f};
        cl.BeginPass(device->SwapchainFormat(), w, h, clear);
        cl.SetViewportScissor(w, h);
        batcher.Record(cl, gameCam.ViewProj(aspect));
        ui.Render(cl, w, h); // sprite 之后、EndPass 之前（动态块内唯一合法插入点）
        cl.EndPass();

        bool needRe = false, lost = false;
        device->EndFrameAndPresent(needRe, lost);
        if (lost || needRe) {
            if (lost || !device->RecreateSwapchain()) continue;
        }
        batcher.AdvanceFrame();

        if (frame > 0) {
            frameSec += NowS() - t0;
            ++statFrames;
        }
        if (frame > 200 && !hudShown && ui.IsDocumentShown("Assets/UI/hud.rml"))
            hudShown = true; // 进局观察位（点击→流程→HUD 显示链）
        if (frame > 200 && !cardsShown && ui.IsDocumentShown("Assets/UI/cards.rml"))
            cardsShown = true; // 升级/死亡动态弹卡观察位（UI.Show 运行时装载链）
        // --smoke-scene：菜单态预置脏态 + DDOL 根位标记 + 首跳（帧尾 Request →
        // 下一帧 Essential #18 执行 = ADR-017"下一帧装载"时序）
        if (smokeScene && sceneHop == -1 && frame == 80) {
            sceneHop = 0;
            sceneHopPrev = world.ActiveSceneHandle();
            sceneHopEntry = sceneHopPrev;
            audio.SetPaused(true);          // D6 强制清的断言源（每跳重置）
            world.Fx().PopupText("stale", 0.0f, 0.0f); // Fx 整场清的断言源（每跳重置）
            scene.Each([&](ecs::Entity e) {
                if (const ecs::Meta* m = scene.TryGet<ecs::Meta>(e);
                    m && m->guid == kSceneSmokeDdolGuid)
                    sceneSmokeDdol = e;
            });
            if (!sceneSmokeDdol.IsNull())
                ecs::MarkDontDestroyOnLoad(scene, sceneSmokeDdol); // 批⑦ D1 根位式
            sceneDdolBaseline = ecs::CollectDontDestroyOnLoadLineage(scene);
            world.Switcher().Request({.name = sceneHops[0].name,
                                      .path = "Scenes/SceneB.scene",
                                      .jsonText = sceneHops[0].json});
        }
        // 本跳已执行（active 拨动）：收集七面断言；全过则脏态重置 + 下一跳请求
        if (smokeScene && sceneHop >= 0 && sceneHop < 4 &&
            world.ActiveSceneHandle() != sceneHopPrev) {
            const uint32_t newH = world.ActiveSceneHandle();
            sceneHopHandles[sceneHop] = newH;
            const uint32_t prevLeft = ecs::CountSceneGroup(scene, sceneHopPrev);
            // DDOL 系来源 = 入口组——旧组归零期望值 =（本跳旧组 == 入口组 ? 基数 : 0）
            const uint32_t expectLeft =
                sceneHopPrev == sceneHopEntry ? (uint32_t)sceneDdolBaseline.size() : 0;
            const uint32_t orphan =
                ecs::CountSceneGroup(scene, ecs::kSceneHandleUnassigned);
            const std::vector<ecs::Entity> survivors =
                ecs::CollectDontDestroyOnLoadLineage(scene);
            bool ddolStable = !sceneDdolBaseline.empty() &&
                              survivors.size() == sceneDdolBaseline.size();
            for (size_t i = 0; ddolStable && i < survivors.size(); ++i)
                ddolStable = survivors[i] == sceneDdolBaseline[i]; // 句柄不变
            bool freshHandle = newH != sceneHopPrev; // 每载一档（含同名重装发新句柄）
            for (int k = 0; k < sceneHop; ++k)
                if (sceneHopHandles[k] == newH) freshHandle = false;
            const ecs::World::SceneRecord* recPrev = world.FindSceneRecord(sceneHopPrev);
            const ecs::World::SceneRecord* recNew = world.FindSceneRecord(newH);
            sceneHopOk[sceneHop] =
                prevLeft == expectLeft && orphan == 0 && ddolStable && freshHandle &&
                ecs::CountSceneGroup(scene, newH) == 3 &&
                ui.HasDocument("Assets/UI/main.rml") && // 批⑨：CSharp 幸存者仍挂
                !ui.IsDocumentShown("Assets/UI/main.rml") && // 流程已隐（未知场不认领）
                ui.IsDocumentShown("Assets/UI/cards.rml") &&
                !audio.IsPaused() && world.Fx().TextCount() == 0 &&
                recPrev && !recPrev->isLoaded && recNew && recNew->isLoaded;
            if (++sceneHop < 4) {
                sceneHopPrev = newH;
                audio.SetPaused(true);
                world.Fx().PopupText("stale", 0.0f, 0.0f);
                world.Switcher().Request(
                    {.name = sceneHops[sceneHop].name,
                     .path = std::string("Scenes/") + sceneHops[sceneHop].name + ".scene",
                     .jsonText = sceneHops[sceneHop].json});
            }
        }
        // --smoke-scene 第 5 跳（批⑧ async，门控变体）：C++ 直发 RequestAsync +
        // allowSceneActivation=false——预备/门关期间世界逐帧哈希不变（staging 零
        // 可见性）+ progress 单调且封顶 0.9 → 稳定后开门 → 激活（七面 + isDone/
        // progress==1.0 终态）。预算默认 4ms（本夹具 3 实体一帧直抵门 = 0.9 停）；
        // 多帧分帧行为归 engine 单测（TestSceneAsyncMachineContract）
        if (smokeScene && sceneHop == 4 && sceneHopAsync == -1 && frame == 100) {
            sceneHopAsync = 0; // 0 = 门关观测中
            sceneHopPrev = world.ActiveSceneHandle();
            audio.SetPaused(true);      // D6 强制清断言源（本跳重置）
            world.Fx().PopupText("stale", 0.0f, 0.0f); // Fx 整场清断言源
            sceneAsyncOp = world.Switcher().RequestAsync(
                {.name = "SceneB", .path = "Scenes/SceneB.scene",
                 .jsonText = kSceneSmokeSceneB});
            world.Switcher().Async().SetActivation(sceneAsyncOp, false);
            asyncHashBaseline = ecs::ComputeStateHash(scene);
            asyncLastProgress = 0.0f;
            asyncGateHold = 0;
            asyncMonotonic = sceneAsyncOp != 0;
            asyncHashStable = true;
            asyncCapped = true;
        }
        if (smokeScene && sceneHopAsync == 0 && sceneAsyncOp != 0) {
            ecs::AsyncSceneQuery q;
            if (world.Switcher().Async().Query(sceneAsyncOp, q)) {
                asyncMonotonic = asyncMonotonic && q.progress >= asyncLastProgress - 1e-6f;
                asyncLastProgress = q.progress;
                if (!q.isDone) {
                    asyncHashStable = asyncHashStable &&
                                      ecs::ComputeStateHash(scene) == asyncHashBaseline;
                    asyncCapped = asyncCapped && q.progress <= 0.9f + 1e-6f;
                    if (q.progress >= 0.9f - 1e-6f && ++asyncGateHold >= 2) {
                        world.Switcher().Async().SetActivation(sceneAsyncOp, true);
                        sceneHopAsync = 1; // 1 = 已开门待激活
                    }
                }
            }
        }
        if (smokeScene && sceneHopAsync == 1 && world.ActiveSceneHandle() != sceneHopPrev) {
            // 激活落地：终态 + 七面（对照同步跳同款口径；SceneB 3 实体）
            const uint32_t newH = world.ActiveSceneHandle();
            const uint32_t orphan =
                ecs::CountSceneGroup(scene, ecs::kSceneHandleUnassigned);
            const std::vector<ecs::Entity> survivors =
                ecs::CollectDontDestroyOnLoadLineage(scene);
            bool ddolStable = survivors.size() == sceneDdolBaseline.size();
            for (size_t i = 0; ddolStable && i < survivors.size(); ++i)
                ddolStable = survivors[i] == sceneDdolBaseline[i];
            const ecs::World::SceneRecord* recPrev = world.FindSceneRecord(sceneHopPrev);
            const ecs::World::SceneRecord* recNew = world.FindSceneRecord(newH);
            ecs::AsyncSceneQuery q;
            const bool terminal = world.Switcher().Async().Query(sceneAsyncOp, q) &&
                                  q.isDone && q.progress >= 1.0f - 1e-6f;
            sceneHopAsync = 2; // 终态（RESULT 汇总）
            sceneHopAsyncOk = terminal && orphan == 0 && ddolStable &&
                              ecs::CountSceneGroup(scene, newH) == 3 &&
                              ui.HasDocument("Assets/UI/main.rml") && // 批⑨ 同步跳同款
                              !ui.IsDocumentShown("Assets/UI/main.rml") &&
                              ui.IsDocumentShown("Assets/UI/cards.rml") &&
                              !audio.IsPaused() && world.Fx().TextCount() == 0 &&
                              recPrev && !recPrev->isLoaded && recNew && recNew->isLoaded &&
                              asyncMonotonic && asyncHashStable && asyncCapped;
        }
        ++frame;
        if (paced && (frame % 120) == 0 && statFrames > 0 && frameSec > 0) {
            std::printf("[lemon-game] fps=%.1f alive=%u\n",
                        (double)statFrames / frameSec, scene.AliveCount());
            std::fflush(stdout); // 重定向/管道下块缓冲即时可见（日志采集面）
        }
    }

    // ---- 收尾：存档兜底落盘（脚本显式 Flush 之外的保险——编辑器 ExitPlay 同款）----
    for (uint8_t ch = 0; ch < kSaveChannelCount; ++ch)
        assets::SaveStore::Write(root, ch, world.Saves(ch));
    audio.StopAll();

    if (smoke) {
        // RESULT 行（回归口径）：装载链在场（uidoc/audio/脚本系统）+ 进局成功
        // （点击驱动 hudShown + 动态弹卡 cardsShown）+ 场景活体 + 可见精灵 + 零
        // UI 契约错误 => OK。纯 UI 入口场景（无 SpriteRenderer）跑 --frames 不带
        // 点击驱动的诊断形态会 FAIL visible——回归夹具恒用模板（点击进局后有
        // gameplay 精灵）；cards 断言限定模板（demo 的卡走 RtUi 编辑器叠层，
        // lemon-game 无呈现面——批④ §5 登记边界）
        const bool ok = scene.AliveCount() > 0 && rm.LastStats().visible > 0 &&
                        batcher.LastBatchCount() > 0 && ui.ContractErrorCount() == 0 &&
                        ui.DocumentLoadCount() >= 4 && mountedAudio > 0 && // 批⑨：code-mount 装载链（四屏预装起）
                        hudShown && cardsShown; // 脚本链断言 = 行为面（hud/cards 由 C# 驱动）——
                                                // 批⑨ 起模板零批量系统（RunSweeper 退役）
        std::printf(
            "[lemon-game] RESULT game-smoke: frames=%llu fps=%.1f alive=%u visible=%u "
            "batches=%u ticks=%llu uidoc=%u audio=%u atlas=%u scriptSys=%u contractErr=%u "
            "hud=%d cards=%d => %s\n",
            (unsigned long long)frame, frameSec > 0 ? (double)statFrames / frameSec : 0.0,
            scene.AliveCount(), rm.LastStats().visible, batcher.LastBatchCount(),
            (unsigned long long)world.TickIndex(), ui.DocumentLoadCount(), mountedAudio, atlasPages,
            host.BatchSystemCount(), ui.ContractErrorCount(), hudShown ? 1 : 0,
            cardsShown ? 1 : 0, ok ? "OK" : "FAIL");
        std::fflush(stdout);
        g_host = nullptr;
        ui.Shutdown();
        return ok ? 0 : 1;
    }
    if (smokeScene) {
        // RESULT 行（回归口径）：四跳全断言（每跳七面）+ 第 5 跳 async 终态
        //（门控变体：单调/哈希不变/0.9 封顶/七面）+ 零 UI 契约错误 => OK。
        // sceneHop < 4 或 async 跳未终态 = 帧预算内未完成（红）。
        const uint32_t newH = world.ActiveSceneHandle();
        bool hopsAll = sceneHop == 4;
        for (int k = 0; k < 4; ++k) hopsAll = hopsAll && sceneHopOk[k];
        hopsAll = hopsAll && sceneHopAsync == 2 && sceneHopAsyncOk;
        const bool ok = hopsAll && ui.ContractErrorCount() == 0;
        std::printf(
            "[lemon-game] RESULT scene-smoke: frames=%llu hops=%d async=%d entry=%u final=%u "
            "finalGroup=%u orphan=%u ddolAlive=%d ddolLineage=%zu hopOk=%d%d%d%d "
            "asyncOk=%d(monotonic=%d hashStable=%d capped=%d prog=%.2f) "
            "uiMainIdle=%d uiCardsShown=%d unpaused=%d fxCleared=%d contractErr=%u "
            "=> %s\n",
            (unsigned long long)frame, sceneHop, sceneHopAsync == 2 ? 1 : 0, sceneHopEntry,
            newH, ecs::CountSceneGroup(scene, newH),
            ecs::CountSceneGroup(scene, ecs::kSceneHandleUnassigned),
            scene.Alive(sceneSmokeDdol) ? 1 : 0,
            ecs::CollectDontDestroyOnLoadLineage(scene).size(),
            sceneHopOk[0] ? 1 : 0, sceneHopOk[1] ? 1 : 0, sceneHopOk[2] ? 1 : 0,
            sceneHopOk[3] ? 1 : 0, sceneHopAsyncOk ? 1 : 0, asyncMonotonic ? 1 : 0,
            asyncHashStable ? 1 : 0, asyncCapped ? 1 : 0, (double)asyncLastProgress,
            ui.IsDocumentShown("Assets/UI/main.rml") ? 0 : 1,
            ui.IsDocumentShown("Assets/UI/cards.rml") ? 1 : 0,
            audio.IsPaused() ? 0 : 1, world.Fx().TextCount() == 0 ? 1 : 0,
            ui.ContractErrorCount(), ok ? "OK" : "FAIL");
        std::fflush(stdout);
        g_host = nullptr;
        ui.Shutdown();
        return ok ? 0 : 1;
    }
    if (statFrames > 0 && frameSec > 0)
        std::printf("[lemon-game] exit: frames=%llu fps=%.1f\n", (unsigned long long)frame,
                    (double)statFrames / frameSec);
    g_host = nullptr;
    ui.Shutdown();
    device->SavePipelineCache();
    return 0;
}
