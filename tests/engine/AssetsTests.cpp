// Lemon 引擎单测 — AssetsTests — 资产域（AssetIndex/manifest/LAT1
// 图集/project.lemon/SpriteRefs/PrefabCache）（M7c 批⓪ T2 自 engine_tests.cpp
// 按域拆出，函数体逐字节原样搬运； include/using 为全 TU 共享全集——跨域头依赖零编译风险，IWYU
// 精简不做）

#include "TestFramework.h"

// Lemon 引擎单测 — 纯逻辑层（数学/批键/图集 UV/相机/粒子池/音频混音）
// 断言风格：LEMON_ASSERT 失败即 abort，进程退出码非 0 = 测试失败。
#include "Core/Log.h"
#include "Core/Process.h" // CurrentProcessId（批⑦ win 清账：unistd/getpid 是 POSIX-only）

#include <chrono>
#include <cstdio>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <limits>
#include <filesystem>
#include <fstream>
#include <thread>
#include "Audio/AudioChannel.h" // M6c 批②：命令通道（World.h 链亦达，显式声明测试意图）
#include "Audio/AudioEngine.h"
#include "Audio/BakedClip.h"
#include "Audio/SpscRing.h" // M6c 批①b：SPSC 环序锁
#include "Assets/AssetIndex.h" // M7a 批②：运行时只读索引
#include "Assets/PlayCaches.h" // 批⑪ H2：Play 三缓存构建（坏 clip 红字跳过验收）
#include "Assets/AtlasBake.h" // M7a 批⑥：LAT1 容器/装箱/烤制
#include "Assets/AtlasStore.h" // M7a 批⑥：LAT1 装载登记核
#include "Assets/ProjectFile.h" // M7a 批②：project.lemon 只读解析
#include "Assets/SpriteRefs.h" // M7a 批②：guid 归一引擎本体
#include "Assets/PrefabCache.h" // M7a 批③：Play 世界 Prefab 工厂缓存
#include "Renderer/CameraFollow.h" // M7a 批③：相机跟随纯函数
#include "Renderer/SceneExtractor.h" // M7a 批③：ECS→渲染提取下沉件
#include "Core/Guid.h"
#include "Core/Math.h"
#include "stb_image_write.h" // M7a 批⑥：LAT1 夹具播种 PNG（实现符号在引擎 StbImage.cpp 单 TU）
#include "Components/AudioComponents.h" // M6c 批②：AudioSource
#include "Components/CoreComponents.h"
#include "ECS/Hierarchy.h"
#include "Renderer/Atlas.h"
#include "Renderer/BitmapFont.h"
#include "Renderer/Camera2D.h"
#include "Renderer/Particles.h"
#include "Renderer/Quality.h"
#include "Renderer/Renderable.h"
// ---------------------------------------------------------------- M2 Core --
#include "Core/FunctionRef.h"
#include "Core/JobSystem.h"
#include "Core/Pool.h"
#include "Core/Random.h"
#include "Core/RingQueue.h"
#include <atomic>
#include <numeric>
// ------------------------------------------------------- M2 ECS 骨架/组件 --
#include "Components/BehaviorComponents.h"
#include "Components/GameplayComponents.h"
#include "Components/RenderComponents.h"
#include "ECS/ComponentRegistry.h"
#include "ECS/SaveChannel.h"
#include "ECS/Scene.h"
#include "ECS/StateHash.h"
#include "ECS/World.h"
#include "Scripting/ScriptBox.h"
// --------------------------------------------------- M2 场景序列化(.scene) --
#include "Serialization/SceneArchive.h"
// ------------------------------------------------ M2 空间哈希 + Team -------
#include "Physics2D/SpatialHash.h"
// ------------------------------------------- M2 系统管线（16 系统端到端）--
#include "Systems/Systems.h"
// --------------------------------------------- M2 审计修复回归 --------------
// ------------------ M2 复核轮新增测试（2026-09-19，只读审计配套） -----------
// 2026-09-19 修复轮：ISSUE-1..8 已全部修复，原 [ISSUE-n] "固化现状"断言已同步
// 改为断言正确行为（问题登记与修法见 docs/Reports/2026-09-19-m2-review-checklist.md）。
// ---- M4.1：Hierarchy 链维护/防环/世界矩阵（内核 #1 + M2 复审 N6 遗留环检测测试）----
// ---- M4.1：Meta.guid 序列化往返（内核 #5）----

#ifdef LEMON_EDITOR_CORE
#include "Assets/AssetDatabase.h"
#include "Assets/SaveStore.h" // M7a 批③：SaveStore 直测（原 EditorContext 三方法已下沉）
#include "Assets/AnimAsset.h"
#include "Assets/ControllerAsset.h"
#include "Assets/TableAsset.h"
#include "Assets/FileWatcher.h"
#include "Assets/ProjectWizard.h"
#include "EditorContext.h"
#include "Serialization/SceneArchive.h"
#include "ECS/World.h"
#include "Scripting/ScriptBox.h"
#endif

using namespace lemon;
using namespace lemon::math;
using namespace lemon::renderer;
using namespace lemon::ecs;
using namespace lemon::physics2d;

namespace {

// ---- M7a 批②：project.lemon 只读解析 + entryScene 回退链 ----

void TestProjectFile() {
    namespace fs = std::filesystem;
    using namespace lemon::assets;

    const ProjectFile pf =
        ParseProjectFile("{\"schemaVersion\":1,\"name\":\"demo\",\"engineVersion\":\"0.4.0-m4\","
                         "\"guid\":\"9e9b2af4ee867201\",\"entryScene\":\"Scenes/MainMenu.scene\"}");
    Expect(pf.ok && pf.name == "demo" && pf.guid == 0x9e9b2af4ee867201ull &&
               pf.engineVersion == "0.4.0-m4" && pf.entryScene == "Scenes/MainMenu.scene",
           "project file full parse");
    Expect(ParseProjectFile("{\"name\":\"x\"}").ok, "minimal (name only) ok");
    Expect(!ParseProjectFile("{\"nope\":1}").ok, "missing name rejected");
    Expect(!ParseProjectFile("not json").ok, "bad json rejected");
    Expect(ParseProjectFile("{\"name\":\"x\",\"engineVersion\":\"9.9.9\"}").ok,
           "engineVersion mismatch tolerated (warn-not-block)");

    // ResolveEntryScene 三态（临时项目夹具）
    const fs::path root = fs::temp_directory_path() /
                          ("lemon-test-projfile-" + std::to_string(lemon::CurrentProcessId()));
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "Scenes", ec);
    {
        std::ofstream f(root / "Scenes" / "A.scene", std::ios::trunc);
        f << "{}";
    }
    {
        std::ofstream f(root / "project.lemon", std::ios::trunc);
        f << "{\"name\":\"p\"}";
    }
    ProjectFile bare = LoadProjectFile(root.string());
    Expect(bare.ok && ResolveEntryScene(root.string(), bare) == "Scenes/A.scene",
           "fallback: sole .scene resolves");
    {
        std::ofstream f(root / "Scenes" / "B.scene", std::ios::trunc);
        f << "{}";
    }
    Expect(ResolveEntryScene(root.string(), bare).empty(),
           "multi-scene without entryScene = empty (caller red-flags)");
    bare.entryScene = "Scenes/B.scene";
    Expect(ResolveEntryScene(root.string(), bare) == "Scenes/B.scene",
           "declared entryScene honored");
    bare.entryScene = "Scenes/Gone.scene";
    Expect(ResolveEntryScene(root.string(), bare).empty(),
           "dangling declaration = empty (no silent fallback)");
    fs::remove_all(root, ec);
}

// ---- M7a 批②：assets::ResolveSpriteRefs 引擎本体四态（mock 查询面；编辑器
// 端到端链路归 TestSpriteGuidResolve，此处在引擎侧锁进程独立性）----

void TestSpriteRefsEngine() {
    using namespace lemon::assets;
    struct MockSource : SpriteRefSource {
        std::vector<SpriteEntryView> views;
        uint32_t base = 100;
        const SpriteEntryView* SpriteByGuid(uint64_t g) const override {
            for (const SpriteEntryView& v : views)
                if (v.guid == g) return &v;
            return nullptr;
        }
        const SpriteEntryView* SpriteByWholeId(uint32_t id) const override {
            if (id == 0) return nullptr;
            for (const SpriteEntryView& v : views)
                if (v.spriteId == id) return &v;
            return nullptr;
        }
        uint32_t SpriteIdBase() const override { return base; }
    };

    World w;
    Scene& s = w.CreateScene("refs");
    MockSource src;
    const SpriteEntryView whole{0x1111222233334444ull, 100, 0, 0, true}; // 整图：本体 100
    const SpriteEntryView sheet{0x5555666677778888ull, 101, 102, 4,
                                true}; // 切片：本体 101、块 102..105
    src.views = {whole, sheet};

    SpriteRenderer& hitWhole =
        s.Emplace<SpriteRenderer>(s.Create()); // ① guid 命中：旧号区间外 → 本体号
    hitWhole.spriteGuid = whole.guid;
    hitWhole.spriteId = 777;
    SpriteRenderer& hitCell = s.Emplace<SpriteRenderer>(s.Create()); // ①' 切片表区间外 → cell 0
    hitCell.spriteGuid = sheet.guid;
    hitCell.spriteId = 999;
    SpriteRenderer& inRange = s.Emplace<SpriteRenderer>(s.Create()); // ①'' 区间内 → 保号
    inRange.spriteGuid = sheet.guid;
    inRange.spriteId = 104;
    SpriteRenderer& dangl = s.Emplace<SpriteRenderer>(s.Create()); // ② 悬空：保号 + 计数
    dangl.spriteGuid = 0xdeadbeefdeadbeefull;
    dangl.spriteId = 555;
    SpriteRenderer& legacy = s.Emplace<SpriteRenderer>(s.Create()); // ③ 存量：本体号 → 回填 guid
    legacy.spriteId = 100;
    SpriteRenderer& cell = s.Emplace<SpriteRenderer>(s.Create()); // ③' cell 号不回填
    cell.spriteId = 103;
    SpriteRenderer& proc = s.Emplace<SpriteRenderer>(s.Create()); // ③'' 程序化页号不回填
    proc.spriteId = 4;

    const SpriteRefStats st = ResolveSpriteRefs(s, src);
    Expect(hitWhole.spriteId == 100, "whole: out-of-range re-normalized to body id");
    Expect(hitCell.spriteId == 102, "sliced: out-of-range falls back to cell 0");
    Expect(inRange.spriteId == 104, "in-range id kept (no rewrite)");
    Expect(dangl.spriteId == 555 && st.danglingGuid == 1, "dangling keeps legacy id + counted");
    Expect(legacy.spriteGuid == whole.guid && st.backfilled == 1, "legacy body id backfilled");
    Expect(cell.spriteGuid == 0, "cell id NOT backfilled (only body ids)");
    Expect(proc.spriteGuid == 0, "procedural page id NOT backfilled");
    // 幂等：已归一场景再跑零写入
    const SpriteRefStats st2 = ResolveSpriteRefs(s, src);
    Expect(st2.backfilled == 0 && st2.danglingGuid == 1, "resolve idempotent");
}

// ---- M4.4-a：Atlas 页热更新（AssetGpuCache 热重导入的登记侧语义）----

#ifdef LEMON_EDITOR_CORE
void TestAtlasPageHotUpdate() {
    AtlasRegistry reg;
    rhi::Texture fake{2}; // 纯登记测试：句柄只是整数，无 GPU 语义
    reg.RegisterAtlas(2, fake, 64, 64);
    uint32_t id = reg.AddSprite(2, 0, 0, 64, 64);
    Expect(id == 1, "first sprite id is 1");
    const SpriteInfo& s0 = reg.GetSprite(id);
    Expect(s0.widthPx == 64 && s0.heightPx == 64 && s0.u1 == 1.0f && s0.v1 == 1.0f,
           "full-page sprite uv/dims");
    reg.UpdateAtlasPage(2, rhi::Texture{3}, 96, 48);
    const SpriteInfo& s1 = reg.GetSprite(id);
    Expect(s1.widthPx == 96 && s1.heightPx == 48, "hot update refreshes pixel dims");
    Expect(s1.u0 == 0.0f && s1.v0 == 0.0f && s1.u1 == 1.0f && s1.v1 == 1.0f,
           "full-page uv stays 0..1 after resize");
    Expect(s1.atlasIndex == 2, "atlas slot preserved");
}
#endif // LEMON_EDITOR_CORE

