// Lemon 引擎单测 — EcsTests — ECS
// 域（场景/World/组件目录/存档/空间哈希/TargetBoard/系统管线/存档通道/池护栏）（M7c 批⓪ T2 自
// engine_tests.cpp 按域拆出，函数体逐字节原样搬运； include/using 为全 TU
// 共享全集——跨域头依赖零编译风险，IWYU 精简不做）

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

void TestSceneLifecycle() {
    World world;
    Scene& scene = world.CreateScene("Arena");
    world.SetActiveScene(&scene);

    // 创建 + 组件
    Entity e = scene.Create();
    Expect(scene.Alive(e), "entity alive");
    auto& tf = scene.Emplace<Transform2D>(e, Transform2D{{10, 20}});
    tf.rot = 0.5f;
    Expect(scene.Has<Transform2D>(e), "has component");
    Expect(scene.Get<Transform2D>(e).pos == Vec2(10, 20), "component roundtrip");
    Expect(scene.TryGet<Velocity>(e) == nullptr, "tryget missing is null");

    // 两阶段销毁：Destroy 后当帧仍可访问，Commit 后才消失
    scene.Destroy(e);
    Expect(scene.Alive(e), "deferred destroy keeps alive in-frame");
    Expect(scene.PendingDestroyCount() == 1, "destroy queued");
    scene.CommitDestroys();
    Expect(!scene.Alive(e), "commit destroys entity");
    Expect(scene.PendingDestroyCount() == 0, "queue drained");
    Expect(scene.DestroyedTotal() == 1, "destroy counter");

    // 重复入队幂等
    Entity f = scene.Create();
    scene.Destroy(f);
    scene.Destroy(f);
    scene.CommitDestroys();
    Expect(scene.DestroyedTotal() == 2, "duplicate destroy idempotent");

    // 实体 id 回收（EnTT version 位前进：旧句柄失活）
    Entity g = scene.Create();
    Expect(!scene.Alive(e), "stale handle invalid after recycle");
    Expect(scene.Alive(g), "new handle valid");
    Expect(scene.AliveCount() == 1, "alive count");
}

void TestWorldServices() {
    WorldDesc d;
    d.seed = 777;
    d.threadCount = 1;
    World world(d);
    Expect(world.Jobs().ThreadCount() == 1, "world owns jobs");
    Expect(world.ActiveScene() == nullptr, "no active scene initially");

    // 系统子流：同 id 同实例、不同 id 序列不同、与手动 Rng 同 seed 一致
    Rng& sys3 = world.SystemRng(3);
    Rng ref(777, kRngStreamBase + 3);
    Expect(sys3.Next() == ref.Next(), "system rng matches seed+stream");
    Rng& sys3Again = world.SystemRng(3);
    Expect(&sys3 == &sys3Again, "system rng cached");
    Rng& sys4 = world.SystemRng(4);
    Expect(sys4.Next() != sys3.Next(), "streams diverge");

    // 事件队列：入队 FIFO、帧末派发清空（派发端在块 6 系统）
    auto& q = world.Events();
    EventPacket p{};
    p.type = GameEvent::Hit;
    p.dst = Entity{42};
    p.payload[0] = 12.5f;
    q.Push(p);
    Expect(q.Size() == 1 && q.Front().payload[0] == 12.5f, "event queued");
    q.Clear();
    Expect(q.Empty(), "events drained");
}

void TestComponentRegistry() {
    RegisterAllComponents();
    auto& reg = ComponentRegistry::Instance();
    Expect(reg.Count() == 32,
           "catalog count (5 core + 4 render + 12 behavior + 6 gameplay + M5 批② WaveDirector + "
           "T3d AnimGraph/AnimParams + M6b 批③d 前置 UIDocument + M6c 批② AudioSource)");

    // 按 name 可查、id 稳定
    const ComponentMeta* tf = reg.Find("Transform2D");
    Expect(tf != nullptr && tf->fieldCount == 3, "transform meta");
    Expect(reg.Find("NoSuchComponent") == nullptr, "unknown name null");
    Expect(reg.At(tf->id).name == std::string_view("Transform2D"), "id lookup stable");

    // offset 元数据与真实布局一致（序列化正确性的前提）
    const ComponentMeta* chase = reg.Find("Chase");
    Expect(chase != nullptr && chase->sizeOf == sizeof(Chase), "chase size");
    for (uint16_t i = 0; i < chase->fieldCount; ++i) {
        const FieldMeta& f = chase->fields[i];
        Expect(f.offset + 4 <= chase->sizeOf, "field offset within struct");
    }
    const FieldMeta& speedField = *[](const ComponentMeta& m) {
        for (uint16_t i = 0; i < m.fieldCount; ++i)
            if (std::string_view(m.fields[i].name) == "speed") return m.fields + i;
        return m.fields;
    }(*chase);
    Chase sample;
    sample.speed = 123.0f;
    Expect(*(float*)((char*)&sample + speedField.offset) == 123.0f, "field offset deref");

    // 全组件 POD 校验（状态哈希/序列化的前提）
    Expect(std::is_trivially_copyable_v<Transform2D>, "transform trivial");
    Expect(std::is_trivially_copyable_v<Meta>, "meta trivial");
    Expect(std::is_trivially_copyable_v<Chase>, "chase trivial");
    Expect(std::is_trivially_copyable_v<Projectile>, "projectile trivial");
    Expect(std::is_trivially_copyable_v<StatusEffects>, "status trivial");
    Expect(std::is_trivially_copyable_v<Inventory>, "inventory trivial");

    // SpriteRenderer 默认启用（2026-09-21 回归：曾默认 flags=0 禁用 → 提取静默跳过，
    // Inspector Add Component / 脚本 Emplace 新增即不可见；NSDMI 经 Emplace 值初始化生效）
    Expect((SpriteRenderer{}.flags & kSrEnabled) != 0, "sprite flags default enabled");
    {
        World w;
        Scene& s = w.CreateScene("sr_defaults");
        Entity e = s.Create();
        s.Emplace<SpriteRenderer>(e);
        Expect((s.Get<SpriteRenderer>(e).flags & kSrEnabled) != 0,
               "sprite emplace default enabled");
    }
}

void TestVerifyWorldAutoRegistersCatalog() {
    // ISSUE-9 回归：World 构造即登记组件目录——bench-sim 曾漏调 RegisterAllComponents，
    // StateHash 遍历空注册表逐帧恒等，M2 回放验收恒真空转（M3-0 修复，2026-09-19）
    World world;
    Expect(ComponentRegistry::Instance().Count() == 32, "world ctor auto-registers catalog");
}

void TestSceneArchive() {
    RegisterAllComponents();
    World world;
    Scene& src = world.CreateScene("Arena01");

    // 怪物：Transform + Chase + Health + Meta(tag)
    Entity monster = src.Create();
    src.Emplace<Transform2D>(monster, Transform2D{{128, -64}, 0.25f, {2, 2}});
    Chase& chase = src.Emplace<Chase>(monster);
    chase.speed = 88.0f;
    chase.aggroRange = 400.0f;
    chase.targetTeam = 0;
    src.Emplace<Health>(monster, Health{.max = 200.0f, .cur = 150.0f, .iFrames = 0.5f});
    Meta& meta = src.Emplace<Meta>(monster);
    std::strcpy(meta.tag, "elite-01");

    // 父子（EntityRef roundtrip：parent 指向先出现的 monster）
    Entity child = src.Create();
    src.Emplace<Transform2D>(child, Transform2D{{1, 2}});
    Hierarchy& h = src.Emplace<Hierarchy>(child);
    h.parent = monster;

    std::string text = SceneArchive::Save(src);

    // 载入到新场景
    World world2;
    Scene& dst = world2.CreateScene("reload");
    Expect(SceneArchive::Load(dst, text), "scene load ok");
    // 场景名往返（M16：Load 原从不读回 doc["name"]，存"Arena01"再开回"reload"默认名）
    Expect(std::string(dst.Name()) == "Arena01", "scene name roundtrip restored");

    Expect(dst.AliveCount() == 2, "entity count roundtrip");
    // 找回组件（实体句柄会变，按组件数据定位）
    bool foundMonster = false, foundChild = false;
    dst.Each([&](Entity e) {
        if (auto* c = dst.TryGet<Chase>(e); c) {
            foundMonster = true;
            ExpectNear(c->speed, 88.0f, 1e-6f, "chase.speed roundtrip");
            ExpectNear(c->aggroRange, 400.0f, 1e-6f, "chase.aggro roundtrip");
            Expect(dst.Get<Health>(e).cur == 150.0f, "health.cur roundtrip");
            Expect(std::string_view(dst.Get<Meta>(e).tag) == "elite-01", "meta.tag roundtrip");
            const auto& tf = dst.Get<Transform2D>(e);
            Expect(tf.pos == Vec2(128, -64) && std::fabs(tf.rot - 0.25f) < 1e-6f &&
                       tf.scale == Vec2(2, 2),
                   "transform roundtrip");
        }
        if (auto* h2 = dst.TryGet<Hierarchy>(e); h2) {
            foundChild = true;
            Expect(!h2->parent.IsNull(), "hierarchy parent remapped");
            Expect(dst.Alive(h2->parent), "parent handle valid in new scene");
            Expect(dst.Has<Chase>(h2->parent), "parent points to monster");
        }
    });
    Expect(foundMonster && foundChild, "both entities located");

    // 二次 roundtrip 稳定（组件数据不动点；场景名自 M16 起随档往返，同样不动点）
    std::string text2 = SceneArchive::Save(dst);
    World world3;
    Scene& third = world3.CreateScene("reload");
    Expect(SceneArchive::Load(third, text2), "second load ok");
    Expect(SceneArchive::Save(third) == text2, "roundtrip is a fixed point");

    // 容错：未知组件跳过、坏 json 拒绝
    std::string withUnknown = R"({"schemaVersion":1,"name":"x","entities":[)"
                              R"({"components":{"FutureComponent":{"a":1},"Chase":{"speed":5}}}]})";
    World w4;
    Scene& s4 = w4.CreateScene("fwd");
    Expect(SceneArchive::Load(s4, withUnknown), "unknown component tolerated");
    Expect(!SceneArchive::Load(s4, "{ not json"), "invalid json rejected");
    Expect(!SceneArchive::Load(s4, R"({"name":"x","entities":[]})"), "missing version rejected");
}

void TestTeamTable() {
    TeamTable t = TeamTable::Default();
    Expect(t.Relation(0, 1) == TeamRelation::Hostile, "player vs monsters hostile");
    Expect(t.Relation(1, 0) == TeamRelation::Hostile, "relation symmetric");
    Expect(t.Relation(1, 1) == TeamRelation::SoftCollide, "monsters self soft-collide");
    Expect(t.Relation(0, 3) == TeamRelation::Ghost, "bullets ghost through player");
    Expect(t.Relation(1, 3) == TeamRelation::Hostile, "bullets hit monsters");
    // 未声明组合默认 Ghost（安全失败）
    Expect(t.Relation(0, 7) == TeamRelation::Ghost, "undeclared pair defaults ghost");
    // 运行时覆写
    t.SetRelation(0, 7, TeamRelation::Hostile);
    Expect(t.Hostile(0, 7) && t.Hostile(7, 0), "override applies both ways");
}

