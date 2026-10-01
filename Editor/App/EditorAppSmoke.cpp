// Lemon 编辑器 — EditorApp 冒烟播种族实现（SeedSmokeScene/SeedSmokeProject/
// SeedSmokeUiRmlProject/SeedSmokeUiDocument/SeedJudgementScene 成员函数 +
// WriteAnimSheetAssets/SeedBenchSurvivorScene 自由函数）。批③a 2026-09-29 自
// EditorApp.cpp 机械外迁：成员函数跨 TU 定义（类定义 App/EditorApp.h 零改动），
// 代码逐行原样；夹具常量见 App/EditorAppSmoke.h。
#include "App/EditorApp.h" // 显式类头（本批修正：原经 Panels/BuiltInPanels.h 传递拿到）
#include "App/EditorAppSmoke.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <vector>

#include "stb_image_write.h"

#include "App/ImGuiBackend.h"
#include "Assets/AssetDatabase.h"
#include "Assets/ClipEdit.h" // M6a 批② T3：smoke-anim clip 编辑链（面板数据面同款）
#include "Assets/ControllerEdit.h" // T3d：smoke-anim graph 链（controller 数据面）
#include "Interaction/ViewportRenderer.h"
#include "Components/BehaviorComponents.h"
#include "Components/CoreComponents.h"
#include "Components/GameplayComponents.h"
#include "Components/RenderComponents.h"
#include "Core/Process.h" // CurrentProcessId（tempdir 唯一名；Windows 阻断项①，07 §3.6）
#include "ECS/Hierarchy.h"
#include "Core/Log.h"
#include "EditorContext.h"
#include "Panels/BuiltInPanels.h" // SceneViewPanel 完整类型（smoke-drag/ui 注入走 scenePanel_-> 方法；标记表只认类型名漏了成员名）
#include "Ui/UiSubsystem.h" // 批③a（ADR-014）：游戏 UI 层（RmlUi）
#include "imgui.h"
#include "imgui_internal.h" // DockBuilder（docking 分支布局编程 API）+ FindWindowByName
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
                          ("lemon-bench-survivor-" + std::to_string(lemon::CurrentProcessId()));
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