// ---- M7a 前置：低 32 位碰撞体检 + 发号唯一性（2026-10-01，svr-test Player/Mob 实证）----
// prefabId/clipId/controllerId/表 id 均取资产 GUID 低 32 位（03 §69 恒 uint32），
// 同类型两资产低 32 位同值 = 运行时静默丢映射。锁两件事：①体检按【同类型域】红字
// （跨类型同低 32 位合法——不同键空间，HealthIssues==1 钉死域语义）；②新发号避开
// 域内已占低 32 位。

#ifdef LEMON_EDITOR_CORE
void TestAssetDatabaseLow32Collision() {
    namespace fs = std::filesystem;
    using lemon::editor::AssetDatabase;
    using lemon::editor::AssetEntry;

    const fs::path root = fs::temp_directory_path() /
                          ("lemon-test-low32-" + std::to_string(lemon::CurrentProcessId()));
    std::error_code ec;
    fs::remove_all(root, ec);

    AssetDatabase db;
    Expect(db.OpenProject(root.string(), /*spriteIdBase=*/100), "open project");

    auto put = [&](const std::string& rel, const char* guidHex) {
        const fs::path p = root / rel;
        fs::create_directories(p.parent_path(), ec);
        {
            std::ofstream f(p, std::ios::binary);
            f << "x";
        }
        std::ofstream f(p.string() + ".meta", std::ios::trunc);
        f << "{\"guid\":\"" << guidHex << "\"}";
    };
    put("Assets/a.anim", "111100000000000a");
    put("Assets/b.anim", "222200000000000a"); // 与 a 同低 32 位（…0000000a）→ 红字
    put("Prefabs/p.prefab", "333300000000000a"); // 同低 32 位但异域（prefab≠clip）→ 不报
    {
        std::ofstream f(root / "Assets" / "c.anim", std::ios::binary);
        f << "y";
    } // 无 meta → 新发号

    db.Rescan();
    Expect(db.HealthIssues() == 1, "same-type low32 collision flagged exactly once");
    const AssetEntry* c = db.FindByPath("Assets/c.anim");
    Expect(c && c->guid != 0 && (uint32_t)c->guid != 0x0000000aull,
           "allocated guid avoids taken low32 in domain");
    fs::remove_all(root, ec);
}
#endif // LEMON_EDITOR_CORE

// ---- 孤儿 .meta 清扫（2026-10-01 拍板：Unity/Cocos 式自动清 + 引用判据保守保留）----
// 三态：源缺失 + guid 零引用 = 扫描期自动删；源缺失 + 仍被引用 = meta 保留 + 红字
// （"只恢复源文件"场景的复链钩子，盲删永久断引用）；源+meta 双删但仍被引用 = 红字。
// 引用面 = 项目数据文本（hex 小写/大写 + 十进制三针）；手动 SweepOrphanMetas() 同判定。

#ifdef LEMON_EDITOR_CORE
void TestOrphanMetaSweep() {
    namespace fs = std::filesystem;
    using lemon::editor::AssetDatabase;

    const fs::path root = fs::temp_directory_path() /
                          ("lemon-test-sweep-" + std::to_string(lemon::CurrentProcessId()));
    std::error_code ec;
    fs::remove_all(root, ec);

    AssetDatabase db;
    Expect(db.OpenProject(root.string(), /*spriteIdBase=*/100), "open project for sweep");

    auto put = [&](const std::string& rel, const char* guidHex) {
        const fs::path p = root / rel;
        fs::create_directories(p.parent_path(), ec);
        {
            std::ofstream f(p, std::ios::binary);
            f << "x";
        }
        std::ofstream f(p.string() + ".meta", std::ios::trunc);
        f << "{\"guid\":\"" << guidHex << "\"}";
    };
    put("Assets/hero.png", "aaaa000000000001"); // 将被引用
    put("Assets/mob.png", "aaaa000000000002"); // 零引用
    put("Assets/gem.png", "aaaa000000000003"); // 将被引用（十进制形态）
    fs::create_directories(root / "Scenes", ec);
    {
        std::ofstream f(root / "Scenes" / "ref.scene", std::ios::trunc);
        f << "{\"heroRef\": \"aaaa000000000001\", \"gemRef\": " << 0xaaaa000000000003ull
          << "}"; // hex 串 + 十进制双形态
    }
    db.Rescan();
    Expect(db.FindByPath("Assets/hero.png") && db.FindByPath("Assets/mob.png") &&
               db.FindByPath("Assets/gem.png"),
           "three assets alive");

    // ① 外部删源（meta 残留）：hero 被引用 → 条目出表 + meta 保留 + 红字恰一次；
    // mob 零引用 → meta 自动清扫、零红字
    fs::remove(root / "Assets" / "hero.png", ec);
    fs::remove(root / "Assets" / "mob.png", ec);
    db.Rescan();
    Expect(db.FindByPath("Assets/hero.png") == nullptr, "referenced deleted entry dropped");
    Expect(fs::exists(root / "Assets" / "hero.png.meta", ec), "referenced orphan meta kept");
    Expect(!fs::exists(root / "Assets" / "mob.png.meta", ec), "unreferenced orphan meta swept");
    Expect(db.HealthIssues() == 1, "kept referenced orphan flagged exactly once");

    // ② 双删（源+meta 同时）但仍被引用 → 出表 + 红字（悬空可见性；与 ① 的保留红共存）
    fs::remove(root / "Assets" / "gem.png", ec);
    fs::remove(root / "Assets" / "gem.png.meta", ec);
    db.Rescan();
    Expect(db.FindByPath("Assets/gem.png") == nullptr, "double-deleted entry dropped");
    Expect(db.HealthIssues() == 2, "double-deleted-but-referenced flagged (plus kept meta)");

    // ③ 手动清扫入口（菜单）：同判定立即执行并出报告；复用 ① 的 hero.meta（引用态）
    {
        std::ofstream f(root / "Assets" / "stray.png.meta", std::ios::trunc);
        f << "{\"guid\":\"aaaa000000000004\"}";
    }
    AssetDatabase::OrphanSweepResult r = db.SweepOrphanMetas();
    Expect(r.cleaned.size() == 1 && r.keptReferenced.size() == 1, "manual sweep report");
    Expect(!fs::exists(root / "Assets" / "stray.png.meta", ec), "stray swept by manual call");
    Expect(fs::exists(root / "Assets" / "hero.png.meta", ec), "referenced meta still kept");
    fs::remove_all(root, ec);
}
#endif // LEMON_EDITOR_CORE

// ---- M4.4-a：AssetDatabase 生命周期（GUID 稳定/manifest 记账/体检）----

#ifdef LEMON_EDITOR_CORE
void TestAssetDatabaseLifecycle() {
    namespace fs = std::filesystem;
    using lemon::editor::AssetDatabase;
    using lemon::editor::AssetEntry;
    using lemon::editor::AssetType;

    const fs::path root = fs::temp_directory_path() /
                          ("lemon-test-assets-" + std::to_string(lemon::CurrentProcessId()));
    std::error_code ec;
    fs::remove_all(root, ec);

    AssetDatabase db;
    Expect(db.OpenProject(root.string(), /*spriteIdBase=*/100), "open project");
    Expect(db.SpriteAssetCount() == 0, "empty project starts clean");
    Expect(db.HealthIssues() == 0, "empty project no health issues");

    // 手工放两个资产（内容任意——DB 只哈希不解码）+ 一个预置 .meta 固定 guid
    fs::create_directories(root / "Assets" / "icons", ec);
    {
        std::ofstream f(root / "Assets" / "hero.png", std::ios::binary);
        f << "png-bytes-1";
    }
    {
        std::ofstream f(root / "Assets" / "icons" / "coin.png", std::ios::binary);
        f << "png-B";
    }
    {
        std::ofstream f(root / "Assets" / "notes.txt", std::ios::binary);
        f << "x";
    }
    {
        std::ofstream f(root / "Assets" / "icons" / "coin.png.meta", std::ios::trunc);
        f << "{\"guid\":\"1122334455667788\",\"type\":\"sprite\"}";
    }

    db.Rescan();
    Expect(db.SpriteAssetCount() == 2, "two sprites discovered");
    const AssetEntry* hero = db.FindByPath("Assets/hero.png");
    const AssetEntry* coin = db.FindByPath("Assets/icons/coin.png");
    Expect(hero && coin, "entries located by path (project-root relative)");
    Expect(hero->type == AssetType::Sprite && coin->type == AssetType::Sprite, "png typed sprite");
    Expect(db.FindByPath("Assets/notes.txt") != nullptr, "generic file tracked");
    Expect(coin->guid == 0x1122334455667788ull, "preset meta guid honored");
    Expect(hero->spriteId == 100 && coin->spriteId == 101, "spriteIds allocated from base");
    const uint64_t heroGuid = hero->guid;
    Expect(heroGuid != 0, "auto guid assigned");
    Expect(fs::exists(root / "Assets" / "hero.png.meta", ec), "meta sidecar written");

    // M4.5 扫根（06 §1）：根级 Prefabs/ 入索引；Game/Scenes 排除
    fs::create_directories(root / "Prefabs", ec);
    fs::create_directories(root / "Game", ec);
    {
        std::ofstream f(root / "Prefabs" / "mob.prefab", std::ios::binary);
        f << "{}";
    }
    {
        std::ofstream f(root / "Game" / "GameMain.cs", std::ios::binary);
        f << "// x";
    }
    {
        std::ofstream f(root / "Scenes" / "Main.scene", std::ios::binary);
        f << "{}";
    }
    db.Rescan();
    const AssetEntry* pf = db.FindByPath("Prefabs/mob.prefab");
    Expect(pf && pf->type == AssetType::Prefab, "root-level Prefabs/ indexed");
    Expect(db.FindByPath("Game/GameMain.cs") == nullptr, "Game/ excluded from asset scan");
    Expect(db.FindByPath("Scenes/Main.scene") == nullptr, "Scenes/ excluded from asset scan");

    // guid 持久：重开项目（新实例走 manifest 携带；M4.4 旧格式键自动迁移同号）→ 同 guid 同 spriteId
    {
        AssetDatabase db2;
        Expect(db2.OpenProject(root.string(), 100), "reopen project");
        const AssetEntry* h2 = db2.FindByPath("Assets/hero.png");
        Expect(h2 && h2->guid == heroGuid && h2->spriteId == 100,
               "guid/spriteId stable across sessions (manifest)");
        const AssetEntry* c2 = db2.FindByGuid(0x1122334455667788ull);
        Expect(c2 && c2->spriteId == 101, "preset guid stable across sessions");
    }

    // 内容变化 → modified（guid 不变）
    {
        std::ofstream f(root / "Assets" / "hero.png", std::ios::binary | std::ios::trunc);
        f << "png-bytes-CHANGED-longer";
    }
    db.Rescan();
    Expect(db.LastChange().modified.size() == 1 && db.LastChange().modified[0] == heroGuid,
           "content change detected as modified");
    Expect(db.FindByGuid(heroGuid) && db.FindByGuid(heroGuid)->relPath == "Assets/hero.png",
           "guid survives content change");

    // 重命名 → 引用不断（guid 不变路径变；meta 随行）。relPath 语义 = 项目根相对
    {
        AssetEntry* h = const_cast<AssetEntry*>(db.FindByGuid(heroGuid));
        Expect(db.Rename(*h, "Assets/renamed/hero2.png"), "rename ok");
        Expect(db.FindByGuid(heroGuid)->relPath == "Assets/renamed/hero2.png", "path moved");
        Expect(fs::exists(root / "Assets" / "renamed" / "hero2.png.meta", ec),
               "meta traveled with file");
        db.Rescan();
        Expect(db.FindByGuid(heroGuid) && !db.FindByGuid(heroGuid)->missing,
               "renamed asset rescans alive (guid intact)");
    }

    // 删除文件 → 条目出表（墓碑 2026-10-01 退役，06 §2.2 修订）；号不回收
    fs::remove(root / "Assets" / "icons" / "coin.png", ec);
    fs::remove(root / "Assets" / "icons" / "coin.png.meta", ec);
    db.Rescan();
    Expect(db.FindByGuid(0x1122334455667788ull) == nullptr,
           "deleted asset entry dropped (no tombstone)");
    Expect(db.LastChange().removed.size() == 1, "removal reported");
    {
        std::ofstream f(root / "Assets" / "new.png", std::ios::binary);
        f << "n";
    }
    db.Rescan();
    const AssetEntry* np = db.FindByPath("Assets/new.png");
    Expect(np && np->spriteId == 102, "new sprite id never reuses freed id");

    // 孤儿 meta：零引用 = 扫描期自动清扫（不再红字永续）
    {
        std::ofstream f(root / "Assets" / "orphan.png.meta", std::ios::trunc);
        f << "{}";
    }
    db.Rescan();
    Expect(!fs::exists(root / "Assets" / "orphan.png.meta", ec), "unreferenced orphan meta swept");
    Expect(db.HealthIssues() == 0, "sweep leaves no health issue");

    fs::remove_all(root, ec);
}
#endif // LEMON_EDITOR_CORE

