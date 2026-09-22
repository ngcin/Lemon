// ---------------------------------------------------------------------------
// lemon-script-tests — M3 脚本桥测试。
// M3-2a：CoreCLRHost 最小闭环（bootstrap 哨兵）。
// M3-1：布局一致性护栏——C# 镜像自报表 vs ComponentRegistry 逐项对照、整块 blit、
//       typed roundtrip、PCG32 golden（位级）、EventPacket 布局。
// ---------------------------------------------------------------------------
#include <cstddef>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include <memory>

#include "Components/BehaviorComponents.h"
#include "Components/CoreComponents.h"
#include "Components/GameplayComponents.h"
#include "Core/Random.h"
#include "ECS/ComponentRegistry.h"
#include "ECS/Events.h"
#include "ECS/StateHash.h"
#include "ECS/World.h"
#include "Scripting/CoreCLRHost.h"
#include "Scripting/ScriptHost.h"
#include "Systems/Systems.h"

namespace {

int g_checks = 0;

void Expect(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        std::printf("script-tests: FAIL %s\n", what);
        std::exit(1);
    }
}

// 与 Lemon.SDK LayoutTables.Fnv64 同源（basis/prime 一致即可）
uint64_t Fnv64(const char* s) {
    uint64_t h = 1469598103934665603ull;
    for (const char* p = s; *p; ++p) {
        h ^= (uint8_t)*p;
        h *= 1099511628211ull;
    }
    return h;
}

// ---- 桥侧布局表 POD 镜像（与 Lemon.SDK/Interop/LayoutTables.cs 声明逐字节一致）----
struct CompLayoutRow {
    uint64_t nameHash;
    uint32_t sizeOf;
    uint16_t fieldCount, firstField;
};
struct FieldLayoutRow {
    uint64_t nameHash;
    uint16_t offset;
    uint8_t type, flags;
};
struct SegLayoutRow {
    uint64_t nameHash;
    uint16_t segOffset, elemSize, countOffset, maxCount, elemFieldFirst, elemFieldCount;
};
static_assert(sizeof(CompLayoutRow) == 16);
static_assert(sizeof(FieldLayoutRow) == 16); // ulong 对齐 8：12B 字段 + 4B 尾垫（C# 同规则）
static_assert(sizeof(SegLayoutRow) == 24); // 同上（C# Sequential 自然对齐同规则）

// ---- 导出函数指针（启动期一次取全）----
int (*lemonSdkLayout)(void*, int, void*, int, void*, int) = nullptr;
void (*lemonBlitCopy)(void*, const void*, int, int) = nullptr;
void (*lemonRoundtripTyped)(void*, void*) = nullptr;
void (*lemonRngFill)(uint64_t, uint64_t, void*, int) = nullptr;
void (*lemonRngFloat01)(uint64_t, uint64_t, void*, int) = nullptr;
int (*lemonEventPacketLayout)(uint16_t*, uint16_t*, uint16_t*, uint16_t*, uint16_t*,
                              uint16_t*) = nullptr;

