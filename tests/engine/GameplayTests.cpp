// Lemon 引擎单测 — GameplayTests —
// 玩法域（AI/动画/补间/Fx/成长/击退/弹幕/波次导演/时序死亡语义）（M7c 批⓪ T2 自 engine_tests.cpp
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

// iFrames 递减与多段击杀（M5 批⓪ T1；DevLog 2026-09-22 P0 回归）：
// 受击置窗 → 窗内免疫（在途弹压着重叠也不重复伤害）→ 窗尽复拍 → 第三击致死。
// 复拍间隔 = ceil(iframeWindow/dt) tick（60Hz/0.1s 窗 ≈ 6 tick；FP 余量按 5..8 带断言）。

void TestVerifyIframesDecrementAndKill() {
    World world;
    Scene& s = world.CreateScene("ifr");
    world.SetActiveScene(&s);

    Entity shooter = s.Create(); // team0 射手（弹体势力继承口径）
    s.Emplace<Transform2D>(shooter, Transform2D{{0, 0}});
    s.Emplace<Meta>(shooter).team = 0;

    Entity victim = s.Create(); // team1 受害者：hp 30 / 弹伤 12 → 需 3 次命中
    s.Emplace<Transform2D>(victim, Transform2D{{50, 0}});
    s.Emplace<Meta>(victim).team = 1;
    s.Emplace<Health>(victim, Health{.max = 30.0f, .cur = 30.0f});

    auto fire = [&]() { // 在受害者处生成一发命中即毁的弹（pierce 0 = 默认）
        Entity p = s.Create();
        s.Emplace<Transform2D>(p, Transform2D{{50, 0}});
        s.Emplace<Meta>(p).team = 0;
        s.Emplace<Velocity>(p);
        Projectile& pr = s.Emplace<Projectile>(p);
        pr.damage = 12.0f;
        pr.lifetime = 30.0f;
    };

    int hits = 0, deaths = 0;
    world.SetEventSink([&](World&, const EventPacket& p) {
        if (p.type == GameEvent::Hit && p.dst == victim) ++hits;
        if (p.type == GameEvent::Death && p.src == victim) ++deaths;
    });

    // 命中链最小管线：哈希重建(#8) → 命中(#9) → 数值(#11 递减) → 派发 → 提交
    world.Pipeline().AddSystem(std::make_unique<SpatialHashRebuildSystem>());
    world.Pipeline().AddSystem(std::make_unique<HitboxSystem>());
    world.Pipeline().AddSystem(std::make_unique<StatSystem>());
    world.Pipeline().AddSystem(std::make_unique<ScriptEventDispatchSystem>());
    world.Pipeline().AddSystem(std::make_unique<DestroyCommitSystem>());
    world.Pipeline().ResolveOrder();

    const float dt = 1.0f / 60.0f;

    fire(); // tick0：第 1 击（18/30）+ 置窗
    world.Step(dt);
    Expect(hits == 1 && deaths == 0, "first hit lands");
    Expect(s.Get<Health>(victim).cur == 18.0f, "cur after hit 1");
    Expect(s.Get<Health>(victim).iFrames > 0.0f, "iFrames window armed");

    fire(); // 在途弹 B 压着重叠 4 tick：窗内必须免疫（修复前恒免疫、修复后也不得过窗）
    for (int i = 0; i < 4; ++i) world.Step(dt);
    Expect(hits == 1, "immune within window (4 ticks)");
    Expect(s.Get<Health>(victim).cur == 18.0f, "no damage within window");

    bool relanded = false; // 窗尽（≈6 tick，容差 ≤4 步）：B 补上第 2 击
    for (int i = 0; i < 4 && !relanded; ++i) {
        world.Step(dt);
        relanded = hits == 2;
    }
    Expect(relanded, "re-hit after window expiry");
    Expect(s.Get<Health>(victim).cur == 6.0f, "cur after hit 2");

    fire(); // 第 3 击：窗尽后致死（hp<=0 早退防第 4 击）
    for (int i = 0; i < 8 && deaths == 0; ++i) world.Step(dt);
    world.Step(dt); // DestroyCommit 在 Essential 阶段（下一 tick 首）提交本 tick 销毁
    Expect(deaths == 1, "killed by multi-hit damage");
    Expect(hits == 3, "exactly three hits total");
    Expect(!s.Alive(victim), "victim destroyed");
}

// 命中记忆与穿透收口（M5 批⓪ T2）：一弹一目标一次（弹 lifetime 内不重复伤同目标）；
// 穿透耗尽即毁。慢弹压着重叠多 tick 是回归重点——iFrames 窗尽后不得借窗复伤同目标。

void TestVerifyHitMemoryAndPierce() {
    World world;
    Scene& s = world.CreateScene("pierce");
    world.SetActiveScene(&s);

    Entity shooter = s.Create();
    s.Emplace<Transform2D>(shooter, Transform2D{{0, 0}});
    s.Emplace<Meta>(shooter).team = 0;

    // 场景 A：单怪 + 慢穿透弹压着重叠 30 tick（0.5s ≫ 0.1s 无敌窗）→ 恰一击
    Entity victim = s.Create();
    s.Emplace<Transform2D>(victim, Transform2D{{50, 0}});
    s.Emplace<Meta>(victim).team = 1;
    s.Emplace<Health>(victim, Health{.max = 100.0f, .cur = 100.0f});

    Entity p = s.Create();
    s.Emplace<Transform2D>(p, Transform2D{{50, 0}});
    s.Emplace<Meta>(p).team = 0;
    s.Emplace<Velocity>(p);
    Projectile& slow = s.Emplace<Projectile>(p);
    slow.damage = 10.0f;
    slow.lifetime = 30.0f;
    slow.pierce = 3;

    int hits = 0;
    world.SetEventSink([&](World&, const EventPacket& e) {
        if (e.type == GameEvent::Hit && e.dst == victim) ++hits;
    });
    world.Pipeline().AddSystem(std::make_unique<SpatialHashRebuildSystem>());
    world.Pipeline().AddSystem(std::make_unique<HitboxSystem>());
    world.Pipeline().AddSystem(std::make_unique<StatSystem>());
    world.Pipeline().AddSystem(std::make_unique<ScriptEventDispatchSystem>());
    world.Pipeline().AddSystem(std::make_unique<DestroyCommitSystem>());
    world.Pipeline().ResolveOrder();
    const float dt = 1.0f / 60.0f;

    for (int i = 0; i < 30; ++i) world.Step(dt);
    Expect(hits == 1, "one hit per target per projectile (memory)");
    Expect(s.Get<Projectile>(p).hits == 1, "projectile hit counter");
    Expect(s.Get<Health>(victim).cur == 90.0f, "victim damaged exactly once");
    Expect(s.Alive(p), "pierce not exhausted");

    // 场景 B：两怪同线（4px 内）+ pierce 1 → 双伤、弹毁。
    // 布点离场景 A 受害者 ≥16px：hitRadius 默认 12（原 6+6 有效口径），不得误伤
    Entity v2 = s.Create();
    s.Emplace<Transform2D>(v2, Transform2D{{66, 0}});
    s.Emplace<Meta>(v2).team = 1;
    s.Emplace<Health>(v2, Health{.max = 100.0f, .cur = 100.0f});
    Entity v3 = s.Create();
    s.Emplace<Transform2D>(v3, Transform2D{{70, 0}});
    s.Emplace<Meta>(v3).team = 1;
    s.Emplace<Health>(v3, Health{.max = 100.0f, .cur = 100.0f});

    Entity q = s.Create();
    s.Emplace<Transform2D>(q, Transform2D{{66, 0}});
    s.Emplace<Meta>(q).team = 0;
    s.Emplace<Velocity>(q);
    Projectile& pierce1 = s.Emplace<Projectile>(q);
    pierce1.damage = 10.0f;
    pierce1.lifetime = 30.0f;
    pierce1.pierce = 1;

    world.Step(dt); // v2、v3 各中一击，穿透耗尽
    world.Step(dt); // DestroyCommit 在 Essential（下一 tick 首）提交
    Expect(!s.Alive(q), "pierce exhausted -> projectile destroyed");
    Expect(s.Get<Health>(v2).cur == 90.0f && s.Get<Health>(v3).cur == 90.0f,
           "both in-line targets hit once");
    Expect(hits == 1, "scenario-A victim out of second projectile's range");
}

// M6 扫掠防穿透（review 2026-10-09，D2 追认：阈值门控 + 子步进）：高速弹单步
// 位移 > 2×hitRadius 时点采样有漏检带——修复前弹心每 tick 只查当前圆域，步长
// 136px（speed 8192@60Hz）对 hitRadius 12 的目标可整段跳过。修复后沿本帧位移
// 段子步采样（间距 ≤ 2×hitRadius），段含目标即命中；慢弹路径零扰动由
// TestVerifyHitMemoryAndPierce 既有断言把守。

void TestProjectileSweepAntiTunnel() {
    World world;
    Scene& s = world.CreateScene("sweep");
    world.SetActiveScene(&s);

    // 静止目标在 x=200：speed 8192 → 步长 ≈136.5px，tick1 弹心落 136.5、tick2
    // 落 273——两点距目标均 > 12（点采样两 tick 全漏）；tick2 位移段 [136.5, 273]
    // 含 200，子步采样必经目标圆域
    Entity victim = s.Create();
    s.Emplace<Transform2D>(victim, Transform2D{{200, 0}});
    s.Emplace<Meta>(victim).team = 1;
    s.Emplace<Health>(victim, Health{.max = 100.0f, .cur = 100.0f});

    Entity p = s.Create();
    s.Emplace<Transform2D>(p, Transform2D{{0, 0}});
    s.Emplace<Meta>(p).team = 0;
    s.Emplace<Velocity>(p, Velocity{{8192.0f, 0.0f}});
    Projectile& fast = s.Emplace<Projectile>(p);
    fast.damage = 25.0f;
    fast.lifetime = 1.0f;

    int hits = 0;
    world.SetEventSink([&](World&, const EventPacket& e) {
        if (e.type == GameEvent::Hit && e.dst == victim) ++hits;
    });
    world.Pipeline().AddSystem(std::make_unique<MovementSystem>());
    world.Pipeline().AddSystem(std::make_unique<SpatialHashRebuildSystem>());
    world.Pipeline().AddSystem(std::make_unique<HitboxSystem>());
    world.Pipeline().AddSystem(std::make_unique<StatSystem>());
    world.Pipeline().AddSystem(std::make_unique<ScriptEventDispatchSystem>());
    world.Pipeline().AddSystem(std::make_unique<DestroyCommitSystem>());
    world.Pipeline().ResolveOrder();
    const float dt = 1.0f / 60.0f;

    world.Step(dt); // tick1：段 [0,136.5] 不含目标（双点 0/136.5 距 200 均 > 12）
    Expect(hits == 0, "tick1: target not on first segment");
    world.Step(dt); // tick2：段 [136.5,273] 含 200 → 子步扫掠命中（点采样则漏）
    Expect(hits == 1, "tunneling projectile hits via swept substeps");
    Expect(s.Get<Health>(victim).cur == 75.0f, "sweep damage applied exactly once");
    world.Step(dt); // 弹已毁（非穿透）——后续段不再伤
    Expect(hits == 1, "no double hit from later segments");
    Expect(!s.Alive(p), "non-piercing projectile consumed on swept hit");
}

// 磁吸与拾取（M5 批① T2）：双侧取大触程（gem.magnetRadius vs Stats.pickupRadius）、
// 直写 pos 飞行、触距 8px 入账按 kind 分发、Pickup 事件、目标死亡回落。