void TestSpatialHash() {
    World world;
    Scene& s = world.CreateScene("hash");

    // 网格布置：4×4 间距 100px，team 交错（0/1）
    Entity ents[16];
    for (int i = 0; i < 16; ++i) {
        ents[i] = s.Create();
        s.Emplace<Transform2D>(ents[i],
                               Transform2D{{(float)(i % 4) * 100.0f, (float)(i / 4) * 100.0f}});
        s.Emplace<Meta>(ents[i]).team = (uint32_t)(i % 2);
    }

    SpatialHash hash;
    hash.Configure(64.0f);
    hash.Rebuild(s);
    Expect(hash.ItemCount() == 16, "all items hashed");
    Expect(hash.CellCount() > 0 && hash.CellCount() <= 16, "cell count sane");

    // OverlapCircle：中心 (50,50) 半径 75 → 四角距离 70.7 全命中，其余 ≥ 112 不命中
    {
        int hits = 0;
        QueryFilter f; // 全队
        hash.OverlapCircle(s, {50, 50}, 75.0f, f, 0.0f, [&](Entity, const Transform2D&) {
            ++hits;
            return true;
        });
        Expect(hits == 4, "circle overlap count");
    }
    // teamMask 过滤：只 team1（奇数下标 → (100,0) 和 (0,100)）
    {
        int hits = 0;
        QueryFilter f;
        f.teamMask = 1u << 1;
        hash.OverlapCircle(s, {50, 50}, 75.0f, f, 0.0f, [&](Entity, const Transform2D&) {
            ++hits;
            return true;
        });
        Expect(hits == 2, "team mask filters");
    }
    // exclude
    {
        QueryFilter f;
        f.exclude = ents[0];
        bool seen0 = false;
        hash.OverlapCircle(s, {0, 0}, 10.0f, f, 0.0f, [&](Entity e, const Transform2D&) {
            seen0 |= (e == ents[0]);
            return true;
        });
        Expect(!seen0, "exclude filters self");
    }

    // cell 边界跨格查询（实体在 (200,200) 恰在 cell 角）
    {
        int hits = 0;
        hash.OverlapCircle(s, {200, 200}, 1.0f, QueryFilter{}, 0.0f,
                           [&](Entity, const Transform2D&) {
                               ++hits;
                               return true;
                           });
        Expect(hits == 1, "boundary point found");
    }

    // OverlapBox
    {
        int hits = 0;
        hash.OverlapBox(s, Rect::FromCenterHalf({50, 50}, 55, 55), QueryFilter{}, 0.0f,
                        [&](Entity, const Transform2D&) {
                            ++hits;
                            return true;
                        });
        Expect(hits == 4, "box overlap count");
    }

    // Raycast：从 (-50, 0) 向 +x，最近命中 (0,0)
    {
        RayHit h = hash.Raycast(s, {-50, 0}, {1, 0}, 1000.0f, QueryFilter{}, 4.0f);
        Expect(!h.entity.IsNull(), "raycast hit");
        ExpectNear(h.point.x, 0.0f, 4.1f, "raycast near (0,0)");
        // 排除首实体后命中 (100,0)
        QueryFilter f;
        f.exclude = h.entity;
        RayHit h2 = hash.Raycast(s, {-50, 0}, {1, 0}, 1000.0f, f, 4.0f);
        ExpectNear(h2.point.x, 100.0f, 4.1f, "raycast next along +x");
    }

    // 命中序确定性：cell 内 id 升序（两次重建后同序）
    {
        std::vector<uint64_t> order1, order2;
        for (int round = 0; round < 2; ++round) {
            hash.Rebuild(s);
            if (round == 0) {
                hash.OverlapCircle(s, {50, 50}, 60.0f, QueryFilter{}, 0.0f,
                                   [&](Entity e, const Transform2D&) {
                                       order1.push_back(e.id);
                                       return true;
                                   });
            } else {
                hash.OverlapCircle(s, {50, 50}, 60.0f, QueryFilter{}, 0.0f,
                                   [&](Entity e, const Transform2D&) {
                                       order2.push_back(e.id);
                                       return true;
                                   });
            }
        }
        Expect(order1 == order2, "hit order stable across rebuilds");
        Expect(std::is_sorted(order1.begin(), order1.end()), "hit order = id ascending");
    }

    // PointQuery（id 最小优先）
    {
        Entity e = hash.PointQuery(s, {100, 100}, QueryFilter{}, 10.0f);
        Expect(!e.IsNull() && s.Has<Transform2D>(e), "point query finds");
    }
}

/// 最小预制体工厂：monster(prefab 1) / projectile(prefab 2)

Entity TestSpawnFactory(Scene& s, uint32_t prefabId, Vec2 pos, uint32_t team) {
    Entity e = s.Create();
    s.Emplace<Transform2D>(e, Transform2D{pos});
    s.Emplace<Meta>(e).team = team;
    s.Emplace<Velocity>(e);
    if (prefabId == 1) { // 怪
        s.Emplace<Health>(e, Health{.max = 50.0f, .cur = 50.0f});
        s.Emplace<Chase>(e);
    } else if (prefabId == 2) { // 投射物
        s.Emplace<Projectile>(e, Projectile{.speed = 300.0f, .lifetime = 3.0f, .damage = 15.0f});
    } else {
        return Entity::Null();
    }
    return e;
}

void TestSystemPipelineOrder() {
    WorldDesc d;
    d.threadCount = 1;
    World world(d);
    world.InstallDefaultSystems();
    auto& p = world.Pipeline();

    Expect(p.Systems().size() == 20, "20 systems installed（T3d 批② +AnimGraphSystem；A 档 "
                                     "+TweenSystem；M6c 批② +AudioSystem）");
    // Essential 阶段只有 DestroyCommit；FixedTick 按表序
    // （#9 Pickup = M5 批①；T3d 批② AnimGraph 插在 CSharpBatch 后——图评估读当
    // tick 脚本参数，写段由下一 tick Animator 消费，与脚本直写 Play 同拍；
    // A 档补间 Tween 插在 AnimGraph 后、事件派发前——脚本当 tick 发起即首写、
    // 存活补间拥有字段、完成事件当帧派发；M6c 批② Audio 插在 Tween 后、事件
    // 派发前——C# 当 tick staging 的音频命令本 tick 落地、零 RNG/零 ECS 写）
    const char* expected[] = {"InputSnapshot",
                              "Director",
                              "Spawn",
                              "AI",
                              "Navigation",
                              "Separation",
                              "Movement",
                              "SpatialHashRebuild",
                              "Pickup",
                              "Hitbox",
                              "Trigger",
                              "Stat",
                              "Animator",
                              "ProjectileLifetime",
                              "CSharpBatch",
                              "AnimGraph",
                              "Tween",
                              "Audio",
                              "ScriptEventDispatch"};
    uint32_t fi = 0;
    for (const auto& s : p.Systems()) {
        if (s->Stage() == SystemStage::Essential) {
            Expect(std::string_view(s->Name()) == "DestroyCommit", "essential is destroy");
        } else {
            Expect(fi < 19 && std::string_view(s->Name()) == expected[fi], "fixedtick order");
            ++fi;
        }
    }
    Expect(fi == 19, "19 fixedtick systems");
    Expect(p.Profiles().size() == 20, "profiles allocated");
}

void TestSimulationEndToEnd() {
    WorldDesc d;
    d.threadCount = 1; // 诊断档（逻辑验证单线程）
    World world(d);
    world.SetSpawnFn(TestSpawnFactory);
    world.InstallDefaultSystems();

    Scene& s = world.CreateScene("arena");
    world.SetActiveScene(&s);
    world.SetBounds(Rect::FromMinSize({-500, -500}, {1000, 1000}));

    // 玩家（team 0）+ 追击怪（team 1, Chase→0）+ 射手怪（team 1, Shooter→0）
    Entity player = s.Create();
    s.Emplace<Transform2D>(player, Transform2D{{0, 0}});
    s.Emplace<Meta>(player).team = 0;

    Entity monster = TestSpawnFactory(s, 1, {100, 0}, 1);
    Chase& chase = s.Get<Chase>(monster);
    chase.speed = 100;
    chase.aggroRange = 500;
    chase.targetTeam = 0;

    Entity shooter = TestSpawnFactory(s, 1, {-100, 0}, 1);
    s.Remove<Chase>(shooter);
    Shooter& sh = s.Emplace<Shooter>(shooter);
    sh.interval = 0.2f;
    sh.range = 500;
    sh.targetTeam = 0;
    sh.projectileId = 2;
    sh.cooldown = 0.1f;

    // 事件收集（帧末派发）
    int spawnEvents = 0, hitEvents = 0, deathEvents = 0;
    world.SetEventSink([&](World&, const EventPacket& e) {
        if (e.type == GameEvent::Spawn) ++spawnEvents;
        if (e.type == GameEvent::Hit) ++hitEvents;
        if (e.type == GameEvent::Death) ++deathEvents;
    });

    const float dt = 1.0f / 60.0f;

    // 帧 1：目标板当帧生效（AI 最近邻不再依赖哈希暖场），怪朝玩家 (-x) 移动
    world.Step(dt);
    Expect(s.Get<Velocity>(monster).v.x < 0.0f, "chase moves toward player (-x)");
    float d0 = Length(s.Get<Transform2D>(monster).pos - Vec2(0, 0));

    // 60 帧（1 秒）：怪贴近（keepRange 内停）；射手持续开火生成投射物
    for (int i = 0; i < 60; ++i) world.Step(dt);
    float d1 = Length(s.Get<Transform2D>(monster).pos - Vec2(0, 0));
    Expect(d1 < d0, "chaser closed distance");
    Expect(spawnEvents > 0, "shooter spawned projectiles (spawn events)");
    // 投射物生成且带速度朝玩家
    uint32_t projectiles = 0;
    s.Each([&](Entity e) {
        if (s.Has<Projectile>(e)) {
            ++projectiles;
        }
    });
    Expect(projectiles > 0, "projectiles alive");

    // 命中链路：给玩家血量，怪队投射物 hostile→0 命中 → Hit/Death 事件。
    // T1（M5 批⓪）后多段伤害真实致死：30 hp / 弹伤 15 → 2 击（隔 ~6 tick 无敌窗）
    // → 玩家死亡并销毁（修复前：首击置 iFrames 后无递减 → 恒免疫、永不死）。
    s.Emplace<Health>(player, Health{.max = 30.0f, .cur = 30.0f});
    for (int i = 0; i < 120 && s.Alive(player); ++i) world.Step(dt);
    Expect(hitEvents > 0, "projectiles hit player");
    Expect(!s.Alive(player), "multi-hit damage killed player (T1)");
    Expect(deathEvents >= 1, "death events fired");

    // 投射物寿命回收：跑足寿命周期，场上投射物数受控（生成率≈销毁率）
    for (int i = 0; i < 300; ++i) world.Step(dt);
    uint32_t projAfter = 0;
    s.Each([&](Entity e) { projAfter += s.Has<Projectile>(e) ? 1 : 0; });
    Expect(projAfter < 50, "lifetime reaps projectiles");

    // 管线 profile 数据（F3 数据源）
    const SystemProfile* ai = world.Pipeline().FindProfile("AI");
    const SystemProfile* mv = world.Pipeline().FindProfile("Movement");
    Expect(ai && ai->runs == world.TickIndex(), "AI ran every tick");
    Expect(mv && mv->runs == world.TickIndex(), "Movement ran every tick");
    Expect(ai->totalMs >= 0.0 && mv->lastMs >= 0.0f, "timings sane");
}

