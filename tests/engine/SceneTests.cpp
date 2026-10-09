// Lemon 引擎单测 — SceneTests — 档2 场景管理（ADR-017；M7c 批⑥/⑥b/⑥c）
// 域（SceneMembership 打标/组清场/DDOL 根树幸存 · World 场景档案 · SceneArchive
// BuildInto 追加装载与 Load 清空对照 · StateHash 对 membership 不敏感 · 批⑥b
// SceneSwitcher 换场编排 + SpawnPrefab 出生打标 · 批⑥c 多次换场/DDOL 轨迹 +
// 孪生世界确定性轨迹）。
#include "TestFramework.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "Components/BehaviorComponents.h"
#include "Components/CoreComponents.h"
#include "Components/RenderComponents.h"
#include "ECS/Hierarchy.h"
#include "ECS/SceneMembership.h"
#include "ECS/SceneSwitcher.h"
#include "ECS/StateHash.h"
#include "ECS/World.h"
#include "Serialization/SceneArchive.h"

using namespace lemon;
using namespace lemon::ecs;

namespace {

/// 造小场景并序列化（count 实体，Transform2D + Meta.tag）——BuildInto 测试源
/// （Save 产文本，规避手写档格式漂移）
std::string MakeSceneText(const char* name, const char* tag, int count) {
    World w;
    Scene& s = w.CreateScene(name);
    for (int i = 0; i < count; ++i) {
        Entity e = s.Create();
        s.Emplace<Transform2D>(e, Transform2D{{float(10 * (i + 1)), 20.0f}});
        Meta& m = s.Emplace<Meta>(e);
        std::strcpy(m.tag, tag);
    }
    return SceneArchive::Save(s);
}

void TestSceneMembershipStampAndCount() {
    World w;
    Scene& s = w.CreateScene("stamp");
    s.Create();
    s.Create();
    s.Create();
    Expect(CountSceneGroup(s, kSceneHandleUnassigned) == 3, "unstamped -> unassigned group");
    uint32_t h1 = w.CreateSceneRecord("Grass", "Scenes/Grass.scene");
    Expect(h1 != kSceneHandleUnassigned, "handle issued from 1 (0 reserved)");
    Expect(StampSceneMembership(s, h1) == 3, "fresh load stamps all");
    Expect(CountSceneGroup(s, h1) == 3, "stamped group count");
    Expect(CountSceneGroup(s, kSceneHandleUnassigned) == 0, "none left unassigned");
    // 增量语义：已指派实体不被重打标（幸存者来源组保留）
    uint32_t h2 = w.CreateSceneRecord("Volcano", "Scenes/Volcano.scene");
    s.Create(); // 新未打标实体（模拟 BuildInto 二装的新建段）
    Expect(StampSceneMembership(s, h2) == 1, "incremental stamp only recruits unassigned");
    Expect(CountSceneGroup(s, h1) == 3 && CountSceneGroup(s, h2) == 1, "groups disjoint");
}

void TestSceneGroupTeardownKeepsDDOL() {
    World w;
    Scene& s = w.CreateScene("teardown");
    uint32_t h1 = w.CreateSceneRecord("Grass");
    uint32_t h2 = w.CreateSceneRecord("Volcano");

    // 组1：root ← child（SceneSetParent 建链）+ 孤儿实体；组2：单实体
    Entity root = s.Create();
    s.Emplace<Transform2D>(root, Transform2D{{0, 0}});
    Entity child = s.Create();
    s.Emplace<Transform2D>(child, Transform2D{{1, 1}});
    Expect(SceneSetParent(s, child, root), "child attached");
    Entity lone = s.Create();
    Entity other = s.Create();
    StampSceneMembership(s, h1); // 此时全部未打标 → 全进 h1
    // other 改判 h2（覆写需要先抹掉：直接改组件字段——测试内构造，非装载路径）
    s.Get<SceneMembership>(other).scene = h2;

    Expect(MarkDontDestroyOnLoad(s, root) == 1, "ddol marks root only (batch7 D1 root-bit)");
    Expect(CountDontDestroyOnLoad(s) == 1, "ddol root count (lineage via ancestry)");

    // 组1 清场：只带走非 DDOL 系的 lone；root（本位）与 child（祖先链）幸存
    Expect(QueueDestroySceneGroup(s, h1) == 1, "only non-ddol member queued");
    s.CommitDestroys();
    Expect(!s.Alive(lone), "lone destroyed after commit");
    Expect(s.Alive(root) && s.Alive(child), "ddol tree survives teardown");
    Expect(CountSceneGroup(s, h1) == 2, "survivors keep origin scene value");
    Expect(s.Alive(other), "other group untouched by h1 teardown");

    // 组2 清场：无 DDOL 全灭；DDOL 在任何组清场下幸存
    Expect(QueueDestroySceneGroup(s, h2) == 1, "h2 fully queued");
    s.CommitDestroys();
    Expect(!s.Alive(other), "h2 destroyed");
    Expect(QueueDestroySceneGroup(s, h1) == 0, "repeat teardown of survivors is no-op");
    Expect(CountDontDestroyOnLoad(s) == 1, "ddol persists across group teardowns");
}

// 批⑦ D1 根位式语义：后挂子实体随根幸存（Unity 对齐）、移出 DDOL 树随新归属清场
void TestDdolRootBitLineageSemantics() {
    World w;
    Scene& s = w.CreateScene("ddol-lineage");
    const uint32_t h1 = w.CreateSceneRecord("A");
    Entity root = s.Create();
    s.Emplace<Transform2D>(root, Transform2D{{0, 0}});
    Entity early = s.Create(); // 标记前已在树内
    s.Emplace<Transform2D>(early, Transform2D{{1, 1}});
    Expect(SceneSetParent(s, early, root), "early child attached");
    StampSceneMembership(s, h1);
    Expect(MarkDontDestroyOnLoad(s, root) == 1, "root marked (single bit)");
    // 后挂：标记之后 spawn + attach 到 DDOL 根下（打 active = h1，模拟 Instantiate 落点）
    Entity late = s.Create();
    s.Emplace<Transform2D>(late, Transform2D{{2, 2}});
    s.Emplace<SceneMembership>(late).scene = h1;
    Expect(SceneSetParent(s, late, root), "late child attached after marking");
    Expect(CollectDontDestroyOnLoadLineage(s).size() == 3, "lineage = root+early+late");

    // 清场：后挂 late 幸存（Unity"随根幸存"——⑥c review 前置④ 的裁决面）
    Expect(QueueDestroySceneGroup(s, h1) == 0, "nothing queued (all ddol lineage)");
    // 移出：late 摘根成普通根实体 → 再清场被收（Unity：DDOL 不跟人走）
    Expect(SceneDetach(s, late), "late detached out of ddol tree");
    Expect(QueueDestroySceneGroup(s, h1) == 1, "moved-out child queued");
    s.CommitDestroys();
    Expect(!s.Alive(late) && s.Alive(root) && s.Alive(early),
           "moved-out dies, ddol root tree survives");
}

// 批⑦ D2「除 DDOL 系外全清」+ 未指派自愈报告（组 0 泄漏的 WARN 面）
void TestClearAllExceptDdolLineageReportsUnassigned() {
    World w;
    Scene& s = w.CreateScene("clear-all");
    const uint32_t h1 = w.CreateSceneRecord("A");
    Entity ddol = s.Create();
    Entity victim = s.Create();
    StampSceneMembership(s, h1); // ddol + victim → h1
    MarkDontDestroyOnLoad(s, ddol);
    Entity leak = s.Create(); // 未打标（不变量破坏模拟——组 0 泄漏）
    Expect(CountSceneGroup(s, kSceneHandleUnassigned) == 1, "leak sits in group 0");

    const SceneClearReport rep = QueueDestroyAllExceptDdolLineage(s);
    Expect(rep.queued == 2, "victim + unassigned leak both queued");
    Expect(rep.unassignedCollected == 1, "unassigned collection reported (WARN face)");
    s.CommitDestroys();
    Expect(!s.Alive(victim) && !s.Alive(leak), "victim + leak cleared");
    Expect(s.Alive(ddol), "ddol lineage survives");
    Expect(CountSceneGroup(s, kSceneHandleUnassigned) == 0, "group 0 self-healed");
}

void TestSceneArchiveBuildIntoAppends() {
    const std::string textA = MakeSceneText("Grass", "mob", 2);
    const std::string textB = MakeSceneText("Volcano", "lava", 1);

    World w;
    Scene& s = w.CreateScene("runtime");
    uint32_t hA = w.CreateSceneRecord("Grass", "Scenes/Grass.scene");
    uint32_t hB = w.CreateSceneRecord("Volcano", "Scenes/Volcano.scene");

    Expect(SceneArchive::BuildInto(s, textA), "build A ok");
    Expect(s.AliveCount() == 2, "A entities built");
    Expect(StampSceneMembership(s, hA) == 2, "A stamped");
    // A 组首个实体标 DDOL（模拟跨场管理器宿主）
    Entity ddol = Entity::Null();
    s.Each([&](Entity e) {
        if (s.Get<SceneMembership>(e).scene == hA && ddol.IsNull()) ddol = e;
    });
    Expect(MarkDontDestroyOnLoad(s, ddol) == 1, "one A entity marked ddol");

    // 二装 B：不清空——A 实体（含 DDOL）在场共存，B 新实体追加
    Expect(SceneArchive::BuildInto(s, textB), "build B ok");
    Expect(s.AliveCount() == 3, "B appended, A survivors intact");
    Expect(StampSceneMembership(s, hB) == 1, "incremental stamp recruits only B");
    Expect(CountSceneGroup(s, hA) == 2 && CountSceneGroup(s, hB) == 1, "groups disjoint");
    Expect(std::string(s.Name()) == "Volcano", "BuildInto restores doc name");

    // 组 A 清场：带走非 DDOL 的另一实体，DDOL 幸存，B 完好
    Expect(QueueDestroySceneGroup(s, hA) == 1, "A teardown takes non-ddol only");
    s.CommitDestroys();
    Expect(s.AliveCount() == 2, "ddol + B remain");
    Expect(CountSceneGroup(s, hB) == 1, "B untouched");

    // 坏档拒绝且场景不动（BuildInto 前半失败语义）
    const uint32_t before = s.AliveCount();
    Expect(!SceneArchive::BuildInto(s, "{ not json"), "invalid json rejected");
    Expect(s.AliveCount() == before, "scene untouched on parse failure");

    // 对照：Load 仍是清空重建语义（编辑态/现状兼容——DDOL 也一并清）
    Expect(SceneArchive::Load(s, textA), "load ok");
    Expect(s.AliveCount() == 2, "load clears everything incl ddol (edit-mode semantics)");
}

void TestStateHashIgnoresMembership() {
    World w;
    Scene& s = w.CreateScene("hash");
    for (int i = 0; i < 3; ++i) {
        Entity e = s.Create();
        s.Emplace<Transform2D>(e, Transform2D{{float(i), float(i * 2)}});
    }
    const uint64_t h0 = ComputeStateHash(s);
    StampSceneMembership(s, w.CreateSceneRecord("Grass"));
    MarkDontDestroyOnLoad(s, [&] {
        Entity first = Entity::Null();
        s.Each([&](Entity e) {
            if (first.IsNull()) first = e;
        });
        return first;
    }());
    Expect(ComputeStateHash(s) == h0, "membership/ddol flags not hashed (zero re-record)");
}

void TestWorldSceneRecords() {
    World w;
    Expect(w.ActiveSceneHandle() == kSceneHandleUnassigned, "active handle starts 0");
    uint32_t h1 = w.CreateSceneRecord("MainMenu", "Scenes/MainMenu.scene");
    uint32_t h2 = w.CreateSceneRecord("Grass", "Scenes/Grass.scene");
    Expect(h1 != h2 && h1 != 0 && h2 != 0, "handles distinct, nonzero");
    Expect(w.SceneRecordCount() == 2, "record count");
    World::SceneRecord* r1 = w.FindSceneRecord(h1);
    Expect(r1 && r1->name == "MainMenu" && r1->path == "Scenes/MainMenu.scene", "find by handle");
    Expect(r1->isLoaded == false, "record starts unloaded");
    r1->isLoaded = true; // 装载态归换场编排维护（此处直接验证可写性）
    Expect(w.FindSceneRecord(9999) == nullptr, "unknown handle not found");
    Expect(w.SceneRecordAt(1) == w.FindSceneRecord(h2), "index access matches");
    Expect(w.SceneRecordAt(2) == nullptr, "out of range is nullptr");
    w.SetActiveSceneHandle(h2);
    Expect(w.ActiveSceneHandle() == h2, "active handle set");
    Expect(w.FindSceneRecord(h1)->isLoaded, "record writable (orchestrator contract)");
}

} // namespace