void TestVerifyMagnetAndPickup() {
    World world;
    Scene& s = world.CreateScene("pickup");
    world.SetActiveScene(&s);

    // 收集者：磁力压到 8（段 A 够不到 30px）——首段专测宝石自程侧（段 B）
    Entity player = s.Create();
    s.Emplace<Transform2D>(player, Transform2D{{0, 0}});
    s.Emplace<Stats>(player).pickupRadius = 8.0f;
    s.Emplace<XpProgress>(player);

    // 宝石 30px：自程 48 内磁吸；320px/s = 5.33px/tick → 数 tick 后触距入账
    Entity gem = s.Create();
    s.Emplace<Transform2D>(gem, Transform2D{{30, 0}});
    s.Emplace<Collectible>(gem, Collectible{.kind = 0, .value = 5.0f});

    int pickups = 0;
    float evKind = -1.0f, evVal = -1.0f;
    world.SetEventSink([&](World&, const EventPacket& e) {
        if (e.type == GameEvent::Pickup) {
            ++pickups;
            evKind = e.payload[0];
            evVal = e.payload[1];
        }
    });

    world.Pipeline().AddSystem(std::make_unique<SpatialHashRebuildSystem>());
    world.Pipeline().AddSystem(std::make_unique<PickupSystem>());
    world.Pipeline().AddSystem(std::make_unique<ScriptEventDispatchSystem>());
    world.Pipeline().AddSystem(std::make_unique<DestroyCommitSystem>());
    world.Pipeline().ResolveOrder();
    const float dt = 1.0f / 60.0f;

    world.Step(dt);
    Expect(s.Get<Collectible>(gem).state == 1, "magnetized (self radius side)");
    const Vec2 p1 = s.Get<Transform2D>(gem).pos;
    Expect(p1.x < 30.0f && p1.y == 0.0f, "gem flies toward collector (direct pos write)");

    for (int i = 0; i < 8 && s.Alive(gem); ++i) world.Step(dt);
    world.Step(dt); // DestroyCommit 在 Essential（下一 tick 首）提交
    Expect(!s.Alive(gem), "gem picked up (touch distance)");
    Expect(pickups == 1, "one pickup event");
    Expect(evKind == 0.0f && evVal == 5.0f, "pickup payload kind/value");
    Expect(ExpectNear0(s.Get<XpProgress>(player).xp, 5.0f), "xp credited");

    // 双侧取大（段 A）：玩家磁力 120 > 宝石自程 48 → 100px 外的宝石也吸；
    // 负对照 200px 超两侧触程 → 原地不动
    Entity farGem = s.Create();
    s.Emplace<Transform2D>(farGem, Transform2D{{100, 0}});
    s.Emplace<Collectible>(farGem, Collectible{.kind = 1, .value = 7.0f});
    Entity idle = s.Create();
    s.Emplace<Transform2D>(idle, Transform2D{{200, 0}});
    s.Emplace<Collectible>(idle, Collectible{.kind = 0});
    s.Emplace<Inventory>(player);
    s.Get<Stats>(player).pickupRadius = 120.0f;

    world.Step(dt);
    Expect(s.Get<Collectible>(farGem).state == 1, "player stat side wins (max rule)");
    Expect(s.Get<Transform2D>(farGem).pos.x < 100.0f, "far gem flying");
    const Vec2 idlePos = s.Get<Transform2D>(idle).pos;
    for (int i = 0; i < 4; ++i) world.Step(dt);
    Expect(s.Get<Transform2D>(idle).pos == idlePos, "out of both radii stays idle");
    for (int i = 0; i < 40 && s.Alive(farGem); ++i) world.Step(dt);
    Expect(!s.Alive(farGem), "coin picked up");
    Expect(s.Get<Inventory>(player).gold == 7u, "coin -> gold");

    // heart：触距内 → 同 tick 磁吸即入账；治疗上限钳制
    s.Emplace<Health>(player, Health{.max = 100.0f, .cur = 90.0f});
    Entity heart = s.Create();
    s.Emplace<Transform2D>(heart, Transform2D{{4, 0}});
    s.Emplace<Collectible>(heart, Collectible{.kind = 2, .value = 20.0f});
    world.Step(dt);
    world.Step(dt); // 提交销毁
    Expect(!s.Alive(heart), "heart picked same tick as magnetize");
    Expect(s.Get<Health>(player).cur == 100.0f, "heal clamped at max");

    // 目标死亡回落：磁吸中销毁收集者 → state 回 0、位置冻结（宝石不丢可再吸）
    Entity gem3 = s.Create();
    s.Emplace<Transform2D>(gem3, Transform2D{{-60, 0}}); // 自程 48 不及，靠玩家磁力 120
    s.Emplace<Collectible>(gem3, Collectible{.kind = 0});
    world.Step(dt);
    Expect(s.Get<Collectible>(gem3).state == 1, "gem3 magnetized via player stat");
    s.Destroy(player);
    world.Step(dt); // Essential 先提交销毁 → 同 tick 段 C 检活回落
    Expect(s.Get<Collectible>(gem3).state == 0, "falls back idle on collector death");
    const Vec2 frozen = s.Get<Transform2D>(gem3).pos;
    for (int i = 0; i < 3; ++i) world.Step(dt);
    Expect(s.Get<Transform2D>(gem3).pos == frozen, "idle gem position frozen");
}

// XP 入账升级联动（M5 批① T2）：拾取同 tick 经 #12 升级环 → LevelUp 恰一次

void TestVerifyPickupXpLevelUp() {
    World world;
    Scene& s = world.CreateScene("lvl");
    world.SetActiveScene(&s);

    Entity player = s.Create();
    s.Emplace<Transform2D>(player, Transform2D{{0, 0}});
    XpProgress& xp = s.Emplace<XpProgress>(player);
    xp.xpToNext = 5.0f;
    Entity gem = s.Create();
    s.Emplace<Transform2D>(gem, Transform2D{{4, 0}}); // 触距内：同 tick 磁吸即入账
    s.Emplace<Collectible>(gem, Collectible{.kind = 0, .value = 10.0f});

    int levelUps = 0, pickups = 0;
    world.SetEventSink([&](World&, const EventPacket& e) {
        if (e.type == GameEvent::LevelUp) ++levelUps;
        if (e.type == GameEvent::Pickup) ++pickups;
    });
    world.Pipeline().AddSystem(std::make_unique<SpatialHashRebuildSystem>());
    world.Pipeline().AddSystem(std::make_unique<PickupSystem>());
    world.Pipeline().AddSystem(std::make_unique<StatSystem>());
    world.Pipeline().AddSystem(std::make_unique<ScriptEventDispatchSystem>());
    world.Pipeline().AddSystem(std::make_unique<DestroyCommitSystem>());
    world.Pipeline().ResolveOrder();

    world.Step(1.0f / 60.0f);
    const XpProgress& x = s.Get<XpProgress>(player);
    Expect(pickups == 1 && levelUps == 1, "pickup + level-up same tick");
    Expect(x.level == 2 && ExpectNear0(x.xp, 5.0f), "level 2 with carry 5");
    Expect(ExpectNear0(x.xpToNext, 7.0f), "xpToNext = ceil(5*1.25) = 7");
}

struct WaveSpawnCounter {
    int spawns = 0;
    Vec2 lastPos{999, 999};
    uint32_t lastTeam = 99;
    Entity operator()(Scene& sc, uint32_t prefabId, Vec2 pos, uint32_t team) {
        if (prefabId != 1) return Entity::Null();
        Entity e = sc.Create();
        sc.Emplace<Transform2D>(e, Transform2D{pos});
        sc.Emplace<Meta>(e).team = team;
        ++spawns;
        lastPos = pos;
        lastTeam = team;
        return e;
    }
};

void InstallDirectorPipeline(World& w) {
    w.Pipeline().AddSystem(std::make_unique<DirectorSystem>());
    w.Pipeline().AddSystem(std::make_unique<ScriptEventDispatchSystem>());
    w.Pipeline().AddSystem(std::make_unique<DestroyCommitSystem>());
    w.Pipeline().ResolveOrder();
}

// 波推进时刻 / WaveStart 契约 / 出生环与队伍覆盖 / 晚波不生效（D3/D5/D6）

void TestWaveDirectorWavesAndEvent() {
    World world;
    Scene& s = world.CreateScene("wdir");
    world.SetActiveScene(&s);
    WaveSpawnCounter ctr;
    world.SetSpawnFn(
        [&ctr](Scene& sc, uint32_t id, Vec2 pos, uint32_t team) { return ctr(sc, id, pos, team); });

    Entity dir = s.Create();
    s.Emplace<Transform2D>(dir, Transform2D{{100, 0}});
    WaveDirector& wd = s.Emplace<WaveDirector>(dir);
    wd.spawnTeam = 1;
    wd.waveCount = 3;
    wd.waves[0] = WaveDef{.startTime = 0.5f};
    wd.waves[0].entryCount = 1;
    wd.waves[0].entries[0] = WaveEntry{.prefabId = 1, .count = 3, .interval = 0.1f, .range = 50.0f};
    wd.waves[1] = WaveDef{.startTime = 1.5f};
    wd.waves[1].entryCount = 1;
    wd.waves[1].entries[0] = WaveEntry{.prefabId = 1, .count = 2, .interval = 0.1f, .range = 50.0f};
    wd.waves[2] = WaveDef{.startTime = 99.0f}; // 窗口外：永不生效
    wd.waves[2].entryCount = 1;
    wd.waves[2].entries[0] = WaveEntry{.prefabId = 1, .count = 7, .interval = 0.1f, .range = 50.0f};

    int waveStarts = 0;
    float planned0 = -1.0f, index1 = -1.0f;
    world.SetEventSink([&](World&, const EventPacket& e) {
        if (e.type != GameEvent::WaveStart) return;
        ++waveStarts;
        if (e.payload[0] < 0.5f)
            planned0 = e.payload[1];
        else
            index1 = e.payload[0];
    });
    InstallDirectorPipeline(world);

    const float dt = 1.0f / 60.0f;
    for (int i = 0; i < 30; ++i) world.Step(dt);
    Expect(waveStarts == 1, "wave 0 fires at t=0.5s (tick 30)");
    for (int i = 30; i < 120; ++i) world.Step(dt);
    Expect(waveStarts == 2, "wave 1 at t=1.5s; wave 2 (99s) never");
    Expect(ctr.spawns == 5, "3 + 2 spawned; late wave not started");
    Expect(planned0 == 3.0f && index1 == 1.0f, "payload [0]=wave index [1]=planned total");
    Expect(Length(ctr.lastPos - Vec2{100, 0}) <= 50.0f + 1e-4f, "spawn within entry range ring");
    Expect(ctr.lastTeam == 1, "spawn team override");
    Expect(s.Get<WaveDirector>(dir).waveIndex == 2, "wave cursor = started waves");
}

// rampMult 加速（interval/rampMult）+ 波重叠 = 后波接管（D3）