// ---- F-02（2026-09-24）：路径 containment——重命名/导入/项目名不得越出项目根 ----
// ---- M6a 批② T3b-3：SetGridSlice（.meta importer 写入 → Rescan 连号块/烧号/撤销）----

#ifdef LEMON_EDITOR_CORE
void TestGridSliceConfig() {
    namespace fs = std::filesystem;
    using lemon::editor::AssetDatabase;
    using lemon::editor::AssetEntry;

    const fs::path root = fs::temp_directory_path() /
                          ("lemon-test-slice-" + std::to_string(lemon::CurrentProcessId()));
    std::error_code ec;
    fs::remove_all(root, ec);
    AssetDatabase db;
    Expect(db.OpenProject(root.string(), 100), "open project for slice");
    {
        std::ofstream f(root / "Assets" / "sheet.png", std::ios::binary);
        f << "png";
    }
    db.Rescan();
    const AssetEntry* e = db.FindByPath("Assets/sheet.png");
    Expect(e && !e->Sliced(), "unsliced at start");

    AssetEntry* m = db.FindByGuid(e->guid);
    Expect(db.SetGridSlice(*m, 32, 48, 8, 1), "set grid slice writes meta");
    db.Rescan();
    e = db.FindByPath("Assets/sheet.png");
    Expect(e && e->Sliced() && e->gridCols == 8 && e->gridRows == 1 && e->cellW == 32 &&
               e->cellH == 48 && e->sliceCount == 8 && e->SliceSpriteId(7) == e->sliceBase + 7,
           "rescan picks up importer block");
    Expect(e->SliceSpriteId(8) == 0, "cell out of range -> 0");

    // frames 增大 → 新块烧号（旧块留号；"只增不减"语义）
    const uint32_t oldBase = e->sliceBase;
    m = db.FindByGuid(e->guid);
    Expect(db.SetGridSlice(*m, 32, 48, 8, 2), "grow grid");
    db.Rescan();
    e = db.FindByPath("Assets/sheet.png");
    Expect(e && e->sliceCount == 16 && e->sliceBase >= oldBase + 8, "grown block burns ids");

    // 撤销切片 → 整图（meta importer 段移除）
    m = db.FindByGuid(e->guid);
    Expect(db.SetGridSlice(*m, 0, 0, 0, 0), "clear slice");
    db.Rescan();
    e = db.FindByPath("Assets/sheet.png");
    Expect(e && !e->Sliced(), "cleared back to whole image");

    // 非 sprite 拒绝
    {
        std::ofstream f(root / "Assets" / "x.anim", std::ios::binary);
        f << "{}";
    }
    db.Rescan();
    if (const AssetEntry* c = db.FindByPath("Assets/x.anim"))
        Expect(!db.SetGridSlice(*db.FindByGuid(c->guid), 1, 1, 1, 1), "non-sprite rejected");
    else
        Expect(false, "clip entry found");
    fs::remove_all(root, ec);
}

// ---- M7c 批②：SetAudioImporter（.meta importer 写入 → Rescan 读回；哨兵 = 键缺省）----
void TestAudioImporterConfig() {
    namespace fs = std::filesystem;
    using lemon::editor::AssetDatabase;
    using lemon::editor::AssetEntry;

    const fs::path root = fs::temp_directory_path() /
                          ("lemon-test-afx-" + std::to_string(lemon::CurrentProcessId()));
    std::error_code ec;
    fs::remove_all(root, ec);
    AssetDatabase db;
    Expect(db.OpenProject(root.string(), 100), "open project for audio fx");
    {
        std::ofstream f(root / "Assets" / "hit.wav", std::ios::binary);
        f << "wav";
    }
    db.Rescan();
    const AssetEntry* e = db.FindByPath("Assets/hit.wav");
    Expect(e && e->audioRetriggerCd < 0.0f && e->audioVoiceCap == 0 &&
               e->audioPitchJitter < 0.0f,
           "fx sentinels (inherit) by default");

    // 覆写写入：meta 落盘（三键序列化）→ Rescan 读回同值（pitchJitter 0 = 显式关）
    AssetEntry* m = db.FindByGuid(e->guid);
    lemon::audio::ClipFx fx;
    fx.retriggerCdSec = 0.15f;
    fx.voiceCap = 2;
    fx.pitchJitter = 0.0f;
    Expect(db.SetAudioImporter(*m, 1.5f, 10.0f, true, fx), "set audio importer writes meta");
    {
        std::ifstream mf(root / "Assets" / "hit.wav.meta", std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(mf)),
                               std::istreambuf_iterator<char>());
        Expect(text.find("\"retrigger\"") != std::string::npos &&
                   text.find("\"voiceCap\"") != std::string::npos &&
                   text.find("\"pitchJitter\"") != std::string::npos,
               "fx keys serialized");
    }
    db.Rescan();
    e = db.FindByPath("Assets/hit.wav");
    Expect(e && e->audioLoopStart == 1.5f && e->audioLoopEnd == 10.0f && e->audioPreload &&
               e->audioRetriggerCd == 0.15f && e->audioVoiceCap == 2 &&
               e->audioPitchJitter == 0.0f,
           "rescan picks up fx overrides");

    // 哨兵回写 = 键缺省（继承；与"显式 0"可区分——pitchJitter 0 此前读过为 0）
    m = db.FindByGuid(e->guid);
    const lemon::audio::ClipFx inherit; // 全哨兵
    Expect(db.SetAudioImporter(*m, 0.0f, 0.0f, false, inherit), "rewrite with sentinels");
    {
        std::ifstream mf(root / "Assets" / "hit.wav.meta", std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(mf)),
                               std::istreambuf_iterator<char>());
        Expect(text.find("\"retrigger\"") == std::string::npos &&
                   text.find("\"voiceCap\"") == std::string::npos &&
                   text.find("\"pitchJitter\"") == std::string::npos,
               "sentinels drop fx keys");
    }
    db.Rescan();
    e = db.FindByPath("Assets/hit.wav");
    Expect(e && e->audioRetriggerCd < 0.0f && e->audioVoiceCap == 0 &&
               e->audioPitchJitter < 0.0f,
           "sentinel readback inherits");

    // 域外值（手改 meta）→ 宽容拒绝回继承（与 loop 段同款不红字口径）
    {
        std::ofstream f(root / "Assets" / "hit.wav.meta", std::ios::trunc);
        f << "{\"guid\":\"" << lemon::assets::GuidToHex(e->guid)
          << "\",\"type\":\"audio\",\"importer\":{\"retrigger\":99.0,\"voiceCap\":999,"
             "\"pitchJitter\":-0.5}}";
    }
    db.Rescan();
    e = db.FindByPath("Assets/hit.wav");
    Expect(e && e->audioRetriggerCd < 0.0f && e->audioVoiceCap == 0 &&
               e->audioPitchJitter < 0.0f,
           "out-of-domain values rejected to inherit");

    // 非 audio 拒绝
    {
        std::ofstream f(root / "Assets" / "x.png", std::ios::binary);
        f << "png";
    }
    db.Rescan();
    if (const AssetEntry* c = db.FindByPath("Assets/x.png"))
        Expect(!db.SetAudioImporter(*db.FindByGuid(c->guid), 0, 0, false, {}),
               "non-audio rejected");
    else
        Expect(false, "png entry found");
    fs::remove_all(root, ec);
}
#endif // LEMON_EDITOR_CORE

#ifdef LEMON_EDITOR_CORE
void TestAssetPathContainment() {
    namespace fs = std::filesystem;
    using lemon::editor::AssetDatabase;
    using lemon::editor::AssetEntry;

    const fs::path root = fs::temp_directory_path() /
                          ("lemon-test-paths-" + std::to_string(lemon::CurrentProcessId()));
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "Assets", ec);
    {
        std::ofstream f(root / "Assets" / "hero.png", std::ios::binary);
        f << "png";
    }

    AssetDatabase db;
    Expect(db.OpenProject(root.string(), 100), "open project");
    db.Rescan();
    AssetEntry* hero = db.FindByPath("Assets/hero.png");
    Expect(hero != nullptr, "hero located");
    const fs::path outside = root.parent_path() / "lemon-escape-probe.png";

    // 越界重命名拒绝：文件不动、条目不变
    Expect(!db.Rename(*hero, "../escape.png"), "rename with .. rejected");
    Expect(hero->relPath == "Assets/hero.png" && fs::exists(root / "Assets" / "hero.png", ec),
           "hero unmoved after rejected rename");
    // 绝对路径落点拒绝
    Expect(!db.Rename(*hero, outside.string()), "absolute rename target rejected");
    // 越界导入拒绝
    {
        std::ofstream f(root / "src.png", std::ios::binary);
        f << "x";
    }
    Expect(db.ImportFile((root / "src.png").string(), "../stolen.png") == nullptr,
           "import with .. rejected");
    Expect(!fs::exists(outside, ec), "nothing escaped project root");

    // 项目名消毒：向导拒绝 ".." 形逃逸名（不创建任何目录）
    lemon::editor::ProjectDesc evil;
    evil.parentDir = root.string();
    evil.name = "../escaped-project";
    evil.engineVersion = "0";
    Expect(lemon::editor::ProjectWizard::Create(evil).empty(), "wizard rejects escaping name");
    Expect(!fs::exists(root.parent_path() / "escaped-project", ec), "no dir escaped parent");

    fs::remove_all(root, ec);
    fs::remove(outside, ec);
}
#endif // LEMON_EDITOR_CORE

// ---- M7a 批③：Play 装配下沉件单测 ----
// PrefabCache：低 32 索引建账 + Spawn 语义（树装载/pos 覆盖/team 覆盖/prefabId
// 回链/未命中 Null）+ scripts 缺席零挂载（槽保持 typeId=-1 不炸）

#ifdef LEMON_EDITOR_CORE
class TestPrefabSource final : public lemon::assets::PrefabSource {
public:
    std::vector<std::pair<uint64_t, std::string>> items;
    void EachPrefab(
        const std::function<bool(uint64_t guid, const std::string& absPath)>& fn) const override {
        for (const auto& [g, p] : items)
            if (!fn(g, p)) return;
    }
};
#endif // LEMON_EDITOR_CORE