// ---- 批⑥b：换场编排（SceneSwitcher）--------------------------------------

namespace {

void s_each_first(Scene& s, uint32_t handle, Entity& out) {
    out = Entity::Null();
    s.Each([&](Entity e) {
        const SceneMembership* m = s.TryGet<SceneMembership>(e);
        if (out.IsNull() && m && m->scene == handle) out = e;
    });
}

const std::string& w_find_name(World& w, uint32_t handle) {
    static std::string empty;
    const World::SceneRecord* r = w.FindSceneRecord(handle);
    return r ? r->name : empty;
}

/// 编排夹具：建档+装载+打标（宿主 F2 同款初始面），返回场景组句柄
struct SwitchFixture {
    World w;
    Scene& s;
    uint32_t handle;
    explicit SwitchFixture(const char* name)
        : w(), s(w.CreateScene("run")),
          handle(w.CreateSceneRecord(name, (std::string("Scenes/") + name + ".scene").c_str())) {
        SceneArchive::BuildInto(s, MakeSceneText(name, "mob", 2));
        StampSceneMembership(s, handle);
        w.SetActiveScene(&s);
        w.SetActiveSceneHandle(handle);
    }
};

void TestSceneSwitchFullSemantics() {
    SwitchFixture fx("Grass");
    World& w = fx.w;
    Scene& s = fx.s;
    // 组内一实体标 DDOL（跨场管理器）
    Entity ddol = Entity::Null(), victim = Entity::Null();
    s.Each([&](Entity e) {
        if (s.Get<SceneMembership>(e).scene == fx.handle && ddol.IsNull()) ddol = e;
        else if (s.Get<SceneMembership>(e).scene == fx.handle) victim = e;
    });
    Expect(MarkDontDestroyOnLoad(s, ddol) == 1, "one entity marked ddol");

    w.Switcher().Request({.name = "Volcano", .path = "Scenes/Volcano.scene",
                          .jsonText = MakeSceneText("Volcano", "lava", 3)});
    const SceneSwitchReport rep = w.Switcher().Execute(w, s);
    Expect(rep.status == SceneSwitchStatus::Success, "switch succeeds");
    Expect(rep.oldHandle == fx.handle && rep.newHandle != fx.handle, "handles reported");
    Expect(rep.destroyedOldGroup == 1, "non-ddol old member destroyed");
    Expect(rep.ddolSurvivors == 1, "ddol survivor count");
    Expect(rep.stampedNew == 3, "new group stamped (3 built)");
    Expect(!s.Alive(victim), "victim gone");
    Expect(s.Alive(ddol), "ddol survives with stable handle");
    Expect(CountSceneGroup(s, fx.handle) == 1, "old group reduced to survivor");
    Expect(CountSceneGroup(s, rep.newHandle) == 3, "new group fully stamped");
    Expect(CountSceneGroup(s, kSceneHandleUnassigned) == 0, "zero orphans");
    Expect(w.ActiveSceneHandle() == rep.newHandle, "active handle switched");
    const World::SceneRecord* rOld = w.FindSceneRecord(fx.handle);
    const World::SceneRecord* rNew = w.FindSceneRecord(rep.newHandle);
    Expect(rOld && !rOld->isLoaded, "old record unloaded");
    Expect(rNew && rNew->isLoaded && rNew->name == "Volcano", "new record loaded+named");
    Expect(!w.Switcher().HasPending(), "request consumed");
}

void TestSceneSwitchAtomicOnBadJson() {
    SwitchFixture fx("Grass");
    const uint64_t h0 = ComputeStateHash(fx.s);
    fx.w.Switcher().Request({.name = "Broken", .path = "Scenes/Broken.scene",
                             .jsonText = "{ not json"});
    const SceneSwitchReport rep = fx.w.Switcher().Execute(fx.w, fx.s);
    Expect(rep.status == SceneSwitchStatus::ParseFailed, "bad doc fails loudly");
    Expect(rep.newHandle == 0, "no record created on parse failure");
    Expect(ComputeStateHash(fx.s) == h0, "world bit-identical (atomicity)");
    Expect(fx.w.ActiveSceneHandle() == fx.handle, "active unchanged");
    Expect(fx.w.SceneRecordCount() == 1, "no stray records");
    // 好档随后可用（失败后开关器不留残态）
    fx.w.Switcher().Request({.name = "Ok", .path = "Scenes/Ok.scene",
                             .jsonText = MakeSceneText("Ok", "x", 1)});
    Expect(fx.w.Switcher().Execute(fx.w, fx.s).status == SceneSwitchStatus::Success,
           "switcher recovers after failure");
}

void TestSceneSwitchSweepsFxAndSkipsNullAudio() {
    SwitchFixture fx("Grass");
    fx.w.Fx().PopupText("stale", 0.0f, 0.0f);
    fx.w.Fx().Bar(1234, 0.5f);
    Expect(fx.w.Fx().TextCount() == 1 && fx.w.Fx().BarCount() == 1, "fx seeded");
    // 无音频后端（AudioSink()==nullptr）：强制清走 no-op 分支不崩
    fx.w.Switcher().Request({.name = "B", .path = "b.scene",
                             .jsonText = MakeSceneText("B", "b", 1)});
    fx.w.Switcher().Execute(fx.w, fx.s);
    Expect(fx.w.Fx().TextCount() == 0 && fx.w.Fx().BarCount() == 0,
           "fx fully cleared (non-entity sweep)");
}

void TestSceneSwitchHookOrdering() {
    SwitchFixture fx("Grass");
    Entity ddol = Entity::Null();
    s_each_first(fx.s, fx.handle, ddol);
    MarkDontDestroyOnLoad(fx.s, ddol);
    uint32_t sweepAlive = 9999, afterOld = 9999, afterNew = 9999, afterActive = 0;
    uint32_t sweepSeen = 0;
    fx.w.Switcher().SetHooks({
        .sweep = [&] {
            sweepAlive = fx.s.AliveCount(); // 清场提交后、装载前：只剩 DDOL
            sweepSeen = fx.w.ActiveSceneHandle();
        },
        .afterBuild = [&](Scene&) {
            afterOld = CountSceneGroup(fx.s, fx.handle);
            afterNew = CountSceneGroup(fx.s, 0); // 占位：下面换 newHandle 复核
            afterActive = fx.w.ActiveSceneHandle(); // 档案面已收口（Awake 前置条件）
        },
    });
    fx.w.Switcher().Request({.name = "V", .path = "v.scene",
                             .jsonText = MakeSceneText("V", "v", 2)});
    const SceneSwitchReport rep = fx.w.Switcher().Execute(fx.w, fx.s);
    Expect(rep.status == SceneSwitchStatus::Success, "switch ok");
    Expect(sweepAlive == 1, "sweep sees post-commit pre-build world (ddol only)");
    Expect(sweepSeen == fx.handle, "sweep runs before active flip");
    Expect(afterOld == 1, "afterBuild: old group already torn down");
    Expect(afterNew == 0 && CountSceneGroup(fx.s, rep.newHandle) == 2,
           "afterBuild: new group already stamped");
    Expect(afterActive == rep.newHandle, "afterBuild: active already flipped");
}

// 批⑦ D3：换场事件同步直推——协议⑤ 序 Unloaded → Loaded → ActiveChanged（载荷
// 句柄随 kind），ScriptHost 之外的后端零波及（默认空实现）
void TestSceneSwitchEventOrdering() {
    struct RecordingBackend final : public IScriptBackend {
        std::vector<uint8_t> kinds;
        std::vector<std::pair<uint32_t, uint32_t>> handles;
        void TickBatch(World&, Scene&, float) override {}
        void PullPendingEvents(World&) override {}
        void DispatchEvents(World&, Scene&, const EventPacket*, uint32_t) override {}
        void ApplyStructural(World&, Scene&) override {}
        void NotifyPendingDestroys(World&, Scene&) override {}
        void SceneEventNotify(World&, Scene&, SceneEventKind kind, uint32_t oldH,
                              uint32_t newH, uint8_t mode) override {
            kinds.push_back((uint8_t)kind);
            handles.emplace_back(oldH, newH);
            Expect(mode == 0, "single mode passthrough");
        }
    };
    SwitchFixture fx("Grass");
    RecordingBackend backend;
    fx.w.SetScriptBackend(&backend);
    fx.w.Switcher().Request({.name = "V", .path = "v.scene",
                             .jsonText = MakeSceneText("V", "v", 2)});
    const SceneSwitchReport rep = fx.w.Switcher().Execute(fx.w, fx.s);
    Expect(rep.status == SceneSwitchStatus::Success, "switch ok");
    Expect(backend.kinds.size() == 3, "three events pushed");
    Expect(backend.kinds.size() == 3 &&
               backend.kinds[0] == (uint8_t)SceneEventKind::Unloaded &&
               backend.kinds[1] == (uint8_t)SceneEventKind::Loaded &&
               backend.kinds[2] == (uint8_t)SceneEventKind::ActiveChanged,
           "protocol-5 order: unloaded -> loaded -> activeChanged");
    Expect(backend.handles.size() == 3 &&
               backend.handles[0].first == fx.handle &&
               backend.handles[0].second == 0 &&
               backend.handles[1] == std::make_pair(fx.handle, rep.newHandle) &&
               backend.handles[2] == std::make_pair(fx.handle, rep.newHandle),
           "event payloads carry old/new handles");
    // 无换场不推（NoPending 短路零事件）
    const size_t before = backend.kinds.size();
    fx.w.Switcher().Execute(fx.w, fx.s);
    Expect(backend.kinds.size() == before, "idle execute pushes no events");
    // 解析失败不推（原子性 = 事件面也原子）
    fx.w.Switcher().Request({.name = "Bad", .path = "bad.scene", .jsonText = "{ nope"});
    fx.w.Switcher().Execute(fx.w, fx.s);
    Expect(backend.kinds.size() == before, "parse failure pushes no events");
}

void TestSceneSwitchNoPendingIsNoop() {
    SwitchFixture fx("Grass");
    const uint64_t h0 = ComputeStateHash(fx.s);
    Expect(fx.w.Switcher().Execute(fx.w, fx.s).status == SceneSwitchStatus::NoPending,
           "idle execute reports NoPending");
    Expect(ComputeStateHash(fx.s) == h0, "idle execute touches nothing");
    // last-wins 覆盖：第二次 Request 顶掉第一次（单槽）
    fx.w.Switcher().Request({.name = "A", .path = "a.scene", .jsonText = MakeSceneText("A", "a", 1)});
    fx.w.Switcher().Request({.name = "B", .path = "b.scene", .jsonText = MakeSceneText("B", "b", 1)});
    const SceneSwitchReport rep = fx.w.Switcher().Execute(fx.w, fx.s);
    Expect(rep.status == SceneSwitchStatus::Success &&
                w_find_name(fx.w, rep.newHandle) == "B",
            "last request wins (single slot)");
}

void TestSceneSwitchViaEssentialPipeline() {
    // 系统集成面：InstallDefaultSystems 的 World 上 Request → 一次 Step 的 Essential
    // 段完成换场（SceneSwitchSystem 在 DestroyCommitSystem 之后，F1 形态②落点）
    World w;
    w.InstallDefaultSystems();
    Scene& s = w.CreateScene("run");
    w.SetActiveScene(&s);
    const uint32_t h1 = w.CreateSceneRecord("Grass", "g.scene");
    SceneArchive::BuildInto(s, MakeSceneText("Grass", "mob", 2));
    StampSceneMembership(s, h1);
    w.SetActiveSceneHandle(h1);
    w.Switcher().Request({.name = "V", .path = "v.scene",
                          .jsonText = MakeSceneText("V", "v", 2)});
    w.Step(1.0f / 60.0f);
    Expect(w.ActiveSceneHandle() != h1, "switch executed inside Essential stage");
    Expect(!w.Switcher().HasPending(), "request consumed by pipeline");
    Expect(CountSceneGroup(s, w.ActiveSceneHandle()) == 2, "new group live");
    Expect(CountSceneGroup(s, kSceneHandleUnassigned) == 0, "zero orphans");
}

void TestSpawnPrefabStampsTree() {
    World w;
    Scene& s = w.CreateScene("run");
    w.SetActiveScene(&s);
    const uint32_t h1 = w.CreateSceneRecord("Grass");
    w.SetActiveSceneHandle(h1);
    // spawn 工厂：root + child 子树（LoadEntityTree 同构——root 返回，child 挂链）
    w.SetSpawnFn([](Scene& sc, uint32_t, Vec2, uint32_t) {
        Entity root = sc.Create();
        sc.Emplace<Transform2D>(root, Transform2D{{0, 0}});
        Entity child = sc.Create();
        sc.Emplace<Transform2D>(child, Transform2D{{1, 1}});
        SceneSetParent(sc, child, root);
        return root;
    });
    Expect(!w.HasSpawnFn() == false, "spawn fn registered"); // 双反演 = 有注册
    Entity spawned = w.SpawnPrefab(7, Vec2{0, 0}, 1);
    Expect(!spawned.IsNull(), "spawned");
    Expect(CountSceneGroup(s, h1) == 2, "spawned subtree stamped to active handle");
    Expect(CountSceneGroup(s, kSceneHandleUnassigned) == 0, "no untagged spawn residue");
    // 换场后 active 拨新组——再 spawn 落新组（Instantiate 落点随 active）
    const uint32_t h2 = w.CreateSceneRecord("Volcano");
    w.SetActiveSceneHandle(h2);
    Entity later = w.SpawnPrefab(7, Vec2{0, 0}, 1);
    Expect(!later.IsNull() && s.Get<SceneMembership>(later).scene == h2,
           "later spawn stamps new active");
}

// ---- 批⑥c：回放扩展（多次换场/DDOL 轨迹 + 孪生世界确定性）------------------

void TestSceneMultiSwitchDDOLTrajectory() {
    // 四跳（Grass→Volcano→Grass 同名重装→Cave）：DDOL 幸存者句柄/flags/来源组
    // 全程稳定、重装同名场景发新句柄（每载一档，旧句柄不复活）、每跳零孤组、
    // 档案 isLoaded 逐跳翻转
    SwitchFixture fx("Grass");
    World& w = fx.w;
    Scene& s = fx.s;
    Entity ddol = Entity::Null();
    s_each_first(s, fx.handle, ddol);
    Expect(MarkDontDestroyOnLoad(s, ddol) == 1, "manager marked ddol");
    const Entity ddolSaved = ddol; // 句柄轨迹对照（Entity 按 id 比较）

    struct Hop { const char* name; const char* tag; int count; };
    const Hop hops[] = {{"Volcano", "lava", 3}, {"Grass", "mob2", 2}, {"Cave", "bat", 4}};
    uint32_t prev = fx.handle;
    for (const Hop& hop : hops) {
        w.Switcher().Request({.name = hop.name,
                              .path = std::string("Scenes/") + hop.name + ".scene",
                              .jsonText = MakeSceneText(hop.name, hop.tag, hop.count)});
        const SceneSwitchReport rep = w.Switcher().Execute(w, s);
        Expect(rep.status == SceneSwitchStatus::Success, "hop succeeds");
        Expect(rep.newHandle != prev, "fresh handle per load");
        // DDOL 挂在初始 Grass 组：清场后该组余 1；其余组无幸存者全清
        Expect(CountSceneGroup(s, prev) == (prev == fx.handle ? 1u : 0u),
               "old group cleared (ddol origin keeps survivor)");
        Expect(CountSceneGroup(s, rep.newHandle) == (uint32_t)hop.count, "new group full");
        Expect(CountSceneGroup(s, kSceneHandleUnassigned) == 0, "zero orphans every hop");
        Expect(s.Alive(ddol) && ddol == ddolSaved, "ddol handle stable across hops");
        const SceneMembership* mm = s.TryGet<SceneMembership>(ddol);
        Expect(mm && mm->scene == fx.handle &&
                    (mm->flags & kSceneFlagDontDestroyOnLoad) != 0,
               "ddol origin scene + flag stable");
        Expect(w.ActiveSceneHandle() == rep.newHandle, "active follows each hop");
        const World::SceneRecord* rOld = w.FindSceneRecord(prev);
        const World::SceneRecord* rNew = w.FindSceneRecord(rep.newHandle);
        Expect(rOld && !rOld->isLoaded && rNew && rNew->isLoaded, "records flipped per hop");
        prev = rep.newHandle;
    }
    Expect(w.SceneRecordCount() == 4, "one record per load (handles never reused)");
    Expect(w_find_name(w, prev) == std::string("Cave"), "final scene named");
}

void TestSceneSwitchDeterministicTrajectory() {
    // 孪生世界锁步（回放轨迹引擎面）：同初始态 + 同脚本化请求序列（两次换场 +
    // 初始 DDOL 标记 + 每帧 Fx 灌脏）→ 逐帧 ComputeStateHash 一致 + 场景身份
    // 轨迹一致 + DDOL 轨迹一致——"同请求序列 ⇒ 同状态轨迹"（C# op 入回放流
    // 归批⑦，此处证引擎面确定性）
    const std::string docGrass = MakeSceneText("Grass", "mob", 3);
    const std::string docVolcano = MakeSceneText("Volcano", "lava", 2);

    struct Twin {
        World w;
        Scene* s = nullptr;
        Entity ddol = Entity::Null();
        uint32_t origin = 0;
        std::vector<uint32_t> activeTraj;
    };
    auto setup = [&](Twin& t) {
        t.w.InstallDefaultSystems();
        t.s = &t.w.CreateScene("run");
        t.origin = t.w.CreateSceneRecord("Grass", "Scenes/Grass.scene");
        SceneArchive::BuildInto(*t.s, docGrass);
        StampSceneMembership(*t.s, t.origin);
        t.w.SetActiveScene(t.s);
        t.w.SetActiveSceneHandle(t.origin);
        s_each_first(*t.s, t.origin, t.ddol);
        MarkDontDestroyOnLoad(*t.s, t.ddol);
    };
    Twin a, b;
    setup(a);
    setup(b);

    const int kFrames = 20;
    auto requestHop = [&](World& w, bool toVolcano) {
        SceneSwitchRequest r;
        r.name = toVolcano ? "Volcano" : "Grass";
        r.path = toVolcano ? "Scenes/Volcano.scene" : "Scenes/Grass.scene";
        r.jsonText = toVolcano ? docVolcano : docGrass;
        w.Switcher().Request(std::move(r));
    };
    int firstHashBad = -1, firstTrajBad = -1;
    for (int f = 0; f < kFrames; ++f) {
        if (f == 4) { // Grass → Volcano
            requestHop(a.w, true);
            requestHop(b.w, true);
        }
        if (f == 12) { // Volcano → Grass（同名重装）
            requestHop(a.w, false);
            requestHop(b.w, false);
        }
        // 每帧 Fx 灌脏（非实体附着，不入哈希——换场随行清扫的确定性面）
        a.w.Fx().PopupText("x", 0.0f, 0.0f);
        b.w.Fx().PopupText("x", 0.0f, 0.0f);
        a.w.Step(1.0f / 60.0f);
        b.w.Step(1.0f / 60.0f);
        a.activeTraj.push_back(a.w.ActiveSceneHandle());
        b.activeTraj.push_back(b.w.ActiveSceneHandle());
        if (ComputeStateHash(*a.s) != ComputeStateHash(*b.s) && firstHashBad < 0)
            firstHashBad = f;
        if (a.activeTraj.back() != b.activeTraj.back() && firstTrajBad < 0)
            firstTrajBad = f;
    }
    Expect(firstHashBad < 0, "per-frame state hash streams identical");
    Expect(firstTrajBad < 0, "scene identity trajectory identical");
    Expect(a.activeTraj.size() == (size_t)kFrames && a.activeTraj[4] != a.origin &&
                a.activeTraj[12] != a.activeTraj[4],
           "switches landed on scripted frames (fresh handles each)");
    Expect(a.s->Alive(a.ddol) && b.s->Alive(b.ddol), "ddol survived both twins");
    const SceneMembership* ma = a.s->TryGet<SceneMembership>(a.ddol);
    const SceneMembership* mb = b.s->TryGet<SceneMembership>(b.ddol);
    Expect(ma && mb && ma->scene == mb->scene && ma->scene == a.origin &&
                (ma->flags & kSceneFlagDontDestroyOnLoad) != 0,
           "ddol trajectory identical (origin handle + flag)");
    Expect(CountSceneGroup(*a.s, kSceneHandleUnassigned) == 0 &&
                CountSceneGroup(*b.s, kSceneHandleUnassigned) == 0,
           "zero orphans both twins");
}

} // namespace