void TestWaveDirectorRampAndOverlap() {
    const float dt = 1.0f / 60.0f;
    // ramp 2：interval 0.1 → 有效 0.05s = 3 tick/生 → 12 tick 内 4 生（基线仅 2）
    {
        World world;
        Scene& s = world.CreateScene("ramp2");
        world.SetActiveScene(&s);
        WaveSpawnCounter ctr;
        world.SetSpawnFn([&ctr](Scene& sc, uint32_t id, Vec2 pos, uint32_t team) {
            return ctr(sc, id, pos, team);
        });
        Entity dir = s.Create();
        s.Emplace<Transform2D>(dir, Transform2D{{0, 0}});
        WaveDirector& wd = s.Emplace<WaveDirector>(dir);
        wd.waveCount = 1;
        wd.waves[0] = WaveDef{.startTime = 0.0f, .rampMult = 2.0f};
        wd.waves[0].entryCount = 1;
        wd.waves[0].entries[0] = WaveEntry{.prefabId = 1, .count = 4, .interval = 0.1f};
        InstallDirectorPipeline(world);
        for (int i = 0; i < 12; ++i) world.Step(dt);
        Expect(ctr.spawns == 4, "rampMult 2: 4 spawns in 12 ticks (3-tick cadence)");
    }
    // 双波同 tick 到期：WaveStart ×2 但仅后波持有运行时（前波条目废止）
    {
        World world;
        Scene& s = world.CreateScene("ovl");
        world.SetActiveScene(&s);
        WaveSpawnCounter ctr;
        world.SetSpawnFn([&ctr](Scene& sc, uint32_t id, Vec2 pos, uint32_t team) {
            return ctr(sc, id, pos, team);
        });
        Entity dir = s.Create();
        s.Emplace<Transform2D>(dir, Transform2D{{0, 0}});
        WaveDirector& wd = s.Emplace<WaveDirector>(dir);
        wd.waveCount = 2;
        wd.waves[0].startTime = 0.0f;
        wd.waves[0].entryCount = 1;
        wd.waves[0].entries[0] = WaveEntry{.prefabId = 1, .count = 5, .interval = 0.1f};
        wd.waves[1].startTime = 0.0f; // 同 tick 到期 → 接管
        wd.waves[1].entryCount = 1;
        wd.waves[1].entries[0] = WaveEntry{.prefabId = 1, .count = 2, .interval = 0.1f};
        int waveStarts = 0;
        world.SetEventSink([&](World&, const EventPacket& e) {
            if (e.type == GameEvent::WaveStart) ++waveStarts;
        });
        InstallDirectorPipeline(world);
        for (int i = 0; i < 60; ++i) world.Step(dt);
        Expect(waveStarts == 2, "both overlapping waves announce");
        Expect(ctr.spawns == 2, "later wave takes over; earlier entries dropped");
    }
}

// capAlive 同队闸门：普查 + 乐观自增 → 精确停在闸值（D4）

void TestWaveDirectorCapAlive() {
    World world;
    Scene& s = world.CreateScene("cap");
    world.SetActiveScene(&s);
    WaveSpawnCounter ctr;
    world.SetSpawnFn(
        [&ctr](Scene& sc, uint32_t id, Vec2 pos, uint32_t team) { return ctr(sc, id, pos, team); });
    Entity dir = s.Create();
    s.Emplace<Transform2D>(dir, Transform2D{{0, 0}});
    WaveDirector& wd = s.Emplace<WaveDirector>(dir);
    wd.spawnTeam = 1;
    wd.capAlive = 2;
    wd.waveCount = 1;
    wd.waves[0].startTime = 0.0f;
    wd.waves[0].entryCount = 1;
    wd.waves[0].entries[0] = WaveEntry{.prefabId = 1, .count = 10, .interval = 1.0f / 60.0f};
    InstallDirectorPipeline(world);

    const float dt = 1.0f / 60.0f;
    for (int i = 0; i < 120; ++i) world.Step(dt);
    uint32_t alive = 0;
    s.View<Meta>().each([&](auto, Meta& m) {
        if (m.team == 1) ++alive;
    });
    Expect(ctr.spawns == 2, "capAlive 2: exactly 2 births, no retry churn");
    Expect(alive == 2, "team alive holds at cap");
}

// timeScale=0 冻结波次（time 停、零事件零出生、RNG 不消耗；恢复即照发——D3×批① D5）

void TestWaveDirectorTimeScaleFreeze() {
    World world;
    Scene& s = world.CreateScene("frz");
    world.SetActiveScene(&s);
    WaveSpawnCounter ctr;
    world.SetSpawnFn(
        [&ctr](Scene& sc, uint32_t id, Vec2 pos, uint32_t team) { return ctr(sc, id, pos, team); });
    Entity dir = s.Create();
    s.Emplace<Transform2D>(dir, Transform2D{{0, 0}});
    WaveDirector& wd = s.Emplace<WaveDirector>(dir);
    wd.waveCount = 1;
    wd.waves[0].startTime = 0.5f;
    wd.waves[0].entryCount = 1;
    wd.waves[0].entries[0] = WaveEntry{.prefabId = 1, .count = 3, .interval = 0.1f};
    int waveStarts = 0;
    world.SetEventSink([&](World&, const EventPacket& e) {
        if (e.type == GameEvent::WaveStart) ++waveStarts;
    });
    InstallDirectorPipeline(world);

    const float dt = 1.0f / 60.0f;
    world.SetTimeScale(0.0f);
    for (int i = 0; i < 60; ++i) world.Step(dt);
    Expect(waveStarts == 0 && ctr.spawns == 0, "frozen: no wave, no spawn");
    Expect(s.Get<WaveDirector>(dir).time == 0.0f, "director time frozen at 0");

    world.SetTimeScale(1.0f);
    for (int i = 0; i < 30; ++i) world.Step(dt);
    Expect(waveStarts == 1 && ctr.spawns >= 1, "resume: wave fires on schedule");
}

// #61（review 2026-10-02）：连发型 Spawner（interval≤dt → 出生后冷却钳 0）在
// timeScale=0 冻结期不得逐 tick 泄漏 burst/RNG——原实现到期冷却照走出生分支，
// 与"冻结波次、RNG 不消耗"注释自述相悖。导演段常规路径冻结安全（重置后冷却
// 恒 >0），本测试专钉 Spawner 缺口 + 阴性验证修复。

void TestSpawnFreezeNoLeak() {
    World world;
    Scene& s = world.CreateScene("frz-sp");
    world.SetActiveScene(&s);
    WaveSpawnCounter ctr;
    world.SetSpawnFn(
        [&ctr](Scene& sc, uint32_t id, Vec2 pos, uint32_t team) { return ctr(sc, id, pos, team); });
    Entity sp = s.Create();
    s.Emplace<Transform2D>(sp, Transform2D{{0, 0}});
    Spawner& spn = s.Emplace<Spawner>(sp);
    spn.prefabId = 1; // WaveSpawnCounter 只认 prefab 1
    spn.interval = 0.001f; // < dt：每 tick 到期，出生后冷却钳 0（泄漏触发形态）
    spn.burst = 2;
    world.Pipeline().AddSystem(std::make_unique<SpawnSystem>());
    world.Pipeline().AddSystem(std::make_unique<DestroyCommitSystem>());
    world.Pipeline().ResolveOrder();
    const float dt = 1.0f / 60.0f;

    world.Step(dt); // 常速 1 tick：burst 出生、冷却钳位 0
    Expect(ctr.spawns >= 1, "#61: fast spawner produced first burst");
    const int base = ctr.spawns;

    world.SetTimeScale(0.0f);
    for (int i = 0; i < 60; ++i) world.Step(dt);
    Expect(ctr.spawns == base, "#61: frozen spawner leaks no burst (RNG idle)");
}

// 波表 roundtrip；运行时（time/waveIndex/cd/spawned）不入档（T1 登记表护栏）

void TestWaveDirectorArchive() {
    World world;
    Scene& src = world.CreateScene("warc");
    Entity e = src.Create();
    src.Emplace<Transform2D>(e, Transform2D{{7, 8}});
    WaveDirector& wd = src.Emplace<WaveDirector>(e);
    wd.spawnTeam = 3;
    wd.capAlive = 777;
    wd.waveCount = 2;
    wd.waves[0].startTime = 1.25f;
    wd.waves[0].rampMult = 2.5f;
    wd.waves[0].entryCount = 2;
    wd.waves[0].entries[0] =
        WaveEntry{.prefabId = 0xAABBCCDDu, .count = 11, .interval = 0.05f, .range = 333.0f};
    wd.waves[0].entries[1] = WaveEntry{.prefabId = 7, .count = 1, .interval = 0.2f, .range = 40.0f};
    wd.waves[1].startTime = 30.0f;
    wd.waves[1].entryCount = 1;
    wd.waves[1].entries[0] = WaveEntry{.prefabId = 9, .count = 5, .interval = 0.1f, .range = 60.0f};
    // RT 污染（读档必须回落默认）
    wd.time = 9.9f;
    wd.waveIndex = 1;
    wd.waveCooldown[1] = 0.42f;
    wd.waveSpawned[2] = 3;

    const std::string text = SceneArchive::Save(src);
    Expect(text.find("\"waves\"") != std::string::npos, "wave table serialized");
    Expect(text.find("\"e0prefab\"") != std::string::npos, "flattened entry keys serialized");
    Expect(text.find("\"cd0\"") == std::string::npos, "cooldown RT not serialized");
    Expect(text.find("\"waveIndex\"") == std::string::npos, "waveIndex RT not serialized");

    World w2;
    Scene& dst = w2.CreateScene("warc2");
    Expect(SceneArchive::Load(dst, text), "wave scene load");
    dst.View<WaveDirector>().each([&](auto, WaveDirector& r) {
        Expect(r.spawnTeam == 3 && r.capAlive == 777, "director config roundtrip");
        Expect(r.waveCount == 2, "wave count roundtrip");
        Expect(r.waves[0].startTime == 1.25f && r.waves[0].rampMult == 2.5f, "wave 0 header");
        Expect(r.waves[0].entries[0].prefabId == 0xAABBCCDDu && r.waves[0].entries[0].count == 11 &&
                   r.waves[0].entries[0].interval == 0.05f && r.waves[0].entries[0].range == 333.0f,
               "entry 0 roundtrip");
        Expect(r.waves[0].entryCount == 2 && r.waves[0].entries[1].count == 1, "entry 1 roundtrip");
        Expect(r.waves[1].startTime == 30.0f && r.waves[1].entries[0].prefabId == 9,
               "wave 1 roundtrip");
        Expect(r.time == 0.0f && r.waveIndex == 0 && r.waveCooldown[1] == 0.0f &&
                   r.waveSpawned[2] == 0,
               "runtime fields default after load");
    });
}

// 孪生世界同种子：RNG 子流 1 消费序 + seg 原始字节零化 → StateHash 相等（§13 护栏）

void TestWaveDirectorDeterminism() {
    auto run = [](uint64_t& hashOut, int& spawnsOut) {
        World world;
        Scene& s = world.CreateScene("det");
        world.SetActiveScene(&s);
        WaveSpawnCounter ctr;
        world.SetSpawnFn([&ctr](Scene& sc, uint32_t id, Vec2 pos, uint32_t team) {
            return ctr(sc, id, pos, team);
        });
        Entity dir = s.Create();
        s.Emplace<Transform2D>(dir, Transform2D{{10, -5}});
        WaveDirector& wd = s.Emplace<WaveDirector>(dir);
        wd.waveCount = 2;
        wd.waves[0].startTime = 0.0f;
        wd.waves[0].entryCount = 1;
        wd.waves[0].entries[0] =
            WaveEntry{.prefabId = 1, .count = 8, .interval = 0.05f, .range = 50.0f};
        wd.waves[1].startTime = 1.0f;
        wd.waves[1].entryCount = 1;
        wd.waves[1].entries[0] =
            WaveEntry{.prefabId = 1, .count = 4, .interval = 0.1f, .range = 30.0f};
        InstallDirectorPipeline(world);
        for (int i = 0; i < 300; ++i) world.Step(1.0f / 60.0f);
        hashOut = ComputeStateHash(s);
        spawnsOut = ctr.spawns;
    };
    uint64_t ha = 0, hb = 0;
    int sa = 0, sb = 0;
    run(ha, sa);
    run(hb, sb);
    Expect(sa == 12 && sa == sb, "both worlds spawn full wave tables (RNG path exercised)");
    Expect(ha == hb, "twin worlds: identical state hash");
}