// 批③b：--smoke-uirml 资产夹具（temp 项目，幂等清残留；夹具纪律 = 空目录自播种）。
// 三件套：uirml.rml（面板/标题/正文 + <img>）/ uirml.rcss（<link> 样式）/ tex.png
// （96×96 特征色贴图——贴图桥像素断言源）。热重载中点改写 .rml/.rcss（帧循环钩子）
//（批④ 自 EditorApp.cpp 迁回——函数批③a 外迁时注释遗留原处）
void EditorApp::SeedSmokeUiRmlProject() {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path tmp =
        fs::temp_directory_path() / ("lemon-uirml-" + std::to_string(lemon::CurrentProcessId()));
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
    // M4.4 装配通路：--script 时挂 SpawnerBehaviour（Play 中刷怪断言用）。
    // 挂前先解析：该类是 --script 测试装置（TestScript）注册的——真项目 Game 程序集
    // 无此类（svr-test 2026-10-01 实证：旧门控 ctx_.Scripts() 使 --smoke --play 恒
    // FAIL），不挂不断言（EditorApp 侧 smokeSpawnScript_ 同步）。
    if (ctx_.Scripts() && ctx_.ResolveScriptTypeId("SpawnerBehaviour") >= 0) {
        ctx_.AttachScript(root, 0, "SpawnerBehaviour");
        smokeSpawnScript_ = true;
    }
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

// ---- --smoke-drag 注入状态（批③c-1 2026-09-29 自 Run 局部变量收敛为结构体：
// 状态机整体外迁 EditorAppSmoke.cpp，字段/初始化/注释逐项原样；仅本 TU 使用）----
//（前提：Run 进程单次调用 = EditorEntry 唯一入口——全局生命周期与 Run 局部等价）
namespace {
struct DragSmokeState {
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
};
DragSmokeState g_dragSmoke;
} // namespace

// ---- --smoke-drag 帧注入状态机（M4.7c 交互回归；批③c-1 自 Run 外迁）----
// 帧号锚定/执行时序逐位不变——Run 主循环原位调用；五段：移动/旋转/8 向
// resize/缩放/甩飞防护+F 聚焦（详注随代码原样迁此）。
void EditorApp::SmokeDragFrame(uint64_t frame) {
    if (Launch().smokeDrag && scenePanel_) {
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
                g_dragSmoke.dragTarget = e;
                g_dragSmoke.dragBefore = tf.pos;
                break;
            }
            snapEnabled_ = false;       // 断言免吸附台阶化（现默认已关，显式防默认变更）
            cam.zoom = 1.0f;
            cam.halfHeight = 360.0f;
            cam.center = g_dragSmoke.dragBefore;    // 目标居中（屏幕位置确定性）
            ctx_.Select(g_dragSmoke.dragTarget, false);
            // 世界→窗口点换算（λ 内自取 SceneCam：cam 为帧内局部量，跨帧捕获会悬垂）
            g_dragSmoke.worldToPt = [&](Vec2 w) {
                Camera2D& c = viewport_->SceneCam();
                const Vec2 s = viewport_->WorldToScreen(
                    c, w, scenePanel_->LastRtW(), scenePanel_->LastRtH());
                return Vec2{scenePanel_->LastVpX() +
                                s.x * scenePanel_->LastVpW() / (float)scenePanel_->LastRtW(),
                            scenePanel_->LastVpY() +
                                s.y * scenePanel_->LastVpH() / (float)scenePanel_->LastRtH()};
            };
        } else if (frame == 4 && !g_dragSmoke.dragTarget.IsNull()) {
            // 按下点 = 目标中心 + 世界 (20,20)px（体内、避开中心块/轴带）。
            // 只记世界坐标，注入帧才换算屏幕点——面板矩形早帧可能还在 settle
            g_dragSmoke.moveWorld = Vec2{g_dragSmoke.dragBefore.x + 20.0f, g_dragSmoke.dragBefore.y + 20.0f};
            g_dragSmoke.moveReady = true;
        } else if (frame >= 5 && frame <= 21 && g_dragSmoke.moveReady) {
            if (scenePanel_->LastRtW() == 0) { /* 面板未就绪：跳过本帧注入 */ }
            else {
                const float moveX =
                    frame <= 9 ? 0.0f : std::min<float>((float)(frame - 9) * 4.0f, 44.0f);
                int btn = -1; // -1 = 本帧不动按键
                if (frame == 9 || (frame >= 10 && frame < 21)) btn = 1; // 9 按下，10-20 按住
                else if (frame == 21) btn = 0;                          // 21 释放
                const Vec2 pt = g_dragSmoke.worldToPt(g_dragSmoke.moveWorld);
                ui_->SetInputOverride(pt.x + moveX, pt.y, btn);
            }
        } else if (frame == 23 && !g_dragSmoke.dragDone) {
            g_dragSmoke.dragDone = true; // 释放后一帧取值（EndGizmoDrag 已结算）
            if (ctx_.ActiveScene().Alive(g_dragSmoke.dragTarget) &&
                ctx_.ActiveScene().Has<ecs::Transform2D>(g_dragSmoke.dragTarget)) {
                const Vec2 after = ctx_.ActiveScene().Get<ecs::Transform2D>(g_dragSmoke.dragTarget).pos;
                const float wpp = 2.0f * cam.halfHeight /
                                  std::max(1.0f, scenePanel_->LastVpH()); // 世界/点
                g_dragSmoke.dragDx = after.x - g_dragSmoke.dragBefore.x;
                g_dragSmoke.dragDy = after.y - g_dragSmoke.dragBefore.y;
                g_dragSmoke.dragPassed = std::fabs(g_dragSmoke.dragDx - 44.0f * wpp) < 3.0f && std::fabs(g_dragSmoke.dragDy) < 3.0f;
            }
        }
        // ---- 第二段：旋转（同一 arm 路径，验证绕质心角度数学）----
        // 24 切 Rotate 工具；25-27 悬停；28 按下于体内点（中心 +45° 半径 28px）；
        // 29-38 沿圆弧 −90°；39 释放；41 断言 rot Δ=−π/2（snap 已关 = 连续角）。
        else if (frame == 24) {
            tool_ = EditTool::Rotate;
            if (ctx_.ActiveScene().Has<ecs::Transform2D>(g_dragSmoke.dragTarget))
                g_dragSmoke.rotBefore = ctx_.ActiveScene().Get<ecs::Transform2D>(g_dragSmoke.dragTarget).rot;
        } else if (frame == 25 && scenePanel_->LastRtW() > 0) {
            // 弧上点 → 屏幕点换算；弧心 = 移动段结束后的当前位置。
            // λ 内自取 SceneCam（cam 为帧内局部量，跨帧捕获会悬垂）
            const Vec2 rotCenter =
                ctx_.ActiveScene().Alive(g_dragSmoke.dragTarget) &&
                        ctx_.ActiveScene().Has<ecs::Transform2D>(g_dragSmoke.dragTarget)
                    ? ctx_.ActiveScene().Get<ecs::Transform2D>(g_dragSmoke.dragTarget).pos
                    : g_dragSmoke.dragBefore;
            g_dragSmoke.arcWorldToPt = [&, rotCenter](float ang) {
                const Vec2 w{rotCenter.x + 28.0f * std::cos(ang),
                             rotCenter.y + 28.0f * std::sin(ang)};
                const Vec2 s = viewport_->WorldToScreen(
                    viewport_->SceneCam(), w, scenePanel_->LastRtW(), scenePanel_->LastRtH());
                return Vec2{scenePanel_->LastVpX() +
                                s.x * scenePanel_->LastVpW() / (float)scenePanel_->LastRtW(),
                            scenePanel_->LastVpY() +
                                s.y * scenePanel_->LastVpH() / (float)scenePanel_->LastRtH()};
            };
            g_dragSmoke.rotReady = true;
        } else if (frame >= 26 && frame <= 39 && g_dragSmoke.rotReady) {
            constexpr float kA0 = 0.78539818f;           // 45°
            const float t = frame <= 28 ? 0.0f
                : std::min<float>((float)(frame - 28) / 10.0f, 1.0f); // 28-38 走弧
            const float ang = kA0 - t * 1.57079637f;     // 45° → −45°
            const Vec2 p = g_dragSmoke.arcWorldToPt(ang);
            int btn = -1;
            if (frame == 28 || (frame >= 29 && frame < 39)) btn = 1;
            else if (frame == 39) btn = 0;
            ui_->SetInputOverride(p.x, p.y, btn);
        } else if (frame == 41 && !g_dragSmoke.rotDone) {
            g_dragSmoke.rotDone = true;
            if (ctx_.ActiveScene().Alive(g_dragSmoke.dragTarget) &&
                ctx_.ActiveScene().Has<ecs::Transform2D>(g_dragSmoke.dragTarget)) {
                g_dragSmoke.rotAfter = ctx_.ActiveScene().Get<ecs::Transform2D>(g_dragSmoke.dragTarget).rot;
                g_dragSmoke.rotPassed = std::fabs(g_dragSmoke.rotAfter - g_dragSmoke.rotBefore + 1.57079637f) < 0.06f;
                g_dragSmoke.dragPassed = g_dragSmoke.dragPassed && g_dragSmoke.rotPassed;
            }
        }
        // ---- 第三段：Select 8 向 resize（43 切工具；44 按右边中点手柄；
        //      45-53 外拖 27pt；54 释放；56 断言 scale.x 增大 + 左缘锚定）----
        else if (frame == 43) {
            tool_ = EditTool::Select;
            if (ctx_.ActiveScene().Alive(g_dragSmoke.dragTarget) &&
                ctx_.ActiveScene().Has<ecs::Transform2D>(g_dragSmoke.dragTarget)) {
                g_dragSmoke.scaleBefore = ctx_.ActiveScene().Get<ecs::Transform2D>(g_dragSmoke.dragTarget).scale.x;
                Vec2 c, s;
                float r = 0;
                if (viewport_->WorldBoundsOf(ctx_, g_dragSmoke.dragTarget, c, s, r)) {
                    // 右边中点手柄（世界点，注入帧才换算屏幕）+ 外拖方向
                    // （实体本地 +X 轴的世界朝向——旋转段后已转 ~−90°）
                    const float cs = std::cos(r), sn = std::sin(r);
                    g_dragSmoke.handleWorld = Vec2{c.x + cs * s.x * 0.5f, c.y + sn * s.x * 0.5f};
                    g_dragSmoke.anchorLeft0 = c.x - cs * s.x * 0.5f;
                    g_dragSmoke.handleDragDir = Vec2{cs, sn};
                    g_dragSmoke.resizeReady = true;
                }
            }
        } else if (frame >= 44 && frame <= 54 && g_dragSmoke.resizeReady) {
            if (scenePanel_->LastRtW() > 0) {
                const float off =
                    frame <= 44 ? 0.0f : std::min<float>((frame - 44) * 3.0f, 27.0f);
                int btn = -1;
                if (frame == 44 || (frame >= 45 && frame < 54)) btn = 1;
                else if (frame == 54) btn = 0;
                const Vec2 pt = g_dragSmoke.worldToPt(g_dragSmoke.handleWorld);
                ui_->SetInputOverride(pt.x + g_dragSmoke.handleDragDir.x * off,
                                      pt.y + g_dragSmoke.handleDragDir.y * off, btn);
            }
        } else if (frame == 56 && !g_dragSmoke.resizeDone) {
            g_dragSmoke.resizeDone = true;
            if (ctx_.ActiveScene().Alive(g_dragSmoke.dragTarget) &&
                ctx_.ActiveScene().Has<ecs::Transform2D>(g_dragSmoke.dragTarget)) {
                const auto& tf = ctx_.ActiveScene().Get<ecs::Transform2D>(g_dragSmoke.dragTarget);
                g_dragSmoke.scaleAfter = tf.scale.x;
                Vec2 c, s;
                float r = 0;
                if (viewport_->WorldBoundsOf(ctx_, g_dragSmoke.dragTarget, c, s, r))
                    g_dragSmoke.anchorLeft1 = c.x - std::cos(r) * s.x * 0.5f;
                const bool grew = g_dragSmoke.scaleAfter > g_dragSmoke.scaleBefore * 1.15f;
                const bool anchored = std::fabs(g_dragSmoke.anchorLeft1 - g_dragSmoke.anchorLeft0) < 6.0f;
                g_dragSmoke.resizePassed = grew && anchored;
                g_dragSmoke.dragPassed = g_dragSmoke.dragPassed && g_dragSmoke.resizePassed;
            }
        }
        // ---- 第四段：缩放（58 记锚；59-61 滚轮 +1 前推；63 断言 zoom 增大
        //      且选中对象屏幕位置不动）----
        else if (frame == 58) {
            g_dragSmoke.zoomBefore = cam.zoom;
            if (ctx_.ActiveScene().Alive(g_dragSmoke.dragTarget) &&
                ctx_.ActiveScene().Has<ecs::Transform2D>(g_dragSmoke.dragTarget)) {
                // 锚点 = 对象当前中心（移动/resize 段后已不在原位）
                const Vec2 cur = ctx_.ActiveScene().Get<ecs::Transform2D>(g_dragSmoke.dragTarget).pos;
                g_dragSmoke.selScreen0 = g_dragSmoke.worldToPt(cur);
            }
        } else if (frame >= 59 && frame <= 61 && scenePanel_->LastRtW() > 0) {
            const Vec2 ctr{scenePanel_->LastVpX() + scenePanel_->LastVpW() * 0.5f,
                           scenePanel_->LastVpY() + scenePanel_->LastVpH() * 0.5f};
            ui_->SetInputOverride(ctr.x, ctr.y, -1, /*wheel=*/1);
        } else if (frame == 63 && !g_dragSmoke.zoomDone) {
            g_dragSmoke.zoomDone = true;
            g_dragSmoke.zoomAfter = cam.zoom;
            if (ctx_.ActiveScene().Alive(g_dragSmoke.dragTarget) &&
                ctx_.ActiveScene().Has<ecs::Transform2D>(g_dragSmoke.dragTarget)) {
                const Vec2 cur = ctx_.ActiveScene().Get<ecs::Transform2D>(g_dragSmoke.dragTarget).pos;
                g_dragSmoke.selScreen1 = g_dragSmoke.worldToPt(cur);
            }
            const bool zoomedIn = g_dragSmoke.zoomAfter > g_dragSmoke.zoomBefore * 1.25f;
            const bool anchored =
                Length(Vec2{g_dragSmoke.selScreen1.x - g_dragSmoke.selScreen0.x, g_dragSmoke.selScreen1.y - g_dragSmoke.selScreen0.y}) < 12.0f;
            g_dragSmoke.zoomPassed = zoomedIn && anchored;
            g_dragSmoke.dragPassed = g_dragSmoke.dragPassed && g_dragSmoke.zoomPassed;
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
            if (ctx_.ActiveScene().Alive(g_dragSmoke.dragTarget) &&
                ctx_.ActiveScene().Has<ecs::Transform2D>(g_dragSmoke.dragTarget)) {
                g_dragSmoke.slingWorld = Vec2{2.0e5f, 3.5e5f};
                ctx_.ActiveScene().Get<ecs::Transform2D>(g_dragSmoke.dragTarget).pos = g_dragSmoke.slingWorld;
                ctx_.Select(g_dragSmoke.dragTarget, false);
                g_dragSmoke.slingCenter0 = viewport_->SceneCam().center;
            }
        } else if (frame >= 73 && frame <= 75 && scenePanel_->LastRtW() > 0) {
            const Vec2 ctr{scenePanel_->LastVpX() + scenePanel_->LastVpW() * 0.5f,
                           scenePanel_->LastVpY() + scenePanel_->LastVpH() * 0.5f};
            ui_->SetInputOverride(ctr.x, ctr.y, -1, /*wheel=*/1);
        } else if (frame == 77 && !g_dragSmoke.slingDone) {
            g_dragSmoke.slingDone = true;
            const Vec2 c = viewport_->SceneCam().center;
            g_dragSmoke.slingDx = c.x - g_dragSmoke.slingCenter0.x;
            g_dragSmoke.slingDy = c.y - g_dragSmoke.slingCenter0.y;
            // 容差 16 世界单位：注入中心点与逐帧视口中心有 ±1pt 布局抖动，
            // 极缩小下折 ~2 单位/格；真甩飞（无锚点回退）= 每格 2 万+
            g_dragSmoke.slingPassed = std::fabs(g_dragSmoke.slingDx) < 16.0f && std::fabs(g_dragSmoke.slingDy) < 16.0f;
        } else if (frame == 79) {
            ui_->SetKeyTapOverride(ImGuiKey_F);
        } else if (frame == 81 && !g_dragSmoke.focusDone) {
            g_dragSmoke.focusDone = true;
            const Vec2 c = viewport_->SceneCam().center;
            g_dragSmoke.focusDelta = Length(Vec2{c.x - g_dragSmoke.slingWorld.x, c.y - g_dragSmoke.slingWorld.y});
            g_dragSmoke.slingPassed = g_dragSmoke.slingPassed && g_dragSmoke.focusDelta < 32.0f;
            g_dragSmoke.dragPassed = g_dragSmoke.dragPassed && g_dragSmoke.slingPassed;
        }
    }
}