void TestLayoutAgainstRegistry() {
    lemon::ecs::RegisterAllComponents();
    auto& reg = lemon::ecs::ComponentRegistry::Instance();
    Expect(reg.Count() == 27, "registry count 27");

    std::vector<CompLayoutRow> comps(64);
    std::vector<FieldLayoutRow> fields(256);
    std::vector<SegLayoutRow> segs(8);
    int n = lemonSdkLayout(comps.data(), (int)comps.size(), fields.data(), (int)fields.size(),
                           segs.data(), (int)segs.size());
    Expect(n == 27, "sdk layout comp count");

    uint32_t totalFields = 0;
    for (int i = 0; i < n; i++) {
        const auto& cm = reg.At((uint16_t)i);
        const auto& cr = comps[i];
        char what[128];
        std::snprintf(what, sizeof what, "comp[%d] name %s", i, cm.name);
        Expect(cr.nameHash == Fnv64(cm.name), what);
        std::snprintf(what, sizeof what, "comp[%d] %s sizeOf %u vs %u", i, cm.name, cr.sizeOf,
                      cm.sizeOf);
        Expect(cr.sizeOf == cm.sizeOf, what);
        std::snprintf(what, sizeof what, "comp[%d] %s fieldCount", i, cm.name);
        Expect(cr.fieldCount == cm.fieldCount, what);

        for (uint16_t f = 0; f < cm.fieldCount; f++) {
            const auto& fm = cm.fields[f];
            const auto& fr = fields[cr.firstField + f];
            std::snprintf(what, sizeof what, "%s.%s name", cm.name, fm.name);
            Expect(fr.nameHash == Fnv64(fm.name), what);
            std::snprintf(what, sizeof what, "%s.%s offset %u vs %u", cm.name, fm.name, fr.offset,
                          fm.offset);
            Expect(fr.offset == fm.offset, what);
            std::snprintf(what, sizeof what, "%s.%s type %u vs %u", cm.name, fm.name, fr.type,
                          (unsigned)fm.type);
            Expect(fr.type == (uint8_t)fm.type, what);
            std::snprintf(what, sizeof what, "%s.%s flags(runtime)", cm.name, fm.name);
            Expect(fr.flags == (fm.flags & lemon::ecs::kFieldRuntime), what);
            ++totalFields;
        }

        // 数组段对照（ArraySegMeta）
        if (cm.arraySeg) {
            const auto& s = *cm.arraySeg;
            const SegLayoutRow* found = nullptr;
            for (const auto& sr : segs)
                if (sr.nameHash == cr.nameHash) { found = &sr; break; }
            std::snprintf(what, sizeof what, "%s seg present", cm.name);
            Expect(found != nullptr, what);
            if (found) {
                std::snprintf(what, sizeof what, "%s seg offset", cm.name);
                Expect(found->segOffset == s.offset, what);
                std::snprintf(what, sizeof what, "%s seg elemSize", cm.name);
                Expect(found->elemSize == s.elemSize, what);
                std::snprintf(what, sizeof what, "%s seg countOffset", cm.name);
                Expect(found->countOffset == s.countOffset, what);
                std::snprintf(what, sizeof what, "%s seg maxCount", cm.name);
                Expect(found->maxCount == s.maxCount, what);
                if (s.elemFields) {
                    for (uint16_t e = 0; e < s.elemFieldCount; e++) {
                        const auto& em = s.elemFields[e];
                        const auto& er = fields[found->elemFieldFirst + e];
                        std::snprintf(what, sizeof what, "%s elem[%u] %s name", cm.name, e,
                                      em.name);
                        Expect(er.nameHash == Fnv64(em.name), what);
                        std::snprintf(what, sizeof what, "%s elem[%u] %s offset", cm.name, e,
                                      em.name);
                        Expect(er.offset == em.offset, what);
                        std::snprintf(what, sizeof what, "%s elem[%u] %s type", cm.name, e,
                                      em.name);
                        Expect(er.type == (uint8_t)em.type, what);
                    }
                }
            }
        }
    }
    std::printf("script-tests: layout 27 comps / %u fields checked\n", totalFields);
}

void TestBlitCopy() {
    auto& reg = lemon::ecs::ComponentRegistry::Instance();
    lemon::Rng rng(0xDEADBEEFu, 0x77u);
    for (uint16_t id = 0; id < reg.Count(); id++) {
        const auto& m = reg.At(id);
        const int N = 8;
        std::vector<uint8_t> src((size_t)m.sizeOf * N), dst((size_t)m.sizeOf * N, 0xAB);
        for (auto& b : src) b = (uint8_t)rng.Next();
        lemonBlitCopy(dst.data(), src.data(), id, N);
        char what[96];
        std::snprintf(what, sizeof what, "blit %s (size %u x8)", m.name, m.sizeOf);
        Expect(std::memcmp(dst.data(), src.data(), src.size()) == 0, what);
    }
}