// timeScale（M5 批① T3）：Step 内缩放 dt；=0 冻结（位置不动、tick 照推）、
// 0.5 半速（同 tick 数位移对半）；setter clamp [0,8]

void TestVerifyTimeScale() {
    World world;
    Scene& s = world.CreateScene("ts");
    world.SetActiveScene(&s);

    Entity e = s.Create();
    s.Emplace<Transform2D>(e, Transform2D{{0, 0}});
    s.Emplace<Velocity>(e, Velocity{.v = {100.0f, 0.0f}});

    world.Pipeline().AddSystem(std::make_unique<MovementSystem>());
    world.Pipeline().AddSystem(std::make_unique<DestroyCommitSystem>());
    world.Pipeline().ResolveOrder();
    const float dt = 1.0f / 60.0f;

    for (int i = 0; i < 60; ++i) world.Step(dt);
    Expect(std::fabs(s.Get<Transform2D>(e).pos.x - 100.0f) < 0.01f,
           "full speed 1s = 100px"); // 累加容差放宽（1e-5 对 60 步过紧）

    world.SetTimeScale(0.5f);
    for (int i = 0; i < 30; ++i) world.Step(dt); // 名义 0.5s × 0.5 = +25px
    Expect(std::fabs(s.Get<Transform2D>(e).pos.x - 125.0f) < 0.01f, "half speed +25px");

    world.SetTimeScale(0.0f); // 冻结暂停：tick 照推、位置不动（RNG 不消耗口径）
    const uint64_t tickBefore = world.TickIndex();
    for (int i = 0; i < 10; ++i) world.Step(dt);
    Expect(std::fabs(s.Get<Transform2D>(e).pos.x - 125.0f) < 0.01f, "frozen position holds");
    Expect(world.TickIndex() == tickBefore + 10, "ticks advance while paused");

    world.SetTimeScale(-3.0f);
    Expect(world.TimeScale() == 0.0f, "negative clamped to 0");
    world.SetTimeScale(99.0f);
    Expect(world.TimeScale() == 8.0f, "overshoot clamped to 8");
}

// 双死防护：同帧两发投射物 + 一个 Hazard 打同一目标 → 恰一个 Death 事件

void TestNoDoubleDeathEvents() {
    World world;
    Scene& s = world.CreateScene("dd");
    world.SetActiveScene(&s);

    Entity shooter = s.Create(); // team0 射手（供弹体势力继承）
    s.Emplace<Transform2D>(shooter, Transform2D{{0, 0}});
    s.Emplace<Meta>(shooter).team = 0;

    Entity victim = s.Create(); // team1 受害者
    s.Emplace<Transform2D>(victim, Transform2D{{50, 0}});
    s.Emplace<Meta>(victim).team = 1;
    s.Emplace<Health>(victim, Health{.max = 10.0f, .cur = 10.0f});

    for (int i = 0; i < 2; ++i) { // 两发足以致死的弹（damage 10）
        Entity p = s.Create();
        s.Emplace<Transform2D>(p, Transform2D{{50, 0}});
        s.Emplace<Meta>(p).team = 0;
        s.Emplace<Velocity>(p);
        Projectile& pr = s.Emplace<Projectile>(p);
        pr.damage = 10.0f;
        pr.lifetime = 10.0f;
    }
    Entity hz = s.Create(); // 叠一个同 tick Hazard
    s.Emplace<Transform2D>(hz, Transform2D{{50, 0}});
    s.Emplace<Meta>(hz).team = 0;
    Hazard& h = s.Emplace<Hazard>(hz);
    h.dps = 1000.0f;
    h.tickInterval = 0.5f;
    h.tickPhase = 0.0f;

    s.Spatial().Rebuild(s);

    // 只装命中相关系统（事件计数干净）
    world.Pipeline().AddSystem(std::make_unique<HitboxSystem>());
    world.Pipeline().AddSystem(std::make_unique<ScriptEventDispatchSystem>());
    world.Pipeline().AddSystem(std::make_unique<DestroyCommitSystem>());
    world.Pipeline().ResolveOrder();
    int deaths = 0;
    world.SetEventSink([&](World&, const EventPacket& p) {
        if (p.type == GameEvent::Death && p.src == victim) ++deaths;
    });
    world.Step(1.0f / 60.0f);
    Expect(deaths == 1, "exactly one death event for victim");
    Expect(world.Events().Size() == 0, "events drained at frame end");
}

// ---- AI Chase：追击 / keepRange 停 / 出警戒归零 ----

void TestVerifyAISystemChase() {
    World w;
    w.InstallDefaultSystems();
    Scene& s = w.CreateScene("chase");
    w.SetActiveScene(&s);
    Entity m = s.Create();
    s.Emplace<Transform2D>(m, Transform2D{{0, 0}});
    s.Emplace<Meta>(m).team = 1;
    s.Emplace<Velocity>(m);
    Chase& ch = s.Emplace<Chase>(m);
    ch.speed = 100.0f;
    ch.aggroRange = 2000.0f;
    ch.keepRange = 24.0f;
    ch.targetTeam = 0;
    Entity p = s.Create();
    s.Emplace<Transform2D>(p, Transform2D{{100, 0}});
    s.Emplace<Meta>(p).team = 0;
    s.Emplace<Velocity>(p);

    w.Step(1.0f / 60.0f);
    Vec2 v = s.Get<Velocity>(m).v;
    Expect(v.x > 90.0f && std::fabs(v.y) < 1.0f, "ai: chases along +x at speed");
    ExpectNear(Length(v), 100.0f, 0.5f, "ai: velocity magnitude = speed");

    s.Get<Transform2D>(p).pos = {0, 0}; // 贴脸
    w.Step(1.0f / 60.0f);
    Expect(s.Get<Velocity>(m).v == Vec2::Zero(), "ai: keepRange stops");

    s.Get<Transform2D>(p).pos = {5000, 0}; // 出警戒
    w.Step(1.0f / 60.0f);
    Expect(s.Get<Velocity>(m).v == Vec2::Zero(), "ai: target lost → zero");
}

// ---- Flee / Patrol 行为 ----

void TestVerifyFleeAndPatrol() {
    {
        World w;
        w.InstallDefaultSystems();
        Scene& s = w.CreateScene("flee");
        w.SetActiveScene(&s);
        Entity f = s.Create();
        s.Emplace<Transform2D>(f, Transform2D{{0, 0}});
        s.Emplace<Meta>(f).team = 2;
        s.Emplace<Velocity>(f);
        Flee& fl = s.Emplace<Flee>(f);
        fl.speed = 80.0f;
        fl.range = 200.0f;
        Entity threat = s.Create();
        s.Emplace<Transform2D>(threat, Transform2D{{50, 0}});
        s.Emplace<Meta>(threat).team = 0;

        // [ISSUE-5 已修复] NearestAny 排除自身：威胁在 +50 → 逃向 -x
        w.Step(1.0f / 60.0f);
        Vec2 v = s.Get<Velocity>(f).v;
        Expect(v.x < -70.0f && std::fabs(v.y) < 1.0f, "flee: runs away from threat");
        ExpectNear(Length(v), 80.0f, 0.5f, "flee: velocity magnitude = fl.speed");

        s.Get<Transform2D>(threat).pos = {5000, 0};
        s.Get<Velocity>(f).v = {7, 7}; // 哨兵：无威胁时不得覆写
        w.Step(1.0f / 60.0f);
        Expect(s.Get<Velocity>(f).v == Vec2({7, 7}),
               "flee: no threat keeps velocity (no overwrite)");
    }
    {
        World w;
        w.InstallDefaultSystems();
        Scene& s = w.CreateScene("patrol");
        w.SetActiveScene(&s);
        Entity e = s.Create();
        s.Emplace<Transform2D>(e, Transform2D{{99, 0}});
        s.Emplace<Velocity>(e);
        Patrol& pt = s.Emplace<Patrol>(e);
        pt.a = {0, 0};
        pt.b = {100, 0};
        pt.headingToB = 1;

        w.Step(1.0f / 60.0f);
        Expect(pt.headingToB == 0, "patrol: flips at endpoint");
        // [ISSUE-6 已修复] 折返同帧改向：折返帧速度立即朝新端点（原滞后一帧）
        Expect(s.Get<Velocity>(e).v.x < 0, "patrol: flip-frame velocity already toward new dest");
        w.Step(1.0f / 60.0f);
        Expect(s.Get<Velocity>(e).v.x < 0, "patrol: keeps heading back");
    }
}

// ---- Animator：loop 有界 / 非 loop 增长 ----

void TestVerifyAnimatorAdvance() {
    World w;
    w.InstallDefaultSystems();
    Scene& s = w.CreateScene("anim");
    w.SetActiveScene(&s);
    Entity loopy = s.Create();
    Animator2D& al = s.Emplace<Animator2D>(loopy);
    al.speed = 2.0f;
    al.loop = 1;
    Entity oncey = s.Create();
    Animator2D& ao = s.Emplace<Animator2D>(oncey);
    ao.speed = 1.0f;
    ao.loop = 0;
    for (int i = 0; i < 120; ++i) w.Step(1.0f / 60.0f); // 2 模拟秒
    Expect(al.time >= 0.0f && al.time < 1.0f, "anim: loop keeps time bounded");
    Expect(ao.time > 1.0f, "anim: non-loop advances unbounded (M2 语义)");

    // 批③回退护栏：表非空但 clipId 未命中（错绑）→ 仍走 M2 旧算术（金档零漂移前提）
    w.Clips().Add(0xEEEEu, {501u}, 8.0f, true); // 表非空即可
    Entity missy = s.Create();
    Animator2D& am = s.Emplace<Animator2D>(missy);
    am.clipId = 0xDEADBEEFu; // 未登记 id
    am.speed = 1.0f;
    am.loop = 1;
    for (int i = 0; i < 120; ++i) w.Step(1.0f / 60.0f);
    Expect(am.time >= 0.0f && am.time < 1.0f, "anim: unknown clipId falls back to M2 loop");
}

// ---- 批③：ClipTable 帧映射（fps 截断/回绕/钳末帧/暂停/半速/负速/无渲染器/孪生/roundtrip）----

