// Lemon 编辑器 — 应用壳实现（M4.md §3.1；主循环承 anim-smoke 全链基线）
// M4.0：壳 + 默认布局 + DPI/字体 + smoke；M4.1：EditorContext/场景 IO/快捷键/关闭确认。
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

#include "App/ImGuiBackend.h"
#include "Assets/AssetDatabase.h"
#include "Assets/ProjectWizard.h"
#include "Interaction/ViewportRenderer.h"
#include "Tooling/Icons.h"
#include "Tooling/Theme.h"
#include "Components/BehaviorComponents.h"
#include "Components/CoreComponents.h"
#include "Components/GameplayComponents.h"
#include "Components/RenderComponents.h"
#include <unistd.h> // getpid（bench-survivor tempdir）
#include "ECS/Hierarchy.h"
#include "Core/Log.h"
#include "EditorContext.h"
#include "Panels/BuiltInPanels.h"
#include "Platform/Window.h"
#include "Renderer/RHI.h"
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
// M5 批④：C# Save.Flush → 编辑器域落盘（项目 .lemon/saves/；无项目 = no-op）
void HookSaveFlush(ecs::World& w) {
    if (g_app) g_app->Ctx().WriteSaveFile(w.Saves());
}

// ---- M5 批③：--smoke-anim 固定 guid（程序化 4 帧表 + clip；yami 包同段命名）----
constexpr uint64_t kAnimSheetGuid = 0x5bd31a7c30000001ull; // anim-sheet.png（128×32，4×32×32 格）
constexpr uint64_t kAnimClipGuid = 0x5bd31a7c30000002ull;  // anim.clip（fps10 × cells 0..3）
constexpr uint64_t kYamiHeroSheetGuid = 0x5bd31a7c10000001ull; // Samples yami-dungeon hero_1（在场即验）
constexpr uint64_t kYamiHeroClipGuid = 0x5bd31a7c20000001ull;  // hero-walk.clip（9 帧 @8fps）

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
    std::filesystem::path clip = assetsDir / "anim.clip";
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
    // 动画（M5 批③）：clipId = anim.clip GUID 低 32 位（EnterPlay 建表；fps10×4 帧）
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

// ---- M5 批④：vs-survivor 模板（Templates/vs-survivor 生成器 + 冒烟）------------
// 生成器 = 开发工具（--gen-vs-template 跑一次、产物入库随仓库版本管理）；
// --smoke-template = 面向回归的模板链冒烟（向导复制 → build → Play → 断言）。
// GUID 全固定：模板内引用锚点（06 §7"模板即项目"——资产 .meta 随行、
// project.lemon 的项目 GUID 由向导复制时重生成 = 存档隔离键）。
namespace vs_template {
constexpr uint64_t kGemPng = 0x7e57000000000001ull;       // gem.png（程序化 16×16）
constexpr uint64_t kBulletPng = 0x7e57000000000002ull;    // bullet.png
constexpr uint64_t kPiercePng = 0x7e57000000000003ull;    // pierce.png
constexpr uint64_t kBladePng = 0x7e57000000000004ull;     // blade.png
constexpr uint64_t kMobPf = 0x7e57100000000001ull;        // Mob.prefab
constexpr uint64_t kBossPf = 0x7e57100000000002ull;       // BossMob.prefab
constexpr uint64_t kBulletPf = 0x7e57100000000003ull;     // Bullet.prefab
constexpr uint64_t kPiercePf = 0x7e57100000000004ull;     // PierceBullet.prefab
constexpr uint64_t kGemPf = 0x7e57100000000005ull;        // Gem.prefab
constexpr uint64_t kBladePf = 0x7e57100000000006ull;      // Blade.prefab
// yami 素材沿用批③入库 guid（Templates 侧拷贝即引用同源）
constexpr uint64_t kHeroSheet = 0x5bd31a7c10000001ull;
constexpr uint64_t kMonsterSheet = 0x5bd31a7c10000003ull;
constexpr uint64_t kBossSheet = 0x5bd31a7c10000005ull;
constexpr uint64_t kHeroClip = 0x5bd31a7c20000001ull;
constexpr uint64_t kMonsterClip = 0x5bd31a7c20000002ull;

/// 程序化小图（16×16：宝石/直射弹/穿透弹/环绕刃）+ 固定 guid meta
void WriteProceduralAssets(const std::filesystem::path& assets) {
    namespace fs = std::filesystem;
    struct Spec { const char* file; uint64_t guid; uint8_t r, g, b; };
    const Spec specs[] = {
        {"gem.png", kGemPng, 70, 160, 255},     // 宝石蓝
        {"bullet.png", kBulletPng, 255, 220, 80}, // 直射黄
        {"pierce.png", kPiercePng, 255, 140, 60}, // 穿透橙
        {"blade.png", kBladePng, 180, 240, 255},  // 刃青白
    };
    for (const Spec& sp : specs) {
        const fs::path png = assets / sp.file;
        if (!fs::exists(png)) {
            std::vector<uint8_t> px(16 * 16 * 4);
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 16; ++x) {
                    const float dx = x - 7.5f, dy = y - 7.5f;
                    const float r = dx * dx + dy * dy;
                    uint8_t* q = &px[((size_t)y * 16 + x) * 4];
                    if (r > 7.5f * 7.5f) {
                        q[3] = 0; // 圆外透明
                    } else if (r > 5.5f * 5.5f) {
                        q[0] = sp.r / 2; q[1] = sp.g / 2; q[2] = sp.b / 2; q[3] = 255;
                    } else {
                        q[0] = sp.r; q[1] = sp.g; q[2] = sp.b; q[3] = 255;
                    }
                }
            stbi_write_png(png.string().c_str(), 16, 16, 4, px.data(), 16 * 4);
        }
        const fs::path meta = fs::path(png.string() + ".meta");
        if (!fs::exists(meta)) {
            std::ofstream f(meta, std::ios::trunc);
            f << "{\n  \"guid\": \"" << AssetDatabase::GuidToHex(sp.guid)
              << "\",\n  \"type\": \"sprite\",\n  \"hash\": 0,\n  \"importedAt\": 0\n}\n";
        }
    }
}