void TestRoundtripTyped() {
    lemon::ecs::Transform2D t{};
    lemon::ecs::StatusEffects s{};
    std::memset(&t, 0, sizeof t);
    std::memset(&s, 0, sizeof s);
    lemonRoundtripTyped(&t, &s);
    Expect(t.pos.x == 11.0f && t.pos.y == 22.0f && t.rot == 0.5f && t.scale.x == 3.0f &&
               t.scale.y == 4.0f,
           "typed roundtrip Transform2D");
    Expect(s.count == 2, "typed roundtrip StatusEffects.count");
    Expect(s.active[1].id == 7 && s.active[1].stacks == 3 && s.active[1].remain == 1.25f &&
               s.active[1].source == 9,
           "typed roundtrip StatusEffects slot 1");
}

void TestRngGolden() {
    const uint64_t seeds[] = {1, 0x4C454D4F4Eull, 0x123456789ABCDEF0ull};
    const uint64_t streams[] = {0, 1, 0x4C320000ull + 14, 0x51};
    uint32_t u32[64];
    float f01[64];
    for (uint64_t seed : seeds) {
        for (uint64_t stream : streams) {
            lemon::Rng cpp(seed, stream);
            lemonRngFill(seed, stream, u32, 64);
            bool same = true;
            for (int i = 0; i < 64; i++) same = same && u32[i] == cpp.Next();
            Expect(same, "pcg32 Next sequence bit-exact");

            lemon::Rng cpp2(seed, stream);
            lemonRngFloat01(seed, stream, f01, 64);
            bool sameF = true;
            for (int i = 0; i < 64; i++) sameF = sameF && f01[i] == cpp2.Float01();
            Expect(sameF, "pcg32 Float01 sequence bit-exact");
        }
    }
}

void TestEventPacketLayout() {
    uint16_t oType, oUser, oSrc, oDst, oPayload, oUserArg;
    int size = lemonEventPacketLayout(&oType, &oUser, &oSrc, &oDst, &oPayload, &oUserArg);
    using lemon::ecs::EventPacket;
    Expect(size == sizeof(EventPacket) && size == 48, "eventpacket sizeof 48");
    Expect(oType == offsetof(EventPacket, type), "eventpacket off type");
    Expect(oUser == offsetof(EventPacket, user), "eventpacket off user");
    Expect(oSrc == offsetof(EventPacket, src), "eventpacket off src");
    Expect(oDst == offsetof(EventPacket, dst), "eventpacket off dst");
    Expect(oPayload == offsetof(EventPacket, payload), "eventpacket off payload");
    Expect(oUserArg == offsetof(EventPacket, userArg), "eventpacket off userArg");
}

int (*lemonDmLoad)(const char*) = nullptr;
int (*lemonDmUnload)() = nullptr;
double (*lemonDmTick)(float) = nullptr;
lemon::scripting::ScriptHost g_sh;

static void* GetExport(const char* method) {
    return g_sh.RawHost().GetExport("Lemon.Entry.Exports, Lemon.Entry", method);
}