#ifdef LEMON_EDITOR_CORE
void TestPrefabCachePlaySpawn() {
    using namespace lemon::ecs;
    namespace fs = std::filesystem;
    const std::string tag = std::to_string(lemon::CurrentProcessId());
    const fs::path root = fs::temp_directory_path() / ("lemon-test-prefabc-" + tag);
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);

    // 源 prefab：父子两实体（带 tag/Meta），SceneArchive 真源序列化
    std::string jsonA, jsonB;
    {
        World w;
        Scene& s = w.CreateScene("src");
        Entity p = s.Create();
        s.Emplace<Transform2D>(p, Transform2D{{1, 2}});
        Meta& mp = s.Emplace<Meta>(p);
        std::snprintf(mp.tag, sizeof(mp.tag), "Parent");
        Entity c = s.Create();
        s.Emplace<Transform2D>(c, Transform2D{{5, 6}});
        Expect(SceneSetParent(s, c, p), "source parent-child linked");
        jsonA = SceneArchive::SaveEntityTree(s, p);
        Entity solo = s.Create();
        s.Emplace<Transform2D>(solo, Transform2D{{9, 9}});
        Meta& ms = s.Emplace<Meta>(solo);
        std::snprintf(ms.tag, sizeof(ms.tag), "Solo");
        jsonB = SceneArchive::SaveEntityTree(s, solo);
        Expect(!jsonA.empty() && !jsonB.empty(), "prefab source serialized");
    }
    const uint64_t guidA = 0x1111222233334444ull, guidB = 0xaaaabbbbccccddddull;
    {
        fs::path fa = root / "a.prefab", fb = root / "b.prefab";
        {
            std::ofstream f(fa, std::ios::binary);
            f << jsonA;
        }
        {
            std::ofstream f(fb, std::ios::binary);
            f << jsonB;
        }
    }
    TestPrefabSource src;
    src.items.push_back({guidA, (root / "a.prefab").string()});
    src.items.push_back({guidB, (root / "b.prefab").string()});

    lemon::assets::PrefabCache cache;
    Expect(cache.Empty(), "cache starts empty");
    cache.Build(src);
    Expect(cache.Size() == 2, "two prefabs cached");

    World w;
    Scene& s = w.CreateScene("play");
    const uint32_t idA = (uint32_t)guidA, idB = (uint32_t)guidB;
    // Spawn：树装载 + root pos 覆盖 + prefabId 回链 + team 覆盖
    Entity rootE = cache.Spawn(s, idA, Vec2{100, 200}, 7);
    Expect(!rootE.IsNull() && s.Alive(rootE), "spawn lands entity tree");
    Expect(s.Get<Transform2D>(rootE).pos == Vec2(100, 200), "root pos overridden");
    Expect(s.Get<Meta>(rootE).prefabId == guidA, "prefabId backlink full guid");
    Expect(s.Get<Meta>(rootE).team == 7, "team overridden");
    const Hierarchy* h = s.TryGet<Hierarchy>(rootE);
    Expect(h && !h->firstChild.IsNull() && s.Alive(h->firstChild), "child in tree");
    Expect(s.Get<Transform2D>(h->firstChild).pos == Vec2(5, 6), "child keeps local pos");
    // 未命中：Null + 重复未命中不炸（告警去重内部态）
    Expect(cache.Spawn(s, 0xdeadbeef, Vec2{}, 0).IsNull(), "unknown id -> null");
    Expect(cache.Spawn(s, 0xdeadbeef, Vec2{}, 0).IsNull(), "repeat unknown still null");
    // 裸 InstantiateJson：无缓存直用（交互路径）
    Entity solo = lemon::assets::PrefabCache::InstantiateJson(s, jsonB, guidB, Vec2{-1, -2});
    Expect(!solo.IsNull() && s.Get<Transform2D>(solo).pos == Vec2(-1, -2) &&
               s.Get<Meta>(solo).prefabId == guidB,
           "InstantiateJson direct");
    // 空场景快照语义：Clear 后未命中
    cache.Clear();
    Expect(cache.Spawn(s, idB, Vec2{}, 0).IsNull(), "cleared cache -> null");
    // scripts 缺席（nullptr）路径 = Spawn 本就不解析；ResolveTreeScripts 无宿主
    // 不可测（ScriptHost 构造需 CLR）——编辑器/冒烟链覆盖
    (void)idB;
    fs::remove_all(root, ec);
}
#endif // LEMON_EDITOR_CORE

// ---- M7a 批① M21：manifest .bak 备份与坏主档恢复 ----
// 判别设计（阴性可分）：hero 与 aaa/bbb 同代发号（hero 非首号）→ 删 aaa/bbb 后
// 保存（gen2）→ 毒化主档（半截 JSON 模拟掉电）→ 重开。恢复成功 = hero 保住
// gen1 号；恢复失败/无机制 = 弃档重建按现存资产重排（hero 独活 = 拿首号 ≠ gen1）
// → id 不等即红。精灵资产（.png）才有真 spriteId（clip 型恒 0 不可判别）。

#ifdef LEMON_EDITOR_CORE
void TestManifestBakRecovery() {
    namespace fs = std::filesystem;
    using lemon::editor::AssetDatabase;
    using lemon::editor::AssetEntry;

    const fs::path root = fs::temp_directory_path() /
                          ("lemon-test-manifestbak-" + std::to_string(lemon::CurrentProcessId()));
    std::error_code ec;
    fs::remove_all(root, ec);

    auto put = [&](const std::string& rel, const char* guidHex) {
        const fs::path p = root / rel;
        fs::create_directories(p.parent_path(), ec);
        {
            std::ofstream f(p, std::ios::binary);
            f << "x";
        }
        std::ofstream f(p.string() + ".meta", std::ios::trunc);
        f << "{\"guid\":\"" << guidHex << "\"}";
    };
    put("Assets/aaa.png", "bbbb000000000001");
    put("Assets/bbb.png", "bbbb000000000002");
    put("Assets/hero.png", "bbbb000000000003");

    const std::string manifestPath = (root / ".lemon" / "manifest.json").string();
    uint32_t idGen1 = 0;
    {
        AssetDatabase db;
        Expect(db.OpenProject(root.string(), /*spriteIdBase=*/100), "manifest-bak: open gen1");
        const AssetEntry* h = db.FindByPath("Assets/hero.png");
        Expect(h && h->spriteId > 100, "manifest-bak: hero got non-first id in gen1");
        idGen1 = h ? h->spriteId : 0;
    }
    // 删两件 → gen2（记账保 hero 原号；此时 .bak = gen1 好档）
    fs::remove(root / "Assets" / "aaa.png", ec);
    fs::remove(root / "Assets" / "aaa.png.meta", ec);
    fs::remove(root / "Assets" / "bbb.png", ec);
    fs::remove(root / "Assets" / "bbb.png.meta", ec);
    {
        AssetDatabase db;
        Expect(db.OpenProject(root.string(), /*spriteIdBase=*/100), "manifest-bak: open gen2");
        const AssetEntry* h = db.FindByPath("Assets/hero.png");
        Expect(h && h->spriteId == idGen1, "manifest-bak: carry keeps hero id in gen2");
        Expect(fs::exists(manifestPath + ".bak", ec), "manifest-bak: .bak exists after gen2");
    }
    // 毒化主档：合法前缀 + 截断（掉电半写形态）
    {
        std::ofstream w(manifestPath, std::ios::binary | std::ios::trunc);
        w << "{\n  \"version\": 1,\n  \"nextSpriteId\": 999,\n  \"assets\": [\n    "
             "{\"guid\": 1, \"path\": \"Assets/he";
        w.close();
    }
    uint32_t idRecovered = 0;
    {
        AssetDatabase db2;
        Expect(db2.OpenProject(root.string(), /*spriteIdBase=*/100),
               "manifest-bak: reopen after corruption");
        if (const AssetEntry* h = db2.FindByPath("Assets/hero.png")) idRecovered = h->spriteId;
    }
    Expect(idRecovered == idGen1,
           "manifest-bak: corrupted main recovered to gen-1 sprite ids（M21；"
           "若走了重排 hero 独活拿首号必不等）");
    Expect(!fs::exists(manifestPath + ".tmp", ec), "manifest-bak: no tmp residue");

    fs::remove_all(root, ec);
}
#endif // LEMON_EDITOR_CORE

// ---- M7a 批②：AssetIndex 只读索引——manifest 快路径 vs 回退扫描双路一致性 ----
// 快路径 = 编辑器 AssetDatabase 建账落盘的 manifest 直读；回退 = 删 manifest
// （git clean -xfd 模拟，.bak 同删——兜底恢复路径归 TestManifestBakRecovery 族）
// 后 .meta 真源 + 路径序派生号。两路 guid→path 必须全等（.meta 随文件走）。

#ifdef LEMON_EDITOR_CORE
void TestAssetIndexConsistency() {
    namespace fs = std::filesystem;
    using lemon::assets::AssetIndex;
    using lemon::assets::AssetType;
    using lemon::editor::AssetDatabase;

    const fs::path root = fs::temp_directory_path() /
                          ("lemon-test-assetindex-" + std::to_string(lemon::CurrentProcessId()));
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "Assets", ec);
    fs::create_directories(root / "Prefabs", ec);
    // 夹具：整图/切片表两个 sprite + clip + prefab（meta 全部预设 guid——外部迁入形态）
    const uint64_t heroGuid = 0x1000000000000001ull, sheetGuid = 0x1000000000000002ull,
                   walkGuid = 0x1000000000000003ull, mobGuid = 0x1000000000000004ull;
    {
        std::ofstream f(root / "Assets" / "hero.png", std::ios::binary);
        f << "png";
    }
    {
        std::ofstream f(root / "Assets" / "hero.png.meta", std::ios::trunc);
        f << "{\"guid\":\"" << lemon::assets::GuidToHex(heroGuid) << "\",\"type\":\"sprite\"}";
    }
    {
        std::ofstream f(root / "Assets" / "sheet.png", std::ios::binary);
        f << "png";
    }
    {
        std::ofstream f(root / "Assets" / "sheet.png.meta", std::ios::trunc);
        f << "{\"guid\":\"" << lemon::assets::GuidToHex(sheetGuid)
          << "\",\"type\":\"sprite\",\"importer\":{\"slice\":\"grid\",\"cell\":[8,8],"
             "\"frames\":[2,2]}}";
    }
    {
        std::ofstream f(root / "Assets" / "walk.anim", std::ios::trunc);
        f << "{}";
    }
    {
        std::ofstream f(root / "Assets" / "walk.anim.meta", std::ios::trunc);
        f << "{\"guid\":\"" << lemon::assets::GuidToHex(walkGuid) << "\",\"type\":\"clip\"}";
    }
    {
        std::ofstream f(root / "Prefabs" / "mob.prefab", std::ios::trunc);
        f << "{}";
    }
    {
        std::ofstream f(root / "Prefabs" / "mob.prefab.meta", std::ios::trunc);
        f << "{\"guid\":\"" << lemon::assets::GuidToHex(mobGuid) << "\",\"type\":\"prefab\"}";
    }
    // M7a 批④：音频夹具（importer 段 loop/preload = AudioMount 装载消费面）
    const uint64_t hitGuid = 0x1000000000000005ull;
    {
        std::ofstream f(root / "Assets" / "hit.wav", std::ios::binary);
        f << "wav";
    }
    {
        std::ofstream f(root / "Assets" / "hit.wav.meta", std::ios::trunc);
        f << "{\"guid\":\"" << lemon::assets::GuidToHex(hitGuid)
          << "\",\"type\":\"audio\",\"importer\":{\"loop\":[1.5,10.0],\"preload\":true,"
            "\"retrigger\":0.12,\"voiceCap\":2,\"pitchJitter\":0.05}}";
    }

    // 编辑器建账（发号 + manifest 落盘；base=100 与 TestSpriteGuidResolve 同款）
    AssetDatabase db;
    Expect(db.OpenProject(root.string(), 100), "editor db opens fixture project");
    const lemon::editor::AssetEntry* dbHero = db.FindByPath("Assets/hero.png");
    const lemon::editor::AssetEntry* dbSheet = db.FindByPath("Assets/sheet.png");
    const lemon::editor::AssetEntry* dbWalk = db.FindByPath("Assets/walk.anim");
    const lemon::editor::AssetEntry* dbMob = db.FindByPath("Prefabs/mob.prefab");
    Expect(dbHero && dbSheet && dbWalk && dbMob, "db indexed all four assets");
    Expect(dbSheet->Sliced() && dbSheet->sliceCount == 4, "db allocated slice block");
    Expect(db.FindByGuid(walkGuid) == dbWalk, "db preset guids honored");

    // ---- 快路径：两路 guid→path/type/spriteId/slice 全等 ----
    AssetIndex idx;
    Expect(idx.Open(root.string(), 100) && idx.FromManifest(), "index opens via manifest");
    Expect(idx.Entries().size() == db.Entries().size(), "entry count equal both paths");
    for (const lemon::editor::AssetEntry& dbe : db.Entries()) {
        const lemon::assets::IndexedEntry* ie = idx.FindByPath(dbe.relPath);
        Expect(ie && ie->guid == dbe.guid && ie->type == dbe.type,
               "fast-path: guid/path/type equal");
        if (dbe.type == AssetType::Sprite) {
            Expect(ie->spriteId == dbe.spriteId && ie->sliceBase == dbe.sliceBase &&
                       ie->sliceCount == dbe.sliceCount,
                   "fast-path: spriteId/slice bookkeeping equal");
        }
    }
    Expect(idx.FindByGuid(mobGuid) == idx.FindByPath("Prefabs/mob.prefab"),
           "fast-path: guid lookup consistent");
    Expect(idx.FindByLowId(AssetType::Prefab, (uint32_t)mobGuid) != nullptr,
           "fast-path: low-32 prefab lookup");
    // M7a 批④：音频条目——类型串写入面（AssetTypeName Audio 分支勘误的锁）+
    // importer 字段（loop/preload 走 .meta 小 IO 读入）
    {
        std::ifstream mf(root / ".lemon" / "manifest.json", std::ios::binary);
        const std::string manifestText((std::istreambuf_iterator<char>(mf)),
                                       std::istreambuf_iterator<char>());
        Expect(manifestText.find("\"audio\"") != std::string::npos,
               "manifest writes 'audio' type string (AssetTypeName fix)");
        const lemon::assets::IndexedEntry* hit = idx.FindByPath("Assets/hit.wav");
        Expect(hit && hit->type == AssetType::Audio, "fast-path: audio entry typed");
        Expect(hit && hit->audioLoopStart == 1.5f && hit->audioLoopEnd == 10.0f &&
                   hit->audioPreload,
               "fast-path: audio importer fields from .meta");
        // M7c 批②：fx 三键同源读入（AudioMount → ClipFx 的运行时路）
        Expect(hit && hit->audioRetriggerCd == 0.12f && hit->audioVoiceCap == 2 &&
                   hit->audioPitchJitter == 0.05f,
               "fast-path: audio fx overrides from .meta");
        // generic 自愈：旧账期音频被记 "generic"（AssetTypeName 漏分支产物）——
        // 快路径按扩展名重派（否则 AudioMount 漏装全部音频）
        {
            std::string m = manifestText;
            const size_t pos = m.find("\"audio\"");
            Expect(pos != std::string::npos, "self-heal: audio marker found");
            if (pos != std::string::npos) {
                m.replace(pos, 7, "\"generic\"");
                {
                    std::ofstream of(root / ".lemon" / "manifest.json", std::ios::trunc);
                    of << m;
                }
                AssetIndex heal;
                Expect(heal.Open(root.string(), 100) && heal.FromManifest(),
                       "self-heal: reopen with generic-typed audio");
                const lemon::assets::IndexedEntry* h2 = heal.FindByPath("Assets/hit.wav");
                Expect(h2 && h2->type == AssetType::Audio,
                       "self-heal: generic re-derived from extension");
            }
        }
    }

    // ---- 回退：删 manifest（+.bak）→ .meta 真源扫描，派生号确定性 ----
    fs::remove(root / ".lemon" / "manifest.json", ec);
    fs::remove(root / ".lemon" / "manifest.json.bak", ec);
    AssetIndex idx2;
    Expect(idx2.Open(root.string(), 100) && !idx2.FromManifest(), "fallback scan engaged");
    // guid 全等（.meta 真源）+ 类型全等（扩展名判定单源）
    for (const lemon::editor::AssetEntry& dbe : db.Entries()) {
        const lemon::assets::IndexedEntry* ie = idx2.FindByPath(dbe.relPath);
        Expect(ie && ie->guid == dbe.guid && ie->type == dbe.type,
               "fallback: guid/path/type equal (meta is truth)");
    }
    { // M7a 批④：回退扫描的音频 importer 字段（与快路径同值——.meta 单源）
        const lemon::assets::IndexedEntry* hit2 = idx2.FindByPath("Assets/hit.wav");
        Expect(hit2 && hit2->type == AssetType::Audio && hit2->audioLoopStart == 1.5f &&
                   hit2->audioLoopEnd == 10.0f && hit2->audioPreload,
               "fallback: audio importer fields from .meta");
        // M7c 批②：fx 三键同值（单源）
        Expect(hit2 && hit2->audioRetriggerCd == 0.12f && hit2->audioVoiceCap == 2 &&
                   hit2->audioPitchJitter == 0.05f,
               "fallback: audio fx overrides from .meta");
    }
    // 路径序派生号：hero(路径序首 sprite)=100、sheet 本体=101 + 块 102..105
    const lemon::assets::IndexedEntry* hero2 = idx2.FindByPath("Assets/hero.png");
    const lemon::assets::IndexedEntry* sheet2 = idx2.FindByPath("Assets/sheet.png");
    Expect(hero2 && hero2->spriteId == 100, "fallback: path-order id derivation (hero=100)");
    Expect(sheet2 && sheet2->spriteId == 101 && sheet2->sliceBase == 102 &&
               sheet2->sliceCount == 4 && sheet2->cellW == 8 && sheet2->gridCols == 2,
           "fallback: slice block derived after body id");
    // id 数值与编辑器可不同（此处恰好同序）——确定性：再开一次同号
    AssetIndex idx3;
    Expect(idx3.Open(root.string(), 100), "reopen for determinism check");
    for (const lemon::assets::IndexedEntry& e : idx2.Entries()) {
        const lemon::assets::IndexedEntry* again = idx3.FindByPath(e.relPath);
        Expect(again && again->spriteId == e.spriteId && again->sliceBase == e.sliceBase,
               "fallback derivation deterministic across opens");
    }
    // 本体号/切片号查询面
    Expect(idx2.FindByWholeSpriteId(101) == sheet2, "whole-id lookup hits body only");
    Expect(idx2.FindByWholeSpriteId(103) == nullptr, "cell id is not a whole id");
    Expect(idx2.FindBySpriteId(103) == sheet2, "sprite-id lookup covers slice range");
    // 无 .meta 散文件不认（只读侧不发号）
    {
        std::ofstream f(root / "Assets" / "stray.png", std::ios::binary);
        f << "png";
    }
    AssetIndex idx4;
    Expect(idx4.Open(root.string(), 100) && idx4.FindByPath("Assets/stray.png") == nullptr,
           "stray file without .meta skipped (read-only: no id minting)");

    fs::remove_all(root, ec);
}
#endif // LEMON_EDITOR_CORE