/// 模板 Game/ 脚本工程（csproj 固定名 Game → dll = Game.dll；HintPath 创建期锚）
void WriteGameSources(const std::filesystem::path& game, const std::string& sdkDir) {
    namespace fs = std::filesystem;
    {
        std::ofstream f(game / "Game.csproj", std::ios::trunc);
        f << "<Project Sdk=\"Microsoft.NET.Sdk\">\n"
          << "  <!-- Lemon vs-survivor 模板脚本工程（M5 批④） -->\n"
          << "  <PropertyGroup>\n"
          << "    <TargetFramework>net10.0</TargetFramework>\n"
          << "    <AllowUnsafeBlocks>true</AllowUnsafeBlocks>\n"
          << "    <Nullable>enable</Nullable>\n"
          << "    <AssemblyName>Game</AssemblyName>\n"
          << "    <RootNamespace>Game</RootNamespace>\n"
          << "    <GenerateRuntimeConfigurationFiles>false</GenerateRuntimeConfigurationFiles>\n"
          << "  </PropertyGroup>\n"
          << "  <ItemGroup>\n"
          << "    <Reference Include=\"Lemon.SDK\">\n"
          << "      <HintPath>" << (fs::path(sdkDir) / "Lemon.SDK.dll").string()
          << "</HintPath>\n"
          << "    </Reference>\n"
          << "  </ItemGroup>\n"
          << "</Project>\n";
    }
    {
        std::ofstream f(game / "GameMain.cs", std::ios::trunc);
        f << R"CS(using Lemon;

public static class GameMain
{
    /// <summary>一局共享态（M6a 批⓪ T4 三拆）：战斗写（计时/击杀/纪录/死亡），
    /// HUD/移动读。静态随 A 线换域重建、不经 StateBag 通道——迁移由 PlayerCombat
    /// 的 OnHotReloadOut/In 代收代还。</summary>
    public static class Run
    {
        public static float Time;  // 本局秒（死亡冻结）
        public static int Kills;   // 本局击杀
        public static int Best;    // 历史最高（Save "vs.best" 持久）
        public static bool Dead;   // 死亡结算相位（三脚本共用的闸）
    }

    public static void Configure()
    {
        // 注册序 = 跨类型 Update 执行序（04 §3.2）：移动 → 战斗 → HUD
        Lemon.Behaviours.Register<PlayerMovement>();
        Lemon.Behaviours.Register<PlayerCombat>();
        Lemon.Behaviours.Register<PlayerHud>();
    }
}
)CS";
    }
    {
        std::ofstream f(game / "PlayerMovement.cs", std::ios::trunc);
        f << R"CS(using Lemon;
using Lemon.Interop;

/// <summary>移动（M6a 批⓪ T4 拆分）：8 向 + 软竞技场钳制；死亡相位停走。</summary>
public sealed class PlayerMovement : LemonBehaviour
{
    private const float kArenaHalf = 1000f; // 软竞技场边界（脚本层钳制）

    protected override void Update()
    {
        if (GameMain.Run.Dead) return;
        var tf = gameObject.GetComponent<Transform2D>();
        var stats = gameObject.GetComponent<Stats>();
        Vec2 axis = Input.Axis;
        tf.Pos = new Vec2(
            System.Math.Clamp(tf.Pos.X + axis.X * stats.MoveSpeed * Time.DeltaTime,
                              -kArenaHalf, kArenaHalf),
            System.Math.Clamp(tf.Pos.Y + axis.Y * stats.MoveSpeed * Time.DeltaTime,
                              -kArenaHalf, kArenaHalf));
        gameObject.SetComponent(tf);
    }
}
)CS";
    }
    {
        std::ofstream f(game / "PlayerCombat.cs", std::ios::trunc);
        f << R"CS(using System.Collections.Generic;
using Lemon;
using Lemon.Interop;

/// <summary>战斗（M6a 批⓪ T4 拆分）：击杀/宝石掉落 + 环绕刃自愈 + 升级三选一
///（固定序轮换，零 RNG）+ 死亡结算/复活（死亡对话框点击）。一局共享态写
/// GameMain.Run（HUD 读）。</summary>
public sealed class PlayerCombat : LemonBehaviour
{
    // 模板资产 GUID（Templates/vs-survivor 生成期固定——引用锚点，勿改）
    private const string kGemPrefab = "7e57100000000005";
    private const string kBladePrefab = "7e57100000000006";
    private const uint kPiercePrefabLow = 0x00000004; // PierceBullet.prefab 低 32 位

    private static readonly string[] kOptions = {
        "移速 +10%", "磁力 +25%", "射速 +15%", "穿透弹", "生命上限 +25", "环绕之刃 +1",
    };

    private int _pendingLevels; // LevelUp 事件累计的待选次数
    private bool _cardsShown;
    private int _pickRotation;  // 三选一轮换序（确定性）
    private int _bladeCount = 2;
    private readonly List<ulong> _blades = new();
    private float _bladeAngle;

    public PlayerCombat()
    {
        // Subscribe 助手（M15）：实例销毁自动退订（裸 Events.Subscribe 只增不删，
        // 死亡→复活重挂会逐局累积订阅）
        Subscribe(GameEvent.LevelUp, m => {
            if (m.Src.Id == gameObject.Entity.Id) ++_pendingLevels;
        });
        Subscribe(GameEvent.Death, OnDeath);
    }

    private void OnDeath(GameEventMsg m)
    {
        var src = GameObject.From(m.Src);
        if (!src.Alive || !src.TryGetComponent<Meta>(out var meta)) return;
        if (meta.Team == 1) {
            ++GameMain.Run.Kills;
            if (src.TryGetComponent<Transform2D>(out var tf)) // 两阶段销毁：当帧可读
                Instantiate.Prefab(kGemPrefab, new Vec2(tf.Pos.X, tf.Pos.Y));
        } else if (m.Src.Id == gameObject.Entity.Id) {
            Die();
        }
    }

    protected override void Start()
    {
        GameMain.Run.Best =
            int.TryParse(Save.GetString("vs.best"), out var b) ? b : 0; // 上一局纪录
    }

    protected override void Update()
    {
        if (GameMain.Run.Dead) {
            // 死亡对话框：点击/数字键 1 → CardPick()==0 复活（消费式回读，与升级
            // 卡片同通道；批④后修④——R 键路径废弃，交互不依赖键盘焦点路由）
            if (Ui.CardPick() == 0) Revive();
            return;
        }
        GameMain.Run.Time += Time.DeltaTime;

        UpdateBlades(gameObject.GetComponent<Transform2D>());
        UpdateCards();
    }

    private void UpdateBlades(Transform2D playerTf)
    {
        _bladeAngle += 2.2f * Time.DeltaTime;
        while (_blades.Count < _bladeCount) { // 自愈：热重装/丢失即补（挂玩家当前位置）
            var g = Instantiate.Prefab(kBladePrefab,
                                       new Vec2(playerTf.Pos.X, playerTf.Pos.Y));
            _blades.Add(g.Entity.Id);
        }
        _blades.RemoveAll(id => !GameObject.From(new EntityHandle { Id = id }).Alive);
        for (int i = 0; i < _blades.Count; ++i) {
            var b = GameObject.From(new EntityHandle { Id = _blades[i] });
            if (!b.TryGetComponent<Transform2D>(out var bt)) continue;
            float a = _bladeAngle + i * (6.2831853f / _blades.Count);
            bt.Pos = new Vec2(playerTf.Pos.X + 90f * System.MathF.Cos(a),
                              playerTf.Pos.Y + 90f * System.MathF.Sin(a));
            bt.Rot = a;
            b.SetComponent(bt);
        }
    }

    private void UpdateCards()
    {
        if (_cardsShown) {
            int pick = Ui.CardPick();
            if (pick < 0) return;
            int n = _pickRotation - 1; // ShowCards 时已自增
            ApplyOption((n + pick * 2) % 6);
            --_pendingLevels;
            _cardsShown = false;
            Ui.HideCards();
            if (_pendingLevels <= 0) Time.Scale = 1f; // 选完恢复（多级连选继续冻结）
            return;
        }
        if (_pendingLevels > 0) {
            Time.Scale = 0f; // 卡片期间冻结（RNG 不消耗，批① D5 语义）
            int n = _pickRotation++;
            Ui.ShowCards("升级！三选一", kOptions[n % 6], kOptions[(n + 2) % 6],
                         kOptions[(n + 4) % 6]);
            _cardsShown = true;
        }
    }

    private void ApplyOption(int o)
    {
        switch (o) {
        case 0: { // 移速
            var st = gameObject.GetComponent<Stats>();
            st.MoveSpeed *= 1.10f;
            gameObject.SetComponent(st);
            break;
        }
        case 1: { // 磁力
            var st = gameObject.GetComponent<Stats>();
            st.PickupRadius *= 1.25f;
            gameObject.SetComponent(st);
            break;
        }
        case 2: { // 射速
            var sh = gameObject.GetComponent<Shooter>();
            sh.Interval = System.Math.Max(0.05f, sh.Interval * 0.85f);
            gameObject.SetComponent(sh);
            break;
        }
        case 3: { // 穿透弹（切弹种）
            var sh = gameObject.GetComponent<Shooter>();
            sh.ProjectileId = kPiercePrefabLow;
            gameObject.SetComponent(sh);
            break;
        }
        case 4: { // 生命上限
            var hp = gameObject.GetComponent<Health>();
            hp.Max += 25f;
            hp.Cur += 25f;
            gameObject.SetComponent(hp);
            break;
        }
        case 5: ++_bladeCount; break; // 环绕 +1（UpdateBlades 自愈补挂）
        }
    }

    private void Die()
    {
        if (GameMain.Run.Dead) return; // 多源 Death（弹道/区域）只结算一次
        GameMain.Run.Dead = true;
        _cardsShown = false; // 弃置在途升级卡（_pendingLevels 保留，复活后重弹）
        Time.Scale = 0f;
        int score = GameMain.Run.Kills * 10 + (int)GameMain.Run.Time;
        bool newBest = score > GameMain.Run.Best;
        if (newBest) {
            GameMain.Run.Best = score;
            Save.SetString("vs.best", score.ToString());
            Save.Flush(); // 立即落盘（ExitPlay 兜底之外的显式路径）
        }
        string title = newBest ? $"★ 新纪录 {score} 分！"
                               : $"本局 {score} 分（最高 {GameMain.Run.Best}）";
        Ui.Set("over", title, -1f, 0xFF5080FFu);
        Ui.ShowDialog(title, "复活");
    }

    private void Revive()
    {
        GameMain.Run.Dead = false;
        var hp = gameObject.GetComponent<Health>();
        hp.Cur = hp.Max;
        hp.IFrames = 2f; // 复活无敌 2s（StatSystem 递减）
        gameObject.SetComponent(hp);
        Time.Scale = 1f;
        Ui.HideCards();
        Ui.Clear("over");
    }

    // 热重载状态迁移（数值面，含 GameMain.Run 共享态——静态随域重建必须经包走；
    // 刃实体经 UpdateBlades 自愈重建轨道）
    protected override void OnHotReloadOut(StateBag bag)
    {
        bag.Set("time", GameMain.Run.Time);
        bag.Set("kills", GameMain.Run.Kills);
        bag.Set("best", GameMain.Run.Best);
        bag.Set("dead", GameMain.Run.Dead);
        bag.Set("pending", _pendingLevels);
        bag.Set("rotation", _pickRotation);
        bag.Set("blades", _bladeCount);
    }

    protected override void OnHotReloadIn(StateBag bag)
    {
        if (bag.TryGet("time", out float t)) GameMain.Run.Time = t;
        if (bag.TryGet("kills", out int k)) GameMain.Run.Kills = k;
        if (bag.TryGet("best", out int b)) GameMain.Run.Best = b;
        if (bag.TryGet("dead", out bool d)) GameMain.Run.Dead = d;
        if (bag.TryGet("pending", out int p)) _pendingLevels = p;
        if (bag.TryGet("rotation", out int r)) _pickRotation = r;
        if (bag.TryGet("blades", out int n)) _bladeCount = n;
    }
}
)CS";
    }
    {
        std::ofstream f(game / "PlayerHud.cs", std::ios::trunc);
        f << R"CS(using Lemon;
using Lemon.Interop;

/// <summary>HUD（M6a 批⓪ T4 拆分）：四要素行 + 波次横幅。读 GameMain.Run 共享态；
/// 死亡相位冻结末帧（"over" 结算行由战斗侧写）。</summary>
public sealed class PlayerHud : LemonBehaviour
{
    private const uint kColorHp = 0xFF30B0F0u;   // 血条红（ABGR）
    private const uint kColorXp = 0xFF30D8F0u;   // 经验金
    private const uint kColorTime = 0xFFF0F0F0u; // 计时白
    private const uint kColorKill = 0xFF4098F0u; // 击杀橙
    private const uint kColorWave = 0xFF60E0A0u; // 波次绿

    public PlayerHud()
    {
        Subscribe(GameEvent.WaveStart, m =>
            Ui.Set("wave", $"—— 第 {(int)m.P0 + 1} 波 ——", -1f, kColorWave));
    }

    protected override void Update()
    {
        if (GameMain.Run.Dead) return;
        var hp = gameObject.GetComponent<Health>();
        var xp = gameObject.GetComponent<XpProgress>();
        Ui.Set("hp", $"HP {(int)hp.Cur}/{(int)hp.Max}",
               hp.Max > 0f ? hp.Cur / hp.Max : 0f, kColorHp);
        Ui.Set("xp", $"LV {xp.Level} {(int)xp.Xp}/{(int)xp.XpToNext}",
               xp.XpToNext > 0f ? xp.Xp / xp.XpToNext : 0f, kColorXp);
        int t = (int)GameMain.Run.Time;
        Ui.Set("time", $"{t / 60}:{t % 60:00}", -1f, kColorTime);
        Ui.Set("kills", $"击杀 {GameMain.Run.Kills}", -1f, kColorKill);
        Ui.Set("best", $"最高纪录 {GameMain.Run.Best}", -1f);
    }
}
)CS";
    }
}