void TestDomainManager() {
    // 诊断 0：UCO 线程就地 load→unload（spike UnloadSelfTest 同形态；本测试进程内首个对照）
    auto minCycleUco =
        (int (*)(const char*))GetExport("lemon_diag_min_cycle_uco");
    if (minCycleUco)
        std::printf("script-tests: [diag] min-cycle on UCO thread (spike selftest form): %s\n",
                    minCycleUco(LEMON_SCRIPT_DIR "/TestScript.dll") ? "OK" : "TIMEOUT");

    // 诊断 1：单命令内 load→unload（域线程）
    auto minCycleFn = (int (*)(const char*))GetExport("lemon_diag_min_cycle");
    auto loadMinimalFn = (int (*)(const char*))GetExport("lemon_diag_load_minimal");
    if (minCycleFn)
        std::printf("script-tests: [diag] min-cycle (load+unload in one command): %s\n",
                    minCycleFn(LEMON_SCRIPT_DIR "/TestScript.dll") ? "OK" : "TIMEOUT");

    // 诊断 2：裸 LoadFromAssemblyPath（无委托/反射缓存）→ 跨命令 unload
    if (loadMinimalFn) {
        std::printf("script-tests: [diag] minimal load: %s\n",
                    loadMinimalFn(LEMON_SCRIPT_DIR "/TestScript.dll") ? "OK" : "FAIL");
        std::printf("script-tests: [diag] minimal load -> cross-command unload: %s\n",
                    lemonDmUnload() ? "OK" : "TIMEOUT");
    }

    // 对照组 A：load → 不执行 → unload
    // M3-2b 实测（.NET 10.0.12 / macOS x64）：常驻域线程触碰 ALC 类型系统后必然 pin
    // ——卸载能力本身存在（见 UCO 探针 OK），但域线程执行模型下收不回（ADR-010 修订）。
    // 断言固化现状：unload=0（pin）；runtime 修复后此断言与探针一起翻转。
    Expect(lemonDmLoad(LEMON_SCRIPT_DIR "/TestScript.dll") == 1, "dm load TestScript");
    int clean = lemonDmUnload();
    std::printf("script-tests: [diag] clean load->unload (no tick): %s\n", clean ? "OK" : "TIMEOUT");
    Expect(clean == 0, "dm unload pinned by resident domain thread (known runtime behavior)");

    // 正式组：load → tick → unload（M4 热重载走整域重建路线，见 ADR-010 修订）
    Expect(lemonDmLoad(LEMON_SCRIPT_DIR "/TestScript.dll") == 1, "dm load TestScript (2nd)");
    double a = lemonDmTick(0.5f);
    double b = lemonDmTick(0.25f);
    Expect(a == 0.5 && b == 0.75, "dm tick state kept across calls");

    int unloaded = lemonDmUnload();
    std::printf("script-tests: [diag] load->tick->unload: %s\n", unloaded ? "OK" : "TIMEOUT");
    Expect(unloaded == 0, "dm unload after tick pinned (known runtime behavior)");

    // 卸载后调用 = NaN 哨兵（隔离；不崩、不复活——域状态已清）
    Expect(std::isnan(lemonDmTick(1.0f)), "dm tick after unload = NaN");

    // 重载：状态归零（新域），恢复工作
    Expect(lemonDmLoad(LEMON_SCRIPT_DIR "/TestScript.dll") == 1, "dm reload");
    double c = lemonDmTick(0.1f);
    std::printf("script-tests: [diag] reload first tick = %f\n", c);
    Expect(c == (double)0.1f, "dm reload state reset + working");

    // ---- M4.5 换装探针（ADR-010 A 线；lemon_dm_reload = 热重载真实路径）----
    // tick 换状态 → 整域换装（StateBag 捕获/丢引用/尽力卸载/新域装载）→ 新域可用 +
    // 计数可见。本 runtime 预期 leak ≥ 1（域线程 pin；探针复测 2026-09-20 同 M3-2b）。
    {
        // 0.25f 二进制精确（0.1f+0.3f 会有双精度舍入差——比较口径必须两侧同算式）
        double d1 = lemonDmTick(0.25f);
        Expect(d1 == 0.1f + 0.25, "pre-reload tick 0.35 (exact binary)");
        auto reloadFn = (int (*)(const char*, int*, int*))GetExport("lemon_dm_reload");
        auto leaksFn = (int (*)())GetExport("lemon_hr_leaks");
        auto reloadsFn = (int (*)())GetExport("lemon_hr_reloads");
        Expect(reloadFn != nullptr, "lemon_dm_reload exported");
        int leaks = -1, collected = -1;
        Expect(reloadFn(LEMON_SCRIPT_DIR "/TestScript.dll", &leaks, &collected) == 1,
               "dm reload (hot swap) ok");
        double d2 = lemonDmTick(0.1f);
        std::printf("script-tests: [diag] hot-reload: leak=%d collected=%d reloads=%d "
                    "fresh-tick=%f\n",
                    leaks, collected, reloadsFn ? reloadsFn() : -1, d2);
        Expect(d2 == (double)0.1f, "hot-reload new domain fresh state");
        Expect(leaks >= 0 && leaksFn && leaksFn() == leaks, "leak count visible + consistent");
        Expect(reloadsFn && reloadsFn() >= 1, "reload count visible");
        // 换装后新域 GameMain.Configure 已跑：behaviours 列表可拉（4 个类型）
        auto listFn = (int (*)(char*, int))GetExport("lemon_behaviours_list");
        char buf[4096];
        int n = listFn ? listFn(buf, (int)sizeof buf) : -1;
        Expect(n == 4, "behaviours list after hot reload (Counting/Spawner/InputMover/TimeProbe)");
        // M4.6 回归（用户实测闪退根因）：相对路径进 dm_reload 曾抛 ArgumentException
        // 逃逸 UnmanagedCallersOnly → coreclr abort。拦截层必须转 0 返回且进程存活
        // （本断言能跑到 = 进程没死）。换装失败后旧域已弃——再换一次真路径恢复。
        Expect(reloadFn("./not/absolute.dll", &leaks, &collected) == 0,
               "dm reload relative path contained (no abort)");
        Expect(reloadFn(LEMON_SCRIPT_DIR "/TestScript.dll", &leaks, &collected) == 1,
               "dm reload recovers after contained failure");
        Expect(lemonDmTick(0.1f) == (double)0.1f, "recovered domain ticks");
    }
}