void TestSeparationForce() {
    WorldDesc d;
    d.threadCount = 1;
    World world(d);
    world.InstallDefaultSystems();
    Scene& s = world.CreateScene("sep");
    world.SetActiveScene(&s);

    // 两只同队怪（soft-collide）贴近：分离力应把彼此推开
    Entity a = s.Create(), b = s.Create();
    for (Entity e : {a, b}) {
        s.Emplace<Transform2D>(e, Transform2D{{0, 0}});
        s.Emplace<Meta>(e).team = 1; // monsters: (1,1) soft-collide
        s.Emplace<Velocity>(e);
    }
    s.Get<Transform2D>(a).pos = {0, 0};
    s.Get<Transform2D>(b).pos = {10, 0};

    const float dt = 1.0f / 60.0f;
    world.Step(dt); // AI（无行为组件不动）→ Separation 读哈希（首帧空）
    world.Step(dt); // 第二帧哈希已有数据 → 分离力生效
    Vec2 va = s.Get<Velocity>(a).v, vb = s.Get<Velocity>(b).v;
    Expect(va.x < 0.0f && vb.x > 0.0f, "separation pushes apart on x");
    ExpectNear(va.x, -vb.x, 1e-4f, "separation symmetric");

    // 不同队（无 soft-collide 关系）不分离
    s.Get<Meta>(b).team = 2;
    s.Get<Velocity>(a).v = {};
    s.Get<Velocity>(b).v = {};
    world.Step(dt);
    world.Step(dt);
    Expect(s.Get<Velocity>(a).v == Vec2::Zero(), "non-softcollide no force");
}

// 数组段保真 + RT 字段不入档（SceneArchive 修复回归）

void TestArchiveArraySegAndRuntimeFields() {
    World world;
    Scene& src = world.CreateScene("seg");

    Entity e = src.Create();
    src.Emplace<Transform2D>(e, Transform2D{{3, 4}});
    StatusEffects& st = src.Emplace<StatusEffects>(e);
    st.active[0] = StatusInst{11, 2, 4.5f, 0xABCDu};
    st.active[1] = StatusInst{12, 1, 0.25f, 0x1234u};
    st.count = 2;
    Inventory& inv = src.Emplace<Inventory>(e);
    inv.items[0] = ItemStack{101, 3};
    inv.items[1] = ItemStack{102, 8};
    inv.items[2] = ItemStack{103, 1};
    inv.count = 3;
    inv.gold = 777;
    Equipment& eq = src.Emplace<Equipment>(e);
    eq.relicIds[0] = 7;
    eq.relicIds[1] = 8;
    eq.relicIds[2] = 9;

    // RT 字段（修复：此前漏标被误序列化）
    src.Emplace<Health>(e, Health{.max = 200.0f,
                                  .cur = 150.0f,
                                  .iFrames = 0.5f,
                                  .iframeWindow = 0.35f}); // iFrames RT；iframeWindow 落档
    Entity sp = src.Create();
    src.Emplace<Transform2D>(sp, Transform2D{{0, 0}});
    Spawner& spo = src.Emplace<Spawner>(sp);
    spo.cooldown = 0.42f; // RT

    // Projectile：配置字段落档 roundtrip；命中记忆/计数 RT 不入档（M5 批⓪ T2）
    Entity pe = src.Create();
    src.Emplace<Transform2D>(pe, Transform2D{{2, 2}});
    Projectile& pp = src.Emplace<Projectile>(pe);
    pp.hitRadius = 9.0f;
    pp.knockback = 120.0f;
    pp.pierce = 2;
    pp.hits = 3; // RT
    pp.hitMemory[0] = 0x1234u; // RT

    // Collectible：磁吸三参数落档；state/target RT 不入档（M5 批① T1）
    Entity ce = src.Create();
    src.Emplace<Transform2D>(ce, Transform2D{{6, 6}});
    Collectible& cc = src.Emplace<Collectible>(ce);
    cc.kind = 2;
    cc.magnetRadius = 64.0f;
    cc.magnetSpeed = 400.0f;
    cc.value = 3.5f;
    cc.state = 1; // RT
    cc.target = Entity{1}; // RT（非空以验读档回落）

    std::string text = SceneArchive::Save(src);
    Expect(text.find("\"iFrames\"") == std::string::npos, "iFrames not serialized");
    Expect(text.find("\"iframeWindow\"") != std::string::npos,
           "iframeWindow serialized (config field)");
    Expect(text.find("\"cooldown\"") == std::string::npos, "spawner cooldown not serialized");
    Expect(text.find("\"hitRadius\"") != std::string::npos,
           "projectile hitRadius serialized (config)");
    Expect(text.find("\"hitMemory0\"") == std::string::npos, "hit memory not serialized (runtime)");
    Expect(text.find("\"magnetSpeed\"") != std::string::npos,
           "collectible magnetSpeed serialized (config)");
    Expect(text.find("\"state\"") == std::string::npos,
           "collectible state not serialized (runtime)");
    Expect(text.find("\"target\"") == std::string::npos,
           "collectible target not serialized (runtime)");

    World w2;
    Scene& dst = w2.CreateScene("seg2");
    Expect(SceneArchive::Load(dst, text), "seg scene load");
    Expect(dst.AliveCount() == 4, "seg entity count");

    bool found = false;
    dst.View<StatusEffects>().each([&](auto, StatusEffects& s2) {
        found = true;
        Expect(s2.count == 2, "status count roundtrip");
        Expect(s2.active[0].id == 11 && s2.active[0].stacks == 2 &&
                   s2.active[0].source == 0xABCDu && ExpectNear0(s2.active[0].remain, 4.5f),
               "status[0] roundtrip");
        Expect(s2.active[1].id == 12 && s2.active[1].source == 0x1234u, "status[1] roundtrip");
    });
    Expect(found, "status entity located");
    dst.View<Inventory>().each([&](auto, Inventory& i2) {
        Expect(i2.count == 3, "inventory count roundtrip");
        Expect(i2.items[0].itemId == 101 && i2.items[0].count == 3, "item[0] roundtrip");
        Expect(i2.items[2].itemId == 103 && i2.items[2].count == 1, "item[2] roundtrip");
        Expect(i2.gold == 777, "gold roundtrip");
    });
    dst.View<Equipment>().each([&](auto, Equipment& e2) {
        Expect(e2.relicIds[0] == 7 && e2.relicIds[1] == 8 && e2.relicIds[2] == 9,
               "relicIds[0..2] roundtrip");
    });
    // RT 字段读档后回落默认值；配置字段 roundtrip
    dst.View<Spawner>().each(
        [&](auto, Spawner& s2) { Expect(s2.cooldown == 0.0f, "cooldown reset (runtime)"); });
    bool sawHealth = false;
    dst.View<Health>().each([&](auto, Health& h2) {
        sawHealth = true;
        Expect(h2.iFrames == 0.0f, "iFrames reset (runtime)");
        Expect(ExpectNear0(h2.iframeWindow, 0.35f), "iframeWindow roundtrip");
    });
    Expect(sawHealth, "health entity located after load");
    bool sawProj = false;
    dst.View<Projectile>().each([&](auto, Projectile& p2) {
        sawProj = true;
        Expect(ExpectNear0(p2.hitRadius, 9.0f), "hitRadius roundtrip");
        Expect(ExpectNear0(p2.knockback, 120.0f), "knockback roundtrip");
        Expect(p2.pierce == 2, "pierce roundtrip");
        Expect(p2.hits == 0 && p2.hitMemory[0] == 0, "runtime fields reset");
    });
    Expect(sawProj, "projectile entity located after load");
    bool sawCol = false;
    dst.View<Collectible>().each([&](auto, Collectible& c2) {
        sawCol = true;
        Expect(c2.kind == 2, "collectible kind roundtrip");
        Expect(ExpectNear0(c2.magnetRadius, 64.0f), "magnetRadius roundtrip");
        Expect(ExpectNear0(c2.magnetSpeed, 400.0f), "magnetSpeed roundtrip");
        Expect(ExpectNear0(c2.value, 3.5f), "value roundtrip");
        Expect(c2.state == 0 && c2.target.IsNull(), "collectible runtime fields reset");
    });
    Expect(sawCol, "collectible entity located after load");
}

// 恶意/畸形 .scene 不抛穿加载器（json 异常降级修复回归）

void TestArchiveMalformedTolerance() {
    World w;
    Scene& s = w.CreateScene("bad");
    // 字段类型错（pos 是字符串）
    Expect(SceneArchive::Load(s, R"({"schemaVersion":1,"entities":[)"
                                 R"({"components":{"Transform2D":{"pos":"oops","rot":0}}}]})"),
           "type-mismatched field tolerated");
    bool sawDefaultTf = false;
    s.View<Transform2D>().each([&](auto, Transform2D& tf) {
        sawDefaultTf = true;
        Expect(tf.pos == Vec2::Zero(), "bad field left at default");
        Expect(tf.rot == 0.0f, "sibling field still read");
    });
    Expect(sawDefaultTf, "entity created despite bad field");
    // entities 非数组
    Expect(!SceneArchive::Load(s, R"({"schemaVersion":1,"entities":5})"),
           "non-array entities rejected");
    // components 非对象
    Expect(SceneArchive::Load(s, R"({"schemaVersion":1,"entities":[)"
                                 R"({"components":17}]})"),
           "non-object components tolerated");
    // 数组段类型坏（items 非数组）→ 不崩，count 保持 0
    Expect(SceneArchive::Load(s, R"({"schemaVersion":1,"entities":[)"
                                 R"({"components":{"Inventory":{"gold":9,"items":"x"}}}]})"),
           "bad array seg tolerated");
    bool sawInv = false;
    s.View<Inventory>().each([&](auto, Inventory& inv) {
        sawInv = true;
        Expect(inv.count == 0, "bad items leaves count 0");
        Expect(inv.gold == 9, "sibling scalar still read");
    });
    Expect(sawInv, "inventory present after bad seg");
}

// 越界 team/layer 实体静默不命中（PassFilter 判断反转修复回归）

void TestSpatialHashRangeClamp() {
    World world;
    Scene& s = world.CreateScene("range");
    Entity bad = s.Create();
    s.Emplace<Transform2D>(bad, Transform2D{{0, 0}});
    s.Emplace<Meta>(bad).team = 40; // 越界（位索引域 [0,32)）
    Entity badLayer = s.Create();
    s.Emplace<Transform2D>(badLayer, Transform2D{{10, 0}});
    s.Emplace<Meta>(badLayer).layer = 20;

    SpatialHash hash;
    hash.Configure(64.0f);
    hash.Rebuild(s);
    int hits = 0;
    hash.OverlapCircle(s, {0, 0}, 100.0f, QueryFilter{}, 0.0f, [&](Entity, const Transform2D&) {
        ++hits;
        return true;
    });
    Expect(hits == 0, "out-of-range team/layer never hit");
}

// 查询侧两级加速（2026-09-24 方案 A）：Item 内联 team/layer 位 + cell 级 team
// 位图整格早退——语义零漂移的机制证明（无 Meta 恒放行 / 越界恒不命中 / 掩码
// 命中集合与回调序 = 默认过滤 + 回调内手过滤逐项一致）