// ---- --smoke-drag 末帧裁决（批③c-1 自 Run 外迁）：分段计数 + 断言行打印，
// 返回 pass（false = Run 置退出码 1；面板缺失 = false 同语义）----
bool EditorApp::SmokeDragVerdict() {
    bool pass = false;
    if (scenePanel_) {
        std::printf("[lemon] smoke-drag: press=%d armed=%d updates=%d "
                    "move=(%.1f,%.1f) rot=%.3frad(exp -1.571) "
                    "scale %.2f→%.2f leftΔ=%.1f zoom %.2f→%.2f selΔ=%.1fpx "
                    "slingΔ=(%.0f,%.0f) focusΔ=%.0f => %s\n",
                    scenePanel_->dbgPress_, scenePanel_->dbgArmed_, scenePanel_->dbgUpdates_,
                    g_dragSmoke.dragDx, g_dragSmoke.dragDy,
                    g_dragSmoke.rotAfter - g_dragSmoke.rotBefore,
                    g_dragSmoke.scaleBefore, g_dragSmoke.scaleAfter,
                    std::fabs(g_dragSmoke.anchorLeft1 - g_dragSmoke.anchorLeft0),
                    g_dragSmoke.zoomBefore, g_dragSmoke.zoomAfter,
                    Length(Vec2{g_dragSmoke.selScreen1.x - g_dragSmoke.selScreen0.x,
                                g_dragSmoke.selScreen1.y - g_dragSmoke.selScreen0.y}),
                    g_dragSmoke.slingDx, g_dragSmoke.slingDy, g_dragSmoke.focusDelta,
                    g_dragSmoke.dragPassed ? "OK" : "FAIL");
        pass = g_dragSmoke.dragPassed;
    } else {
        std::printf("[lemon] smoke-drag: Scene 面板未找到 => FAIL\n");
    }
    return pass;
}

// ---- --smoke-ui（M4.7d 真人会话回归）状态机（批③c-2 自 Run 外迁；
// A–K 段：工具/点选/复制删除 Undo/scrub/保存/Play/重命名/挂父子/目录/
// 布局/Console——帧号锚定/执行时序逐位不变，Run 原位调用）----
//（前提：Run 进程单次调用 = EditorEntry 唯一入口——全局生命周期与 Run 局部等价）
namespace {
struct UiSmokeState {
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
};
UiSmokeState g_uiSmoke;
} // namespace

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