void TestBatchSystem() {
    using namespace lemon::ecs;
    // 档②：ScriptHost 装配用户程序集 → World 注入 → #14 域线程执行 → 精确断言
    Expect(g_sh.LoadUserAssembly(LEMON_SCRIPT_DIR "/TestScript.dll"), "load user assembly");
    {
        auto diagS = (int (*)(unsigned char*, int))GetExport("lemon_diag_scripting");
        char info[256] = {};
        diagS((unsigned char*)info, (int)sizeof info - 1);
        std::printf("script-tests: [diag] scripting: %s", info);
    }
    std::printf("script-tests: [diag] batch systems = %u\n", g_sh.BatchSystemCount());
    Expect(g_sh.BatchSystemCount() == 2, "two batch systems registered");

    auto runSim = [&](uint64_t seed) {
        WorldDesc d;
        d.threadCount = 1;
        d.seed = seed;
        World w(d);
        Scene& s = w.CreateScene("BatchT");
        w.SetActiveScene(&s);
        w.SetScriptBackend(&g_sh);
        w.Pipeline().AddSystem(std::make_unique<CSharpBatchSystem>());
        w.Pipeline().ResolveOrder();

        for (int i = 0; i < 100; i++) {
            Entity e = s.Create();
            s.Emplace<Velocity>(e, Velocity{{1.0f, 0.5f}});
            if (i < 30) s.Emplace<Projectile>(e); // 30 个双组件实体（AND 查询）
        }
        for (int step = 0; step < 4; step++) w.Step(0.25f);

        int velChecked = 0, projChecked = 0;
        for (auto [e, v] : s.View<Velocity>().each()) {
            (void)e;
            Expect(v.v.x == 61.0f, "velocity x == 1 + 4*15");
            Expect(v.v.y == 0.5f, "velocity y untouched");
            ++velChecked;
        }
        for (auto [e, pr] : s.View<Projectile>().each()) {
            (void)e;
            Expect(pr.age == 1.0f, "projectile age == 4*0.25");
            ++projChecked;
        }
        Expect(velChecked == 100, "all velocities iterated");
        Expect(projChecked == 30, "AND query matched 30");
        return ComputeStateHash(s);
    };

    uint64_t h1 = runSim(1234);
    uint64_t h2 = runSim(1234);
    Expect(h1 == h2, "script batch deterministic (same seed, same hash)");
    uint64_t h3 = runSim(9999);
    (void)h3; // 不同种子路径仅执行（哈希值本身与种子无强绑定，不做不等断言）
}