// --smoke-guid（M6a 批⓪ T5）：sprite 引用 GUID 稳定性链。三难并发——导入新图
//（字典序插队 = 全体 spriteId 重排）+ 资产改名（.meta 随行）+ 删 manifest
//（跨进程重开 = 无记账 fresh 分配）→ 重开后逐实体断言 guid 不断链、spriteId
// 归一到新号、零悬空、改名资产引用存活。T2 单测（TestSpriteGuidResolve）的
// 编辑器全链复证；纯资产/场景链（不编译脚本，独立 EditorContext 模拟重开进程）。
bool RunGuidSmokeChain(uint32_t spriteIdBase) {
    namespace fs = std::filesystem;
    using ecs::Entity;
    std::error_code ec;
    const fs::path tmp = fs::temp_directory_path() /
                         ("lemon-smoke-guid-" + std::to_string(::getpid()));
    fs::remove_all(tmp, ec);
    ProjectDesc d;
    d.parentDir = tmp.string();
    d.name = "GuidSmoke";
    d.engineVersion = "0.5.0-m5";
    d.templateName = "vs-survivor";
    d.templateDir = std::string(LEMON_TEMPLATE_DIR) + "/vs-survivor";
    const std::string root = ProjectWizard::Create(d);
    if (root.empty()) {
        LEMON_ERROR("smoke-guid：向导复制失败（模板缺失/不可写）");
        return false;
    }

    // 局1：开项目 + 场景，编辑态实例化六 prefab（T2 的 Instantiate 解析路径）
    // → 存档 → 记基线（tag → guid/id）
    EditorContext ctx;
    if (!ctx.Assets().OpenProject(root, spriteIdBase)) {
        LEMON_ERROR("smoke-guid：open project 失败");
        return false;
    }
    if (!ctx.OpenScene(root + "/Scenes/Main.scene")) {
        LEMON_ERROR("smoke-guid：open scene 失败");
        return false;
    }
    struct Row {
        std::string tag;
        uint64_t guid;
        uint32_t id;
    };
    auto collect = [](EditorContext& c, std::vector<Row>& out) {
        out.clear();
        c.EditScene().Each([&](Entity e) {
            if (const ecs::SpriteRenderer* sr = c.EditScene().TryGet<ecs::SpriteRenderer>(e))
                out.push_back(
                    {std::string(c.EditScene().Get<ecs::Meta>(e).tag), sr->spriteGuid,
                     sr->spriteId});
        });
    };
    const Vec2 spawnAt[6] = {{-300, 0}, {300, 0}, {0, -300}, {0, 300}, {-300, -300}, {300, 300}};
    const uint64_t prefabGuids[6] = {kMobPf,   kBossPf, kBulletPf,
                                     kPiercePf, kGemPf, kBladePf};
    for (int i = 0; i < 6; ++i)
        if (ctx.InstantiatePrefabAsset(prefabGuids[i], spawnAt[i]).IsNull()) {
            LEMON_ERROR("smoke-guid：prefab %d 实例化失败", i);
            return false;
        }
    if (!ctx.SaveScene()) {
        LEMON_ERROR("smoke-guid：基线存档失败");
        return false;
    }
    std::vector<Row> base;
    collect(ctx, base);
    if (base.size() != 7) { // Player + 六 prefab 根
        LEMON_ERROR("smoke-guid：基线实体数 %zu ≠ 7", base.size());
        return false;
    }
    for (const Row& r : base)
        if (r.guid == 0) {
            LEMON_ERROR("smoke-guid：基线实体 '%s' 无 spriteGuid（生成器未双写？）",
                        r.tag.c_str());
            return false;
        }

    // 三难并发：插队导入（字典序最前 → 全体 spriteId 后移）+ hero 表改名
    //（.meta 随行 = guid 存活）+ 删 manifest（重开 = 无记账 fresh 分配）
    {
        std::ofstream f(root + "/Assets/aaa_insert.png", std::ios::binary);
        f << "png-insert"; // DB 只哈希不解码，内容任意
    }
    fs::rename(root + "/Assets/dungeon_hero_1.png", root + "/Assets/hero_renamed.png", ec);
    fs::rename(root + "/Assets/dungeon_hero_1.png.meta", root + "/Assets/hero_renamed.png.meta",
               ec);
    fs::remove(root + "/.lemon/manifest.json", ec);

    // 局2：新 EditorContext = 模拟重开进程（DB 空表 + 无 manifest = 全体重排）
    EditorContext ctx2;
    if (!ctx2.Assets().OpenProject(root, spriteIdBase)) {
        LEMON_ERROR("smoke-guid：reopen project 失败");
        return false;
    }
    if (!ctx2.OpenScene(root + "/Scenes/Main.scene")) {
        LEMON_ERROR("smoke-guid：reopen scene 失败");
        return false;
    }
    std::vector<Row> after;
    collect(ctx2, after);
    if (after.size() != base.size()) {
        LEMON_ERROR("smoke-guid：重开实体数 %zu ≠ %zu", after.size(), base.size());
        return false;
    }
    int drifted = 0, guidBroken = 0, idWrong = 0, dangling = 0;
    for (const Row& b : base) {
        const Row* a = nullptr;
        for (const Row& r : after)
            if (r.tag == b.tag) a = &r;
        if (!a) {
            ++guidBroken;
            continue;
        }
        if (a->guid != b.guid) ++guidBroken;
        const AssetEntry* en = ctx2.Assets().FindByGuid(b.guid);
        if (!en || en->missing) {
            ++dangling;
            continue;
        }
        const uint32_t expect = en->sliceCount > 0 ? en->sliceBase : en->spriteId;
        if (a->id != expect) {
            ++idWrong;
            LEMON_WARN("smoke-guid row '%s': id %u → %u (expect %u, %s slices=%u base=%u)",
                       b.tag.c_str(), b.id, a->id, expect, en->relPath.c_str(),
                       en->sliceCount, en->sliceBase);
        }
        if (a->id != b.id) ++drifted; // 漂移确证（链非空转）
    }
    // 改名资产存活：guid → 新路径（引用不断链的直接证据）
    const AssetEntry* hero = ctx2.Assets().FindByGuid(kHeroSheet);
    const bool renamedOk =
        hero && hero->relPath == "Assets/hero_renamed.png" && !hero->missing;
    if (guidBroken || idWrong || dangling || !renamedOk || drifted < 3) {
        LEMON_ERROR("smoke-guid：guidBroken=%d idWrong=%d dangling=%d renamed=%d "
                    "drifted=%d（重排未生效 = 链空转）",
                    guidBroken, idWrong, dangling, renamedOk ? 1 : 0, drifted);
        return false;
    }
    std::printf("[lemon] smoke-guid: entities=%zu drift=%d/7 renamed=OK dangling=0 => OK\n",
                after.size(), drifted);
    fs::remove_all(tmp, ec);
    return true;
}
} // namespace vs_template