void TestSpatialHashQueryFastPath() {
    World world;
    Scene& s = world.CreateScene("fastpath");

    // 混合布置（同格 (0..30)² 内）：team1 ×2 / team2 ×1 / 无 Meta ×1 / 越界 ×1；
    // 远处格 (500,0)：纯 team1 群 ×3（整格早退靶）
    Entity t1a = s.Create(), t1b = s.Create(), t2 = s.Create(), noMeta = s.Create(),
           bad = s.Create();
    for (Entity e : {t1a, t1b, t2, noMeta, bad})
        s.Emplace<Transform2D>(e, Transform2D{{10.0f, 10.0f}});
    s.Emplace<Meta>(t1a).team = 1;
    s.Emplace<Meta>(t1b).team = 1;
    s.Emplace<Meta>(t2).team = 2;
    s.Emplace<Meta>(bad).team = 40;
    Entity farEnt[3];
    for (int i = 0; i < 3; ++i) {
        farEnt[i] = s.Create();
        s.Emplace<Transform2D>(farEnt[i], Transform2D{{500.0f + (float)i, 0.0f}});
        s.Emplace<Meta>(farEnt[i]).team = 1;
    }

    SpatialHash hash;
    hash.Configure(64.0f);
    hash.Rebuild(s);

    // ① 无 Meta 实体恒放行（默认过滤命中近格全部 4 个有效实体）
    {
        int hits = 0;
        hash.OverlapCircle(s, {0, 0}, 64.0f, QueryFilter{}, 0.0f, [&](Entity, const Transform2D&) {
            ++hits;
            return true;
        });
        Expect(hits == 4, "noMeta passes default filter");
    }
    // ② teamMask 查询：同格无 Meta 实体不被掩码误杀、不被整格早退漏掉
    {
        int hits = 0;
        bool sawNoMeta = false;
        QueryFilter f;
        f.teamMask = 1u << 2; // 只要 team2
        hash.OverlapCircle(s, {0, 0}, 64.0f, f, 0.0f, [&](Entity e, const Transform2D&) {
            ++hits;
            sawNoMeta |= (e == noMeta);
            return true;
        });
        Expect(hits == 2 && sawNoMeta, "team2 + noMeta (hasNoMeta 钉住整格)");
    }
    // ③ 纯 team1 远格 + teamMask=team2 → 整格早退零命中
    {
        int hits = 0;
        QueryFilter f;
        f.teamMask = 1u << 2;
        hash.OverlapCircle(s, {500.0f, 0.0f}, 64.0f, f, 0.0f, [&](Entity, const Transform2D&) {
            ++hits;
            return true;
        });
        Expect(hits == 0, "pure-team1 cell skipped for team2 mask");
    }
    // ④ layerMask 过滤走内联位（t2 的 Meta 在布置段已建——此处 Get 即可；
    // 重复 Emplace 在 Debug 撞 EnTT "Slot not available"、Release 静默重复入池）
    {
        s.Get<Meta>(t2).layer = 3;
        hash.Rebuild(s);
        int hits = 0;
        QueryFilter f;
        f.layerMask = 1u << 3;
        hash.OverlapCircle(s, {0, 0}, 64.0f, f, 0.0f, [&](Entity e, const Transform2D&) {
            ++hits;
            return e == t2 || e == noMeta; // 命中只允许 t2 与无 Meta 实体
        });
        Expect(hits == 2, "layer mask via inline bits");
        s.Get<Meta>(t2).layer = 0;
        hash.Rebuild(s);
    }
    // ⑤ 差分等价：掩码查询命中序 ≡ 默认查询 + 回调内手过滤（含跨格排序）
    {
        for (uint32_t mask = 1; mask < 8; ++mask) {
            std::vector<uint64_t> masked, manual;
            QueryFilter f;
            f.teamMask = mask;
            hash.OverlapCircle(s, {0, 0}, 600.0f, f, 0.0f, [&](Entity e, const Transform2D&) {
                masked.push_back(e.id);
                return true;
            });
            hash.OverlapCircle(s, {0, 0}, 600.0f, QueryFilter{}, 0.0f,
                               [&](Entity e, const Transform2D&) {
                                   if (const Meta* m = s.TryGet<Meta>(e)) {
                                       if (m->team >= 32 || !(mask & (1u << m->team)))
                                           return true; // 旧 PassFilter 语义（越界恒不命中）
                                   }
                                   manual.push_back(e.id);
                                   return true;
                               });
            Expect(masked == manual, "mask query == manual filter (order included)");
        }
    }
    // ⑥ HostileMask（TeamTable 行掩码）
    {
        TeamTable t = TeamTable::Default();
        Expect(t.HostileMask(0) == (1u << 1), "player hostile to monsters only");
        Expect(t.HostileMask(1) == ((1u << 0) | (1u << 3)), "monsters hostile to player + bullets");
        Expect(t.HostileMask(31) == 0, "unconfigured team = empty mask");
        Expect(t.HostileMask(40) == 0, "out-of-range team = empty mask");
    }
}

// TargetBoard 网格最近邻等价性钉板（2026-09-26）：随机布点下网格路径（≥kMinList
// 走 CSR 桶 + 位图环搜）与线性参考（旧实现语义）逐查询一致——网格化/后续并行化
// 改动的正确性由"金回放实证"升级为单测钉板。等距平局是文档化语义差异（网格 =
// 环扫序先见者 vs 线性 = 池序靠前者，Systems.h 注释）；随机浮点布点下精确等距
// 概率为零，若出现（布点退化）按失败报而非静默跳过。边界一并钉格：未声明队恒
// Null、空程无候选 Null、排除自身、无 Meta 实体不入板。

void TestTargetBoardGridEquivalence() {
    World world;
    Scene& s = world.CreateScene("board");

    // 确定性布点（本地 LCG，零依赖；续战同源可复现）
    uint64_t seed = 0x9E3779B97F4A7C15ull;
    auto rand01 = [&seed]() {
        seed = seed * 6364136223846793005ull + 1442695040888963407ull;
        return ((seed >> 33) & 0xFFFFFF) / (float)0x1000000;
    };
    auto randPos = [&rand01]() {
        return Vec2{(rand01() - 0.5f) * 2000.0f, (rand01() - 0.5f) * 2000.0f};
    };

    // 布置：team1 ×200（≥ kMinList=64 → 网格路径）/ team3 ×150 / team0 ×30（未
    // 声明队）/ 无 Meta ×20（不进板）。创建序 = Rebuild 收集序（新场景无销毁）。
    struct Ref {
        Entity e;
        Vec2 pos;
    };
    std::vector<Ref> team1, team3;
    auto spawnTeam = [&](uint32_t team, std::vector<Ref>& into, int n) {
        for (int i = 0; i < n; ++i) {
            Entity e = s.Create();
            Vec2 p = randPos();
            s.Emplace<Transform2D>(e, Transform2D{p});
            s.Emplace<Meta>(e).team = team;
            into.push_back({e, p});
        }
    };
    spawnTeam(1, team1, 200);
    spawnTeam(3, team3, 150);
    for (int i = 0; i < 30; ++i) {
        Entity e = s.Create();
        s.Emplace<Transform2D>(e, Transform2D{randPos()});
        s.Emplace<Meta>(e).team = 0;
    }
    for (int i = 0; i < 20; ++i) {
        Entity e = s.Create();
        s.Emplace<Transform2D>(e, Transform2D{randPos()}); // 无 Meta：不入板
    }

    TargetBoard board;
    board.DeclareTeams({1u, 3u});
    board.Rebuild(s, false);

    // 线性参考（旧实现语义：严格小于 = 等距保池序靠前者；tie 位 = 存在等距并列）
    struct LinResult {
        Entity e;
        float d2;
        bool tied;
    };
    auto linearNearest = [](const std::vector<Ref>& list, Vec2 from, float range, Entity exclude) {
        LinResult r{Entity::Null(), range * range, false};
        float best = r.d2;
        Entity bestE = Entity::Null();
        for (const Ref& ref : list) {
            if (ref.e == exclude) continue;
            float d2 = LengthSq(ref.pos - from);
            if (d2 < best) {
                best = d2;
                bestE = ref.e;
                r.tied = false;
            } else if (d2 == best && bestE != Entity::Null()) {
                r.tied = true; // 精确等距并列（随机浮点下不应发生）
            }
        }
        r.e = bestE;
        r.d2 = best;
        return r;
    };

    // 查询矩阵：400 随机点 × 4 档半径 × 排除自身/无排除，双队对拍
    int checked = 0, tied = 0;
    const float ranges[] = {40.0f, 180.0f, 600.0f, 2500.0f};
    for (int q = 0; q < 400; ++q) {
        Vec2 from = randPos() * 1.2f; // 含板外查询点
        for (float range : ranges) {
            for (int excl = 0; excl < 2; ++excl) {
                Entity exclude = excl ? team1[(q * 7) % team1.size()].e : Entity::Null();
                LinResult want = linearNearest(team1, from, range, exclude);
                Entity got = board.Nearest(1u, from, range, exclude);
                if (want.tied) {
                    ++tied;
                    continue; // 语义差异域：见函数头注释
                }
                Expect(got == want.e, "team1 grid nearest == linear reference");
                ++checked;

                LinResult want3 = linearNearest(team3, from, range, Entity::Null());
                Entity got3 = board.Nearest(3u, from, range, Entity::Null());
                if (!want3.tied) {
                    Expect(got3 == want3.e, "team3 grid nearest == linear reference");
                    ++checked;
                }
            }
        }
    }
    // 边界：未声明队恒 Null；远离布点域的短程 = Null
    Expect(board.Nearest(0u, {0.0f, 0.0f}, 5000.0f, Entity::Null()).IsNull(),
           "undeclared team always null");
    Expect(board.Nearest(1u, {9000.0f, 9000.0f}, 100.0f, Entity::Null()).IsNull(),
           "empty range null");
    Expect(checked >= 3000, "query matrix coverage");
    Expect(tied == 0, "random floats must not produce exact ties");
}

// TargetBoard 坏坐标防御（review 2026-10-02 #19）：NaN/极端坐标（脚本写
// Transform、手改场景档可达）不得把进程炸掉——原实现 NaN 的 float→int 是 UB、
// 1e9×1e9 两簇要分配 >百 TB 占位位图 = bad_alloc terminate。修复后：NaN 剪除、
// 跨度弃格降级线性，Nearest 仍正确返回最近正常目标。

void TestTargetBoardBadCoordDefense() {
    World world;
    Scene& s = world.CreateScene("board-bad");
    auto spawn = [&s](uint32_t team, Vec2 pos) {
        Entity e = s.Create();
        s.Emplace<Transform2D>(e, Transform2D{pos});
        s.Emplace<Meta>(e).team = team;
        return e;
    };
    // 100 正常点（≥ kMinList=64 触发网格路径）+ NaN / ±Inf / 1e9 三簇毒点
    Entity near0 = spawn(1, {10.0f, 0.0f});
    for (int i = 0; i < 99; ++i) spawn(1, {(float)(100 + i * 8), 0.0f});
    spawn(1, {std::numeric_limits<float>::quiet_NaN(), 0.0f});
    spawn(1, {0.0f, std::numeric_limits<float>::infinity()});
    spawn(1, {1e9f, 1e9f});
    spawn(1, {-1e9f, -1e9f});
    TargetBoard board;
    board.DeclareTeams({1u});
    board.Rebuild(s, false); // 修复前：此处 bad_alloc / UB
    Expect(board.Nearest(1u, {0.0f, 0.0f}, 500.0f, Entity::Null()) == near0,
           "bad-coord board still finds nearest sane target");
    Expect(
        board.Nearest(1u, {std::numeric_limits<float>::quiet_NaN(), 0.0f}, 500.0f, Entity::Null())
            .IsNull(),
        "NaN query point returns null safely");
}