// ---- 批⑧：LoadSceneAsync（分帧状态机 / 契约 / 回放激活帧 / 压测）--------------

namespace {

// 异步事件记录后端（kind 序 + 载荷；批⑧ 起四事件）
struct AsyncRecordingBackend final : public IScriptBackend {
    std::vector<uint8_t> kinds;
    std::vector<std::pair<uint32_t, uint32_t>> handles;
    void TickBatch(World&, Scene&, float) override {}
    void PullPendingEvents(World&) override {}
    void DispatchEvents(World&, Scene&, const EventPacket*, uint32_t) override {}
    void ApplyStructural(World&, Scene&) override {}
    void NotifyPendingDestroys(World&, Scene&) override {}
    void SceneEventNotify(World&, Scene&, SceneEventKind kind, uint32_t oldH,
                          uint32_t newH, uint8_t) override {
        kinds.push_back((uint8_t)kind);
        handles.emplace_back(oldH, newH);
    }
    bool KindIs(size_t i, SceneEventKind k) const {
        return kinds.size() > i && kinds[i] == (uint8_t)k;
    }
};

/// 大档生成器（压测/多帧行为）：每实体 4 组件（Transform2D/Meta/Velocity/
/// SpriteRenderer——解码路径全覆盖）
std::string MakeBigSceneText(const char* name, int count) {
    World w;
    Scene& s = w.CreateScene(name);
    for (int i = 0; i < count; ++i) {
        Entity e = s.Create();
        s.Emplace<Transform2D>(e, Transform2D{{float(i % 100), float(i / 100)}});
        Meta& m = s.Emplace<Meta>(e);
        std::strcpy(m.tag, "mob");
        s.Emplace<Velocity>(e, Velocity{{1.0f, 0.5f}});
        SpriteRenderer& sr = s.Emplace<SpriteRenderer>(e);
        sr.spriteId = (uint32_t)(i % 32);
    }
    return SceneArchive::Save(s);
}

void TestSceneAsyncMachineContract() {
    // ① 小档一帧效：默认预算下 TickAsync 单次调用直抵激活（与同步同帧效）
    {
        SwitchFixture fx("Grass");
        AsyncRecordingBackend backend;
        fx.w.SetScriptBackend(&backend);
        const uint32_t op = fx.w.Switcher().RequestAsync(
            {.name = "Volcano", .path = "Scenes/Volcano.scene",
             .jsonText = MakeSceneText("Volcano", "lava", 3)});
        Expect(op == 1, "first op id is 1");
        fx.w.Switcher().TickAsync(fx.w, fx.s); // 一帧：预备段 + 激活同窗
        Expect(fx.w.ActiveSceneHandle() != fx.handle, "activated same tick (small doc)");
        AsyncSceneQuery q;
        Expect(fx.w.Switcher().Async().Query(op, q) && q.isDone && q.progress >= 1.0f,
               "terminal: isDone + progress 1.0");
        Expect(fx.w.Switcher().Async().Query(9999, q) == false, "unknown op rejected");
        Expect(backend.kinds.size() == 4, "four events (U/L/A/AsyncCompleted)");
        Expect(backend.KindIs(0, SceneEventKind::Unloaded) &&
                   backend.KindIs(1, SceneEventKind::Loaded) &&
                   backend.KindIs(2, SceneEventKind::ActiveChanged) &&
                   backend.KindIs(3, SceneEventKind::AsyncCompleted),
               "kind order incl. async tail");
        Expect(backend.handles.size() == 4 &&
                   backend.handles[3].first == op &&
                   backend.handles[3].second == fx.w.ActiveSceneHandle(),
               "asyncCompleted payload: opId + new handle");
        Expect(CountSceneGroup(fx.s, fx.w.ActiveSceneHandle()) == 3, "new group 3");
        Expect(CountSceneGroup(fx.s, kSceneHandleUnassigned) == 0, "zero orphans");
    }
    // ② 门控：关 = 停 0.9（世界逐位不动），开 = 下一 Essential 激活
    {
        SwitchFixture fx("Grass");
        AsyncRecordingBackend backend;
        fx.w.SetScriptBackend(&backend);
        Entity ddol = Entity::Null();
        s_each_first(fx.s, fx.handle, ddol);
        MarkDontDestroyOnLoad(fx.s, ddol);
        const uint64_t h0 = ComputeStateHash(fx.s);
        const uint32_t op = fx.w.Switcher().RequestAsync(
            {.name = "V", .path = "v.scene", .jsonText = MakeSceneText("V", "v", 2)});
        Expect(fx.w.Switcher().Async().SetActivation(op, false), "gate closed");
        for (int i = 0; i < 5; ++i) {
            fx.w.Switcher().TickAsync(fx.w, fx.s);
            AsyncSceneQuery q;
            Expect(fx.w.Switcher().Async().Query(op, q) && !q.isDone &&
                       q.progress <= 0.9f + 1e-6f,
                   "gate closed: not done, capped at 0.9");
            Expect(fx.w.ActiveSceneHandle() == fx.handle, "gate closed: active unchanged");
            Expect(ComputeStateHash(fx.s) == h0, "gate closed: world bit-identical");
        }
        AsyncSceneQuery q;
        fx.w.Switcher().Async().Query(op, q);
        Expect(q.progress >= 0.9f - 1e-6f, "staged work complete at gate (0.9)");
        Expect(fx.w.Switcher().Async().SetActivation(op, true), "gate reopened");
        fx.w.Switcher().TickAsync(fx.w, fx.s);
        Expect(fx.w.ActiveSceneHandle() != fx.handle, "activated after gate open");
        Expect(fx.s.Alive(ddol), "ddol survives async activation");
        Expect(fx.w.Switcher().Async().Query(op, q) && q.isDone && q.progress >= 1.0f,
               "terminal after gate open");
        Expect(backend.kinds.size() == 4, "exactly one event batch (completed once)");
        // 终态不可改门
        Expect(!fx.w.Switcher().Async().SetActivation(op, false),
               "terminal op rejects gate change");
    }
    // ③ 多帧分帧 + 预备期零可见性：小预算 + 大档 → staging 帧主世界哈希逐帧不变、
    // progress 单调上升；激活后七面成立
    {
        SwitchFixture fx("Grass");
        const std::string big = MakeBigSceneText("Big", 400);
        const uint64_t h0 = ComputeStateHash(fx.s);
        fx.w.Switcher().Async().SetBudgetMs(0.0001f); // 每 tick 恰一个 chunk
        const uint32_t op = fx.w.Switcher().RequestAsync(
            {.name = "Big", .path = "Scenes/Big.scene", .jsonText = big});
        float last = 0.0f;
        int stagedFrames = 0;
        AsyncSceneQuery q;
        while (fx.w.ActiveSceneHandle() == fx.handle && stagedFrames < 200) {
            fx.w.Switcher().TickAsync(fx.w, fx.s);
            ++stagedFrames;
            Expect(fx.w.Switcher().Async().Query(op, q) && q.progress >= last - 1e-6f,
                   "progress monotonic across frames");
            last = q.progress;
            if (!q.isDone)
                Expect(ComputeStateHash(fx.s) == h0,
                       "staging frame: main world bit-identical");
        }
        Expect(stagedFrames >= 2, "big doc spreads across frames under tiny budget");
        Expect(fx.w.ActiveSceneHandle() != fx.handle, "activated after staged frames");
        Expect(fx.w.Switcher().Async().Query(op, q) && q.isDone, "terminal after spread");
        Expect(CountSceneGroup(fx.s, fx.w.ActiveSceneHandle()) == 400, "400 integrated");
        Expect(CountSceneGroup(fx.s, kSceneHandleUnassigned) == 0, "zero orphans after");
        Expect(std::string(fx.s.Name()) == "Big", "scene name restored from staged doc");
    }
    // ④ 失败契约（批文件 §5）：Parse 段失败 = 世界逐位不动 + 终态 + kind3(newHandle=0)
    {
        SwitchFixture fx("Grass");
        AsyncRecordingBackend backend;
        fx.w.SetScriptBackend(&backend);
        const uint64_t h0 = ComputeStateHash(fx.s);
        const uint32_t op = fx.w.Switcher().RequestAsync(
            {.name = "Broken", .path = "Scenes/Broken.scene", .jsonText = "{ not json"});
        Expect(op != 0, "op issued even for bad doc (failure surfaces at Parse stage)");
        fx.w.Switcher().TickAsync(fx.w, fx.s);
        AsyncSceneQuery q;
        Expect(fx.w.Switcher().Async().Query(op, q) && q.isDone,
               "failed op reaches terminal (isDone)");
        Expect(fx.w.ActiveSceneHandle() == fx.handle, "world untouched on async failure");
        Expect(ComputeStateHash(fx.s) == h0, "world bit-identical on async failure");
        Expect(fx.w.SceneRecordCount() == 1, "no stray record on async failure");
        Expect(backend.kinds.size() == 1 && backend.KindIs(0, SceneEventKind::AsyncCompleted),
               "only AsyncCompleted(fail) pushed");
        Expect(backend.handles.size() == 1 && backend.handles[0].first == op &&
                   backend.handles[0].second == 0,
               "fail payload: opId + zero handle");
    }
    // ⑤ 单槽统一：同步请求取消在途 async（completed 不推）；async 取代同步 pending
    {
        SwitchFixture fx("Grass");
        AsyncRecordingBackend backend;
        fx.w.SetScriptBackend(&backend);
        const uint32_t op = fx.w.Switcher().RequestAsync(
            {.name = "A", .path = "a.scene", .jsonText = MakeSceneText("A", "a", 1)});
        fx.w.Switcher().Async().SetActivation(op, false); // 卡在门，保证在途
        fx.w.Switcher().TickAsync(fx.w, fx.s);
        fx.w.Switcher().Request({.name = "S", .path = "s.scene",
                                 .jsonText = MakeSceneText("S", "s", 1)});
        Expect(!fx.w.Switcher().Async().HasInFlight(), "sync request cancels in-flight async");
        fx.w.Switcher().Execute(fx.w, fx.s);
        Expect(fx.w.ActiveSceneHandle() != fx.handle, "sync switch executed");
        bool sawAsyncTail = false;
        for (uint8_t k : backend.kinds)
            if (k == (uint8_t)SceneEventKind::AsyncCompleted) sawAsyncTail = true;
        Expect(!sawAsyncTail, "cancelled op: completed never pushed");
        // 反向：async 取代未消费 pending
        fx.w.Switcher().Request({.name = "P", .path = "p.scene",
                                 .jsonText = MakeSceneText("P", "p", 1)});
        const uint32_t op2 = fx.w.Switcher().RequestAsync(
            {.name = "Q", .path = "q.scene", .jsonText = MakeSceneText("Q", "q", 1)});
        Expect(!fx.w.Switcher().HasPending(), "async request clears sync pending");
        fx.w.Switcher().TickAsync(fx.w, fx.s);
        Expect(w_find_name(fx.w, fx.w.ActiveSceneHandle()) == std::string("Q"),
               "async wins over superseded sync pending");
        (void)op2;
    }
}

void TestSceneAsyncReplayFrameContract() {
    // 回放激活帧契约（ADR-017 D3）：A = 异步装载（低预算多帧，观测激活帧 M）；
    // B = 同步路径在帧 M 执行（= 回放侧"同步重放 + 记录激活帧"形态）。两侧逐帧
    // ComputeStateHash 全等（含加载帧——staging 零可见性 + 激活帧原子集成的机械
    // 证明）+ 激活后实体句柄集合逐位一致（集成段复刻同步槽位序列）。
    const std::string docGrass = MakeSceneText("Grass", "mob", 6);
    const std::string docBig = MakeBigSceneText("Big", 120);

    struct Twin {
        World w;
        Scene* s = nullptr;
        Entity ddol = Entity::Null();
        uint32_t origin = 0;
    };
    auto setup = [&](Twin& t) {
        t.w.InstallDefaultSystems();
        t.s = &t.w.CreateScene("run");
        t.origin = t.w.CreateSceneRecord("Grass", "Scenes/Grass.scene");
        SceneArchive::BuildInto(*t.s, docGrass);
        StampSceneMembership(*t.s, t.origin);
        t.w.SetActiveScene(t.s);
        t.w.SetActiveSceneHandle(t.origin);
        s_each_first(*t.s, t.origin, t.ddol);
        MarkDontDestroyOnLoad(*t.s, t.ddol);
    };
    Twin a, b;
    setup(a);
    setup(b);
    a.w.Switcher().Async().SetBudgetMs(0.0001f); // A：多帧预备
    const uint32_t op = a.w.Switcher().RequestAsync(
        {.name = "Big", .path = "Scenes/Big.scene", .jsonText = docBig});
    Expect(op != 0, "async op issued on twin A");

    const int kMaxFrames = 200;
    int activatedAt = -1;
    std::vector<uint64_t> hashA, hashB;
    for (int f = 1; f <= kMaxFrames; ++f) {
        a.w.Step(1.0f / 60.0f); // Essential 内 SceneSwitchSystem：Execute + TickAsync
        if (a.w.ActiveSceneHandle() != a.origin && activatedAt < 0) {
            activatedAt = f;
            // 回放侧等价形态：激活帧已记录（= 本帧），同步路径恰在本帧 Essential
            // 执行——Request 先入槽，B 的本帧 Step 内消费（装载 + 同帧系统推进
            // 与 A 完全同位）
            b.w.Switcher().Request({.name = "Big", .path = "Scenes/Big.scene",
                                    .jsonText = docBig});
        }
        b.w.Step(1.0f / 60.0f);
        hashA.push_back(ComputeStateHash(*a.s));
        hashB.push_back(ComputeStateHash(*b.s));
    }
    Expect(activatedAt > 1, "async activation observed at a multi-frame boundary");
    // A 加载帧（1..activatedAt-1）世界 = 旧场照常 tick：与 B 同帧哈希全等
    int firstBad = -1;
    for (int f = 0; f < kMaxFrames; ++f)
        if (hashA[(size_t)f] != hashB[(size_t)f] && firstBad < 0) firstBad = f;
    Expect(firstBad < 0, "per-frame hash streams identical (load frames + activation)");
    // 激活后实体句柄逐位一致（集成段复刻同步槽位分配序列的机械断言）
    std::vector<uint64_t> idsA, idsB;
    a.s->Each([&](Entity e) { idsA.push_back(e.id); });
    b.s->Each([&](Entity e) { idsB.push_back(e.id); });
    std::sort(idsA.begin(), idsA.end());
    std::sort(idsB.begin(), idsB.end());
    Expect(idsA == idsB, "entity handle sets bit-identical (async integrate == sync build)");
    Expect(idsA.size() == 120 + 1, "120 built + ddol survivor");
    Expect(a.s->Alive(a.ddol) && b.s->Alive(b.ddol) && a.ddol == b.ddol,
           "ddol handles stable and identical");
    Expect(CountSceneGroup(*a.s, a.w.ActiveSceneHandle()) == 120 &&
               CountSceneGroup(*b.s, b.w.ActiveSceneHandle()) == 120,
           "both twins fully stamped");
    Expect(a.w.ActiveSceneHandle() == b.w.ActiveSceneHandle(), "same new handle (records aligned)");
    // 后续帧继续锁步（激活后世界同轨）
    const uint64_t hA0 = ComputeStateHash(*a.s);
    for (int f = 0; f < 10; ++f) {
        a.w.Step(1.0f / 60.0f);
        b.w.Step(1.0f / 60.0f);
        if (ComputeStateHash(*a.s) != ComputeStateHash(*b.s)) firstBad = kMaxFrames + f;
    }
    (void)hA0;
    Expect(firstBad < 0, "post-activation frames stay in lockstep");
}

void TestSceneAsyncPressure() {
    // 分帧压测（批⑧ 出口判据；数字入 DevLog，无硬性能门禁——机器相关）：
    // 20k 实体 ×4 组件合成档，4ms 预算——staged 帧 TickAsync 墙钟 ≤ 预算+容差
    //（chunk 粒度容差）、激活帧墙钟/总帧数打印、对照同步单帧全量装载。
    const int kN = 20000;
    const std::string big = MakeBigSceneText("Big", kN);

    SwitchFixture fx("Grass");
    fx.w.Switcher().Async().SetBudgetMs(4.0f);
    const uint32_t op = fx.w.Switcher().RequestAsync(
        {.name = "Big", .path = "Scenes/Big.scene", .jsonText = big});
    AsyncSceneQuery q;
    int stagedFrames = 0;
    float parseTickMs = 0.0f, maxStagedMs = 0.0f, activationMs = 0.0f;
    while (fx.w.ActiveSceneHandle() == fx.handle && stagedFrames < 1000) {
        const auto t0 = std::chrono::steady_clock::now();
        fx.w.Switcher().TickAsync(fx.w, fx.s);
        const float ms = std::chrono::duration<float, std::milli>(
                             std::chrono::steady_clock::now() - t0)
                             .count();
        const bool activated = fx.w.ActiveSceneHandle() != fx.handle;
        if (stagedFrames == 0)
            parseTickMs = ms; // 首帧含 Parse 原子段（预算豁免——ADR D3 口径，单列）
        else if (activated)
            activationMs = ms;
        else
            maxStagedMs = ms > maxStagedMs ? ms : maxStagedMs;
        ++stagedFrames;
    }
    Expect(fx.w.Switcher().Async().Query(op, q) && q.isDone, "pressure: op terminal");
    Expect(CountSceneGroup(fx.s, fx.w.ActiveSceneHandle()) == (uint32_t)kN,
           "pressure: all 20k integrated");
    Expect(CountSceneGroup(fx.s, kSceneHandleUnassigned) == 0, "pressure: zero orphans");
    // 预算断言 = sanity 天花板而非硬门（墙钟预算对 CI/机器噪声敏感——精确数字
    // 归 DevLog；结构保证 = 每 chunk 后 overBudget 检查。天花板取 3× 预算：稳态
    // 实测 4.0-5.7ms，超 12ms = 机制性回归而非噪声）。sanitizer 构建豁免：ASAN/
    // UBSAN 插桩 2-4x 减速会把墙钟推过任何固定天花板（b11b ASAN 门实跑 20k 场
    // 稳态翻倍超标而 abort——插桩成本非机制回归）；终端性/孤儿/计数断言不受影响
#if !defined(__SANITIZE_ADDRESS__) && !(defined(__has_feature) && \
    (__has_feature(address_sanitizer) || __has_feature(undefined_behavior_sanitizer)))
    Expect(maxStagedMs <= 12.0f, "staged frames respect budget (sanity ceiling 3x)");
#endif