/// --gen-vs-template <dir>：产出 vs-survivor 模板项目文件（跑一次、入库）。
/// 结构 = 完整项目：yami 素材 + 程序化小图（.meta 固定 guid）+ Prefabs（固定 guid
/// 占位 meta → 扫描后覆写内容）+ Game/ 脚本 + Main.scene（玩家/导演 + 波表）。
bool GenerateVsTemplate(EditorContext& ctx, uint32_t spriteIdBase,
                        const std::string& outRoot) {
    namespace fs = std::filesystem;
    using namespace vs_template;
    std::error_code ec;
    const fs::path root = fs::absolute(outRoot, ec);
    // 2026-09-24 审查 F-02：remove_all 前先验目标"是本生成器产物"——仅认领含
    // project.lemon + Game/ 的 lemon 工程；任意已有目录一律拒绝删除。重新生成
    // （Templates/vs-survivor 入库目录满足标记）不受影响。
    if (fs::exists(root, ec)) {
        const bool lemonProject = fs::is_regular_file(root / "project.lemon", ec) &&
                                  fs::is_directory(root / "Game", ec);
        if (!lemonProject) {
            LEMON_ERROR("gen-vs-template：目标已存在且不是 lemon 工程（缺 project.lemon/"
                        "Game/ 标记），拒绝删除：%s",
                        root.string().c_str());
            return false;
        }
        fs::remove_all(root, ec);
    }
    for (const char* dir : {"Assets", "Prefabs", "Scenes", "Game", "Data", ".lemon/editor"})
        fs::create_directories(root / dir, ec);

    // 1) yami 素材拷贝（批③入库包 → 模板自带；.meta 随行 = guid/切片稳定）
    const fs::path yamiSrc = fs::path(LEMON_TEMPLATE_DIR).parent_path() /
                             "Samples/Assets/yami-dungeon";
    if (!fs::is_directory(yamiSrc, ec)) {
        LEMON_ERROR("gen-vs-template：yami 素材包缺失 %s", yamiSrc.string().c_str());
        return false;
    }
    for (const char* f : {"dungeon_hero_1.png", "dungeon_hero_1.png.meta",
                          "dungeon_monster_2.png", "dungeon_monster_2.png.meta",
                          "dungeon_boss_1.png", "dungeon_boss_1.png.meta",
                          "hero-walk.clip", "hero-walk.clip.meta",
                          "monster-walk.clip", "monster-walk.clip.meta"})
        fs::copy(yamiSrc / f, root / "Assets" / f, fs::copy_options::overwrite_existing, ec);

    // 2) 程序化小图 + Game/ 脚本工程 + prefab 占位（固定 guid meta 先行——
    //    OpenProject 扫描按 meta 记账，之后覆写 .prefab 内容 guid 不动）
    WriteProceduralAssets(root / "Assets");
#ifdef LEMON_SCRIPT_DIR
    WriteGameSources(root / "Game", LEMON_SCRIPT_DIR);
#endif
    struct Pf { const char* file; uint64_t guid; };
    for (const Pf& pf : {Pf{"Mob.prefab", kMobPf}, Pf{"BossMob.prefab", kBossPf},
                         Pf{"Bullet.prefab", kBulletPf}, Pf{"PierceBullet.prefab", kPiercePf},
                         Pf{"Gem.prefab", kGemPf}, Pf{"Blade.prefab", kBladePf}}) {
        {
            std::ofstream f(root / "Prefabs" / pf.file, std::ios::trunc);
            f << "{\"entities\":[],\"name\":\"tpl\",\"schemaVersion\":1}\n"; // 占位（后覆写）
        }
        std::ofstream m(fs::path((root / "Prefabs" / pf.file).string() + ".meta"),
                        std::ios::trunc);
        m << "{\n  \"guid\": \"" << AssetDatabase::GuidToHex(pf.guid)
          << "\",\n  \"type\": \"prefab\",\n  \"hash\": 0,\n  \"importedAt\": 0\n}\n";
    }
    {
        std::ofstream f(root / "project.lemon", std::ios::trunc);
        f << "{\n  \"schemaVersion\": 1,\n  \"name\": \"vs-survivor\",\n"
             "  \"engineVersion\": \"0.5.0-m5\",\n  \"guid\": \"tpl-placeholder\"\n}\n";
    }
    {
        std::ofstream f(root / "README.md", std::ios::trunc);
        f << "# vs-survivor 模板（M5 批④）\n\n"
             "吸血鬼幸存者式开局模板：8 向移动 + 直射弹（可升级穿透）+ 环绕刃 +\n"
             "经验宝石磁吸 + 升级三选一（数字键 1/2/3 或点击卡片）+ 16 波导演\n"
             "（t=565s Boss 波）+ HUD 四要素 + 死亡结算/最高分存档（死亡对话框点击复活）。\n\n"
             "由 `lemon-editor --gen-vs-template <dir>` 生成（改玩法请改 Game/ 下\n"
             "脚本或场景后重新生成，勿手改 .prefab 内 guid）。\n\n"
             "## 素材来源与许可\n\n"
             "- `dungeon_*` 精灵表与 `*.clip`：yami-rpg-editor（MIT，Copyright (c) 2025\n"
             "  Yami & Xuran & Contributors）——随模板再分发需在发布物保留版权声明\n"
             "  （仓库根 THIRD_PARTY.md 已登记）。\n"
             "- `gem/bullet/pierce/blade.png`：程序化生成（无版权负担）。\n\n"
             "## 玩法锚点\n\n"
             "- 玩家：`Player` 实体挂三脚本（M6a 批⓪ scripts[]：移动 → 战斗 → HUD，\n"
             "注册序 = 跨类型 Update 执行序）；一局共享态在 GameMain.Run。\n"
             "- 波次：`Director` 实体 WaveDirector（Inspector 数组段可调参）。\n"
             "- 三选一池：PlayerCombat.kOptions（固定序轮换，零 RNG = 回放友好）。\n"
             "- 素材引用：.scene 双写 spriteGuid（真源）+ spriteId（进程内号）——\n"
             "改名/移位/manifest 重建后打开场景自动归一（M6a 批⓪ T2）。\n";
    }

    // 3) 打开项目（扫描记账）→ 播种场景 + 覆写 prefab 内容。
    // base = 调用方传入（OpenProjectPipeline 同规则——真实打开路径一致）
    if (!ctx.Assets().OpenProject(root.string(), spriteIdBase)) return false;
    ctx.NewScene();
    ecs::Scene& s = ctx.EditScene();

    // 玩家（hero 表 0 帧 + 走路 clip）。M6a 批⓪ T2：guid/id 双写（CreateSpriteEntityByGuid
    // 携切片表 cell-0 惯例——切片表首帧 = sliceBase，整图 = 本体号）
    ecs::Entity player = ctx.CreateSpriteEntityByGuid("Player", kHeroSheet);
    s.Get<ecs::Meta>(player).team = 0;
    s.Emplace<ecs::Health>(player, ecs::Health{.max = 100.0f, .cur = 100.0f});
    s.Emplace<ecs::Stats>(player).pickupRadius = 96.0f;
    s.Emplace<ecs::XpProgress>(player, ecs::XpProgress{.xpToNext = 30.0f});
    ecs::Shooter& psh = s.Emplace<ecs::Shooter>(player);
    psh.projectileId = (uint32_t)kBulletPf;
    psh.interval = 0.12f;
    psh.range = 2000.0f;
    psh.targetTeam = 1;
    s.Emplace<ecs::Animator2D>(player).clipId = (uint32_t)kHeroClip;
    // M6a 批⓪ T4 三拆：槽序镜像注册序（移动 → 战斗 → HUD）；Game/ 不入资产扫描
    // → scriptGuid 恒 0（className 是持久键）
    ctx.AttachScript(player, 0, "PlayerMovement");
    ctx.AttachScript(player, 0, "PlayerCombat");
    ctx.AttachScript(player, 0, "PlayerHud");

    // 导演（16 波：15 波小怪递增 + t=565s Boss；后波接管语义下条目都在波内完成）
    ecs::Entity director = ctx.CreateEntity("Director");
    ecs::WaveDirector& wd = s.Emplace<ecs::WaveDirector>(director);
    wd.spawnTeam = 1;
    wd.capAlive = 300;
    wd.waveCount = 16;
    for (int w = 0; w < 15; ++w) {
        ecs::WaveDef& def = wd.waves[w];
        def.startTime = 5.0f + 35.0f * (float)w;
        def.rampMult = 1.0f;
        def.entryCount = 2;
        def.entries[0] = ecs::WaveEntry{.prefabId = (uint32_t)kMobPf,
                                        .count = (uint16_t)(30 + w * 4),
                                        .interval = 0.6f, .range = 560.0f};
        def.entries[1] = ecs::WaveEntry{.prefabId = (uint32_t)kMobPf,
                                        .count = (uint16_t)(15 + w * 2),
                                        .interval = 0.35f, .range = 320.0f};
    }
    {
        ecs::WaveDef& def = wd.waves[15];
        def.startTime = 565.0f;
        def.entryCount = 1;
        def.entries[0] = ecs::WaveEntry{.prefabId = (uint32_t)kBossPf, .count = 1,
                                        .interval = 1.0f, .range = 80.0f};
    }

    // prefab 内容（scratch 实体 → SaveEntityTree → 覆写 .prefab；导出后销毁）
    auto exportPrefab = [&](const char* tag, uint32_t team, uint64_t spriteGuid,
                            auto build) -> bool {
        ecs::Entity e = ctx.CreateSpriteEntityByGuid(tag, spriteGuid);
        s.Get<ecs::Meta>(e).team = team;
        build(e);
        const std::string json = ecs::SceneArchive::SaveEntityTree(s, e);
        ctx.DestroyEntityTree(e);
        if (json.empty()) return false;
        std::ofstream f(root / "Prefabs" / (std::string(tag) + ".prefab"),
                        std::ios::trunc);
        f << json;
        return true;
    };
    if (!exportPrefab("Mob", 1, kMonsterSheet, [&](ecs::Entity e) {
            s.Emplace<ecs::Health>(e, ecs::Health{.max = 20.0f, .cur = 20.0f});
            s.Emplace<ecs::Knockback>(e);
            s.Emplace<ecs::Velocity>(e);
            s.Emplace<ecs::Animator2D>(e).clipId = (uint32_t)kMonsterClip;
            ecs::Chase& ch = s.Emplace<ecs::Chase>(e);
            ch.speed = 60.0f;
            ch.aggroRange = 2500.0f;
            ch.keepRange = 20.0f;
            ch.targetTeam = 0;
            ecs::Hazard& hz = s.Emplace<ecs::Hazard>(e); // 近身接触伤害
            hz.dps = 8.0f;
            hz.tickInterval = 0.8f;
            hz.radius = 24.0f;
        }))
        return false;
    if (!exportPrefab("BossMob", 1, kBossSheet, [&](ecs::Entity e) {
            s.Emplace<ecs::Health>(e, ecs::Health{.max = 600.0f, .cur = 600.0f});
            s.Emplace<ecs::Knockback>(e);
            s.Emplace<ecs::Velocity>(e);
            s.Emplace<ecs::Animator2D>(e).clipId = 0; // boss clip 帧率低，先静态
            ecs::Chase& ch = s.Emplace<ecs::Chase>(e);
            ch.speed = 32.0f;
            ch.aggroRange = 3000.0f;
            ch.keepRange = 26.0f;
            ch.targetTeam = 0;
            ecs::Hazard& hz = s.Emplace<ecs::Hazard>(e);
            hz.dps = 18.0f;
            hz.tickInterval = 0.5f;
            hz.radius = 40.0f;
        }))
        return false;
    auto bulletBody = [&](ecs::Entity e, float dmg, uint8_t pierce, float life) {
        s.Emplace<ecs::Velocity>(e);
        ecs::Projectile& pr = s.Emplace<ecs::Projectile>(e);
        pr.damage = dmg;
        pr.speed = 320.0f;
        pr.lifetime = life;
        pr.pierce = pierce;
    };
    if (!exportPrefab("Bullet", 0, kBulletPng, [&](ecs::Entity e) {
            bulletBody(e, 8.0f, 0, 2.5f);
        }))
        return false;
    if (!exportPrefab("PierceBullet", 0, kPiercePng,
                      [&](ecs::Entity e) { bulletBody(e, 6.0f, 3, 3.0f); }))
        return false;
    if (!exportPrefab("Gem", 2, kGemPng, [&](ecs::Entity e) {
            ecs::Collectible& c = s.Emplace<ecs::Collectible>(e);
            c.kind = 0;
            c.value = 1.0f;
        }))
        return false;
    if (!exportPrefab("Blade", 0, kBladePng, [&](ecs::Entity e) {
            ecs::Hazard& hz = s.Emplace<ecs::Hazard>(e);
            hz.dps = 6.0f;
            hz.tickInterval = 0.4f;
            hz.radius = 44.0f;
        }))
        return false;

    // 4) 场景落盘 + 终态记账（prefab 内容覆写后 hash 刷新）
    {
        std::ofstream f(root / "Scenes" / "Main.scene", std::ios::trunc);
        f << ecs::SceneArchive::Save(s);
    }
    ctx.Assets().Rescan();
    ctx.Assets().SaveManifest();
    LEMON_LOG("gen-vs-template：OK → %s（scene %zuB，%u 实体）", root.string().c_str(),
              ecs::SceneArchive::Save(s).size(), s.AliveCount());
    return true;
}