// TargetBoard 并行 Rebuild 同构钉板（2026-09-26 并行化批）：大场（≥kParallelMin）
// 下并行收集/归并/逐队建桶与串行路径逐位一致——list 内容（序+值）强比较 +
// NearestAny/Nearest 行为对拍。平局布点（同 cell ±8px 等距对）专钉"收集序 = view
// 序"：等距平局语义 = 序先见者，序乱即翻结果。掺销毁+重建（entt swap_only 池回收
// → view 序与创建序分叉），避免只测到"新场景顺序退化"。

void TestTargetBoardParallelRebuildIsomorphic() {
    World world; // 默认多线程 JobSystem
    Scene& s = world.CreateScene("board-par");

    auto spawn = [&s](uint32_t team, Vec2 pos) {
        Entity e = s.Create();
        s.Emplace<Transform2D>(e, Transform2D{pos});
        s.Emplace<Meta>(e).team = team;
        return e;
    };

    // team1 ×4200 成对平局布点（±8px 同 cell 32px 内）；team3 ×4200 平移域同款；
    // team0 ×300（未声明队）；无 Meta ×100（不入板）。总 8700 ≥ kParallelMin。
    for (int i = 0; i < 4200; i += 2) {
        const int gx = (i / 2) % 70, gy = (i / 2) / 70;
        const Vec2 c{(float)(gx * 32 + 16), (float)(gy * 32 + 16)};
        spawn(1u, c + Vec2{-8.0f, 0.0f});
        spawn(1u, c + Vec2{8.0f, 0.0f});
        const Vec2 c3{(float)(gx * 32 + 16 + 5000), (float)(gy * 32 + 16)};
        spawn(3u, c3 + Vec2{-8.0f, 0.0f});
        spawn(3u, c3 + Vec2{8.0f, 0.0f});
    }
    for (int i = 0; i < 300; ++i) spawn(0u, Vec2{(float)(i * 17), (float)(i * 13 - 900)});
    for (int i = 0; i < 100; ++i) {
        Entity e = s.Create();
        s.Emplace<Transform2D>(e, Transform2D{Vec2{(float)(i * 31 - 950), (float)(i * 7)}});
    }
    // 销毁散布 + 重建（池 slot 复用 → view 序与创建序分叉）
    {
        // team3 每 10 个销毁 1 个（从场景扫描，避免依赖创建序簿记）
        std::vector<Entity> t3;
        for (auto [ent, meta, tf] : s.View<Meta, Transform2D>().each())
            if (meta.team == 3u) t3.push_back(Scene::FromEntt(ent));
        for (size_t i = 0; i < t3.size(); i += 10) s.Destroy(t3[i]);
    }
    s.CommitDestroys();
    for (int i = 0; i < 50; ++i) // 重建队（复用回收 slot）
        spawn(1u, Vec2{(float)(i * 41 + 8000), (float)(i * 3)});

    TargetBoard serial, par;
    serial.DeclareTeams({1u, 3u});
    par.DeclareTeams({1u, 3u});
    serial.Rebuild(s, /*collectAll=*/true, /*jobs=*/nullptr);
    par.Rebuild(s, true, &world.Jobs());

    // 内容级：list 逐位（序 + 值）强比较
    for (uint32_t team : {1u, 3u}) {
        const auto& a = serial.TeamEntries(team);
        const auto& b = par.TeamEntries(team);
        Expect(a.size() == b.size(), "parallel rebuild list size matches serial");
        bool same = a.size() == b.size();
        for (size_t i = 0; same && i < a.size(); ++i)
            same = a[i].e.id == b[i].e.id && a[i].pos.x == b[i].pos.x && a[i].pos.y == b[i].pos.y;
        Expect(same, "parallel rebuild list bit-identical to serial (view order)");
    }

    // 行为级：平局查询（同 cell 等距对 → 序先见者）+ 随机查询对拍（双队 + 全表）
    for (int i = 0; i < 2100; i += 2) { // 平局点 = 对中心（每对一格，抽一半格）
        const int gx = (i / 2) % 70, gy = (i / 2) / 70;
        const Vec2 c{(float)(gx * 32 + 16), (float)(gy * 32 + 16)};
        const Vec2 c3{c.x + 5000.0f, c.y};
        Expect(serial.Nearest(1u, c, 64.0f, Entity::Null()) ==
                   par.Nearest(1u, c, 64.0f, Entity::Null()),
               "team1 tie-break identical (order-sensitive)");
        Expect(serial.Nearest(3u, c3, 64.0f, Entity::Null()) ==
                   par.Nearest(3u, c3, 64.0f, Entity::Null()),
               "team3 tie-break identical (order-sensitive)");
    }
    uint64_t seed = 0x853C49E6748FEA9Bull;
    auto rand01 = [&seed]() {
        seed = seed * 6364136223846793005ull + 1442695040888963407ull;
        return ((seed >> 33) & 0xFFFFFF) / (float)0x1000000;
    };
    const float ranges[] = {40.0f, 180.0f, 900.0f, 4000.0f};
    for (int q = 0; q < 200; ++q) {
        const Vec2 from{(rand01() - 0.5f) * 11000.0f, (rand01() - 0.5f) * 4000.0f};
        for (float range : ranges) {
            Expect(serial.Nearest(1u, from, range, Entity::Null()) ==
                       par.Nearest(1u, from, range, Entity::Null()),
                   "random nearest team1 identical");
            Expect(serial.NearestAny(from, range, Entity::Null()) ==
                       par.NearestAny(from, range, Entity::Null()),
                   "random nearest-any identical");
        }
    }
}

// 并发 Destroy（Scene::Destroy 数据竞争修复回归；ASan/TSan 下有效放大）

void TestConcurrentDestroy() {
    World world; // 默认多线程 JobSystem
    Scene& s = world.CreateScene("concurrent");
    std::vector<Entity> ents(4000);
    for (Entity& e : ents) e = s.Create();

    world.Jobs().ParallelFor((uint32_t)ents.size(), 64, [&](uint32_t b, uint32_t e2) {
        for (uint32_t i = b; i < e2; ++i) s.Destroy(ents[i]);
    });
    Expect(s.PendingDestroyCount() == 4000, "all destroys queued");
    s.CommitDestroys();
    Expect(s.AliveCount() == 0, "all destroys committed");
    Expect(s.PendingDestroyCount() == 0, "queue drained");
}

// DestroyQueueTag 语义（Destroy 打标、Commit 随销毁移除）

void TestDestroyQueueTagLifecycle() {
    Scene s("tag");
    Entity e = s.Create();
    s.Emplace<Transform2D>(e, Transform2D{{1, 1}});
    s.Destroy(e);
    Expect(s.Has<DestroyQueueTag>(e), "destroy tags entity");
    Expect(s.Alive(e), "still alive until commit");
    s.CommitDestroys();
    Expect(!s.Alive(e), "committed destroy");
}

// ---- F-03（2026-09-24）：SaveChannel 坏档防线——长度字段先验上限再分配 ----

void TestSaveChannelHardening() {
    using ecs::SaveChannel;

    // 正常 roundtrip（含空值条目）
    SaveChannel ch;
    const uint8_t payload[] = {1, 2, 3, 4, 5};
    Expect(ch.Set("score", payload, sizeof(payload)), "set entry");
    Expect(ch.Set("empty", nullptr, 0), "set empty value");
    const std::vector<uint8_t> bytes = ch.Encode();
    SaveChannel back;
    Expect(back.Decode(bytes.data(), bytes.size()), "roundtrip decode");
    Expect(back.Count() == 2 && back.GetLen("score") == 5, "roundtrip entries");

    auto u16 = [](std::vector<uint8_t>& v, uint16_t x) {
        v.push_back((uint8_t)x);
        v.push_back((uint8_t)(x >> 8));
    };
    auto u32 = [](std::vector<uint8_t>& v, uint32_t x) {
        v.push_back((uint8_t)x);
        v.push_back((uint8_t)(x >> 8));
        v.push_back((uint8_t)(x >> 16));
        v.push_back((uint8_t)(x >> 24));
    };
    auto header = [&](std::vector<uint8_t>& v, uint32_t count) {
        v.insert(v.end(), {'L', 'E', 'M', 'O', 'N', 'S', 'A', 'V'});
        u32(v, 1);
        u32(v, count);
    };

    // 巨额条目数：4 B 头声明 ~4e9 条 → 拒绝（老实现 reserve(count) 直接 bad_alloc）
    {
        std::vector<uint8_t> bad;
        header(bad, 0xFFFFFFFEu);
        SaveChannel s;
        Expect(!s.Decode(bad.data(), bad.size()), "huge entry count rejected");
    }
    // 巨额单值：valLen 声明近 4 GiB 而剩余 0 字节 → 拒绝（老实现 vector(valLen) 先炸）
    {
        std::vector<uint8_t> bad;
        header(bad, 1);
        u16(bad, 1);
        bad.push_back('k');
        u32(bad, 0xFFFFFFF0u);
        SaveChannel s;
        Expect(!s.Decode(bad.data(), bad.size()), "huge valLen rejected");
    }
    // 截断半档拒绝（宁可不载不载错）
    {
        std::vector<uint8_t> bad = bytes;
        bad.resize(bad.size() - 2);
        SaveChannel s;
        Expect(!s.Decode(bad.data(), bad.size()), "truncated archive rejected");
    }
    // 尾随垃圾拒绝（写侧精确落盘，多字节 = 损坏信号）
    {
        std::vector<uint8_t> bad = bytes;
        bad.push_back(0xAA);
        SaveChannel s;
        Expect(!s.Decode(bad.data(), bad.size()), "trailing garbage rejected");
    }
    // 重复 key 拒绝（写侧 map 语义不产生）
    {
        std::vector<uint8_t> bad;
        header(bad, 2);
        u16(bad, 1);
        bad.push_back('k');
        u32(bad, 1);
        bad.push_back('v');
        u16(bad, 1);
        bad.push_back('k');
        u32(bad, 1);
        bad.push_back('w');
        SaveChannel s;
        Expect(!s.Decode(bad.data(), bad.size()), "duplicate key rejected");
    }
    // key 超长拒绝（>255 与 Set 契约一致）
    {
        std::vector<uint8_t> bad;
        header(bad, 1);
        u16(bad, 300);
        bad.insert(bad.end(), 300, 'k');
        u32(bad, 0);
        SaveChannel s;
        Expect(!s.Decode(bad.data(), bad.size()), "oversized key rejected");
    }
}

// ---- F-08.2（2026-09-24）：销毁提交点通知接线——C++ 路径入队的销毁也走
// IScriptBackend::NotifyPendingDestroys（真链路 C# OnDestroy 在 script-tests 对拍）----