// ---- --smoke-ui 帧注入状态机（批③c-2 自 Run 外迁）----
void EditorApp::SmokeUiFrame(uint64_t frame) {
    if (Launch().smokeUi && scenePanel_) {
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
                if (std::strcmp(m->tag, "Player") == 0) g_uiSmoke.uiTarget = e;
                if (std::strcmp(m->tag, "Gate") == 0) g_uiSmoke.uiGate = e;
            });
            snapEnabled_ = false;
            Camera2D& cam = viewport_->SceneCam();
            cam.zoom = 1.0f;
            cam.halfHeight = 360.0f;
            cam.center = ctx_.ActiveScene().Get<ecs::Transform2D>(g_uiSmoke.uiTarget).pos;
            g_uiSmoke.uiBaseCount = ctx_.ActiveScene().AliveCount();
            g_uiSmoke.uiTargetSubtree = SubtreeSizeOf(ctx_.ActiveScene(), g_uiSmoke.uiTarget);
            g_uiSmoke.uiRot0 = ctx_.ActiveScene().Get<ecs::Transform2D>(g_uiSmoke.uiTarget).rot;
            // 场景路径先行落定（否则 Ctrl+S 走另存为弹窗——那是 FilePicker 冒烟）
            fs::create_directories(fs::path(launchCopy_.projectDir) / "Scenes", ec);
            ctx_.SaveScene(
                (fs::path(launchCopy_.projectDir) / "Scenes" / "ui.scene").string());
        }
        // ---- A. 工具快捷键（tap 下帧断言 Tool）----
        else if (frame == 6) ui_->SetKeyTapOverride(ImGuiKey_Q);
        else if (frame == 7) g_uiSmoke.toolsOk = tool_ == EditTool::Select;
        else if (frame == 8) ui_->SetKeyTapOverride(ImGuiKey_W);
        else if (frame == 9) g_uiSmoke.toolsOk = g_uiSmoke.toolsOk && tool_ == EditTool::Move;
        else if (frame == 10) ui_->SetKeyTapOverride(ImGuiKey_E);
        else if (frame == 11) g_uiSmoke.toolsOk = g_uiSmoke.toolsOk && tool_ == EditTool::Rotate;
        else if (frame == 12) ui_->SetKeyTapOverride(ImGuiKey_R);
        else if (frame == 13) g_uiSmoke.toolsOk = g_uiSmoke.toolsOk && tool_ == EditTool::Scale;
        else if (frame == 14) ui_->SetKeyTapOverride(ImGuiKey_Q);
        else if (frame == 15) g_uiSmoke.toolsOk = g_uiSmoke.toolsOk && tool_ == EditTool::Select;
        // ---- B. 视口点选（Select 工具，点击实体体内 +6px）----
        else if (frame == 16 && scenePanel_->LastRtW() > 0) {
            const Vec2 w = ctx_.ActiveScene().Get<ecs::Transform2D>(g_uiSmoke.uiTarget).pos;
            const Vec2 s = viewport_->WorldToScreen(
                viewport_->SceneCam(), Vec2{w.x + 6.0f, w.y + 6.0f},
                scenePanel_->LastRtW(), scenePanel_->LastRtH());
            g_uiSmoke.uiPt0 = Vec2{scenePanel_->LastVpX() +
                             s.x * scenePanel_->LastVpW() / (float)scenePanel_->LastRtW(),
                         scenePanel_->LastVpY() +
                             s.y * scenePanel_->LastVpH() / (float)scenePanel_->LastRtH()};
            hold(g_uiSmoke.uiPt0);
        } else if (frame == 17) {
            release(g_uiSmoke.uiPt0);
        } else if (frame == 18) {
            g_uiSmoke.selOk = ctx_.Primary() == g_uiSmoke.uiTarget;
        }
        // ---- C. 复制/删除 + Undo/Redo 全往返（栈序 [复制,删除]）----
        else if (frame == 20)
            ui_->SetKeyChordOverride((int)ImGuiMod_Ctrl, (int)ImGuiKey_D);
        else if (frame == 22)
            g_uiSmoke.dupOk = ctx_.ActiveScene().AliveCount() == g_uiSmoke.uiBaseCount + g_uiSmoke.uiTargetSubtree &&
                    !ctx_.Primary().IsNull(); // C8：+子树大小（Player 带 3 Mob）
        else if (frame == 24)
            ui_->SetKeyChordOverride((int)ImGuiMod_Ctrl, (int)ImGuiKey_Delete);
        else if (frame == 26) g_uiSmoke.delOk = ctx_.ActiveScene().AliveCount() == g_uiSmoke.uiBaseCount;
        else if (frame == 28)
            ui_->SetKeyChordOverride((int)ImGuiMod_Ctrl, (int)ImGuiKey_Z);
        else if (frame == 30)
            g_uiSmoke.dupUndoOk = ctx_.ActiveScene().AliveCount() == g_uiSmoke.uiBaseCount + g_uiSmoke.uiTargetSubtree;
        else if (frame == 32)
            ui_->SetKeyChordOverride((int)ImGuiMod_Ctrl, (int)ImGuiKey_Z);
        else if (frame == 34)
            g_uiSmoke.dupUndoOk = g_uiSmoke.dupUndoOk && ctx_.ActiveScene().AliveCount() == g_uiSmoke.uiBaseCount;
        else if (frame == 36)
            ui_->SetKeyChordOverride((int)ImGuiMod_Ctrl, (int)ImGuiKey_Y);
        else if (frame == 38)
            g_uiSmoke.dupRedoOk = ctx_.ActiveScene().AliveCount() == g_uiSmoke.uiBaseCount + g_uiSmoke.uiTargetSubtree;
        else if (frame == 40)
            ui_->SetKeyChordOverride((int)ImGuiMod_Ctrl, (int)ImGuiKey_Y);
        else if (frame == 42)
            g_uiSmoke.delRedoOk = ctx_.ActiveScene().AliveCount() == g_uiSmoke.uiBaseCount;
        else if (frame == 43) {
            // 结构轨 Undo/Redo = 整场景 JSON 重载（实体全重建、句柄版本全变）
            // ——后续段（scrub/挂父子）必须用新句柄
            g_uiSmoke.uiTarget = ecs::Entity::Null();
            ctx_.ActiveScene().Each([&](ecs::Entity e) {
                const ecs::Meta* m = ctx_.ActiveScene().TryGet<ecs::Meta>(e);
                if (m && std::strcmp(m->tag, "Player") == 0) g_uiSmoke.uiTarget = e;
            });
        }
        // ---- D. label-scrub：拖 Transform2D.rot 名字 +28px（0.5°/px = +14°）→
        //         断言值与 Undo 往返（属性轨 = 本轮修好的提交顺序）----
        else if (frame == 44) {
            ctx_.Select(g_uiSmoke.uiTarget, false);
        } else if (frame == 46) {
            g_uiSmoke.uiPt0 = rectCenter("Transform2D.rot");
            if (g_uiSmoke.uiPt0.x > -1.0e8f) hold(g_uiSmoke.uiPt0);
        } else if (frame >= 47 && frame <= 53) {
            g_uiSmoke.uiPt1 = Vec2{g_uiSmoke.uiPt0.x + (float)(frame - 46) * 4.0f, g_uiSmoke.uiPt0.y};
            hold(g_uiSmoke.uiPt1);
        } else if (frame == 54) {
            release(g_uiSmoke.uiPt1);
        } else if (frame == 56) {
            g_uiSmoke.uiRot1 = ctx_.ActiveScene().Get<ecs::Transform2D>(g_uiSmoke.uiTarget).rot;
            const float deltaDeg = (g_uiSmoke.uiRot1 - g_uiSmoke.uiRot0) * 57.29577951f;
            g_uiSmoke.scrubOk = std::fabs(deltaDeg - 14.0f) < 3.0f;
        } else if (frame == 58)
            ui_->SetKeyChordOverride((int)ImGuiMod_Ctrl, (int)ImGuiKey_Z);
        else if (frame == 60) {
            const float back = ctx_.ActiveScene().Get<ecs::Transform2D>(g_uiSmoke.uiTarget).rot;
            g_uiSmoke.scrubUndoOk = std::fabs(back - g_uiSmoke.uiRot0) < 1.0e-4f;
        }
        // ---- E. Ctrl+S（路径已在帧 4 落定 → 直存不弹窗）----
        else if (frame == 62) {
            g_uiSmoke.saveOk = ctx_.dirty; // scrub 的编辑已置脏（undo 也保持 dirty，简化语义）
            ui_->SetKeyChordOverride((int)ImGuiMod_Ctrl, (int)ImGuiKey_S);
        } else if (frame == 64) {
            std::error_code ec;
            g_uiSmoke.saveOk = g_uiSmoke.saveOk && !ctx_.dirty &&
                     std::filesystem::is_regular_file(
                         std::filesystem::path(ctx_.ScenePath()), ec);
        }
        // ---- F. Ctrl+P Play 往返 ----
        else if (frame == 66)
            ui_->SetKeyChordOverride((int)ImGuiMod_Ctrl, (int)ImGuiKey_P);
        else if (frame == 68) {
            g_uiSmoke.playOk = ctx_.Playing();
        } else if (frame == 70)
            ui_->SetKeyChordOverride((int)ImGuiMod_Ctrl, (int)ImGuiKey_P);
        else if (frame == 72) {
            g_uiSmoke.stopOk = !ctx_.Playing();
        } else if (frame == 74) {
            // ExitPlay 按快照重建了场景（实体全部重建）——句柄重找，后续段有效
            g_uiSmoke.uiTarget = g_uiSmoke.uiGate = ecs::Entity::Null();
            ctx_.ActiveScene().Each([&](ecs::Entity e) {
                const ecs::Meta* m = ctx_.ActiveScene().TryGet<ecs::Meta>(e);
                if (!m) return;
                if (std::strcmp(m->tag, "Player") == 0) g_uiSmoke.uiTarget = e;
                if (std::strcmp(m->tag, "Gate") == 0) g_uiSmoke.uiGate = e;
            });
        }
        // ---- G. Hierarchy 点击选中 + F2 重命名（打字 + Enter）----
        else if (frame == 76) {
            g_uiSmoke.uiPt0 = rectCenter("hier.Gate");
            if (g_uiSmoke.uiPt0.x > -1.0e8f) hold(g_uiSmoke.uiPt0);
        } else if (frame == 77) {
            release(g_uiSmoke.uiPt0);
        } else if (frame == 79) {
            g_uiSmoke.gselOk = ctx_.Primary() == g_uiSmoke.uiGate;
        } else if (frame == 80) {
            ui_->SetKeyTapOverride(ImGuiKey_F2);
        } else if (frame == 82) {
            ui_->SetTextOverride("Gate2");
        } else if (frame == 84) {
            ui_->SetKeyTapOverride(ImGuiKey_Enter);
        } else if (frame == 86) {
            g_uiSmoke.renameOk = !g_uiSmoke.uiGate.IsNull() &&
                       std::strcmp(ctx_.ActiveScene().Get<ecs::Meta>(g_uiSmoke.uiGate).tag,
                                   "Gate2") == 0;
        } else if (frame == 88)
            ui_->SetKeyChordOverride((int)ImGuiMod_Ctrl, (int)ImGuiKey_Z);
        else if (frame == 90)
            g_uiSmoke.renameUndoOk = std::strcmp(ctx_.ActiveScene().Get<ecs::Meta>(g_uiSmoke.uiGate).tag,
                                       "Gate") == 0;
        // ---- H. 行拖拽挂父子（Gate 拖到 Player 上 → Undo 摘回）----
        // 压 8 帧动程 + 目标处停 1 帧（DnD 激活 = 阈值 + 移动帧；过短偶发不激活）
        else if (frame == 92) {
            g_uiSmoke.uiPt0 = rectCenter("hier.Gate");
            if (g_uiSmoke.uiPt0.x > -1.0e8f) ui_->SetInputOverride(g_uiSmoke.uiPt0.x, g_uiSmoke.uiPt0.y, -1);
        } else if (frame == 93) {
            g_uiSmoke.uiPt0 = rectCenter("hier.Gate");
            g_uiSmoke.uiPt1 = rectCenter("hier.Player");
            // x+24 偏移按下：G 段刚点过同一行（注入帧距 < 双击时限），原点按下
            // 会被 ImGui 判成双击触发重命名行，行布局整体位移（真人手速不会）
            if (g_uiSmoke.uiPt0.x > -1.0e8f && g_uiSmoke.uiPt1.x > -1.0e8f)
                hold(Vec2{g_uiSmoke.uiPt0.x + 24.0f, g_uiSmoke.uiPt0.y});
            g_uiSmoke.uiPt0.x += 24.0f;
        } else if (frame >= 94 && frame <= 100) {
            const float t = std::min<float>((float)(frame - 93) / 7.0f, 1.0f);
            hold(Vec2{g_uiSmoke.uiPt0.x + (g_uiSmoke.uiPt1.x - g_uiSmoke.uiPt0.x) * t,
                      g_uiSmoke.uiPt0.y + (g_uiSmoke.uiPt1.y - g_uiSmoke.uiPt0.y) * t});
            if (frame == 98 && std::getenv("LEMON_SMOKE_UI_DEBUG"))
                std::printf("[smoke-ui dbg] drag98 dnd=%d activeId=%llx\n",
                            ImGui::GetCurrentContext()->DragDropActive ? 1 : 0,
                            (unsigned long long)ImGui::GetCurrentContext()->ActiveId);
        } else if (frame == 101) {
            // 松手前必须重读目标行现位：拖拽启动会触发行布局变化，用 f93
            // 时的旧坐标会松开在源行上（SourceId==id 被拒，永不投递）
            g_uiSmoke.uiPt1 = rectCenter("hier.Player");
            hold(g_uiSmoke.uiPt1);
        } else if (frame == 102) {
            release(g_uiSmoke.uiPt1);
        } else if (frame == 103 && std::getenv("LEMON_SMOKE_UI_DEBUG")) {
            const ecs::Hierarchy* h = ctx_.ActiveScene().TryGet<ecs::Hierarchy>(g_uiSmoke.uiGate);
            std::printf("[smoke-ui dbg] f=103 parent=%lld expect=%lld gateAlive=%d\n",
                        h ? (long long)(h->parent.id & 0xFFFFFFFF) : -1,
                        (long long)(g_uiSmoke.uiTarget.id & 0xFFFFFFFF),
                        ctx_.ActiveScene().Alive(g_uiSmoke.uiGate) ? 1 : 0);
        } else if (frame == 104) {
            const ecs::Hierarchy* h = ctx_.ActiveScene().TryGet<ecs::Hierarchy>(g_uiSmoke.uiGate);
            g_uiSmoke.parentOk = h && h->parent == g_uiSmoke.uiTarget;
        } else if (frame == 106)
            ui_->SetKeyChordOverride((int)ImGuiMod_Ctrl, (int)ImGuiKey_Z);
        else if (frame == 108) {
            const ecs::Hierarchy* h = ctx_.ActiveScene().TryGet<ecs::Hierarchy>(g_uiSmoke.uiGate);
            g_uiSmoke.parentUndoOk = !h || h->parent.IsNull();
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
                    (long long)(g_uiSmoke.uiTarget.id & 0xFFFFFFFF),
                    !ctx_.ActiveScene().Has<ecs::Transform2D>(g_uiSmoke.uiTarget)
                        ? -99.0f
                        : ctx_.ActiveScene().Get<ecs::Transform2D>(g_uiSmoke.uiTarget).rot,
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
            g_uiSmoke.uiPt0 = rectCenter("assets.folder.sub");
            if (g_uiSmoke.uiPt0.x > -1.0e8f) hold(g_uiSmoke.uiPt0);
        } else if (frame == 113) {
            release(g_uiSmoke.uiPt0);
        } else if (frame == 115) {
            g_uiSmoke.folderOk = assetPanel_ && assetPanel_->CurrentDir() == "Assets/sub";
        } else if (frame == 117) {
            g_uiSmoke.uiPt0 = rectCenter("crumbAssets");
            if (g_uiSmoke.uiPt0.x > -1.0e8f) hold(g_uiSmoke.uiPt0);
        } else if (frame == 118) {
            release(g_uiSmoke.uiPt0);
        } else if (frame == 120) {
            g_uiSmoke.crumbOk = assetPanel_ && assetPanel_->CurrentDir().empty();
        }
        // ---- J. 命名布局：combo → 保存… → 模态打字 + Enter → 文件存在；
        //         再切回默认布局 ----
        else if (frame == 122) {
            g_uiSmoke.uiPt0 = rectCenter("layout.combo");
            if (g_uiSmoke.uiPt0.x > -1.0e8f) ui_->SetInputOverride(g_uiSmoke.uiPt0.x, g_uiSmoke.uiPt0.y, -1);
        } else if (frame == 123) {
            g_uiSmoke.uiPt0 = rectCenter("layout.combo");
            if (g_uiSmoke.uiPt0.x > -1.0e8f) hold(g_uiSmoke.uiPt0);
        } else if (frame == 124) {
            release(g_uiSmoke.uiPt0);
        } else if (frame == 126) {
            g_uiSmoke.uiPt0 = rectCenter("layout.item.save");
            if (g_uiSmoke.uiPt0.x > -1.0e8f) hold(g_uiSmoke.uiPt0);
        } else if (frame == 127) {
            release(g_uiSmoke.uiPt0);
        } else if (frame == 129) {
            g_uiSmoke.uiPt0 = rectCenter("layout.nameInput");
            if (g_uiSmoke.uiPt0.x > -1.0e8f) hold(g_uiSmoke.uiPt0);
        } else if (frame == 130) {
            release(g_uiSmoke.uiPt0);
        } else if (frame == 131) {
            ui_->SetTextOverride("ui");
        } else if (frame == 133) {
            ui_->SetKeyTapOverride(ImGuiKey_Enter);
        } else if (frame == 135) {
            std::error_code ec;
            g_uiSmoke.layoutOk =
                std::filesystem::is_regular_file(".lemon/editor/layouts/ui.ini", ec);
        } else if (frame == 137) {
            g_uiSmoke.uiPt0 = rectCenter("layout.combo");
            if (g_uiSmoke.uiPt0.x > -1.0e8f) hold(g_uiSmoke.uiPt0);
        } else if (frame == 138) {
            release(g_uiSmoke.uiPt0);
        } else if (frame == 140) {
            g_uiSmoke.uiPt0 = rectCenter("layout.item.default");
            if (g_uiSmoke.uiPt0.x > -1.0e8f) hold(g_uiSmoke.uiPt0);
        } else if (frame == 141) {
            release(g_uiSmoke.uiPt0);
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
                g_uiSmoke.uiPt0 = Vec2{(mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f};
                hold(g_uiSmoke.uiPt0);
            } else {
                g_uiSmoke.uiPt0 = Vec2{-1.0e9f, -1.0e9f}; // 开关没画出来 = 标签/面板体未就位
            }
        } else if (frame == 148) {
            release(g_uiSmoke.uiPt0);
        } else if (frame == 151) {
            // 收紧：开关被找到并点击 = 标签已切 + 面板体已绘制 + 控件可点，缺一不可
            // （f151 而非 f150——诊断块占 f150，同链 else-if 会被它截胡）
            g_uiSmoke.consoleOk = g_uiSmoke.uiPt0.x > -1.0e8f;
        }
        // ---- 总裁决 ----
        else if (frame == 154 && !g_uiSmoke.uiVerdictDone) {
            g_uiSmoke.uiVerdictDone = true;
            g_uiSmoke.uiAllOk = g_uiSmoke.toolsOk && g_uiSmoke.selOk && g_uiSmoke.dupOk && g_uiSmoke.delOk && g_uiSmoke.dupUndoOk && g_uiSmoke.dupRedoOk &&
                      g_uiSmoke.delRedoOk && g_uiSmoke.scrubOk && g_uiSmoke.scrubUndoOk && g_uiSmoke.saveOk && g_uiSmoke.playOk && g_uiSmoke.stopOk &&
                      g_uiSmoke.gselOk && g_uiSmoke.renameOk && g_uiSmoke.renameUndoOk && g_uiSmoke.parentOk && g_uiSmoke.parentUndoOk &&
                      g_uiSmoke.folderOk && g_uiSmoke.crumbOk && g_uiSmoke.layoutOk && g_uiSmoke.consoleOk;
            std::printf("[lemon] smoke-ui: tools=%d sel=%d dup=%d del=%d dupZ=%d "
                        "dupUndo=%d delRedo=%d scrub=%.1fdeg/%d/%d save=%d play=%d/%d "
                        "gsel=%d rename=%d/%d parent=%d/%d dir=%d/%d layout=%d "
                        "console=%d => %s\n",
                        g_uiSmoke.toolsOk, g_uiSmoke.selOk, g_uiSmoke.dupOk, g_uiSmoke.delOk, g_uiSmoke.dupUndoOk, g_uiSmoke.dupRedoOk, g_uiSmoke.delRedoOk,
                        (g_uiSmoke.uiRot1 - g_uiSmoke.uiRot0) * 57.29577951f, g_uiSmoke.scrubOk, g_uiSmoke.scrubUndoOk, g_uiSmoke.saveOk,
                        g_uiSmoke.playOk, g_uiSmoke.stopOk, g_uiSmoke.gselOk, g_uiSmoke.renameOk, g_uiSmoke.renameUndoOk, g_uiSmoke.parentOk,
                        g_uiSmoke.parentUndoOk, g_uiSmoke.folderOk, g_uiSmoke.crumbOk, g_uiSmoke.layoutOk, g_uiSmoke.consoleOk,
                        g_uiSmoke.uiAllOk ? "OK" : "FAIL");
        }
    }
}

