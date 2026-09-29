// Lemon 编辑器 — 应用壳实现（M4.md §3.1；主循环承 anim-smoke 全链基线）
// M4.0：壳 + 默认布局 + DPI/字体 + smoke；M4.1：EditorContext/场景 IO/快捷键/关闭确认。
// 2026-09-29 批②：UI 骨架/编辑动作/脚本管线拆出 EditorAppChrome/Actions/Scripts.cpp（机械拆分零行为变化）
// 2026-09-29 批①：vs-survivor 模板生成器外迁 Templates/VsTemplateGen.{h,cpp}（机械搬移零行为变化）
#include "App/EditorApp.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <sstream>
#include <vector>

#include <nlohmann/json.hpp>

#include "stb_image_write.h"

// 批③c：UI 桥钩子目标（gameUi_ 生命周期镜像——Init 成功置位 / Shutdown 清空；
// UiHooks 是 C 函数指针，不能捕 this）
static ::lemon::ui::UiSubsystem* s_gameUiForHooks = nullptr;


#include <SDL3/SDL.h>

#include "App/ImGuiBackend.h"
#include "Assets/AssetDatabase.h"
#include "Assets/ClipEdit.h" // M6a 批② T3：smoke-anim clip 编辑链（面板数据面同款）
#include "Assets/ControllerEdit.h" // T3d：smoke-anim graph 链（controller 数据面）
#include "Assets/ProjectWizard.h"
#include "Interaction/ViewportRenderer.h"
#include "Templates/VsTemplateGen.h"
#include "App/RecentProjects.h"
#include "Tooling/Icons.h"
#include "Tooling/Theme.h"
#include "Tooling/ThumbCache.h"
#include "Components/BehaviorComponents.h"
#include "Components/CoreComponents.h"
#include "Components/GameplayComponents.h"
#include "Components/RenderComponents.h"
#include "Components/UiComponents.h"
#include <unistd.h> // getpid（bench-survivor tempdir）
#include "ECS/Hierarchy.h"
#include "Core/Log.h"
#include "EditorContext.h"
#include "Panels/BuiltInPanels.h"
#include "Platform/Window.h"
#include "Renderer/RHI.h"
#include "Ui/UiSubsystem.h" // 批③a（ADR-014）：游戏 UI 层（RmlUi）
#include "Scripting/ScriptHost.h"
#include "Serialization/SceneArchive.h"
#include "imgui.h"
#include "imgui_internal.h" // DockBuilder（docking 分支布局编程 API）+ FindWindowByName
#include "misc/cpp/imgui_stdlib.h" // InputText(std::string*) 重载（Layout 命名等）
#include "Tooling/TestHooks.h"

namespace lemon::editor {

EditorApp::EditorApp() = default;
EditorApp::~EditorApp() = default;

namespace {
// C# native 资产钩子（M4.4 #8；进程一份——EditorApp 即进程单例）
EditorApp* g_app = nullptr;
// smoke-ui C8：Ctrl+D 已子树化——计数断言的增量 = 选中根的子树大小
// （种子 Player 带 3 个 Mob 子节点，子树 = 4）
uint32_t SubtreeSizeOf(ecs::Scene& s, ecs::Entity root) {
    uint32_t n = 0;
    ecs::Entity stack[64];
    int top = 0;
    if (!root.IsNull() && s.Alive(root)) stack[top++] = root;
    while (top > 0) {
        ecs::Entity e = stack[--top];
        ++n;
        const ecs::Hierarchy* h = s.TryGet<ecs::Hierarchy>(e);
        for (ecs::Entity c = h && !h->firstChild.IsNull() && s.Alive(h->firstChild)
                                 ? h->firstChild
                                 : ecs::Entity::Null();
             !c.IsNull() && s.Alive(c);) {
            const ecs::Hierarchy* ch = s.TryGet<ecs::Hierarchy>(c);
            const ecs::Entity nx =
                ch && !ch->next.IsNull() && s.Alive(ch->next) ? ch->next : ecs::Entity::Null();
            if (top < 64) stack[top++] = c;
            c = nx;
        }
    }
    return n;
}
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

// ---- M5 批③：--smoke-anim 固定 guid（程序化 4 帧表 + clip；yami 包同段命名）----
constexpr uint64_t kAnimSheetGuid = 0x5bd31a7c30000001ull; // anim-sheet.png（128×32，4×32×32 格）
constexpr uint64_t kAnimClipGuid = 0x5bd31a7c30000002ull;  // anim.anim（fps10 × cells 0..3）
constexpr uint64_t kAnimHitClipGuid = 0x5bd31a7c30000003ull; // anim-hit.anim（M6a 批①：
// fps12 × cells [3,2] loop=0——切段断言的受击段）
constexpr uint64_t kAnimEditClipGuid = 0x5bd31a7c30000004ull; // anim-edit.anim（M6a 批②
// T3：AnimationPanel 数据面编辑链断言——写→改 fps/增帧→存→roundtrip→Play 快照）
constexpr uint64_t kAnimWholeClipGuid = 0x5bd31a7c30000005ull; // anim-whole.anim（T3b-1
// 整图引用断言——未切片 smoke.png cell0 → 本体号，Play 快照 1 帧）
constexpr uint64_t kAnimSetGuid = 0x5bd31a7c30000006ull; // anim-set.override（T3c：动画集
// 按名解析断言——段引用上述三段 clip，集内按名/GUID hex 回退的反查锚点）
constexpr uint64_t kAnimGraphGuid = 0x5bd31a7c30000007ull; // anim-graph.controller（T3d：
// whole→walk 条件边 / walk→hit trigger 边 / hit→whole exitTime 边——graph 链断言锚点）
constexpr uint64_t kSmokePngGuid = 0x5bd31a7c10e9f2c8ull;     // smoke.png（未切片整图）
constexpr uint64_t kYamiHeroSheetGuid = 0x5bd31a7c10000001ull; // Samples yami-dungeon hero_1（在场即验）
constexpr uint64_t kYamiHeroClipGuid = 0x5bd31a7c20000001ull;  // hero-walk.anim（9 帧 @8fps）

/// 程序化动画素材三件套（sheet png + grid meta + clip + clip meta）落 assetsDir。
/// smoke-anim（SeedSmokeProject）与 bench-survivor（万怪动画化）共用；须在
/// OpenProject/Rescan 前落盘（切片记账/导入随扫描走）。
void WriteAnimSheetAssets(const std::filesystem::path& assetsDir) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(assetsDir, ec);
    std::filesystem::path sheet = assetsDir / "anim-sheet.png";
    if (!fs::exists(sheet, ec)) {
        std::vector<uint8_t> px(128 * 32 * 4);
        for (int y = 0; y < 32; ++y)
            for (int x = 0; x < 128; ++x) {
                const int frame = x / 32;
                uint8_t* q = &px[((size_t)y * 128 + x) * 4];
                q[0] = (uint8_t)(60 * (frame + 1));
                q[1] = (uint8_t)(255 - 50 * frame);
                q[2] = 128;
                q[3] = 255;
            }
        stbi_write_png(sheet.string().c_str(), 128, 32, 4, px.data(), 128 * 4);
    }
    std::filesystem::path sheetMeta = sheet.string() + ".meta";
    if (!fs::exists(sheetMeta, ec)) {
        std::ofstream f(sheetMeta, std::ios::trunc);
        f << "{\n  \"guid\": \"5bd31a7c30000001\",\n  \"type\": \"sprite\",\n"
             "  \"importer\": { \"slice\": \"grid\", \"cell\": [32, 32], \"frames\": [4, 1] },\n"
             "  \"hash\": 0,\n  \"importedAt\": 0\n}\n";
    }
    std::filesystem::path clip = assetsDir / "anim.anim";
    if (!fs::exists(clip, ec)) {
        std::ofstream f(clip, std::ios::trunc);
        f << "{\n  \"schemaVersion\": 1,\n  \"name\": \"smoke-anim\",\n  \"fps\": 10,\n"
             "  \"loop\": true,\n  \"frames\": [\n"
             "    { \"sheet\": \"5bd31a7c30000001\", \"cell\": 0 },\n"
             "    { \"sheet\": \"5bd31a7c30000001\", \"cell\": 1 },\n"
             "    { \"sheet\": \"5bd31a7c30000001\", \"cell\": 2 },\n"
             "    { \"sheet\": \"5bd31a7c30000001\", \"cell\": 3 }\n  ]\n}\n";
    }
    std::filesystem::path clipMeta = clip.string() + ".meta";
    if (!fs::exists(clipMeta, ec)) {
        std::ofstream f(clipMeta, std::ios::trunc);
        f << "{\n  \"guid\": \"5bd31a7c30000002\",\n  \"type\": \"clip\",\n  \"hash\": 0,\n"
             "  \"importedAt\": 0\n}\n";
    }
    // M6a 批①：受击段（fps12 × 尾两帧 loop=0——Play(hit)+Queue(walk) 切段断言用）
    std::filesystem::path hitClip = assetsDir / "anim-hit.anim";
    if (!fs::exists(hitClip, ec)) {
        std::ofstream f(hitClip, std::ios::trunc);
        f << "{\n  \"schemaVersion\": 1,\n  \"name\": \"smoke-anim-hit\",\n  \"fps\": 12,\n"
             "  \"loop\": false,\n  \"frames\": [\n"
             "    { \"sheet\": \"5bd31a7c30000001\", \"cell\": 3 },\n"
             "    { \"sheet\": \"5bd31a7c30000001\", \"cell\": 2 }\n  ]\n}\n";
    }
    std::filesystem::path hitClipMeta = hitClip.string() + ".meta";
    if (!fs::exists(hitClipMeta, ec)) {
        std::ofstream f(hitClipMeta, std::ios::trunc);
        f << "{\n  \"guid\": \"5bd31a7c30000003\",\n  \"type\": \"clip\",\n  \"hash\": 0,\n"
             "  \"importedAt\": 0\n}\n";
    }
}

// ---- M5 清障③：bench-survivor 压测场景播种（08 §3：编辑器内 1 万怪 ≥45fps）----
// 临时项目 + 程序化播种（怪 prefab 走清障② SpawnFn 桥；Spawner capAlive 顶格 =
// "导演拉满"）。恒用 tempdir：MakePrefabFrom 会往项目写 Prefabs/——不污染用户工程。
bool SeedBenchSurvivorScene(EditorContext& ctx) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path root = fs::temp_directory_path() /
                          ("lemon-bench-survivor-" + std::to_string(::getpid()));
    fs::remove_all(root, ec);
    // M5 批③动画化：程序化 4 帧表 + clip 先落盘（OpenProject 扫描即切片记账/导入）
    // ——万怪 Animator2D 帧映射进压测口径（Animator 系统成本进 09 §6.10 台账）
    WriteAnimSheetAssets(root / "Assets");
    if (!ctx.Assets().OpenProject(root.string(), 100)) return false;

    ecs::Scene& s = ctx.EditScene();
    // 怪模板 → prefab（源保留在场景：多 1 只白送的怪，无碍计量）
    ecs::Entity mob = ctx.CreateSpriteEntity("BenchMob", 4);
    s.Emplace<ecs::Health>(mob, ecs::Health{.max = 30.0f, .cur = 30.0f});
    s.Emplace<ecs::Knockback>(mob);
    s.Emplace<ecs::Velocity>(mob);
    // Hazard 化（2026-09-24 方案 A 批）：对齐 vs-survivor 模板 Mob.prefab（dps 8/
    // radius 24/tick 0.8）——Perf10k 压测（09 §6.10 注记）证实的"万怪密团 Hazard
    // 查询"真实工作形状进回归口径
    s.Emplace<ecs::Hazard>(mob, ecs::Hazard{.dps = 8.0f, .tickInterval = 0.8f,
                                            .radius = 24.0f});
    // 动画（M5 批③）：clipId = anim.anim GUID 低 32 位（EnterPlay 建表；fps10×4 帧）
    s.Emplace<ecs::Animator2D>(mob).clipId = (uint32_t)kAnimClipGuid;
    ecs::Chase& ch = s.Emplace<ecs::Chase>(mob);
    ch.speed = 70.0f;
    ch.aggroRange = 2000.0f;
    ch.keepRange = 24.0f;
    ch.targetTeam = 0;
    const uint64_t pguid = ctx.MakePrefabFrom(mob);
    if (pguid == 0) return false;
    // 玩家（Chase 目标；team 0）。HP 1e6：Hazard 化口径下防玩家死亡扰计量
    // （Perf10k 同款——接触环 Hazard 实伤害照付，玩家局内不死）
    ecs::Entity player = ctx.CreateSpriteEntity("BenchPlayer", 3);
    s.Get<ecs::Meta>(player).team = 0;
    s.Emplace<ecs::Health>(player, ecs::Health{.max = 1'000'000.0f, .cur = 1'000'000.0f});
    // 弹体模板 → prefab（M5 批⓪ 战斗化：万怪场真实战斗闭环进 destroyed 计量）。
    // 口径与 bench-sim 对齐：dmg 12 vs hp 30 → 3 击；pierce 0 = 命中即毁。
    // 模板弹挪出战场（出生环 ≤600 内不经过 (800,0)），寿命 2.5s 在 4s 预热内回收。
    ecs::Entity bullet = ctx.CreateSpriteEntity("BenchBullet", 4);
    s.Get<ecs::Meta>(bullet).team = 0;
    s.Emplace<ecs::Velocity>(bullet);
    ecs::Projectile& bp = s.Emplace<ecs::Projectile>(bullet);
    bp.damage = 12.0f;
    bp.speed = 320.0f;
    bp.lifetime = 2.5f;
    s.Get<ecs::Transform2D>(bullet).pos = {800.0f, 0.0f};
    const uint64_t bguid = ctx.MakePrefabFrom(bullet);
    if (bguid == 0) return false;
    // 玩家射手：20 发/s 朝最近怪（命中/击退/击杀/补怪全链路在编辑器 Play 内跑通）
    ecs::Shooter& psh = s.Emplace<ecs::Shooter>(player);
    psh.projectileId = (uint32_t)bguid;
    psh.interval = 0.05f;
    psh.range = 2000.0f;
    psh.targetTeam = 1;
    // 成长闭环（M5 批① 成长化）：玩家 = 收集者（XpProgress 约定）+ 磁力（段 A 触程）
    s.Emplace<ecs::XpProgress>(player);
    s.Emplace<ecs::Stats>(player).pickupRadius = 96.0f;
    // 宝石模板 → prefab（中立队 2：无 Health 不参战、无 Velocity 不入分离/移动）。
    // 模板挪出战场（(0,600) 在出生环 ≤600 边缘外圈、玩家 96 触程外，不进计量）
    ecs::Entity gem = ctx.CreateSpriteEntity("BenchGem", 4);
    s.Get<ecs::Meta>(gem).team = 2;
    ecs::Collectible& gc = s.Emplace<ecs::Collectible>(gem);
    gc.kind = 0;
    gc.value = 1.0f;
    s.Get<ecs::Transform2D>(gem).pos = {0.0f, 600.0f};
    const uint64_t gguid = ctx.MakePrefabFrom(gem);
    if (gguid == 0) return false;
    // 玩家侧宝石 Spawner：40 颗/s 撒 400px 盘 → 磁吸/拾取/XP/LevelUp 全链路进基线
    // 口径（地面稳态存量随局累积、capAlive 2000 封顶——PickupSystem 满载观测项）
    ecs::Spawner& gsp = s.Emplace<ecs::Spawner>(player);
    gsp.prefabId = (uint32_t)gguid;
    gsp.interval = 0.2f;
    gsp.burst = 8;
    gsp.maxAlive = 2000;
    gsp.spawnTeam = 2;
    gsp.range = 400.0f;
    // Spawner：interval 0 = 每帧开闸、burst 64 → ~125 帧涨满 8000（M5 批② 导演化：
    // 闸门 10000→8000，让 2000 头寸给 BenchDirector 波次——导演通道进压测口径）
    ecs::Entity spawner = ctx.CreateEntity("BenchSpawner");
    ecs::Spawner& sp = s.Emplace<ecs::Spawner>(spawner);
    sp.prefabId = (uint32_t)pguid; // 低 32 位（M5 清障②映射约定）
    sp.interval = 0.0f;
    sp.burst = 64;
    sp.maxAlive = 8000;
    sp.spawnTeam = 1;
    sp.range = 600.0f;
    sp.cooldown = 0.0f;
    // 导演（M5 批②）：3 波 × 4 条目（2×60/s + 2×30/s = 180/s/波），1s 起波每 5s
    // 一波。注意波重叠 = 后波接管（前波条目废止）→ 计划容量按"仅末波满速"算：
    // 15s（900 帧）窗口 ≈ 2520 出生 − 战斗击杀 ≈ 2200 净增，teamAlive 顶到
    // capAlive 10000——alive>8000 即导演通道实证（波表数据驱动，Inspector 可编辑）
    ecs::Entity director = ctx.CreateEntity("BenchDirector");
    s.Get<ecs::Transform2D>(director).pos = Vec2{0.0f, 0.0f};
    ecs::WaveDirector& wd = s.Emplace<ecs::WaveDirector>(director);
    wd.spawnTeam = 1;
    wd.capAlive = 10000;
    wd.waveCount = 3;
    for (int w = 0; w < 3; ++w) {
        ecs::WaveDef& def = wd.waves[w];
        def.startTime = 1.0f + 5.0f * (float)w;
        def.rampMult = 1.0f;
        def.entryCount = 4;
        def.entries[0] = ecs::WaveEntry{.prefabId = (uint32_t)pguid, .count = 999,
                                        .interval = 1.0f / 60.0f, .range = 550.0f};
        def.entries[1] = ecs::WaveEntry{.prefabId = (uint32_t)pguid, .count = 999,
                                        .interval = 1.0f / 60.0f, .range = 400.0f};
        def.entries[2] = ecs::WaveEntry{.prefabId = (uint32_t)pguid, .count = 999,
                                        .interval = 1.0f / 30.0f, .range = 300.0f};
        def.entries[3] = ecs::WaveEntry{.prefabId = (uint32_t)pguid, .count = 999,
                                        .interval = 1.0f / 30.0f, .range = 200.0f};
    }
    LEMON_LOG("bench-survivor 播种：prefab guid %016llx（低 32 位 %08x）",
              (unsigned long long)pguid, sp.prefabId);
    return true;
}