void TestDestroyNotifyWiring() {
    struct RecordingBackend final : ecs::IScriptBackend {
        std::vector<uint64_t> notified;
        int structuralCalls = 0;
        void TickBatch(ecs::World&, ecs::Scene&, float) override {}
        void PullPendingEvents(ecs::World&) override {}
        void DispatchEvents(ecs::World&, ecs::Scene&, const ecs::EventPacket*, uint32_t) override {}
        void ApplyStructural(ecs::World&, ecs::Scene&) override { ++structuralCalls; }
        void NotifyPendingDestroys(ecs::World&, ecs::Scene& s) override {
            // 与 ScriptHost 实现同形状：待销毁 ∩ ScriptBox，实体级 notified 去重
            // 恰好一次（空 tag 不进 each() 载荷——entt 3.15 语义，tag 只作过滤器）
            for (auto&& [ent, sb] : s.View<ecs::DestroyQueueTag, scripting::ScriptBox>().each()) {
                if (sb.notified & scripting::kScriptFlagDestroyNotified) continue;
                sb.notified |= scripting::kScriptFlagDestroyNotified;
                notified.push_back(ecs::Scene::FromEntt(ent).id);
            }
        }
    };

    WorldDesc d;
    d.threadCount = 1;
    World w(d);
    Scene& s = w.CreateScene("notify");
    w.SetActiveScene(&s);
    w.Pipeline().AddSystem(std::make_unique<DestroyCommitSystem>());
    w.Pipeline().ResolveOrder();
    RecordingBackend backend;
    w.SetScriptBackend(&backend);

    Entity scripted = s.Create();
    s.Emplace<scripting::ScriptBox>(scripted);
    Entity plain = s.Create();

    s.Destroy(scripted); // C++ 系统路径（战斗击杀/投射物到期同形状）
    s.Destroy(plain);
    w.Step(1.0f / 60.0f);
    Expect(backend.structuralCalls == 1, "structural applied each step");
    Expect(backend.notified.size() == 1 && backend.notified[0] == scripted.id,
           "queued scripted entity notified once (plain entity skipped)");
    Expect(!s.Alive(scripted) && !s.Alive(plain), "destroys committed after notify");

    w.Step(1.0f / 60.0f);
    Expect(backend.notified.size() == 1, "no duplicate notify on later steps");
}

// World::Step 无活动场景 = 空步不崩

void TestWorldStepWithoutScene() {
    WorldDesc d;
    d.threadCount = 1;
    World w(d);
    w.InstallDefaultSystems();
    w.Step(1.0f / 60.0f);
    Expect(w.TickIndex() == 0, "no-scene step is a no-op");
}

// ---- 实体生命周期：回收槽位版本号 ----

void TestVerifyEntityRecycleAndVersion() {
    Scene s("recycle");
    Entity a = s.Create(), b = s.Create();
    Expect(s.AliveCount() == 2, "recycle: two alive");
    s.Destroy(b);
    s.CommitDestroys();
    Expect(!s.Alive(b), "recycle: destroyed handle invalid");
    Expect(s.Alive(a), "recycle: sibling untouched");
    Expect(s.AliveCount() == 1, "recycle: exact alive count");
    Entity c = s.Create(); // entt 回收：同 index、version+1 → 新句柄 ≠ 旧句柄
    Expect(s.Alive(c), "recycle: new handle valid");
    Expect(c.id != b.id, "recycle: version bumped on reuse");
    Expect(!s.Alive(b), "recycle: stale handle stays invalid");
}

// ---- 存档不含已销毁实体（Each tombstone 过滤端到端回归）----

void TestVerifySaveExcludesDestroyed() {
    World w;
    Scene& s = w.CreateScene("dead");
    Entity keep = s.Create();
    s.Emplace<Transform2D>(keep, Transform2D{{1, 1}});
    Entity kill = s.Create();
    s.Emplace<Transform2D>(kill, Transform2D{{2, 2}});
    s.Destroy(kill);
    s.CommitDestroys();
    std::string text = SceneArchive::Save(s);
    size_t slots = 0;
    for (size_t p = text.find("\"Transform2D\""); p != std::string::npos;
         p = text.find("\"Transform2D\"", p + 1))
        ++slots;
    Expect(slots == 1, "save excludes destroyed entity");
    World w2;
    Scene& dst = w2.CreateScene("dead2");
    Expect(SceneArchive::Load(dst, text), "load after destroy ok");
    Expect(dst.AliveCount() == 1, "reload alive count exact");

    // review 2026-10-02 #10：销毁窗口期（已入队未提交）保存——引擎侧防线。
    // 修复前死实体 + "DestroyQueueTag":{} 一并入档，读档复活成永生僵尸
    // （Load 后永不入队，CommitDestroys 只消费当帧队列）
    World w3;
    Scene& s3 = w3.CreateScene("win");
    Entity keep3 = s3.Create();
    s3.Emplace<Transform2D>(keep3, Transform2D{{3, 3}});
    Entity dying = s3.Create();
    s3.Emplace<Transform2D>(dying, Transform2D{{4, 4}});
    s3.Destroy(dying); // 只入队，不提交（真实事故 = 结构轨 after 快照先于清队）
    std::string text3 = SceneArchive::Save(s3);
    size_t slots3 = 0;
    for (size_t p = text3.find("\"Transform2D\""); p != std::string::npos;
         p = text3.find("\"Transform2D\"", p + 1))
        ++slots3;
    Expect(slots3 == 1, "save excludes queued-destroy entity (window)");
    Expect(text3.find("DestroyQueueTag") == std::string::npos, "save never serializes queue tag");
    World w4;
    Scene& dst4 = w4.CreateScene("win2");
    Expect(SceneArchive::Load(dst4, text3), "load queued-window save ok");
    Expect(dst4.AliveCount() == 1, "queued-window reload has no zombie");
    // 旧档防御：手工构造含 DestroyQueueTag 的档（历史版本写出的形态）拒读标记
    const std::string legacy = "{\"schemaVersion\":2,\"name\":\"lz\",\"entities\":[{\"components\":"
                               "{\"Transform2D\":{\"pos\":[5,5]}},\"DestroyQueueTag\":{}}]}";
    World w5;
    Scene& dst5 = w5.CreateScene("lz");
    Expect(SceneArchive::Load(dst5, legacy), "legacy tag archive loads");
    Entity first5{};
    dst5.Each([&](Entity e) {
        if (first5.IsNull()) first5 = e;
    });
    Expect(dst5.AliveCount() == 1 && !first5.IsNull() && !dst5.Has<DestroyQueueTag>(first5),
           "legacy tag refused on read (no zombie marker)");
}

// ---- schema 版本防线 ----

void TestVerifySchemaVersionGuards() {
    World w;
    Scene& s = w.CreateScene("ver");
    Expect(!SceneArchive::Load(s, R"({"schemaVersion":999,"entities":[]})"),
           "future schema rejected");
    // [ISSUE-1 已修复] schemaVersion 类型错（字符串/负数/浮点）不再抛穿 Load，
    // 一律按无效版本拒绝（原 doc.value() 对字符串抛 type_error 抛穿加载器）
    bool threw = false;
    bool loaded = true;
    try {
        loaded = SceneArchive::Load(s, R"({"schemaVersion":"1","entities":[]})");
    } catch (const std::exception&) {
        threw = true;
    }
    Expect(!threw, "issue-1 fixed: string schemaVersion no longer throws");
    Expect(!loaded, "issue-1 fixed: string schemaVersion rejected");
    Expect(!SceneArchive::Load(s, R"({"schemaVersion":-1,"entities":[]})"),
           "issue-1 fixed: negative version rejected");
    Expect(!SceneArchive::Load(s, R"({"schemaVersion":1.5,"entities":[]})"),
           "issue-1 fixed: float version rejected");
}

// ---- [ISSUE-2 已修复] 数组段 count 越容量读档即钳制 ----
// {"StatusEffects":{"count":200}}（无 active 键）→ Load 钳到容量 4，StatSystem
// 按 count 遍历安全（修复前越界读写，ASan 实锤 Systems.cpp:501）

void TestVerifyArrayCountClamped() {
    World w;
    w.InstallDefaultSystems();
    Scene& s = w.CreateScene("cnt");
    w.SetActiveScene(&s);
    Expect(SceneArchive::Load(s, R"({"schemaVersion":1,"entities":[)"
                                 R"({"components":{"StatusEffects":{"count":200}}}]})"),
           "issue-2 fixed: load succeeds");
    bool checked = false;
    s.View<StatusEffects>().each([&](auto, StatusEffects& st) {
        Expect(st.count == 4, "issue-2 fixed: count clamped to capacity");
        checked = true;
    });
    Expect(checked, "issue-2 fixed: component present");
    w.Step(1.0f / 60.0f); // 修复前此步按 count=200 越界遍历
    s.View<StatusEffects>().each([&](auto, StatusEffects& st) {
        Expect(st.count == 0, "issue-2 fixed: zeroed effects expire safely");
    });
}

// ---- 空间查询：盒重叠 + 销毁后陈旧条目（惰性语义）----

void TestVerifyBoxQueryAndStaleEntries() {
    World w;
    Scene& s = w.CreateScene("box");
    for (int i = 0; i < 3; ++i)
        s.Emplace<Transform2D>(s.Create(), Transform2D{{(float)i * 100.0f, 0}});
    SpatialHash h;
    h.Configure(64.0f);
    h.Rebuild(s);
    int hits = 0;
    h.OverlapBox(s, Rect{Vec2{-10, -10}, Vec2{10, 10}}, QueryFilter{}, 0.0f,
                 [&](Entity, const Transform2D&) {
                     ++hits;
                     return true;
                 });
    Expect(hits == 1, "box: exact hit");
    hits = 0;
    h.OverlapBox(s, Rect{Vec2{-10, -10}, Vec2{10, 10}}, QueryFilter{}, 100.0f,
                 [&](Entity, const Transform2D&) {
                     ++hits;
                     return true;
                 });
    Expect(hits == 2, "box: probeRadius expands reach");

    // 销毁后、重建前：valid() 过滤保命中正确；条目留在 items 里（惰性）
    Entity victim;
    int idx = 0;
    s.Each([&](Entity e) {
        if (idx++ == 1) victim = e;
    });
    Expect(!victim.IsNull(), "stale: entity located");
    s.Destroy(victim);
    s.CommitDestroys();
    hits = 0;
    h.OverlapCircle(s, {100, 0}, 10.0f, QueryFilter{}, 0.0f, [&](Entity, const Transform2D&) {
        ++hits;
        return true;
    });
    Expect(hits == 0, "stale: destroyed entity never reported");
    Expect(h.ItemCount() == 3, "stale: entry lingers until rebuild");
    h.Rebuild(s);
    Expect(h.ItemCount() == 2, "stale: rebuild purges");
}

// ---- Raycast：命中/截距/背面/退化方向 ----

void TestVerifyRaycast() {
    World w;
    Scene& s = w.CreateScene("ray");
    s.Emplace<Transform2D>(s.Create(), Transform2D{{0, 0}});
    SpatialHash h;
    h.Configure(64.0f);
    h.Rebuild(s);

    RayHit hit = h.Raycast(s, {-100, 0}, {1, 0}, 1000.0f, QueryFilter{}, 8.0f);
    Expect(!hit.entity.IsNull(), "ray: hits entity ahead");
    ExpectNear(hit.distance, 100.0f, 0.5f, "ray: distance to center");
    ExpectNear(hit.normal.x, -1.0f, 1e-4f, "ray: normal faces ray origin");

    hit = h.Raycast(s, {-100, 0}, {1, 0}, 50.0f, QueryFilter{}, 8.0f);
    Expect(hit.entity.IsNull(), "ray: beyond maxDist no hit");

    hit = h.Raycast(s, {100, 0}, {1, 0}, 1000.0f, QueryFilter{}, 8.0f);
    Expect(hit.entity.IsNull(), "ray: behind origin no hit");

    hit = h.Raycast(s, {0, 0}, {0, 0}, 1000.0f, QueryFilter{}, 8.0f);
    Expect(!hit.entity.IsNull(), "ray: degenerate dir falls back to point query");
}