// ImGui 错误汇（1.92 内部回调口；DockBuilder 同源引用 imgui_internal）：ID 冲突/
// 空标签等程序员错误在这里现形——冒烟断言清零（M4.5 修复 Inspector ##v 撞号后
// 加的程序化防线：这类错只在交互时弹窗，无头冒烟原本测不到）。
int g_imguiErrorCount = 0;
// M5 批④ --smoke-template 证据计数（事件 sink + 帧循环采样写入；verdict 汇总）
int g_tplWaveStarts = 0, g_tplLevelUps = 0, g_tplDeaths = 0;
int g_tplGems = 0, g_tplMobs = 0; // 峰值快照（帧内采样）
char g_tplHudRows[64] = "";
bool g_tplHudOk = false, g_tplBestLoaded = false, g_tplWaveRow = false;
bool g_tplCardsSeen = false, g_tplPicked = false, g_tplCardsHidden = false;
bool g_tplDeathSeen = false, g_tplRevived = false, g_tplScriptOk = true; // 批④后修④死亡链
bool g_tplDeathArmed = false; // 压血一shot（站桩下自动炮火清怪快于刷怪，磨不死）
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

// ---- 最近项目（M4.6 §4-4；$HOME/.lemon/recent.json，用户级跨项目共享）----
// 解析失败 = 静默清空重来（recent 是便利件不是账本，任何损坏不得阻断启动）。
std::string RecentProjectsPath() {
    const char* home = std::getenv("HOME");
    return home ? std::string(home) + "/.lemon/recent.json" : std::string();
}
std::vector<std::string> LoadRecentProjects() {
    std::vector<std::string> out;
    const std::string p = RecentProjectsPath();
    if (p.empty()) return out;
    std::error_code ec;
    std::ifstream f(p, std::ios::binary);
    if (!f) return out;
    std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    const nlohmann::json doc = nlohmann::json::parse(text, nullptr, false);
    if (doc.is_discarded() || !doc.contains("projects")) return out;
    for (const auto& e : doc.at("projects"))
        if (e.is_string()) out.push_back(e.get<std::string>());
    return out;
}
void SaveRecentProjects(const std::vector<std::string>& v) {
    const std::string p = RecentProjectsPath();
    if (p.empty()) return;
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(p).parent_path(), ec);
    nlohmann::json doc;
    doc["version"] = 1;
    doc["projects"] = v;
    std::ofstream of(p, std::ios::binary | std::ios::trunc);
    of << doc.dump(2);
}
void PushRecentProject(const std::string& root, std::vector<std::string>& cur) {
    cur.erase(std::remove(cur.begin(), cur.end(), root), cur.end());
    cur.insert(cur.begin(), root);
    if (cur.size() > 5) cur.resize(5);
    SaveRecentProjects(cur);
}
} // namespace

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
    // Profiler 补进 bottom 区（BUG-3：此前未停靠 → 首启以浮窗随机遮挡
    // Hierarchy）；先于 Console/Assets 停靠 = 不抢当前标签，默认隐藏页
    ImGui::DockBuilderDockWindow("Profiler", bottomId);
    ImGui::DockBuilderDockWindow("Console", bottomId);
    ImGui::DockBuilderDockWindow("Assets", bottomId);  // 同区域 = 标签页
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
            if (ctx_.ExitPlay()) tabFocusPending_ = -1;
            else LEMON_WARN("Stop 后快照校验失败（编辑场景已按快照重建）");
        } else if (TryEnterPlay()) {
            tabFocusPending_ = 1;
        }
    }
    ImGui::PopStyleColor(3);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", playing ? "Stop（恢复编辑场景）" : "Play（进入沙盒）");
    ImGui::SameLine();
    ImGui::BeginDisabled(!playing);
    if (ui::IconButton(*this, IconKind::Pause, "##pause", paused_)) paused_ = !paused_;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", "暂停/继续（仅 Play 态）");
    ImGui::SameLine();
    if (ui::IconButton(*this, IconKind::Step, "##step", false)) singleStep_ = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", "单步一帧（仅 Play 态）");
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
    if (!picker_.IsOpen()) {
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S) && !playing_) MenuSaveScene();
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_O) && !playing_) MenuOpenScene();
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_D)) {
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
        if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
            // 结构轨：删除前快照（此前漏推——Del 键删完 Ctrl+Z 无效，与右键
            // "删除 (Del)" 菜单不对称；smoke-ui 真人链路抓到）
            const std::string before = ctx_.SnapshotSceneJson();
            bool any = false;
            for (ecs::Entity e : ctx_.Selection()) {
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
                if (ctx_.ExitPlay()) tabFocusPending_ = -1;
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
        ImGui::SetWindowFocus(tabFocusPending_ > 0 ? "Game" : "Scene");
        tabFocusPending_ = 0;
    }
    testhooks::SetEnabled(launchCopy_.smokeUi); // 性能批②：仅注入会话登记矩形
    testhooks::ClearAll();     // --smoke-ui 矩形登记每帧重建（防陈旧矩形误导注入）
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
                if (ctx_.EnterPlay()) tabFocusPending_ = 1;
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

// ---- 场景 IO 动作 ----
void EditorApp::MenuNewScene() {
    if (ctx_.dirty && !ConfirmUnsaved(PendingSceneOp::NewScene)) return;
    ctx_.NewScene();
    LEMON_LOG("新建场景（untitled）");
}

void EditorApp::MenuOpenScene() {
    if (ctx_.dirty && !ConfirmUnsaved(PendingSceneOp::OpenScene)) return;
    pickerMode_ = PickerMode::Open;
    picker_.Open("打开场景", PickerStartDir(), "", ".scene");
}

// 场景选择器起始目录：当前场景父目录 → 项目 Scenes/ → 项目根 → CWD（无项目）。
// 新建场景无路径时此前退 CWD（= 启动目录），与打开的项目无关（2026-09-22 反馈）。
// 注意只能传存在的目录：FilePicker 对不存在的默认目录会自退 CWD。
std::string EditorApp::PickerStartDir() {
    std::error_code ec;
    const std::string cur = std::filesystem::path(ctx_.ScenePath()).parent_path().string();
    if (!cur.empty()) return cur;
    const std::string& root = ctx_.Assets().ProjectRoot();
    if (!root.empty()) {
        if (std::filesystem::is_directory(root + "/Scenes", ec)) return root + "/Scenes";
        return root; // 老项目无 Scenes/：退项目根（勿传不存在目录）
    }
    return std::filesystem::current_path(ec).string();
}

void EditorApp::MenuOpenRecentScene(std::string path) {
    if (ctx_.Playing()) {
        LEMON_WARN("Play 中不能切换场景（先 Stop）");
        return;
    }
    if (path == ctx_.ScenePath()) return; // 已是当前场景：无操作
    if (ctx_.dirty) { // 脏场景：确认后直达路径（模态期间持有）
        pendingScenePath_ = path;
        ConfirmUnsaved(PendingSceneOp::RecentScene);
        return;
    }
    ctx_.OpenScene(path);
}

void EditorApp::MenuSaveScene() {
    if (ctx_.ScenePath().empty()) {
        MenuSaveSceneAs();
        return;
    }
    ctx_.SaveScene();
}

void EditorApp::MenuSaveSceneAs() {
    pickerMode_ = PickerMode::Save;
    picker_.Open("另存场景", PickerStartDir(), ctx_.SceneName(), ".scene");
}

bool EditorApp::ConfirmUnsaved(PendingSceneOp after) {
    if (!ctx_.dirty) return true;
    quitConfirmOpen_ = true; // 复用确认模态（按上下文分流文案与去向）
    confirmContext_ = ConfirmContext::SceneOp;
    pendingSceneOp_ = after; // 保存/丢弃后续做（取消则作废）
    return false;            // 异步：模态按钮里推进（2026-09-21 补齐 M4.2 欠账）
}

// ---------------------------------------------------------------- 资产 ----
void EditorApp::MenuImportAsset() {
    // 无项目守卫：AssetsRoot() = "/Assets"（根_),拷贝必然失败且报错误导（M4.6 实测坑）
    if (ctx_.Assets().ProjectRoot().empty()) {
        LEMON_ERROR("导入失败：未打开项目。文件 → 新建项目... 或 打开项目...（也可 --project <dir> 启动）");
        return;
    }
    pickerMode_ = PickerMode::Import;
    picker_.Open("导入资产", ctx_.Assets().AssetsRoot(), "", ""); // 任意扩展名
}

void EditorApp::MenuOpenProject() {
    if (ctx_.Playing()) {
        LEMON_WARN("Play 中不能切换项目（先 Stop）");
        return;
    }
    if (ctx_.dirty && !ConfirmUnsaved(PendingSceneOp::OpenProject)) return; // 脏场景确认（同款异步环）
    // 起点目录：已开项目 → 其父目录（同级切换常见）；否则 HOME
    namespace fs = std::filesystem;
    std::error_code ec;
    std::string start;
    const std::string& cur = ctx_.Assets().ProjectRoot();
    if (!cur.empty()) start = fs::path(cur).parent_path().string();
    if (start.empty() || !fs::is_directory(start, ec)) {
        if (const char* home = std::getenv("HOME")) start = home;
        else start = fs::current_path(ec).string();
    }
    pickerMode_ = PickerMode::OpenProject;
    picker_.OpenDir("打开项目（选择项目目录）", start); // M4.6 §4-2：选目录而非 project.lemon
}

bool EditorApp::OpenProjectInSession(const std::string& root) {
    // 切项目落地（选择器/最近项目/引导卡共用）：管线 + 新会话场景。
    // Play/脏场景守卫在调用方（菜单入口已拦；此函数为最后一道防线）。
    if (ctx_.Playing()) {
        LEMON_WARN("Play 中不能切换项目（先 Stop）");
        return false;
    }
    if (!OpenProjectPipeline(root)) return false;
    if (ctx_.dirty) LEMON_WARN("切项目：场景有未保存更改，已被丢弃");
    ctx_.NewScene(); // 切项目 = 新会话场景（旧场景引用旧项目资产/脚本）
    return true;
}

void EditorApp::RescanAssets() {
    AssetDatabase& db = ctx_.Assets();
    db.Rescan();
    const AssetDatabase::ChangeSet& cs = db.LastChange();
    for (uint64_t g : cs.added)
        if (const AssetEntry* e = db.FindByGuid(g); e && e->type == AssetType::Sprite)
            gpuAssets_.ImportSprite(*e);
    for (uint64_t g : cs.modified)
        if (const AssetEntry* e = db.FindByGuid(g); e && e->type == AssetType::Sprite)
            gpuAssets_.ImportSprite(*e);
    for (uint64_t g : cs.removed) gpuAssets_.Evict(g); // 幽灵页（号保留；M6 图集回收）
    db.SaveManifest();
    if (!cs.Empty())
        LEMON_LOG("资产重扫：+%zu ~%zu -%zu", cs.added.size(), cs.modified.size(),
                  cs.removed.size());
}

// ------------------------------------------------ 项目/脚本管线（M4.5）----
// ---- M4.6b 日常编辑效率（§5）----
void EditorApp::CopySelection() {
    // §5-1：拷贝"选中子树的根"（祖先也在选中集内的跳过——整树由祖先携带）。
    // 树 JSON 经 SceneArchive（父子结构/组件全量；guid 由粘贴侧换新）
    entityClip_.clear();
    entityClipRootPos_.clear();
    ecs::Scene& s = ctx_.ActiveScene();
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
        if (ancestorSelected) continue;
        entityClip_.push_back(ecs::SceneArchive::SaveEntityTree(s, e));
        Vec2 pos{0, 0};
        if (const ecs::Transform2D* t = s.TryGet<ecs::Transform2D>(e)) pos = t->pos;
        entityClipRootPos_.push_back(pos);
    }
    if (!entityClip_.empty())
        LEMON_LOG("已复制 %zu 个实体（子树结构随行，Ctrl+V 粘贴）", entityClip_.size());
}