// ImGui 错误汇（1.92 内部回调口；DockBuilder 同源引用 imgui_internal）：ID 冲突/
// 空标签等程序员错误在这里现形——冒烟断言清零（M4.5 修复 Inspector ##v 撞号后
// 加的程序化防线：这类错只在交互时弹窗，无头冒烟原本测不到）。
int g_imguiErrorCount = 0;
// M5 批④ --smoke-template 证据计数（事件 sink + 帧循环采样写入；verdict 汇总）
int g_tplWaveStarts = 0, g_tplLevelUps = 0, g_tplDeaths = 0;
int g_tplGems = 0, g_tplMobs = 0; // 峰值快照（帧内采样）
// 批③d 前置 T5 → 批③d-1 随迁：模板场景现挂 2 UIDocument（HUD/cards）——零装载
// 护栏升级为"通道 A 装载恰 2"（装载点单一性防线的同型收紧；bench 场景仍零装载）
int g_tplUiLoads = -1;
// 批③d-1：文档化断言态（原 RtUi 行/卡片探针随迁）+ 层序三拍状态机
bool g_tplHudDocOk = false;
int g_tplLayerStage = 0; // 0 基线请求→1 取回→2 等卡片→3 取回→4 等隐藏→5 取回→6 完
int g_tplHudPixN0 = -1, g_tplHudPixDuring = -1, g_tplHudPixAfter = -1;
bool g_tplCapReq = false, g_tplCapPending = false;
int g_tplCapPix = -1;
int g_tplClickPhase = 0, g_tplClickCooldown = 0; // 直灌点击三帧（定位/down/up）+ 冷却
bool g_tplPointerHold = false; // 指针保持窗（FeedGameUiInput 让位——Update 建悬停用）
float g_tplClickX = 0, g_tplClickY = 0;
char g_tplHudRows[64] = "";
bool g_tplBestLoaded = false, g_tplWaveRow = false; // g_tplHudOk → g_tplHudDocOk（批③d-1 随迁）
// T8 后修：进度条断言（文本探针测不出"样式写了布局没生效"——bar-fill 曾因
// RmlUi 默认 inline 宽高被忽略而恒 0，文本六行全绿）。后修② 条改原生 progress：
// 断言 = 轨道盒（120dp×10dp×ratio）+ value 属性回读对文本行数值
bool g_tplHudBarBox = false;
// M6a 批①：受击切段链（怪 clipId 曾 = monster-hit 段）+ fx 通道（飘字/血条在场）
bool g_tplMobHitClip = false, g_tplFxText = false, g_tplFxBar = false;
bool g_tplCardsSeen = false, g_tplPicked = false, g_tplCardsHidden = false;
bool g_tplDeathSeen = false, g_tplRevived = false, g_tplScriptOk = true; // 批④后修④死亡链
bool g_tplDeathArmed = false; // 压血一shot（站桩下自动炮火清怪快于刷怪，磨不死）
// M6a 批② T4：数值表载入断言（weapons 4 行 × upgrades 7 行 × balance 2 行——
// 含列头行；PlayerCombat.Start 读、EnterPlay 快照建 TableStore）
bool g_tplTablesOk = false;
void ImGuiErrorSink(ImGuiContext*, void* user_data, const char* msg) {
    ++*static_cast<int*>(user_data);
    LEMON_WARN("ImGui 错误：%s", msg);
}

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
    if (launch.finalTest) {
        // 终验第一步：向导建项目（blank 模板；目录必须不存在 → --project 传父目录，
        // 项目名固定 lemon-final，保证可重复跑）
#ifndef LEMON_SCRIPT_DIR
        LEMON_ERROR("终验需要 LEMON_BUILD_SCRIPTING=ON 构建");
        return 1;
#else
        namespace fs = std::filesystem;
        std::error_code ec;
        fs::path p(launch.projectDir.empty() ? "/tmp/lemon-m45" : launch.projectDir);
        fs::remove_all(p / "lemon-final", ec); // 幂等：清上次终验残留
        ProjectDesc desc;
        desc.parentDir = p.string();
        desc.name = "lemon-final";
        desc.sdkDir = LEMON_SCRIPT_DIR;
        desc.engineVersion = "0.4.0-m4";
        const std::string root = ProjectWizard::Create(desc, &wizardSpawnGuid_);
        if (root.empty()) {
            LEMON_ERROR("终验失败：项目向导创建失败");
            return 1;
        }
        launchCopy_.projectDir = root;
        launch_ = &launchCopy_;
        LEMON_LOG("final: 向导建项目 OK %s", root.c_str());
#endif
    }
    // --smoke-template（M5 批④）：向导复制 vs-survivor 到 tempdir（幂等清残留）→
    // 走标准 OpenProjectPipeline（Game/ 编译 + 脚本宿主 + watcher）→ 开 Main.scene
    if (launch.smokeTemplate) {
#ifndef LEMON_SCRIPT_DIR
        LEMON_ERROR("--smoke-template 需要 LEMON_BUILD_SCRIPTING=ON 构建");
        return 1;
#else
        namespace fs = std::filesystem;
        std::error_code ec;
        const fs::path tmp = fs::temp_directory_path() /
                             ("lemon-smoke-template-" + std::to_string(::getpid()));
        fs::remove_all(tmp, ec);
        ProjectDesc d;
        d.parentDir = tmp.string();
        d.name = "VsSmoke";
        d.sdkDir = LEMON_SCRIPT_DIR;
        d.engineVersion = "0.5.0-m5";
        d.templateName = "vs-survivor";
        d.templateDir = std::string(LEMON_TEMPLATE_DIR) + "/vs-survivor";
        const std::string root = ProjectWizard::Create(d);
        if (root.empty()) {
            LEMON_ERROR("smoke-template：向导复制失败（模板缺失/不可写）");
            return 1;
        }
        launchCopy_.projectDir = root;
        launch_ = &launchCopy_;
        LEMON_LOG("smoke-template: 向导复制 OK %s", root.c_str());
#endif
    }
    // 批③b：--smoke-uirml 资产夹具（temp 项目：.rml + .rcss + 贴图，标准管线打开）
    // ——文档/样式/贴图/热重载四通道全走真实资产路径（③a 的内存文档退役）
    if (launch.smokeUirml) SeedSmokeUiRmlProject();
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
        if (!ctx_.OpenScene(launch_->projectDir + "/Scenes/Main.scene")) return 1;
        SeedJudgementScene(wizardSpawnGuid_);
        smokeSeeded_ = ctx_.ActiveScene().AliveCount();
    } else if (launch.smokeTemplate) {
        // M5 批④：模板链冒烟——Main.scene（玩家/导演已由生成器播种）；进 Play 前
        // 预置存档 = EnterPlay 载入路径的机械验证。M6a 批② T5 双载体：meta.sav
        // （vs.best=123，新名——模板 Chan.Meta 读点）+ game.sav（旧名——slot 档
        // 惰性迁移链载体；出 Play 断言迁移落新名）
        if (!ctx_.OpenScene(launch_->projectDir + "/Scenes/Main.scene")) return 1;
        {
            namespace fs = std::filesystem;
            std::error_code ec;
            const fs::path saves = fs::path(launch_->projectDir) / ".lemon/saves";
            fs::create_directories(saves, ec);
            lemon::ecs::SaveChannel pre;
            pre.Set("vs.best", "123", 3);
            const std::vector<uint8_t> bytes = pre.Encode();
            std::ofstream(saves / "meta.sav", std::ios::binary | std::ios::trunc)
                .write((const char*)bytes.data(), (std::streamsize)bytes.size());
            std::ofstream(saves / "game.sav", std::ios::binary | std::ios::trunc)
                .write((const char*)bytes.data(), (std::streamsize)bytes.size());
        }
        smokeSeeded_ = ctx_.ActiveScene().AliveCount();
        forceDefaultLayout_ = true; // overlay 断言依赖 Scene 面板前台（同 smoke-drag 语义）
        // overlay 三要素需要选中实体：选玩家（tag "Player"）
        ctx_.EditScene().Each([&](ecs::Entity e) {
            if (const ecs::Meta* m = ctx_.EditScene().TryGet<ecs::Meta>(e);
                m && std::strcmp(m->tag, "Player") == 0)
                ctx_.Select(e, false);
        });
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

    // M6a 批② T3：--smoke-anim 扩 clip 编辑链（验收②）。专档 anim-edit.anim
    //（固定 guid，幂等重写基线）走 AnimationPanel 数据面同款链路：落盘 →
    // ParseClipJson → 改 fps/增帧 → ClipToJson → 原子写 → Rescan → 回读 roundtrip。
    // 不动 anim.anim 种子（既有帧映射/切段断言零改动）。
    bool smokeClipEditOk = false, smokeClipPlayCacheOk = false;
    bool smokeWholeOk = false;
    bool smokeSetEditOk = false, smokeSetPlayCacheOk = false;
    bool smokeGraphEditOk = false, smokeGraphCacheOk = false, smokeGraphSwitchOk = false;
    bool smokeSetOpenOk = false; // 2026-09-27：.override 直开集修复回归位
    bool smokeSetCreateOk = false; // 2026-09-27 热修②：新建入口清残留编辑态回归位
    bool smokeSetCreateOpenOk = false; // 热修③：模态真实点击创建 → OpenSet 全链
    bool smokeSetCreateFlowDone = false;
    bool smokeSetModalQueuedPrev = false; // 诊断：模态 queued 探针的帧间沿
    bool smokePickClickOk = false, smokePickShiftOk = false, smokePickAllOk = false;
    bool smokePickFlowDone = false;
    bool smokeSheetAllOk = false, smokeSheetClearOk = false, smokeSheetCloseOk = false,
         smokeSheetRowOk = false;
    bool smokeSheetFlowDone = false; // 三图优化点③：选帧对话框计数/清空/关窗
    bool smokeAddCountOk = false, smokeAddOrderOk = false, smokeAddFlowDone = false;
    size_t smokeAddBase = 0; // 帧序事故回归：多图追加前编辑态帧数基数
    bool smokeFolderCreateOk = false, smokeDblClipOk = false; // T3-UX7：极简创建/断路
    bool smokeDragItemOk = false; // T3-UX9：帧格交互件回归位（项 ID 非零）
    bool smokeLeftColOk = false, smokeLeftColDone = false; // 三图优化点①：左列段行元信息
    // 批③d 前置 T5：UIDocument 双通道/层序/stale 断言位（钩子写、Run 尾裁决读）
    int smokeUiLayerBTopN = -1, smokeUiLayerATopN = -1; // 帧中点两捕获的 #802040 计数
    bool smokeUiStaleOk = false, smokeUiKeepCOk = false; // 第二局动态屏 Hide / Edit 双击保持
    uint32_t smokeUiLoadsP2 = 0; // 第二局装载增量（恰 1 = 只重装声明文档）
    bool smokeUiExitP2 = false; // Play→Stop→Play 往返完成
    bool smokeUiHasDocB = false; // 220 帧前 dyn 存量（删除播种后终帧恒 false，不能终帧读）
    bool smokeUiDelEvictOk = false; // 真人验收②回灌：删 .rml → 已装载文档逐出
    char smokeUiDynText[64] = {}; // 220 帧前 dyntitle 快照（同上——逐出后终帧读恒空）
    // 真人验收②二轮回灌：僵尸渲染双防线（簿记清 ≠ 画面清）。形态 A = Play 中删
    // 正在显示的文档（editprev，212 删 → 253 活画面像素须清零）；形态 B = 编辑态
    // 删（302 双击重装载 → 304 删 → 345 重进 Play 须无残留）。seed = 删前确在显示
    bool smokeUiEvictSeedOk = false;
    int smokeUiZombiePixN = -1, smokeUiEvictPixN = -1; // 253 / 348 帧 #604080 计数
    // 形态 D（2026-09-29 用户实报收口）：删场景 UIDocument 实体 → 重进 Play 不
    // 得残留显示。stopHide = 405 Stop 后 Scene 文档即被清场（HideNonEditDocuments）
    // 且 Edit 预览豁免；delEnt = 410 第四局（无脚本模式全量断言——脚本模式 C# 帧 1
    // 会经通道 B 合法拉回，非僵尸）：uirml 装载保留 + 不显示 + 装载增量 0；pix =
    // 411 帧渲染块 #802040 计数（<5）。420 重建实体进终局，终帧画面回归原口径
    bool smokeUiExitHideOk = false, smokeUiDelEntOk = false;
    int smokeUiDelEntPixN = -1;
    uint32_t smokeUiLoadsP4Base = 0; // 405 Stop 时装载计数（第四局增量断言基线）
    // 形态 D 二段：resetSeed = 421 直调 ExitPlay（绕过清场）后 uirml 仍 shown
    //（防线一确不在场）；resetOnly = 424 第六局 Reset origin 判据独自完成清场
    bool smokeUiResetSeedOk = false, smokeUiResetOnlyOk = false;
    int smokeUiNegP2 = -1; // 251 帧负面行容器快照（终帧在三局——通道 A 重装已清）
    bool smokeUiEvictEditOk = false; // 三局入 Play 后 editprev 已不在 docs
    Vec2 smokeCreatePt{-1.0e9f, -1.0e9f};
    Vec2 smokePickPt{-1.0e9f, -1.0e9f};
    Vec2 smokeSheetPt{-1.0e9f, -1.0e9f};
    if (launch.smokeAnim) {
        const std::filesystem::path clipPath =
            std::filesystem::path(ctx_.Assets().ProjectRoot()) / "Assets" / "anim-edit.anim";
        const std::filesystem::path setPath =
            std::filesystem::path(ctx_.Assets().ProjectRoot()) / "Assets" / "anim-set.override";
        {
            std::ofstream f(clipPath, std::ios::trunc);
            f << "{\n  \"schemaVersion\": 1,\n  \"name\": \"smoke-anim-edit\",\n  \"fps\": 10,\n"
                 "  \"loop\": true,\n  \"frames\": [\n"
                 "    {\n      \"sheet\": \"5bd31a7c30000001\",\n      \"cell\": 0\n    }\n"
                 "  ]\n}\n";
            std::ofstream m(clipPath.string() + ".meta", std::ios::trunc);
            m << "{\n  \"guid\": \"5bd31a7c30000004\",\n  \"type\": \"clip\",\n  \"hash\": 0,\n"
                 "  \"importedAt\": 0\n}\n";
            // T3b-1 整图引用：未切片 smoke.png（cell 0）单帧——BuildPlayClipCache
            // 应解析为本体号（文件夹多单图动画的运行时前提）
            const std::filesystem::path wholePath =
                std::filesystem::path(ctx_.Assets().ProjectRoot()) / "Assets" /
                "anim-whole.anim";
            std::ofstream wf(wholePath, std::ios::trunc);
            wf << "{\n  \"schemaVersion\": 1,\n  \"name\": \"smoke-anim-whole\",\n  \"fps\": 2,\n"
                  "  \"loop\": true,\n  \"frames\": [\n"
                  "    {\n      \"sheet\": \"5bd31a7c10e9f2c8\",\n      \"cell\": 0\n    }\n"
                  "  ]\n}\n";
            std::ofstream wm(wholePath.string() + ".meta", std::ios::trunc);
            wm << "{\n  \"guid\": \"5bd31a7c30000005\",\n  \"type\": \"clip\",\n  \"hash\": 0,\n"
                  "  \"importedAt\": 0\n}\n";
            // T3c 动画集：段引用上述三段 clip（walk/hit/edit/whole）——集工作台 +
            // BuildPlayClipCache 集登记 + FindByName 反查的数据面
            std::ofstream sf(setPath, std::ios::trunc);
            sf << "{\n  \"schemaVersion\": 1,\n  \"name\": \"smoke-anim-set\",\n"
                  "  \"segments\": [\n"
                  "    {\n      \"name\": \"walk\",\n      \"clip\": \"5bd31a7c30000002\"\n    },\n"
                  "    {\n      \"name\": \"hit\",\n      \"clip\": \"5bd31a7c30000003\"\n    },\n"
                  "    {\n      \"name\": \"edit\",\n      \"clip\": \"5bd31a7c30000004\"\n    },\n"
                  "    {\n      \"name\": \"whole\",\n      \"clip\": \"5bd31a7c30000005\"\n    }\n"
                  "  ]\n}\n";
            std::ofstream sm(setPath.string() + ".meta", std::ios::trunc);
            sm << "{\n  \"guid\": \"5bd31a7c30000006\",\n  \"type\": \"animset\",\n  \"hash\": 0,\n"
                  "  \"importedAt\": 0\n}\n";
            // T3d：状态机档——状态名对齐集段名（whole/walk/hit）；whole 稳态绕开
            // 队列钩子（其按 clipId==anim.anim 认实体，graph 链实体全程不落 walk 稳态）
            const std::filesystem::path graphPath =
                std::filesystem::path(ctx_.Assets().ProjectRoot()) / "Assets" /
                "anim-graph.controller";
            std::ofstream gf(graphPath, std::ios::trunc);
            gf << "{\n  \"schemaVersion\": 1,\n  \"name\": \"smoke-anim-graph\",\n"
                  "  \"params\": [\n"
                  "    { \"name\": \"speed\", \"kind\": \"float\" },\n"
                  "    { \"name\": \"atk\", \"kind\": \"trigger\" }\n  ],\n"
                  "  \"entry\": \"whole\",\n  \"states\": [\"whole\", \"walk\", \"hit\"],\n"
                  "  \"transitions\": [\n"
                  "    { \"from\": \"whole\", \"to\": \"walk\", \"when\": [{ \"param\": \"speed\", \">\": 0.1 }] },\n"
                  "    { \"from\": \"walk\", \"to\": \"hit\", \"when\": [{ \"trigger\": \"atk\" }] },\n"
                  "    { \"from\": \"hit\", \"to\": \"whole\", \"on\": \"exitTime\" }\n  ]\n}\n";
            std::ofstream gm(graphPath.string() + ".meta", std::ios::trunc);
            gm << "{\n  \"guid\": \"5bd31a7c30000007\",\n  \"type\": \"controller\",\n  \"hash\": 0,\n"
                  "  \"importedAt\": 0\n}\n";
            // T3-UX4：选择器多选断言原料——9 张 2×2 PNG（pick0..pick8，文件名序 =
            // tile 序；加 smoke.png 共 10 张图 → 注入点击 1 / Shift 范围 8 / Ctrl+A 10）
            {
                const uint8_t px[16] = {0xC0, 0x40, 0x40, 0xFF, 0x40, 0xC0, 0x40, 0xFF,
                                        0x40, 0x40, 0xC0, 0xFF, 0x80, 0x80, 0x20, 0xFF};
                for (int k = 0; k < 9; ++k) {
                    const std::filesystem::path pp =
                        std::filesystem::path(ctx_.Assets().ProjectRoot()) / "Assets" /
                        ("pick" + std::to_string(k) + ".png");
                    stbi_write_png(pp.string().c_str(), 2, 2, 4, px, 8);
                }
            }
        }
        RescanAssets();
        std::ifstream rf(clipPath, std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(rf)), std::istreambuf_iterator<char>());
        if (ClipData c = ParseClipJson(text); c.ok) {
            c.fps = 13.0f;                        // 改帧率（面板 DragInt 同字段）
            c.frames.push_back({kAnimSheetGuid, 3}); // 增帧（承接 sheet）
            if (WriteFileAtomic(clipPath.string(), ClipToJson(c) + "\n")) {
                RescanAssets(); // 保存 = 原子写 + 主动重扫（面板同款）
                std::ifstream vf(clipPath, std::ios::binary);
                std::string vtext((std::istreambuf_iterator<char>(vf)),
                                  std::istreambuf_iterator<char>());
                const ClipData v = ParseClipJson(vtext);
                smokeClipEditOk = v.ok && v.fps == 13.0f && v.frames.size() == 2 &&
                                  v.frames[1].cell == 3 && v.frames[1].sheetGuid == kAnimSheetGuid;
            }
        }
        if (!smokeClipEditOk)
            LEMON_ERROR("smoke-anim：clip 编辑链失败（anim-edit.anim roundtrip 不符）");
        { // T3c：集档 roundtrip（数据面同款链路）
            std::ifstream vf(setPath, std::ios::binary);
            std::string vtext((std::istreambuf_iterator<char>(vf)),
                              std::istreambuf_iterator<char>());
            const AnimSetData v = ParseAnimSetJson(vtext);
            smokeSetEditOk = v.ok && v.name == "smoke-anim-set" && v.segments.size() == 4 &&
                             v.segments[0].name == "walk" &&
                             v.segments[0].clipGuid == kAnimClipGuid;
            if (!smokeSetEditOk)
                LEMON_ERROR("smoke-anim：动画集解析失败（anim-set.override roundtrip 不符）");
        }
        { // T3d：controller 档 roundtrip（ControllerEdit 数据面同款链路）
            const std::filesystem::path graphPath =
                std::filesystem::path(ctx_.Assets().ProjectRoot()) / "Assets" /
                "anim-graph.controller";
            std::ifstream vf(graphPath, std::ios::binary);
            std::string vtext((std::istreambuf_iterator<char>(vf)),
                              std::istreambuf_iterator<char>());
            const ControllerData v = ParseControllerJson(vtext);
            smokeGraphEditOk = v.ok && v.name == "smoke-anim-graph" &&
                               v.states.size() == 3 && v.params.size() == 2 &&
                               v.transitions.size() == 3 && v.transitions[2].exitTime;
            if (!smokeGraphEditOk)
                LEMON_ERROR("smoke-anim：状态机解析失败（anim-graph.controller 不符）");
        }
        // 2026-09-27 修复回归：双击 .override（AnimSet 直开分支）必须置 setGuid_
        //（曾误走 SetTarget 只设 targetGuid_ → 空态/历史集——真人实测报告）。
        // 必须先于下方的 clip 归并调用：归并路径本身会置 setGuid_ 同值，后置
        // 断言会假通过
        OpenAnimationEditor(kAnimSetGuid);
        if (PanelRegistry::Entry* en = panels_.FindEntry("Animation"))
            smokeSetOpenOk =
                static_cast<AnimationPanel*>(en->panel)->SetGuidForTest() == kAnimSetGuid;
        if (!smokeSetOpenOk)
            LEMON_ERROR("smoke-anim：双击 .override 未开集（setGuid_ 未置位）");
        // 2026-09-27 热修②回归：右键"新建动画集…"必须清残留编辑态（弹窗背后曾
        // 照渲染上一个集——真人实测报告）。须在下方归并重开之前断言：归并路径
        // 会重置 setGuid_，后置断言会假通过（与 open 同理）；归并照旧保住面板
        // 本体 OnGui 覆盖（LoadFrom 重入）。模态不点创建 = 零落盘副作用。
        // 冒烟自清（热修④续）：目标名 player = 冒烟专属产物——工程里已有同名集
        //（用户真人验收建过）会让模态创建撞路拒 → 集不开 → 后续 FilePicker/
        // 选帧/建 clip 模态链全被卡（2026-09-27 真人报告"player.override 已存在"）
        {
            namespace fs = std::filesystem;
            std::error_code ec;
            const fs::path root = fs::path(ctx_.Assets().ProjectRoot());
            const bool removed =
                fs::remove(root / "Assets/player.override", ec) ||
                fs::remove(root / "Assets/player.override.meta", ec);
            if (removed) RescanAssets(); // DB 不留幻影（墓碑复活留给模态创建路径）
        }
        OpenAnimationCreateSet("");
        if (PanelRegistry::Entry* en = panels_.FindEntry("Animation"))
            smokeSetCreateOk =
                static_cast<AnimationPanel*>(en->panel)->SetGuidForTest() == 0;
        if (!smokeSetCreateOk)
            LEMON_ERROR("smoke-anim：新建动画集入口未清残留集（setGuid_ 未归零）");
        // T3b：面板本体无头覆盖——开 Animation 面板每帧跑 OnGui（装载/胶片带/
        // 预览路径；ImGui 错误计数归零断言兜底）。选编辑过的 anim-edit（后续
        // 编辑写回 hash 变更 → 覆盖 LoadFrom 重入）
        OpenAnimationEditor(kAnimEditClipGuid);
    }

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
    }
    // 批③a（ADR-014）：--smoke-uirml 独立进 Play——playTest 的"进/出往返"语义与
    // smoke 门绑定（上方块），本模式只需"Play 中持续渲染 UI"一态（Stop 由循环后
    // --play 收尾块统一处理，见下方 ctx_.ExitPlay）
    if (launchCopy_.smokeUirml && !ctx_.Playing()) {
        if (PlayBlockedByScripts()) {
            LEMON_ERROR("smoke-uirml 已阻止进入 Play：Game/ 编译失败（脚本宿主未装配）");
            return 1;
        }
        // 批③c：--script 时挂 UI 全链探针（此处场景已定型——Init 期创建会被后续
        // 场景装配块 OpenScene/SeedSmoke 换掉，快照 0 实体实测教训）
        if (ctx_.Scripts()) {
            ecs::Entity probe = ctx_.ActiveScene().Create();
            ctx_.AttachScript(probe, 0, "UiProbeBehaviour");
            LEMON_LOG("uirml 播种：UiProbeBehaviour 挂载（实体 %llu）",
                      (unsigned long long)probe.id);
        }
        // 批③d 前置 T5：夹具场景声明主文档（通道 A 验收面）——UIDocument 进快照，
        // EnterPlay 声明式装载 + showOnStart 归位；dyn/editprev 不声明（通道 B /
        // Edit 双击装载两块独立断言面）
        {
            ecs::Entity ue = ctx_.CreateEntity("UIDocument");
            ecs::UIDocument& ud = ctx_.ActiveScene().Emplace<ecs::UIDocument>(ue);
            if (const AssetEntry* en = ctx_.Assets().FindByPath("Assets/UI/uirml.rml"))
                ud.sourceAssetGuid = en->guid;
            else
                LEMON_ERROR("uirml 夹具缺 Assets/UI/uirml.rml——UIDocument 播种失败");
        }
        if (!ctx_.EnterPlay()) return 1;
        MountSceneUiDocuments(); // 批③d 前置（通道 A）：夹具主文档声明装载
        // 批③b 补：Play 按钮/菜单路径都设的翻页标志——③a 独立进 Play 分支漏了它，
        // 中央区标签页停在 Scene，--screenshot（交换链）只见 Scene 不见 UI（用户
        // 走查 2026-09-28 报；gameRT 本身有 UI，像素断言不受影响）
        tabFocusPending_ = 1;
    }
    // M6a 批② T3：编辑后的 clip 进 Play 快照生效（BuildPlayClipCache 吃到 13fps×2 帧
    // ——验收②"保存 → 重进 Play 帧率/帧数生效"的程序化侧）。T3b-1：整图引用
    // 单帧档同断言（未切片 smoke.png cell0 → 本体号 1 帧）。
    if (launch.smokeAnim && ctx_.Playing()) {
        if (const ecs::ClipDef* cd =
                ctx_.ActiveWorld().Clips().Find((uint32_t)kAnimEditClipGuid))
            smokeClipPlayCacheOk = cd->fps == 13.0f && cd->frames.size() == 2;
        if (const ecs::ClipDef* wd =
                ctx_.ActiveWorld().Clips().Find((uint32_t)kAnimWholeClipGuid))
            smokeWholeOk = wd->frames.size() == 1;
        if (!smokeClipPlayCacheOk)
            LEMON_ERROR("smoke-anim：编辑档未进 Play clip 快照（anim-edit 13fps×2 帧）");
        if (!smokeWholeOk)
            LEMON_ERROR("smoke-anim：整图引用未进 Play clip 快照（anim-whole 1 帧）");
        // T3c：集登记进 Play 快照 + 按名/反查双口径（walk/hit 按名命中、whole 归集）
        smokeSetPlayCacheOk =
            ctx_.ActiveWorld().Clips().FindByName((uint32_t)kAnimSetGuid, "walk") ==
                (uint32_t)kAnimClipGuid &&
            ctx_.ActiveWorld().Clips().FindByName((uint32_t)kAnimSetGuid, "hit") ==
                (uint32_t)kAnimHitClipGuid &&
            ctx_.ActiveWorld().Clips().SetOfClip((uint32_t)kAnimWholeClipGuid) ==
                (uint32_t)kAnimSetGuid;
        if (!smokeSetPlayCacheOk)
            LEMON_ERROR("smoke-anim：动画集未进 Play 快照或按名反查不符（anim-set.override）");
        // T3d：状态机进 Play 快照 + 图驱动三段切换（whole→walk 条件边 → hit
        // trigger 边 → hit→whole exitTime 段末回归）。同步步进 Play 世界（Animator
        // → CSharpBatch → AnimGraph 全链；无脚本世界照常，图评估不依赖脚本）。
        // 独立实体 + whole 稳态 = 与队列钩子（按 clipId==anim.anim 认实体）零交集。
        if (const ecs::ControllerDef* cd =
                ctx_.ActiveWorld().Controllers().Find((uint32_t)kAnimGraphGuid)) {
            smokeGraphCacheOk = cd->states.size() == 3 && cd->transitions.size() == 3;
        }
        if (!smokeGraphCacheOk)
            LEMON_ERROR("smoke-anim：状态机未进 Play 快照（anim-graph.controller）");
        {
            ecs::World& w = ctx_.ActiveWorld();
            ecs::Scene& ps = ctx_.ActiveScene();
            ecs::Entity ge = ps.Create();
            ps.Emplace<ecs::Transform2D>(ge);
            ecs::Animator2D& ga = ps.Emplace<ecs::Animator2D>(ge); // clipId=0：图初始化进 entry
            (void)ga;
            ecs::AnimGraph& gg = ps.Emplace<ecs::AnimGraph>(ge);
            gg.controllerGuid = kAnimGraphGuid;
            gg.setGuid = kAnimSetGuid;
            ecs::AnimParams& gp = ps.Emplace<ecs::AnimParams>(ge);
            const float dt = 1.0f / 60.0f;
            w.Step(dt); // tick1：图初始化（参数播种 + entry=whole）
            gp.v[0] = 5.0f; // SetParam(speed)——测试直写（C# 侧同字段，script-tests 已覆盖）
            w.Step(dt);     // tick2：whole→walk 条件边
            const bool toWalk = ps.Get<ecs::Animator2D>(ge).clipId == (uint32_t)kAnimClipGuid;
            gp.v[0] = 0.0f; // speed 归零：exitTime 回 whole 后不再被 whole→walk 弹回
            gp.v[1] = 1.0f; // Trigger(atk)
            w.Step(dt);     // tick3：walk→hit trigger 边（消费即清）
            const bool toHit = ps.Get<ecs::Animator2D>(ge).clipId == (uint32_t)kAnimHitClipGuid;
            const bool trigCleared = gp.v[1] == 0.0f;
            for (int i = 0; i < 14; ++i) w.Step(dt); // hit（非 loop 2 帧 @12fps）收尾 → exitTime 回 whole
            smokeGraphSwitchOk =
                toWalk && toHit && trigCleared &&
                ps.Get<ecs::Animator2D>(ge).clipId == (uint32_t)kAnimWholeClipGuid;
            if (!smokeGraphSwitchOk)
                LEMON_ERROR("smoke-anim：图驱动切换不符（toWalk=%d toHit=%d trig=%d end=%08x）",
                            (int)toWalk, (int)toHit, (int)trigCleared,
                            ps.Get<ecs::Animator2D>(ge).clipId);
        }
    }
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
    if (launch.smokeTemplate && ctx_.Playing()) {
        // 批③d-1：通道 A 装载数（恰 2 = HUD+cards 场景声明；bench 场景零装载口径
        // 不变——装载点只在 MountSceneUiDocuments 的 EnterPlay 扫描）
        g_tplUiLoads = gameUi_ ? (int)gameUi_->DocumentLoadCount() : -1;
        ctx_.ActiveWorld().SetEventSink(
            [](ecs::World&, const ecs::EventPacket& p) {
                if (p.type == ecs::GameEvent::WaveStart) ++g_tplWaveStarts;
                else if (p.type == ecs::GameEvent::LevelUp) ++g_tplLevelUps;
                else if (p.type == ecs::GameEvent::Death) ++g_tplDeaths;
            });
    }

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
    // --smoke-drag 注入状态（M4.7c 交互回归）
    ecs::Entity dragTarget{};
    Vec2 dragBefore{0, 0};
    float dragDx = 0.0f, dragDy = 0.0f;
    bool dragPassed = false, dragDone = false;
    // 第二段（旋转）：弧点换算闭包 + 前后角
    float rotBefore = 0.0f, rotAfter = 0.0f;
    bool rotPassed = false, rotDone = false, rotReady = false;
    std::function<Vec2(float)> arcWorldToPt;
    // 第三段（Select 8 向 resize）：右边中点手柄外拖 → scale.x 增大 + 左缘锚定
    float scaleBefore = 0.0f, scaleAfter = 0.0f;
    float anchorLeft0 = 0.0f, anchorLeft1 = 0.0f;
    bool resizeDone = false, resizePassed = false, resizeReady = false, moveReady = false;
    Vec2 handleWorld{0, 0};   // 手柄世界点（注入帧换算屏幕）
    Vec2 moveWorld{0, 0};     // 移动段按下世界点
    Vec2 handleDragDir{1, 0}; // 手柄外拖方向 = 实体本地 +X 世界朝向
    // 第四段（缩放）：滚轮前推 = 放大 + 选中对象屏幕位置不动
    float zoomBefore = 0.0f, zoomAfter = 0.0f;
    Vec2 selScreen0{0, 0}, selScreen1{0, 0};
    bool zoomDone = false, zoomPassed = false;
    std::function<Vec2(Vec2)> worldToPt; // 世界→窗口点（cam 就绪后于帧 3 装配）
    // 第五段（甩飞防护 + F 聚焦）：远处实体不得拽走相机（锚点回退）；F 键找回
    Vec2 slingWorld{0, 0}, slingCenter0{0, 0};
    float slingDx = 0.0f, slingDy = 0.0f, focusDelta = 0.0f;
    bool slingDone = false, slingPassed = false, focusDone = false;
    // --smoke-ui（M4.7d 真人会话回归）：快捷键/点选/复制删除 Undo 往返/label-scrub/
    // 保存/Play/重命名/挂父子/目录导航/命名布局。断言旗标逐段置位，末帧总裁决。
    ecs::Entity uiTarget{}, uiGate{};
    uint32_t uiBaseCount = 0;
    uint32_t uiTargetSubtree = 1; // C8：Ctrl+D 子树化后的计数增量（frame5 实测）
    float uiRot0 = 0.0f, uiRot1 = 0.0f;
    Vec2 uiPt0{0, 0}, uiPt1{0, 0};
    bool uiAllOk = false, uiVerdictDone = false;
    bool toolsOk = false, selOk = false, dupOk = false, delOk = false, dupUndoOk = false,
         dupRedoOk = false, delRedoOk = false, scrubOk = false, scrubUndoOk = false,
         saveOk = false, playOk = false, stopOk = false, gselOk = false, renameOk = false,
         renameUndoOk = false, parentOk = false, parentUndoOk = false, folderOk = false,
         crumbOk = false, layoutOk = false, consoleOk = false;
    const double autosaveClock0 = ImGui::GetTime(); // steady 秒（TickAutosave 节拍源）
    // 终验冷启动口径 = 编辑器主循环首帧（向导建项目 + Game 首次编译是创建期工作，
    // 另由 final-wizard/编译日志计量——不混入 §6 #3 判定）
    const auto tColdStart = launch.finalTest ? std::chrono::steady_clock::now() : tStart;
    // --bench-survivor 帧时统计（M5 清障③）：全帧耗时含渲染提交与 present 等待——
    // Immediate 呈现下 = 真实负载；Play Step 分段计时同步累计（诊断细分）。预热
    // 240 帧剔除（Spawner ~156 帧涨满 1 万 + 稳态余量）。
    constexpr uint64_t kBenchWarmup = 240;
    double benchFrameSum = 0.0, benchFrameMax = 0.0;
    // M5 性能批：帧段分解（把缺口拆到环节）。段界 = 帧内打时间戳、帧末统一累计——
    // resize/acquire 失败走 continue 的帧整帧不参与（与全帧口径一致）
    double benchPumpSum = 0.0, benchSimSum = 0.0, benchGlueSum = 0.0, benchUiSum = 0.0,
           benchAcqSum = 0.0, benchSceneSum = 0.0, benchUiDrawSum = 0.0, benchPresentSum = 0.0;
    uint64_t benchFrameN = 0;
    // 性能批②：尖刺归因原料——每段 max（带帧号）+ 历史最坏帧的八段快照 + 尖刺帧
    // （>25ms）内的分段和（看尖刺集体偏向哪段）
    constexpr int kSegN = 8;
    double benchSegMax[kSegN] = {};
    uint64_t benchSegMaxF[kSegN] = {};
    double benchMaxSeg[kSegN] = {}; // frameMax 刷新时刻的八段值
    double benchSpikeSeg[kSegN] = {};
    uint64_t benchSpikeN = 0;
    bool benchSimProfileZeroed = false;
    std::vector<ecs::SystemProfile> benchPlayProfiles; // Stop 前捕获（Play 世界随 ExitPlay 析构）
    // M5 批③ smoke-anim 证据（帧循环内累积：单帧采样会踩回绕 0）
    uint16_t smokeAnimMax[2] = {0, 0};    // [0] 程序表 / [1] yami 表：见过的最大 curFrame
    bool smokeAnimSlice[2] = {false, false}; // sr.spriteId 曾落入对应切片连号区间
    // M6a 批①：切段链（frame 60 直写 Play(hit)+Queue(walk) → hit 段在场 → 收尾回 walk）
    // + fx 通道（frame 30 写飘字/血条 → 通道计数）；GameView 渲染不属断言依赖
    bool smokeQueueHit = false, smokeQueueBack = false, smokeFxSeen = false;
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
        if (launchCopy_.smokeUirml && !launchCopy_.projectDir.empty()) {
            namespace fs = std::filesystem;
            const fs::path uiDir = fs::path(launchCopy_.projectDir) / "Assets" / "UI";
            auto rewriteFile = [](const fs::path& p, const std::string& from, const std::string& to) {
                std::ifstream in(p, std::ios::binary);
                std::string t((std::istreambuf_iterator<char>(in)),
                              std::istreambuf_iterator<char>());
                const size_t at = t.find(from);
                if (at == std::string::npos) return false;
                t.replace(at, from.size(), to);
                std::ofstream out(p, std::ios::trunc);
                out << t;
                return true;
            };
            // 批③c：M7 合成点击全链（帧 60 按下/61 释放，经 ImGui 注入 →
            // FeedGameUiInput → RmlUi → UiEvent → 下一帧 #16 → C# 回执 → RtUi uiev）
            if ((frame == 60 || frame == 61) && gameUi_) {
                float cx, cy;
                if (gameUi_->TryGetItemCenter("Assets/UI/uirml.rml", "cards", "opt0", &cx,
                                              &cy)) {
                    const float sx = gvCanvasX_ + cx * gvCanvasW_ / (float)gvRtW_;
                    const float sy = gvCanvasY_ + cy * gvCanvasH_ / (float)gvRtH_;
                    ui_->SetInputOverride(sx, sy, frame == 60 ? 1 : 0);
                } else {
                    LEMON_ERROR("uirml-smoke: 点击注入失败——cards/opt0 未克隆（帧 %llu，"
                                "items=%d neg=%d pending=%u）",
                                (unsigned long long)frame,
                                gameUi_->ContainerItemCount("Assets/UI/uirml.rml", "cards"),
                                gameUi_->ContainerItemCount("Assets/UI/uirml.rml", "negbox"),
                                gameUi_->PendingEventCount());
                }
            }
            // 批③c：负面契约 op 直灌（原 frame 160；批③d 前置移 210 = 第二局内——
            // EnterPlay 通道 A 重装主文档会 InvalidateContainers，160 时点的负面容器
            // 活不到终帧）：field 'nope' 不在模板集（has: lab）→ 响亮失败恰 1 +
            // 负面容器克隆 1 行（不渲染）。行块用八进制转义（十六进制 \x 会贪婪
            // 连吃后续 hex 字母——C 词法坑）
            if (frame == 210 && gameUi_ && ctx_.Playing()) {
                static const char kNegArena[] =
                    "Assets/UI/uirml.rml\0"               // s0=0（19 字符 + NUL）
                    "negbox\0"                             // s1=20
                    "nrow\0"                               // s2=27
                    "\001b\001\000\004nope\001\000x";      // s3=32 起：1 行 b/{nope=x}
                const ::lemon::ui::UiOpC op = {
                    (uint8_t)::lemon::ui::UiOpType::SetItems, 0, 4, 0,
                    0, 20, 27, 32, 1, 0, 0};
                gameUi_->ApplyOps(&op, 1, kNegArena, (uint32_t)sizeof(kNegArena));
                LEMON_LOG("uirml-smoke: 负面契约 op 直灌（field 'nope'）");
            }
            // 批③d 前置 T5：通道 B 无脚本路径——dyn.rml 未装载，Show op 落空 →
            // resolver 现载（脚本会话由 C# UiRefill 的 UI.Show 走同一通道，本钩让位）
            if (frame == 20 && gameUi_ && !ctx_.Scripts()) {
                static const char kDynArena[] = "Assets/UI/dyn.rml"; // s0=0（NUL 终止）
                const ::lemon::ui::UiOpC op = {
                    (uint8_t)::lemon::ui::UiOpType::Show, 0, 1, 0,
                    0, 0, 0, 0, 0, 0, 0};
                gameUi_->ApplyOps(&op, 1, kDynArena, (uint32_t)sizeof(kDynArena));
                LEMON_LOG("uirml-smoke: 通道 B 直灌 Show（dyn.rml 落空兜底）");
            }
            // 批③d 前置 T5：层序断言两段中点捕获（D1 甲-轻量：层级序 = 最近 Show 序）。
            // 170/190 渲染块录 gameRT，171/191 钩子取回数 #802040：段① B 后 Show 在上
            // → 计数 > 500；175 显式重 Show A → 段② A 在上 → 计数 < 5
            if ((frame == 171 || frame == 191) && gameUi_) {
                std::vector<uint8_t> rt;
                uint32_t rw = 0, rh = 0;
                if (device_->DebugFetchTextureCapture(rt, rw, rh)) {
                    const int n = CountPixelsNear(rt, rw, rh, 128, 32, 64, 30); // #802040
                    if (frame == 171) smokeUiLayerBTopN = n;
                    else smokeUiLayerATopN = n;
                }
            }
            if (frame == 175 && gameUi_)
                gameUi_->ShowDocument("Assets/UI/uirml.rml", true); // 重 Show = 提层
            // 批③c：uiev 终值捕获（Play 中的 RtUi 槽；退 Play 后 play world 即毁，
            // 终帧 VERDICT 只能读快照）。批③d 前置：移至 199——200 起 Play→Stop→Play
            // 往返，第二局 RtUi 是新世界（无点击/无重载 → 槽恒空）
            if (frame == 199 && ctx_.Playing()) {
                const lemon::ecs::RtUiChannel& rt = ctx_.ActiveWorld().RtUi();
                for (uint32_t i = 0; i < rt.Count(); ++i)
                    if (std::strcmp(rt.At(i).key, "uiev") == 0)
                        std::snprintf(smokeUiEvText_, sizeof(smokeUiEvText_), "%s",
                                      rt.At(i).text);
            }
            // 批③d 前置 T5：Play→Stop→Play 往返（§3 归位机断言）。200 Stop（快照
            // 逐字节校验）；203 重进——装载增量恰 1（只重装声明文档，dyn 装载保留）；
            // dyn（上局 C# Show 过 = stale）被 Hide；editprev（Edit 双击装载，非
            // stale）保持可见；A 回声明态 shown。断言须在 TickPlay 前（C# 本局
            // UiRefill 的 Show 尚未到达）
            if (frame == 200 && ctx_.Playing()) {
                smokeUiExitP2 = StopPlay();
                if (!smokeUiExitP2) LEMON_ERROR("uirml-smoke: 第二局前 ExitPlay 失败");
            }
            // 上步 Stop 失败（不该发生）时跳过第二局断言——终帧 FAIL 兜底
            if (frame == 203 && !ctx_.Playing() && smokeUiExitP2) {
                const uint32_t loadsBefore =
                    gameUi_ ? gameUi_->DocumentLoadCount() : 0;
                if (TryEnterPlay()) {
                    tabFocusPending_ = 1;
                    smokeUiStaleOk =
                        gameUi_ && gameUi_->HasDocument("Assets/UI/dyn.rml") &&
                        !gameUi_->IsDocumentShown("Assets/UI/dyn.rml") &&
                        gameUi_->IsDocumentShown("Assets/UI/uirml.rml");
                    smokeUiKeepCOk =
                        gameUi_ && gameUi_->IsDocumentShown("Assets/UI/editprev.rml");
                    smokeUiLoadsP2 =
                        gameUi_ ? gameUi_->DocumentLoadCount() - loadsBefore : 0;
                }
            }
            // 批③d 前置 T5 补（真人验收②回灌）：删 .rml → 已装载文档逐出。212 删
            // dyn.rml 后**不直调重扫**——完全走 watcher（500ms 快照轮询 → ConsumeDirty
            // → RescanAssets removed 分支 UnloadDocument，= 真人删除路径）；252 断言
            // docs map 已无该文档。红字另证（本钩在 Play 中且场景未声明 dyn）
            if (frame == 212 && gameUi_) {
                smokeUiHasDocB = gameUi_->HasDocument("Assets/UI/dyn.rml");
                gameUi_->TryGetElementText("Assets/UI/dyn.rml", "dyntitle",
                                           smokeUiDynText, sizeof(smokeUiDynText));
                std::error_code ecd;
                std::filesystem::remove(uiDir / "dyn.rml", ecd);
                LEMON_LOG("uirml-smoke: dyn.rml 删除播种（等 watcher 驱动逐出）");
            }
            // 真人验收②二轮·形态 A：Play 中删**正在显示**的文档（editprev 非 stale
            // 保持可见 = 用户滞留形态）→ watcher 逐出 → 253 帧活画面上面板色必须清零
            // （僵尸渲染防线——docs 簿记清不等于 RmlUi 上下文真摘除）
            if (frame == 212 && gameUi_ && ctx_.Playing()) {
                smokeUiEvictSeedOk = gameUi_->IsDocumentShown("Assets/UI/editprev.rml");
                std::error_code ecd;
                std::filesystem::remove(uiDir / "editprev.rml", ecd);
                LEMON_LOG("uirml-smoke: editprev.rml Play 中删除播种（等 watcher 逐出）");
            }
            if (frame == 251 && gameUi_) // 负面行容器快照（210 负面 op 后；三局重装即清）
                smokeUiNegP2 = gameUi_->ContainerItemCount("Assets/UI/uirml.rml", "negbox");
            if (frame == 253 && gameUi_) { // 252 帧渲染块已录 gameRT（midUirmlCapture）
                std::vector<uint8_t> rt;
                uint32_t rw = 0, rh = 0;
                if (device_->DebugFetchTextureCapture(rt, rw, rh))
                    smokeUiZombiePixN =
                        CountPixelsNear(rt, rw, rh, 96, 64, 128, 30); // #604080
            }
            if (frame == 252 && gameUi_)
                smokeUiDelEvictOk = !gameUi_->HasDocument("Assets/UI/dyn.rml");
            // 真人验收②二轮·形态 B：编辑态删 → 重进 Play。256 Stop；258 复种 dyn
            // （三局 C# UiRefill 依赖，否则 resolver 落空记契约错）+ 复种 editprev；
            // 302 双击通道重装载（= ③b Edit 双击形态，等 watcher 复活墓碑后）→ 304
            // 编辑态删除 → watcher 逐出 → 345 重进 Play 断言无残留（簿记 + 348 像素）；
            // 352 复种 + 394 重装载 → 终帧 hasDocC/cPrevN 回归原口径
            if (frame == 256 && ctx_.Playing()) {
                if (!StopPlay()) LEMON_ERROR("uirml-smoke: 三局前 ExitPlay 失败");
            }
            if (frame == 258) {
                {
                    std::ofstream f(uiDir / "dyn.rml", std::ios::trunc);
                    f << "<rml>\n<head><title>dyn</title>\n"
                         "<link type=\"text/rcss\" rel=\"stylesheet\" href=\"uirml.rcss\"/>\n"
                         "</head>\n<body>\n<div id=\"dynpanel\">通道 B 动态屏\n"
                         "  <div id=\"dyntitle\">未装载</div>\n"
                         "</div>\n</body>\n</rml>\n";
                    std::ofstream g(uiDir / "editprev.rml", std::ios::trunc);
                    g << "<rml>\n<head><title>edit preview</title>\n"
                         "<link type=\"text/rcss\" rel=\"stylesheet\" href=\"uirml.rcss\"/>\n"
                         "</head>\n<body>\n<div id=\"editpanel\">Edit 预览</div>\n"
                         "</body>\n</rml>\n";
                }
                LEMON_LOG("uirml-smoke: dyn/editprev 复种（等 watcher 复活墓碑）");
            }
            if (frame == 302 && gameUi_) {
                if (const AssetEntry* e =
                        ctx_.Assets().FindByPath("Assets/UI/editprev.rml"))
                    LoadUiDocument(e->guid); // 双击通道重装载 + Show（形态 B 种子）
                else
                    LEMON_ERROR("uirml-smoke: 形态 B 种子失败（复种未被 watcher 处理）");
            }
            if (frame == 304) {
                std::error_code ecd;
                std::filesystem::remove(uiDir / "editprev.rml", ecd);
                // 形态 C（事件吞噬）：删除后**同帧**裸 Rescan（= ImportFile/
                // MakePrefabFrom 的"文件操作后立即重扫"形态）——立墓碑但**不经过**
                // RescanAssets 的 UI 逐出半边 → removed 事件被吃，后续 watcher 重扫
                // prev.missing 已真、cs.removed 恒空——事件驱动逐出从此失效，残留
                // 文档成僵尸。状态对账（ReconcileUiDocuments）在此形态下必须仍治愈
                ctx_.Assets().Rescan();
                LEMON_LOG(
                    "uirml-smoke: editprev.rml 编辑态删除 + 同帧裸 Rescan（事件吞噬）");
            }
            if (frame == 345 && !ctx_.Playing()) {
                if (TryEnterPlay()) {
                    smokeUiEvictEditOk =
                        gameUi_ && !gameUi_->HasDocument("Assets/UI/editprev.rml");
                    tabFocusPending_ = 1;
                } else
                    LEMON_ERROR("uirml-smoke: 三局 EnterPlay 失败");
            }
            if (frame == 348 && gameUi_) { // 347 帧渲染块已录 gameRT
                std::vector<uint8_t> rt;
                uint32_t rw = 0, rh = 0;
                if (device_->DebugFetchTextureCapture(rt, rw, rh))
                    smokeUiEvictPixN =
                        CountPixelsNear(rt, rw, rh, 96, 64, 128, 30); // #604080
            }
            if (frame == 352) {
                std::ofstream g(uiDir / "editprev.rml", std::ios::trunc);
                g << "<rml>\n<head><title>edit preview</title>\n"
                     "<link type=\"text/rcss\" rel=\"stylesheet\" href=\"uirml.rcss\"/>\n"
                     "</head>\n<body>\n<div id=\"editpanel\">Edit 预览</div>\n"
                     "</body>\n</rml>\n";
                LEMON_LOG("uirml-smoke: editprev 终局复种（终帧断言复位）");
            }
            if (frame == 394 && gameUi_) {
                if (const AssetEntry* e =
                        ctx_.Assets().FindByPath("Assets/UI/editprev.rml"))
                    LoadUiDocument(e->guid); // 终帧 hasDocC/cPrevN 复位（Play 中装载）
                else
                    LEMON_ERROR("uirml-smoke: 终局复种未被 watcher 处理");
            }
            // 形态 D（2026-09-29 用户实报：删场景 UIDocument 实体 → 重进 Play 仍
            // 显示——Scene 来源非 stale，旧 ResetDynamicDocuments 漏放行）。405
            // Stop（StopPlay 内清场）→ 断言 uirml 即刻不显示 + editprev 豁免保持；
            // 407 编辑态删声明实体；410 第四局——无脚本模式断言装载保留/不显示/
            // 零装载增量；412 像素（411 渲染块录）。脚本模式 C# 帧 1 会经通道 B
            // 合法 Show 拉回 uirml（动态屏语义，非僵尸）——delEnt/pix 仅无脚本
            // 模式裁决。415 Stop；417 重建声明实体；420 终局 EnterPlay（终帧像素
            // 断言回归原口径：uirml 声明显示）
            if (frame == 405 && ctx_.Playing()) {
                if (StopPlay()) {
                    smokeUiLoadsP4Base = gameUi_ ? gameUi_->DocumentLoadCount() : 0;
                    smokeUiExitHideOk =
                        gameUi_ && gameUi_->HasDocument("Assets/UI/uirml.rml") &&
                        !gameUi_->IsDocumentShown("Assets/UI/uirml.rml") &&
                        gameUi_->IsDocumentShown("Assets/UI/editprev.rml");
                    if (!smokeUiExitHideOk)
                        LEMON_ERROR("uirml-smoke: 形态 D 清场断言失败（Stop 后 "
                                    "Scene 文档应 Hide / Edit 预览应保持）");
                } else
                    LEMON_ERROR("uirml-smoke: 形态 D Stop 失败");
            }
            if (frame == 407 && !ctx_.Playing()) { // 编辑态删声明实体（= 用户操作）
                bool removed = false;
                ctx_.ActiveScene().View<ecs::UIDocument>().each(
                    [&](auto raw, ecs::UIDocument&) {
                        if (removed) return;
                        ctx_.DestroyEntityTree(ecs::Scene::FromEntt(raw));
                        removed = true;
                    });
                if (removed)
                    LEMON_LOG("uirml-smoke: 形态 D——编辑态删除 UIDocument 声明实体");
                else
                    LEMON_ERROR("uirml-smoke: 形态 D 删实体失败（场景无 UIDocument）");
            }
            if (frame == 410 && !ctx_.Playing() && TryEnterPlay()) {
                tabFocusPending_ = 1;
                // 簿记断言全模式安全：本钩子先于当帧 TickPlay——脚本模式 C# 的
                // 帧 1 UiRefill（合法通道 B 拉回）发生在断言之后
                smokeUiDelEntOk =
                    gameUi_ && gameUi_->HasDocument("Assets/UI/uirml.rml") &&
                    !gameUi_->IsDocumentShown("Assets/UI/uirml.rml") &&
                    gameUi_->DocumentLoadCount() == smokeUiLoadsP4Base;
                if (!smokeUiDelEntOk)
                    LEMON_ERROR("uirml-smoke: 形态 D 第四局残留（声明移除后"
                                "须：装载保留 + 不显示 + 零装载增量）");
            }
            if (frame == 412 && gameUi_ && launch.script.empty()) { // 411 渲染块已录
                std::vector<uint8_t> rt;
                uint32_t rw = 0, rh = 0;
                if (device_->DebugFetchTextureCapture(rt, rw, rh))
                    smokeUiDelEntPixN = CountPixelsNear(rt, rw, rh, 128, 32, 64, 30);
            }
            if (frame == 415 && ctx_.Playing()) {
                if (!StopPlay()) LEMON_ERROR("uirml-smoke: 形态 D 终局前 Stop 失败");
            }
            if (frame == 417 && !ctx_.Playing()) { // 重建声明实体（终局画面回归）
                const AssetEntry* en =
                    ctx_.Assets().FindByPath("Assets/UI/uirml.rml");
                if (en) {
                    ecs::Entity ue = ctx_.CreateEntity("UIDocument");
                    ctx_.ActiveScene().Emplace<ecs::UIDocument>(ue).sourceAssetGuid =
                        en->guid;
                } else
                    LEMON_ERROR("uirml-smoke: 形态 D 重建实体失败（夹具资产缺）");
            }
            if (frame == 420 && !ctx_.Playing()) {
                if (TryEnterPlay()) tabFocusPending_ = 1;
                else LEMON_ERROR("uirml-smoke: 终局 EnterPlay 失败");
            }
            // 形态 D 第二段（防线二独立验收面）：421 Stop **直调 ctx_.ExitPlay**
            // 绕过 StopPlay 清场（= 清场被跳过/失效的窗口形态）→ uirml 保持
            // shown → 422 删声明实体 → 424 第六局：无声明 + Scene 来源 + shown——
            // ResetDynamicDocuments 的 origin 判据必须独自完成清场（防线一不在
            // 场）。426 常规 Stop；428 重建实体；430 真终局（画面回归原口径）
            if (frame == 421 && ctx_.Playing()) {
                if (ctx_.ExitPlay()) // 直调（绕过 StopPlay 清场 = 防线一失效形态）
                    // seed 在场证明：Stop 后 uirml 仍 shown（清场确被绕过，防线二
                    // 是本局唯一清场者——TryEnterPlay 内部 Reset 先于一切读数）
                    smokeUiResetSeedOk = gameUi_ &&
                                         gameUi_->IsDocumentShown("Assets/UI/uirml.rml");
                else
                    LEMON_ERROR("uirml-smoke: 形态 D 二段 Stop 失败（绕过清场）");
            }
            if (frame == 422 && !ctx_.Playing()) {
                bool removed = false;
                ctx_.ActiveScene().View<ecs::UIDocument>().each(
                    [&](auto raw, ecs::UIDocument&) {
                        if (removed) return;
                        ctx_.DestroyEntityTree(ecs::Scene::FromEntt(raw));
                        removed = true;
                    });
                if (!removed)
                    LEMON_ERROR("uirml-smoke: 形态 D 二段删实体失败（无声明实体）");
            }
            if (frame == 424 && !ctx_.Playing() && TryEnterPlay()) {
                tabFocusPending_ = 1;
                smokeUiResetOnlyOk = smokeUiResetSeedOk && gameUi_ &&
                                     !gameUi_->IsDocumentShown("Assets/UI/uirml.rml");
                if (!smokeUiResetOnlyOk)
                    LEMON_ERROR("uirml-smoke: 形态 D 二段失败（防线二/Reset origin "
                                "判据未清未声明文档）");
            }
            if (frame == 426 && ctx_.Playing()) {
                if (!StopPlay()) LEMON_ERROR("uirml-smoke: 形态 D 二段 Stop 失败");
            }
            if (frame == 428 && !ctx_.Playing()) {
                if (const AssetEntry* en =
                        ctx_.Assets().FindByPath("Assets/UI/uirml.rml")) {
                    ecs::Entity ue = ctx_.CreateEntity("UIDocument");
                    ctx_.ActiveScene().Emplace<ecs::UIDocument>(ue).sourceAssetGuid =
                        en->guid;
                } else
                    LEMON_ERROR("uirml-smoke: 形态 D 二段重建实体失败");
            }
            if (frame == 430 && !ctx_.Playing()) {
                if (TryEnterPlay()) tabFocusPending_ = 1;
                else LEMON_ERROR("uirml-smoke: 真终局 EnterPlay 失败");
            }
            if (frame == 100) {
                if (rewriteFile(uiDir / "uirml.rml", "#ffd060", "#40ff90"))
                    LEMON_LOG("uirml-smoke: .rml 热重载播种（标题金→绿）");
                RescanAssets();
            }
            if (frame == 140) {
                if (rewriteFile(uiDir / "uirml.rcss", "color: #e0e0e0", "color: #80c0ff"))
                    LEMON_LOG("uirml-smoke: .rcss 热重载播种（正文灰→蓝，经 ReloadStyleSheets）");
                RescanAssets();
            }
        }
        // 终验（§6 #1/#2/#6/#7）：Play 中热重载——改 .cs 落盘 → 编译换装 → 新逻辑 +
        // StateBag 续跑（刷怪窗口 40→70；续跑总刷怪 66 = 换装前 16 + 换装后 50）
        if (launch.finalTest && frame == 20) {
            namespace fs = std::filesystem;
            const fs::path sp = fs::path(launch_->projectDir) / "Game" / "SpawnerBehaviour.cs";
            std::ifstream in(sp, std::ios::binary);
            std::string src((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            const size_t at = src.find("_tick > 40");
            finalPlayReloadOk_ = at != std::string::npos;
            if (finalPlayReloadOk_) {
                finalAliveAtReload_ = ctx_.ActiveScene().AliveCount();
                src.replace(at, 10, "_tick > 70");
                {
                    std::ofstream out(sp, std::ios::trunc);
                    out << src;
                } // 先落盘再编译（流未 close 就 build 会读到半截文件——实测坑）
                finalReloadFrame_ = frame;
                finalPlayReloadOk_ = TryHotReloadScripts("final-play（Play 中）");
                finalPlayReloadMs_ = hotReloadMs_;
            }
            if (!finalPlayReloadOk_) LEMON_ERROR("final: Play 中热重载失败");
        }
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
            }
            if (playDiag_ && frame >= 60 && frame < 120)
                in.ax = 1.0f; // 诊断注入：D 键右走（自动化无真人点击，不经聚焦门）
            if (launch.smokeTemplate && !gameViewFocused_) {
                // 模板冒烟注入（批④后修④ 两段）：kDeathArm 前切向环绕风筝（角速
                // 1.2rad/s × 速 240 → 半径 ~200）攒击杀/升级链；此后站桩送死——
                // 近身 Hazard 磨死 → 死亡对话框 → 自动 pick 复活（断言见 tplDeath 段）
                constexpr uint64_t kDeathArm = 2100;
                if (frame < kDeathArm) {
                    const float a = 0.02f * (float)frame;
                    in.ax = -std::sin(a);
                    in.ay = std::cos(a);
                } else {
                    in.ax = 0.0f; // 站桩 + 证据段压血（清怪快于刷怪，磨不死）
                    in.ay = 0.0f;
                }
            }
            ctx_.ActiveWorld().ApplyInput(in);
            bPump = BenchClock::now(); // 段界：pump（轮询/watcher/自动备份）结束 = sim 开始
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

        // --smoke-drag（M4.7c 交互回归）：ImGui 事件注入模拟"点选已选实体 → 拖 44pt →
        // 释放"，断言 Transform 位移 ≈ 屏幕位移 × 世界/点。链路分段计数由
        // SceneViewPanel 诊断成员给出（按下→arm→4px 阈值→Update）。与悬停扫掠互斥。
        if (launch.smokeDrag && scenePanel_) {
            Camera2D& cam = viewport_->SceneCam();
            if (frame == 2) {
                // 注入回归不吃 ini 漂移账：标记后由 BuildUI 在合法作用域内重建默认布局
                // （DockBuilder 调用必须在 NewFrame 内 + ##LemonEditor 窗口 ID 栈上）
                forceDefaultLayout_ = true;
            } else if (frame == 3) {
                noProjectCardDismissed_ = true; // 中央卡会截走视口中心点击（无项目模式）
                for (auto [ent, tf, sr] :
                     ctx_.ActiveScene().View<ecs::Transform2D, ecs::SpriteRenderer>().each()) {
                    if (!(sr.flags & 0x4)) continue;
                    ecs::Entity e = ecs::Scene::FromEntt(ent);
                    // 只取根实体：子实体 Transform2D.pos 是本地坐标（世界 = 父链合成），
                    // 拿本地当世界设相机中心会让拾取点全部落空
                    if (const ecs::Hierarchy* h = ctx_.ActiveScene().TryGet<ecs::Hierarchy>(e);
                        h && !h->parent.IsNull())
                        continue;
                    dragTarget = e;
                    dragBefore = tf.pos;
                    break;
                }
                snapEnabled_ = false;       // 断言免吸附台阶化（现默认已关，显式防默认变更）
                cam.zoom = 1.0f;
                cam.halfHeight = 360.0f;
                cam.center = dragBefore;    // 目标居中（屏幕位置确定性）
                ctx_.Select(dragTarget, false);
                // 世界→窗口点换算（λ 内自取 SceneCam：cam 为帧内局部量，跨帧捕获会悬垂）
                worldToPt = [&](Vec2 w) {
                    Camera2D& c = viewport_->SceneCam();
                    const Vec2 s = viewport_->WorldToScreen(
                        c, w, scenePanel_->LastRtW(), scenePanel_->LastRtH());
                    return Vec2{scenePanel_->LastVpX() +
                                    s.x * scenePanel_->LastVpW() / (float)scenePanel_->LastRtW(),
                                scenePanel_->LastVpY() +
                                    s.y * scenePanel_->LastVpH() / (float)scenePanel_->LastRtH()};
                };
            } else if (frame == 4 && !dragTarget.IsNull()) {
                // 按下点 = 目标中心 + 世界 (20,20)px（体内、避开中心块/轴带）。
                // 只记世界坐标，注入帧才换算屏幕点——面板矩形早帧可能还在 settle
                moveWorld = Vec2{dragBefore.x + 20.0f, dragBefore.y + 20.0f};
                moveReady = true;
            } else if (frame >= 5 && frame <= 21 && moveReady) {
                if (scenePanel_->LastRtW() == 0) { /* 面板未就绪：跳过本帧注入 */ }
                else {
                    const float moveX =
                        frame <= 9 ? 0.0f : std::min<float>((float)(frame - 9) * 4.0f, 44.0f);
                    int btn = -1; // -1 = 本帧不动按键
                    if (frame == 9 || (frame >= 10 && frame < 21)) btn = 1; // 9 按下，10-20 按住
                    else if (frame == 21) btn = 0;                          // 21 释放
                    const Vec2 pt = worldToPt(moveWorld);
                    ui_->SetInputOverride(pt.x + moveX, pt.y, btn);
                }
            } else if (frame == 23 && !dragDone) {
                dragDone = true; // 释放后一帧取值（EndGizmoDrag 已结算）
                if (ctx_.ActiveScene().Alive(dragTarget) &&
                    ctx_.ActiveScene().Has<ecs::Transform2D>(dragTarget)) {
                    const Vec2 after = ctx_.ActiveScene().Get<ecs::Transform2D>(dragTarget).pos;
                    const float wpp = 2.0f * cam.halfHeight /
                                      std::max(1.0f, scenePanel_->LastVpH()); // 世界/点
                    dragDx = after.x - dragBefore.x;
                    dragDy = after.y - dragBefore.y;
                    dragPassed = std::fabs(dragDx - 44.0f * wpp) < 3.0f && std::fabs(dragDy) < 3.0f;
                }
            }
            // ---- 第二段：旋转（同一 arm 路径，验证绕质心角度数学）----
            // 24 切 Rotate 工具；25-27 悬停；28 按下于体内点（中心 +45° 半径 28px）；
            // 29-38 沿圆弧 −90°；39 释放；41 断言 rot Δ=−π/2（snap 已关 = 连续角）。
            else if (frame == 24) {
                tool_ = EditTool::Rotate;
                if (ctx_.ActiveScene().Has<ecs::Transform2D>(dragTarget))
                    rotBefore = ctx_.ActiveScene().Get<ecs::Transform2D>(dragTarget).rot;
            } else if (frame == 25 && scenePanel_->LastRtW() > 0) {
                // 弧上点 → 屏幕点换算；弧心 = 移动段结束后的当前位置。
                // λ 内自取 SceneCam（cam 为帧内局部量，跨帧捕获会悬垂）
                const Vec2 rotCenter =
                    ctx_.ActiveScene().Alive(dragTarget) &&
                            ctx_.ActiveScene().Has<ecs::Transform2D>(dragTarget)
                        ? ctx_.ActiveScene().Get<ecs::Transform2D>(dragTarget).pos
                        : dragBefore;
                arcWorldToPt = [&, rotCenter](float ang) {
                    const Vec2 w{rotCenter.x + 28.0f * std::cos(ang),
                                 rotCenter.y + 28.0f * std::sin(ang)};
                    const Vec2 s = viewport_->WorldToScreen(
                        viewport_->SceneCam(), w, scenePanel_->LastRtW(), scenePanel_->LastRtH());
                    return Vec2{scenePanel_->LastVpX() +
                                    s.x * scenePanel_->LastVpW() / (float)scenePanel_->LastRtW(),
                                scenePanel_->LastVpY() +
                                    s.y * scenePanel_->LastVpH() / (float)scenePanel_->LastRtH()};
                };
                rotReady = true;
            } else if (frame >= 26 && frame <= 39 && rotReady) {
                constexpr float kA0 = 0.78539818f;           // 45°
                const float t = frame <= 28 ? 0.0f
                    : std::min<float>((float)(frame - 28) / 10.0f, 1.0f); // 28-38 走弧
                const float ang = kA0 - t * 1.57079637f;     // 45° → −45°
                const Vec2 p = arcWorldToPt(ang);
                int btn = -1;
                if (frame == 28 || (frame >= 29 && frame < 39)) btn = 1;
                else if (frame == 39) btn = 0;
                ui_->SetInputOverride(p.x, p.y, btn);
            } else if (frame == 41 && !rotDone) {
                rotDone = true;
                if (ctx_.ActiveScene().Alive(dragTarget) &&
                    ctx_.ActiveScene().Has<ecs::Transform2D>(dragTarget)) {
                    rotAfter = ctx_.ActiveScene().Get<ecs::Transform2D>(dragTarget).rot;
                    rotPassed = std::fabs(rotAfter - rotBefore + 1.57079637f) < 0.06f;
                    dragPassed = dragPassed && rotPassed;
                }
            }
            // ---- 第三段：Select 8 向 resize（43 切工具；44 按右边中点手柄；
            //      45-53 外拖 27pt；54 释放；56 断言 scale.x 增大 + 左缘锚定）----
            else if (frame == 43) {
                tool_ = EditTool::Select;
                if (ctx_.ActiveScene().Alive(dragTarget) &&
                    ctx_.ActiveScene().Has<ecs::Transform2D>(dragTarget)) {
                    scaleBefore = ctx_.ActiveScene().Get<ecs::Transform2D>(dragTarget).scale.x;
                    Vec2 c, s;
                    float r = 0;
                    if (viewport_->WorldBoundsOf(ctx_, dragTarget, c, s, r)) {
                        // 右边中点手柄（世界点，注入帧才换算屏幕）+ 外拖方向
                        // （实体本地 +X 轴的世界朝向——旋转段后已转 ~−90°）
                        const float cs = std::cos(r), sn = std::sin(r);
                        handleWorld = Vec2{c.x + cs * s.x * 0.5f, c.y + sn * s.x * 0.5f};
                        anchorLeft0 = c.x - cs * s.x * 0.5f;
                        handleDragDir = Vec2{cs, sn};
                        resizeReady = true;
                    }
                }
            } else if (frame >= 44 && frame <= 54 && resizeReady) {
                if (scenePanel_->LastRtW() > 0) {
                    const float off =
                        frame <= 44 ? 0.0f : std::min<float>((frame - 44) * 3.0f, 27.0f);
                    int btn = -1;
                    if (frame == 44 || (frame >= 45 && frame < 54)) btn = 1;
                    else if (frame == 54) btn = 0;
                    const Vec2 pt = worldToPt(handleWorld);
                    ui_->SetInputOverride(pt.x + handleDragDir.x * off,
                                          pt.y + handleDragDir.y * off, btn);
                }
            } else if (frame == 56 && !resizeDone) {
                resizeDone = true;
                if (ctx_.ActiveScene().Alive(dragTarget) &&
                    ctx_.ActiveScene().Has<ecs::Transform2D>(dragTarget)) {
                    const auto& tf = ctx_.ActiveScene().Get<ecs::Transform2D>(dragTarget);
                    scaleAfter = tf.scale.x;
                    Vec2 c, s;
                    float r = 0;
                    if (viewport_->WorldBoundsOf(ctx_, dragTarget, c, s, r))
                        anchorLeft1 = c.x - std::cos(r) * s.x * 0.5f;
                    const bool grew = scaleAfter > scaleBefore * 1.15f;
                    const bool anchored = std::fabs(anchorLeft1 - anchorLeft0) < 6.0f;
                    resizePassed = grew && anchored;
                    dragPassed = dragPassed && resizePassed;
                }
            }
            // ---- 第四段：缩放（58 记锚；59-61 滚轮 +1 前推；63 断言 zoom 增大
            //      且选中对象屏幕位置不动）----
            else if (frame == 58) {
                zoomBefore = cam.zoom;
                if (ctx_.ActiveScene().Alive(dragTarget) &&
                    ctx_.ActiveScene().Has<ecs::Transform2D>(dragTarget)) {
                    // 锚点 = 对象当前中心（移动/resize 段后已不在原位）
                    const Vec2 cur = ctx_.ActiveScene().Get<ecs::Transform2D>(dragTarget).pos;
                    selScreen0 = worldToPt(cur);
                }
            } else if (frame >= 59 && frame <= 61 && scenePanel_->LastRtW() > 0) {
                const Vec2 ctr{scenePanel_->LastVpX() + scenePanel_->LastVpW() * 0.5f,
                               scenePanel_->LastVpY() + scenePanel_->LastVpH() * 0.5f};
                ui_->SetInputOverride(ctr.x, ctr.y, -1, /*wheel=*/1);
            } else if (frame == 63 && !zoomDone) {
                zoomDone = true;
                zoomAfter = cam.zoom;
                if (ctx_.ActiveScene().Alive(dragTarget) &&
                    ctx_.ActiveScene().Has<ecs::Transform2D>(dragTarget)) {
                    const Vec2 cur = ctx_.ActiveScene().Get<ecs::Transform2D>(dragTarget).pos;
                    selScreen1 = worldToPt(cur);
                }
                const bool zoomedIn = zoomAfter > zoomBefore * 1.25f;
                const bool anchored =
                    Length(Vec2{selScreen1.x - selScreen0.x, selScreen1.y - selScreen0.y}) < 12.0f;
                zoomPassed = zoomedIn && anchored;
                dragPassed = dragPassed && zoomPassed;
            }
            // ---- 临时诊断（网格 v4）：末段跳极缩小取景，配 --screenshot 做像素扫描 ----
            else if (frame == 70) {
                cam.zoom = 0.05f;
                cam.halfHeight = 360.0f / cam.zoom;
                cam.center = Vec2{0, 0};
                ctx_.ClearSelection();
                gridVisible_ = true; // 网格显示门控（极缩小诊断截图需网格在画）
            }
            // ---- 第五段：甩飞防护 + F 聚焦（72 抛远实体并选中；73-75 视口中心滚轮
            //      +1；77 断言相机没被拽走——视野外对象禁止锚定；79 注 F；81 断言
            //      相机聚焦到实体）----
            else if (frame == 72) {
                if (ctx_.ActiveScene().Alive(dragTarget) &&
                    ctx_.ActiveScene().Has<ecs::Transform2D>(dragTarget)) {
                    slingWorld = Vec2{2.0e5f, 3.5e5f};
                    ctx_.ActiveScene().Get<ecs::Transform2D>(dragTarget).pos = slingWorld;
                    ctx_.Select(dragTarget, false);
                    slingCenter0 = viewport_->SceneCam().center;
                }
            } else if (frame >= 73 && frame <= 75 && scenePanel_->LastRtW() > 0) {
                const Vec2 ctr{scenePanel_->LastVpX() + scenePanel_->LastVpW() * 0.5f,
                               scenePanel_->LastVpY() + scenePanel_->LastVpH() * 0.5f};
                ui_->SetInputOverride(ctr.x, ctr.y, -1, /*wheel=*/1);
            } else if (frame == 77 && !slingDone) {
                slingDone = true;
                const Vec2 c = viewport_->SceneCam().center;
                slingDx = c.x - slingCenter0.x;
                slingDy = c.y - slingCenter0.y;
                // 容差 16 世界单位：注入中心点与逐帧视口中心有 ±1pt 布局抖动，
                // 极缩小下折 ~2 单位/格；真甩飞（无锚点回退）= 每格 2 万+
                slingPassed = std::fabs(slingDx) < 16.0f && std::fabs(slingDy) < 16.0f;
            } else if (frame == 79) {
                ui_->SetKeyTapOverride(ImGuiKey_F);
            } else if (frame == 81 && !focusDone) {
                focusDone = true;
                const Vec2 c = viewport_->SceneCam().center;
                focusDelta = Length(Vec2{c.x - slingWorld.x, c.y - slingWorld.y});
                slingPassed = slingPassed && focusDelta < 32.0f;
                dragPassed = dragPassed && slingPassed;
            }
        }

        // ---- 热修③（2026-09-27 用户实测：创建后面板空态无入口）真实模态点击
        // 全链复现：播种块 OpenAnimationCreateSet("") 已排好模态（名 player、无
        // 源目录 = 纯空集），此处注入点击"创建"（TestHooks 矩形 + 真实 ImGui
        // 管线，smoke-ui 同款 hold/release 隔帧模式）→ TryCreateSet 落盘
        // Assets/player.override → Rescan → OpenSet；f45 断言 setGuid_ 已切到
        // 新集（空集工作台 = 左列可见，"＋ 新建"入口在位）。
        if (launch.smokeAnim) {
            if (frame >= 2 && frame < 40) {
                ImVec2 a, b;
                const bool q = testhooks::Find("animset.queued", a, b);
                if (q != smokeSetModalQueuedPrev) {
                    std::printf("[lemon] probe: 新建集模态 queued=%s @f%llu\n",
                                q ? "ON" : "OFF", (unsigned long long)frame);
                    smokeSetModalQueuedPrev = q;
                }
                // 三图优化点①：左列段行元信息缓存（f30 = 模态开着但工作台照画，
                // 四段行都已解析）。walk/hit/whole = 播种原值；edit = 播种 1 帧 +
                // clip 编辑链 +1 的复合值——顺带锁住"元信息读的是存盘内容"
                if (frame == 30 && !smokeLeftColDone) {
                    smokeLeftColDone = true;
                    if (PanelRegistry::Entry* en = panels_.FindEntry("Animation")) {
                        AnimationPanel* ap = static_cast<AnimationPanel*>(en->panel);
                        const int walk = ap->SegRowFramesForTest(0);
                        const int hit = ap->SegRowFramesForTest(1);
                        const int edit = ap->SegRowFramesForTest(2);
                        const int whole = ap->SegRowFramesForTest(3);
                        smokeLeftColOk = walk == 4 && hit == 2 && edit == 2 && whole == 1;
                        std::printf("[lemon] probe: 左列段行 walk=%d hit=%d edit=%d "
                                    "whole=%d (expect 4/2/2/1) @f%llu\n",
                                    walk, hit, edit, whole, (unsigned long long)frame);
                    }
                    if (!smokeLeftColOk)
                        LEMON_ERROR("smoke-anim：左列段行元信息未填充/帧数不符（优化点①）");
                }
            } else if (frame == 40) {
                ImVec2 mn, mx;
                if (testhooks::Find("animset.create", mn, mx)) {
                    smokeCreatePt = Vec2{(mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f};
                    ui_->SetInputOverride(smokeCreatePt.x, smokeCreatePt.y, 1);
                } else {
                    ImVec2 a, b;
                    const bool skip = testhooks::Find("animpanel.skip", a, b);
                    const bool began = testhooks::Find("animpanel.begin", a, b);
                    const bool queued = testhooks::Find("animset.queued", a, b);
                    LEMON_ERROR("smoke-anim：新建集模态「创建」按钮未登记（探针 skip=%d "
                                "begin=%d queued=%d）",
                                skip ? 1 : 0, began ? 1 : 0, queued ? 1 : 0);
                }
            } else if (frame == 41 && smokeCreatePt.x > -1.0e8f) {
                ui_->SetInputOverride(smokeCreatePt.x, smokeCreatePt.y, 0);
            } else if (frame == 45 && !smokeSetCreateFlowDone) {
                smokeSetCreateFlowDone = true;
                const AssetEntry* ne = ctx_.Assets().FindByPath("Assets/player.override");
                if (PanelRegistry::Entry* en = panels_.FindEntry("Animation"))
                    smokeSetCreateOpenOk =
                        ne && !ne->missing && ne->type == AssetType::AnimSet &&
                        static_cast<AnimationPanel*>(en->panel)->SetGuidForTest() == ne->guid;
                if (!smokeSetCreateOpenOk)
                    LEMON_ERROR("smoke-anim：模态创建后未开新集（setGuid_ 未切到 player）");
            }
            // ---- T3-UX4：选择器系统式多选全链（真实弹窗 + 注入点击 + 语义直调）。
            // 原料 = 播种 9 张 pick*.png + smoke.png + yami 图（Assets 无子目录 →
            // tile 序 = 文件名序：tile0=pick0 … tile9=smoke tile10=yami）。
            // f50 开（多选默认缩略图网格）；f55/56 真实点击 tile0（选 1）；f60
            // Shift 范围 tile0..tile7（选 8）；f64 Ctrl/Cmd+A（全选 11）。修饰键
            // 按住不便注入 → Shift 走语义直调（ApplyMultiClickForTest = 点击处理
            // 器同入口），Ctrl+A 走真实 chord 注入（mac 侧 Ctrl 和弦映射 Cmd）。
            else if (frame == 50) {
                if (PanelRegistry::Entry* en = panels_.FindEntry("Animation"))
                    static_cast<AnimationPanel*>(en->panel)->StartImageFilePick(*this);
            } else if (frame == 55) {
                ImVec2 mn, mx;
                if (testhooks::Find("picker.tile0", mn, mx)) {
                    smokePickPt = Vec2{(mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f};
                    ui_->SetInputOverride(smokePickPt.x, smokePickPt.y, 1);
                } else {
                    LEMON_ERROR("smoke-anim：选择器 tile0 未登记（网格没画）");
                }
            } else if (frame == 56 && smokePickPt.x > -1.0e8f) {
                ui_->SetInputOverride(smokePickPt.x, smokePickPt.y, 0);
            } else if (frame == 58) {
                if (PanelRegistry::Entry* en = panels_.FindEntry("Animation"))
                    smokePickClickOk =
                        static_cast<AnimationPanel*>(en->panel)->PickerSelCountForTest() == 1;
                if (!smokePickClickOk)
                    LEMON_ERROR("smoke-anim：注入点击后多选计数 != 1");
            } else if (frame == 60) {
                if (PanelRegistry::Entry* en = panels_.FindEntry("Animation"))
                    static_cast<AnimationPanel*>(en->panel)->PickerClickForTest(7, false, true);
            } else if (frame == 62) {
                if (PanelRegistry::Entry* en = panels_.FindEntry("Animation"))
                    smokePickShiftOk =
                        static_cast<AnimationPanel*>(en->panel)->PickerSelCountForTest() == 8;
                if (!smokePickShiftOk)
                    LEMON_ERROR("smoke-anim：Shift 范围选择后计数 != 8");
            } else if (frame == 64) {
                ui_->SetKeyChordOverride((int)ImGuiMod_Ctrl, (int)ImGuiKey_A);
            } else if (frame == 66 && !smokePickFlowDone) {
                smokePickFlowDone = true;
                if (PanelRegistry::Entry* en = panels_.FindEntry("Animation"))
                    smokePickAllOk =
                        static_cast<AnimationPanel*>(en->panel)->PickerSelCountForTest() == 11;
                if (!smokePickAllOk)
                    LEMON_ERROR("smoke-anim：Ctrl+A 全选后计数 != 11");
            }
            // ---- 三图优化点③（选帧对话框已选计数+清空）：f68 真实点击关掉第一段
            // 文件选择器（picker.cancel 本轮新登记——此前该链测完不关窗，叠加
            // 模态栈留隐患）→ f70 语义直开选帧对话框（OpenSheetPickForTest，第一
            // 段不重跑）→ f72/73 真实点「全选」→ f76 断言右栏计数=4（anim-sheet
            // 4×1 格）→ f78/79 点「清空」→ f82 断言计数=0 且 sheetpick.count 矩形
            // 在（计数确画在右参数区的证据）→ f84/85 点「取消」→ f88 断言对话框
            // 关（pickOpen_ 回落防悬空开态）。
            else if (frame == 68) {
                ImVec2 mn, mx;
                if (testhooks::Find("picker.cancel", mn, mx)) {
                    smokeSheetPt = Vec2{(mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f};
                    ui_->SetInputOverride(smokeSheetPt.x, smokeSheetPt.y, 1);
                } else {
                    LEMON_ERROR("smoke-anim：文件选择器「取消」未登记（选择器没开？）");
                }
            } else if (frame == 69 && smokeSheetPt.x > -1.0e8f) {
                ui_->SetInputOverride(smokeSheetPt.x, smokeSheetPt.y, 0);
            } else if (frame == 70) {
                if (PanelRegistry::Entry* en = panels_.FindEntry("Animation"))
                    static_cast<AnimationPanel*>(en->panel)
                        ->OpenSheetPickForTest(*this, kAnimSheetGuid);
            } else if (frame == 72 || frame == 78 || frame == 84) {
                const char* key = frame == 72   ? "sheetpick.all"
                                  : frame == 78 ? "sheetpick.none"
                                                : "sheetpick.cancel";
                ImVec2 mn, mx;
                if (testhooks::Find(key, mn, mx)) {
                    smokeSheetPt = Vec2{(mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f};
                    ui_->SetInputOverride(smokeSheetPt.x, smokeSheetPt.y, 1);
                } else {
                    LEMON_ERROR("smoke-anim：选帧对话框 %s 未登记（对话框没开？）", key);
                }
            } else if ((frame == 73 || frame == 79 || frame == 85) && smokeSheetPt.x > -1.0e8f) {
                ui_->SetInputOverride(smokeSheetPt.x, smokeSheetPt.y, 0);
            } else if (frame == 76) {
                if (PanelRegistry::Entry* en = panels_.FindEntry("Animation"))
                    smokeSheetAllOk =
                        static_cast<AnimationPanel*>(en->panel)->SheetPickCountForTest() == 4;
                if (!smokeSheetAllOk)
                    LEMON_ERROR("smoke-anim：选帧对话框全选后已选计数 != 4");
            } else if (frame == 80) {
                // 底行越窗锁（2026-09-28 用户截图报：右对齐公式按两钮算，「替换为」
                // 整钮出窗、「添加」右缘贴边被裁）：对话框开着的帧取替换钮与模态窗
                // 矩形（TestHooks 每帧清空，只能在开窗帧断），右缘须在窗内。
                ImVec2 rmn, rmx, wmn, wmx;
                smokeSheetRowOk = testhooks::Find("sheetpick.replace", rmn, rmx) &&
                                  testhooks::Find("sheetpick.win", wmn, wmx) &&
                                  rmx.x <= wmx.x - 2.0f;
                if (!smokeSheetRowOk)
                    LEMON_ERROR("smoke-anim：底行「替换为」按钮越出选帧对话框窗界");
            } else if (frame == 82) {
                ImVec2 mn, mx;
                const bool cntRect = testhooks::Find("sheetpick.count", mn, mx);
                if (PanelRegistry::Entry* en = panels_.FindEntry("Animation"))
                    smokeSheetClearOk =
                        cntRect &&
                        static_cast<AnimationPanel*>(en->panel)->SheetPickCountForTest() == 0;
                if (!smokeSheetClearOk)
                    LEMON_ERROR("smoke-anim：清空后已选计数 != 0 或右栏计数矩形未登记");
            } else if (frame == 88 && !smokeSheetFlowDone) {
                smokeSheetFlowDone = true;
                if (PanelRegistry::Entry* en = panels_.FindEntry("Animation"))
                    smokeSheetCloseOk =
                        !static_cast<AnimationPanel*>(en->panel)->SheetPickOpenForTest();
                if (!smokeSheetCloseOk)
                    LEMON_ERROR("smoke-anim：取消后选帧对话框未关（pickOpen_ 悬空）");
            }
            // ---- 帧序事故回归（2026-09-27 用户实测 Monster03 dying 反序；根因 =
            // InsertFrameAfter(-1) 头插冒充末尾追加）：f90 重开多图通道（记加帧前
            // 基数）→ f92 Ctrl+A 全选 11 图（显示序 = 文件名升序归一）→ f96/97
            // 真实点「打开」确认 → f104 断言：追加数 = 11 且逐位 = Assets 目录
            // 文件名升序 guid 序（旧 bug 下首帧 = 排序最后一张图）。
            else if (frame == 90) {
                if (PanelRegistry::Entry* en = panels_.FindEntry("Animation")) {
                    AnimationPanel* p = static_cast<AnimationPanel*>(en->panel);
                    smokeAddBase = p->EditFrameCountForTest();
                    p->StartImageFilePick(*this);
                }
            } else if (frame == 92) {
                ui_->SetKeyChordOverride((int)ImGuiMod_Ctrl, (int)ImGuiKey_A);
            } else if (frame == 94) {
                if (PanelRegistry::Entry* en = panels_.FindEntry("Animation")) {
                    const size_t n = static_cast<AnimationPanel*>(en->panel)
                                         ->PickerSelCountForTest();
                    if (n != 11) LEMON_ERROR("smoke-anim：重开选择器 Ctrl+A 后计数 != 11（%zu）", n);
                }
            } else if (frame == 96) {
                ImVec2 mn, mx;
                if (testhooks::Find("picker.open", mn, mx)) {
                    smokeSheetPt = Vec2{(mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f};
                    ui_->SetInputOverride(smokeSheetPt.x, smokeSheetPt.y, 1);
                } else {
                    LEMON_ERROR("smoke-anim：选择器「打开」未登记（选择器没开/无选中？）");
                }
            } else if (frame == 97 && smokeSheetPt.x > -1.0e8f) {
                ui_->SetInputOverride(smokeSheetPt.x, smokeSheetPt.y, 0);
            } else if (frame == 104 && !smokeAddFlowDone) {
                smokeAddFlowDone = true;
                std::vector<uint64_t> want; // 期望 = Assets 根图片按文件名升序的 guid
                for (const AssetEntry* e : ctx_.Assets().EntriesInDir("Assets"))
                    if (e->type == AssetType::Sprite && !e->missing) want.push_back(e->guid);
                AnimationPanel* p = nullptr;
                if (PanelRegistry::Entry* en = panels_.FindEntry("Animation"))
                    p = static_cast<AnimationPanel*>(en->panel);
                if (p) {
                    smokeAddCountOk = p->EditFrameCountForTest() == smokeAddBase + want.size();
                    if (!smokeAddCountOk)
                        LEMON_ERROR("smoke-anim：多图追加后帧数 %zu != 基数 %zu + %zu",
                                    p->EditFrameCountForTest(), smokeAddBase, want.size());
                    smokeAddOrderOk = smokeAddCountOk;
                    for (size_t k = 0; k < want.size() && smokeAddOrderOk; ++k)
                        smokeAddOrderOk = p->EditFrameSheetForTest(smokeAddBase + k) == want[k];
                    if (!smokeAddOrderOk)
                        LEMON_ERROR("smoke-anim：多图追加帧序 != 文件名升序（第 %zu 位起错位）",
                                    smokeAddBase);
                }
            }
            // ---- T3-UX7：极简文件夹创建框 + 裸 clip 双击断路。f107 现场播种
            // walksrc/ 三图（文件名序 pick0<1<2——不随项目播种：选择器网格含目录
            // 瓦片会挤占 tile 序，f50 链的 1/8 断言依赖"Assets 无子目录"前提）
            // → f108 语义开框（浏览器在 Assets 根 → 保存位置 = Assets）→
            // f110/111 真实点「创建」→ f114 断言 Assets/walksrc.anim 落盘 + 面板
            // 切到新 clip + 3 帧 = 文件名序（文件夹通道帧序锁）→ f116 先开集
            // （制造旧 bug 的集态残留）→ f117 "双击"新 clip（非任何集成员 = 裸
            // 路径）断言集态清零 + 目标切换（断路回归：旧 bug 下面板继续显示集
            // = "双击没反应"）。
            else if (frame == 107) {
                namespace fs = std::filesystem;
                const fs::path a = fs::path(launchCopy_.projectDir) / "Assets";
                std::error_code cec;
                fs::create_directories(a / "walksrc", cec);
                for (int k = 0; k < 3; ++k)
                    fs::copy_file(a / ("pick" + std::to_string(k) + ".png"),
                                  a / "walksrc" / ("pick" + std::to_string(k) + ".png"),
                                  fs::copy_options::overwrite_existing, cec);
                RescanAssets();
            } else if (frame == 108) {
                OpenAnimationCreateFromFolder("Assets/walksrc");
            } else if (frame == 110) {
                ImVec2 mn, mx;
                if (testhooks::Find("clipcreate.ok", mn, mx)) {
                    smokeSheetPt = Vec2{(mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f};
                    ui_->SetInputOverride(smokeSheetPt.x, smokeSheetPt.y, 1);
                } else {
                    LEMON_ERROR("smoke-anim：创建框「创建」未登记（框没开？）");
                }
            } else if (frame == 111 && smokeSheetPt.x > -1.0e8f) {
                ui_->SetInputOverride(smokeSheetPt.x, smokeSheetPt.y, 0);
            } else if (frame == 114) {
                AnimationPanel* p = nullptr;
                if (PanelRegistry::Entry* en = panels_.FindEntry("Animation"))
                    p = static_cast<AnimationPanel*>(en->panel);
                const AssetEntry* nc = ctx_.Assets().FindByPath("Assets/walksrc.anim");
                const AssetEntry* i0 = ctx_.Assets().FindByPath("Assets/walksrc/pick0.png");
                smokeFolderCreateOk = p && nc && i0 && !nc->missing &&
                                      nc->type == AssetType::Clip &&
                                      p->TargetGuidForTest() == nc->guid &&
                                      p->EditFrameCountForTest() == 3 &&
                                      p->EditFrameSheetForTest(0) == i0->guid;
                if (!smokeFolderCreateOk)
                    LEMON_ERROR("smoke-anim：文件夹创建链不符（落盘/切换/3帧/首帧序）");
            } else if (frame == 116) {
                OpenAnimationEditor(kAnimSetGuid);
            } else if (frame == 117) {
                AnimationPanel* p = nullptr;
                if (PanelRegistry::Entry* en = panels_.FindEntry("Animation"))
                    p = static_cast<AnimationPanel*>(en->panel);
                const AssetEntry* nc = ctx_.Assets().FindByPath("Assets/walksrc.anim");
                if (p && nc) OpenAnimationEditor(nc->guid); // 双击裸 clip（同步置态）
                smokeDblClipOk = p && nc && p->SetGuidForTest() == 0 &&
                                 p->TargetGuidForTest() == nc->guid;
                if (!smokeDblClipOk)
                    LEMON_ERROR("smoke-anim：双击裸 clip 未切换（集态残留 = 断路回归）");
            }
            // ---- T3-UX9：胶片带拖拽通道回归锁（语义断言，非注入）。事故：
            // 帧格曾用裸 ImGui::Image（无 ID）→ 拖源永假 + 拖靶只认图矩形 =
            // 拖拽重排/入格换图/带尾加帧自出生即死（用户实测报告）。修 = 帧格
            // 改 InvisibleButton + drawlist 直贴。回归位 = 帧格项 ID 非零。
            // 为何不注入真拖：注入点击在普通窗口会被 ImGui 窗口兜底路由吃掉
            //（悬停瞬移 → 点击绑 MoveId；既有注入链全在模态窗不受此限——模态
            // 路由不同）。真人鼠标有真实事件流不受影响；拖拽手感归真人验收。
            else if (frame == 118) {
                AnimationPanel* p = nullptr;
                if (PanelRegistry::Entry* en = panels_.FindEntry("Animation"))
                    p = static_cast<AnimationPanel*>(en->panel);
                smokeDragItemOk = p && p->StripCellItemForTest() != 0;
                if (!smokeDragItemOk)
                    LEMON_ERROR("smoke-anim：帧格非交互件（p=%p frames=%zu id=%u）",
                                (void*)p, p ? p->EditFrameCountForTest() : (size_t)0,
                                p ? p->StripCellItemForTest() : 0u);
            }
        }

        // --smoke-ui（M4.7d 收尾轮）：真人会话注入回归。事件走真实 ImGui 管线
        // （后端 chord/text 注入），点击目标 = 面板登记的控件屏幕矩形
        // （Tooling/TestHooks——上一帧 UI 所画，静态 UI 坐标逐帧稳定）。
        // 覆盖：Q/W/E/R 工具切换 → 点选 → Ctrl+D 复制 → Del 删除 → Undo/Redo
        // 全往返 → label-scrub（拖字段名改 rot + Ctrl+Z）→ Ctrl+S → Ctrl+P
        // Play 往返 → Hierarchy 点击+F2 重命名（打字）→ 行拖拽挂父子（Undo）→
        // 子目录文件夹进入+面包屑返回 → 命名布局保存（模态+打字+Enter）→ 默认
        // 布局还原 → Console 标签页点击 + Collapse 开关。
        if (launch.smokeUi && scenePanel_) {
            auto rectCenter = [](const char* key) -> Vec2 {
                ImVec2 mn, mx;
                if (testhooks::Find(key, mn, mx))
                    return Vec2{(mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f};
                return Vec2{-1.0e9f, -1.0e9f};
            };
            auto hold = [&](Vec2 pt) { ui_->SetInputOverride(pt.x, pt.y, 1); };
            auto release = [&](Vec2 pt) { ui_->SetInputOverride(pt.x, pt.y, 0); };
            if (frame == 2) {
                forceDefaultLayout_ = true; // 注入不吃 ini 漂移账（同 smoke-drag）
            } else if (frame == 4) {
                namespace fs = std::filesystem;
                std::error_code ec;
                // Profiler 已进默认布局 bottom 隐藏标签（BUG-3 修复），不再浮动遮挡；
                // 注入会话仍显式关掉，保证底部 dock 区域注入目标确定
                for (auto& e : panels_.Entries())
                    if (std::strcmp(e.panel->Name(), "Profiler") == 0) e.open = false;
                // 目录导航段原料：Assets/sub/ + 资产副本（重扫保确定性）
                fs::path assets = fs::path(launchCopy_.projectDir) / "Assets";
                fs::create_directories(assets / "sub", ec);
                fs::copy_file(assets / "smoke.png", assets / "sub" / "other.png",
                              fs::copy_options::overwrite_existing, ec);
                RescanAssets();
                ctx_.ActiveScene().Each([&](ecs::Entity e) {
                    const ecs::Meta* m = ctx_.ActiveScene().TryGet<ecs::Meta>(e);
                    if (!m) return;
                    if (std::strcmp(m->tag, "Player") == 0) uiTarget = e;
                    if (std::strcmp(m->tag, "Gate") == 0) uiGate = e;
                });
                snapEnabled_ = false;
                Camera2D& cam = viewport_->SceneCam();
                cam.zoom = 1.0f;
                cam.halfHeight = 360.0f;
                cam.center = ctx_.ActiveScene().Get<ecs::Transform2D>(uiTarget).pos;
                uiBaseCount = ctx_.ActiveScene().AliveCount();
                uiTargetSubtree = SubtreeSizeOf(ctx_.ActiveScene(), uiTarget);
                uiRot0 = ctx_.ActiveScene().Get<ecs::Transform2D>(uiTarget).rot;
                // 场景路径先行落定（否则 Ctrl+S 走另存为弹窗——那是 FilePicker 冒烟）
                fs::create_directories(fs::path(launchCopy_.projectDir) / "Scenes", ec);
                ctx_.SaveScene(
                    (fs::path(launchCopy_.projectDir) / "Scenes" / "ui.scene").string());
            }
            // ---- A. 工具快捷键（tap 下帧断言 Tool）----
            else if (frame == 6) ui_->SetKeyTapOverride(ImGuiKey_Q);
            else if (frame == 7) toolsOk = tool_ == EditTool::Select;
            else if (frame == 8) ui_->SetKeyTapOverride(ImGuiKey_W);
            else if (frame == 9) toolsOk = toolsOk && tool_ == EditTool::Move;
            else if (frame == 10) ui_->SetKeyTapOverride(ImGuiKey_E);
            else if (frame == 11) toolsOk = toolsOk && tool_ == EditTool::Rotate;
            else if (frame == 12) ui_->SetKeyTapOverride(ImGuiKey_R);
            else if (frame == 13) toolsOk = toolsOk && tool_ == EditTool::Scale;
            else if (frame == 14) ui_->SetKeyTapOverride(ImGuiKey_Q);
            else if (frame == 15) toolsOk = toolsOk && tool_ == EditTool::Select;
            // ---- B. 视口点选（Select 工具，点击实体体内 +6px）----
            else if (frame == 16 && scenePanel_->LastRtW() > 0) {
                const Vec2 w = ctx_.ActiveScene().Get<ecs::Transform2D>(uiTarget).pos;
                const Vec2 s = viewport_->WorldToScreen(
                    viewport_->SceneCam(), Vec2{w.x + 6.0f, w.y + 6.0f},
                    scenePanel_->LastRtW(), scenePanel_->LastRtH());
                uiPt0 = Vec2{scenePanel_->LastVpX() +
                                 s.x * scenePanel_->LastVpW() / (float)scenePanel_->LastRtW(),
                             scenePanel_->LastVpY() +
                                 s.y * scenePanel_->LastVpH() / (float)scenePanel_->LastRtH()};
                hold(uiPt0);
            } else if (frame == 17) {
                release(uiPt0);
            } else if (frame == 18) {
                selOk = ctx_.Primary() == uiTarget;
            }
            // ---- C. 复制/删除 + Undo/Redo 全往返（栈序 [复制,删除]）----
            else if (frame == 20)
                ui_->SetKeyChordOverride((int)ImGuiMod_Ctrl, (int)ImGuiKey_D);
            else if (frame == 22)
                dupOk = ctx_.ActiveScene().AliveCount() == uiBaseCount + uiTargetSubtree &&
                        !ctx_.Primary().IsNull(); // C8：+子树大小（Player 带 3 Mob）
            else if (frame == 24)
                ui_->SetKeyChordOverride((int)ImGuiMod_Ctrl, (int)ImGuiKey_Delete);
            else if (frame == 26) delOk = ctx_.ActiveScene().AliveCount() == uiBaseCount;
            else if (frame == 28)
                ui_->SetKeyChordOverride((int)ImGuiMod_Ctrl, (int)ImGuiKey_Z);
            else if (frame == 30)
                dupUndoOk = ctx_.ActiveScene().AliveCount() == uiBaseCount + uiTargetSubtree;
            else if (frame == 32)
                ui_->SetKeyChordOverride((int)ImGuiMod_Ctrl, (int)ImGuiKey_Z);
            else if (frame == 34)
                dupUndoOk = dupUndoOk && ctx_.ActiveScene().AliveCount() == uiBaseCount;
            else if (frame == 36)
                ui_->SetKeyChordOverride((int)ImGuiMod_Ctrl, (int)ImGuiKey_Y);
            else if (frame == 38)
                dupRedoOk = ctx_.ActiveScene().AliveCount() == uiBaseCount + uiTargetSubtree;
            else if (frame == 40)
                ui_->SetKeyChordOverride((int)ImGuiMod_Ctrl, (int)ImGuiKey_Y);
            else if (frame == 42)
                delRedoOk = ctx_.ActiveScene().AliveCount() == uiBaseCount;
            else if (frame == 43) {
                // 结构轨 Undo/Redo = 整场景 JSON 重载（实体全重建、句柄版本全变）
                // ——后续段（scrub/挂父子）必须用新句柄
                uiTarget = ecs::Entity::Null();
                ctx_.ActiveScene().Each([&](ecs::Entity e) {
                    const ecs::Meta* m = ctx_.ActiveScene().TryGet<ecs::Meta>(e);
                    if (m && std::strcmp(m->tag, "Player") == 0) uiTarget = e;
                });
            }
            // ---- D. label-scrub：拖 Transform2D.rot 名字 +28px（0.5°/px = +14°）→
            //         断言值与 Undo 往返（属性轨 = 本轮修好的提交顺序）----
            else if (frame == 44) {
                ctx_.Select(uiTarget, false);
            } else if (frame == 46) {
                uiPt0 = rectCenter("Transform2D.rot");
                if (uiPt0.x > -1.0e8f) hold(uiPt0);
            } else if (frame >= 47 && frame <= 53) {
                uiPt1 = Vec2{uiPt0.x + (float)(frame - 46) * 4.0f, uiPt0.y};
                hold(uiPt1);
            } else if (frame == 54) {
                release(uiPt1);
            } else if (frame == 56) {
                uiRot1 = ctx_.ActiveScene().Get<ecs::Transform2D>(uiTarget).rot;
                const float deltaDeg = (uiRot1 - uiRot0) * 57.29577951f;
                scrubOk = std::fabs(deltaDeg - 14.0f) < 3.0f;
            } else if (frame == 58)
                ui_->SetKeyChordOverride((int)ImGuiMod_Ctrl, (int)ImGuiKey_Z);
            else if (frame == 60) {
                const float back = ctx_.ActiveScene().Get<ecs::Transform2D>(uiTarget).rot;
                scrubUndoOk = std::fabs(back - uiRot0) < 1.0e-4f;
            }
            // ---- E. Ctrl+S（路径已在帧 4 落定 → 直存不弹窗）----
            else if (frame == 62) {
                saveOk = ctx_.dirty; // scrub 的编辑已置脏（undo 也保持 dirty，简化语义）
                ui_->SetKeyChordOverride((int)ImGuiMod_Ctrl, (int)ImGuiKey_S);
            } else if (frame == 64) {
                std::error_code ec;
                saveOk = saveOk && !ctx_.dirty &&
                         std::filesystem::is_regular_file(
                             std::filesystem::path(ctx_.ScenePath()), ec);
            }
            // ---- F. Ctrl+P Play 往返 ----
            else if (frame == 66)
                ui_->SetKeyChordOverride((int)ImGuiMod_Ctrl, (int)ImGuiKey_P);
            else if (frame == 68) {
                playOk = ctx_.Playing();
            } else if (frame == 70)
                ui_->SetKeyChordOverride((int)ImGuiMod_Ctrl, (int)ImGuiKey_P);
            else if (frame == 72) {
                stopOk = !ctx_.Playing();
            } else if (frame == 74) {
                // ExitPlay 按快照重建了场景（实体全部重建）——句柄重找，后续段有效
                uiTarget = uiGate = ecs::Entity::Null();
                ctx_.ActiveScene().Each([&](ecs::Entity e) {
                    const ecs::Meta* m = ctx_.ActiveScene().TryGet<ecs::Meta>(e);
                    if (!m) return;
                    if (std::strcmp(m->tag, "Player") == 0) uiTarget = e;
                    if (std::strcmp(m->tag, "Gate") == 0) uiGate = e;
                });
            }
            // ---- G. Hierarchy 点击选中 + F2 重命名（打字 + Enter）----
            else if (frame == 76) {
                uiPt0 = rectCenter("hier.Gate");
                if (uiPt0.x > -1.0e8f) hold(uiPt0);
            } else if (frame == 77) {
                release(uiPt0);
            } else if (frame == 79) {
                gselOk = ctx_.Primary() == uiGate;
            } else if (frame == 80) {
                ui_->SetKeyTapOverride(ImGuiKey_F2);
            } else if (frame == 82) {
                ui_->SetTextOverride("Gate2");
            } else if (frame == 84) {
                ui_->SetKeyTapOverride(ImGuiKey_Enter);
            } else if (frame == 86) {
                renameOk = !uiGate.IsNull() &&
                           std::strcmp(ctx_.ActiveScene().Get<ecs::Meta>(uiGate).tag,
                                       "Gate2") == 0;
            } else if (frame == 88)
                ui_->SetKeyChordOverride((int)ImGuiMod_Ctrl, (int)ImGuiKey_Z);
            else if (frame == 90)
                renameUndoOk = std::strcmp(ctx_.ActiveScene().Get<ecs::Meta>(uiGate).tag,
                                           "Gate") == 0;
            // ---- H. 行拖拽挂父子（Gate 拖到 Player 上 → Undo 摘回）----
            // 压 8 帧动程 + 目标处停 1 帧（DnD 激活 = 阈值 + 移动帧；过短偶发不激活）
            else if (frame == 92) {
                uiPt0 = rectCenter("hier.Gate");
                if (uiPt0.x > -1.0e8f) ui_->SetInputOverride(uiPt0.x, uiPt0.y, -1);
            } else if (frame == 93) {
                uiPt0 = rectCenter("hier.Gate");
                uiPt1 = rectCenter("hier.Player");
                // x+24 偏移按下：G 段刚点过同一行（注入帧距 < 双击时限），原点按下
                // 会被 ImGui 判成双击触发重命名行，行布局整体位移（真人手速不会）
                if (uiPt0.x > -1.0e8f && uiPt1.x > -1.0e8f)
                    hold(Vec2{uiPt0.x + 24.0f, uiPt0.y});
                uiPt0.x += 24.0f;
            } else if (frame >= 94 && frame <= 100) {
                const float t = std::min<float>((float)(frame - 93) / 7.0f, 1.0f);
                hold(Vec2{uiPt0.x + (uiPt1.x - uiPt0.x) * t,
                          uiPt0.y + (uiPt1.y - uiPt0.y) * t});
                if (frame == 98 && std::getenv("LEMON_SMOKE_UI_DEBUG"))
                    std::printf("[smoke-ui dbg] drag98 dnd=%d activeId=%llx\n",
                                ImGui::GetCurrentContext()->DragDropActive ? 1 : 0,
                                (unsigned long long)ImGui::GetCurrentContext()->ActiveId);
            } else if (frame == 101) {
                // 松手前必须重读目标行现位：拖拽启动会触发行布局变化，用 f93
                // 时的旧坐标会松开在源行上（SourceId==id 被拒，永不投递）
                uiPt1 = rectCenter("hier.Player");
                hold(uiPt1);
            } else if (frame == 102) {
                release(uiPt1);
            } else if (frame == 103 && std::getenv("LEMON_SMOKE_UI_DEBUG")) {
                const ecs::Hierarchy* h = ctx_.ActiveScene().TryGet<ecs::Hierarchy>(uiGate);
                std::printf("[smoke-ui dbg] f=103 parent=%lld expect=%lld gateAlive=%d\n",
                            h ? (long long)(h->parent.id & 0xFFFFFFFF) : -1,
                            (long long)(uiTarget.id & 0xFFFFFFFF),
                            ctx_.ActiveScene().Alive(uiGate) ? 1 : 0);
            } else if (frame == 104) {
                const ecs::Hierarchy* h = ctx_.ActiveScene().TryGet<ecs::Hierarchy>(uiGate);
                parentOk = h && h->parent == uiTarget;
            } else if (frame == 106)
                ui_->SetKeyChordOverride((int)ImGuiMod_Ctrl, (int)ImGuiKey_Z);
            else if (frame == 108) {
                const ecs::Hierarchy* h = ctx_.ActiveScene().TryGet<ecs::Hierarchy>(uiGate);
                parentUndoOk = !h || h->parent.IsNull();
            }
            // ---- 诊断（LEMON_SMOKE_UI_DEBUG=1）：关键帧 dump 登记表与 dock 态 ----
            else if (frame == 45 || frame == 55 || frame == 57 || frame == 65 ||
                     frame == 71 || frame == 97 || frame == 107 || frame == 109 ||
                     frame == 114 || frame == 145 || frame == 150) {
                if (std::getenv("LEMON_SMOKE_UI_DEBUG")) {
                    ImVec2 mn, mx;
                    const bool hasRot = testhooks::Find("Transform2D.rot", mn, mx);
                    ImGuiContext* gctx = ImGui::GetCurrentContext();
                    const char* colOpen = "?";
                    for (auto& pe : panels_.Entries())
                        if (std::strcmp(pe.panel->Name(), "Console") == 0)
                            colOpen = pe.open ? "1" : "0";
                    const ImGuiWindow* cw = ImGui::FindWindowByName("Console");
                    std::printf(
                        "[smoke-ui dbg] f=%d folder.sub=%s rot.label=%s count=%u undoRec=%d "
                        "undo=%d redo=%d dnd=%d primary=%lld target=%lld rot=%.4f "
                        "mouse=(%.0f,%.0f) down=%d dir='%s' collapse=%s con.open=%s "
                        "con.active=%d con.skip=%d con.tabVis=%d\n",
                        (int)frame,
                        testhooks::Find("assets.folder.sub", mn, mx) ? "Y" : "N",
                        hasRot ? "Y" : "N", ctx_.ActiveScene().AliveCount(),
                        (int)ctx_.Undo().Records().size(), ctx_.Undo().CanUndo(),
                        ctx_.Undo().CanRedo(), gctx->DragDropActive ? 1 : 0,
                        (long long)(ctx_.Primary().id & 0xFFFFFFFF),
                        (long long)(uiTarget.id & 0xFFFFFFFF),
                        !ctx_.ActiveScene().Has<ecs::Transform2D>(uiTarget)
                            ? -99.0f
                            : ctx_.ActiveScene().Get<ecs::Transform2D>(uiTarget).rot,
                        ImGui::GetIO().MousePos.x, ImGui::GetIO().MousePos.y,
                        ImGui::GetIO().MouseDown[0] ? 1 : 0,
                        assetPanel_ ? assetPanel_->CurrentDir().c_str() : "?",
                        testhooks::Find("console.collapse", mn, mx) ? "Y" : "N", colOpen,
                        cw ? (cw->Active ? 1 : 0) : -1, cw ? (cw->SkipItems ? 1 : 0) : -1,
                        cw ? (cw->DockTabIsVisible ? 1 : 0) : -1);
                    if (frame == 107 || frame == 145) {
                        ctx_.ActiveScene().Each([&](ecs::Entity e) {
                            const ecs::Meta* m = ctx_.ActiveScene().TryGet<ecs::Meta>(e);
                            std::printf("[smoke-ui dbg]   ent=%lld tag='%s'\n",
                                        (long long)(e.id & 0xFFFFFFFF),
                                        m ? m->tag : "(none)");
                        });
                    }
                    if (frame == 107 || frame == 145) {
                        if (ImGuiWindow* w = ImGui::FindWindowByName("Console")) {
                            std::printf("[smoke-ui dbg]   Console pos=(%.0f,%.0f) "
                                        "tabVisible=%d node=%d\n",
                                        w->Pos.x, w->Pos.y, w->DockTabIsVisible ? 1 : 0,
                                        w->DockNode ? 1 : 0);
                            if (w->DockNode && w->DockNode->TabBar) {
                                ImGuiTabBar* tb = w->DockNode->TabBar;
                                for (int i = 0; i < tb->Tabs.size(); ++i) {
                                    ImGuiTabItem& tab = tb->Tabs[i];
                                    std::printf("[smoke-ui dbg]   tab[%d]='%s' id=%llx "
                                                "off=%.1f w=%.1f\n",
                                                i, ImGui::TabBarGetTabName(tb, &tab),
                                                (unsigned long long)tab.ID, tab.Offset,
                                                tab.Width);
                                }
                                ImGuiContext* gc = ImGui::GetCurrentContext();
                                std::printf("[smoke-ui dbg]   tabId=%llx nextSel=%llx "
                                            "sel=%llx hoveredWin='%s'\n",
                                            (unsigned long long)w->TabId,
                                            (unsigned long long)tb->NextSelectedTabId,
                                            (unsigned long long)tb->SelectedTabId,
                                            gc->HoveredWindow ? gc->HoveredWindow->Name
                                                              : "(null)");
                            }
                        }
                    }
                }
            }
            // ---- I. 子目录文件夹进入 + 面包屑返回 ----
            // Assets 是 dock 标签页之一：非活动标签的面板体不绘制（登记点不存在），
            // 先切 Assets 为活动标签（ImGui 原生 FocusWindow，同真人点标签）
            else if (frame == 110) {
                if (ImGuiWindow* w = ImGui::FindWindowByName("Assets"))
                    ImGui::FocusWindow(w);
            } else if (frame == 112) {
                uiPt0 = rectCenter("assets.folder.sub");
                if (uiPt0.x > -1.0e8f) hold(uiPt0);
            } else if (frame == 113) {
                release(uiPt0);
            } else if (frame == 115) {
                folderOk = assetPanel_ && assetPanel_->CurrentDir() == "Assets/sub";
            } else if (frame == 117) {
                uiPt0 = rectCenter("crumbAssets");
                if (uiPt0.x > -1.0e8f) hold(uiPt0);
            } else if (frame == 118) {
                release(uiPt0);
            } else if (frame == 120) {
                crumbOk = assetPanel_ && assetPanel_->CurrentDir().empty();
            }
            // ---- J. 命名布局：combo → 保存… → 模态打字 + Enter → 文件存在；
            //         再切回默认布局 ----
            else if (frame == 122) {
                uiPt0 = rectCenter("layout.combo");
                if (uiPt0.x > -1.0e8f) ui_->SetInputOverride(uiPt0.x, uiPt0.y, -1);
            } else if (frame == 123) {
                uiPt0 = rectCenter("layout.combo");
                if (uiPt0.x > -1.0e8f) hold(uiPt0);
            } else if (frame == 124) {
                release(uiPt0);
            } else if (frame == 126) {
                uiPt0 = rectCenter("layout.item.save");
                if (uiPt0.x > -1.0e8f) hold(uiPt0);
            } else if (frame == 127) {
                release(uiPt0);
            } else if (frame == 129) {
                uiPt0 = rectCenter("layout.nameInput");
                if (uiPt0.x > -1.0e8f) hold(uiPt0);
            } else if (frame == 130) {
                release(uiPt0);
            } else if (frame == 131) {
                ui_->SetTextOverride("ui");
            } else if (frame == 133) {
                ui_->SetKeyTapOverride(ImGuiKey_Enter);
            } else if (frame == 135) {
                std::error_code ec;
                layoutOk =
                    std::filesystem::is_regular_file(".lemon/editor/layouts/ui.ini", ec);
            } else if (frame == 137) {
                uiPt0 = rectCenter("layout.combo");
                if (uiPt0.x > -1.0e8f) hold(uiPt0);
            } else if (frame == 138) {
                release(uiPt0);
            } else if (frame == 140) {
                uiPt0 = rectCenter("layout.item.default");
                if (uiPt0.x > -1.0e8f) hold(uiPt0);
            } else if (frame == 141) {
                release(uiPt0);
            }
            // ---- K. Console 标签页点击 + Collapse 开关（开关态无状态可断——
            //         断言可点击/不崩；视觉效果由截图归档）----
            else if (frame == 143) {
                // 切到 Console 标签：ImGui 原生 FocusWindow（对 dock 窗口 = 选中其
                // 标签，与真人点标签同语义）。物理点击 dock 标签在注入管线里
                // 与真实鼠标事件竞争，无法稳定命中——此处不改被测面板逻辑。
                if (ImGuiWindow* w = ImGui::FindWindowByName("Console"))
                    if (!w->DockTabIsVisible) ImGui::FocusWindow(w);
            } else if (frame == 147) {
                ImVec2 mn, mx;
                if (testhooks::Find("console.collapse", mn, mx)) {
                    uiPt0 = Vec2{(mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f};
                    hold(uiPt0);
                } else {
                    uiPt0 = Vec2{-1.0e9f, -1.0e9f}; // 开关没画出来 = 标签/面板体未就位
                }
            } else if (frame == 148) {
                release(uiPt0);
            } else if (frame == 151) {
                // 收紧：开关被找到并点击 = 标签已切 + 面板体已绘制 + 控件可点，缺一不可
                // （f151 而非 f150——诊断块占 f150，同链 else-if 会被它截胡）
                consoleOk = uiPt0.x > -1.0e8f;
            }
            // ---- 总裁决 ----
            else if (frame == 154 && !uiVerdictDone) {
                uiVerdictDone = true;
                uiAllOk = toolsOk && selOk && dupOk && delOk && dupUndoOk && dupRedoOk &&
                          delRedoOk && scrubOk && scrubUndoOk && saveOk && playOk && stopOk &&
                          gselOk && renameOk && renameUndoOk && parentOk && parentUndoOk &&
                          folderOk && crumbOk && layoutOk && consoleOk;
                std::printf("[lemon] smoke-ui: tools=%d sel=%d dup=%d del=%d dupZ=%d "
                            "dupUndo=%d delRedo=%d scrub=%.1fdeg/%d/%d save=%d play=%d/%d "
                            "gsel=%d rename=%d/%d parent=%d/%d dir=%d/%d layout=%d "
                            "console=%d => %s\n",
                            toolsOk, selOk, dupOk, delOk, dupUndoOk, dupRedoOk, delRedoOk,
                            (uiRot1 - uiRot0) * 57.29577951f, scrubOk, scrubUndoOk, saveOk,
                            playOk, stopOk, gselOk, renameOk, renameUndoOk, parentOk,
                            parentUndoOk, folderOk, crumbOk, layoutOk, consoleOk,
                            uiAllOk ? "OK" : "FAIL");
            }
        }

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
            if (acq.deviceLost || !device_->RecreateSwapchain()) continue;
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
        // 批③d-1：smoke-template 层序三拍捕获（动态时点——证据块状态机置请求位）。
        // 帧序：证据块（渲染后）置位 → 次帧本块录 gameRT → 同帧尾证据块取回数色
        if (launch.smokeTemplate && g_tplCapReq) {
            g_tplCapReq = false;
            g_tplCapPending = true;
            cl.DebugRecordCapture();
            if (viewport_->GameRenderTarget().IsValid())
                cl.DebugRecordTextureCapture(viewport_->GameRenderTarget());
        }

        bool needRe = false, lost = false;
        device_->EndFrameAndPresent(needRe, lost);
        bPresent = BenchClock::now(); // 段界：present（提交+呈现+可能的先前帧围栏等待）
        if (lost || needRe) {
            if (lost || !device_->RecreateSwapchain()) continue;
        }
        viewport_->AdvanceFrame();
        // 终验 fps 统计（§6 #6：判据场景 Play ≥45fps；预热 60 帧与换装窗口 90 帧剔除
        // ——dotnet build 同步阻塞主线程属换装耗时，不计帧率口径）
        if (launch.finalTest && ctx_.Playing() && frame > 60 &&
            frame - finalReloadFrame_ > 90) {
            const float f = ImGui::GetIO().Framerate;
            if (f > 1.0f && f < finalPlayMinFps_) finalPlayMinFps_ = f;
        }
        // M5 批③ smoke-anim 证据采样：clip 命中表 → curFrame 推进 + spriteId 落切片区间
        if (launch.smokeAnim && ctx_.Playing() && frame > 5) {
            const AssetEntry* sh[2] = {ctx_.Assets().FindByGuid(kAnimSheetGuid),
                                       ctx_.Assets().FindByGuid(kYamiHeroSheetGuid)};
            ecs::Scene& ps = ctx_.ActiveScene();
            // M6a 批① fx 通道注入（frame 30 一次）：飘字 + 血条（渲染链不属断言依赖，
            // 断言 = 通道计数与内容——Simulate/产包数学在 engine-tests 覆盖）
            if (frame == 30) {
                ecs::FxChannel& fx = ctx_.ActiveWorld().Fx();
                fx.PopupText("12", 640.0f, 540.0f, 0xFF5060F0u);
                ps.View<ecs::Animator2D>().each([&](auto ent, ecs::Animator2D& a) {
                    if (a.clipId == (uint32_t)kAnimClipGuid)
                        fx.Bar(ecs::Scene::FromEntt(ent).id, 0.5f);
                });
            }
            // M6a 批①切段链（frame 60 一次）：受击组合拳字段等价写（= Anim.Play(hit,
            // loop:false) + Anim.Queue(walk)；冒烟侧无脚本——编辑器域直写 play 场景）
            if (frame == 60) {
                ps.View<ecs::Animator2D>().each([&](auto ent, ecs::Animator2D& a) {
                    if (a.clipId != (uint32_t)kAnimClipGuid) return;
                    a.clipId = (uint32_t)kAnimHitClipGuid;
                    a.loop = 0;
                    a.time = 0.0f;
                    a.playOnStart = 1;
                    a.nextClipId = (uint32_t)kAnimClipGuid;
                    a.nextLoop = 1;
                    a.fadeRemain = -1.0f;
                });
            }
            ps.View<ecs::Animator2D>().each([&](auto ent, ecs::Animator2D& a) {
                const int slot = a.clipId == (uint32_t)kAnimClipGuid      ? 0
                                 : a.clipId == (uint32_t)kYamiHeroClipGuid ? 1
                                                                           : -1;
                if (slot == 0 && smokeQueueHit) smokeQueueBack = true; // hit 后回 walk
                if (a.clipId == (uint32_t)kAnimHitClipGuid) smokeQueueHit = true;
                if (slot < 0) return;
                if (a.curFrame > smokeAnimMax[slot]) smokeAnimMax[slot] = a.curFrame;
                if (const ecs::SpriteRenderer* sr =
                        ps.TryGet<ecs::SpriteRenderer>(ecs::Scene::FromEntt(ent))) {
                    const AssetEntry* e = sh[slot];
                    if (e && e->Sliced() && sr->spriteId >= e->sliceBase &&
                        sr->spriteId < e->sliceBase + e->sliceCount)
                        smokeAnimSlice[slot] = true;
                }
            });
            if (!smokeFxSeen && frame > 30) {
                const ecs::FxChannel& fx = ctx_.ActiveWorld().Fx();
                smokeFxSeen = fx.TextCount() == 1 && fx.BarCount() >= 1 &&
                              std::strcmp(fx.TextAt(0).text, "12") == 0;
            }
        }
        // M5 批④ smoke-template 证据采样：HUD 四要素行齐 / 存档载入（best=123 回显）/
        // 波次行 / 三选一卡片链（出现 → 注入选择（模拟数字键 1）→ 消费后隐藏）
        if (launch.smokeTemplate && ctx_.Playing() && frame > 5) {
            // M6a 批② T4：三表快照断言（EnterPlay 建、Start 消费——行数 = 列头+数据行）
            if (!g_tplTablesOk) {
                auto rowsOf = [&](uint64_t guid) {
                    const auto* t = ctx_.ActiveWorld().Tables().Find((uint32_t)guid);
                    return t ? (int)t->size() : -1;
                };
                g_tplTablesOk = rowsOf(vs_template::kWeaponsTab) == 4 &&
                                rowsOf(vs_template::kUpgradesTab) == 7 &&
                                rowsOf(vs_template::kBalanceTab) == 2;
            }
            // 批③d-1：HUD 文档化断言（原 RtUi 行探针随迁）——六要素文本经
            // TryGetElementText（DOM 读数，零渲染依赖）；best 含 "123" = 预置存档
            // → C# 读回 → HUD 回显
            static const char* const kHudDoc = "Assets/UI/hud.rml";
            if (gameUi_) {
                char hudTxt[96] = {};
                auto hudHas = [&](const char* id) {
                    return gameUi_->TryGetElementText(kHudDoc, id, hudTxt,
                                                      sizeof hudTxt) &&
                           hudTxt[0] != '\0';
                };
                if (!g_tplHudDocOk) {
                    char t4[4][96] = {};
                    bool ok = true;
                    const char* const ids[4] = {"hp-text", "xp-text", "time", "kills"};
                    for (int i = 0; i < 4; ++i)
                        ok = ok && gameUi_->TryGetElementText(kHudDoc, ids[i], t4[i],
                                                              sizeof t4[i]) &&
                             t4[i][0] != '\0';
                    g_tplHudDocOk = ok;
                }
                if (!g_tplBestLoaded && hudHas("best") && std::strstr(hudTxt, "123"))
                    g_tplBestLoaded = true;
                if (!g_tplWaveRow && hudHas("wave")) g_tplWaveRow = true;
                // T8 后修②：条改原生 <progress>（fill = 引擎定位非 DOM 子元素，
                // 盒探针不可达）——断言换轨双证：① 轨道盒 = 120dp×10dp×ratio
                // （布局在场；display/规则丢失 → 0 尺寸）② value 属性回读 =
                // 文本行同帧数值（C# SetAttr 接线落地；缺属性 = 探针 false）
                if (!g_tplHudBarBox) {
                    char hpT[96] = {}, xpT[96] = {};
                    int hc = -1, hm = -1, xc = -1, xm = -1;
                    if (gameUi_->TryGetElementText(kHudDoc, "hp-text", hpT, sizeof hpT))
                        std::sscanf(hpT, "HP %d/%d", &hc, &hm);
                    if (gameUi_->TryGetElementText(kHudDoc, "xp-text", xpT, sizeof xpT))
                        std::sscanf(xpT, "LV %*d %d/%d", &xc, &xm);
                    const float ratio = gameUi_->DpRatio();
                    auto barOk = [&](const char* id, int cur, int max) {
                        float w = 0, h = 0, v = -1.f;
                        if (cur <= 0 || max <= 0 || ratio <= 0.f) return false;
                        if (!gameUi_->TryGetElementBox(kHudDoc, id, &w, &h)) return false;
                        if (std::fabs(w - 120.f * ratio) > 3.f ||
                            std::fabs(h - 10.f * ratio) > 2.f)
                            return false;
                        if (!gameUi_->TryGetElementAttrF(kHudDoc, id, "value", &v))
                            return false;
                        return std::fabs(v - (float)cur) <= 0.51f;
                    };
                    if (barOk("hp-bar", hc, hm) && barOk("xp-bar", xc, xm))
                        g_tplHudBarBox = true;
                }
            }
            // M6a 批①：受击切段 + fx 通道采样（PlayerCombat.OnHit 写——脚本面端到端）
            if (!g_tplMobHitClip) {
                ctx_.ActiveScene().View<ecs::Animator2D>().each([&](auto, ecs::Animator2D& a) {
                    if (a.clipId == (uint32_t)vs_template::kMonsterHitClip)
                        g_tplMobHitClip = true;
                });
            }
            if (!g_tplFxText || !g_tplFxBar) {
                const lemon::ecs::FxChannel& fx = ctx_.ActiveWorld().Fx();
                if (fx.TextCount() > 0) g_tplFxText = true;
                if (fx.BarCount() > 0) g_tplFxBar = true;
            }
            // 批③d-1：卡片链文档化（原 RtUiCards 探针随迁）。升级卡 3 条 / 死亡对话
            // 框 1 条（key "ok"）；选择 = **引擎直灌点击**（SetPointer + 两帧
            // down/up + 指针保持窗——绕 ImGui 悬停链：template 会话里 GameView
            // IsItemHovered 被压制（mouse 落位正确仍恒假，根因未明），ImGui→RmlUi
            // 路由链由 smoke-uirml 的 60/61 帧点击独立覆盖）：UiEvent → #16 →
            // C# 消费 → Hide。三帧状态机：定位（下一帧 Update 建悬停）→ down → up
            static const char* const kCardsDoc = "Assets/UI/cards.rml";
            const int cardsN =
                gameUi_ ? gameUi_->ContainerItemCount(kCardsDoc, "cards") : -1;
            const bool cardsShown =
                gameUi_ && gameUi_->IsDocumentShown(kCardsDoc);
            if (cardsN == 3) g_tplCardsSeen = true;
            if (g_tplClickCooldown > 0) --g_tplClickCooldown;
            if (g_tplClickPhase == 1) { // down（本帧 Update 已按保持指针建好悬停）
                gameUi_->ProcessMouseButton(0, true);
                g_tplClickPhase = 2;
            } else if (g_tplClickPhase == 2) { // up（RmlUi click 边沿）
                gameUi_->ProcessMouseButton(0, false);
                g_tplClickPhase = 0;
                g_tplPointerHold = false;
                g_tplClickCooldown = 15; // 事件往返（→UiEvent→#16→C#→Hide）余量
                g_tplPicked = true;
            } else if (gameUi_ && cardsShown && cardsN >= 1 &&
                       g_tplClickCooldown == 0 && frame > 120) {
                // 可见卡片逐 key 试探（u0..u5 = 升级池 id；ok = 死亡对话框）——
                // TryGetItemCenter 只对在场条目返回中心
                static const char* const kCardKeys[7] = {"u0", "u1", "u2", "u3",
                                                         "u4", "u5", "ok"};
                for (const char* key : kCardKeys) {
                    float cx, cy;
                    if (gameUi_->TryGetItemCenter(kCardsDoc, "cards", key, &cx, &cy)) {
                        g_tplClickX = cx;
                        g_tplClickY = cy;
                        gameUi_->SetPointer((int)cx, (int)cy, true); // 保持至 up 帧
                        g_tplPointerHold = true;
                        g_tplClickPhase = 1;
                        break;
                    }
                }
            }
            if (g_tplPicked && !cardsShown) g_tplCardsHidden = true; // C# 消费 → Hide
            // 批③d-1：层序三拍（cards 文档的 scrim 压暗 HUD 文字 = cards 在上的像素
            // 级证明——若层序颠倒 scrim 盖不住 HUD）。计数 #f0f0f0 近色（time 行白字）。
            // 帧序：本钩置请求位 → 次帧渲染块录 gameRT → 同帧尾本钩取回
            if (g_tplCapPending) {
                std::vector<uint8_t> rt;
                uint32_t rw = 0, rh = 0;
                if (device_->DebugFetchTextureCapture(rt, rw, rh)) {
                    // HUD 文字区（ratio 派生——画布尺寸跨会话可变，px 写死不健壮）：
                    // 左上 (12dp,10dp) 起 ~230dp × ~110dp（六行纵列，卡片面板居中
                    // 不入区——只有 scrim 会盖进来）。计 #f0f0f0 近色（time 行白字）
                    const float ratio = gameUi_ ? gameUi_->DpRatio() : 1.0f;
                    const uint32_t x1 = (uint32_t)(242.0f * ratio);
                    const uint32_t y1 = (uint32_t)(120.0f * ratio);
                    int n = 0;
                    for (uint32_t y = 0; y < rh && y < y1; ++y)
                        for (uint32_t x = 0; x < rw && x < x1; ++x) {
                            const uint8_t* p = &rt[((size_t)y * rw + x) * 4];
                            if (std::abs((int)p[0] - 240) <= 30 &&
                                std::abs((int)p[1] - 240) <= 30 &&
                                std::abs((int)p[2] - 240) <= 30)
                                ++n;
                        }
                    g_tplCapPix = n;
                }
                g_tplCapPending = false;
            }
            if (g_tplLayerStage == 0 && frame > 100) { // 基线：HUD 已上屏、卡片未到
                g_tplCapReq = true;
                g_tplLayerStage = 1;
            } else if (g_tplLayerStage == 1 && g_tplCapPix >= 0) {
                g_tplHudPixN0 = g_tplCapPix;
                g_tplCapPix = -1;
                g_tplLayerStage = 2;
            } else if (g_tplLayerStage == 2 && cardsShown) {
                g_tplCapReq = true;
                g_tplLayerStage = 3;
            } else if (g_tplLayerStage == 3 && g_tplCapPix >= 0) {
                g_tplHudPixDuring = g_tplCapPix;
                g_tplCapPix = -1;
                g_tplLayerStage = 4;
            } else if (g_tplLayerStage == 4 && g_tplPicked && !cardsShown) {
                g_tplCapReq = true;
                g_tplLayerStage = 5;
            } else if (g_tplLayerStage == 5 && g_tplCapPix >= 0) {
                g_tplHudPixAfter = g_tplCapPix;
                g_tplCapPix = -1;
                g_tplLayerStage = 6;
            }
            // 批③d-1 催命：文档化卡片的选择有几帧事件往返（原 RtUi 直写同帧），
            // 局内时序整体后移 → 波 2 刷新走近的余量变薄（实测一轮贴边一轮超时）。
            // 武装后 50 帧仍未死 = 把追击怪贴脸（保 Hazard 接触真实路径，只省走路）
            if (frame >= 2150 && !g_tplDeathSeen) {
                Vec2 ppos{0, 0};
                bool got = false;
                ctx_.ActiveScene().View<scripting::ScriptBox>().each(
                    [&](auto ent, scripting::ScriptBox&) {
                        const ecs::Entity e = ecs::Scene::FromEntt(ent);
                        if (const ecs::Transform2D* t =
                                ctx_.ActiveScene().TryGet<ecs::Transform2D>(e)) {
                            ppos = t->pos;
                            got = true;
                        }
                    });
                if (got)
                    ctx_.ActiveScene().View<ecs::Transform2D, ecs::Chase>().each(
                        [&](auto, ecs::Transform2D& tf, ecs::Chase&) {
                            tf.pos = Vec2{ppos.x + 18.0f, ppos.y + 6.0f};
                        });
            }
            // 批④后修④死亡链回归：kDeathArm 帧起压血到 0.1 + 掐射击（站桩下
            // 自动炮火半路清怪、玩家碰不到怪——停火让怪群近身，Hazard 真路径击杀）
            // → 玩家脚本实体仍在场（View 命中 = 未被销毁）、flags bit0 未置（未被
            // 异常禁用）→ 点击复活 → 血回满 + 解冻 + 卡片文档隐藏 = 复活成功
            if (frame >= 2100 && !g_tplDeathArmed) {
                g_tplDeathArmed = true;
                ctx_.ActiveScene().View<scripting::ScriptBox>().each(
                    [&](auto ent, scripting::ScriptBox&) {
                        ecs::Entity e = ecs::Scene::FromEntt(ent);
                        if (ecs::Health* hp = ctx_.ActiveScene().TryGet<ecs::Health>(e))
                            hp->cur = 0.1f;
                        if (ecs::Shooter* sh = ctx_.ActiveScene().TryGet<ecs::Shooter>(e))
                            sh->interval = 3600.0f;
                    });
            }
            if (frame >= 2100 && !g_tplRevived) {
                bool anyScript = false;
                ctx_.ActiveScene().View<scripting::ScriptBox>().each(
                    [&](auto ent, scripting::ScriptBox& sb) {
                        anyScript = true;
                        for (uint32_t i = 0; i < sb.count; ++i) // 逐槽查禁用位（M6a 批⓪）
                            if (sb.slots[i].flags & scripting::kScriptFlagDisabled)
                                g_tplScriptOk = false;
                        const ecs::Health* hp = ctx_.ActiveScene().TryGet<ecs::Health>(
                            ecs::Scene::FromEntt(ent));
                        if (!hp) return;
                        if (hp->cur <= 0.0f) g_tplDeathSeen = true;
                        else if (g_tplDeathSeen && hp->cur >= hp->max &&
                                 ctx_.ActiveWorld().TimeScale() > 0.0f && !cardsShown)
                            g_tplRevived = true;
                    });
                if (!anyScript) g_tplScriptOk = false; // 脚本实体消失（销毁回归锚点）
            }
            if (frame % 60 == 0) { // 诊断快照（低频）：文档 HUD 位 + 场内分布
                std::snprintf(g_tplHudRows, sizeof g_tplHudRows, "doc=%d cards=%d",
                              g_tplHudDocOk ? 1 : 0, cardsN);
                ctx_.ActiveScene().View<ecs::Collectible>().each(
                    [](auto, ecs::Collectible&) { ++g_tplGems; });
                ctx_.ActiveScene().View<ecs::Chase>().each(
                    [](auto, ecs::Chase&) { ++g_tplMobs; });
            }
        }
        if (firstFrameMs < 0.0)
            firstFrameMs = std::chrono::duration<double, std::milli>(
                               std::chrono::steady_clock::now() - tColdStart)
                               .count();
        if ((launch.benchSurvivor || launch.benchScene) && ctx_.Playing() &&
            frame >= kBenchWarmup) {
            // M6a 批①：fx 饱和灌入（survivor 专属口径：验收④ = 池满最坏情形，
            // 256 飘字 + 128 血条每帧全量在场）。飘字确定性网格撒玩家周边（渲染
            // 视口内）；血条挂前 128 只动画怪（each 早退收集；怪被击杀 = 渲染侧
            // resolve 跳过、槽位 sticky 到期次帧收集补位——计数恒 128）
            if (launch.benchSurvivor) {
                ecs::FxChannel& fx = ctx_.ActiveWorld().Fx();
                for (uint32_t i = 0; i < ecs::FxChannel::kMaxTexts; ++i)
                    fx.PopupText("12", -300.0f + (float)(i % 16) * 40.0f,
                                 -300.0f + (float)(i / 16) * 40.0f, 0xFF5060F0u);
                uint32_t bars = 0;
                ctx_.ActiveScene().View<ecs::Animator2D>().each(
                    [&](auto ent, ecs::Animator2D&) {
                        if (bars >= ecs::FxChannel::kMaxBars) return;
                        fx.Bar(ecs::Scene::FromEntt(ent).id, 0.5f, 0xFF30B0F0u, 32.0f);
                        ++bars;
                    });
            }
            const auto segMs = [](BenchClock::time_point a, BenchClock::time_point b) {
                return std::chrono::duration<double, std::milli>(b - a).count();
            };
            const double segs[kSegN] = {
                segMs(benchT0, bPump),   segMs(bPump, bSim),  segMs(bSim, bUi0),
                segMs(bUi0, bUi1),       segMs(bUi1, bAcq),   segMs(bAcq, bScene),
                segMs(bScene, bUiDraw),  segMs(bUiDraw, bPresent),
            };
            if (!benchSimProfileZeroed) { // 测量窗口起点：sim 每系统计数清零
                benchSimProfileZeroed = true;
                ctx_.ActiveWorld().Pipeline().ZeroProfiles();
            }
            benchPumpSum += segs[0];
            benchSimSum += segs[1];
            benchGlueSum += segs[2];
            benchUiSum += segs[3];
            benchAcqSum += segs[4];
            benchSceneSum += segs[5];
            benchUiDrawSum += segs[6];
            benchPresentSum += segs[7];
            for (int i = 0; i < kSegN; ++i)
                if (segs[i] > benchSegMax[i]) {
                    benchSegMax[i] = segs[i];
                    benchSegMaxF[i] = frame;
                }
            const double ms = std::chrono::duration<double, std::milli>(
                                  std::chrono::steady_clock::now() - benchT0)
                                  .count();
            benchFrameSum += ms;
            if (ms > benchFrameMax) { // 最坏帧的八段快照（frameMax 归因原料）
                benchFrameMax = ms;
                for (int i = 0; i < kSegN; ++i) benchMaxSeg[i] = segs[i];
            }
            if (ms > 25.0) { // 尖刺帧（avg≈17，+45% 起）：分段和看集体偏向
                ++benchSpikeN;
                for (int i = 0; i < kSegN; ++i) benchSpikeSeg[i] += segs[i];
            }
            ++benchFrameN;
        }
        ++frame;
    }

    bool playVerified = true;
    double playEnterMs = 0, playExitMs = 0;
    uint32_t playAliveAtStop = 0;
    uint32_t benchTeam1Alive = 0, benchWavesStarted = 0;
    uint32_t benchAnimHit = 0, benchAnimTotal = 0;
    uint32_t benchFxTexts = 0, benchFxBars = 0; // 批① fx 饱和证据（停跑时通道计数）
    float benchPlayerHp = -1.0f; // Hazard 化证据（方案 A 批）：玩家（收集者）掉血 =
                                 // 万怪 Hazard tick 真实发生（<1e6 即证）
    if ((launch.benchSurvivor || launch.benchScene) && ctx_.Playing()) {
        benchPlayProfiles = ctx_.ActiveWorld().Pipeline().Profiles(); // ExitPlay 弃世界前留证
        if (launch.benchScene)
            playAliveAtStop = ctx_.ActiveScene().AliveCount();
        if (launch.benchSurvivor) { // 以下证据采集 = survivor 专属（bench-scene 只取 profiles + alive）
        ctx_.ActiveScene().View<ecs::XpProgress>().each([&](auto ent, ecs::XpProgress&) {
            if (const ecs::Health* hp = ctx_.ActiveScene().TryGet<ecs::Health>(
                    ecs::Scene::FromEntt(ent)))
                benchPlayerHp = hp->cur;
        });
        // 导演化证据（M5 批②）：waveIndex=已生效波数；team1 存活突破 Spawner 8000
        // 闸门即导演出生实证（两通道同队，闸门语义见 03 §8 修订注）
        ctx_.ActiveScene().View<ecs::Meta>().each([&](auto, ecs::Meta& m) {
            if (m.team == 1) ++benchTeam1Alive;
        });
        ctx_.ActiveScene().View<ecs::WaveDirector>().each(
            [&](auto, ecs::WaveDirector& w) { benchWavesStarted += w.waveIndex; });
        // 批① fx 饱和证据：停跑时通道计数（帧循环每帧灌满 → 期望 = 池容量）
        benchFxTexts = ctx_.ActiveWorld().Fx().TextCount();
        benchFxBars = ctx_.ActiveWorld().Fx().BarCount();
        // 动画化证据（M5 批③）：Animator2D 实体总数 + spriteId 落切片连号区间数
        // （帧映射每 tick 无条件写 → 命中 = 表达 + 切片解析全通；全数应命中）
        const AssetEntry* sh = ctx_.Assets().FindByGuid(kAnimSheetGuid);
        if (sh && sh->Sliced()) {
            ctx_.ActiveScene().View<ecs::Animator2D>().each([&](auto ent, ecs::Animator2D& a) {
                (void)a;
                ++benchAnimTotal;
                if (const ecs::SpriteRenderer* sr = ctx_.ActiveScene().TryGet<ecs::SpriteRenderer>(
                        ecs::Scene::FromEntt(ent)))
                    if (sr->spriteId >= sh->sliceBase && sr->spriteId < sh->sliceBase + sh->sliceCount)
                        ++benchAnimHit;
            });
        }
        } // survivor 专属证据到此
    }
    if (ctx_.Playing()) { // --play：跑满帧数后 Stop（恢复编辑世界）
        playAliveAtStop = ctx_.ActiveScene().AliveCount();
        playEnterMs = ctx_.LastEnterPlayMs();
        playVerified = StopPlay();
        playExitMs = ctx_.LastExitPlayMs();
    }

    // ---- 冒烟自检（§6 #13：退出码即判据）----
    int exitCode = 0;
    // --smoke-drag 裁决（M4.7c 交互回归）：链路分段计数 + 位移/旋转/缩放/缩放相机断言
    if (launch.smokeDrag) {
        if (scenePanel_)
            std::printf("[lemon] smoke-drag: press=%d armed=%d updates=%d "
                        "move=(%.1f,%.1f) rot=%.3frad(exp -1.571) "
                        "scale %.2f→%.2f leftΔ=%.1f zoom %.2f→%.2f selΔ=%.1fpx "
                        "slingΔ=(%.0f,%.0f) focusΔ=%.0f => %s\n",
                        scenePanel_->dbgPress_, scenePanel_->dbgArmed_, scenePanel_->dbgUpdates_,
                        dragDx, dragDy, rotAfter - rotBefore, scaleBefore, scaleAfter,
                        std::fabs(anchorLeft1 - anchorLeft0), zoomBefore, zoomAfter,
                        Length(Vec2{selScreen1.x - selScreen0.x, selScreen1.y - selScreen0.y}),
                        slingDx, slingDy, focusDelta, dragPassed ? "OK" : "FAIL");
        else
            std::printf("[lemon] smoke-drag: Scene 面板未找到 => FAIL\n");
        if (!dragPassed) exitCode = 1;
    }
    // --smoke-ui 裁决（裁决行已在帧 154 打印；此处只定退出码）
    if (launch.smokeUi && !uiAllOk) exitCode = 1;
    // --bench-survivor 裁决（M5 清障③；08 §3 判据：编辑器内 1 万怪 ≥45fps）
    if (launch.benchSurvivor) {
        const double avg = benchFrameN ? benchFrameSum / (double)benchFrameN : 0.0;
        const double fps = avg > 0.0 ? 1000.0 / avg : 0.0;
        const bool aliveOk = playAliveAtStop >= 10000;
        // 导演化批（M5 批②）：波次 ≥3 生效 + team1 突破 Spawner 8000 闸门
        const bool directorOk = benchWavesStarted >= 3 && benchTeam1Alive > 8000;
        // 动画化批（M5 批③）：万怪帧映射生效（全数命中切片区间）
        const bool animOk = benchAnimTotal >= 10000 && benchAnimHit == benchAnimTotal;
        // Hazard 化批（2026-09-24 方案 A）：玩家掉血 = Hazard tick 进压测口径
        const bool hazardOk = benchPlayerHp >= 0.0f && benchPlayerHp < 1'000'000.0f;
        // fx 批（M6a 批① 验收④）：飘字/血条开启（池满饱和渲染）——≥45fps 门槛不动的
        // 前提下通道饱和在场 = 表现层成本进压测口径
        const bool fxOk = benchFxTexts == ecs::FxChannel::kMaxTexts &&
                          benchFxBars == ecs::FxChannel::kMaxBars;
        const bool pass = aliveOk && avg > 0.0 && avg <= 1000.0 / 45.0 && directorOk &&
                          animOk && hazardOk && fxOk;
        const double segN = benchFrameN ? (double)benchFrameN : 1.0;
        std::printf("[bench-survivor] 分段avg ms: pump=%.2f sim=%.2f glue=%.2f ui=%.2f "
                    "acquire=%.2f scene=%.2f uidraw=%.2f present=%.2f | segSum=%.2f\n",
                    benchPumpSum / segN, benchSimSum / segN, benchGlueSum / segN,
                    benchUiSum / segN, benchAcqSum / segN, benchSceneSum / segN,
                    benchUiDrawSum / segN, benchPresentSum / segN,
                    (benchPumpSum + benchSimSum + benchGlueSum + benchUiSum + benchAcqSum +
                     benchSceneSum + benchUiDrawSum + benchPresentSum) /
                        segN);
        std::printf("[bench-survivor] frames=%u warmup=%u alive=%u stepAvg=%.2fms "
                    "frameAvg=%.2fms frameMax=%.2fms fps=%.0f present=IMMEDIATE(请求)"
                    " director(waves=%u teamAlive=%u/闸8000) anim(%u/%u 切片命中)"
                    " hazard(playerHp=%.0f<1e6 掉血实证)"
                    " fx(texts=%u bars=%u 饱和)"
                    " => %s\n",
                    (unsigned)frame, (unsigned)kBenchWarmup, playAliveAtStop,
                    benchSimSum / segN, avg,
                    benchFrameMax, fps, benchWavesStarted, benchTeam1Alive,
                    benchAnimHit, benchAnimTotal, benchPlayerHp,
                    benchFxTexts, benchFxBars,
                    pass ? "PASS" : "FAIL");
        // 性能批②①：sim 系统级分解（测量窗口 = 预热后 ZeroProfiles 起；avg=totalMs/runs）
        {
            std::vector<ecs::SystemProfile> rows;
            for (const ecs::SystemProfile& p : benchPlayProfiles)
                if (p.runs > 0) rows.push_back(p);
            std::sort(rows.begin(), rows.end(), [](const ecs::SystemProfile& a,
                                                   const ecs::SystemProfile& b) {
                return a.totalMs > b.totalMs;
            });
            double sysSum = 0.0;
            for (const ecs::SystemProfile& p : rows) sysSum += p.totalMs / (double)p.runs;
            std::printf("[bench-survivor] sim系统分解 (Σ=%.2fms vs seg sim=%.2fms):\n",
                        sysSum, benchSimSum / segN);
            for (const ecs::SystemProfile& p : rows)
                std::printf("    %-24s avg=%7.3fms max=%7.3fms runs=%llu\n", p.name,
                            p.totalMs / (double)p.runs, (double)p.maxMs,
                            (unsigned long long)p.runs);
        }
        // 性能批②②：尖刺归因——最坏帧八段快照 + 尖刺帧（>25ms）分段均值 + 每段 max
        {
            const char* segNames[kSegN] = {"pump", "sim", "glue", "ui",
                                           "acquire", "scene", "uidraw", "present"};
            std::printf("[bench-survivor] frameMax=%.2fms 帧八段:", benchFrameMax);
            for (int i = 0; i < kSegN; ++i) std::printf(" %s=%.2f", segNames[i], benchMaxSeg[i]);
            std::printf("\n");
            std::printf("[bench-survivor] 每段max:");
            for (int i = 0; i < kSegN; ++i)
                std::printf(" %s=%.2f@%llu", segNames[i], benchSegMax[i],
                            (unsigned long long)benchSegMaxF[i]);
            std::printf("\n");
            if (benchSpikeN > 0) {
                std::printf("[bench-survivor] 尖刺帧>25ms: %llu 个，其分段均值:",
                            (unsigned long long)benchSpikeN);
                for (int i = 0; i < kSegN; ++i)
                    std::printf(" %s=%.2f", segNames[i], benchSpikeSeg[i] / (double)benchSpikeN);
                std::printf("\n");
            } else {
                std::printf("[bench-survivor] 尖刺帧>25ms: 0 个\n");
            }
        }
        // 性能批②③：ui 段内部归因（探针在 HierarchyPanel，LEMON_BENCH_UI_PROBE 开）
        if (const UiPanelProbe hp = HierarchyPanelProbe(); hp.frames > 0)
            std::printf("[bench-survivor] ui段探针: hierarchy=%.2fms（占 ui %.0f%%，"
                        "LEMON_BENCH_UI_PROBE 口径含打点开销）\n",
                        hp.totalMs / (double)hp.frames,
                        benchUiSum > 0.0 ? 100.0 * (hp.totalMs / (double)hp.frames) /
                                               (benchUiSum / segN)
                                         : 0.0);
        if (!pass) exitCode = 1;
    }
    // --bench-scene 裁决（2026-09-25）：无场景特定判据——只报数，退出码恒 0
    //（测量工具；判读归调用方/README 口径）。分解块与 survivor 同构、前缀独立。
    if (launch.benchScene) {
        const double bAvg = benchFrameN ? benchFrameSum / (double)benchFrameN : 0.0;
        const double bFps = bAvg > 0.0 ? 1000.0 / bAvg : 0.0;
        const double bSegN = benchFrameN ? (double)benchFrameN : 1.0;
        std::printf("[bench-scene] 分段avg ms: pump=%.2f sim=%.2f glue=%.2f ui=%.2f "
                    "acquire=%.2f scene=%.2f uidraw=%.2f present=%.2f | segSum=%.2f\n",
                    benchPumpSum / bSegN, benchSimSum / bSegN, benchGlueSum / bSegN,
                    benchUiSum / bSegN, benchAcqSum / bSegN, benchSceneSum / bSegN,
                    benchUiDrawSum / bSegN, benchPresentSum / bSegN,
                    (benchPumpSum + benchSimSum + benchGlueSum + benchUiSum + benchAcqSum +
                     benchSceneSum + benchUiDrawSum + benchPresentSum) / bSegN);
        std::printf("[bench-scene] RESULT frames=%u warmup=%u alive=%u frameAvg=%.2fms "
                    "fps=%.0f => REPORT\n",
                    (unsigned)frame, (unsigned)kBenchWarmup, playAliveAtStop, bAvg, bFps);
        {
            std::vector<ecs::SystemProfile> rows;
            for (const ecs::SystemProfile& p : benchPlayProfiles)
                if (p.runs > 0) rows.push_back(p);
            std::sort(rows.begin(), rows.end(), [](const ecs::SystemProfile& a,
                                                   const ecs::SystemProfile& b) {
                return a.totalMs > b.totalMs;
            });
            double sysSum = 0.0;
            for (const ecs::SystemProfile& p : rows) sysSum += p.totalMs / (double)p.runs;
            std::printf("[bench-scene] sim系统分解 (Σ=%.2fms vs seg sim=%.2fms):\n",
                        sysSum, benchSimSum / bSegN);
            for (const ecs::SystemProfile& p : rows)
                std::printf("    %-24s avg=%7.3fms max=%7.3fms runs=%llu\n", p.name,
                            p.totalMs / (double)p.runs, (double)p.maxMs,
                            (unsigned long long)p.runs);
        }
        {
            const char* segNames[kSegN] = {"pump", "sim", "glue", "ui",
                                           "acquire", "scene", "uidraw", "present"};
            std::printf("[bench-scene] frameMax=%.2fms 帧八段:", benchFrameMax);
            for (int i = 0; i < kSegN; ++i) std::printf(" %s=%.2f", segNames[i], benchMaxSeg[i]);
            std::printf("\n");
            std::printf("[bench-scene] 每段max:");
            for (int i = 0; i < kSegN; ++i)
                std::printf(" %s=%.2f@%llu", segNames[i], benchSegMax[i],
                            (unsigned long long)benchSegMaxF[i]);
            std::printf("\n");
            if (benchSpikeN > 0) {
                std::printf("[bench-scene] 尖刺帧>25ms: %llu 个，其分段均值:",
                            (unsigned long long)benchSpikeN);
                for (int i = 0; i < kSegN; ++i)
                    std::printf(" %s=%.2f", segNames[i], benchSpikeSeg[i] / (double)benchSpikeN);
                std::printf("\n");
            } else {
                std::printf("[bench-scene] 尖刺帧>25ms: 0 个\n");
            }
        }
    }
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
        // M4.4 脚本链验收：--script + --play → SpawnerBehaviour 每帧刷怪（36 只）
        bool scriptOk = true;
        if (host_ && launch.playTest && !launch.finalTest) {
            scriptOk = playAliveAtStop > smokeSeeded_ + 10;
            std::printf("[lemon] editor-smoke script-spawn: playAlive=%u seeded=%u => %s\n",
                        playAliveAtStop, smokeSeeded_, scriptOk ? "OK" : "FAIL");
        }
        // M5 批③动画链验收：切片记账 + clip 建表 + 帧映射推进 + spriteId 落切片区间。
        // 程序化 4 帧表必验；yami hero-walk 素材在场（Samples 拷入项目）即连带验真链。
        // M6a 批①扩：切段链（Play(hit)+Queue(walk) → hit 在场 → 收尾回 walk）+ fx 通道。
        if (launch.smokeAnim) {
            const AssetEntry* sh = ctx_.Assets().FindByGuid(kAnimSheetGuid);
            const AssetEntry* ysh = ctx_.Assets().FindByGuid(kYamiHeroSheetGuid);
            const AssetEntry* hcl = ctx_.Assets().FindByGuid(kAnimHitClipGuid);
            const bool booked = sh && sh->Sliced() && sh->sliceCount == 4;
            const bool hitBooked = hcl && !hcl->missing;
            bool animOk = booked && smokeAnimMax[0] > 0 && smokeAnimSlice[0];
            char yami[96] = "";
            if (ysh && !ysh->missing && ysh->Sliced()) {
                const bool yOk = smokeAnimMax[1] > 0 && smokeAnimSlice[1] && ysh->sliceCount == 9;
                animOk = animOk && yOk;
                std::snprintf(yami, sizeof(yami), " yami(maxFrame=%u slice=%s frames=%u)",
                              (unsigned)smokeAnimMax[1], smokeAnimSlice[1] ? "YES" : "NO",
                              ysh->sliceCount);
            }
            const bool queueOk = hitBooked && smokeQueueHit && smokeQueueBack;
            animOk = animOk && queueOk && smokeFxSeen;
            // M6a 批② T3：clip 编辑链（roundtrip + Play 快照生效）；T3b-1 整图引用；
            // T3c 动画集（roundtrip + 集登记 + 按名反查）；T3d 状态机（roundtrip +
            // 快照 + whole→walk→hit→whole 图驱动三段切换）
            animOk = animOk && smokeClipEditOk && smokeClipPlayCacheOk && smokeWholeOk &&
                     smokeSetEditOk && smokeSetPlayCacheOk && smokeSetOpenOk &&
                     smokeSetCreateOk && smokeSetCreateOpenOk && smokeGraphEditOk &&
                     smokeGraphCacheOk && smokeGraphSwitchOk &&
                     smokePickClickOk && smokePickShiftOk && smokePickAllOk &&
                     smokeSheetAllOk && smokeSheetClearOk && smokeSheetCloseOk &&
                     smokeSheetRowOk &&
                     smokeAddCountOk && smokeAddOrderOk &&
                     smokeFolderCreateOk && smokeDblClipOk &&
                     smokeDragItemOk &&
                     smokeLeftColOk;
            std::printf("[lemon] smoke-anim: prog(maxFrame=%u slice=%s booked=%u)%s "
                        "queue(hitClip=%s hit=%s back=%s) fx(text/bar=%s) "
                        "edit(rt=%s cache=%s whole=%s) set(rt=%s cache=%s open=%s "
                        "create=%s flow=%s) graph(rt=%s cache=%s switch=%s) "
                        "pick(click=%s shift=%s all=%s sheet(all=%s clear=%s close=%s row=%s)) "
                        "multiadd(count=%s order=%s) create(folder=%s dblclip=%s) "
                        "drag(item=%s) leftcol(meta=%s) => %s\n",
                        (unsigned)smokeAnimMax[0], smokeAnimSlice[0] ? "YES" : "NO",
                        sh ? sh->sliceCount : 0, yami, hitBooked ? "booked" : "MISSING",
                        smokeQueueHit ? "YES" : "NO", smokeQueueBack ? "YES" : "NO",
                        smokeFxSeen ? "YES" : "NO", smokeClipEditOk ? "YES" : "NO",
                        smokeClipPlayCacheOk ? "YES" : "NO", smokeWholeOk ? "YES" : "NO",
                        smokeSetEditOk ? "YES" : "NO", smokeSetPlayCacheOk ? "YES" : "NO",
                        smokeSetOpenOk ? "YES" : "NO", smokeSetCreateOk ? "YES" : "NO",
                        smokeSetCreateOpenOk ? "YES" : "NO",
                        smokeGraphEditOk ? "YES" : "NO", smokeGraphCacheOk ? "YES" : "NO",
                        smokeGraphSwitchOk ? "YES" : "NO",
                        smokePickClickOk ? "YES" : "NO", smokePickShiftOk ? "YES" : "NO",
                        smokePickAllOk ? "YES" : "NO", smokeSheetAllOk ? "YES" : "NO",
                        smokeSheetClearOk ? "YES" : "NO", smokeSheetCloseOk ? "YES" : "NO",
                        smokeSheetRowOk ? "YES" : "NO",
                        smokeAddCountOk ? "YES" : "NO", smokeAddOrderOk ? "YES" : "NO",
                        smokeFolderCreateOk ? "YES" : "NO", smokeDblClipOk ? "YES" : "NO",
                        smokeDragItemOk ? "YES" : "NO",
                        smokeLeftColOk ? "YES" : "NO",
                        animOk ? "OK" : "FAIL");
            if (!animOk) exitCode = 1;
        }
        // M5 批④模板链验收：向导复制 → build → Play 全链在跑（能到这 = 前两环已过）；
        // 断言 HUD 四要素 / 存档载入回显 / 波次 / 击杀 / 升级卡片出现-选择-隐藏。
        // 批③d-1：HUD/卡片断言已随迁文档面（hud(doc)=TryGetElementText / cards 容器
        // 计数 + 合成点击 + IsDocumentShown）；layer = 层序三拍（scrim 压暗复原）；
        // uidoc = 通道 A 装载恰 2（HUD+cards 场景声明）。
        if (launch.smokeTemplate) {
            const bool layerOk = g_tplHudPixN0 > 40 &&
                                 g_tplHudPixDuring < g_tplHudPixN0 / 2 &&
                                 g_tplHudPixAfter > g_tplHudPixN0 / 2;
            const bool tplOk = g_tplHudDocOk && g_tplHudBarBox && g_tplBestLoaded &&
                               g_tplWaveRow &&
                               g_tplDeaths > 0 && g_tplLevelUps > 0 && g_tplCardsSeen &&
                               g_tplPicked && g_tplCardsHidden && g_tplDeathSeen &&
                               g_tplRevived && g_tplScriptOk &&
                               g_tplMobHitClip && g_tplFxText && g_tplFxBar && // 批①
                               g_tplTablesOk && // 批② T4：数值表载入
                               g_tplUiLoads == 2 && layerOk; // 批③d-1：装载恰 2 + 层序三拍
            std::printf("[lemon] smoke-template: hud(doc=%s bar=%s) saveLoad=%s wave(row=%s n=%d) "
                        "kills=%d levelUps=%d cards(doc seen=%s pick=%s hidden=%s) "
                        "layer(%d/%d/%d=%s) "
                        "death(seen=%s revive=%s scriptOk=%s) "
                        "hitClip=%s fx(text=%s bar=%s) tables=%s uidoc=%d => %s\n",
                        g_tplHudDocOk ? "YES" : "NO",
                        g_tplHudBarBox ? "YES" : "NO", g_tplBestLoaded ? "YES" : "NO",
                        g_tplWaveRow ? "YES" : "NO", g_tplWaveStarts, g_tplDeaths,
                        g_tplLevelUps, g_tplCardsSeen ? "YES" : "NO",
                        g_tplPicked ? "YES" : "NO", g_tplCardsHidden ? "YES" : "NO",
                        g_tplHudPixN0, g_tplHudPixDuring, g_tplHudPixAfter,
                        layerOk ? "OK" : "FAIL",
                        g_tplDeathSeen ? "YES" : "NO", g_tplRevived ? "YES" : "NO",
                        g_tplScriptOk ? "YES" : "NO", g_tplMobHitClip ? "YES" : "NO",
                        g_tplFxText ? "YES" : "NO", g_tplFxBar ? "YES" : "NO",
                        g_tplTablesOk ? "YES" : "NO", g_tplUiLoads,
                        tplOk ? "OK" : "FAIL");
            std::printf("[lemon] smoke-template: diag %s gems(peak)=%d mobs(peak)=%d\n",
                        g_tplHudRows, g_tplGems, g_tplMobs);
            if (!tplOk) exitCode = 1;
            // ExitPlay 兜底落盘（写路径）：Stop 后 .lemon/saves/ 三档——slot_0 =
            // 旧 game.sav 惰性迁移后落新名（迁移链闭环：内容含种子键）；meta =
            // vs.best（模板 Chan.Meta）；settings 空档跳过不落文件（Count 0 语义）
            {
                namespace fs = std::filesystem;
                const fs::path savesDir =
                    fs::path(ctx_.Assets().ProjectRoot()) / ".lemon/saves";
                std::error_code ec;
                auto fileOk = [&](const char* name) {
                    return fs::file_size(savesDir / name, ec) > 16 && !ec;
                };
                const bool slotOk = fileOk("slot_0.sav"), metaOk = fileOk("meta.sav");
                const bool skipOk = !fs::exists(savesDir / "settings.sav", ec);
                bool migrateOk = false; // slot_0.sav 解码含种子键 vs.best=123
                if (slotOk) {
                    std::ifstream f(savesDir / "slot_0.sav", std::ios::binary);
                    std::vector<uint8_t> b((std::istreambuf_iterator<char>(f)),
                                           std::istreambuf_iterator<char>());
                    lemon::ecs::SaveChannel ch;
                    char buf[8] = {};
                    migrateOk = ch.Decode(b.data(), b.size()) &&
                                ch.GetLen("vs.best") == 3 && ch.Get("vs.best", buf, 7) == 3 &&
                                std::memcmp(buf, "123", 3) == 0;
                }
                const bool savOk = slotOk && metaOk && skipOk && migrateOk;
                std::printf("[lemon] smoke-template: saves(slot_0=%s meta=%s legacy=%s "
                            "skipEmpty=%s) => %s\n",
                            slotOk ? "YES" : "NO", metaOk ? "YES" : "NO",
                            migrateOk ? "YES" : "NO", skipOk ? "YES" : "NO",
                            savOk ? "OK" : "FAIL");
                if (!savOk) exitCode = 1;
            }
            // 批④后修②回归防线：同进程再开第二个模板拷贝 → spriteId 记账必须与
            // 第一个逐项一致（换项目注册表复位）。修复前第二个项目整体后移上个
            // 项目的精灵数 → 场景烘焙引用悬空、玩家/怪物全不渲染（demo/svr-test
            // 实测 +31；自动重开上次项目后走新建向导 = 稳定触发路径）。
#ifdef LEMON_SCRIPT_DIR
            {
                auto SnapshotIds = [](const AssetDatabase& db) {
                    std::vector<std::pair<std::string, std::string>> m;
                    for (const AssetEntry& e : db.Entries())
                        if (e.type == AssetType::Sprite && !e.missing)
                            m.emplace_back(e.relPath,
                                           std::to_string(e.spriteId) + "/" +
                                               std::to_string(e.sliceBase) + "+" +
                                               std::to_string(e.sliceCount));
                    std::sort(m.begin(), m.end());
                    return m;
                };
                const auto ids1 = SnapshotIds(ctx_.Assets());
                namespace fs = std::filesystem;
                const fs::path tmp2 =
                    fs::temp_directory_path() /
                    ("lemon-smoke-template2-" + std::to_string(::getpid()));
                std::error_code ec2;
                fs::remove_all(tmp2, ec2);
                ProjectDesc d2;
                d2.parentDir = tmp2.string();
                d2.name = "VsSmoke2";
                d2.sdkDir = LEMON_SCRIPT_DIR;
                d2.engineVersion = "0.5.0-m5";
                d2.templateName = "vs-survivor";
                d2.templateDir = std::string(LEMON_TEMPLATE_DIR) + "/vs-survivor";
                bool idOk = false;
                if (const std::string root2 = ProjectWizard::Create(d2);
                    !root2.empty() && OpenProjectPipeline(root2)) {
                    idOk = SnapshotIds(ctx_.Assets()) == ids1;
                }
                std::printf("[lemon] smoke-template: second-project ids %s => %s\n",
                            idOk ? "identical" : "DRIFTED", idOk ? "OK" : "FAIL");
                if (!idOk) exitCode = 1;
            }
#endif
        }
        // ---- M4.5 终验（§6 #1/#2/#3/#6/#7 全量化）----
        bool finalOk = true;
        if (launch.finalTest) {
            // StateBag 续跑断言：换装于 tick=20（窗口 40→70）→ 总刷怪 = 16 + 50 = 66
            // （若状态丢失 = 16 + 66 = 82 ≠ 66；若换装失败 = 36）
            finalStateBagTotal_ = (int)playAliveAtStop - (int)smokeSeeded_;
            const bool bagOk = finalStateBagTotal_ == 66;
            // Edit 态热重载一例（§6 #2 双态各一）：加 EditProbeBehaviour → 注册表可见
            {
                namespace fs = std::filesystem;
                const fs::path gp = fs::path(launch_->projectDir) / "Game";
                std::ofstream probe(gp / "EditProbeBehaviour.cs", std::ios::trunc);
                probe << "public sealed class EditProbeBehaviour : Lemon.LemonBehaviour { }\n";
                std::ifstream in(gp / "GameMain.cs", std::ios::binary);
                std::string src((std::istreambuf_iterator<char>(in)),
                                std::istreambuf_iterator<char>());
                // 在既有注册行前插一行（锚点替换——直接动 Configure 签名会破坏大括号配对）
                const std::string anchor = "Lemon.Behaviours.Register<InputMoverBehaviour>();";
                const size_t at = src.find(anchor);
                if (at != std::string::npos)
                    src.insert(at, "Lemon.Behaviours.Register<EditProbeBehaviour>();\n        ");
                std::ofstream out(gp / "GameMain.cs", std::ios::trunc);
                out << src;
            } // 落盘作用域结束（先 close 再编译）
            finalEditReloadOk_ = TryHotReloadScripts("final-edit（Edit 态）");
            bool seen = false;
            for (const std::string& n : ctx_.ScriptTypeNames()) seen |= n == "EditProbeBehaviour";
            finalEditReloadOk_ = finalEditReloadOk_ && seen;
            // 自动备份/崩溃恢复链（§3.8）：dirty → 快照 → 检出 → 恢复（保持 dirty）→ 落盘 → 清
            bool autosaveOk = false;
            {
                ctx_.Select(ctx_.Primary(), false);
                ctx_.dirty = true;
                const bool wrote = ctx_.AutoSaveNow();
                const std::string rec = ctx_.DetectAutosaveRecovery();
                const bool opened = !rec.empty() && ctx_.OpenSceneRecovery(rec);
                const bool keptDirty = ctx_.dirty;
                const bool saved = ctx_.SaveScene();
                const bool cleared = ctx_.DetectAutosaveRecovery().empty();
                autosaveOk = wrote && opened && keptDirty && saved && cleared;
            }
            const int leaks = host_ ? host_->HotReloadLeakCount() : 0;
            const int reloads = HotReloadCount();
            const bool hrOk = finalPlayReloadOk_ && finalEditReloadOk_ &&
                              finalPlayReloadMs_ <= 2000.0 && hotReloadMs_ <= 2000.0 &&
                              leaks > 0 /* A 线已知泄漏（探针口径）*/;
            const bool fpsOk = finalPlayMinFps_ >= 45.0;
            const bool coldOk = firstFrameMs < 2000.0;
            finalOk = bagOk && hrOk && fpsOk && coldOk && autosaveOk && playVerified;
            std::printf("[lemon] final-wizard: dirs/project.lemon/Game/spawn.png => %s\n", "OK");
            std::printf("[lemon] final-judgement: zero-code scene entities=%u visible=%u\n",
                        smokeSeeded_, viewport_->LastSceneVisible());
            std::printf("[lemon] final-hotreload: play=%.0fms edit=%.0fms(≤2000) "
                        "stateBag=%d/66 reloads=%d leaks=%d(A线) => %s\n",
                        finalPlayReloadMs_, hotReloadMs_, finalStateBagTotal_, reloads, leaks,
                        hrOk ? "OK" : "FAIL");
            std::printf("[lemon] final-play: minFps=%.0f(≥45) enter=%.1fms exit=%.1fms "
                        "byte-exact=%s => %s\n",
                        finalPlayMinFps_, playEnterMs, playExitMs,
                        ctx_.LastExitVerified() ? "YES" : "NO", (fpsOk && playVerified) ? "OK" : "FAIL");
            std::printf("[lemon] final-autosave: snapshot+recovery+save-clear => %s\n",
                        autosaveOk ? "OK" : "FAIL");
            std::printf("[lemon] final-coldstart: %.0fms(<2000) => %s\n", firstFrameMs,
                        coldOk ? "OK" : "FAIL");
            if (!finalOk) std::printf("[lemon] final FAIL 项：bag=%d hr=%d fps=%d cold=%d autosave=%d play=%d\n",
                                      bagOk, hrOk, fpsOk, coldOk, autosaveOk, playVerified);
        }
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
    if (launchCopy_.smokeUirml) {
        bool uiOk = false;
        int panelN = 0, titleGN = 0, bodyBN = 0, texN = 0, titleTopN = 0;
        int oldGoldN = 0, oldGrayN = 0;
        std::vector<uint8_t> rt;
        uint32_t rw = 0, rh = 0;
        const bool fetched = device_->DebugFetchTextureCapture(rt, rw, rh);
        // gameRT 落盘：--screenshot 请求时自动出第二张（<名>-gamert.png，无编辑器
        // 铬的纯游戏画面 = 像素断言同源图）；LEMON_UIRML_DUMP 排障路径保留
        if (fetched && !launch.screenshot.empty()) {
            namespace fs = std::filesystem;
            const fs::path sp(launch.screenshot);
            stbi_write_png(
                (sp.parent_path() / (sp.stem().string() + "-gamert.png")).generic_string().c_str(),
                (int)rw, (int)rh, 4, rt.data(), (int)rw * 4);
        }
        if (std::getenv("LEMON_UIRML_DUMP") && fetched)
            stbi_write_png("/tmp/uirml-rt.png", (int)rw, (int)rh, 4, rt.data(), (int)rw * 4);
        const bool hasDoc = gameUi_ && gameUi_->HasDocument("Assets/UI/uirml.rml");
        const bool notoFont = gameUi_ &&
                              std::strcmp(gameUi_->LoadedFontFamily(), "Noto Sans SC") == 0;
        if (fetched && hasDoc) {
            panelN = CountPixelsNear(rt, rw, rh, 32, 64, 96, 14);    // #204060 面板底
            titleGN = CountPixelsNear(rt, rw, rh, 64, 255, 144, 30); // #40ff90 热重载后标题
            // 正文蓝 tol=24：边框 #60a0ff 与正文 #80c0ff 逐通道差 32——tol 44 时边框
            // 混入计数（实测 4476 ≈ 边框周长像素，文字仍旧灰的假阳性来源）
            bodyBN = CountPixelsNear(rt, rw, rh, 128, 192, 255, 24); // #80c0ff 热重载后正文
            texN = CountPixelsNear(rt, rw, rh, 208, 64, 128, 30);    // #d04080 贴图（桥）
            oldGoldN = CountPixelsNear(rt, rw, rh, 255, 208, 96, 20); // 旧标题色残留
            oldGrayN = CountPixelsNear(rt, rw, rh, 224, 224, 224, 30); // 旧正文色残留
            // 位置断言（③a 真人目检抓纵向翻转后的机器化：标题色像素须集中上半幅）
            const uint32_t halfY = rh / 2;
            for (uint32_t y = 0; y < rh; ++y)
                for (uint32_t x = 0; x < rw; ++x) {
                    const uint8_t* p = &rt[((size_t)y * rw + x) * 4];
                    const int dr = (int)p[0] - 64, dg = (int)p[1] - 255, db = (int)p[2] - 144;
                    if ((uint32_t)(dr * dr + dg * dg + db * db) <= 30u * 30u && y < halfY)
                        ++titleTopN;
                }
            uiOk = notoFont && panelN > 3000 && titleGN > 20 && bodyBN > 20 && texN > 500 &&
                   oldGoldN < 5 && oldGrayN < 5 && titleTopN >= titleGN * 3 / 4;
        }
        // 批③d-1（B1 dp 坐标系）：ratio = gameRT 高/720 + dpbox 尺寸 = dp 值 × ratio
        //（px 定位/dp 尺寸混合元——既有 px 断言不受 ratio 影响的活证）
        const float dpRatio = gameUi_ ? gameUi_->DpRatio() : 1.0f;
        float dpW = -1.f, dpH = -1.f;
        if (gameUi_)
            gameUi_->TryGetElementBox("Assets/UI/uirml.rml", "dpbox", &dpW, &dpH);
        const bool dpRatioOk = fetched && rh > 0 &&
                               std::fabs(dpRatio - (float)rh / 720.0f) < 0.01f;
        const bool dpBoxOk = fetched &&
                             std::fabs(dpW - 100.0f * dpRatio) < 1.0f &&
                             std::fabs(dpH - 50.0f * dpRatio) < 1.0f;
        // 批③c：C# API 全链位（--script 时生效；未带脚本 = n/a 通过——③b 口径
        // 的直跑兼容）。items = 克隆行数（cards 2 + negbox 1 负面行）；text = 探针
        // SetText 后的 title（热重载两段后仍是探针值 = DocumentReloaded 重灌证据）；
        // ev = 合成点击/重灌计数回读（RtUi uiev）；contract = 契约错误恰 1（负面
        // op 直灌的一个，其余零容忍——"响亮失败"回归防线）
        const bool scripted = ctx_.Scripts() != nullptr;
        int itemsN = -1, negN = -1;
        if (gameUi_) {
            itemsN = gameUi_->ContainerItemCount("Assets/UI/uirml.rml", "cards");
            negN = smokeUiNegP2; // 251 帧快照（终帧在三局——通道 A 重装已清负面容器）
        }
        char titleText[64] = {};
        if (gameUi_)
            gameUi_->TryGetElementText("Assets/UI/uirml.rml", "title", titleText,
                                       sizeof(titleText));
        int evClicks = -1, evReloads = -1;
        std::sscanf(smokeUiEvText_, "c%dr%d", &evClicks, &evReloads);
        const uint32_t contractN = gameUi_ ? gameUi_->ContractErrorCount() : 0;
        const bool textOk = std::strcmp(titleText, "升级！三选一") == 0;
        const bool itemsOk = !scripted || (itemsN == 2 && negN == 1);
        const bool evOk = !scripted || (evClicks >= 1 && evReloads >= 2);
        const bool contractOk = !scripted || (contractN == 1 && textOk);
        const bool uiOk3c = itemsOk && evOk && contractOk;
        // 批③d 前置 T5：UIDocument 双通道/层序/stale 裁决位。
        //   uidocA/B = 通道 A/B 装载（场景声明 + Show 落空兜底各至少一条断言）；
        //   layer = D1 层序（B 后 Show 在上 → 171 段 #802040>500；175 重 Show A 后
        //   190 段 <5 = 被盖住）；stale/keepC/p2 = Play→Stop→Play：第二局动态屏被
        //   Hide（装载保留）、Edit 双击装载保持可见、装载增量恰 1（只重装声明文档）
        // hasDocB 读 220 帧存量快照（220 删除播种后终帧恒 false——逐出本身是断言）
        const bool hasDocB = smokeUiHasDocB;
        const bool hasDocC = gameUi_ && gameUi_->HasDocument("Assets/UI/editprev.rml");
        const uint32_t loadsN = gameUi_ ? gameUi_->DocumentLoadCount() : 0;
        int cPrevN = 0;
        char dynText[64] = {};
        if (gameUi_ && fetched)
            cPrevN = CountPixelsNear(rt, rw, rh, 96, 64, 128, 30); // #604080 Edit 预览
        std::snprintf(dynText, sizeof(dynText), "%s", smokeUiDynText); // 220 快照（逐出后终帧读恒空）
        // scripted：C# 同批 Show+SetText 到刚兜底装载的 dyn（通道 B 顺序契约）
        const bool dynTextOk = !scripted || std::strcmp(dynText, "通道B已装载") == 0;
        // 形态 D：stopHide/delEnt 全模式（410 簿记断言先于当帧 C#）；pix 仅无脚本
        // 模式（脚本模式 C# 帧 1 经通道 B 合法 Show 拉回 = 动态屏非僵尸）；
        // reset = 二段防线二独立面（421 绕过清场 → 424 Reset 独自清场）
        const bool evict3Ok = smokeUiExitHideOk && smokeUiDelEntOk &&
                              smokeUiResetSeedOk && smokeUiResetOnlyOk &&
                              (scripted || (smokeUiDelEntPixN >= 0 &&
                                            smokeUiDelEntPixN < 5));
        const bool uidocOk = hasDoc && hasDocB && hasDocC && smokeUiExitP2 &&
                             smokeUiStaleOk && smokeUiKeepCOk && smokeUiLoadsP2 == 1 &&
                             smokeUiLayerBTopN > 500 && smokeUiLayerATopN < 5 &&
                             cPrevN > 100 && dynTextOk && smokeUiDelEvictOk &&
                             smokeUiEvictSeedOk && smokeUiZombiePixN >= 0 &&
                             smokeUiZombiePixN < 5 && smokeUiEvictEditOk &&
                             smokeUiEvictPixN >= 0 && smokeUiEvictPixN < 5 && evict3Ok;
        std::printf("[lemon] smoke-uirml: doc=%d font=%s panel=%d(>3000) titleG=%d(>20) "
                    "bodyB=%d(>20) tex=%d(>500) old=%d/%d(<5) titleTop=%d/%d(≥3/4) "
                    "dp(ratio=%.3f box=%.1fx%.1f/%s) "
                    "items=%d/%d ev=c%dr%d contract=%u/%s "
                    "uidoc(a=%d/b=%d/c=%d loads=%u/%u) layer(bTop=%d aTop=%d) "
                    "p2(stale=%d keepC=%d+%dpx dyn=%s del=%d) "
                    "evict2(seed=%d live=%d edit=%d pix=%d) "
                    "evict3(stopHide=%d delEnt=%d reset=%d/%d pix=%d) => %s\n",
                    hasDoc ? 1 : 0, gameUi_ ? gameUi_->LoadedFontFamily() : "-",
                    panelN, titleGN, bodyBN, texN, oldGoldN, oldGrayN, titleTopN, titleGN,
                    dpRatio, dpW, dpH, (dpRatioOk && dpBoxOk) ? "OK" : "BAD",
                    itemsN, negN, evClicks, evReloads, contractN, textOk ? "textOK" : "textBAD",
                    hasDoc ? 1 : 0, hasDocB ? 1 : 0, hasDocC ? 1 : 0, loadsN, smokeUiLoadsP2,
                    smokeUiLayerBTopN, smokeUiLayerATopN,
                    smokeUiStaleOk ? 1 : 0, smokeUiKeepCOk ? 1 : 0, cPrevN,
                    dynTextOk ? "OK" : "BAD", smokeUiDelEvictOk ? 1 : 0,
                    smokeUiEvictSeedOk ? 1 : 0, smokeUiZombiePixN, smokeUiEvictEditOk ? 1 : 0,
                    smokeUiEvictPixN, smokeUiExitHideOk ? 1 : 0, smokeUiDelEntOk ? 1 : 0,
                    smokeUiResetSeedOk ? 1 : 0, smokeUiResetOnlyOk ? 1 : 0,
                    smokeUiDelEntPixN,
                    (uiOk && uiOk3c && uidocOk && dpRatioOk && dpBoxOk) ? "OK" : "FAIL");
        if (!uiOk || !uiOk3c || !uidocOk || !dpRatioOk || !dpBoxOk) exitCode = 1;
    }

    watcher_.Stop();          // 先停 watcher 线程（此后无资产重扫）
    scriptWatcher_.Stop();    // 与 Game/ 源监视同批收尾
    host_.reset();            // C# 宿主卸载（无脚本时为空操作）
    device_->WaitIdle(); // ImGui 后端资源（描述符池/采样器）可能被在途帧引用，先等闲
    ui_->Shutdown();
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

// 批③b（ADR-014）：双击 .rml → 装载到游戏 UI。文档名 = relPath（RescanAssets
// 热重载对账键）；Show 无条件置位（渲染层只在 Play 中被调用——非 Play 装载即
// 备好，进 Play 即显）。③c C# 装载通道落地前的手动通道。
void EditorApp::LoadUiDocument(uint64_t guid) {
    if (!gameUi_) {
        LEMON_WARN("UI 装载失败：游戏 UI 层不可用（初始化失败/字体缺失，见启动红字）");
        return;
    }
    const AssetEntry* e = ctx_.Assets().FindByGuid(guid);
    if (!e || e->missing || e->type != AssetType::Rml) return;
    const std::string abs = ctx_.Assets().AbsolutePath(*e);
    if (!gameUi_->LoadDocumentFromFile(e->relPath.c_str(), abs.c_str(),
                                       lemon::ui::UiDocOrigin::Edit)) return;
    gameUi_->ShowDocument(e->relPath.c_str(), true);
    LEMON_LOG("UI 文档已装载%s：%s（改动落盘经 watcher/重扫热重载）",
              ctx_.Playing() ? "" : "（进 Play 后 GameView 显示）", e->relPath.c_str());
}

// 批③c（M7/ADR-014 D2）：游戏 UI 输入喂入（Play 段、gameUi_->Update() 前每帧）——
//   鼠标：画布矩形内（ImGui 屏幕点 → 画布 RT 像素）逐帧 SetPointer + 点击边沿；
//   键盘：gameViewFocused_ 时 ImGui key 态差分（边沿）转发（RmlUi 焦点导航/文本编辑）；
//   IME 锚点：窗口点 = 画布原点 + caret×(画布点/RT 像素)——ActivateKeyboard 消费。
//   游戏侧让出门在 ApplyInput（uiHoldsInput）——本函数只喂 UI 不夺编辑器事件。
void EditorApp::FeedGameUiInput() {
    if (!gameUi_) return;
    bool inside = false;
    float px = 0, py = 0;
    if (gvCanvasValid_ && gvCanvasHovered_) {
        const ImVec2 mp = ImGui::GetMousePos();
        inside = mp.x >= gvCanvasX_ && mp.x <= gvCanvasX_ + gvCanvasW_ &&
                 mp.y >= gvCanvasY_ && mp.y <= gvCanvasY_ + gvCanvasH_;
        if (inside) {
            px = (mp.x - gvCanvasX_) * (float)gvRtW_ / gvCanvasW_;
            py = (mp.y - gvCanvasY_) * (float)gvRtH_ / gvCanvasH_;
        }
    }
    // 批③d-1：模板卡片直灌点击窗——指针保持（Update 建悬停、down/up 两帧间不被
    // 真实鼠标复位覆盖；证据块状态机置位，见 smoke-template 段注记）
    if (!g_tplPointerHold) gameUi_->SetPointer((int)px, (int)py, inside);
    // review P2：画布无效（GameView 关闭/未上报）时 W/H 与 RT 均为残 0——0/0 = NaN
    // 会灌进 SDL_SetTextInputArea。守卫 + 恒等中性（锚点 = 光标本位）。
    if (gvCanvasValid_ && gvRtW_ > 0 && gvRtH_ > 0 && gvCanvasW_ > 0.0f && gvCanvasH_ > 0.0f)
        gameUi_->SetImeRectTransform(gvCanvasX_, gvCanvasY_, gvCanvasW_ / (float)gvRtW_,
                                     gvCanvasH_ / (float)gvRtH_);
    else
        gameUi_->SetImeRectTransform(0.0f, 0.0f, 1.0f, 1.0f);
    if (inside) {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) gameUi_->ProcessMouseButton(0, true);
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
            gameUi_->ProcessMouseButton(0, false);
    }
    if (gameViewFocused_) {
        // ImGuiKey → UiKey（波1 集合：字母/数字/方向/编辑键；差分出边沿）
        using UK = ::lemon::ui::UiKey;
        static const struct { int ik; UK uk; } kMap[] = {
            {ImGuiKey_A, UK::A}, {ImGuiKey_B, UK::B}, {ImGuiKey_C, UK::C},
            {ImGuiKey_D, UK::D}, {ImGuiKey_E, UK::E}, {ImGuiKey_F, UK::F},
            {ImGuiKey_G, UK::G}, {ImGuiKey_H, UK::H}, {ImGuiKey_I, UK::I},
            {ImGuiKey_J, UK::J}, {ImGuiKey_K, UK::K}, {ImGuiKey_L, UK::L},
            {ImGuiKey_M, UK::M}, {ImGuiKey_N, UK::N}, {ImGuiKey_O, UK::O},
            {ImGuiKey_P, UK::P}, {ImGuiKey_Q, UK::Q}, {ImGuiKey_R, UK::R},
            {ImGuiKey_S, UK::S}, {ImGuiKey_T, UK::T}, {ImGuiKey_U, UK::U},
            {ImGuiKey_V, UK::V}, {ImGuiKey_W, UK::W}, {ImGuiKey_X, UK::X},
            {ImGuiKey_Y, UK::Y}, {ImGuiKey_Z, UK::Z},
            {ImGuiKey_0, UK::Num0}, {ImGuiKey_1, UK::Num1}, {ImGuiKey_2, UK::Num2},
            {ImGuiKey_3, UK::Num3}, {ImGuiKey_4, UK::Num4}, {ImGuiKey_5, UK::Num5},
            {ImGuiKey_6, UK::Num6}, {ImGuiKey_7, UK::Num7}, {ImGuiKey_8, UK::Num8},
            {ImGuiKey_9, UK::Num9},
            {ImGuiKey_UpArrow, UK::Up}, {ImGuiKey_DownArrow, UK::Down},
            {ImGuiKey_LeftArrow, UK::Left}, {ImGuiKey_RightArrow, UK::Right},
            {ImGuiKey_Backspace, UK::Backspace}, {ImGuiKey_Enter, UK::Return},
            {ImGuiKey_Escape, UK::Escape}, {ImGuiKey_Space, UK::Space},
            {ImGuiKey_Home, UK::Home}, {ImGuiKey_End, UK::End},
            {ImGuiKey_Delete, UK::Delete}, {ImGuiKey_Tab, UK::Tab},
        };
        for (const auto& m : kMap) {
            const bool down = ImGui::IsKeyDown((ImGuiKey)m.ik);
            const int idx = (int)m.uk;
            if (down != gvKeyWasDown_[idx]) gameUi_->ProcessKey(m.uk, down);
            gvKeyWasDown_[idx] = down;
        }
    }
}

// 批③b 贴图桥解析器（RmlUi JoinPath 解析后的绝对路径 → 项目精灵资产 → 图集页）。
// 切片子图由 RmlUi 原生 <img rect="x y w h"> 表达，引擎零机制（M6 波2 扩 GUID/RT 源）
bool EditorApp::ResolveUiTexture(const std::string& source, rhi::Texture& tex, uint32_t& w,
                                 uint32_t& h) {
    namespace fs = std::filesystem;
    const AssetDatabase& db = ctx_.Assets();
    if (db.ProjectRoot().empty()) return false;
    // 批③c（M6 资产源）：GUID 直引协议（img data-field 值 16hex → "guid:<hex>"——
    // JoinPath 见 ':' 直通；图鉴/卡片图标通道，纸面验证 ⓪/① 形态）
    if (source.rfind("guid:", 0) == 0) {
        const AssetEntry* e = db.FindByGuid(AssetDatabase::HexToGuid(source.substr(5).c_str()));
        if (!e || e->missing || e->type != AssetType::Sprite) return false;
        renderer::AtlasRegistry& reg = viewport_->Assets().Registry();
        if (!reg.IsValidSprite(e->spriteId)) return false;
        const renderer::SpriteInfo& si = reg.GetSprite(e->spriteId);
        const rhi::Texture t = reg.AtlasTexture(si.atlasIndex, w, h);
        if (!t.IsValid()) return false;
        tex = t;
        return true;
    }
    std::error_code ec;
    const fs::path rel = fs::relative(fs::path(source), fs::path(db.ProjectRoot()), ec);
    if (ec) return false;
    const std::string relStr = rel.generic_string();
    if (relStr.empty() || relStr == "." || relStr.front() == '.') return false; // 越出项目根
    const AssetEntry* e = db.FindByPath(relStr);
    if (!e || e->missing || e->type != AssetType::Sprite) return false;
    renderer::AtlasRegistry& reg = viewport_->Assets().Registry();
    if (!reg.IsValidSprite(e->spriteId)) return false; // 未导入/空洞
    const renderer::SpriteInfo& si = reg.GetSprite(e->spriteId);
    const rhi::Texture t = reg.AtlasTexture(si.atlasIndex, w, h);
    if (!t.IsValid()) return false;
    tex = t;
    return true;
}

// 批③d 前置（通道 B）：文档解析器（C# UI.Show 的 relPath → 项目 .rml 资产绝对
// 路径）。未开项目/查无/非 Rml 类型/墓碑 = false → ApplyOps 维持响亮失败。
bool EditorApp::ResolveUiDocument(const std::string& relPath, std::string& absPath) {
    const AssetDatabase& db = ctx_.Assets();
    if (db.ProjectRoot().empty()) return false;
    const AssetEntry* e = db.FindByPath(relPath);
    if (!e || e->missing || e->type != AssetType::Rml) return false;
    absPath = db.AbsolutePath(*e);
    return true;
}

// 批③b：项目字体注册（Assets/ 下 otf/ttf/ttc → RmlUi fallback）。RmlUi 无
// UnloadFontFace——切项目旧族名共存（无害）；族名以字体文件为准（报告名传文件名）
void EditorApp::LoadProjectFonts() {
    if (!gameUi_) return;
    namespace fs = std::filesystem;
    uint32_t n = 0;
    for (const AssetEntry& e : ctx_.Assets().Entries()) {
        if (e.missing) continue;
        std::string ext = fs::path(e.relPath).extension().string();
        for (char& c : ext) c = (char)std::tolower((unsigned char)c);
        if (ext != ".otf" && ext != ".ttf" && ext != ".ttc") continue;
        if (gameUi_->LoadFontFace(ctx_.Assets().AbsolutePath(e).c_str(), e.FileName().c_str(),
                                  /*fallback=*/true))
            ++n;
    }
    if (n) LEMON_LOG("项目字体：%u 个已注册为 fallback（RCSS 按 font-family 命中）", n);
}

// 批③b：--smoke-uirml 资产夹具（temp 项目，幂等清残留；夹具纪律 = 空目录自播种）。
// 三件套：uirml.rml（面板/标题/正文 + <img>）/ uirml.rcss（<link> 样式）/ tex.png
// （96×96 特征色贴图——贴图桥像素断言源）。热重载中点改写 .rml/.rcss（帧循环钩子）
void EditorApp::SeedSmokeUiRmlProject() {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path tmp =
        fs::temp_directory_path() / ("lemon-uirml-" + std::to_string(::getpid()));
    fs::remove_all(tmp, ec);
    const fs::path assets = tmp / "Assets" / "UI";
    fs::create_directories(assets, ec);
    {
        std::ofstream f(tmp / "project.lemon", std::ios::trunc);
        f << "{\n  \"schemaVersion\": 1,\n  \"name\": \"uirml\",\n"
             "  \"engineVersion\": \"0.6.0-m6a\"\n}\n";
    }
    { // 贴图：#d04080 纯色（角标会落进旧灰断言的逐通道 ±30 带内——白色 ≠ 免责色）
        std::vector<uint8_t> px(96 * 96 * 4);
        for (int y = 0; y < 96; ++y)
            for (int x = 0; x < 96; ++x) {
                uint8_t* q = &px[((size_t)y * 96 + x) * 4];
                q[0] = 208;
                q[1] = 64;
                q[2] = 128;
                q[3] = 255;
            }
        stbi_write_png((assets / "tex.png").string().c_str(), 96, 96, 4, px.data(), 96 * 4);
    }
    { // 样式表：正字 Noto + 面板/正文/贴图规则（热重载中点正文色 → #80c0ff 蓝）。
        // #title 规则留在 .rml 文档级 <style>——两段热重载各自改自己的文件：
        // frame100 改 .rml（文档级样式，ReloadDocument 单文档路径）、frame140 改
        // .rcss（<link> 表，ReloadStyleSheets 保 DOM 路径）
        std::ofstream f(assets / "uirml.rcss", std::ios::trunc);
        f << "body { font-family: Noto Sans SC; color: #e0e0e0; }\n"
             "#panel { position: absolute; left: 80px; top: 80px; width: 480px; height: 430px;\n"
             "    background: #204060; border: 3px #60a0ff; }\n"
             "#body  { font-size: 16px; margin-left: 28px; }\n"
             "#teximg { position: absolute; left: 320px; top: 120px; width: 96px; height: 96px; }\n"
             // 批③c：模板克隆行（cardb 绿底按钮——items 像素断言目标）+ 负面容器行
             // （display:none——契约错误计数面，渲染零干扰）
             "#cards { margin-left: 24px; margin-top: 8px; }\n"
             ".cardb { display: block; width: 300px; margin: 6px 0; padding: 10px;\n"
             "    background: #40d080; color: #103018; font-size: 15px; text-align: center; }\n"
             ".nb { display: none; }\n"
             // 批③d 前置 T5：dyn = 通道 B 动态屏（覆盖块落在 A 面板右下内区——
             // 层序断言观察窗：B 在上 = #802040 可见 / A 在上 = 被 #204060 盖住）；
             // editprev = Edit 期双击装载代表（A 面板右侧空带，跨 Play 保持可见）
             "#dynpanel { position: absolute; left: 400px; top: 300px; width: 160px;\n"
             "    height: 120px; background: #802040; color: #e0c0d0; font-size: 14px; }\n"
             "#editpanel { position: absolute; left: 590px; top: 80px; width: 140px;\n"
             "    height: 60px; background: #604080; color: #d0c0e8; font-size: 12px; }\n"
             // 批③d-1：dp 坐标系断言元（角位 #305060 避让全部既有计数色带——px
             // 定位 + dp 尺寸：期望渲染尺寸 = 100dp/50dp × ratio）
             "#dpbox { position: absolute; left: 700px; top: 415px; width: 100dp;\n"
             "    height: 50dp; background: #305060; }\n";
    }
    { // 文档：<link> 引样式 + 三要素 + <img>（热重载中点标题色 → #40ff90 绿）
        std::ofstream f(assets / "uirml.rml", std::ios::trunc);
        f << "<rml>\n<head><title>lemon ui smoke</title>\n"
             "<link type=\"text/rcss\" rel=\"stylesheet\" href=\"uirml.rcss\"/>\n"
             "<style>#title { font-size: 28px; color: #ffd060; margin: 24px 0 8px 28px; }\n"
             "</style>\n</head>\n"
             "<body>\n<div id=\"panel\">\n"
             "  <div id=\"title\">Lemon 游戏 UI ③b</div>\n"
             "  <div id=\"body\">资产通道冒烟：字体/样式/贴图/热重载</div>\n"
             "  <img id=\"teximg\" src=\"tex.png\"/>\n"
             // 批③c：M2 模板克隆容器（纸面验证 ⓪ 形态）+ 负面契约容器
             "  <div id=\"cards\" data-template=\"card\">\n"
             "    <ui-template data-name=\"card\"><button class=\"cardb\" data-event=\"pick\">"
             "<span data-field=\"label\"/></button></ui-template>\n"
             "  </div>\n"
             "  <div id=\"negbox\" data-template=\"nrow\">\n"
             "    <ui-template data-name=\"nrow\"><div class=\"nb\"><span data-field=\"lab\"/>"
             "</div></ui-template>\n"
             "  </div>\n"
             "</div>\n"
             // 批③d-1：dp 断言元（panel 外右下角——px 定位不扰动面板区像素断言）
             "<div id=\"dpbox\"/>\n"
             "</body>\n</rml>\n";
    }
    launchCopy_.projectDir = tmp.string();
    launch_ = &launchCopy_;
    { // 批③d 前置 T5：通道 B 动态屏（场景不声明——C# UI.Show / 引擎 op 直灌落空兜底）
        std::ofstream f(assets / "dyn.rml", std::ios::trunc);
        f << "<rml>\n<head><title>dyn</title>\n"
             "<link type=\"text/rcss\" rel=\"stylesheet\" href=\"uirml.rcss\"/>\n</head>\n"
             "<body>\n<div id=\"dynpanel\">通道 B 动态屏\n"
             "  <div id=\"dyntitle\">未装载</div>\n"
             "</div>\n</body>\n</rml>\n";
    }
    { // 批③d 前置 T5：Edit 期双击装载代表（SeedSmokeUiDocument 装载——跨 Play 保持）
        std::ofstream f(assets / "editprev.rml", std::ios::trunc);
        f << "<rml>\n<head><title>edit preview</title>\n"
             "<link type=\"text/rcss\" rel=\"stylesheet\" href=\"uirml.rcss\"/>\n</head>\n"
             "<body>\n<div id=\"editpanel\">Edit 预览</div>\n</body>\n</rml>\n";
    }
    LEMON_LOG("uirml 夹具：%s", tmp.string().c_str());
}

// 批③b：--smoke-uirml 文档装载（从夹具资产走文件通道——③a 内存文档退役）。
// 批③d 前置 T5 改制：本钩只装载 editprev.rml（Edit 期"双击预览"代表——非 stale，
// 跨 Play 保持可见的 §3 断言面）；主文档 uirml.rml 改由场景 UIDocument 声明 →
// 通道 A EnterPlay 装载（双通道各自有独立断言）；dyn.rml 留给通道 B 落空兜底。
void EditorApp::SeedSmokeUiDocument() {
    const std::string rel = "Assets/UI/editprev.rml";
    const AssetEntry* e = ctx_.Assets().FindByPath(rel);
    if (!e || e->missing) {
        LEMON_ERROR("uirml 播种失败：夹具缺 %s", rel.c_str());
        return;
    }
    if (gameUi_->LoadDocumentFromFile(rel.c_str(), ctx_.Assets().AbsolutePath(*e).c_str(),
                                      lemon::ui::UiDocOrigin::Edit))
        gameUi_->ShowDocument(rel.c_str(), true);
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

void EditorApp::SeedSmokeScene() {
    // 冒烟播种：父子层级 + 常用组件 → Hierarchy/Inspector 有内容可验收
    using namespace lemon::ecs;
    Scene& s = ctx_.ActiveScene();
    (void)0;
    ecs::Entity root = ctx_.CreateEntity("Player");
    s.Get<Transform2D>(root).pos = Vec2{640, 360}; // 相机中心
    {
        SpriteRenderer& sr = s.Emplace<SpriteRenderer>(root); // 默认启用
        sr.spriteId = 3;
        Health& hp = s.Emplace<Health>(root);
        hp.max = hp.cur = 100.0f;
        Stats& st = s.Emplace<Stats>(root);
        st.moveSpeed = 180.0f;
        s.Emplace<XpProgress>(root); // 收集者标记（M5 批①：磁吸/拾取/XP 闭环可玩）
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
    // M4.4 资产链：导入 PNG → 场景实体（CreateSpriteEntityFromAsset 与拖拽/双击同通路）
    if (!launch_->projectDir.empty()) {
        ecs::Entity fromAsset =
            ctx_.CreateSpriteEntityFromAsset("FromAsset", 0x5bd31a7c10e9f2c8ull, Vec2{420, 200});
        if (!fromAsset.IsNull()) s.Get<Transform2D>(fromAsset).scale = Vec2{0.5f, 0.5f};
    }
    // M5 批③动画链：程序化 4 帧表 + anim.anim → Animator2D 帧映射断言实体；
    // yami hero-walk（Samples/Assets/yami-dungeon 拷进项目才在场）→ 真素材端到端验
    if (launch_->smokeAnim) {
        if (const AssetEntry* sheet = ctx_.Assets().FindByGuid(kAnimSheetGuid);
            sheet && !sheet->missing) {
            if (ecs::Entity hero = ctx_.CreateSpriteEntityFromAsset(
                    "AnimHero", kAnimSheetGuid, Vec2{640, 540});
                !hero.IsNull()) {
                ecs::Animator2D& an = s.Emplace<ecs::Animator2D>(hero);
                an.clipId = (uint32_t)kAnimClipGuid; // fps10 × 4 帧
                // 编辑态引用 cell 0（切片表按 cell 口径——批⓪ ResolveSpriteRefs 对
                // 本体号引用会降级 cell 0，快照/重建逐字节比对因此失配；实体在
                // Play 中本就被 Animator 驱到 cell 区间，cell 0 才是真实用法）
                s.Get<ecs::SpriteRenderer>(hero).spriteId = sheet->SliceSpriteId(0);
            }
        }
        if (const AssetEntry* yc = ctx_.Assets().FindByGuid(kYamiHeroClipGuid);
            yc && !yc->missing) {
            if (const AssetEntry* ysh = ctx_.Assets().FindByGuid(kYamiHeroSheetGuid);
                ysh && !ysh->missing && ysh->Sliced()) {
                if (ecs::Entity y = ctx_.CreateSpriteEntityFromAsset(
                        "AnimYami", kYamiHeroSheetGuid, Vec2{840, 540});
                    !y.IsNull()) {
                    ecs::Animator2D& an = s.Emplace<ecs::Animator2D>(y);
                    an.clipId = (uint32_t)kYamiHeroClipGuid; // 9 帧 @8fps（Samples 素材）
                    s.Get<ecs::SpriteRenderer>(y).spriteId = ysh->SliceSpriteId(0);
                }
            }
        }
    }
    // M4.4 装配通路：--script 时挂 SpawnerBehaviour（Play 中刷怪断言用）
    if (ctx_.Scripts()) ctx_.AttachScript(root, 0, "SpawnerBehaviour");
    smokeSeeded_ = (uint32_t)s.AliveCount();
    ctx_.Select(root, false); // Inspector 有主选中
}

void EditorApp::SeedSmokeProject() {
    // 冒烟项目播种：Assets/smoke.png（64×64 红，四角亮标记）+ 固定 guid .meta
    // （TestScript.SpawnerBehaviour.kSpriteGuid 引用同一常量——资产链端到端可断言）
    std::error_code ec;
    std::filesystem::path root(launchCopy_.projectDir);
    std::filesystem::create_directories(root / "Assets", ec);
    // project.lemon（2026-09-22 BUG-1 守卫配套）：冒烟项目此前依赖 --project
    // "收养"任意目录的旧语义——守卫已废，播种时补工程标记
    if (std::filesystem::path pl = root / "project.lemon"; !std::filesystem::exists(pl, ec)) {
        std::ofstream f(pl, std::ios::trunc);
        f << "{\n  \"schemaVersion\": 1,\n  \"name\": \"smoke\",\n"
             "  \"engineVersion\": \"0.4.0-m4\"\n}\n";
    }
    std::filesystem::path png = root / "Assets" / "smoke.png";
    if (!std::filesystem::exists(png, ec)) {
        std::vector<uint8_t> px(64 * 64 * 4);
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x) {
                uint8_t* q = &px[((size_t)y * 64 + x) * 4];
                const bool mark = (x < 8 || x >= 56) && (y < 8 || y >= 56);
                q[0] = mark ? 255 : 200;
                q[1] = mark ? 255 : 40;
                q[2] = 40;
                q[3] = 255;
            }
        stbi_write_png(png.string().c_str(), 64, 64, 4, px.data(), 64 * 4);
    }
    std::filesystem::path meta = png.string() + ".meta";
    if (!std::filesystem::exists(meta, ec)) {
        std::ofstream f(meta, std::ios::trunc);
        f << "{\n  \"guid\": \"5bd31a7c10e9f2c8\",\n  \"type\": \"sprite\",\n  \"hash\": 0,\n"
             "  \"importedAt\": 0\n}\n";
    }

    // M5 批③动画链素材：anim-sheet + grid meta + anim.anim（固定 guid 见 kAnimSheetGuid）
    if (launch_->smokeAnim) WriteAnimSheetAssets(root / "Assets");
}

void EditorApp::SeedJudgementScene(uint64_t spawnGuid) {
    // 终验判据场景（§6 #1）：纯编辑器操作等价物——"走地图 + 刷怪"零代码。
    // 地图 = 背景精灵手摆（种子资产平铺）；角色 = InputMoverBehaviour（方向键走动）；
    // 刷怪器 = SpawnerBehaviour（种子 sprite 周期 Spawn）。
    using namespace lemon::ecs;
    Scene& s = ctx_.ActiveScene();
    // 地面：spawn.png 4×3 平铺（大比例 = 走地图背景）
    for (int ty = 0; ty < 3; ++ty)
        for (int tx = 0; tx < 4; ++tx) {
            char tag[24];
            std::snprintf(tag, sizeof(tag), "Ground%d_%d", ty, tx);
            ecs::Entity g = ctx_.CreateSpriteEntityFromAsset(tag, spawnGuid,
                                                             Vec2{160.0f + tx * 320.0f,
                                                                  140.0f + ty * 240.0f});
            if (!g.IsNull()) s.Get<Transform2D>(g).scale = Vec2{8.0f, 6.0f};
        }
    // 角色（走地图）
    ecs::Entity player = ctx_.CreateSpriteEntityFromAsset("Player", spawnGuid, Vec2{640, 360});
    if (ctx_.Scripts()) ctx_.AttachScript(player, 0, "InputMoverBehaviour");
    // 刷怪器
    ecs::Entity spawner = ctx_.CreateSpriteEntityFromAsset("Spawner", spawnGuid, Vec2{980, 220});
    if (ctx_.Scripts()) ctx_.AttachScript(spawner, 0, "SpawnerBehaviour");
    if (!player.IsNull()) ctx_.Select(player, false);
}

} // namespace lemon::editor