void TestVerifyAnimatorFrameMapping() {
    const float dt = 1.0f / 60.0f;
    World w;
    w.InstallDefaultSystems();
    Scene& s = w.CreateScene("clip");
    w.SetActiveScene(&s);
    // clip 0x77：fps 8 × 3 帧（spriteId 10/11/12）→ 帧界 7.5 tick、周期 22.5 tick
    Expect(w.Clips().Add(0x77u, {10u, 11u, 12u}, 8.0f, true), "clip table add");
    Expect(!w.Clips().Add(0u, {1u}, 8.0f, true), "clip id 0 rejected");
    Expect(w.Clips().Add(0x78u, {}, 8.0f, true) == false, "empty frames rejected");
    Expect(w.Clips().Find(0x77u) != nullptr && w.Clips().Find(0u) == nullptr &&
               w.Clips().Find(0x999u) == nullptr,
           "clip find semantics");

    Entity loopy = s.Create();
    SpriteRenderer& srl = s.Emplace<SpriteRenderer>(loopy);
    srl.spriteId = 999u;
    Animator2D& al = s.Emplace<Animator2D>(loopy);
    al.clipId = 0x77u;

    Entity oncey = s.Create(); // loop=0 钳末帧（M5 收口：time 钳 total 有界）
    SpriteRenderer& sro = s.Emplace<SpriteRenderer>(oncey);
    sro.spriteId = 999u;
    Animator2D& ao = s.Emplace<Animator2D>(oncey);
    ao.clipId = 0x77u;
    ao.loop = 0;

    Entity paused = s.Create(); // playOnStart=0 = 暂停开关（三态全冻结）
    SpriteRenderer& srp = s.Emplace<SpriteRenderer>(paused);
    srp.spriteId = 999u;
    Animator2D& ap = s.Emplace<Animator2D>(paused);
    ap.clipId = 0x77u;
    ap.playOnStart = 0;

    Entity half = s.Create(); // speed 0.5：16 tick == 全速 8 tick
    Animator2D& ah = s.Emplace<Animator2D>(half);
    ah.clipId = 0x77u;
    ah.speed = 0.5f;

    Entity bare = s.Create(); // 无 SpriteRenderer：纯计时推进不炸
    Animator2D& ab = s.Emplace<Animator2D>(bare);
    ab.clipId = 0x77u;

    Entity neg = s.Create(); // 负 speed 防御：time 钳 0、停 0 号帧
    SpriteRenderer& srn = s.Emplace<SpriteRenderer>(neg);
    srn.spriteId = 999u;
    Animator2D& an = s.Emplace<Animator2D>(neg);
    an.clipId = 0x77u;
    an.speed = -1.0f;

    for (int i = 0; i < 8; ++i) w.Step(dt);
    Expect(al.curFrame == 1 && srl.spriteId == 11u, "clip: tick 8 -> frame 1");
    Expect(ah.curFrame == 0, "clip: half speed still frame 0 at tick 8");
    Expect(ab.curFrame == 1, "clip: no-renderer animator advances");
    for (int i = 0; i < 14; ++i) w.Step(dt); // 累计 22 tick
    Expect(al.curFrame == 2 && srl.spriteId == 12u, "clip: tick 22 -> frame 2");
    w.Step(dt); // 23 tick：time 0.3833 ≥ total 0.375 → 回绕
    Expect(al.curFrame == 0 && srl.spriteId == 10u, "clip: tick 23 wraps to frame 0");
    Expect(ah.curFrame == 1, "clip: half speed reaches frame 1 at tick 23 (帧界 15 tick)");
    for (int i = 0; i < 97; ++i) w.Step(dt); // 累计 120 tick
    Expect(ao.time <= 3.0f / 8.0f && ao.curFrame == 2 && sro.spriteId == 12u,
           "clip: non-loop clamps to last frame (time bounded)");
    Expect(ap.time == 0.0f && ap.curFrame == 0 && srp.spriteId == 999u,
           "clip: playOnStart=0 freezes all three");
    Expect(an.time == 0.0f && an.curFrame == 0 && srn.spriteId == 10u,
           "clip: negative speed clamps to frame 0");

    // roundtrip：clipId/speed/loop/playOnStart 入档；time/curFrame 亦入档（FIELD 位未动）
    const std::string text = SceneArchive::Save(s);
    Expect(text.find("\"clipId\"") != std::string::npos, "clipId serialized");
    World w2;
    Scene& dst = w2.CreateScene("clip2");
    Expect(SceneArchive::Load(dst, text), "clip scene load");
    dst.View<Animator2D>().each([&](auto ent2, Animator2D& r) {
        if (Scene::FromEntt(ent2) == oncey) {
            Expect(r.clipId == 0x77u && r.loop == 0 && r.speed == 1.0f,
                   "animator config roundtrip");
        }
    });

    // 孪生世界：帧映射纯函数 + 无 RNG 消费 → 300 tick StateHash 相等（§18 护栏）
    auto run = [](uint64_t& hashOut) {
        World world;
        world.InstallDefaultSystems();
        Scene& sc = world.CreateScene("det");
        world.SetActiveScene(&sc);
        world.Clips().Add(0x77u, {10u, 11u, 12u}, 8.0f, true);
        for (int k = 0; k < 3; ++k) {
            ecs::Entity e = sc.Create();
            sc.Emplace<Transform2D>(e, Transform2D{{(float)k * 30.0f, 5.0f}});
            SpriteRenderer& sr = sc.Emplace<SpriteRenderer>(e);
            sr.spriteId = 999u;
            Animator2D& a = sc.Emplace<Animator2D>(e);
            a.clipId = 0x77u;
            a.speed = 1.0f + 0.5f * (float)k; // 不同速度混合
        }
        for (int i = 0; i < 300; ++i) world.Step(1.0f / 60.0f);
        hashOut = ComputeStateHash(sc);
    };
    uint64_t ha = 0, hb = 0;
    run(ha);
    run(hb);
    Expect(ha == hb, "clip anim twin worlds: identical state hash");
}

// ---- 批①：Animator 换段队列（Queue 收尾/回绕点、CrossFade 倒计/提前收尾、
// 暂停冻结、Play 清队列、目标未命中丢弃、M2 旁路、孪生哈希）----

void TestVerifyAnimatorQueue() {
    const float dt = 1.0f / 60.0f;
    World w;
    w.InstallDefaultSystems();
    Scene& s = w.CreateScene("queue");
    w.SetActiveScene(&s);
    // 0x77：fps8×3 帧（10/11/12），周期 22.5 tick；0x88：fps8×2 帧（20/21）；
    // 0x99：fps4×2 帧（30/31）
    w.Clips().Add(0x77u, {10u, 11u, 12u}, 8.0f, true);
    w.Clips().Add(0x88u, {20u, 21u}, 8.0f, true);
    w.Clips().Add(0x99u, {30u, 31u}, 4.0f, true);

    // ① Queue：非 loop 段收尾即切（tick 23 time 钳 total → 切，新段首帧当帧生效）
    Entity q1 = s.Create();
    SpriteRenderer& sr1 = s.Emplace<SpriteRenderer>(q1);
    Animator2D& a1 = s.Emplace<Animator2D>(q1);
    a1.clipId = 0x77u;
    a1.loop = 0;
    a1.nextClipId = 0x88u;
    a1.nextLoop = 1;
    a1.fadeRemain = -1.0f;
    for (int i = 0; i < 23; ++i) w.Step(dt);
    Expect(a1.clipId == 0x88u && a1.time == 0.0f && a1.loop == 1 && a1.curFrame == 0 &&
               sr1.spriteId == 20u && a1.nextClipId == 0 && a1.fadeRemain == 0.0f,
           "queue: non-loop end switches to next clip frame 0");

    // ② Queue：loop 段回绕点切（tick 23 回绕瞬间切段 + nextLoop=0 生效）
    Entity q2 = s.Create();
    Animator2D& a2 = s.Emplace<Animator2D>(q2);
    a2.clipId = 0x77u;
    a2.nextClipId = 0x88u;
    a2.nextLoop = 0;
    a2.fadeRemain = -1.0f;
    for (int i = 0; i < 23; ++i) w.Step(dt);
    Expect(a2.clipId == 0x88u && a2.time == 0.0f && a2.loop == 0,
           "queue: loop wrap point switches");

    // ③ CrossFade：倒计时到零切（fade=5.5 tick：第 5 tick 未切、第 6 tick 切——
    //    半 tick 余量避开浮点累积误差踩线）
    Entity q3 = s.Create();
    SpriteRenderer& sr3 = s.Emplace<SpriteRenderer>(q3);
    Animator2D& a3 = s.Emplace<Animator2D>(q3);
    a3.clipId = 0x77u;
    a3.nextClipId = 0x88u;
    a3.fadeRemain = 5.5f * dt;
    for (int i = 0; i < 5; ++i) w.Step(dt);
    const bool stillOld = a3.clipId == 0x77u && a3.curFrame == 0; // 未到 7.5 tick 帧界
    for (int i = 0; i < 1; ++i) w.Step(dt);
    Expect(stillOld && a3.clipId == 0x88u && sr3.spriteId == 20u,
           "crossfade: countdown expiry switches (5 no, 6 yes)");

    // ④ CrossFade：非 loop 当前段提前收尾即切（fade 再长也不等）
    Entity q4 = s.Create();
    Animator2D& a4 = s.Emplace<Animator2D>(q4);
    a4.clipId = 0x77u;
    a4.loop = 0;
    for (int i = 0; i < 22; ++i) w.Step(dt); // time 0.3667（一 tick 后收尾）
    a4.nextClipId = 0x88u;
    a4.fadeRemain = 60.0f * dt; // 1 秒长淡入——收尾必须抢先
    w.Step(dt);
    Expect(a4.clipId == 0x88u && a4.time == 0.0f,
           "crossfade: non-loop early end overrides long fade");

    // ⑤ 暂停冻结整个队列（time/倒计/切点三冻；恢复后倒计继续；半 tick 余量同③）
    Entity q5 = s.Create();
    Animator2D& a5 = s.Emplace<Animator2D>(q5);
    a5.clipId = 0x77u;
    a5.playOnStart = 0;
    a5.nextClipId = 0x88u;
    a5.fadeRemain = 3.5f * dt;
    for (int i = 0; i < 10; ++i) w.Step(dt);
    const bool frozen = a5.time == 0.0f && a5.nextClipId == 0x88u && a5.fadeRemain == 3.5f * dt &&
                        a5.clipId == 0x77u;
    a5.playOnStart = 1;
    for (int i = 0; i < 3; ++i) w.Step(dt); // 倒计 0.5dt 余量未到
    const bool stillQueued = a5.clipId == 0x77u && a5.nextClipId == 0x88u;
    w.Step(dt);
    Expect(frozen && stillQueued && a5.clipId == 0x88u,
           "pause freezes queue; resume continues countdown");

    // ⑥ Play 清在途队列（SDK Play 等价字段写：打断一切在途切换）
    Entity q6 = s.Create();
    Animator2D& a6 = s.Emplace<Animator2D>(q6);
    a6.clipId = 0x77u;
    a6.nextClipId = 0x88u;
    a6.fadeRemain = -1.0f;
    // Play(0x99, loop=true)：切段 + 清队列 + 归零
    a6.clipId = 0x99u;
    a6.time = 0.0f;
    a6.playOnStart = 1;
    a6.nextClipId = 0;
    a6.fadeRemain = 0.0f;
    for (int i = 0; i < 5; ++i) w.Step(dt);
    Expect(a6.clipId == 0x99u && a6.time > 0.0f && a6.nextClipId == 0,
           "play clears pending queue and restarts");

    // ⑦ 队列目标未命中 clip 表 = 丢队列（当前段帧映射不受扰）
    Entity q7 = s.Create();
    SpriteRenderer& sr7 = s.Emplace<SpriteRenderer>(q7);
    Animator2D& a7 = s.Emplace<Animator2D>(q7);
    a7.clipId = 0x77u;
    a7.nextClipId = 0x999u; // 未登记
    a7.fadeRemain = 1.0f * dt;
    for (int i = 0; i < 10; ++i) w.Step(dt);
    Expect(a7.nextClipId == 0 && a7.clipId == 0x77u && a7.curFrame == 1 && sr7.spriteId == 11u,
           "queue: unknown target dropped, current clip unaffected");

    // ⑧ 无 clip 表：队列整体旁路——M2 逐位不变 + 队列字段原样保留
    {
        World w2;
        w2.InstallDefaultSystems();
        Scene& s2 = w2.CreateScene("m2queue");
        w2.SetActiveScene(&s2);
        Entity e1 = s2.Create();
        Animator2D& b1 = s2.Emplace<Animator2D>(e1);
        b1.clipId = 0x77u; // 表空 → M2 路径
        b1.nextClipId = 0x88u;
        b1.fadeRemain = -1.0f;
        Entity e2 = s2.Create();
        Animator2D& b2 = s2.Emplace<Animator2D>(e2);
        b2.clipId = 0x77u;
        for (int i = 0; i < 120; ++i) w2.Step(dt);
        Expect(b1.time >= 0.0f && b1.time < 1.0f && b2.time == b1.time && b1.nextClipId == 0x88u &&
                   b1.fadeRemain == -1.0f,
               "queue: empty clip table bypasses queue (M2 bit-identical)");
    }

    // ⑨ 孪生世界：队列演化确定性（含运行中段切换指令——两侧同码执行）
    auto run = [](uint64_t& hashOut) {
        World world;
        world.InstallDefaultSystems();
        Scene& sc = world.CreateScene("detq");
        world.SetActiveScene(&sc);
        world.Clips().Add(0x77u, {10u, 11u, 12u}, 8.0f, true);
        world.Clips().Add(0x88u, {20u, 21u}, 8.0f, true);
        Entity victim = sc.Create();
        sc.Emplace<SpriteRenderer>(victim);
        Animator2D& v = sc.Emplace<Animator2D>(victim);
        v.clipId = 0x77u;
        Entity fader = sc.Create();
        Animator2D& f = sc.Emplace<Animator2D>(fader);
        f.clipId = 0x88u;
        f.loop = 0;
        for (int i = 0; i < 300; ++i) {
            if (i == 30) { // 受击组合拳：Play(hit) + Queue(walk)
                v.loop = 0;
                v.time = 0.0f;
                v.nextClipId = 0x88u;
                v.nextLoop = 1;
                v.fadeRemain = -1.0f;
            }
            if (i == 100) f.nextClipId = 0x77u, f.nextLoop = 1, f.fadeRemain = 0.05f;
            world.Step(1.0f / 60.0f);
        }
        hashOut = ComputeStateHash(sc);
    };
    uint64_t qa = 0, qb = 0;
    run(qa);
    run(qb);
    Expect(qa == qb, "anim queue twin worlds: identical state hash");
}