// ---- M7a 批⑤：打包账 manifest.pkg.json（AssetIndex::ExportManifest → pkg 快路径）----

void TestAssetIndexPkgManifest() {
    namespace fs = std::filesystem;
    using lemon::assets::AssetIndex;
    using lemon::assets::AssetType;

    const fs::path root = fs::temp_directory_path() /
                          ("lemon-test-pkgmanifest-" + std::to_string(lemon::CurrentProcessId()));
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "Assets", ec);
    fs::create_directories(root / "Prefabs", ec);
    // 夹具：无编辑器账（.lemon 不存在）——回退扫描 → 导出 → pkg 快路径回读全等
    const uint64_t heroGuid = 0x2000000000000001ull, sheetGuid = 0x2000000000000002ull,
                   walkGuid = 0x2000000000000003ull, mobGuid = 0x2000000000000004ull;
    {
        std::ofstream f(root / "Assets" / "hero.png", std::ios::binary);
        f << "png";
    }
    {
        std::ofstream f(root / "Assets" / "hero.png.meta", std::ios::trunc);
        f << "{\"guid\":\"" << lemon::assets::GuidToHex(heroGuid) << "\",\"type\":\"sprite\"}";
    }
    {
        std::ofstream f(root / "Assets" / "sheet.png", std::ios::binary);
        f << "png";
    }
    {
        std::ofstream f(root / "Assets" / "sheet.png.meta", std::ios::trunc);
        f << "{\"guid\":\"" << lemon::assets::GuidToHex(sheetGuid)
          << "\",\"type\":\"sprite\",\"importer\":{\"slice\":\"grid\",\"cell\":[8,8],"
             "\"frames\":[2,2]}}";
    }
    {
        std::ofstream f(root / "Assets" / "walk.anim", std::ios::trunc);
        f << "{}";
    }
    {
        std::ofstream f(root / "Assets" / "walk.anim.meta", std::ios::trunc);
        f << "{\"guid\":\"" << lemon::assets::GuidToHex(walkGuid) << "\",\"type\":\"clip\"}";
    }
    {
        std::ofstream f(root / "Prefabs" / "mob.prefab", std::ios::trunc);
        f << "{}";
    }
    {
        std::ofstream f(root / "Prefabs" / "mob.prefab.meta", std::ios::trunc);
        f << "{\"guid\":\"" << lemon::assets::GuidToHex(mobGuid) << "\",\"type\":\"prefab\"}";
    }

    AssetIndex scan;
    Expect(scan.Open(root.string(), 2) && !scan.FromManifest(),
           "scan opens fallback (no manifest)");
    Expect(scan.ExportManifest((root / ".lemon" / "manifest.pkg.json").string()),
           "export pkg manifest");

    AssetIndex pkg;
    Expect(pkg.Open(root.string(), 2) && pkg.FromManifest(), "reopen hits pkg manifest fast path");
    Expect(pkg.Entries().size() == scan.Entries().size(), "pkg account entry count equal");
    for (const lemon::assets::IndexedEntry& e : scan.Entries()) {
        const lemon::assets::IndexedEntry* p = pkg.FindByPath(e.relPath);
        Expect(p && p->guid == e.guid && p->type == e.type,
               "pkg manifest: guid/path/type roundtrip");
        if (e.type == AssetType::Sprite)
            Expect(p && p->spriteId == e.spriteId && p->sliceBase == e.sliceBase &&
                       p->sliceCount == e.sliceCount,
                   "pkg manifest: spriteId/slice roundtrip");
    }
    const lemon::assets::IndexedEntry* sheet = pkg.FindByGuid(sheetGuid);
    Expect(sheet && sheet->Sliced() && sheet->SliceSpriteId(2) == sheet->sliceBase + 2,
           "pkg manifest: slice block usable (cell id contiguous)");
    // 编辑器账不干扰包账优先级：写入 manifest.json 后 pkg 账仍首查（包形态语义）
    {
        std::ofstream f(root / ".lemon" / "manifest.json", std::ios::trunc);
        f << "{\"assets\":[],\"nextSpriteId\":2}";
    }
    AssetIndex stillPkg;
    Expect(stillPkg.Open(root.string(), 2) && stillPkg.FromManifest() &&
               stillPkg.Entries().size() == scan.Entries().size(),
           "pkg manifest takes precedence over editor manifest");
    fs::remove_all(root, ec);
}

// ---- M7a 批⑥：LAT1 图集容器 v1（ADR-016 M5；writer/reader/装载登记核）----