// ---- --smoke-ui 末帧裁决（裁决行已在帧 154 块内打印；此处只回退出码位）----
bool EditorApp::SmokeUiVerdict() {
    return g_uiSmoke.uiAllOk;
}
// ---- --smoke-anim 状态（批③c-3 自 Run 三段局部收敛；字段/注释逐项原样。
// 前提：Run 进程单次调用 = EditorEntry 唯一入口——全局生命周期与 Run 局部等价）----
namespace {
struct AnimSmokeState {
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
    Vec2 smokeCreatePt{-1.0e9f, -1.0e9f};
    Vec2 smokePickPt{-1.0e9f, -1.0e9f};
    Vec2 smokeSheetPt{-1.0e9f, -1.0e9f};
    // M5 批③ smoke-anim 证据（帧循环内累积：单帧采样会踩回绕 0）
    uint16_t smokeAnimMax[2] = {0, 0};    // [0] 程序表 / [1] yami 表：见过的最大 curFrame
    bool smokeAnimSlice[2] = {false, false}; // sr.spriteId 曾落入对应切片连号区间
    // M6a 批①：切段链（frame 60 直写 Play(hit)+Queue(walk) → hit 段在场 → 收尾回 walk）
    // + fx 通道（frame 30 写飘字/血条 → 通道计数）；GameView 渲染不属断言依赖
    bool smokeQueueHit = false, smokeQueueBack = false, smokeFxSeen = false;
};
AnimSmokeState g_animSmoke;
} // namespace

