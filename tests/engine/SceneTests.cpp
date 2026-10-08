// Lemon 引擎单测 — SceneTests — 档2 场景管理（ADR-017；M7c 批⑥）
// 域（SceneMembership 打标/组清场/DDOL 根树幸存 · World 场景档案 · SceneArchive
// BuildInto 追加装载与 Load 清空对照 · StateHash 对 membership 不敏感）。
#include "TestFramework.h"

#include <cstring>
#include <string>

#include "Components/CoreComponents.h"
#include "ECS/Hierarchy.h"
#include "ECS/SceneMembership.h"
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

    Expect(MarkDontDestroyOnLoadTree(s, root) == 2, "ddol retags whole subtree (root+child)");
    Expect(CountDontDestroyOnLoad(s) == 2, "ddol count");

    // 组1 清场：只带走非 DDOL 的 lone；root/child 幸存（Unity"根树整体幸存"语义）
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
    Expect(CountDontDestroyOnLoad(s) == 2, "ddol persists across group teardowns");
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
    Expect(MarkDontDestroyOnLoadTree(s, ddol) == 1, "one A entity marked ddol");

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
    MarkDontDestroyOnLoadTree(s, [&] {
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

void RunSceneTests() {
    TestSceneMembershipStampAndCount();
    TestSceneGroupTeardownKeepsDDOL();
    TestSceneArchiveBuildIntoAppends();
    TestStateHashIgnoresMembership();
    TestWorldSceneRecords();
}