// ---- M6a 批② T3b-2：PingPong 往返帧映射（纯函数；0/1 旧语义由既有覆盖）----

void TestVerifyAnimatorPingPong() {
    World w;
    w.InstallDefaultSystems();
    Scene& s = w.CreateScene("pp");
    w.SetActiveScene(&s);
    w.Clips().Add(0x99u, {20u, 21u, 22u, 23u}, 1.0f, true);

    Entity e = s.Create();
    SpriteRenderer& sr = s.Emplace<SpriteRenderer>(e);
    sr.spriteId = 999u;
    Animator2D& a = s.Emplace<Animator2D>(e);
    a.clipId = 0x99u;
    a.loop = 2; // PingPong

    // dt=1s、fps=1 → 第 t 步后 pos=t。period=2(n-1)=6：0..3..0 往返（含 t=0 起点）
    const int seq[16] = {1, 2, 3, 2, 1, 0, 1, 2, 3, 2, 1, 0, 1, 2, 3, 2};
    bool allOk = true;
    for (int i = 0; i < 16; ++i) {
        w.Step(1.0f);
        if (a.curFrame != (uint16_t)seq[i] || sr.spriteId != 20u + (uint32_t)seq[i]) allOk = false;
    }
    Expect(allOk, "clip: pingpong frame sequence 1,2,3,2,1,0 ...");
    Expect(a.time < 6.0f, "clip: pingpong time bounded by period 2(n-1)/fps");

    // 单帧 clip PingPong 防御：恒帧 0（period 0 路径）
    w.Clips().Add(0x9Au, {30u}, 1.0f, true);
    a.clipId = 0x9Au;
    for (int k = 0; k < 5; ++k) w.Step(1.0f);
    Expect(a.curFrame == 0 && sr.spriteId == 30u, "clip: single-frame pingpong stays 0");
}

// ---- A 档补间单元（2026-09-28 用户插入项）：TweenTable 建链校验/缓动精确值/
// Yoyo 折返/Once 完成恰一事件/同字段顶替/颜色字节插值/Kill 三通道/销毁自清。
// C# ABI 端到端在 script-tests TestTweenSdk（探针 typeId 15）。----

void TestTweenTable() {
    WorldDesc d;
    d.threadCount = 1;
    World w(d);
    Scene& s = w.CreateScene("tween");
    w.SetActiveScene(&s);
    TweenTable& tt = w.Tweens();

    Entity e = s.Create();
    s.Emplace<Transform2D>(e); // scale=(1,1)、pos=(0,0) 默认
    const float to[4] = {2.0f, 2.0f, 0.0f, 0.0f};

    // 拒建：字段名未命中 / 白名单外类型（Meta.tag = Blob24）/ 组件缺
    Expect(tt.Create(s, e, 0, "nope", to, 1.0f, TweenEase::Linear, TweenMode::Once) == 0,
           "tween: unknown field rejected");
    Expect(tt.Create(s, e, 3, "tag", to, 1.0f, TweenEase::Linear, TweenMode::Once) == 0,
           "tween: non-animatable type rejected");
    {
        Entity bare = s.Create();
        Expect(tt.Create(s, bare, 0, "scale", to, 1.0f, TweenEase::Linear, TweenMode::Once) == 0,
               "tween: missing component rejected");
    }

    // 建立与顶替（同实体同字段 = 新句柄接掌，旧句柄亡）
    const uint64_t h1 = tt.Create(s, e, 0, "scale", to, 1.0f, TweenEase::OutCubic, TweenMode::Once);
    Expect(h1 != 0 && tt.Alive(h1) && tt.Count() == 1, "tween: created");
    const uint64_t h2 = tt.Create(s, e, 0, "scale", to, 1.0f, TweenEase::OutCubic, TweenMode::Once);
    Expect(h2 != 0 && h2 != h1 && !tt.Alive(h1) && tt.Count() == 1, "tween: same field replaced");

    // OutCubic 精确中值：e(0.5) = 1+(-0.5)³ = 0.875 → scale 1→2 = 1.875
    tt.Advance(w, s, 0.5f);
    Expect(s.Get<Transform2D>(e).scale.x == 1.875f, "tween: OutCubic(0.5) exact 1.875");

    // Once 完成：终值精确 + 事件恰一次 + 条目移除
    tt.Advance(w, s, 0.6f); // elapsed 1.1 ≥ 1
    Expect(!tt.Alive(h2) && tt.Count() == 0, "tween: once removed");
    Expect(s.Get<Transform2D>(e).scale.x == 2.0f, "tween: final exact");
    Expect(w.Events().Size() == 1, "tween: one TweenFinished queued");

    // Yoyo 折返：pos 0→10（1s），1.25 → 三角 0.75 → 7.5；永续无完成事件
    const float toPos[4] = {10.0f, 0.0f, 0.0f, 0.0f};
    const uint64_t hy = tt.Create(s, e, 0, "pos", toPos, 1.0f, TweenEase::Linear, TweenMode::Yoyo);
    tt.Advance(w, s, 1.25f);
    Expect(s.Get<Transform2D>(e).pos.x == 7.5f, "tween: yoyo fold 7.5");
    Expect(tt.Alive(hy), "tween: yoyo stays alive");
    Expect(w.Events().Size() == 1, "tween: yoyo fires no finish");

    // 颜色字节插值：0xFF0000FF → 0xFFFFFFFF，t=0.5 → g/b = 127.5 四舍五入 128
    {
        Entity c = s.Create();
        SpriteRenderer& sr = s.Emplace<SpriteRenderer>(c);
        sr.colorRGBA = 0xFF0000FFu;
        const float toCol[4] = {255.0f, 255.0f, 255.0f, 255.0f};
        tt.Create(s, c, 5, "colorRGBA", toCol, 1.0f, TweenEase::Linear, TweenMode::Once);
        tt.Advance(w, s, 0.5f);
        Expect(s.Get<SpriteRenderer>(c).colorRGBA == 0xFF8080FFu, "tween: color bytes 0x80");
    }

    // Kill 三通道 + 陈旧句柄
    Expect(tt.KillField(e, 0, "pos") == 1 && !tt.Alive(hy), "tween: kill field");
    Expect(tt.KillField(e, 0, "pos") == 0, "tween: kill idempotent");
    Expect(!tt.Alive(hy + 12345), "tween: stale handle dead");

    // 实体销毁自清（两阶段：提交后随 Advance 消失）
    {
        Entity v = s.Create();
        s.Emplace<Transform2D>(v);
        tt.Create(s, v, 0, "scale", to, 1.0f, TweenEase::Linear, TweenMode::Once);
        s.Destroy(v);
        s.CommitDestroys();
        tt.Advance(w, s, 0.5f);
        Expect(tt.Count() == 0, "tween: destroyed entity swept");
    }

    tt.Clear();
    Expect(tt.Count() == 0, "tween: clear");
}

// ---- 批①：FxChannel（飘字池淘汰/上浮淡出、血条覆写/sticky、产包数学）----

void TestVerifyFxChannel() {
    // ① 飘字环形池：满池后最老者淘汰（第 kMaxTexts+1 条覆写第 1 条槽位；容量引
    // 用常量——提额时本测试自动跟随，M7c 批④ 256→512 实证）
    {
        FxChannel fx;
        char buf[8];
        const int cap = (int)FxChannel::kMaxTexts;
        for (int i = 0; i < cap + 1; ++i) {
            std::snprintf(buf, sizeof(buf), "%d", i);
            fx.PopupText(buf, (float)i, 0.0f);
        }
        Expect(fx.TextCount() == FxChannel::kMaxTexts, "fx: text pool capped at kMaxTexts");
        Expect(std::string(fx.TextAt(0).text) == "1" &&
                   std::string(fx.TextAt(cap - 1).text) == std::to_string(cap),
               "fx: oldest text evicted, order preserved");
        // 长文本 16 字符截断
        fx.PopupText("01234567890123456789", 0, 0);
        Expect(std::string(fx.TextAt(cap - 1).text) == "012345678901234",
               "fx: text truncated to 15 chars");
    }
    // ② Simulate：上浮（前 70% 匀升）/淡出（末 30%）/到期回收
    {
        FxChannel fx;
        fx.PopupText("12", 100.0f, 50.0f);
        fx.Simulate(0.4f);
        const FxText& t = fx.TextAt(0);
        const float riseMid = fx.TextRise(t), alphaMid = fx.TextAlpha(t);
        fx.Simulate(0.36f); // age 0.76（末 30% 窗内）
        const float riseLate = fx.TextRise(t), alphaLate = fx.TextAlpha(t);
        Expect(riseMid < 0.0f && riseMid > -FxChannel::kTextRise && alphaMid == 1.0f &&
                   riseLate == -FxChannel::kTextRise && alphaLate < 1.0f && alphaLate > 0.0f,
               "fx: rise ramps then holds (Y-down: rise = -y); alpha fades in last 30%");
        fx.Simulate(0.05f); // age 0.81 ≥ life 0.8 → 回收
        Expect(fx.TextCount() == 0, "fx: expired text recycled");
    }
    // ③ 血条键控覆写刷新（age 归零续命）；④ sticky 过期释放
    {
        FxChannel fx;
        fx.Bar(0xAAu, 0.5f);
        fx.Simulate(1.0f);
        fx.Bar(0xAAu, 0.8f, 0xFF00FF00u, 48.0f);
        const FxBar* b = nullptr;
        for (const FxBar& s : fx.Bars())
            if (s.entity == 0xAAu) b = &s;
        Expect(fx.BarCount() == 1 && b && b->age == 0.0f && b->frac == 0.8f && b->width == 48.0f &&
                   b->color == 0xFF00FF00u,
               "fx: bar refresh resets age and updates fields");
        fx.Simulate(FxChannel::kBarSticky + 0.01f);
        Expect(fx.BarCount() == 0, "fx: bar expires after sticky window");
    }
    // ⑤ 血条槽满淘汰最旧（129 实体 → age 最大者让位）
    {
        FxChannel fx;
        for (uint64_t e = 1; e <= 128; ++e) fx.Bar(e, 0.5f);
        fx.Simulate(1.0f);
        fx.Bar(0xE1u, 0.1f); // 全满 → 淘汰 age 最大（= entity 1，最先入）
        bool hasNew = false, evictedOld = true;
        for (const FxBar& s : fx.Bars()) {
            if (s.entity == 0xE1u) hasNew = true;
            if (s.entity == 1u) evictedOld = false;
        }
        Expect(fx.BarCount() == 128 && hasNew && evictedOld,
               "fx: full bar pool evicts least-recently-refreshed");
    }
    // ⑥ ExtractBarQuads 数学：bg 整宽居中 + fg 比例宽左锚 + 悬空跳过 + 视口剔除
    {
        FxChannel fx;
        fx.Bar(0x100u, 0.5f, 0xFF30B0F0u, 32.0f); // 在视野内
        fx.Bar(0x101u, 1.0f, 0xFF30B0F0u, 32.0f); // 视野外（右侧远处）
        fx.Bar(0x102u, 1.0f, 0xFF30B0F0u, 32.0f); // 悬空实体（resolve false）
        auto resolve = [](uint64_t e, Vec2& out, float& outTop) {
            if (e == 0x100u) {
                out = {0.0f, 0.0f};
                outTop = 12.0f; // 精灵头顶（贴头顶锚定断言）
                return true;
            }
            if (e == 0x101u) {
                out = {5000.0f, 0.0f};
                outTop = 0.0f;
                return true;
            }
            return false; // 0x102 悬空
        };
        FxQuad q[8];
        const uint32_t n =
            fx.ExtractBarQuads(q, 8, resolve, Rect{Vec2{-320, -180}, Vec2{320, 180}});
        Expect(n == 2, "fx: quads = 2 (viewport + dangling filtered)");
        const FxQuad& bg = q[0];
        const FxQuad& fg = q[1];
        Expect(bg.size.x == 32.0f && bg.size.y == FxChannel::kBarHeight &&
                   bg.color == FxChannel::kBarBgColor && bg.center.x == 0.0f,
               "fx: bg quad full width centered");
        ExpectNear(bg.center.y, -12.0f * 0.5f - FxChannel::kBarHeight * 0.5f - 2.0f, 1e-4f,
                   "fx: bar anchored above sprite top (Y-down: head = center - outTop/2)");
        Expect(fg.size.x == 16.0f && fg.center.x == -8.0f && fg.color == 0xFF30B0F0u,
               "fx: fg quad proportional width left-anchored");
    }
}