void EditorApp::PasteClipboard() {
    if (entityClip_.empty() || ctx_.Playing()) return;
    const std::string before = ctx_.SnapshotSceneJson();
    ecs::Scene& s = ctx_.ActiveScene();
    bool any = false;
    for (size_t i = 0; i < entityClip_.size(); ++i) {
        ecs::Entity root = ecs::SceneArchive::LoadEntityTree(s, entityClip_[i]);
        if (root.IsNull()) continue;
        // 相对偏移：根整体 +24/+24（连续粘贴不与原件叠死；子树相对位置随序列化保留）
        if (s.Has<ecs::Transform2D>(root))
            s.Get<ecs::Transform2D>(root).pos = entityClipRootPos_[i] + Vec2{24.0f, 24.0f};
        ctx_.Select(root, any); // 粘贴根全进选择集（末位 = 主选中）
        any = true;
    }
    if (any) {
        ctx_.PushStructuralUndo("粘贴实体", before);
        LEMON_LOG("已粘贴 %zu 个实体（偏移 +24,+24）", entityClip_.size());
    }
}

void EditorApp::ImportDroppedFile(const std::string& absPath) {
    // §5-3：OS 拖入窗口的文件 → 当前资产目录（AssetBrowser 浏览目录；面板不可见 = 根）
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::is_regular_file(absPath, ec)) {
        LEMON_WARN("拖入跳过（非文件）：%s", absPath.c_str());
        return;
    }
    if (ctx_.Assets().ProjectRoot().empty()) {
        LEMON_ERROR("拖入导入失败：未打开项目。文件 → 新建项目... 或 打开项目...");
        return;
    }
    std::string subDir; // ImportFile 的 relDest 相对 Assets/（"" = 根）
    for (auto& en : panels_.Entries())
        if (auto* browser = dynamic_cast<AssetBrowserPanel*>(en.panel)) {
            const std::string& d = browser->CurrentDir(); // "" 或 "Assets[/x]"
            if (d.rfind("Assets/", 0) == 0) subDir = d.substr(7);
            break;
        }
    if (!subDir.empty() && subDir.back() != '/') subDir += '/';
    // 重名不覆盖：自动加序号（拖同名文件静默覆盖旧资产太危险）
    const std::string stem = fs::path(absPath).stem().string();
    const std::string ext = fs::path(absPath).extension().string();
    std::string relDest = subDir + stem + ext;
    for (int i = 2; ctx_.Assets().FindByPath("Assets/" + relDest); ++i)
        relDest = subDir + stem + " " + std::to_string(i) + ext;
    if (const AssetEntry* e = ctx_.Assets().ImportFile(absPath, relDest)) {
        if (e->type == AssetType::Sprite) gpuAssets_.ImportSprite(*e);
        LEMON_LOG("拖入导入：%s（guid %016llx）", e->relPath.c_str(),
                  (unsigned long long)e->guid);
    }
}

void EditorApp::QueueScriptRebuild(const char* reason) {
    // §5-5：排队后本帧 BuildUI 画"编译中…" → 下帧主循环才真构建（dotnet 阻塞 1–2s
    // 期间屏幕上留着提示；主线程阻塞现状不动 = §6 观察项）
    if (compileQueued_) return; // 已排队（合并）
    compileQueued_ = true;
    compileQueuedReason_ = reason;
}

void EditorApp::LogCompileErrors(const std::string& dotnetOutput) {
    // §5-6：dotnet 输出 → `file(l,c): error CSxxxx: msg` 红字进 Console（可读性：
    // 绝对路径裁成项目相对）
    const std::vector<std::string> errs = ProjectWizard::ExtractCompileErrors(dotnetOutput);
    if (errs.empty()) {
        std::string snippet = dotnetOutput.substr(0, 400);
        LEMON_ERROR("编译失败（dotnet 输出无 error 行；输出片段）：%s", snippet.c_str());
        return;
    }
    const std::string prefix = ctx_.Assets().ProjectRoot() + "/";
    for (const std::string& e : errs) {
        std::string line = e;
        if (line.rfind(prefix, 0) == 0) line = line.substr(prefix.size());
        LEMON_ERROR("编译错误：%s", line.c_str());
    }
    if (errs.size() >= 50) LEMON_WARN("编译错误超 50 条，仅列前 50");
}

// ------------------------------------------------ 项目/脚本管线（M4.5）----
bool EditorApp::FindGameProject(std::string& csproj, std::string& dll) {
    namespace fs = std::filesystem;
    const std::string& root = ctx_.Assets().ProjectRoot();
    if (root.empty()) return false;
    std::error_code ec;
    for (auto it = fs::directory_iterator(root + "/Game", ec);
         it != fs::directory_iterator(); it.increment(ec)) {
        if (ec || !it->is_regular_file(ec)) continue;
        if (it->path().extension() != ".csproj") continue;
        csproj = it->path().string();
        dll = root + "/.lemon/bin/" +
              it->path().stem().string() + ".dll";
        return true;
    }
    return false;
}

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
    // spriteId 基址 = 程序化图集登记后首个可用号（恒定；跨会话稳定由 manifest 记账）
    const uint32_t spriteIdBase = viewport_->Assets().Registry().SpriteCount() + 1;
    if (!ctx_.Assets().OpenProject(projectRoot, spriteIdBase)) return false;
    gpuAssets_.Init(*device_, ui_.get(), &viewport_->Assets().Registry(),
                    ctx_.Assets(), /*firstSlot=*/3); // 0=调色板 1=字体页 2=图标形状页(M4.7b)
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
                                  launch_->finalTest || !launch_->smokeClose.empty();
    if (!injectionSession)
        PushRecentProject(ctx_.Assets().ProjectRoot(), recentProjects_);
    ctx_.LoadRecentScenes(); // M4.8-b：项目内最近场景随项目装载
    return true;
}

bool EditorApp::InitScriptHostFrom(const std::string& dllAbs) {
#ifdef LEMON_SCRIPT_DIR
    namespace fs = std::filesystem;
    if (!fs::exists(dllAbs)) {
        LEMON_WARN("脚本装配失败：程序集不存在 %s", dllAbs.c_str());
        return false;
    }
    // 不变量先行：ctx 指针先清，宿主怎么动都不悬空（Profiler 每帧经 ctx.Scripts()
    // 调 GcAllocated——悬空 = SIGSEGV，M4.6 实测）
    ctx_.SetScriptHost(nullptr);
    // 已有宿主（会话内切项目/二次装配）：CoreCLR 进程单例，二次 Initialize 必失败
    // （script-tests 探针钉板 second-host init=0）——复用宿主走 A 线换装装配新项目
    // 程序集。原实现 make_unique 先毁旧宿主 → ctx 悬空 + 二次初始化失败 = 闪退双因
    if (host_) {
        const auto info = host_->HotReloadAssembly(dllAbs.c_str());
        if (info.ok) {
            ctx_.SetScriptHost(host_.get());
            LEMON_LOG("脚本域换装至：%s（复用宿主）", dllAbs.c_str());
            return true;
        }
        LEMON_WARN("脚本域换装失败（%s）——转无脚本状态（修错后可再装配）", dllAbs.c_str());
        return false;
    }
    host_ = std::make_unique<scripting::ScriptHost>();
    // DomainManager 要求绝对路径（ALC LoadFromAssemblyPath 约束）
    std::error_code eca;
    std::string scriptAbs = std::filesystem::absolute(dllAbs, eca).generic_string();
    if (host_->Initialize(nullptr, LEMON_SCRIPT_DIR "/Lemon.Entry.runtimeconfig.json",
                          LEMON_SCRIPT_DIR "/Lemon.Entry.dll") &&
        host_->LoadUserAssembly(scriptAbs.c_str())) {
        ctx_.SetScriptHost(host_.get());
        LEMON_LOG("脚本宿主就绪：%s（类型 %zu 个）", scriptAbs.c_str(),
                  ctx_.ScriptTypeNames().size());
        return true;
    }
    LEMON_WARN("脚本宿主初始化失败（%s）——无脚本继续", scriptAbs.c_str());
    host_.reset();
#endif
    return false;
}