// ---- --smoke-anim 预循环播种（夹具四档 + 编辑链 roundtrip + 面板入口）（批③c-3 自 Run 外迁；挂点原位逐位不变）----
void EditorApp::SmokeAnimSeed() {
    if (Launch().smokeAnim) {
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
                g_animSmoke.smokeClipEditOk = v.ok && v.fps == 13.0f && v.frames.size() == 2 &&
                                  v.frames[1].cell == 3 && v.frames[1].sheetGuid == kAnimSheetGuid;
            }
        }
        if (!g_animSmoke.smokeClipEditOk)
            LEMON_ERROR("smoke-anim：clip 编辑链失败（anim-edit.anim roundtrip 不符）");
        { // T3c：集档 roundtrip（数据面同款链路）
            std::ifstream vf(setPath, std::ios::binary);
            std::string vtext((std::istreambuf_iterator<char>(vf)),
                              std::istreambuf_iterator<char>());
            const AnimSetData v = ParseAnimSetJson(vtext);
            g_animSmoke.smokeSetEditOk = v.ok && v.name == "smoke-anim-set" && v.segments.size() == 4 &&
                             v.segments[0].name == "walk" &&
                             v.segments[0].clipGuid == kAnimClipGuid;
            if (!g_animSmoke.smokeSetEditOk)
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
            g_animSmoke.smokeGraphEditOk = v.ok && v.name == "smoke-anim-graph" &&
                               v.states.size() == 3 && v.params.size() == 2 &&
                               v.transitions.size() == 3 && v.transitions[2].exitTime;
            if (!g_animSmoke.smokeGraphEditOk)
                LEMON_ERROR("smoke-anim：状态机解析失败（anim-graph.controller 不符）");
        }
        // 2026-09-27 修复回归：双击 .override（AnimSet 直开分支）必须置 setGuid_
        //（曾误走 SetTarget 只设 targetGuid_ → 空态/历史集——真人实测报告）。
        // 必须先于下方的 clip 归并调用：归并路径本身会置 setGuid_ 同值，后置
        // 断言会假通过
        OpenAnimationEditor(kAnimSetGuid);
        if (PanelRegistry::Entry* en = panels_.FindEntry("Animation"))
            g_animSmoke.smokeSetOpenOk =
                static_cast<AnimationPanel*>(en->panel)->SetGuidForTest() == kAnimSetGuid;
        if (!g_animSmoke.smokeSetOpenOk)
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
            g_animSmoke.smokeSetCreateOk =
                static_cast<AnimationPanel*>(en->panel)->SetGuidForTest() == 0;
        if (!g_animSmoke.smokeSetCreateOk)
            LEMON_ERROR("smoke-anim：新建动画集入口未清残留集（setGuid_ 未归零）");
        // T3b：面板本体无头覆盖——开 Animation 面板每帧跑 OnGui（装载/胶片带/
        // 预览路径；ImGui 错误计数归零断言兜底）。选编辑过的 anim-edit（后续
        // 编辑写回 hash 变更 → 覆盖 LoadFrom 重入）
        OpenAnimationEditor(kAnimEditClipGuid);
    }
}

// ---- --smoke-anim EnterPlay 后快照断言位（clip/集/controller + 图驱动三段切换）（批③c-3 自 Run 外迁；挂点原位逐位不变）----
void EditorApp::SmokeAnimPlaySetup() {
    if (Launch().smokeAnim && ctx_.Playing()) {
        if (const ecs::ClipDef* cd =
                ctx_.ActiveWorld().Clips().Find((uint32_t)kAnimEditClipGuid))
            g_animSmoke.smokeClipPlayCacheOk = cd->fps == 13.0f && cd->frames.size() == 2;
        if (const ecs::ClipDef* wd =
                ctx_.ActiveWorld().Clips().Find((uint32_t)kAnimWholeClipGuid))
            g_animSmoke.smokeWholeOk = wd->frames.size() == 1;
        if (!g_animSmoke.smokeClipPlayCacheOk)
            LEMON_ERROR("smoke-anim：编辑档未进 Play clip 快照（anim-edit 13fps×2 帧）");
        if (!g_animSmoke.smokeWholeOk)
            LEMON_ERROR("smoke-anim：整图引用未进 Play clip 快照（anim-whole 1 帧）");
        // T3c：集登记进 Play 快照 + 按名/反查双口径（walk/hit 按名命中、whole 归集）
        g_animSmoke.smokeSetPlayCacheOk =
            ctx_.ActiveWorld().Clips().FindByName((uint32_t)kAnimSetGuid, "walk") ==
                (uint32_t)kAnimClipGuid &&
            ctx_.ActiveWorld().Clips().FindByName((uint32_t)kAnimSetGuid, "hit") ==
                (uint32_t)kAnimHitClipGuid &&
            ctx_.ActiveWorld().Clips().SetOfClip((uint32_t)kAnimWholeClipGuid) ==
                (uint32_t)kAnimSetGuid;
        if (!g_animSmoke.smokeSetPlayCacheOk)
            LEMON_ERROR("smoke-anim：动画集未进 Play 快照或按名反查不符（anim-set.override）");
        // T3d：状态机进 Play 快照 + 图驱动三段切换（whole→walk 条件边 → hit
        // trigger 边 → hit→whole exitTime 段末回归）。同步步进 Play 世界（Animator
        // → CSharpBatch → AnimGraph 全链；无脚本世界照常，图评估不依赖脚本）。
        // 独立实体 + whole 稳态 = 与队列钩子（按 clipId==anim.anim 认实体）零交集。
        if (const ecs::ControllerDef* cd =
                ctx_.ActiveWorld().Controllers().Find((uint32_t)kAnimGraphGuid)) {
            g_animSmoke.smokeGraphCacheOk = cd->states.size() == 3 && cd->transitions.size() == 3;
        }
        if (!g_animSmoke.smokeGraphCacheOk)
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
            g_animSmoke.smokeGraphSwitchOk =
                toWalk && toHit && trigCleared &&
                ps.Get<ecs::Animator2D>(ge).clipId == (uint32_t)kAnimWholeClipGuid;
            if (!g_animSmoke.smokeGraphSwitchOk)
                LEMON_ERROR("smoke-anim：图驱动切换不符（toWalk=%d toHit=%d trig=%d end=%08x）",
                            (int)toWalk, (int)toHit, (int)trigCleared,
                            ps.Get<ecs::Animator2D>(ge).clipId);
        }
    }
}