// ---- M7c 批① S2/S3：Fx 表现升级数学（lag 收敛/UV 裁剪区间/Pop 曲线/寿命覆盖）----
void TestFxPresentationUpgradeMath() {
    // ① 延迟条 lagFrac：掉血线性收敛向 frac；回升贴平（fg 全盖不可见语义）
    {
        FxChannel fx;
        fx.BarEx(0x200u, 1.0f, 0xFF30B0F0u, 40.0f, 0, 0, 0xFFE0F0F0u, 6.0f);
        const FxBar* b = nullptr;
        for (const FxBar& s : fx.Bars())
            if (s.entity == 0x200u) b = &s;
        Expect(b && b->lagFrac == 1.0f && b->height == 6.0f && b->lagColor != 0,
               "fx: bar-ex birth has no fake lag");
        fx.BarEx(0x200u, 0.2f, 0xFF30B0F0u, 40.0f, 0, 0, 0xFFE0F0F0u, 6.0f); // 掉血
        float prevLag = 1.0f;
        bool monotone = true, converged = false;
        for (int i = 0; i < 150; ++i) { // 2.5s @60fps：138 步收敛，全程在 sticky 3s 内
            fx.Simulate(1.0f / 60.0f);
            if (b->lagFrac > prevLag + 1e-6f) monotone = false;
            prevLag = b->lagFrac;
            if (std::fabs(b->lagFrac - 0.2f) < 1e-4f) converged = true;
        }
        Expect(monotone, "fx: lag descends monotonically after damage");
        Expect(converged && std::fabs(b->lagFrac - 0.2f) < 1e-4f,
               "fx: lag converges to frac");
        fx.BarEx(0x200u, 0.9f, 0xFF30B0F0u, 40.0f, 0, 0, 0xFFE0F0F0u, 6.0f); // 回升
        fx.Simulate(1.0f / 60.0f);
        Expect(std::fabs(b->lagFrac - 0.9f) < 1e-5f, "fx: heal snaps lag to frac");
    }
    // ② 贴图产包：bg 全幅 + lag/fg 横向 UV 裁剪区间（uFrac = 比例，防压扁语义）
    {
        FxChannel fx;
        fx.BarEx(0x300u, 1.0f, 0xFF30B0F0u, 64.0f, 0x11u, 0x22u, 0xFFE0E0E0u, 8.0f);
        fx.BarEx(0x300u, 0.5f, 0xFF30B0F0u, 64.0f, 0x11u, 0x22u, 0xFFE0E0E0u, 8.0f);
        fx.Simulate(1.0f); // 掉血后 1s：lagFrac ≈ 1-0.35 = 0.65 > frac
        auto resolve = [](uint64_t, Vec2& out, float& outTop) {
            out = {0.0f, 0.0f};
            outTop = 0.0f;
            return true;
        };
        FxQuad q[4];
        const uint32_t n = fx.ExtractBarQuads(q, 4, resolve, Rect{Vec2{-320, -180}, Vec2{320, 180}});
        Expect(n == 3, "fx: textured bar emits bg+lag+fg");
        Expect(q[0].spriteId == 0x11u && q[0].uFrac == 1.0f && q[0].color == 0xFFFFFFFFu,
               "fx: bg full-bleed texture");
        Expect(q[1].spriteId == 0x22u && q[1].color == 0xFFE0E0E0u && q[1].uFrac > 0.5f,
               "fx: lag textured with lagColor, uFrac trails");
        Expect(q[2].spriteId == 0x22u && q[2].uFrac == 0.5f && q[2].size.x == 32.0f,
               "fx: fg u-clipped at frac (width proportional)");
        // 无 lag（白精灵现状路径）：2 quads、spriteId 0
        FxChannel fx2;
        fx2.Bar(0x301u, 0.5f);
        const uint32_t n2 = fx2.ExtractBarQuads(q, 4, resolve, Rect{Vec2{-320, -180}, Vec2{320, 180}});
        Expect(n2 == 2 && q[0].spriteId == 0 && q[1].uFrac == 0.5f,
               "fx: legacy bar path unchanged");
    }
    // ③ Pop 曲线：出生 1.4× 回落 1.0、上浮比 Linear 陡；漂移恒速；寿命覆盖
    {
        FxText pop{}, lin{};
        pop.curve = FxCurve::Pop;
        pop.life = 1.0f;
        lin.life = 1.0f;
        float dx, dy, sc, al;
        pop.age = 0.0f;
        FxChannel::TextMotion(pop, dx, dy, sc, al);
        ExpectNear(sc, 1.0f + FxChannel::kPopScaleBoost, 1e-4f, "pop scale born at boost");
        ExpectNear(dy, 0.0f, 1e-4f, "pop born at anchor");
        pop.age = 0.5f; // 窗口（0.35）外
        FxChannel::TextMotion(pop, dx, dy, sc, al);
        ExpectNear(sc, 1.0f, 1e-4f, "pop scale settles to 1.0");
        lin.age = 0.5f;
        float ldx, ldy, lsc, lal;
        FxChannel::TextMotion(lin, ldx, ldy, lsc, lal);
        Expect(dy < ldy, "pop rise steeper than linear at mid-life (Y-down: rise = -y)");
        pop.driftX = 30.0f;
        pop.age = 0.4f;
        FxChannel::TextMotion(pop, dx, dy, sc, al);
        ExpectNear(dx, 12.0f, 1e-4f, "drift = velocity × age");
        // 寿命覆盖：life 2.0 的条目在 0.81s（默认寿命+ε）后仍在场且过期即隐形
        FxChannel fx;
        fx.PopupTextEx("2s", 0, 0, 0xFFFFFFFFu, 1.0f, 2.0f, 0.0f, FxCurve::Linear);
        fx.PopupText("d", 10, 0); // 默认 0.8s
        fx.Simulate(0.81f);
        Expect(fx.TextCount() == 2, "fx: per-text life honored");
        Expect(std::string(fx.TextAt(0).text).empty() == false,
               "fx: long-life text alive");
        Expect(std::string(fx.TextAt(1).text).empty(), "fx: expired text hidden in place");
        fx.Simulate(1.2f); // 总 2.01s ≥ 2.0：长寿命条到寿回收，早已隐形的短寿命条
        // 一并从队首滑出（环形计数回收逐条推进到未过期队首为止）
        Expect(fx.TextCount() == 0, "fx: long-life text recycled at its own life");
    }
    // ④ 池语义不变：PopupTextEx 满池仍最老者淘汰（S3 登记不排口径的回归锚）
    {
        FxChannel fx;
        char buf[8];
        const int cap = (int)FxChannel::kMaxTexts;
        for (int i = 0; i < cap + 1; ++i) {
            std::snprintf(buf, sizeof(buf), "%d", i);
            fx.PopupTextEx(buf, 0, 0, 0xFFFFFFFFu, 1.5f, 1.2f, 5.0f, FxCurve::Pop);
        }
        Expect(fx.TextCount() == FxChannel::kMaxTexts, "fx: ex pool capped same as legacy");
    }
}

// ---- Stat：到期压缩保序 + xpToNext=0 终止性 ----

void TestVerifyStatEffectsAndXp() {
    World w;
    w.InstallDefaultSystems();
    Scene& s = w.CreateScene("stat");
    w.SetActiveScene(&s);
    Entity e = s.Create();
    StatusEffects& st = s.Emplace<StatusEffects>(e);
    st.active[0] = StatusInst{1, 1, 0.01f, 0}; // 本帧内到期
    st.active[1] = StatusInst{2, 3, 0.50f, 0};
    st.count = 2;
    Entity x = s.Create();
    XpProgress& xp = s.Emplace<XpProgress>(x);
    xp.xp = 5.0f;
    xp.xpToNext = 0.0f; // 资产配 0：修复前挂死，回归防线

    w.Step(1.0f / 60.0f);
    Expect(st.count == 1, "stat: expired effect removed");
    Expect(st.active[0].id == 2 && st.active[0].stacks == 3, "stat: compaction preserves order");
    ExpectNear(st.active[0].remain, 0.50f - 1.0f / 60.0f, 1e-4f, "stat: remain ticks");

    w.Step(1.0f / 60.0f); // 能走到这里 = xp 环已终止
    const XpProgress& after = s.Get<XpProgress>(x);
    Expect(after.level >= 2, "stat: level advanced");
    Expect(after.xp < after.xpToNext, "stat: xp below threshold");
}

// ---- Movement：击退衰减积分 + 边界钳制 ----

