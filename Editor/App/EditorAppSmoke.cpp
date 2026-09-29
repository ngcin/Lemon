// Lemon 编辑器 — EditorApp 冒烟播种族实现（SeedSmokeScene/SeedSmokeProject/
// SeedSmokeUiRmlProject/SeedSmokeUiDocument/SeedJudgementScene 成员函数 +
// WriteAnimSheetAssets/SeedBenchSurvivorScene 自由函数）。批③a 2026-09-29 自
// EditorApp.cpp 机械外迁：成员函数跨 TU 定义（类定义 App/EditorApp.h 零改动），
// 代码逐行原样；夹具常量见 App/EditorAppSmoke.h。
#include "App/EditorAppSmoke.h"

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