// ---- --smoke-anim 帧链（模态点击/选择器多选/帧序/极简创建/胶片带）（批③c-3 自 Run 外迁；挂点原位逐位不变）----
void EditorApp::SmokeAnimFrame(uint64_t frame) {
    if (Launch().smokeAnim) {
        if (frame >= 2 && frame < 40) {
            ImVec2 a, b;
            const bool q = testhooks::Find("animset.queued", a, b);
            if (q != g_animSmoke.smokeSetModalQueuedPrev) {
                std::printf("[lemon] probe: 新建集模态 queued=%s @f%llu\n",
                            q ? "ON" : "OFF", (unsigned long long)frame);
                g_animSmoke.smokeSetModalQueuedPrev = q;
            }
            // 三图优化点①：左列段行元信息缓存（f30 = 模态开着但工作台照画，
            // 四段行都已解析）。walk/hit/whole = 播种原值；edit = 播种 1 帧 +
            // clip 编辑链 +1 的复合值——顺带锁住"元信息读的是存盘内容"
            if (frame == 30 && !g_animSmoke.smokeLeftColDone) {
                g_animSmoke.smokeLeftColDone = true;
                if (PanelRegistry::Entry* en = panels_.FindEntry("Animation")) {
                    AnimationPanel* ap = static_cast<AnimationPanel*>(en->panel);
                    const int walk = ap->SegRowFramesForTest(0);
                    const int hit = ap->SegRowFramesForTest(1);
                    const int edit = ap->SegRowFramesForTest(2);
                    const int whole = ap->SegRowFramesForTest(3);
                    g_animSmoke.smokeLeftColOk = walk == 4 && hit == 2 && edit == 2 && whole == 1;
                    std::printf("[lemon] probe: 左列段行 walk=%d hit=%d edit=%d "
                                "whole=%d (expect 4/2/2/1) @f%llu\n",
                                walk, hit, edit, whole, (unsigned long long)frame);
                }
                if (!g_animSmoke.smokeLeftColOk)
                    LEMON_ERROR("smoke-anim：左列段行元信息未填充/帧数不符（优化点①）");
            }
        } else if (frame == 40) {
            ImVec2 mn, mx;
            if (testhooks::Find("animset.create", mn, mx)) {
                g_animSmoke.smokeCreatePt = Vec2{(mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f};
                ui_->SetInputOverride(g_animSmoke.smokeCreatePt.x, g_animSmoke.smokeCreatePt.y, 1);
            } else {
                ImVec2 a, b;
                const bool skip = testhooks::Find("animpanel.skip", a, b);
                const bool began = testhooks::Find("animpanel.begin", a, b);
                const bool queued = testhooks::Find("animset.queued", a, b);
                LEMON_ERROR("smoke-anim：新建集模态「创建」按钮未登记（探针 skip=%d "
                            "begin=%d queued=%d）",
                            skip ? 1 : 0, began ? 1 : 0, queued ? 1 : 0);
            }
        } else if (frame == 41 && g_animSmoke.smokeCreatePt.x > -1.0e8f) {
            ui_->SetInputOverride(g_animSmoke.smokeCreatePt.x, g_animSmoke.smokeCreatePt.y, 0);
        } else if (frame == 45 && !g_animSmoke.smokeSetCreateFlowDone) {
            g_animSmoke.smokeSetCreateFlowDone = true;
            const AssetEntry* ne = ctx_.Assets().FindByPath("Assets/player.override");
            if (PanelRegistry::Entry* en = panels_.FindEntry("Animation"))
                g_animSmoke.smokeSetCreateOpenOk =
                    ne && !ne->missing && ne->type == AssetType::AnimSet &&
                    static_cast<AnimationPanel*>(en->panel)->SetGuidForTest() == ne->guid;
            if (!g_animSmoke.smokeSetCreateOpenOk)
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
                g_animSmoke.smokePickPt = Vec2{(mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f};
                ui_->SetInputOverride(g_animSmoke.smokePickPt.x, g_animSmoke.smokePickPt.y, 1);
            } else {
                LEMON_ERROR("smoke-anim：选择器 tile0 未登记（网格没画）");
            }
        } else if (frame == 56 && g_animSmoke.smokePickPt.x > -1.0e8f) {
            ui_->SetInputOverride(g_animSmoke.smokePickPt.x, g_animSmoke.smokePickPt.y, 0);
        } else if (frame == 58) {
            if (PanelRegistry::Entry* en = panels_.FindEntry("Animation"))
                g_animSmoke.smokePickClickOk =
                    static_cast<AnimationPanel*>(en->panel)->PickerSelCountForTest() == 1;
            if (!g_animSmoke.smokePickClickOk)
                LEMON_ERROR("smoke-anim：注入点击后多选计数 != 1");
        } else if (frame == 60) {
            if (PanelRegistry::Entry* en = panels_.FindEntry("Animation"))
                static_cast<AnimationPanel*>(en->panel)->PickerClickForTest(7, false, true);
        } else if (frame == 62) {
            if (PanelRegistry::Entry* en = panels_.FindEntry("Animation"))
                g_animSmoke.smokePickShiftOk =
                    static_cast<AnimationPanel*>(en->panel)->PickerSelCountForTest() == 8;
            if (!g_animSmoke.smokePickShiftOk)
                LEMON_ERROR("smoke-anim：Shift 范围选择后计数 != 8");
        } else if (frame == 64) {
            ui_->SetKeyChordOverride((int)ImGuiMod_Ctrl, (int)ImGuiKey_A);
        } else if (frame == 66 && !g_animSmoke.smokePickFlowDone) {
            g_animSmoke.smokePickFlowDone = true;
            if (PanelRegistry::Entry* en = panels_.FindEntry("Animation"))
                g_animSmoke.smokePickAllOk =
                    static_cast<AnimationPanel*>(en->panel)->PickerSelCountForTest() == 11;
            if (!g_animSmoke.smokePickAllOk)
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
                g_animSmoke.smokeSheetPt = Vec2{(mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f};
                ui_->SetInputOverride(g_animSmoke.smokeSheetPt.x, g_animSmoke.smokeSheetPt.y, 1);
            } else {
                LEMON_ERROR("smoke-anim：文件选择器「取消」未登记（选择器没开？）");
            }
        } else if (frame == 69 && g_animSmoke.smokeSheetPt.x > -1.0e8f) {
            ui_->SetInputOverride(g_animSmoke.smokeSheetPt.x, g_animSmoke.smokeSheetPt.y, 0);
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
                g_animSmoke.smokeSheetPt = Vec2{(mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f};
                ui_->SetInputOverride(g_animSmoke.smokeSheetPt.x, g_animSmoke.smokeSheetPt.y, 1);
            } else {
                LEMON_ERROR("smoke-anim：选帧对话框 %s 未登记（对话框没开？）", key);
            }
        } else if ((frame == 73 || frame == 79 || frame == 85) && g_animSmoke.smokeSheetPt.x > -1.0e8f) {
            ui_->SetInputOverride(g_animSmoke.smokeSheetPt.x, g_animSmoke.smokeSheetPt.y, 0);
        } else if (frame == 76) {
            if (PanelRegistry::Entry* en = panels_.FindEntry("Animation"))
                g_animSmoke.smokeSheetAllOk =
                    static_cast<AnimationPanel*>(en->panel)->SheetPickCountForTest() == 4;
            if (!g_animSmoke.smokeSheetAllOk)
                LEMON_ERROR("smoke-anim：选帧对话框全选后已选计数 != 4");
        } else if (frame == 80) {
            // 底行越窗锁（2026-09-28 用户截图报：右对齐公式按两钮算，「替换为」
            // 整钮出窗、「添加」右缘贴边被裁）：对话框开着的帧取替换钮与模态窗
            // 矩形（TestHooks 每帧清空，只能在开窗帧断），右缘须在窗内。
            ImVec2 rmn, rmx, wmn, wmx;
            g_animSmoke.smokeSheetRowOk = testhooks::Find("sheetpick.replace", rmn, rmx) &&
                              testhooks::Find("sheetpick.win", wmn, wmx) &&
                              rmx.x <= wmx.x - 2.0f;
            if (!g_animSmoke.smokeSheetRowOk)
                LEMON_ERROR("smoke-anim：底行「替换为」按钮越出选帧对话框窗界");
        } else if (frame == 82) {
            ImVec2 mn, mx;
            const bool cntRect = testhooks::Find("sheetpick.count", mn, mx);
            if (PanelRegistry::Entry* en = panels_.FindEntry("Animation"))
                g_animSmoke.smokeSheetClearOk =
                    cntRect &&
                    static_cast<AnimationPanel*>(en->panel)->SheetPickCountForTest() == 0;
            if (!g_animSmoke.smokeSheetClearOk)
                LEMON_ERROR("smoke-anim：清空后已选计数 != 0 或右栏计数矩形未登记");
        } else if (frame == 88 && !g_animSmoke.smokeSheetFlowDone) {
            g_animSmoke.smokeSheetFlowDone = true;
            if (PanelRegistry::Entry* en = panels_.FindEntry("Animation"))
                g_animSmoke.smokeSheetCloseOk =
                    !static_cast<AnimationPanel*>(en->panel)->SheetPickOpenForTest();
            if (!g_animSmoke.smokeSheetCloseOk)
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
                g_animSmoke.smokeAddBase = p->EditFrameCountForTest();
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
                g_animSmoke.smokeSheetPt = Vec2{(mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f};
                ui_->SetInputOverride(g_animSmoke.smokeSheetPt.x, g_animSmoke.smokeSheetPt.y, 1);
            } else {
                LEMON_ERROR("smoke-anim：选择器「打开」未登记（选择器没开/无选中？）");
            }
        } else if (frame == 97 && g_animSmoke.smokeSheetPt.x > -1.0e8f) {
            ui_->SetInputOverride(g_animSmoke.smokeSheetPt.x, g_animSmoke.smokeSheetPt.y, 0);
        } else if (frame == 104 && !g_animSmoke.smokeAddFlowDone) {
            g_animSmoke.smokeAddFlowDone = true;
            std::vector<uint64_t> want; // 期望 = Assets 根图片按文件名升序的 guid
            for (const AssetEntry* e : ctx_.Assets().EntriesInDir("Assets"))
                if (e->type == AssetType::Sprite && !e->missing) want.push_back(e->guid);
            AnimationPanel* p = nullptr;
            if (PanelRegistry::Entry* en = panels_.FindEntry("Animation"))
                p = static_cast<AnimationPanel*>(en->panel);
            if (p) {
                g_animSmoke.smokeAddCountOk = p->EditFrameCountForTest() == g_animSmoke.smokeAddBase + want.size();
                if (!g_animSmoke.smokeAddCountOk)
                    LEMON_ERROR("smoke-anim：多图追加后帧数 %zu != 基数 %zu + %zu",
                                p->EditFrameCountForTest(), g_animSmoke.smokeAddBase, want.size());
                g_animSmoke.smokeAddOrderOk = g_animSmoke.smokeAddCountOk;
                for (size_t k = 0; k < want.size() && g_animSmoke.smokeAddOrderOk; ++k)
                    g_animSmoke.smokeAddOrderOk = p->EditFrameSheetForTest(g_animSmoke.smokeAddBase + k) == want[k];
                if (!g_animSmoke.smokeAddOrderOk)
                    LEMON_ERROR("smoke-anim：多图追加帧序 != 文件名升序（第 %zu 位起错位）",
                                g_animSmoke.smokeAddBase);
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
                g_animSmoke.smokeSheetPt = Vec2{(mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f};
                ui_->SetInputOverride(g_animSmoke.smokeSheetPt.x, g_animSmoke.smokeSheetPt.y, 1);
            } else {
                LEMON_ERROR("smoke-anim：创建框「创建」未登记（框没开？）");
            }
        } else if (frame == 111 && g_animSmoke.smokeSheetPt.x > -1.0e8f) {
            ui_->SetInputOverride(g_animSmoke.smokeSheetPt.x, g_animSmoke.smokeSheetPt.y, 0);
        } else if (frame == 114) {
            AnimationPanel* p = nullptr;
            if (PanelRegistry::Entry* en = panels_.FindEntry("Animation"))
                p = static_cast<AnimationPanel*>(en->panel);
            const AssetEntry* nc = ctx_.Assets().FindByPath("Assets/walksrc.anim");
            const AssetEntry* i0 = ctx_.Assets().FindByPath("Assets/walksrc/pick0.png");
            g_animSmoke.smokeFolderCreateOk = p && nc && i0 && !nc->missing &&
                                  nc->type == AssetType::Clip &&
                                  p->TargetGuidForTest() == nc->guid &&
                                  p->EditFrameCountForTest() == 3 &&
                                  p->EditFrameSheetForTest(0) == i0->guid;
            if (!g_animSmoke.smokeFolderCreateOk)
                LEMON_ERROR("smoke-anim：文件夹创建链不符（落盘/切换/3帧/首帧序）");
        } else if (frame == 116) {
            OpenAnimationEditor(kAnimSetGuid);
        } else if (frame == 117) {
            AnimationPanel* p = nullptr;
            if (PanelRegistry::Entry* en = panels_.FindEntry("Animation"))
                p = static_cast<AnimationPanel*>(en->panel);
            const AssetEntry* nc = ctx_.Assets().FindByPath("Assets/walksrc.anim");
            if (p && nc) OpenAnimationEditor(nc->guid); // 双击裸 clip（同步置态）
            g_animSmoke.smokeDblClipOk = p && nc && p->SetGuidForTest() == 0 &&
                             p->TargetGuidForTest() == nc->guid;
            if (!g_animSmoke.smokeDblClipOk)
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
            g_animSmoke.smokeDragItemOk = p && p->StripCellItemForTest() != 0;
            if (!g_animSmoke.smokeDragItemOk)
                LEMON_ERROR("smoke-anim：帧格非交互件（p=%p frames=%zu id=%u）",
                            (void*)p, p ? p->EditFrameCountForTest() : (size_t)0,
                            p ? p->StripCellItemForTest() : 0u);
        }
    }
}

// ---- --smoke-anim 帧采样（curFrame 推进/切片区间/切段回 walk/fx 通道）（批③c-3 自 Run 外迁；挂点原位逐位不变）----
void EditorApp::SmokeAnimSample(uint64_t frame) {
    if (Launch().smokeAnim && ctx_.Playing() && frame > 5) {
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
            if (slot == 0 && g_animSmoke.smokeQueueHit) g_animSmoke.smokeQueueBack = true; // hit 后回 walk
            if (a.clipId == (uint32_t)kAnimHitClipGuid) g_animSmoke.smokeQueueHit = true;
            if (slot < 0) return;
            if (a.curFrame > g_animSmoke.smokeAnimMax[slot]) g_animSmoke.smokeAnimMax[slot] = a.curFrame;
            if (const ecs::SpriteRenderer* sr =
                    ps.TryGet<ecs::SpriteRenderer>(ecs::Scene::FromEntt(ent))) {
                const AssetEntry* e = sh[slot];
                if (e && e->Sliced() && sr->spriteId >= e->sliceBase &&
                    sr->spriteId < e->sliceBase + e->sliceCount)
                    g_animSmoke.smokeAnimSlice[slot] = true;
            }
        });
        if (!g_animSmoke.smokeFxSeen && frame > 30) {
            const ecs::FxChannel& fx = ctx_.ActiveWorld().Fx();
            g_animSmoke.smokeFxSeen = fx.TextCount() == 1 && fx.BarCount() >= 1 &&
                          std::strcmp(fx.TextAt(0).text, "12") == 0;
        }
    }
}