// ---- ParallelFor：单线程档与多线程池全下标恰一次覆盖 ----

void TestVerifyParallelForCoverage() {
    for (int mode = 0; mode < 2; ++mode) {
        WorldDesc d;
        d.threadCount = (mode == 0) ? 1 : 0; // 单线程诊断档 / 多线程池
        World w(d);
        constexpr uint32_t kN = 997; // 质数：块不整除
        std::vector<uint8_t> seen(kN, 0);
        std::atomic<uint32_t> total{0};
        w.Jobs().ParallelFor(kN, 64, [&](uint32_t b, uint32_t e) {
            for (uint32_t i = b; i < e; ++i) {
                seen[i] += 1;
                total.fetch_add(1, std::memory_order_relaxed);
            }
        });
        Expect(total.load() == kN, "pfor: every index visited");
        bool allOnce = true;
        for (uint32_t i = 0; i < kN; ++i) allOnce &= (seen[i] == 1);
        Expect(allOnce, "pfor: each index exactly once");
    }
}

// ---- RingQueue：跨多次扩容的 FIFO 保序 ----

void TestVerifyRingQueueGrowOrder() {
    RingQueue<uint32_t> q(16); // 16→32→…→1024 连续扩容
    for (uint32_t i = 0; i < 600; ++i) Expect(q.Push(i * 3u), "rq: push ok");
    Expect(q.Size() == 600, "rq: size after growth");
    for (uint32_t i = 0; i < 600; ++i) {
        Expect(q.Front() == i * 3u, "rq: FIFO order across growth");
        q.Pop();
    }
    Expect(q.Empty(), "rq: drained");
}

// ---- Pool：槽位复用与计数 ----

void TestVerifyPoolSlotReuse() {
    Pool<uint64_t> p;
    uint32_t a = p.Acquire(11u);
    uint32_t b = p.Acquire(22u);
    Expect(a != b, "pool: distinct slots");
    p.Release(a);
    uint32_t c = p.Acquire(33u);
    Expect(c == a, "pool: freed slot reused");
    Expect(p[c] == 33u && p[b] == 22u, "pool: payloads intact");
    Expect(p.ReuseHits() == 1 && p.LiveCount() == 2, "pool: counters");
}

// ---- 系统子流：同 id 稳定、异 id 独立 ----

void TestVerifySystemRngStreams() {
    World w;
    Rng& r0 = w.SystemRng(0);
    Expect(&w.SystemRng(0) == &r0, "srng: same id same stream");
    Rng& r1 = w.SystemRng(1);
    Expect(r0.Next() != r1.Next(), "srng: distinct streams diverge");
}

// ---- TeamTable：越界关系安全方向 ----

void TestVerifyTeamRangeSafety() {
    TeamTable t = TeamTable::Default();
    Expect(t.Relation(32, 0) == TeamRelation::Neutral, "team: oor returns neutral");
    Expect(t.Relation(4096, 1) == TeamRelation::Neutral, "team: big oor neutral");
}

// ---- null EntityRef roundtrip ----

void TestVerifyNullEntityRefRoundtrip() {
    World w;
    Scene& src = w.CreateScene("nullref");
    Entity e = src.Create();
    src.Emplace<Transform2D>(e, Transform2D{{5, 5}});
    src.Emplace<Hierarchy>(e); // 全 null
    std::string text = SceneArchive::Save(src);
    Expect(text.find("null") != std::string::npos, "null refs serialized");
    World w2;
    Scene& dst = w2.CreateScene("nullref2");
    Expect(SceneArchive::Load(dst, text), "null-ref load ok");
    dst.View<Hierarchy>().each([&](auto, Hierarchy& h) {
        Expect(h.parent.IsNull() && h.firstChild.IsNull(), "null refs preserved");
    });
}

// ---- StateHash：重复稳定 / 裸实体不可见 / 状态变化可检 ----

void TestVerifyStateHashStability() {
    World w;
    Scene& s = w.CreateScene("hashst");
    Entity e = s.Create();
    s.Emplace<Transform2D>(e, Transform2D{{1, 2}});
    s.Emplace<Health>(e, Health{.max = 10.0f, .cur = 10.0f});
    uint64_t h0 = ComputeStateHash(s);
    Expect(h0 == ComputeStateHash(s), "hash: deterministic repeat");
    Entity spare = s.Create(); // 无组件实体不改变哈希
    Expect(ComputeStateHash(s) == h0, "hash: bare entity invisible");
    s.Destroy(spare);
    s.CommitDestroys();
    Expect(ComputeStateHash(s) == h0, "hash: bare destroy invisible");
    s.Get<Health>(e).cur = 5.0f;
    Expect(ComputeStateHash(s) != h0, "hash: state change detectable");
}

void TestHierarchyChainLifecycle() {
    using namespace lemon::ecs;
    RegisterAllComponents();
    Scene s("h");
    auto mk = [&](Vec2 pos, float rot = 0, Vec2 scale = {1, 1}) {
        Entity e = s.Create();
        Transform2D t;
        t.pos = pos;
        t.rot = rot;
        t.scale = scale;
        s.Emplace<Transform2D>(e, t);
        return e;
    };

    // 直链 root→a→b→c（深度 0/1/2/3）
    Entity root = mk({100, 50});
    Entity a = mk({10, 0});
    Entity b = mk({5, 5}, lemon::math::kPi / 2); // 90°（Y 向下系顺时针）
    Entity c = mk({20, 0}, 0, {2, 3});
    Expect(SceneSetParent(s, a, root), "set parent a-root");
    Expect(SceneSetParent(s, b, a), "set parent b-a");
    Expect(SceneSetParent(s, c, b), "set parent c-b");
    Expect(HierarchyDepth(s, root) == 0 && HierarchyDepth(s, c) == 3, "depths 0..3");

    // 链完整性：firstChild/next/prev 三向
    Expect(s.Get<Hierarchy>(root).firstChild == a, "root.firstChild = a");
    Expect(s.Get<Hierarchy>(a).firstChild == b && s.Get<Hierarchy>(b).firstChild == c,
           "chain down");
    Expect(s.Get<Hierarchy>(b).prev.IsNull() && s.Get<Hierarchy>(c).next.IsNull(),
           "edge links null");

    // 防环：父挂到自身后代被拒（N6 遗留项落地）
    Expect(!SceneSetParent(s, root, c), "cycle reject root→c(descendant)");
    Expect(!SceneSetParent(s, a, b), "cycle reject a→b(child)");
    Expect(!SceneSetParent(s, a, a), "self parent reject");
    // 拒绝后结构不变
    Expect(s.Get<Hierarchy>(a).parent == root, "a still child of root");

    // 深度上限：c(3) 下再挂 6 层——第 6 层落深度 9 > 8 被拒；成功链最深恰为 8
    Entity chain[6];
    Entity cur = c;
    bool lastOk = true;
    for (int i = 0; i < 6; ++i) {
        chain[i] = mk({0, 0});
        lastOk = SceneSetParent(s, chain[i], cur);
        if (lastOk) cur = chain[i];
    }
    Expect(!lastOk, "depth limit 8 enforced");
    Expect(HierarchyDepth(s, cur) == (int)kMaxHierarchyDepth, "deepest = exactly 8");

    // 世界矩阵合成 vs Mat3x2 参照（内核 #1 验收口径）
    WorldTransform2D wt;
    Expect(ComputeWorldTransform(s, c, wt), "world transform ok");
    Mat3x2 ref = Mat3x2::FromTRS({100, 50}, 0, {1, 1}) * Mat3x2::FromTRS({10, 0}, 0, {1, 1}) *
                 Mat3x2::FromTRS({5, 5}, lemon::math::kPi / 2, {1, 1}) *
                 Mat3x2::FromTRS({20, 0}, 0, {2, 3});
    Vec2 rp = ref.Apply({0, 0}); // 原点 = 世界位置
    ExpectNear(wt.pos.x, rp.x, 1e-3f, "world pos x matches Mat3x2");
    ExpectNear(wt.pos.y, rp.y, 1e-3f, "world pos y matches Mat3x2");
    ExpectNear(wt.rot, lemon::math::kPi / 2, 1e-5f, "world rot additive");
    Expect(wt.scale == Vec2(2, 3), "world scale multiplicative");
    // 带父缩放/父旋转的局部偏移：换 b 的 scale 验证 scale ⊙ localPos
    s.Get<Transform2D>(b).scale = {2, 2};
    Expect(ComputeWorldTransform(s, c, wt), "recompute ok");
    Mat3x2 ref2 = Mat3x2::FromTRS({100, 50}, 0, {1, 1}) * Mat3x2::FromTRS({10, 0}, 0, {1, 1}) *
                  Mat3x2::FromTRS({5, 5}, lemon::math::kPi / 2, {2, 2}) *
                  Mat3x2::FromTRS({20, 0}, 0, {2, 3});
    Vec2 rp2 = ref2.Apply({0, 0});
    ExpectNear(wt.pos.x, rp2.x, 1e-3f, "scaled parent pos x");
    ExpectNear(wt.pos.y, rp2.y, 1e-3f, "scaled parent pos y");
    s.Get<Transform2D>(b).scale = {1, 1};

    // 摘根：b 摘出后 a.firstChild 置空、b 子树随行
    Expect(SceneDetach(s, b), "detach b");
    Expect(s.Get<Hierarchy>(a).firstChild.IsNull(), "a.firstChild cleared");
    Expect(s.Get<Hierarchy>(b).firstChild == c, "b keeps child c");
    Expect(HierarchyDepth(s, c) == 1, "c depth 1 after detach");

    // 重挂：b→root（a 的兄弟）
    Expect(SceneSetParent(s, b, root), "rehang b under root");
    Expect(s.Get<Hierarchy>(root).firstChild == b, "new child at head");
    Expect(s.Get<Hierarchy>(b).next == a && s.Get<Hierarchy>(a).prev == b, "sibling links");

    // 子树销毁：root 树（a、b、c 及深链）全灭，旁观者存活
    Entity outsider = mk({0, 0});
    SceneDestroyEntityTree(s, root);
    s.CommitDestroys();
    Expect(!s.Alive(root) && !s.Alive(a) && !s.Alive(b) && !s.Alive(c), "tree destroyed");
    Expect(s.Alive(outsider), "outsider survives");
    bool chainGone = true;
    for (int i = 0; i < 5; ++i) // chain[5] 被深度拒绝、不在树内 → 必须存活
        if (s.Alive(chain[i])) chainGone = false;
    Expect(chainGone, "in-tree chain destroyed");
    Expect(s.Alive(chain[5]), "rejected node not in tree, survives");
}

// ---- 用户手测复现（2026-09-21 第八轮）：C 拖拽挂到 P（全链）→ 存档往返
// （= EnterPlay 快照同路径）→ 移动 P → 子世界位置必须跟随。此前数学有测、
// 往返只有"半链"（手写 Hierarchy 只设 parent）覆盖，全链往返 + 跟随是空白。----