void TestEventBridge() {
    using namespace lemon::ecs;
    // M3-4：C++ 入队 Hit → C# 订阅者（同帧）→ C# push Custom → 下一帧 sink 收到
    auto receivedFn = (int (*)(int))GetExport("lemon_events_received");
    Expect(receivedFn != nullptr, "events exports resolved");

    WorldDesc d;
    d.threadCount = 1;
    World w(d);
    Scene& s = w.CreateScene("EvtT");
    w.SetActiveScene(&s);
    w.SetScriptBackend(&g_sh);
    w.Pipeline().AddSystem(std::make_unique<CSharpBatchSystem>());
    w.Pipeline().AddSystem(std::make_unique<ScriptEventDispatchSystem>());
    w.Pipeline().ResolveOrder();

    int customSeen = 0, badUser = 0;
    w.SetEventSink([&](World&, const EventPacket& p) {
        if (p.type == GameEvent::Custom) {
            ++customSeen;
            if (p.user != 42) ++badUser;
        }
    });

    for (int i = 0; i < 3; i++) {
        EventPacket p{};
        p.type = GameEvent::Hit;
        p.src = Entity{(uint64_t)(i + 1)};
        w.Events().Push(p);
    }
    w.Step(0.25f); // C# 收 3 Hit、push 3 Custom（pending）；sink 尚未收到 Custom
    Expect(receivedFn((int)GameEvent::Hit) == 3, "C# received 3 Hit same frame");
    w.Step(0.25f); // #15 拉取 3 Custom 入队 → sink + C# 同帧收到
    Expect(customSeen == 3 && badUser == 0, "sink received 3 Custom (user=42)");
    Expect(receivedFn((int)GameEvent::Custom) == 3, "C# received 3 Custom");
    Expect(w.Events().Size() == 0, "queue drained");
}