// ---- --smoke-anim 末帧裁决（批③c-3 自 Run 外迁）：RESULT 行打印，返回 animOk ----
bool EditorApp::SmokeAnimVerdict() {
    const AssetEntry* sh = ctx_.Assets().FindByGuid(kAnimSheetGuid);
    const AssetEntry* ysh = ctx_.Assets().FindByGuid(kYamiHeroSheetGuid);
    const AssetEntry* hcl = ctx_.Assets().FindByGuid(kAnimHitClipGuid);
    const bool booked = sh && sh->Sliced() && sh->sliceCount == 4;
    const bool hitBooked = hcl && !hcl->missing;
    bool animOk = booked && g_animSmoke.smokeAnimMax[0] > 0 && g_animSmoke.smokeAnimSlice[0];
    char yami[96] = "";
    if (ysh && !ysh->missing && ysh->Sliced()) {
        const bool yOk = g_animSmoke.smokeAnimMax[1] > 0 && g_animSmoke.smokeAnimSlice[1] && ysh->sliceCount == 9;
        animOk = animOk && yOk;
        std::snprintf(yami, sizeof(yami), " yami(maxFrame=%u slice=%s frames=%u)",
                      (unsigned)g_animSmoke.smokeAnimMax[1], g_animSmoke.smokeAnimSlice[1] ? "YES" : "NO",
                      ysh->sliceCount);
    }
    const bool queueOk = hitBooked && g_animSmoke.smokeQueueHit && g_animSmoke.smokeQueueBack;
    animOk = animOk && queueOk && g_animSmoke.smokeFxSeen;
    // M6a 批② T3：clip 编辑链（roundtrip + Play 快照生效）；T3b-1 整图引用；
    // T3c 动画集（roundtrip + 集登记 + 按名反查）；T3d 状态机（roundtrip +
    // 快照 + whole→walk→hit→whole 图驱动三段切换）
    animOk = animOk && g_animSmoke.smokeClipEditOk && g_animSmoke.smokeClipPlayCacheOk && g_animSmoke.smokeWholeOk &&
             g_animSmoke.smokeSetEditOk && g_animSmoke.smokeSetPlayCacheOk && g_animSmoke.smokeSetOpenOk &&
             g_animSmoke.smokeSetCreateOk && g_animSmoke.smokeSetCreateOpenOk && g_animSmoke.smokeGraphEditOk &&
             g_animSmoke.smokeGraphCacheOk && g_animSmoke.smokeGraphSwitchOk &&
             g_animSmoke.smokePickClickOk && g_animSmoke.smokePickShiftOk && g_animSmoke.smokePickAllOk &&
             g_animSmoke.smokeSheetAllOk && g_animSmoke.smokeSheetClearOk && g_animSmoke.smokeSheetCloseOk &&
             g_animSmoke.smokeSheetRowOk &&
             g_animSmoke.smokeAddCountOk && g_animSmoke.smokeAddOrderOk &&
             g_animSmoke.smokeFolderCreateOk && g_animSmoke.smokeDblClipOk &&
             g_animSmoke.smokeDragItemOk &&
             g_animSmoke.smokeLeftColOk;
    std::printf("[lemon] smoke-anim: prog(maxFrame=%u slice=%s booked=%u)%s "
                "queue(hitClip=%s hit=%s back=%s) fx(text/bar=%s) "
                "edit(rt=%s cache=%s whole=%s) set(rt=%s cache=%s open=%s "
                "create=%s flow=%s) graph(rt=%s cache=%s switch=%s) "
                "pick(click=%s shift=%s all=%s sheet(all=%s clear=%s close=%s row=%s)) "
                "multiadd(count=%s order=%s) create(folder=%s dblclip=%s) "
                "drag(item=%s) leftcol(meta=%s) => %s\n",
                (unsigned)g_animSmoke.smokeAnimMax[0], g_animSmoke.smokeAnimSlice[0] ? "YES" : "NO",
                sh ? sh->sliceCount : 0, yami, hitBooked ? "booked" : "MISSING",
                g_animSmoke.smokeQueueHit ? "YES" : "NO", g_animSmoke.smokeQueueBack ? "YES" : "NO",
                g_animSmoke.smokeFxSeen ? "YES" : "NO", g_animSmoke.smokeClipEditOk ? "YES" : "NO",
                g_animSmoke.smokeClipPlayCacheOk ? "YES" : "NO", g_animSmoke.smokeWholeOk ? "YES" : "NO",
                g_animSmoke.smokeSetEditOk ? "YES" : "NO", g_animSmoke.smokeSetPlayCacheOk ? "YES" : "NO",
                g_animSmoke.smokeSetOpenOk ? "YES" : "NO", g_animSmoke.smokeSetCreateOk ? "YES" : "NO",
                g_animSmoke.smokeSetCreateOpenOk ? "YES" : "NO",
                g_animSmoke.smokeGraphEditOk ? "YES" : "NO", g_animSmoke.smokeGraphCacheOk ? "YES" : "NO",
                g_animSmoke.smokeGraphSwitchOk ? "YES" : "NO",
                g_animSmoke.smokePickClickOk ? "YES" : "NO", g_animSmoke.smokePickShiftOk ? "YES" : "NO",
                g_animSmoke.smokePickAllOk ? "YES" : "NO", g_animSmoke.smokeSheetAllOk ? "YES" : "NO",
                g_animSmoke.smokeSheetClearOk ? "YES" : "NO", g_animSmoke.smokeSheetCloseOk ? "YES" : "NO",
                g_animSmoke.smokeSheetRowOk ? "YES" : "NO",
                g_animSmoke.smokeAddCountOk ? "YES" : "NO", g_animSmoke.smokeAddOrderOk ? "YES" : "NO",
                g_animSmoke.smokeFolderCreateOk ? "YES" : "NO", g_animSmoke.smokeDblClipOk ? "YES" : "NO",
                g_animSmoke.smokeDragItemOk ? "YES" : "NO",
                g_animSmoke.smokeLeftColOk ? "YES" : "NO",
                animOk ? "OK" : "FAIL");
    return animOk;
}
} // namespace lemon::editor