    // 对照：同步单帧全量装载同一档
    SwitchFixture fx2("Grass");
    const auto t1 = std::chrono::steady_clock::now();
    fx2.w.Switcher().Request({.name = "Big", .path = "Scenes/Big.scene", .jsonText = big});
    fx2.w.Switcher().Execute(fx2.w, fx2.s);
    const float syncMs = std::chrono::duration<float, std::milli>(
                             std::chrono::steady_clock::now() - t1)
                             .count();
    Expect(CountSceneGroup(fx2.s, fx2.w.ActiveSceneHandle()) == (uint32_t)kN,
           "pressure: sync load built same count");

    LEMON_LOG("[async-pressure] entities=%d comps=4 budgetMs=4.0 stagedFrames=%d "
              "parseTickMs=%.3f maxStagedMs=%.3f activationMs=%.3f syncOneShotMs=%.3f",
              kN, stagedFrames, parseTickMs, maxStagedMs, activationMs, syncMs);
}

} // namespace

void RunSceneTests() {
    TestSceneMembershipStampAndCount();
    TestSceneGroupTeardownKeepsDDOL();
    TestSceneArchiveBuildIntoAppends();
    TestStateHashIgnoresMembership();
    TestWorldSceneRecords();
    TestDdolRootBitLineageSemantics();       // 批⑦ D1
    TestClearAllExceptDdolLineageReportsUnassigned(); // 批⑦ D2
    TestSceneSwitchFullSemantics();
    TestSceneSwitchAtomicOnBadJson();
    TestSceneSwitchSweepsFxAndSkipsNullAudio();
    TestSceneSwitchHookOrdering();
    TestSceneSwitchEventOrdering();          // 批⑦ D3
    TestSceneSwitchNoPendingIsNoop();
    TestSceneSwitchViaEssentialPipeline();
    TestSpawnPrefabStampsTree();
    TestSceneMultiSwitchDDOLTrajectory();
    TestSceneSwitchDeterministicTrajectory();
    TestSceneAsyncMachineContract();         // 批⑧
    TestSceneAsyncReplayFrameContract();     // 批⑧
    TestSceneAsyncPressure();                // 批⑧
}