void TestAssetIndexSliceRebase() {
    namespace fs = std::filesystem;
    using lemon::assets::AssetIndex;
    using lemon::assets::AssetType;

    // 验收热修 2026-10-05 的号域平移语义：packager 低基线记账（无编辑器账项目
    // fallback 自 base=2 发号）对运行时大基线（程序化页后）Open 时——本体重派、
    // 低域切片块**随本体连号重派**（几何真源 .meta 在场即登记链活）；健康块保号；
    // 越上界坏账块清零（原防御保留）
    const fs::path root = fs::temp_directory_path() /
                          ("lemon-test-slicerebase-" + std::to_string(lemon::CurrentProcessId()));
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "Assets", ec);
    fs::create_directories(root / ".lemon", ec);
    const uint64_t sheetGuid = 0x6000000000000001ull, wholeGuid = 0x6000000000000002ull;
    {
        std::ofstream f(root / "Assets" / "sheet.png", std::ios::binary);
        f << "png";
    }
    {
        std::ofstream f(root / "Assets" / "sheet.png.meta", std::ios::trunc);
        f << "{\"guid\":\"" << lemon::assets::GuidToHex(sheetGuid)
          << "\",\"type\":\"sprite\",\"importer\":{\"slice\":\"grid\",\"cell\":[8,4],"
             "\"frames\":[2,2]}}";
    }
    {
        std::ofstream f(root / "Assets" / "whole.png", std::ios::binary);
        f << "png";
    }
    {
        std::ofstream f(root / "Assets" / "whole.png.meta", std::ios::trunc);
        f << "{\"guid\":\"" << lemon::assets::GuidToHex(wholeGuid) << "\",\"type\":\"sprite\"}";
    }
    const auto WriteManifest = [&](const char* extra) {
        std::ofstream f(root / ".lemon" / "manifest.json", std::ios::trunc);
        f << "{\"assets\":["
          << "{\"path\":\"Assets/sheet.png\",\"guid\":" << sheetGuid
          << ",\"type\":\"sprite\",\"spriteId\":3,\"slice\":{\"base\":4,\"count\":4}},"
          << "{\"path\":\"Assets/whole.png\",\"guid\":" << wholeGuid
          << ",\"type\":\"sprite\",\"spriteId\":8}"
          << "],\"nextSpriteId\":9" << extra << "}";
    };

    { // 低域块随本体连号重派（Open base=100 → 全部记账低于基线）
        WriteManifest("");
        AssetIndex idx;
        Expect(idx.Open(root.string(), 100) && idx.FromManifest(), "rebase: manifest fast path");
        const lemon::assets::IndexedEntry* sh = idx.FindByGuid(sheetGuid);
        const lemon::assets::IndexedEntry* wh = idx.FindByGuid(wholeGuid);
        Expect(sh && wh, "rebase: entries present");
        if (sh && wh) {
            Expect(sh->spriteId >= 100 && wh->spriteId >= 100, "rebase: bodies reassigned >= base");
            Expect(sh->sliceCount == 4 && sh->sliceBase == sh->spriteId + 1,
                   "rebase: slice block follows body contiguously");
            Expect(sh->SliceSpriteId(3) == sh->sliceBase + 3, "rebase: cell ids contiguous");
            Expect(wh->spriteId >= sh->sliceBase + sh->sliceCount,
                   "rebase: no id collision between block and later body");
        }
    }
    { // 健康块保号：记账全在基线上域 → 不重排（原号原样）
        std::ofstream f(root / ".lemon" / "manifest.json", std::ios::trunc);
        f << "{\"assets\":["
          << "{\"path\":\"Assets/sheet.png\",\"guid\":" << sheetGuid
          << ",\"type\":\"sprite\",\"spriteId\":100,\"slice\":{\"base\":101,\"count\":4}},"
          << "{\"path\":\"Assets/whole.png\",\"guid\":" << wholeGuid
          << ",\"type\":\"sprite\",\"spriteId\":105}"
          << "],\"nextSpriteId\":106}";
        f.close(); // flush 落盘后再 Open（ofstream 存活期内缓冲未刷 = 读到空档）
        AssetIndex idx;
        Expect(idx.Open(root.string(), 100) && idx.FromManifest(), "healthy: manifest fast path");
        const lemon::assets::IndexedEntry* sh = idx.FindByGuid(sheetGuid);
        Expect(sh && sh->spriteId == 100 && sh->sliceBase == 101 && sh->sliceCount == 4,
               "healthy: slice block preserved as-is");
    }
    { // 越上界坏账块清零（manifest 不自洽防御保留）
        std::ofstream f(root / ".lemon" / "manifest.json", std::ios::trunc);
        f << "{\"assets\":["
          << "{\"path\":\"Assets/sheet.png\",\"guid\":" << sheetGuid
          << ",\"type\":\"sprite\",\"spriteId\":100,\"slice\":{\"base\":101,\"count\":10}},"
          << "{\"path\":\"Assets/whole.png\",\"guid\":" << wholeGuid
          << ",\"type\":\"sprite\",\"spriteId\":105}"
          << "],\"nextSpriteId\":106}";
        f.close();
        AssetIndex idx;
        Expect(idx.Open(root.string(), 100) && idx.FromManifest(), "badblock: manifest fast path");
        const lemon::assets::IndexedEntry* sh = idx.FindByGuid(sheetGuid);
        Expect(sh && sh->spriteId == 100 && sh->sliceCount == 0 && !sh->Sliced(),
               "badblock: over-ceiling block cleared to whole-sprite");
    }
    { // review 2026-10-05 回绕防线：sliceBase+count 精确回绕（0xFFFFFFF0+0x10=0）
        // 绕不过 sane 收口——不崩、块清零；spriteId 巨号（> sane 上限钳后的
        // idCeiling）同判坏账重派
        std::ofstream f(root / ".lemon" / "manifest.json", std::ios::trunc);
        f << "{\"assets\":["
          << "{\"path\":\"Assets/sheet.png\",\"guid\":" << sheetGuid
          << ",\"type\":\"sprite\",\"spriteId\":4294967295,\"slice\":{\"base\":4294967280,"
             "\"count\":16}},"
          << "{\"path\":\"Assets/whole.png\",\"guid\":" << wholeGuid
          << ",\"type\":\"sprite\",\"spriteId\":4294967295}"
          << "],\"nextSpriteId\":4294967295}";
        f.close();
        AssetIndex idx;
        Expect(idx.Open(root.string(), 100) && idx.FromManifest(),
               "wraparound: opens without crash/abort");
        const lemon::assets::IndexedEntry* sh = idx.FindByGuid(sheetGuid);
        const lemon::assets::IndexedEntry* wh = idx.FindByGuid(wholeGuid);
        Expect(sh && wh, "wraparound: entries present");
        if (sh && wh) {
            Expect(sh->spriteId >= 100 && sh->spriteId < (1u << 23),
                   "wraparound: giant spriteId bad account reassigned in sane range");
            Expect(sh->sliceCount == 0,
                   "wraparound: wrap-around slice block cleared (not giant-registered)");
            Expect(idx.FindBySpriteId(4294967290) == nullptr,
                   "wraparound: wrapped block range yields no lookup hit");
        }
    }
    fs::remove_all(root, ec);
}

// ---- M13（review 2026-10-09 b11b）：manifest sliceCount 与 .meta 网格对账 ----
// 陈旧 manifest（.bak 恢复 / pkg 快照 + 后续改大的 .meta）配新网格时，块号域
// 本身合法但数量错账——RegisterGridSlices 以 SliceSpriteId 越界回 0 触发保留号
// 断言 abort。对不上 = 清块转全幅（宁缺勿错；编辑器 Rescan 归位）。
void TestManifestSliceCountMismatch() {
    namespace fs = std::filesystem;
    using lemon::assets::AssetIndex;

    const fs::path root = fs::temp_directory_path() /
                          ("lemon-test-slicemismatch-" + std::to_string(lemon::CurrentProcessId()));
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "Assets", ec);
    fs::create_directories(root / ".lemon", ec);
    const uint64_t sheetGuid = 0x6100000000000001ull;
    {
        std::ofstream f(root / "Assets" / "sheet.png", std::ios::binary);
        f << "png";
    }
    // .meta 声明 4×4 网格（16 格）；manifest 记 8 = 陈旧账（曾为 2×4）
    {
        std::ofstream f(root / "Assets" / "sheet.png.meta", std::ios::trunc);
        f << "{\"guid\":\"" << lemon::assets::GuidToHex(sheetGuid)
          << "\",\"type\":\"sprite\",\"importer\":{\"slice\":\"grid\",\"cell\":[8,4],"
             "\"frames\":[4,4]}}";
    }
    {
        std::ofstream f(root / ".lemon" / "manifest.json", std::ios::trunc);
        f << "{\"assets\":["
          << "{\"path\":\"Assets/sheet.png\",\"guid\":" << sheetGuid
          << ",\"type\":\"sprite\",\"spriteId\":101,\"slice\":{\"base\":102,\"count\":8}}"
          << "],\"nextSpriteId\":120}";
    }
    AssetIndex idx;
    Expect(idx.Open(root.string(), 100) && idx.FromManifest(), "mismatch: manifest fast path");
    const lemon::assets::IndexedEntry* sh = idx.FindByGuid(sheetGuid);
    Expect(sh, "mismatch: entry present");
    if (sh) {
        Expect(sh->sliceCount == 0 && sh->sliceBase == 0,
               "mismatch: stale slice bookkeeping cleared to whole-image");
        Expect(sh->spriteId != 0, "mismatch: body id retained");
    }
    fs::remove_all(root, ec);
}

// ---- M15（review 2026-10-09 b11b）：字体 outline 色非抛解析 ----
// std::stoull 对非 hex 串抛 invalid_argument、超域抛 out_of_range，从 Open 全链
// 无捕获 → boot/packager 终止。修复 = ParseHexU32 整串消费，坏串保持默认色。
void TestFontImporterBadOutline() {
    namespace fs = std::filesystem;
    using lemon::assets::AssetIndex;

    const fs::path root = fs::temp_directory_path() /
                          ("lemon-test-fontbad-" + std::to_string(lemon::CurrentProcessId()));
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "Assets", ec);
    fs::create_directories(root / ".lemon", ec);
    const uint64_t fontGuid = 0x6200000000000001ull;
    {
        std::ofstream f(root / "Assets" / "body.ttf", std::ios::binary);
        f << "ttf";
    }
    // "zz" = stoull invalid_argument 形态（修复前测试进程自身 terminate）；
    // 姊妹字段同段解析正常 = 只坏色值、不殃及 charset/size/outlinePx
    {
        std::ofstream f(root / "Assets" / "body.ttf.meta", std::ios::trunc);
        f << "{\"guid\":\"" << lemon::assets::GuidToHex(fontGuid)
          << "\",\"type\":\"font\",\"importer\":{\"charset\":\"AB\",\"size\":32,"
             "\"outline\":[2,\"zz\"]}}";
    }
    {
        std::ofstream f(root / ".lemon" / "manifest.json", std::ios::trunc);
        f << "{\"assets\":["
          << "{\"path\":\"Assets/body.ttf\",\"guid\":" << fontGuid << ",\"type\":\"font\"}"
          << "]}";
    }
    AssetIndex idx; // 修复前：stoull("zz") 抛 → 测试进程自身 terminate（红得响亮）
    Expect(idx.Open(root.string(), 100), "font-bad: open survives bad outline color");
    const lemon::assets::IndexedEntry* fo = idx.FindByGuid(fontGuid);
    Expect(fo, "font-bad: entry present");
    if (fo) {
        Expect(fo->fontOutlineColor == 0xFF202020u,
               "font-bad: bad hex keeps default outline color");
        Expect(fo->fontPx == 32 && fo->fontOutlinePx == 2,
               "font-bad: sibling importer fields still parsed");
    }
    fs::remove_all(root, ec);
}

void TestBakedAtlasContainer() {
    namespace fs = std::filesystem;
    using namespace lemon::assets;

    const fs::path dir = fs::temp_directory_path() /
                         ("lemon-test-lat1-" + std::to_string(lemon::CurrentProcessId()));
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);

    BakedAtlasBuild b;
    b.pages = {{64, 32}, {16, 16}};
    BakedAtlasEntry e1{}, e2{};
    e1.guid = 0x3000000000000001ull, e1.page = 0, e1.x = 2, e1.y = 2, e1.w = 60, e1.h = 28;
    e2.guid = 0x3000000000000002ull, e2.page = 1, e2.w = 16, e2.h = 16;
    b.entries = {e1, e2};
    b.pagePixels.emplace_back(64 * 32 * 4, 0xAB);
    b.pagePixels.emplace_back(16 * 16 * 4, 0xCD);
    const fs::path p = dir / "atlas.baked";
    Expect(WriteBakedAtlasFile(p.string(), b), "LAT1 write ok");
    BakedAtlasBuild r;
    Expect(LoadBakedAtlasFile(p.string(), r), "LAT1 load ok");
    Expect(r.pages == b.pages && r.entries == b.entries && r.pagePixels == b.pagePixels,
           "LAT1 roundtrip fields+pixels equal");

    // 写侧自洽校验（review 2026-10-05：坏 build 拒写盘——"写盘成功但永不可载"
    // 的包在烤制期直白拒绝）：像素尺寸不符 / 页号越界 / 矩形越界 / 重复 guid
    const fs::path rejectPath = dir / "reject.baked";
    {
        BakedAtlasBuild bad = b;
        bad.pagePixels[0].pop_back(); // 像素载荷与页尺寸不符
        Expect(!WriteBakedAtlasFile(rejectPath.string(), bad),
               "LAT1 write rejects pixel/page size mismatch");
    }
    {
        BakedAtlasBuild bad = b;
        bad.entries[1].page = 2; // 页号越界
        Expect(!WriteBakedAtlasFile(rejectPath.string(), bad), "LAT1 write rejects page oob");
    }
    {
        BakedAtlasBuild bad = b;
        bad.entries[0].w = 63; // 2+63 > 页宽 64
        Expect(!WriteBakedAtlasFile(rejectPath.string(), bad), "LAT1 write rejects rect oob");
    }
    {
        BakedAtlasBuild bad = b;
        bad.entries[1].guid = bad.entries[0].guid; // 重复 guid
        Expect(!WriteBakedAtlasFile(rejectPath.string(), bad), "LAT1 write rejects dup guid");
    }
    Expect(!fs::exists(rejectPath, ec), "LAT1 rejected builds leave no file");

    // 篡改/截断阴性面：单字节改 → 拒载（拒载原因面 = 魔数/版本/头长/计数域/
    // 尺寸域/payloadBytes 对账/条目界内/guid 唯一）
    std::vector<uint8_t> raw;
    {
        std::ifstream f(p, std::ios::binary);
        raw.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    }
    const auto RawWrite = [&](const std::vector<uint8_t>& bytes) {
        std::ofstream f(dir / "tampered.baked", std::ios::binary | std::ios::trunc);
        f.write((const char*)bytes.data(), (std::streamsize)bytes.size());
    };
    const auto TamperAt = [&](size_t off, uint8_t v, const char* what) {
        std::vector<uint8_t> t = raw;
        t[off] = v;
        RawWrite(t);
        BakedAtlasBuild rb;
        Expect(!LoadBakedAtlasFile((dir / "tampered.baked").string(), rb), what);
    };
    TamperAt(0, 'X', "LAT1 bad magic rejected");
    TamperAt(4, 2, "LAT1 bad version rejected");
    TamperAt(6, 24, "LAT1 bad headerSize rejected");
    TamperAt(28, uint8_t(raw[28] ^ 0xFF), "LAT1 payloadBytes mismatch rejected");
    TamperAt(20, uint8_t(raw[20] + 1), "LAT1 entryCount mismatch rejected");
    TamperAt(32, 0, "LAT1 zero page dim rejected");
    { // 条目矩形越界：entry0.x 2 → 60（60+60 > 页宽 64）
        std::vector<uint8_t> t = raw;
        t[32 + 2 * 8 /*pageDims*/ + 10 /*entry0.x*/] = 60;
        RawWrite(t);
        BakedAtlasBuild rb;
        Expect(!LoadBakedAtlasFile((dir / "tampered.baked").string(), rb),
               "LAT1 entry rect out of page rejected");
    }
    { // guid 重复：entry1.guid := entry0.guid
        std::vector<uint8_t> t = raw;
        const size_t e1off = 32 + 2 * 8 + 1 * 18;
        std::memcpy(&t[e1off], &raw[32 + 2 * 8], 8);
        RawWrite(t);
        BakedAtlasBuild rb;
        Expect(!LoadBakedAtlasFile((dir / "tampered.baked").string(), rb),
               "LAT1 duplicate guid rejected");
    }
    { // 截断
        RawWrite({raw.begin(), raw.end() - 10});
        BakedAtlasBuild rb;
        Expect(!LoadBakedAtlasFile((dir / "tampered.baked").string(), rb),
               "LAT1 truncated payload rejected");
    }
    { // 尾部多出
        std::vector<uint8_t> t = raw;
        t.insert(t.end(), {1, 2, 3});
        RawWrite(t);
        BakedAtlasBuild rb;
        Expect(!LoadBakedAtlasFile((dir / "tampered.baked").string(), rb),
               "LAT1 trailing bytes rejected");
    }
    { // 零条目
        std::vector<uint8_t> t = raw;
        t[20] = t[21] = t[22] = t[23] = 0; // entryCount = 0
        RawWrite(t);
        BakedAtlasBuild rb;
        Expect(!LoadBakedAtlasFile((dir / "tampered.baked").string(), rb),
               "LAT1 zero entries rejected");
    }
    fs::remove_all(dir, ec);
}