bool EditorApp::ScriptSourceChanged() {
    // FileWatcher 只报"有变化"；这里过滤出真正需要重编译的源写（.cs/.csproj，
    // 排除 obj/bin 生成物——dotnet build 会改写它们，否则自我触发死循环）
    namespace fs = std::filesystem;
    const std::string gameDir = ctx_.Assets().ProjectRoot() + "/Game";
    std::error_code ec;
    int64_t newest = 0;
    for (auto it = fs::recursive_directory_iterator(gameDir,
                                                    fs::directory_options::skip_permission_denied,
                                                    ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        const fs::directory_entry& de = *it;
        const std::string name = de.path().filename().string();
        if (de.is_directory(ec)) {
            if (name == "obj" || name == "bin" || (!name.empty() && name[0] == '.'))
                it.disable_recursion_pending();
            continue;
        }
        if (name.size() < 3) continue;
        const std::string ext = de.path().extension().string();
        if (ext != ".cs" && ext != ".csproj") continue;
        auto wt = fs::last_write_time(de.path(), ec);
        if (ec) continue;
        const int64_t s = (int64_t)wt.time_since_epoch().count();
        if (s > newest) newest = s;
    }
    if (newest == 0 || newest <= lastHandledCsWrite_) return false;
    lastHandledCsWrite_ = newest;
    return true;
}

bool EditorApp::TryHotReloadScripts(const char* reason) {
    if (!host_) {
        // 无宿主 + 有 Game/ 工程 = 启动期编译失败后的恢复路径（2026-09-22 与 Play
        // 阻断配套）：此前直接跳过 = 修错保存后必须重启编辑器。这里试首装——
        // 修错 → watcher → 编译队列 → 本函数 → InitScriptHostFrom（无宿主即首装
        // 路径），成功后 Play 阻断自动解除。
        std::string csproj, dll;
        if (!FindGameProject(csproj, dll)) {
            LEMON_WARN("热重载跳过：无脚本宿主（%s）", reason);
            return false;
        }
        std::string buildOut;
        if (ProjectWizard::BuildGameProject(csproj, ctx_.Assets().ProjectRoot() + "/.lemon/bin",
                                            nullptr, &buildOut) != 0) {
            LEMON_ERROR("脚本首装编译失败（Play 仍被阻断）：dotnet build（%s）", reason);
            LogCompileErrors(buildOut);
            return false;
        }
        if (InitScriptHostFrom(dll)) {
            LEMON_LOG("脚本宿主已装配（%s）——Play 可用", reason);
            return true;
        }
        return false;
    }
    std::string csproj, dll;
    const bool hasProject = FindGameProject(csproj, dll);
    // 无 Game/ 工程（--script 直载 dll 形态）：dll 可能已被外部重编——直接换装同一文件
    if (!hasProject) {
        dll = launch_->script;
        if (dll.empty()) return false;
    }
    const auto t0 = std::chrono::steady_clock::now();
    if (hasProject) {
        std::string buildOut; // M4.6 §5-6：捕获输出 → 错误行红字进 Console
        const int rc = ProjectWizard::BuildGameProject(
            csproj, ctx_.Assets().ProjectRoot() + "/.lemon/bin", nullptr, &buildOut);
        if (rc != 0) {
            LEMON_ERROR("热重载编译失败（保持旧域运行）：dotnet build 退出码 %d（%s）", rc,
                        reason);
            LogCompileErrors(buildOut);
            return false;
        }
    }
    const auto info = host_->HotReloadAssembly(dll.c_str());
    const int reattached = ctx_.RefreshScriptsAfterReload();
    hotReloadMs_ = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0)
                       .count();
    if (!info.ok) {
        LEMON_ERROR("热重载换装失败：新域装载异常（旧域已弃，脚本停摆——修错后再触发）");
        return false;
    }
    LEMON_LOG("热重载完成（%s）：编译+换装+重装配 %.0fms（Play 重装配 %d 实例；类型 %zu 个）",
              reason, hotReloadMs_, reattached, ctx_.ScriptTypeNames().size());
    lastBuildMs_ = hotReloadMs_; // 状态栏"上次编译"回显（M4.6 §5-5）
    if (info.leakCount > 0)
        LEMON_WARN("热重载泄漏计数 %d（旧 ALC 未回收——本 runtime 已知限制，ADR-010 A 线；"
                   "每次约百 KB 级，会话内可接受）",
                   info.leakCount);
    return true;
}

void EditorApp::MenuRebuildScripts() { QueueScriptRebuild("手动触发"); }

bool EditorApp::PlayBlockedByScripts() {
    if (host_) return false;
    std::string csproj, dll;
    return FindGameProject(csproj, dll); // 带 Game/ 工程而无宿主 = 启动期编译/装配失败
}

bool EditorApp::TryEnterPlay() {
    if (PlayBlockedByScripts()) {
        playBlockedOpen_ = true;
        LEMON_WARN("已阻止进入 Play：Game/ 编译失败（脚本宿主未装配）。"
                   "错误见 Console 红字；修复保存后自动重编译装配");
        return false;
    }
    return ctx_.EnterPlay();
}

void EditorApp::MenuNewProject() { wizOpen_ = true; }