void TestVerifyFullChainFollowsAfterRoundtrip() {
    World w;
    Scene& s = w.CreateScene("chain");
    Entity p = s.Create();
    s.Emplace<Transform2D>(p, Transform2D{{100, 100}, 0, {1, 1}});
    Entity c = s.Create();
    s.Emplace<Transform2D>(c, Transform2D{{10, 0}, 0, {1, 1}});
    Expect(SceneSetParent(s, c, p), "full link c-p");

    const std::string json = SceneArchive::Save(s);
    World w2;
    Scene& d = w2.CreateScene("reload");
    Expect(SceneArchive::Load(d, json), "reload ok");

    Entity dp{}, dc{}; // 找回：父也持有 Hierarchy（firstChild），按 parent 非空判子
    d.Each([&](Entity e) {
        const Hierarchy* h = d.TryGet<Hierarchy>(e);
        if (h && !h->parent.IsNull())
            dc = e;
        else
            dp = e;
    });
    Expect(!dp.IsNull() && !dc.IsNull(), "entities located after reload");
    Expect(d.Get<Hierarchy>(dc).parent == dp, "parent remapped");
    Expect(d.Get<Hierarchy>(dp).firstChild == dc, "firstChild remapped（全链非半链）");

    // 父移动（同脚本每帧写 pos）→ 子世界位置精确跟随（渲染消费端同一函数）
    WorldTransform2D wt;
    Expect(ComputeWorldTransform(d, dc, wt), "world before");
    const Vec2 before = wt.pos;
    ExpectNear(before.x, 110.0f, 1e-4f, "child world = parent+local");
    d.Get<Transform2D>(dp).pos = {150, 100};
    Expect(ComputeWorldTransform(d, dc, wt), "world after");
    ExpectNear(wt.pos.x, 160.0f, 1e-4f, "child follows parent move after roundtrip");
    ExpectNear(wt.pos.y, 100.0f, 1e-4f, "child y follows");
}

// ---- M6a 批② T5：存档分档——三档三文件路径 + 三档落盘/回读独立 + 空通道跳过 +
// 旧 game.sav 惰性迁移（写恒写新名）+ 坏档兜底按档隔离 + 16 MiB 上限按档 ----

#ifdef LEMON_EDITOR_CORE
void TestSaveChannelSplits() {
    namespace fs = std::filesystem;
    using ecs::SaveChannel;
    using namespace lemon::ecs;
    using lemon::assets::SaveStore;

    const std::string tag = std::to_string(lemon::CurrentProcessId());
    const fs::path root = fs::temp_directory_path() / ("lemon-test-savesplit-" + tag);
    std::error_code ec;
    fs::remove_all(root, ec);
    const std::string rootStr = root.string();

    // ① 三档路径独立；越界 ch 钳 slot_0（防御面——装载循环只传常量不触发）；
    //    空 root = 空串（无项目裸会话全链 no-op 口径）
    Expect(SaveStore::FilePath(rootStr, kSaveSlot).ends_with("slot_0.sav") &&
               SaveStore::FilePath(rootStr, kSaveSettings).ends_with("settings.sav") &&
               SaveStore::FilePath(rootStr, kSaveMeta).ends_with("meta.sav"),
           "three channel file paths");
    Expect(SaveStore::FilePath(rootStr, 77).ends_with("slot_0.sav"), "oob ch clamps to slot_0");
    Expect(SaveStore::FilePath("", kSaveSlot).empty(), "empty root -> empty path");

    // ② 三档落盘互不覆盖 + 空通道跳过 + 回读独立
    {
        SaveChannel slot;
        slot.Set("run.kills", "5", 1);
        SaveChannel meta;
        meta.Set("vs.best", "77", 2);
        Expect(SaveStore::Write(rootStr, kSaveSlot, slot), "slot written");
        Expect(SaveStore::Write(rootStr, kSaveMeta, meta), "meta written");
        Expect(!SaveStore::Write(rootStr, kSaveSettings, SaveChannel{}), "empty channel skipped");
        Expect(!SaveStore::Write("", kSaveSlot, slot), "no project -> write no-op");
        Expect(fs::exists(root / ".lemon/saves/slot_0.sav", ec), "slot file exists");
        Expect(fs::exists(root / ".lemon/saves/meta.sav", ec), "meta file exists");
        Expect(!fs::exists(root / ".lemon/saves/settings.sav", ec), "settings not written");
        SaveChannel back;
        SaveStore::Load(rootStr, kSaveMeta, back);
        Expect(back.Count() == 1 && back.GetLen("vs.best") == 2, "meta roundtrip");
    }

    // ③ 旧 game.sav 惰性迁移：删新档留旧名 → 载入走旧路径；写恒写新名、旧文件保留
    {
        fs::remove(root / ".lemon/saves/slot_0.sav", ec);
        SaveChannel legacy;
        legacy.Set("old.key", "v1", 2);
        {
            std::ofstream f(root / ".lemon/saves/game.sav", std::ios::binary | std::ios::trunc);
            const std::vector<uint8_t> b = legacy.Encode();
            f.write((const char*)b.data(), (std::streamsize)b.size());
        }
        SaveChannel back;
        SaveStore::Load(rootStr, kSaveSlot, back);
        Expect(back.Count() == 1 && back.GetLen("old.key") == 2, "legacy game.sav lazy-migrated");
        Expect(SaveStore::Write(rootStr, kSaveSlot, back), "migrated slot written to new name");
        Expect(fs::exists(root / ".lemon/saves/slot_0.sav", ec), "new name file back");
        Expect(fs::exists(root / ".lemon/saves/game.sav", ec), "legacy file untouched");
        SaveChannel st; // settings 无旧名对应 → 不受迁移影响
        SaveStore::Load(rootStr, kSaveSettings, st);
        Expect(st.Count() == 0, "settings independent of legacy");
    }

    // ④ 坏档兜底按档隔离：settings 主档垃圾 + .bak 好档 → 走 bak；meta 垃圾无
    //    bak → 空通道开局；slot 既有好档不受邻居损坏影响
    {
        SaveChannel good;
        good.Set("bak.key", "1", 1);
        {
            std::ofstream f(root / ".lemon/saves/settings.sav", std::ios::binary | std::ios::trunc);
            f << "garbage-not-lemonsav";
        }
        {
            std::ofstream f(root / ".lemon/saves/settings.sav.bak",
                            std::ios::binary | std::ios::trunc);
            const std::vector<uint8_t> b = good.Encode();
            f.write((const char*)b.data(), (std::streamsize)b.size());
        }
        {
            std::ofstream f(root / ".lemon/saves/meta.sav", std::ios::binary | std::ios::trunc);
            f << "garbage-too";
        }
        SaveChannel st, mt, sl;
        SaveStore::Load(rootStr, kSaveSettings, st);
        SaveStore::Load(rootStr, kSaveMeta, mt);
        SaveStore::Load(rootStr, kSaveSlot, sl);
        Expect(st.Count() == 1 && st.GetLen("bak.key") == 1, "settings bad main -> bak fallback");
        Expect(mt.Count() == 0, "meta corrupt no bak -> empty start");
        Expect(sl.GetLen("old.key") == 2, "slot unaffected by other channels' corruption");
    }

    // ⑤ 16 MiB 上限按档：slot_0.sav 超限 → 跳过不 slurp；主档视为不存在 → 旧名
    //    惰性迁移接力（game.sav 仍在，链式兜底语义钉板）
    {
        {
            std::ofstream f(root / ".lemon/saves/slot_0.sav", std::ios::binary | std::ios::trunc);
            const std::vector<char> big((16u << 20) + 1, 'x');
            f.write(big.data(), (std::streamsize)big.size());
        }
        SaveChannel sl;
        SaveStore::Load(rootStr, kSaveSlot, sl);
        Expect(sl.GetLen("old.key") == 2, "oversize slot skipped, legacy migration takes over");
    }

    fs::remove_all(root, ec);
}
#endif // LEMON_EDITOR_CORE

// poolDataFn（D5 基础）：全组件可判 + 容量内基址不动 + 越容量搬移

#ifdef LEMON_EDITOR_CORE
void TestPoolDataStable() {
    using namespace lemon::ecs;
    auto& reg = ComponentRegistry::Instance();
    for (uint16_t id = 0; id < reg.Count(); ++id) {
        Expect(reg.At(id).poolDataFn != nullptr, "poolDataFn registered for all");
    }
    const ComponentMeta* tfm = reg.Find("Transform2D");
    Expect(tfm && tfm->poolDataFn, "transform poolDataFn");
    World w;
    Scene& s = w.CreateScene("pd");
    w.SetActiveScene(&s);
    Entity e0 = s.Create();
    s.Emplace<Transform2D>(e0, Transform2D{{0, 0}});
    Expect(tfm->poolDataFn(s) != nullptr, "pool base valid after first emplace");
    // 双观察式（与 entt 扩容策略无关）：连续 emplace 中应存在"基址稳定段"
    // （容量余量内，D5 零误伤的机制保证）与随后的"搬移拍"（越容量重分配）
    const void* prev = tfm->poolDataFn(s);
    int stableRun = 0;
    bool sawStable = false, movedAfterStable = false;
    for (int i = 0; i < 100000 && !movedAfterStable; i++) {
        Entity e = s.Create();
        s.Emplace<Transform2D>(e, Transform2D{{0, 0}});
        const void* cur = tfm->poolDataFn(s);
        if (cur == prev) {
            if (++stableRun >= 3) sawStable = true; // 连续 3 次不动 = 稳定段实证
        } else {
            if (sawStable) movedAfterStable = true; // 稳定段后的搬移拍
            stableRun = 0;
        }
        prev = cur;
    }
    Expect(sawStable, "within-capacity stable run observed");
    Expect(movedAfterStable, "over-capacity emplace moves pool base");
}
#endif // LEMON_EDITOR_CORE

} // namespace

void RunEcsTests() {
    TestSceneLifecycle();
    TestWorldServices();
    TestComponentRegistry();
    TestVerifyWorldAutoRegistersCatalog();
    TestSceneArchive();
    TestTeamTable();
    TestSpatialHash();
    TestSystemPipelineOrder();
    TestSimulationEndToEnd();
    TestSeparationForce();
    TestArchiveArraySegAndRuntimeFields();
    TestArchiveMalformedTolerance();
    TestSpatialHashRangeClamp();
    TestSpatialHashQueryFastPath();
    TestTargetBoardGridEquivalence();
    TestTargetBoardBadCoordDefense();
    TestTargetBoardParallelRebuildIsomorphic();
    TestConcurrentDestroy();
    TestDestroyQueueTagLifecycle();
    TestSaveChannelHardening();
    TestDestroyNotifyWiring();
    TestWorldStepWithoutScene();
    TestVerifyEntityRecycleAndVersion();
    TestVerifySaveExcludesDestroyed();
    TestVerifySchemaVersionGuards();
    TestVerifyArrayCountClamped();
    TestVerifyBoxQueryAndStaleEntries();
    TestVerifyRaycast();
    TestVerifyParallelForCoverage();
    TestVerifyRingQueueGrowOrder();
    TestVerifyPoolSlotReuse();
    TestVerifySystemRngStreams();
    TestVerifyTeamRangeSafety();
    TestVerifyNullEntityRefRoundtrip();
    TestVerifyStateHashStability();
    TestHierarchyChainLifecycle();
    TestVerifyFullChainFollowsAfterRoundtrip();
#ifdef LEMON_EDITOR_CORE
    TestSaveChannelSplits();
#endif
#ifdef LEMON_EDITOR_CORE
    TestPoolDataStable();
#endif
}