void TestAtlasBakePack() {
    using namespace lemon::assets;
    const auto MakeImage = [](uint64_t guid, uint32_t w, uint32_t h, uint8_t fill) {
        BakedAtlasImage img;
        img.guid = guid;
        img.w = w;
        img.h = h;
        img.rgba.assign(size_t(w) * h * 4, 0);
        for (size_t i = 0; i < img.rgba.size(); i += 4) {
            img.rgba[i] = fill;
            img.rgba[i + 3] = 0xFF;
        }
        return img;
    };
    const uint64_t gBig = 0x3100000000000001ull, gA = 0x3100000000000002ull,
                   gB = 0x3100000000000003ull, gC = 0x3100000000000004ull,
                   gD = 0x3100000000000005ull;
    std::vector<BakedAtlasImage> images;
    images.push_back(MakeImage(gBig, 5000, 8, 1)); // 超虚拟页宽 → 专属页
    images.push_back(MakeImage(gA, 40, 30, 2));
    images.push_back(MakeImage(gB, 20, 30, 3));
    images.push_back(MakeImage(gC, 10, 10, 4));
    images.push_back(MakeImage(gD, 100, 5, 5));
    BakedAtlasBuild a, b;
    std::string err;
    Expect(PackAtlasPages(images, a, &err), "pack ok");
    Expect(PackAtlasPages(images, b) && a.pages == b.pages && a.entries == b.entries &&
               a.pagePixels == b.pagePixels,
           "pack deterministic (byte equal rerun)");

    // 布局断言：专属页独占 + 普通页 gutter 边距 + 矩形界内 + 互不重叠 + 像素对位
    Expect(a.pages.size() == 3, "oversized gets dedicated page (3 pages)");
    const BakedAtlasEntry* big = nullptr;
    for (const BakedAtlasEntry& e : a.entries)
        if (e.guid == gBig) big = &e;
    Expect(big && big->x == 0 && big->y == 0 && big->w == 5000 && big->h == 8,
           "oversized entry at origin full size");
    Expect(big && a.pages[big->page].w == 5000 && a.pages[big->page].h == 8,
           "dedicated page sized to sprite");
    const BakedAtlasEntry* d = nullptr;
    for (const BakedAtlasEntry& e : a.entries)
        if (e.guid == gD) d = &e;
    Expect(d && d->page != big->page && d->x >= kAtlasGutter && d->y >= kAtlasGutter,
           "normal entry keeps gutter margins (dedicated page closed)");
    for (size_t i = 0; i < a.entries.size(); ++i) {
        const BakedAtlasEntry& e = a.entries[i];
        const BakedAtlasPage& pg = a.pages[e.page];
        Expect(uint32_t(e.x) + e.w <= pg.w && uint32_t(e.y) + e.h <= pg.h,
               "entry rect within page");
        for (size_t j = i + 1; j < a.entries.size(); ++j) { // 同页不重叠
            const BakedAtlasEntry& o = a.entries[j];
            if (o.page != e.page) continue;
            const bool overlap =
                uint32_t(e.x) < uint32_t(o.x) + o.w && uint32_t(o.x) < uint32_t(e.x) + e.w &&
                uint32_t(e.y) < uint32_t(o.y) + o.h && uint32_t(o.y) < uint32_t(e.y) + e.h;
            Expect(!overlap, "same-page entries do not overlap");
        }
        // 像素对位：页面上精灵矩形逐字节 = 源图（合成正确性）
        const BakedAtlasImage* src = nullptr;
        for (const BakedAtlasImage& im : images)
            if (im.guid == e.guid) src = &im;
        Expect(src != nullptr, "entry maps to source image");
        if (src) {
            bool equal = true;
            for (uint32_t row = 0; row < e.h && equal; ++row) {
                const uint8_t* pageRow =
                    &a.pagePixels[e.page][(size_t(e.y + row) * pg.w + e.x) * 4];
                const uint8_t* srcRow = &src->rgba[size_t(row) * src->w * 4];
                equal = std::memcmp(pageRow, srcRow, size_t(e.w) * 4) == 0;
            }
            Expect(equal, "page pixels match source at entry rect");
        }
    }

    // 分页溢出：5 张 2040²（4 张恰满一页，第 5 张开新页——4096 虚拟域算术）
    {
        std::vector<BakedAtlasImage> big5;
        for (uint64_t g = 1; g <= 5; ++g)
            big5.push_back(MakeImage(0x3200000000000000ull + g, 2040, 2040, uint8_t(g)));
        BakedAtlasBuild bb;
        Expect(PackAtlasPages(big5, bb) && bb.pages.size() == 2, "2040x2040 x5 spills to 2 pages");
    }

    // 阴性：零尺寸/超 GPU 域/载荷不符/空输入
    std::string msg;
    BakedAtlasBuild junk;
    Expect(!PackAtlasPages({}, junk, &msg), "empty pack rejected");
    Expect(!PackAtlasPages({MakeImage(1, 0, 4, 0)}, junk, &msg), "zero-size sprite rejected");
    Expect(!PackAtlasPages({MakeImage(1, 17000, 4, 0)}, junk, &msg), "over-GPU-dim rejected");
    {
        BakedAtlasImage bad = MakeImage(1, 4, 4, 0);
        bad.rgba.pop_back();
        Expect(!PackAtlasPages({bad}, junk, &msg), "pixel payload size mismatch rejected");
    }
}

void TestAtlasStoreRegister() {
    namespace fs = std::filesystem;
    using namespace lemon::assets;

    const fs::path root = fs::temp_directory_path() /
                          ("lemon-test-lat1reg-" + std::to_string(lemon::CurrentProcessId()));
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "Assets", ec);
    const uint64_t sheetGuid = 0x4000000000000001ull, wholeGuid = 0x4000000000000002ull;
    // sheet.png 32×8（网格 4×2 格 cell 8×4 恰满图面）+ whole.png 10×6
    const auto FillPattern = [](std::vector<uint8_t>& px, uint8_t seed) {
        for (size_t i = 0; i < px.size(); i += 4) {
            px[i] = uint8_t(seed + (i / 4) % 251);
            px[i + 1] = uint8_t((seed * 7 + i) % 253);
            px[i + 2] = uint8_t(seed ^ uint8_t(i));
            px[i + 3] = 0xFF;
        }
    };
    std::vector<uint8_t> sheetPx(32 * 8 * 4), wholePx(10 * 6 * 4);
    FillPattern(sheetPx, 11);
    FillPattern(wholePx, 22);
    Expect(stbi_write_png((root / "Assets" / "sheet.png").string().c_str(), 32, 8, 4,
                          sheetPx.data(), 32 * 4) != 0,
           "seed sheet.png");
    Expect(stbi_write_png((root / "Assets" / "whole.png").string().c_str(), 10, 6, 4,
                          wholePx.data(), 10 * 4) != 0,
           "seed whole.png");
    {
        std::ofstream f(root / "Assets" / "sheet.png.meta", std::ios::trunc);
        f << "{\"guid\":\"" << GuidToHex(sheetGuid)
          << "\",\"type\":\"sprite\","
             "\"importer\":{\"slice\":\"grid\",\"cell\":[8,4],\"frames\":[4,2]}}";
    }
    {
        std::ofstream f(root / "Assets" / "whole.png.meta", std::ios::trunc);
        f << "{\"guid\":\"" << GuidToHex(wholeGuid) << "\",\"type\":\"sprite\"}";
    }

    AssetIndex index;
    Expect(index.Open(root.string(), 2), "fixture index opens");
    const IndexedEntry* se = index.FindByGuid(sheetGuid);
    const IndexedEntry* we = index.FindByGuid(wholeGuid);
    Expect(se && we && se->Sliced() && se->sliceCount == 8, "sheet sliced 4x2");
    Expect(se->spriteId != 0 && we->spriteId != 0, "sprite ids assigned");

    // LAT1 build：同像素装箱（读回端到端不引 RHI——登记核纯面）
    BakedAtlasImage im1, im2;
    im1.guid = sheetGuid, im1.w = 32, im1.h = 8, im1.rgba = sheetPx;
    im2.guid = wholeGuid, im2.w = 10, im2.h = 6, im2.rgba = wholePx;
    BakedAtlasBuild build;
    Expect(PackAtlasPages({im1, im2}, build), "fixture pack ok");

    renderer::AtlasRegistry atlas;
    for (size_t i = 0; i < build.pages.size(); ++i)
        atlas.RegisterAtlas(uint32_t(2 + i), {}, build.pages[i].w, build.pages[i].h);
    uint32_t reg = 0;
    Expect(RegisterAtlasSprites(atlas, index, build, 2, reg) && reg == 2,
           "register ok (2 whole sprites)");
    for (const BakedAtlasEntry& ent : build.entries) {
        const IndexedEntry* e = index.FindByGuid(ent.guid);
        Expect(e != nullptr, "entry guid in index");
        if (!e) continue;
        const renderer::SpriteInfo& si = atlas.GetSprite(e->spriteId);
        const BakedAtlasPage& pg = build.pages[ent.page];
        Expect(si.atlasIndex == 2 + ent.page && si.widthPx == ent.w && si.heightPx == ent.h,
               "whole sprite registered at manifest id");
        ExpectNear(si.u0, float(ent.x) / float(pg.w), 1e-6f, "whole u0 math");
        ExpectNear(si.v1, float(ent.y + ent.h) / float(pg.h), 1e-6f, "whole v1 math");
        if (ent.guid == sheetGuid) { // 切片子矩形：cell → sliceBase + 行优先号
            for (uint32_t cell = 0; cell < 8; ++cell) {
                const uint32_t id = se->SliceSpriteId(cell);
                const renderer::SpriteInfo& s = atlas.GetSprite(id);
                const uint32_t cx = cell % 4, cy = cell / 4;
                Expect(s.atlasIndex == 2 + ent.page && s.widthPx == 8 && s.heightPx == 4,
                       "slice sprite size");
                ExpectNear(s.u0, float(ent.x + cx * 8) / float(pg.w), 1e-6f, "slice u0 math");
                ExpectNear(s.v0, float(ent.y + cy * 4) / float(pg.h), 1e-6f, "slice v0 math");
            }
        }
    }

    // 阴性：LAT1 条目与索引失配（缺 whole = 包与账不一致）→ 拒绝登记
    {
        BakedAtlasBuild partial;
        Expect(PackAtlasPages({im1}, partial), "partial pack ok");
        renderer::AtlasRegistry at2;
        at2.RegisterAtlas(2, {}, partial.pages[0].w, partial.pages[0].h);
        uint32_t r2 = 0;
        Expect(!RegisterAtlasSprites(at2, index, partial, 2, r2),
               "missing entry vs index rejected (package/ledger mismatch)");
    }
    { // 阴性：未知 guid 多一条
        BakedAtlasBuild ghost = build;
        BakedAtlasImage imG = im2;
        imG.guid = 0x4000000000000099ull;
        Expect(PackAtlasPages({im1, im2, imG}, ghost), "ghost pack ok");
        renderer::AtlasRegistry at3;
        for (size_t i = 0; i < ghost.pages.size(); ++i)
            at3.RegisterAtlas(uint32_t(2 + i), {}, ghost.pages[i].w, ghost.pages[i].h);
        uint32_t r3 = 0;
        Expect(!RegisterAtlasSprites(at3, index, ghost, 2, r3), "unknown guid rejected");
    }
    fs::remove_all(root, ec);
}