void EditorApp::DrawRecoveryModal() {
    if (recoveryPath_.empty()) return;
    if (!ImGui::IsPopupOpen("崩溃恢复") && !recoveryAnswered_) ImGui::OpenPopup("崩溃恢复");
    if (!ImGui::BeginPopupModal("崩溃恢复", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::Text("检测到较新的自动备份：\n%s", recoveryPath_.c_str());
    ImGui::TextUnformatted("（上次会话可能未正常保存。恢复 = 打开备份内容并保持未保存状态）");
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
    if (ImGui::Button("忽略", ImVec2(120, 0))) {
        recoveryPath_.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

int EditorApp::HotReloadCount() const {
    return host_ ? host_->HotReloadCount() : 0;
}


int EditorApp::Run(const EditorLaunch& launch) {
    launchCopy_ = launch;
    launch_ = &launchCopy_;
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
    sd.present = launch.benchSurvivor
                     ? rhi::PresentModePref::Immediate // M5 压测口径：禁 vsync（否则帧时被 60Hz 钉住测不出 45fps 档）
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
        // 预置存档（vs.best=123）= EnterPlay 载入路径的机械验证
        if (!ctx_.OpenScene(launch_->projectDir + "/Scenes/Main.scene")) return 1;
        {
            namespace fs = std::filesystem;
            std::error_code ec;
            const fs::path saves = fs::path(launch_->projectDir) / ".lemon/saves";
            fs::create_directories(saves, ec);
            lemon::ecs::SaveChannel pre;
            pre.Set("vs.best", "123", 3);
            const std::vector<uint8_t> bytes = pre.Encode();
            std::ofstream f(saves / "game.sav", std::ios::binary | std::ios::trunc);
            f.write((const char*)bytes.data(), (std::streamsize)bytes.size());
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

    // 启动恢复检测（§3.8）：场景打开后 autosave 新于盘档 → 提示（交互模态/终验自动恢复）
    if (!launch.finalTest && !ctx_.Assets().ProjectRoot().empty())
        recoveryPath_ = ctx_.DetectAutosaveRecovery();

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
    }
    // --bench-survivor（M5 清障③）：播种压测场景（tempdir 项目 + 1 万怪 Spawner）并进
    // Play。无 Game/（tempdir）——无脚本属合法形态，不走 PlayBlockedByScripts 守卫。
    if (launch.benchSurvivor) {
        if (!SeedBenchSurvivorScene(ctx_)) {
            LEMON_ERROR("bench-survivor 播种失败（临时项目/prefab 导出）");
            return 1;
        }
        if (!ctx_.EnterPlay()) return 1;
    }
    // M5 批④ --smoke-template：EnterPlay 已由上方 playTest 块完成（模板含 Game/、
    // 编译成功才走到这——PlayBlockedByScripts 守卫先行）。此处挂事件计数 sink。
    if (launch.smokeTemplate && ctx_.Playing()) {
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
    if (launch.smokeAnim && launch.frames < 60) {
        LEMON_ERROR("--smoke-anim 需要 --frames N（N>=60：fps10×4 帧周期 24 tick + 预热余量）");
        return 2;
    }
    if (launch.benchSurvivor && launch.frames < 600) {
        LEMON_ERROR("--bench-survivor 需要 --frames N（N>=600：怪海涨满 ~240 帧预热 + "
                    "测量窗 ≥360）");
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
            if (ctx_.ExitPlay()) tabFocusPending_ = -1;
            else LEMON_WARN("Stop 后快照校验失败（编辑场景已按快照重建）");
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
            if (ctx_.dirty && !quitConfirmArmed_) {
                quitConfirmOpen_ = true; // 退出前确认（一次）
                confirmContext_ = ConfirmContext::Exit; // 上一次 SceneOp 不残留
                exitRequested_ = false;
            } else if (!ctx_.dirty) {
                running = false;
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
            if (gameViewFocused_ && !ImGui::GetIO().WantTextInput) {
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
            const float dt = paused_ && !singleStep_ ? 0.0f : 1.0f / 60.0f;
            bPump = BenchClock::now(); // 段界：pump（轮询/watcher/自动备份）结束 = sim 开始
            ctx_.TickPlay(dt);         // Pause = dt0（含 Essential 提交）
            bSim = BenchClock::now();  // 段界：sim（世界步进）结束 = glue 开始
            singleStep_ = false;
            UpdateGameCameraFollow();
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
            ctx_.TickEditor(1.0f / 60.0f); // Essential（销毁提交）+ 空 FixedTick
            UpdateGameCameraFollow();  // 非 Play：退出跟随时回默认位
        }

        // 冒烟悬停扫掠（M4.5）：逐帧走窗口网格 → 会话内所有可见控件至少被悬停
        // 一次——ImGui 的 ID 冲突检查挂 HoveredId 路径，不悬停就永远测不到。
        // 经 SetMouseOverride 注入（SDL 后端每帧轮询真实鼠标，普通事件会被盖掉；
        // 覆盖口在轮询后、NewFrame 排水前生效）。
        if (launch.smoke) {
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
        viewport_->Render(cl, ctx_); // 双视口离屏（BuildUI 已定 RT 尺寸/注入 overlay）
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

        const bool wantCapture = !launch.screenshot.empty() || launch.smoke;
        const bool lastFrame =
            launch.frames > 0 && (int)frame == launch.frames - 1 && wantCapture;
        if (lastFrame) {
            cl.DebugRecordCapture();
            // 场景 RT 回读（冒烟像素断言源：线性空间、无 UI 合成/sRGB 干扰）
            if (launch.smoke && viewport_->SceneRenderTarget().IsValid())
                cl.DebugRecordTextureCapture(viewport_->SceneRenderTarget());
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
            ps.View<ecs::Animator2D>().each([&](auto ent, ecs::Animator2D& a) {
                const int slot = a.clipId == (uint32_t)kAnimClipGuid      ? 0
                                 : a.clipId == (uint32_t)kYamiHeroClipGuid ? 1
                                                                           : -1;
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
        }
        // M5 批④ smoke-template 证据采样：HUD 四要素行齐 / 存档载入（best=123 回显）/
        // 波次行 / 三选一卡片链（出现 → 注入选择（模拟数字键 1）→ 消费后隐藏）
        if (launch.smokeTemplate && ctx_.Playing() && frame > 5) {
            const lemon::ecs::RtUiChannel& rt = ctx_.ActiveWorld().RtUi();
            bool has[6] = {}; // hp/xp/time/kills/best/wave
            static const char* const kKeys[6] = {"hp", "xp", "time", "kills", "best", "wave"};
            for (uint32_t i = 0; i < rt.Count(); ++i)
                for (int k = 0; k < 6; ++k)
                    if (std::strcmp(rt.At(i).key, kKeys[k]) == 0) {
                        has[k] = true;
                        if (k == 4 && std::strstr(rt.At(i).text, "123"))
                            g_tplBestLoaded = true; // 预置存档 → C# 读回 → HUD 回显
                    }
            if (has[0] && has[1] && has[2] && has[3]) g_tplHudOk = true;
            if (has[5]) g_tplWaveRow = true;
            lemon::ecs::RtUiCards& cards = ctx_.ActiveWorld().Cards();
            if (cards.active) {
                g_tplCardsSeen = true;
                if (frame > 120) {
                    cards.pick = 0; // 自动选择（数字键 1/点击同通道；升级卡与死亡
                                    // 对话框通用——消费式回读归 C#，逐帧置 0 幂等）
                    g_tplPicked = true;
                }
            } else if (g_tplPicked) {
                g_tplCardsHidden = true; // C# 消费 → HideCards
            }
            // 批④后修④死亡链回归：kDeathArm 帧起压血到 0.1 + 掐射击（站桩下
            // 自动炮火半路清怪、玩家碰不到怪——停火让怪群近身，Hazard 真路径击杀）
            // → 玩家脚本实体仍在场（View 命中 = 未被销毁）、flags bit0 未置（未被
            // 异常禁用）→ pick 复活 → 血回满 + 解冻 + 对话框隐藏 = 复活成功
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
                                 ctx_.ActiveWorld().TimeScale() > 0.0f && !cards.active)
                            g_tplRevived = true;
                    });
                if (!anyScript) g_tplScriptOk = false; // 脚本实体消失（销毁回归锚点）
            }
            if (frame % 60 == 0) { // 诊断快照（低频）：RtUi 行 + 场内分布
                std::snprintf(g_tplHudRows, sizeof g_tplHudRows, "hp/xp/time/kills=%d%d%d%d",
                              has[0], has[1], has[2], has[3]);
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
        if (launch.benchSurvivor && ctx_.Playing() && frame >= kBenchWarmup) {
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
    float benchPlayerHp = -1.0f; // Hazard 化证据（方案 A 批）：玩家（收集者）掉血 =
                                 // 万怪 Hazard tick 真实发生（<1e6 即证）
    if (launch.benchSurvivor && ctx_.Playing()) {
        benchPlayProfiles = ctx_.ActiveWorld().Pipeline().Profiles(); // ExitPlay 弃世界前留证
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
    }
    if (ctx_.Playing()) { // --play：跑满帧数后 Stop（恢复编辑世界）
        playAliveAtStop = ctx_.ActiveScene().AliveCount();
        playEnterMs = ctx_.LastEnterPlayMs();
        playVerified = ctx_.ExitPlay();
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
        const bool pass =
            aliveOk && avg > 0.0 && avg <= 1000.0 / 45.0 && directorOk && animOk && hazardOk;
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
                    " => %s\n",
                    (unsigned)frame, (unsigned)kBenchWarmup, playAliveAtStop,
                    benchSimSum / segN, avg,
                    benchFrameMax, fps, benchWavesStarted, benchTeam1Alive,
                    benchAnimHit, benchAnimTotal, benchPlayerHp,
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
        if (launch.smokeAnim) {
            const AssetEntry* sh = ctx_.Assets().FindByGuid(kAnimSheetGuid);
            const AssetEntry* ysh = ctx_.Assets().FindByGuid(kYamiHeroSheetGuid);
            const bool booked = sh && sh->Sliced() && sh->sliceCount == 4;
            bool animOk = booked && smokeAnimMax[0] > 0 && smokeAnimSlice[0];
            char yami[96] = "";
            if (ysh && !ysh->missing && ysh->Sliced()) {
                const bool yOk = smokeAnimMax[1] > 0 && smokeAnimSlice[1] && ysh->sliceCount == 9;
                animOk = animOk && yOk;
                std::snprintf(yami, sizeof(yami), " yami(maxFrame=%u slice=%s frames=%u)",
                              (unsigned)smokeAnimMax[1], smokeAnimSlice[1] ? "YES" : "NO",
                              ysh->sliceCount);
            }
            std::printf("[lemon] smoke-anim: prog(maxFrame=%u slice=%s booked=%u)%s => %s\n",
                        (unsigned)smokeAnimMax[0], smokeAnimSlice[0] ? "YES" : "NO",
                        sh ? sh->sliceCount : 0, yami, animOk ? "OK" : "FAIL");
            if (!animOk) exitCode = 1;
        }
        // M5 批④模板链验收：向导复制 → build → Play 全链在跑（能到这 = 前两环已过）；
        // 断言 HUD 四要素 / 存档载入回显 / 波次 / 击杀 / 升级卡片出现-选择-隐藏。
        if (launch.smokeTemplate) {
            const bool tplOk = g_tplHudOk && g_tplBestLoaded && g_tplWaveRow &&
                               g_tplDeaths > 0 && g_tplLevelUps > 0 && g_tplCardsSeen &&
                               g_tplPicked && g_tplCardsHidden && g_tplDeathSeen &&
                               g_tplRevived && g_tplScriptOk;
            std::printf("[lemon] smoke-template: hud=%s saveLoad=%s wave(row=%s n=%d) "
                        "kills=%d levelUps=%d cards(seen=%s pick=%s hidden=%s) "
                        "death(seen=%s revive=%s scriptOk=%s) => %s\n",
                        g_tplHudOk ? "YES" : "NO", g_tplBestLoaded ? "YES" : "NO",
                        g_tplWaveRow ? "YES" : "NO", g_tplWaveStarts, g_tplDeaths,
                        g_tplLevelUps, g_tplCardsSeen ? "YES" : "NO",
                        g_tplPicked ? "YES" : "NO", g_tplCardsHidden ? "YES" : "NO",
                        g_tplDeathSeen ? "YES" : "NO", g_tplRevived ? "YES" : "NO",
                        g_tplScriptOk ? "YES" : "NO", tplOk ? "OK" : "FAIL");
            std::printf("[lemon] smoke-template: diag %s gems(peak)=%d mobs(peak)=%d\n",
                        g_tplHudRows, g_tplGems, g_tplMobs);
            if (!tplOk) exitCode = 1;
            // ExitPlay 兜底落盘（写路径）：Stop 后 .lemon/saves/game.sav 在且含模板档
            const std::string sav = ctx_.Assets().ProjectRoot() + "/.lemon/saves/game.sav";
            std::error_code ec;
            const bool savOk = std::filesystem::file_size(sav, ec) > 16 && !ec;
            std::printf("[lemon] smoke-template: saveFile=%s => %s\n",
                        savOk ? "YES" : "NO", savOk ? "OK" : "FAIL");
            if (!savOk) exitCode = 1;
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

    watcher_.Stop();          // 先停 watcher 线程（此后无资产重扫）
    scriptWatcher_.Stop();    // 与 Game/ 源监视同批收尾
    host_.reset();            // C# 宿主卸载（无脚本时为空操作）
    device_->WaitIdle(); // ImGui 后端资源（描述符池/采样器）可能被在途帧引用，先等闲
    ui_->Shutdown();
    SetLogSink(nullptr, nullptr);
    device_->SavePipelineCache();
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
    // M5 批③动画链：程序化 4 帧表 + anim.clip → Animator2D 帧映射断言实体；
    // yami hero-walk（Samples/Assets/yami-dungeon 拷进项目才在场）→ 真素材端到端验
    if (launch_->smokeAnim) {
        if (const AssetEntry* sheet = ctx_.Assets().FindByGuid(kAnimSheetGuid);
            sheet && !sheet->missing) {
            if (ecs::Entity hero = ctx_.CreateSpriteEntityFromAsset(
                    "AnimHero", kAnimSheetGuid, Vec2{640, 540});
                !hero.IsNull()) {
                ecs::Animator2D& an = s.Emplace<ecs::Animator2D>(hero);
                an.clipId = (uint32_t)kAnimClipGuid; // fps10 × 4 帧
            }
        }
        if (const AssetEntry* yc = ctx_.Assets().FindByGuid(kYamiHeroClipGuid);
            yc && !yc->missing) {
            if (ecs::Entity y = ctx_.CreateSpriteEntityFromAsset(
                    "AnimYami", kYamiHeroSheetGuid, Vec2{840, 540});
                !y.IsNull()) {
                ecs::Animator2D& an = s.Emplace<ecs::Animator2D>(y);
                an.clipId = (uint32_t)kYamiHeroClipGuid; // 9 帧 @8fps（Samples 素材）
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

    // M5 批③动画链素材：anim-sheet + grid meta + anim.clip（固定 guid 见 kAnimSheetGuid）
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