void TestBehaviourAndStructuralOps() {
    using namespace lemon::ecs;
    // M3-5/6：命令缓冲（Create+AddComponent+AttachScript 占位链）→ 生命周期 → 自毁
    auto opsSubmit = (void (*)(unsigned char, unsigned char, uint64_t))GetExport("lemon_ops_submit");
    auto behAttached = (int (*)())GetExport("lemon_behaviours_attached");
    Expect(opsSubmit && behAttached, "ops/behaviour exports resolved");

    // M5 清障①：Time 属于"一局"——本测试 = 新的一局（前面 TestBatch/EventBridge 已把
    // FrameCount 推走；不归零则 TimeProbe 的 FrameCount==1 永不命中，编辑器同语义）
    auto timeResetFn = (void (*)())GetExport("lemon_time_reset");
    Expect(timeResetFn != nullptr, "lemon_time_reset exported");
    timeResetFn();

    WorldDesc d;
    d.threadCount = 1;
    World w(d);
    Scene& s = w.CreateScene("BehT");
    w.SetActiveScene(&s);
    w.SetScriptBackend(&g_sh);
    w.Pipeline().AddSystem(std::make_unique<DestroyCommitSystem>());
    w.Pipeline().AddSystem(std::make_unique<CSharpBatchSystem>());
    w.Pipeline().AddSystem(std::make_unique<ScriptEventDispatchSystem>());
    w.Pipeline().ResolveOrder();

    // sink 记录 behaviour 生命周期回报（Custom 100=Awake 101=Start 102=OnDestroy 201=Update2 读组件）
    // M5 清障①：TimeProbe 回报 250/500/3（DeltaTime/Time/FrameCount × 精确值，见类注释）
    int awake = 0, start = 0, destroy = 0, readBack = 0;
    int timeDt = 0, timeSum = 0, timeFrames = 0;
    w.SetEventSink([&](World&, const EventPacket& p) {
        if (p.type != GameEvent::Custom) return;
        if (p.user == 100) ++awake;
        else if (p.user == 101) ++start;
        else if (p.user == 102) ++destroy;
        else if (p.user == 201) readBack = 1; // 精确匹配（250 语义 = dt×1000，勿用区间判定）
        else if (p.user == 250) ++timeDt;
        else if (p.user == 500) ++timeSum;
        else if (p.user == 3) ++timeFrames;
    });

    // 命令流：Create(占位) → AddComponent<Transform2D>(占位) → AttachScript(占位, typeId 0)
    //（模拟脚本 API：SceneOps.Create/AddComponent/AttachScript —— 走同一条 pending 队列）
    opsSubmit(0, 0, 0x8000000000000001ull); // Create（entity 字段=占位符）
    opsSubmit(2, (unsigned char)0 /*Transform2D id*/, 0x8000000000000001ull);
    opsSubmit(4, 0 /*CountingBehaviour typeId*/, 0x8000000000000001ull);
    // M5 清障①：同帧挂 TimeProbe（typeId 3，表尾注册序；无组件——Update 只读 Time）
    opsSubmit(0, 0, 0x8000000000000002ull);
    opsSubmit(4, 3 /*TimeProbeBehaviour typeId*/, 0x8000000000000002ull);

    w.Step(0.25f); // Essential 应用（Awake）→ #14（Start + Update1：Time 帧1 报 250）→ #15
    Expect(behAttached() == 2, "behaviours attached (Counting + TimeProbe)");
    // 帧首已建实体 + 组件：直接查场景
    int withTransform = 0;
    for (auto [e, t] : s.View<Transform2D>().each()) { (void)e; (void)t; ++withTransform; }
    Expect(withTransform == 1, "structural op created entity + component");

    w.Step(0.25f); // Counting Update2：读组件（报 201）+ Destroy 命令；TimeProbe 帧2 报 500
    w.Step(0.25f); // Counting 应用 Destroy（OnDestroy）；TimeProbe 帧3 报 3 + Destroy 命令
    w.Step(0.25f); // TimeProbe 应用 Destroy；#15 派发帧3 事件（sink 收齐 3）
    Expect(awake == 1 && start == 1 && destroy == 1, "lifecycle Awake/Start/OnDestroy once each");
    Expect(readBack == 1, "behaviour read Transform2D via native api (scale.x default 1)");
    Expect(timeDt == 1 && timeSum == 1 && timeFrames == 1,
           "Time: DeltaTime=0.25 / Time=0.5@f2 / FrameCount=3 reported exactly");
    Expect(behAttached() == 0, "behaviours detached");
    uint32_t alive = s.AliveCount();
    Expect(alive == 0, "entities destroyed via structural op");

    // M5 清障①（续）：lemon_time_reset（编辑器重进 Play 路径）→ 新一局 Time 从零
    timeResetFn();
    {
        World w2(d);
        Scene* s2 = &w2.CreateScene("BehT2");
        w2.SetActiveScene(s2);
        w2.SetScriptBackend(&g_sh);
        w2.Pipeline().AddSystem(std::make_unique<DestroyCommitSystem>());
        w2.Pipeline().AddSystem(std::make_unique<CSharpBatchSystem>());
        w2.Pipeline().AddSystem(std::make_unique<ScriptEventDispatchSystem>());
        w2.Pipeline().ResolveOrder();
        int dtSeenAgain = 0;
        w2.SetEventSink([&](World&, const EventPacket& p) {
            if (p.type == GameEvent::Custom && p.user == 250) ++dtSeenAgain;
        });
        opsSubmit(0, 0, 0x8000000000000003ull); // Create + Attach TimeProbe（新一局）
        opsSubmit(4, 3, 0x8000000000000003ull);
        for (int i = 0; i < 3; i++) w2.Step(0.25f); // 帧1 重新报 250（FrameCount 从 1 重计）
        Expect(dtSeenAgain == 1, "time reset: new session restarts at FrameCount 1");
    }
}

} // namespace