void TestBakeProjectAtlas() {
    namespace fs = std::filesystem;
    using namespace lemon::assets;

    const fs::path root = fs::temp_directory_path() /
                          ("lemon-test-lat1bake-" + std::to_string(lemon::CurrentProcessId()));
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "Assets", ec);
    const uint64_t oneGuid = 0x5000000000000001ull;
    std::vector<uint8_t> px(13 * 7 * 4);
    for (size_t i = 0; i < px.size(); i += 4) {
        px[i] = uint8_t(i % 199);
        px[i + 1] = uint8_t((i * 3) % 197);
        px[i + 2] = uint8_t(i % 251);
        px[i + 3] = 0xFF;
    }
    Expect(stbi_write_png((root / "Assets" / "one.png").string().c_str(), 13, 7, 4, px.data(),
                          13 * 4) != 0,
           "seed one.png");
    {
        std::ofstream f(root / "Assets" / "one.png.meta", std::ios::trunc);
        f << "{\"guid\":\"" << GuidToHex(oneGuid) << "\",\"type\":\"sprite\"}";
    }

    AssetIndex index;
    Expect(index.Open(root.string(), 2), "bake fixture index opens");
    AtlasBakeStats st;
    const fs::path dst = root / ".lemon" / "baked" / "atlas" / "atlas.baked";
    Expect(BakeProjectAtlas(index, dst.string(), st) && st.sprites == 1 && st.pages == 1,
           "bake project atlas (1 sprite 1 page)");
    BakedAtlasBuild rb;
    Expect(LoadBakedAtlasFile(dst.string(), rb) && rb.entries.size() == 1, "baked file loads back");
    const BakedAtlasEntry& e = rb.entries[0];
    Expect(e.guid == oneGuid && e.w == 13 && e.h == 7, "baked entry geometry");
    bool equal = true;
    for (uint32_t row = 0; row < 7 && equal; ++row)
        equal = std::memcmp(&rb.pagePixels[0][(size_t(e.y + row) * rb.pages[0].w + e.x) * 4],
                            &px[size_t(row) * 13 * 4], 13 * 4) == 0;
    Expect(equal, "baked page pixels match png source");

    // 无 sprite 项目：false 且 sprites==0（合法跳过形态，非错误）
    {
        const fs::path empty = fs::temp_directory_path() /
                               ("lemon-test-lat1none-" + std::to_string(lemon::CurrentProcessId()));
        fs::remove_all(empty, ec);
        fs::create_directories(empty / "Assets", ec);
        {
            std::ofstream f(empty / "Assets" / "walk.anim", std::ios::trunc);
            f << "{}";
        }
        {
            std::ofstream f(empty / "Assets" / "walk.anim.meta", std::ios::trunc);
            f << "{\"guid\":\"" << GuidToHex(0x5000000000000002ull) << "\",\"type\":\"clip\"}";
        }
        AssetIndex ei;
        Expect(ei.Open(empty.string(), 2), "spriteless index opens");
        AtlasBakeStats es;
        Expect(!BakeProjectAtlas(ei, (empty / "a.baked").string(), es) && es.sprites == 0,
               "spriteless project skips atlas (false + zero sprites)");
        fs::remove_all(empty, ec);
    }
    fs::remove_all(root, ec);
}

} // namespace

// 批⑪ H2（review 2026-10-09 #H2）：BuildClipCache 坏档红字跳过——弱解析器时代坏
// 字段类型（"loop":1 / "fps":"8" / sheet 数字）抛 nlohmann type_error 穿透 =
// std::terminate，违反「坏 clip 红字跳过不炸 Play」契约（PlayCaches.h）；改调
// AnimAsset::ParseClipJson 后坏 clip 全部跳过、好 clip 照常登记。本用例跑通即
// 主断言（terminate 会让进程直接死）。
class FakeClipSource final : public lemon::assets::PlayCacheSource {
public:
    struct Item {
        uint64_t guid;
        std::string rel, abs;
    };
    std::vector<Item> clips;
    std::vector<lemon::assets::IndexedEntry> sprites; // guid→条目（FindSprite 线性小表）
    void Each(lemon::assets::AssetType type,
              const std::function<void(uint64_t, const std::string&, const std::string&)>& fn)
        const override {
        if (type != lemon::assets::AssetType::Clip) return;
        for (const Item& it : clips) fn(it.guid, it.rel, it.abs);
    }
    const lemon::assets::IndexedEntry* FindSprite(uint64_t guid) const override {
        for (const auto& e : sprites)
            if (e.guid == guid) return &e;
        return nullptr;
    }
    bool HasClip(uint64_t) const override { return true; }
};

void TestClipCacheBadArchive() {
    namespace fs = std::filesystem;
    using lemon::ecs::World;
    const fs::path root = fs::temp_directory_path() /
                          ("lemon-test-clipcache-" + std::to_string(lemon::CurrentProcessId()));
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);

    const std::string sheet = "0011223344556677";
    auto writeFile = [&](const char* name, const std::string& body) {
        std::ofstream f(root / name, std::ios::trunc);
        f << body;
        return (root / name).string();
    };
    const std::string good =
        writeFile("good.anim", "{\"fps\":8,\"loop\":true,\"frames\":[{\"sheet\":\"" + sheet +
                                   "\",\"cell\":0}],\"events\":[{\"frame\":0,\"id\":5}]}");
    const std::string badLoop = writeFile(
        "bad_loop.anim", "{\"fps\":8,\"loop\":1,\"frames\":[{\"sheet\":\"" + sheet + "\",\"cell\":0}]}");
    const std::string badFps = writeFile(
        "bad_fps.anim", "{\"fps\":\"8\",\"loop\":true,\"frames\":[{\"sheet\":\"" + sheet + "\",\"cell\":0}]}");
    const std::string badSheet =
        writeFile("bad_sheet.anim", "{\"fps\":8,\"frames\":[{\"sheet\":42,\"cell\":0}]}");

    // b11a review F4：切片面补钉——sliced sheet（2×2 切片 sliceBase=100）cell 2 连号
    // 解析 + 越界 cell 悬空拒 clip 两条分支
    const std::string sheetSliced = "8899aabbccddeeff";
    const std::string goodSliced = writeFile(
        "good_sliced.anim",
        "{\"fps\":12,\"frames\":[{\"sheet\":\"" + sheetSliced + "\",\"cell\":2}]}");
    const std::string danglingCell = writeFile(
        "dangling.anim",
        "{\"fps\":12,\"frames\":[{\"sheet\":\"" + sheetSliced + "\",\"cell\":9}]}");

    FakeClipSource src;
    src.clips = {{0xAAAA0001ull, "good.anim", good},
                 {0xAAAA0002ull, "bad_loop.anim", badLoop},
                 {0xAAAA0003ull, "bad_fps.anim", badFps},
                 {0xAAAA0004ull, "bad_sheet.anim", badSheet},
                 {0xAAAA0005ull, "good_sliced.anim", goodSliced},
                 {0xAAAA0006ull, "dangling.anim", danglingCell}};
    lemon::assets::IndexedEntry whole;
    whole.guid = 0x0011223344556677ull;
    whole.type = lemon::assets::AssetType::Sprite;
    whole.spriteId = 7; // 未切片整图：cell 0 = 本体号
    lemon::assets::IndexedEntry sliced;
    sliced.guid = 0x8899aabbccddeeffull;
    sliced.type = lemon::assets::AssetType::Sprite;
    sliced.gridCols = 2;
    sliced.gridRows = 2;
    sliced.sliceBase = 100;
    sliced.sliceCount = 4; // Sliced()=true：cell 界内连号 sliceBase+cell
    src.sprites = {whole, sliced};

    World w;
    lemon::assets::BuildClipCache(w, src); // 坏档红字跳过不炸 = 主断言

    const auto* g = w.Clips().Find((uint32_t)0xAAAA0001ull);
    Expect(g != nullptr && g->frames.size() == 1 && g->frames[0] == 7,
           "good clip registered with resolved spriteId");
    Expect(g != nullptr && g->loop && g->events.size() == 1 && g->events[0].id == 5,
           "good clip loop + frame event carried");
    const auto* gs = w.Clips().Find((uint32_t)0xAAAA0005ull);
    Expect(gs != nullptr && gs->frames.size() == 1 && gs->frames[0] == 102,
           "sliced clip resolves to sliceBase+cell");
    Expect(w.Clips().Find((uint32_t)0xAAAA0006ull) == nullptr,
           "out-of-range cell dangling frame rejects clip");
    Expect(w.Clips().Find((uint32_t)0xAAAA0002ull) == nullptr, "bad 'loop':1 skipped not terminate");
    Expect(w.Clips().Find((uint32_t)0xAAAA0003ull) == nullptr, "bad 'fps':string skipped");
    Expect(w.Clips().Find((uint32_t)0xAAAA0004ull) == nullptr, "bad sheet:number skipped");

    fs::remove_all(root, ec);
}

void RunAssetsTests() {
#ifdef LEMON_EDITOR_CORE
    TestProjectFile();
#endif
#ifdef LEMON_EDITOR_CORE
    TestSpriteRefsEngine();
#endif
#ifdef LEMON_EDITOR_CORE
    TestAtlasPageHotUpdate();
#endif
#ifdef LEMON_EDITOR_CORE
    TestAssetDatabaseLow32Collision();
#endif
#ifdef LEMON_EDITOR_CORE
    TestOrphanMetaSweep();
#endif
#ifdef LEMON_EDITOR_CORE
    TestAssetDatabaseLifecycle();
#endif
#ifdef LEMON_EDITOR_CORE
    TestGridSliceConfig();
    TestAudioImporterConfig();
#endif
#ifdef LEMON_EDITOR_CORE
    TestAssetPathContainment();
#endif
#ifdef LEMON_EDITOR_CORE
    TestPrefabCachePlaySpawn();
#endif
#ifdef LEMON_EDITOR_CORE
    TestManifestBakRecovery();
#endif
#ifdef LEMON_EDITOR_CORE
    TestAssetIndexConsistency();
#endif
#ifdef LEMON_EDITOR_CORE
    TestAssetIndexPkgManifest();
#endif
#ifdef LEMON_EDITOR_CORE
    TestAssetIndexSliceRebase();
#endif
#ifdef LEMON_EDITOR_CORE
    TestManifestSliceCountMismatch(); // 批⑪ M13：manifest 切片账对账（b11b）
#endif
#ifdef LEMON_EDITOR_CORE
    TestFontImporterBadOutline(); // 批⑪ M15：outline 色非抛解析（b11b）
#endif
#ifdef LEMON_EDITOR_CORE
    TestBakedAtlasContainer();
#endif
#ifdef LEMON_EDITOR_CORE
    TestAtlasBakePack();
#endif
#ifdef LEMON_EDITOR_CORE
    TestAtlasStoreRegister();
#endif
#ifdef LEMON_EDITOR_CORE
    TestBakeProjectAtlas();
#endif
#ifdef LEMON_EDITOR_CORE
    TestClipCacheBadArchive(); // 批⑪ H2：坏 clip 红字跳过不炸 Play
#endif
}