void TestVerifyMovementKnockbackAndClamp() {
    {
        World w;
        w.InstallDefaultSystems();
        Scene& s = w.CreateScene("kb");
        w.SetActiveScene(&s);
        Entity e = s.Create();
        s.Emplace<Transform2D>(e, Transform2D{{0, 0}});
        s.Emplace<Velocity>(e);
        Knockback& kb = s.Emplace<Knockback>(e);
        kb.impulse = {200, 0};
        kb.decay = 8.0f;
        for (int i = 0; i < 60; ++i) w.Step(1.0f / 60.0f);
        float x = s.Get<Transform2D>(e).pos.x;
        // 离散积分：Σ v0·rⁱ·dt = v0·dt/(1−e^(−decay·dt)) ≈ 26.7（连续理想 25，
        // ~7% 离散滞后属可接受近似，非 bug；impulse 项 exp 衰减同源两侧一致）
        Expect(x > 26.0f && x < 27.5f, "movement: discrete impulse integral");
        Expect(Length(s.Get<Knockback>(e).impulse) < 0.5f, "movement: impulse decayed out");
    }
    {
        World w;
        w.InstallDefaultSystems();
        w.SetBounds(Rect{{0, 0}, {100, 100}});
        Scene& s = w.CreateScene("clamp");
        w.SetActiveScene(&s);
        Entity e = s.Create();
        s.Emplace<Transform2D>(e, Transform2D{{150, 50}});
        s.Emplace<Velocity>(e);
        s.Get<Velocity>(e).v = {10, 0};
        w.Step(1.0f / 60.0f);
        Expect(s.Get<Transform2D>(e).pos.x == 100.0f, "movement: clamped to bounds");
    }
}

// ---- ProjectileLifetime：寿命到期两帧提交节奏 + 越界回收 ----

void TestVerifyProjectileLifetime() {
    {
        World w;
        w.InstallDefaultSystems();
        Scene& s = w.CreateScene("pl");
        w.SetActiveScene(&s);
        Entity p = s.Create();
        s.Emplace<Transform2D>(p, Transform2D{{0, 0}});
        Projectile& pr = s.Emplace<Projectile>(p);
        pr.lifetime = 0.03f; // 2 帧
        w.Step(1.0f / 60.0f);
        Expect(s.Alive(p), "proj: alive on frame 1");
        w.Step(1.0f / 60.0f);
        Expect(s.Alive(p), "proj: queued destroy keeps alive this frame");
        w.Step(1.0f / 60.0f);
        Expect(!s.Alive(p), "proj: committed at next Essential");
    }
    {
        World w;
        w.InstallDefaultSystems();
        w.SetBounds(Rect{{0, 0}, {10, 10}});
        Scene& s = w.CreateScene("pb");
        w.SetActiveScene(&s);
        Entity p = s.Create();
        s.Emplace<Transform2D>(p, Transform2D{{500, 500}});
        s.Emplace<Projectile>(p);
        w.Step(1.0f / 60.0f);
        Expect(s.Alive(p), "proj: out-of-bounds queued");
        w.Step(1.0f / 60.0f);
        Expect(!s.Alive(p), "proj: out-of-bounds committed");
    }
}

// ---- 并行销毁稳定归并（review 2026-10-02 #2）：chunk 分桶归并 = 串行序 ----
// 同帧多 chunk 投射物半数到期，多 worker 档与单线程档的状态哈希必须逐位一致。
// 修复前 worker 直接 Destroy：入队序 = 锁获取序（跨线程漂移）→ 提交序漂移 →
// 池 swap_and_pop 终态与幸存实体 packed 序不定。半数存活使池布局差异进哈希
// （全灭则两档池皆空、顺序不可见）；偶数下标到期使每个 chunk 都有意图。

void TestVerifyParallelDestroyDeterminism() {
    auto run = [](int threads) {
        WorldDesc d;
        d.seed = 7;
        d.threadCount = threads;
        World w(d);
        Scene& s = w.CreateScene("pd");
        w.SetActiveScene(&s);
        for (int i = 0; i < 4096; ++i) { // 16 chunks @ grain 256
            Entity e = s.Create();
            s.Emplace<Transform2D>(e, Transform2D{{(float)(i % 32), (float)(i / 32)}});
            Projectile& pr = s.Emplace<Projectile>(e);
            pr.lifetime = 1.0f;
            pr.age = (i % 2 == 0) ? 1.0f : 0.0f; // 偶下标当帧到期
        }
        ProjectileLifetimeSystem sys;
        sys.Tick(w, s, 1.0f / 60.0f);
        s.CommitDestroys();
        Expect(s.AliveCount() == 2048, "pdestroy: half survived");
        // 槽回收序也须一致：销毁后再创建（entt 从 free_list 取最近销毁槽）
        for (int i = 0; i < 64; ++i) {
            Entity e = s.Create();
            s.Emplace<Transform2D>(e, Transform2D{{0, 0}});
            Projectile& pr = s.Emplace<Projectile>(e);
            pr.lifetime = 9.0f;
        }
        return ComputeStateHash(s);
    };
    const uint64_t hSt = run(1);
    const uint64_t hMt = run(8);
    Expect(hSt == hMt, "pdestroy: mt hash == st hash");
}

// ---- Spawn 配额：maxAlive 有界且稳定 ----

void TestVerifySpawnQuota() {
    World w;
    w.InstallDefaultSystems();
    w.SetSpawnFn([](Scene& s, uint32_t, Vec2 pos, uint32_t team) {
        Entity e = s.Create();
        s.Emplace<Transform2D>(e, Transform2D{pos});
        s.Emplace<Meta>(e).team = team;
        return e;
    });
    Scene& s = w.CreateScene("quota");
    w.SetActiveScene(&s);
    Entity sp = s.Create();
    s.Emplace<Transform2D>(sp, Transform2D{{0, 0}});
    s.Emplace<Meta>(sp).team = 9; // spawner 自身不入 spawnTeam 存量
    Spawner& cfg = s.Emplace<Spawner>(sp);
    cfg.prefabId = 1;
    cfg.interval = 0.01f;
    cfg.burst = 10;
    cfg.maxAlive = 3;
    cfg.spawnTeam = 5;

    uint32_t n = 0;
    for (int i = 0; i < 40; ++i) {
        w.Step(1.0f / 60.0f);
        n = 0;
        s.View<Meta>().each([&](auto, Meta& mt) {
            if (mt.team == 5) ++n;
        });
    }
    Expect(n == 3, "spawn: maxAlive quota enforced and stable");
}

// ---- Trigger：once 字段语义（显式 fired，不再走 _pad）----

void TestVerifyTriggerOnceSemantics() {
    World w;
    w.InstallDefaultSystems();
    w.Teams().SetRelation(0, 0, TeamRelation::Friendly); // 同队非 ghost 可触发
    Scene& s = w.CreateScene("trig");
    w.SetActiveScene(&s);

    Entity t1 = s.Create();
    s.Emplace<Transform2D>(t1, Transform2D{{0, 0}});
    s.Emplace<Meta>(t1).team = 0;
    Trigger2D& g1 = s.Emplace<Trigger2D>(t1);
    g1.triggerId = 101;
    g1.once = 1;
    g1.radius = 40.0f;
    // 触发器分开放置：本块验证 once/Exit 语义本身；共置互触发见下方 [ISSUE-7] 块。
    Entity t2 = s.Create();
    s.Emplace<Transform2D>(t2, Transform2D{{300, 0}});
    s.Emplace<Meta>(t2).team = 0;
    Trigger2D& g2 = s.Emplace<Trigger2D>(t2);
    g2.triggerId = 202;
    g2.once = 0;
    g2.radius = 40.0f;
    Entity v = s.Create();
    s.Emplace<Transform2D>(v, Transform2D{{10, 0}});
    s.Emplace<Meta>(v).team = 0;

    int enter1 = 0, enter2 = 0, exit1 = 0, exit2 = 0;
    w.SetEventSink([&](World&, const EventPacket& p) {
        if (p.type == GameEvent::TriggerEnter && p.src == t1) ++enter1;
        if (p.type == GameEvent::TriggerEnter && p.src == t2) ++enter2;
        if (p.type == GameEvent::TriggerExit && p.src == t1) ++exit1;
        if (p.type == GameEvent::TriggerExit && p.src == t2) ++exit2;
    });

    w.Step(1.0f / 60.0f); // 进入 t1
    s.Get<Transform2D>(v).pos = {300, 0};
    w.Step(1.0f / 60.0f); // t1 出（once 不报 Exit）+ t2 进
    s.Get<Transform2D>(v).pos = {10, 0};
    w.Step(1.0f / 60.0f); // t1 再进（once 抑制）+ t2 出
    s.Get<Transform2D>(v).pos = {300, 0};
    w.Step(1.0f / 60.0f); // t2 再进

    Expect(enter1 == 1, "trigger: once fires exactly once");
    Expect(exit1 == 0, "trigger: once suppresses exit");
    Expect(enter2 == 2, "trigger: re-entry fires again");
    Expect(exit2 == 1, "trigger: normal exit reported");

    // [ISSUE-7 已修复] 共置触发器互不触发（修复前互为"非 ghost 进入者"：
    // 开局即互发假 Enter、anyInside 恒真、Exit 永不产生）
    {
        World w2;
        w2.InstallDefaultSystems();
        w2.Teams().SetRelation(0, 0, TeamRelation::Friendly);
        Scene& s2 = w2.CreateScene("trig2");
        w2.SetActiveScene(&s2);
        Entity a = s2.Create();
        s2.Emplace<Transform2D>(a, Transform2D{{0, 0}});
        s2.Emplace<Meta>(a).team = 0;
        s2.Emplace<Trigger2D>(a).radius = 40.0f;
        Entity b = s2.Create();
        s2.Emplace<Transform2D>(b, Transform2D{{10, 0}});
        s2.Emplace<Meta>(b).team = 0;
        s2.Emplace<Trigger2D>(b).radius = 40.0f;
        int enters = 0;
        w2.SetEventSink([&](World&, const EventPacket& p) {
            if (p.type == GameEvent::TriggerEnter) ++enters;
        });
        w2.Step(1.0f / 60.0f);
        w2.Step(1.0f / 60.0f);
        Expect(enters == 0, "trigger: co-located triggers don't fire each other");
        Entity v = s2.Create(); // 真实访客进入两域
        s2.Emplace<Transform2D>(v, Transform2D{{5, 0}});
        s2.Emplace<Meta>(v).team = 0;
        w2.Step(1.0f / 60.0f);
        Expect(enters == 2, "trigger: real visitor enters both zones");
    }
}

} // namespace

void RunGameplayTests() {
    TestVerifyIframesDecrementAndKill();
    TestVerifyHitMemoryAndPierce();
    TestProjectileSweepAntiTunnel(); // M6：高速弹扫掠防穿透（b11b）
    TestVerifyMagnetAndPickup();
    TestVerifyPickupXpLevelUp();
    TestWaveDirectorWavesAndEvent();
    TestWaveDirectorRampAndOverlap();
    TestWaveDirectorCapAlive();
    TestWaveDirectorTimeScaleFreeze();
    TestSpawnFreezeNoLeak();
    TestWaveDirectorArchive();
    TestWaveDirectorDeterminism();
    TestVerifyTimeScale();
    TestNoDoubleDeathEvents();
    TestVerifyAISystemChase();
    TestVerifyFleeAndPatrol();
    TestVerifyAnimatorAdvance();
    TestVerifyAnimatorFrameMapping();
    TestVerifyAnimatorQueue();
    TestVerifyAnimatorPingPong();
    TestTweenTable();
    TestVerifyFxChannel();
    TestFxPresentationUpgradeMath(); // M7c 批① S2/S3：lag/UV 裁剪/Pop/寿命
    TestVerifyStatEffectsAndXp();
    TestVerifyMovementKnockbackAndClamp();
    TestVerifyProjectileLifetime();
    TestVerifyParallelDestroyDeterminism();
    TestVerifySpawnQuota();
    TestVerifyTriggerOnceSemantics();
}