int main() {
    Expect(g_sh.Initialize(nullptr, LEMON_SCRIPT_DIR "/Lemon.Entry.runtimeconfig.json",
                            LEMON_SCRIPT_DIR "/Lemon.Entry.dll"),
           "script host init (CoreCLR + exports)");

    auto bootstrap = (int (*)())GetExport("Bootstrap");
    Expect(bootstrap != nullptr && bootstrap() == 0x1E0F, "bootstrap magic");

    lemonSdkLayout = (int (*)(void*, int, void*, int, void*, int))GetExport("lemon_sdk_layout");
    lemonBlitCopy = (void (*)(void*, const void*, int, int))GetExport("lemon_blit_copy");
    lemonRoundtripTyped = (void (*)(void*, void*))GetExport("lemon_roundtrip_typed");
    lemonRngFill = (void (*)(uint64_t, uint64_t, void*, int))GetExport("lemon_rng_fill");
    lemonRngFloat01 =
        (void (*)(uint64_t, uint64_t, void*, int))GetExport("lemon_rng_float01");
    lemonEventPacketLayout = (int (*)(uint16_t*, uint16_t*, uint16_t*, uint16_t*, uint16_t*,
                                      uint16_t*))GetExport("lemon_eventpacket_layout");
    Expect(lemonSdkLayout && lemonBlitCopy && lemonRoundtripTyped && lemonRngFill &&
               lemonRngFloat01 && lemonEventPacketLayout,
           "all M3-1 exports resolved");

    lemonDmLoad = (int (*)(const char*))GetExport("lemon_dm_load");
    lemonDmUnload = (int (*)())GetExport("lemon_dm_unload");
    lemonDmTick = (double (*)(float))GetExport("lemon_dm_tick");
    Expect(lemonDmLoad && lemonDmUnload && lemonDmTick, "dm exports resolved");

    TestLayoutAgainstRegistry();
    TestBlitCopy();
    TestRoundtripTyped();
    TestRngGolden();
    TestEventPacketLayout();
    {
        auto sdkCopies = (int (*)(unsigned char*, int))GetExport("lemon_diag_sdk_copies");
        char info[512] = {};
        int n = sdkCopies((unsigned char*)info, (int)sizeof info - 1);
        std::printf("script-tests: [diag] Lemon.SDK copies=%d\n%s", n, info);
    }
    TestDomainManager();
    TestBatchSystem();
    TestEventBridge();
    TestBehaviourAndStructuralOps();

    // M4.6 探针（编辑器切项目场景）：同进程二次 ScriptHost 生命周期。CoreCLR 运行时
    // 进程单例——第二次 Initialize 的真实行为必须钉板（成功/失败都合法，崩 = 缺陷）。
    // 编辑器侧对策 = 复用宿主走 A 线换装（InitScriptHostFrom 不再二次建宿主）。
    {
        lemon::scripting::ScriptHost second;
        const bool init2 =
            second.Initialize(nullptr, LEMON_SCRIPT_DIR "/Lemon.Entry.runtimeconfig.json",
                              LEMON_SCRIPT_DIR "/Lemon.Entry.dll");
        std::printf("script-tests: [diag] second-host init=%d（进程单例事实钉板）\n",
                    init2 ? 1 : 0);
        if (init2) {
            const bool load2 = second.LoadUserAssembly(LEMON_SCRIPT_DIR "/TestScript.dll");
            std::printf("script-tests: [diag] second-host load=%d（旧域在时幂等语义）\n",
                        load2 ? 1 : 0);
        }
        auto gcFn = (unsigned long long (*)())GetExport("lemon_gc_allocated");
        (void)gcFn;
    } // 析构路径也不许崩（探针跑完 = 全程存活）

    std::printf("script-tests: %d checks OK (bootstrap + layout + domain + batch)\n", g_checks);
    return 0;
}
