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
#include <map>
#include <string>
#include <vector>

// strtok_r 是 POSIX 接口；MSVC 的等价物是 strtok_s（签名一致：str/delim/&ctx）
#if defined(_MSC_VER)
#define strtok_r strtok_s
#endif

#include <memory>

#include "Components/BehaviorComponents.h"
#include "Components/CoreComponents.h"
#include "Components/GameplayComponents.h"
#include "Components/RenderComponents.h"
#include "Audio/AudioEngine.h" // M6c 批②：TestAudioSdk 静音引擎
#include "Core/Random.h"
#include "ECS/ComponentRegistry.h"
#include "ECS/ControllerTable.h" // T3d：ControllerDef/AnimParamKind（AnimGraph 探针）
#include "ECS/Events.h"
#include "ECS/SceneMembership.h" // 批⑦：TestSceneSdk 打标/DDOL 系/孤组断言
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
uint32_t (*lemonRngRange)(uint32_t, uint32_t) = nullptr;
int (*lemonEventPacketLayout)(uint16_t*, uint16_t*, uint16_t*, uint16_t*, uint16_t*,
                              uint16_t*) = nullptr;

void TestLayoutAgainstRegistry() {
    lemon::ecs::RegisterAllComponents();
    auto& reg = lemon::ecs::ComponentRegistry::Instance();
    Expect(reg.Count() == 32, "registry count 32（M5 批② WaveDirector + T3d AnimGraph/AnimParams + M6b 批③d 前置 UIDocument + M6c 批② AudioSource）");

    std::vector<CompLayoutRow> comps(64);
    std::vector<FieldLayoutRow> fields(256);
    std::vector<SegLayoutRow> segs(8);
    int n = lemonSdkLayout(comps.data(), (int)comps.size(), fields.data(), (int)fields.size(),
                           segs.data(), (int)segs.size());
    Expect(n == 32, "sdk layout comp count");

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
    std::printf("script-tests: layout %u comps / %u fields checked\n",
                lemon::ecs::ComponentRegistry::Instance().Count(), totalFields);
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
    // F-10（2026-09-24）：单点/逆序区间双端同语义——C# 老实现此处永久死循环
    Expect(lemonRngRange(5, 5) == 5 && lemon::Rng(1, 1).Range(5u, 5u) == 5u,
           "pcg32 Range(x,x) single point no-hang, both ends");
    Expect(lemonRngRange(7, 3) == 7 && lemon::Rng(1, 1).Range(7u, 3u) == 7u,
           "pcg32 Range inverted contract returns lo, both ends");
    // 二次幂 span（整除 2^32、无拒绝区间）：zone 计算截 0，老实现永久死循环
    // （Range(0,1) 抛硬币即中招——C++/C# 双端同修后此处可达）
    Expect(lemonRngRange(0, 1) == lemon::Rng(1, 1).Range(0u, 1u),
           "pcg32 Range pow2 span (0,1) bit-exact, both ends (no hang)");
    Expect(lemonRngRange(0, 3) == lemon::Rng(1, 1).Range(0u, 3u),
           "pcg32 Range pow2 span (0,3) bit-exact, both ends (no hang)");
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

// 复审 4b（2026-09-29）：lemon_api_register2 尺寸握手——SDK 侧截短注册（仅前 3 槽
// 有效）后尾部槽必须为 null（判空降级而非越界读出的野指针）；自检内存/还原全在
// SDK 侧完成，本测试线程串行独占域，Api 换装窗口无并发 tick。
void TestApiHandshake() {
    auto selftest = (int (*)())GetExport("lemon_api_handshake_selftest");
    Expect(selftest, "api handshake selftest export resolved");
    Expect(selftest() == 1, "register2: tail slots null after truncated register");
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
        // 换装后新域 GameMain.Configure 已跑：behaviours 列表可拉（17 个类型）
        auto listFn = (int (*)(char*, int))GetExport("lemon_behaviours_list");
        char buf[4096];
        int n = listFn ? listFn(buf, (int)sizeof buf) : -1;
        Expect(n == 22, "behaviours list after hot reload (+M11/M15/F-08.2/T3/b1/b2/T3c/T3d/A档tween/T5存档档/b3c UI/M6c b2 audio/M7c b1 fx/M7c b7 scene/M7c b8 async probes)");
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
    Expect(g_sh.BatchSystemCount() == 3,
           "three batch systems registered (+D5 BurstSpawn, M7a b3)");

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

    // ---- M7a 批③ D5 护栏（评审 §D5）：批量帧窗口内爆量 Spawn → 池重分配 →
    // stale fail-stop。三断言：置位计数 >0 / 生成照常落地（fail-stop 跳的是剩余
    // 块不是生成本身）/ 后续帧零误报 + 世界稳定。
    {
        using namespace lemon::ecs;
        lemon::scripting::ResetBatchStaleMarkCount();
        WorldDesc d;
        d.threadCount = 1;
        d.seed = 4242;
        World w(d);
        Scene& s = w.CreateScene("D5Burst");
        w.SetActiveScene(&s);
        w.SetScriptBackend(&g_sh);
        w.Pipeline().AddSystem(std::make_unique<CSharpBatchSystem>());
        w.Pipeline().ResolveOrder();
        // 70 个标记实体（Transform2D+StatusEffects）= 2 个满步长块——BurstSpawn
        // 首块首实体爆 1000 只，Transform2D 池必越容量重分配（首块之后的块 =
        // 悬垂跳过对象）
        for (int i = 0; i < 70; i++) {
            Entity e = s.Create();
            s.Emplace<Transform2D>(e, Transform2D{{(float)i, 0}});
            s.Emplace<StatusEffects>(e);
        }
        w.Step(0.25f);
        Expect(lemon::scripting::BatchStaleMarkCount() >= 1,
               "D5: in-window burst spawn marks batch stale");
        Expect(s.AliveCount() == 70 + 3000, "D5: spawns land, remaining blocks skipped not spawned");
        w.Step(0.25f);
        w.Step(0.25f);
        Expect(lemon::scripting::BatchStaleMarkCount() == 1, "D5: subsequent frames clean");
        Expect(s.AliveCount() == 70 + 3000, "D5: world stable after stale frame");
    }
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

// M6c 批②：Lemon.Audio 全 API 面（AudioProbeBehaviour typeId 18 装配——静音引擎 +
// guid 0x1111 单 clip + resolver）。断言：staging 返回值语义（Play/PlayAt 非零、
// 坏 guid = 0）/ Stop 真值序（已提交真、二次假）/ MasterVolume 往返（D6）/
// BGM 槽引擎侧对拍（受理 → 硬切释放）/ StopAll 清场；**ComputeStateHash 跨帧
// 逐位不变** = 音频调用零哈希面的机械反例（零重录纪律，09 §6.8）。
void TestAudioSdk() {
    using namespace lemon::ecs;
    auto opsSubmit = (void (*)(unsigned char, unsigned char, uint64_t))GetExport("lemon_ops_submit");
    auto timeResetFn = (void (*)())GetExport("lemon_time_reset");
    Expect(opsSubmit && timeResetFn, "ops/time exports resolved");
    timeResetFn(); // 本测试 = 新一局（AudioProbe 按 FrameCount 分段）

    lemon::audio::AudioEngine eng;
    Expect(eng.Init({.forceSilent = true}), "silent engine for audio sdk test");
    eng.SetRetriggerCooldown(0); // 探针同帧同 clip 连发 Play+PlayAt（API 语义断言）——节流让路
    std::vector<int16_t> pcm(4800 * 2, 4000); // 0.1s 恒幅
    const uint32_t clip = eng.RegisterClip(std::move(pcm), 2, 4800, 0, 0);
    struct AudCtx {
        uint32_t clip;
    } actx{clip};
    WorldDesc d;
    d.threadCount = 1;
    World w(d);
    Scene& s = w.CreateScene("AudT");
    w.SetActiveScene(&s);
    w.SetScriptBackend(&g_sh);
    // 最小管线（AudioSystem 必在——staging 命令的统一提交点）
    w.Pipeline().AddSystem(std::make_unique<DestroyCommitSystem>());
    w.Pipeline().AddSystem(std::make_unique<CSharpBatchSystem>());
    w.Pipeline().AddSystem(std::make_unique<AudioSystem>());
    w.Pipeline().AddSystem(std::make_unique<ScriptEventDispatchSystem>());
    w.Pipeline().ResolveOrder();
    w.SetAudioBackend(&eng,
                      [](uint64_t g, void* p) -> uint32_t {
                          return g == 0x1111ull ? static_cast<AudCtx*>(p)->clip : 0;
                      },
                      &actx);
    w.SetAudioListener({{0, 0}, 640.0f});

    const uint64_t h0 = ComputeStateHash(s); // 空场基准
    int mark1 = 0, mark2 = 0, mark3 = 0;
    w.SetEventSink([&](World&, const EventPacket& p) {
        if (p.type != GameEvent::Custom) return;
        // review 2026-10-02 #22：AudioProbe 号段迁 1500..1559（原 1300..1307 与
        // AnimFx 1300/1400 撞段且 mark3 无上界会误收 1400）
        if (p.user >= 1500 && p.user < 1520) mark1 = (int)p.user - 1500;
        else if (p.user >= 1520 && p.user < 1540) mark2 = (int)p.user - 1520;
        else if (p.user >= 1540 && p.user < 1560) mark3 = (int)p.user - 1540;
    });

    opsSubmit(0, 0, 0x800000000000A001ull);            // Create
    opsSubmit(2, 0 /*Transform2D*/, 0x800000000000A001ull); // 组件在场 = 哈希非平凡
    opsSubmit(4, 18 /*AudioProbeBehaviour*/, 0x800000000000A001ull);

    w.Step(0.25f); // 帧1：Play/PlayAt/Bgm + 坏 guid
    Expect(mark1 == 7, "probe f1: play+at nonzero, bad guid zero");
    Expect(w.Audio().bgmVoice() != 0, "bgm slot occupied (accepted)");
    Expect(eng.ActiveVoiceCount() == 3, "play + playat + bgm voices active");
    const uint64_t h1 = ComputeStateHash(s); // 探针实体 + Transform 在场

    w.Step(0.25f); // 帧2：Stop 真 + MasterVolume/组音量写 + StopBgm(0) 硬切
    Expect(mark2 == 1, "probe f2: stop submitted voice true");
    Expect(w.Audio().bgmVoice() == 0, "bgm slot released (hard stop)");
    Expect(eng.ActiveVoiceCount() == 1, "only playat remains after stops");
    // review 2026-10-02 #23：SDK 组音量桥首覆盖（NativeAudioSetGroupVolume 此前
    // 经脚本零覆盖）
    Expect(std::fabs(eng.GroupVolume(lemon::audio::Group::Bgm) - 0.25f) < 1e-6f,
           "group volume bridge applied via C# staging");
    Expect(ComputeStateHash(s) == h1,
           "audio calls never touch state hash (zero-replay counterexample)");

    w.Step(0.25f); // 帧3：二次 Stop 假 + MasterVolume 读回 + Paused 置真 + 挂起出生
    Expect(mark3 == 7, "probe f3: second stop false + master roundtrip + paused loop issued");
    // review 2026-10-02 #34：D5 Paused 的 C# staging→命令落地链（此前只演练未断言）
    Expect(w.Audio().pausedStaged(), "paused staged flag landed via C# command");
    Expect(eng.ActiveVoiceCount() == 2,
           "paused Bgm loop occupies slot alongside playat oneshot");

    w.Step(0.25f); // 帧4：Paused getter 读回（#71 桥首覆盖）+ 复位 + StopAll 清场 + 自毁
    Expect(mark3 == 13, "probe f4: Paused getter reads engine state true (bridge #71)");
    Expect(!w.Audio().pausedStaged(), "pause released via C# command");
    Expect(eng.ActiveVoiceCount() == 0, "stopall cleared engine voices");
    w.Step(0.25f); // 销毁提交 + 派发
    Expect(ComputeStateHash(s) == h0, "scene back to empty after destroy");
}

// M7c 批①：Lemon.Fx 表现升级全 API 面（FxProbeBehaviour typeId 19 装配——动效
// FxStyle / 贴图血条 FxBarSkin（假 guid 白精灵降级）/ Crit/Miss 糖 / 旧签名并存）。
// 断言：Fx 通道被写入（引擎侧对拍 TextCount/BarCount——升级面真实到达通道）+
// **ComputeStateHash 跨帧逐位不变**（新参数全开调用零哈希面——表现层永不入回放
// 在 S2/S3/S4 扩张后仍锁死；TestAudioSdk 同款机械反例）。
void TestFxSdk() {
    using namespace lemon::ecs;
    auto opsSubmit = (void (*)(unsigned char, unsigned char, uint64_t))GetExport("lemon_ops_submit");
    auto timeResetFn = (void (*)())GetExport("lemon_time_reset");
    Expect(opsSubmit && timeResetFn, "ops/time exports resolved (fx)");
    timeResetFn(); // FxProbe 按 FrameCount 分段

    WorldDesc d;
    d.threadCount = 1;
    World w(d);
    Scene& s = w.CreateScene("FxT");
    w.SetActiveScene(&s);
    w.SetScriptBackend(&g_sh);
    w.Pipeline().AddSystem(std::make_unique<DestroyCommitSystem>());
    w.Pipeline().AddSystem(std::make_unique<CSharpBatchSystem>());
    w.Pipeline().AddSystem(std::make_unique<ScriptEventDispatchSystem>());
    w.Pipeline().ResolveOrder();

    const uint64_t h0 = ComputeStateHash(s);
    int mark = 0;
    w.SetEventSink([&](World&, const EventPacket& p) {
        if (p.type == GameEvent::Custom && p.user >= 1700 && p.user < 1720)
            mark = (int)p.user - 1700;
    });
    opsSubmit(0, 0, 0x800000000000B001ull);                // Create
    opsSubmit(2, 0 /*Transform2D*/, 0x800000000000B001ull); // 组件在场 = 哈希非平凡
    opsSubmit(4, 19 /*FxProbeBehaviour*/, 0x800000000000B001ull);

    w.Step(0.25f); // 帧1：新参数全开全家桶（动效/贴图/糖）+ 旧签名
    Expect(mark == 1, "probe f1 ran");
    Expect(w.Fx().TextCount() == 4, "fx: styled+crit+miss+legacy texts in channel");
    Expect(w.Fx().BarCount() == 1, "fx: skinned+legacy bars collapse to one entity slot");
    const FxBar* fb = nullptr;
    for (const FxBar& b : w.Fx().Bars())
        if (b.entity != 0) fb = &b;
    Expect(fb && fb->lagColor == 0xFFE0F0F0u && fb->height == 6.0f &&
               fb->bgSpriteId == 0 && fb->fgSpriteId == 0,
           "fx: skin fields landed (fake guid -> white sprite fallback)");
    const uint64_t h1 = ComputeStateHash(s); // 探针实体 + Transform 在场
    w.Step(0.25f); // 帧2：自毁提交
    Expect(ComputeStateHash(s) == h1,
           "fx upgraded calls never touch state hash (replay-immune counterexample)");
    w.Step(0.25f); // 销毁落地
    Expect(ComputeStateHash(s) == h0, "scene back to empty after destroy (fx)");
}

// M7c 批⑦：SceneManager 全链（LoadScene 路径/stem 式、四跳含同名重装、三事件
// 序/时序、DontDestroyOnLoad 幸存、Additive/坏名红字拒、Scene 查询面）。探针 =
// SceneProbeBehaviour（typeId 20），Mark 18xx；事件序断言在 C# 侧（log 全序对拍）。
void TestSceneSdk() {
    using namespace lemon::ecs;
    auto opsSubmit = (void (*)(unsigned char, unsigned char, uint64_t))GetExport("lemon_ops_submit");
    auto timeResetFn = (void (*)())GetExport("lemon_time_reset");
    Expect(opsSubmit && timeResetFn, "ops/time exports resolved (scene)");
    timeResetFn(); // SceneProbe 按 FrameCount 分段

    // 内存场景源（SetSceneSourceHooks 宿主实现：stem 与 "Scenes/<stem>.scene" 双键）
    static const std::map<std::string, std::string> kSceneDocs = {
        {"Main", R"({"schemaVersion":2,"name":"Main","entities":[{"components":{"Transform2D":{"pos":[1.0,2.0],"rot":0.0,"scale":[1.0,1.0]}}}]})"},
        {"Grass", R"({"schemaVersion":2,"name":"Grass","entities":[{"components":{"Transform2D":{"pos":[10.0,20.0],"rot":0.0,"scale":[1.0,1.0]}}}]})"},
        {"Volcano", R"({"schemaVersion":2,"name":"Volcano","entities":[{"components":{"Transform2D":{"pos":[30.0,40.0],"rot":0.0,"scale":[1.0,1.0]}}}]})"},
        {"Cave", R"({"schemaVersion":2,"name":"Cave","entities":[{"components":{"Transform2D":{"pos":[50.0,60.0],"rot":0.0,"scale":[1.0,1.0]}}},{"components":{"Transform2D":{"pos":[70.0,80.0],"rot":0.0,"scale":[1.0,1.0]}}}]})"},
    };
    lemon::scripting::SetSceneSourceHooks(
        {[](const char* nameOrPath, lemon::ecs::SceneSwitchRequest& out) -> bool {
            std::string key = nameOrPath ? nameOrPath : "";
            std::string stem = key;
            if (key.rfind("Scenes/", 0) == 0 && key.size() > 7 &&
                key.substr(key.size() - 6) == ".scene")
                stem = key.substr(7, key.size() - 7 - 6);
            const auto it = kSceneDocs.find(stem);
            if (it == kSceneDocs.end()) return false;
            out.name = it->first;
            out.path = "Scenes/" + it->first + ".scene";
            out.jsonText = it->second;
            out.mode = 0;
            return true;
        }});

    WorldDesc d;
    d.threadCount = 1;
    World w(d);
    Scene& s = w.CreateScene("SceneT");
    w.SetActiveScene(&s);
    w.SetScriptBackend(&g_sh);
    w.Pipeline().AddSystem(std::make_unique<DestroyCommitSystem>());
    w.Pipeline().AddSystem(std::make_unique<SceneSwitchSystem>()); // Essential（After DestroyCommit）
    w.Pipeline().AddSystem(std::make_unique<CSharpBatchSystem>());
    w.Pipeline().AddSystem(std::make_unique<ScriptEventDispatchSystem>());
    w.Pipeline().ResolveOrder();

    // F2：初始建档 + 打标（零未打标从第一帧成立；isLoaded=true——宿主同款）
    const uint32_t hMain = w.CreateSceneRecord("Main", "Scenes/Main.scene");
    if (lemon::ecs::World::SceneRecord* r = w.FindSceneRecord(hMain)) r->isLoaded = true;
    StampSceneMembership(s, hMain);
    w.SetActiveSceneHandle(hMain);

    int mark = 0;
    w.SetEventSink([&](World&, const EventPacket& p) {
        if (p.type == GameEvent::Custom && p.user >= 1800 && p.user < 1820)
            mark = (int)p.user - 1800;
    });
    opsSubmit(0, 0, 0x800000000000C001ull);                  // Create（ApplyStructural 打 active 标）
    opsSubmit(2, 0 /*Transform2D*/, 0x800000000000C001ull);
    opsSubmit(4, 20 /*SceneProbeBehaviour*/, 0x800000000000C001ull);

    w.Step(0.25f); // 帧1：初始查询面
    Expect(mark == 1, "scene: initial query face ok");
    w.Step(0.25f); // 帧2：自标 DDOL（根位）
    Expect(mark == 2, "scene: ddol marked");
    w.Step(0.25f); // 帧3：路径式请求入队
    Expect(mark == 3, "scene: load requested (path form)");
    w.Step(0.25f); // 帧4：Essential 换场（Grass）+ 探针断言事件三连
    Expect(mark == 4, "scene: hop1 events ordered+timed, queries ok");
    const uint32_t hGrass1 = w.ActiveSceneHandle();
    Expect(hGrass1 != hMain, "scene: hop1 active handle");
    w.Step(0.25f); // 帧5：Volcano
    Expect(mark == 5, "scene: hop2 ok (stem form, fresh handle)");
    w.Step(0.25f); // 帧6：Grass 同名重装 + 坏名拒
    Expect(mark == 6, "scene: reload ok (fresh handle), bad name rejected");
    w.Step(0.25f); // 帧7：坏名拒后世界不动 + Additive 拒
    Expect(mark == 7, "scene: bad name left world intact");
    w.Step(0.25f); // 帧8：Additive 拒后世界不动 + Cave 请求
    Expect(mark == 8, "scene: additive rejected, world intact");
    w.Step(0.25f); // 帧9：终态（12 事件全序 + DDOL 跨四跳 + 查询面）
    Expect(mark == 9, "scene: final state + full event log + ddol survivor");
    // 引擎面对拍：四跳后 active = Cave 新句柄、DDOL 系幸存 1（探针实体）、零孤组、
    // 档案 5 份（Main + Grass + Volcano + Grass + Cave——每载一档）
    Expect(w.ActiveSceneHandle() != hGrass1 && w.ActiveSceneHandle() != hMain,
           "scene: engine active handle progressed");
    Expect(w.SceneRecordCount() == 5, "scene: one record per load (handles never reused)");
    Expect(CountSceneGroup(s, lemon::ecs::kSceneHandleUnassigned) == 0, "scene: zero orphans");
    const std::vector<Entity> ddol = CollectDontDestroyOnLoadLineage(s);
    bool probeSurvived = ddol.size() == 1;
    if (probeSurvived) // 唯一 DDOL 幸存者 = 带脚本盒的探针实体（句柄值不写死）
        probeSurvived = s.Alive(ddol[0]) && s.TryGet<lemon::scripting::ScriptBox>(ddol[0]) != nullptr;
    Expect(probeSurvived, "scene: probe entity is the lone ddol survivor (root-bit)");
}

// M7c 批⑧：LoadSceneAsync C# 语义全链（AsyncSceneProbeBehaviour typeId 21；Mark
// 1851..1861）：直通激活 + progress 契约 + 门控 0.9/开门 + await 域线程续跑 +
// 单槽取代（被取代 completed 不推）+ Additive/坏名无效 op。引擎面对拍：档案数/
// 零孤组/终态 active/DDOL 幸存。进度单调性引擎面已由 SceneTests 钉（此处 C# 面）。
void TestSceneAsyncSdk() {
    using namespace lemon::ecs;
    auto opsSubmit = (void (*)(unsigned char, unsigned char, uint64_t))GetExport("lemon_ops_submit");
    auto timeResetFn = (void (*)())GetExport("lemon_time_reset");
    auto playResetFn = (void (*)())GetExport("lemon_play_reset");
    Expect(opsSubmit && timeResetFn && playResetFn,
           "ops/time/play-reset exports resolved (scene-async)");
    timeResetFn();   // AsyncSceneProbe 按 FrameCount 分段
    playResetFn();   // **新 World = 新一局**（编辑器重进 Play 同款）：清 SceneManager
                     // 句柄记忆化（否则上一测试世界的句柄 2/3/4 缓存把本世界同名号
                     // 误映射——TestSceneSdk 后首个再造档案的测试才会踩到的面）+ 清
                     // 上一世界遗留的 behaviour 实例（句柄重发跨域撞车，M5 批④ 同源）
    // 内存场景源已进程级注册（TestSceneSdk 的 kSceneDocs 同域复用——脚本侧
    // LoadSceneAsync 经同一条 SceneSourceHooks 寻址）

    WorldDesc d;
    d.threadCount = 1;
    World w(d);
    Scene& s = w.CreateScene("SceneAsyncT");
    w.SetActiveScene(&s);
    w.SetScriptBackend(&g_sh);
    w.Pipeline().AddSystem(std::make_unique<DestroyCommitSystem>());
    w.Pipeline().AddSystem(std::make_unique<SceneSwitchSystem>());
    w.Pipeline().AddSystem(std::make_unique<CSharpBatchSystem>());
    w.Pipeline().AddSystem(std::make_unique<ScriptEventDispatchSystem>());
    w.Pipeline().ResolveOrder();

    const uint32_t hMain = w.CreateSceneRecord("Main", "Scenes/Main.scene");
    if (lemon::ecs::World::SceneRecord* r = w.FindSceneRecord(hMain)) r->isLoaded = true;
    StampSceneMembership(s, hMain);
    w.SetActiveSceneHandle(hMain);

    int mark = 0;
    w.SetEventSink([&](World&, const EventPacket& p) {
        if (p.type == GameEvent::Custom && p.user >= 1850 && p.user < 1870)
            mark = (int)p.user - 1850;
    });
    opsSubmit(0, 0, 0x800000000000C021ull);                  // Create
    opsSubmit(2, 0 /*Transform2D*/, 0x800000000000C021ull);
    opsSubmit(4, 21 /*AsyncSceneProbeBehaviour*/, 0x800000000000C021ull);

    for (int f = 1; f <= 11; ++f) {
        w.Step(0.25f);
        Expect(mark == f, "scene-async: frame-by-frame contract holds at fc");
    }
    // 引擎面对拍：4 次装载（Volcano/Grass/Cave/Cave——被取代的 Volcano 不建档）
    // + Main 初始档 = 5 档；终态 Cave；零孤组；探针 DDOL 幸存
    Expect(w.SceneRecordCount() == 5, "scene-async: one record per completed load");
    Expect(w.FindSceneRecord(w.ActiveSceneHandle()) &&
                w.FindSceneRecord(w.ActiveSceneHandle())->name == std::string("Cave"),
           "scene-async: final active is Cave");
    Expect(CountSceneGroup(s, lemon::ecs::kSceneHandleUnassigned) == 0, "scene-async: zero orphans");
    Expect(CollectDontDestroyOnLoadLineage(s).size() == 1, "scene-async: probe ddol survivor");
}

// M5 批①：Time.Scale（native 表往返 + 缩放 dt 链到 Time.DeltaTime）与
// Lemon.Ui.Set（World RtUi 定长槽，C++ 侧读回断言）
void TestTimeScaleAndUiChannel() {
    using namespace lemon::ecs;
    auto opsSubmit = (void (*)(unsigned char, unsigned char, uint64_t))GetExport("lemon_ops_submit");
    auto timeResetFn = (void (*)())GetExport("lemon_time_reset");
    Expect(opsSubmit && timeResetFn, "ops/time exports resolved");
    timeResetFn(); // 本测试 = 新一局（ScaleUiProbe 按 FrameCount 分段）

    WorldDesc d;
    d.threadCount = 1;
    World w(d);
    Scene& s = w.CreateScene("TsT");
    w.SetActiveScene(&s);
    w.SetScriptBackend(&g_sh);
    w.Pipeline().AddSystem(std::make_unique<DestroyCommitSystem>());
    w.Pipeline().AddSystem(std::make_unique<CSharpBatchSystem>());
    w.Pipeline().AddSystem(std::make_unique<ScriptEventDispatchSystem>());
    w.Pipeline().ResolveOrder();

    int sawSet = 0, sawDt = 0, sawReset = 0;
    w.SetEventSink([&](World&, const EventPacket& p) {
        if (p.type != GameEvent::Custom) return;
        if (p.user == 305) ++sawSet;      // 帧1：C# 读回 Scale=0.5 → 300+5
        else if (p.user == 525) ++sawDt;  // 帧2：缩放 DeltaTime 0.125 → 400+125
        else if (p.user == 610) ++sawReset; // 帧3：复位读回 1.0 → 600+10
    });

    opsSubmit(0, 0, 0x8000000000000004ull); // Create + Attach ScaleUiProbe（typeId 4 表尾）
    opsSubmit(4, 4, 0x8000000000000004ull);

    w.Step(0.25f); // 帧1：置 Scale=0.5、报 305
    Expect(sawSet == 1, "C# Time.Scale readback 0.5 (Custom 305)");
    Expect(w.TimeScale() == 0.5f, "native set: World.TimeScale == 0.5");

    w.Step(0.25f); // 帧2：DeltaTime = 0.25×0.5、报 525；Ui.Set("xp",...)
    Expect(sawDt == 1, "scaled DeltaTime 0.125 reached C# (Custom 525)");
    bool slotOk = w.RtUi().Count() == 1;
    if (slotOk) {
        const RtUiSlot& slot = w.RtUi().At(0);
        slotOk = std::strcmp(slot.key, "xp") == 0 &&
                 std::strcmp(slot.text, "LV3 45/120") == 0 &&
                 std::fabs(slot.frac - 0.45f) < 1e-5f;
    }
    Expect(slotOk, "ui slot key/text/frac written via Lemon.Ui.Set");

    w.Step(0.25f); // 帧3：复位 Scale=1、报 610、自毁命令
    w.Step(0.25f); // 应用销毁 + 派发
    Expect(sawReset == 1 && w.TimeScale() == 1.0f, "scale restored to 1.0 (Custom 610)");

    // 新 World 自清零（EnterPlay 同语义——编辑器每局新建 playWorld）
    {
        World w2(d);
        Expect(w2.RtUi().Count() == 0 && w2.TimeScale() == 1.0f,
               "fresh world: ui slots clear + scale 1");
    }
}

// M5 批②：WaveStart → C# 订阅 → Ui.Set 波次行（引擎事件→RT UI 最小闭环；
// WaveBannerBehaviour typeId 5 表尾注册，构造期 Subscribe）
void TestWaveStartToUi() {
    using namespace lemon::ecs;
    auto opsSubmit = (void (*)(unsigned char, unsigned char, uint64_t))GetExport("lemon_ops_submit");
    Expect(opsSubmit, "ops export resolved");

    WorldDesc d;
    d.threadCount = 1;
    World w(d);
    Scene& s = w.CreateScene("Wav");
    w.SetActiveScene(&s);
    w.SetScriptBackend(&g_sh);
    w.Pipeline().AddSystem(std::make_unique<DestroyCommitSystem>());
    w.Pipeline().AddSystem(std::make_unique<CSharpBatchSystem>());
    w.Pipeline().AddSystem(std::make_unique<ScriptEventDispatchSystem>());
    w.Pipeline().ResolveOrder();

    opsSubmit(0, 0, 0x8000000000000005ull); // Create + Attach WaveBannerBehaviour（typeId 5）
    opsSubmit(4, 5, 0x8000000000000005ull);

    // 人工推 WaveStart 包（引擎侧 DirectorSystem 同款契约：[0]=波序号 [1]=计划数）
    EventPacket p{};
    p.type = GameEvent::WaveStart;
    p.payload[0] = 2.0f; // 第 3 波（0 起）
    p.payload[1] = 45.0f;
    w.Events().Push(p);

    w.Step(0.25f); // 帧末派发 → C# 订阅 → Ui.Set
    bool slotOk = w.RtUi().Count() == 1;
    if (slotOk) {
        const RtUiSlot& slot = w.RtUi().At(0);
        slotOk = std::strcmp(slot.key, "wave") == 0 &&
                 std::strcmp(slot.text, "WAVE 3 x45") == 0 && slot.frac < 0.0f;
    }
    Expect(slotOk, "WaveStart -> Ui.Set wave banner (key/text/text-only)");
}

// M5 批④：存档通道（SaveChannel 编解码/边界 + C# Save API 往返/Flush no-op）
// + HUD 完整版（着色槽/Clear/卡片/CardPick 消费语义）+ Input.Confirm 位。
// SaveCardsProbeBehaviour typeId 6 表尾注册；Custom 编码见 TestScript.cs 注释。
void TestSaveChannelAndUiCards() {
    using namespace lemon::ecs;
    // ---- 引擎侧单元：Encode/Decode 往返 + 边界（坏档拒绝/键长上限/cap 不足）----
    {
        SaveChannel ch;
        const uint8_t raw[4] = {1, 2, 3, 255};
        Expect(ch.Set("k0", raw, 4) && ch.Set("k1", "ab", 2), "save set entries");
        Expect(ch.GetLen("k0") == 4 && ch.GetLen("nope") == -1, "getlen hit/miss");
        uint8_t out[4] = {};
        Expect(ch.Get("k0", out, 4) == 4 && out[3] == 255, "get copies bytes");
        Expect(ch.Get("k0", out, 3) == -2, "get cap insufficient = -2");
        Expect(!ch.Set("", raw, 4), "empty key rejected");
        const std::vector<uint8_t> enc = ch.Encode();
        SaveChannel ch2;
        Expect(ch2.Decode(enc.data(), enc.size()) && ch2.GetLen("k0") == 4 &&
                   ch2.GetLen("k1") == 2,
               "encode/decode roundtrip");
        Expect(!ch2.Decode(enc.data(), enc.size() / 2), "corrupt (truncated) rejected");
        Expect(ch2.Remove("k0") && !ch2.Remove("k0") && ch2.Count() == 1, "remove once");
    }

    auto opsSubmit = (void (*)(unsigned char, unsigned char, uint64_t))GetExport("lemon_ops_submit");
    auto timeResetFn = (void (*)())GetExport("lemon_time_reset");
    Expect(opsSubmit && timeResetFn, "ops/time exports resolved");
    timeResetFn(); // 探针按 FrameCount 分段——本测试 = 新一局（批① 测试同款处理）

    WorldDesc d;
    d.threadCount = 1;
    World w(d);
    Scene& s = w.CreateScene("SvC");
    w.SetActiveScene(&s);
    w.SetScriptBackend(&g_sh);
    w.Pipeline().AddSystem(std::make_unique<DestroyCommitSystem>());
    w.Pipeline().AddSystem(std::make_unique<CSharpBatchSystem>());
    w.Pipeline().AddSystem(std::make_unique<ScriptEventDispatchSystem>());
    w.Pipeline().ResolveOrder();

    int saw700 = 0, saw800 = 0, saw900 = 0, saw960 = 0, saw980 = 0;
    w.SetEventSink([&](World&, const EventPacket& p) {
        if (p.type != GameEvent::Custom) return;
        if (p.user == 700) ++saw700;
        else if (p.user == 801) ++saw800;
        else if (p.user == 910) ++saw900;
        else if (p.user == 965) ++saw960;
        else if (p.user == 980) ++saw980;
    });

    opsSubmit(0, 0, 0x8000000000000006ull); // Create + Attach SaveCardsProbe（typeId 6 表尾）
    opsSubmit(4, 6, 0x8000000000000006ull);

    w.Step(0.25f); // 帧1：写档 + 着色槽 + Clear 删行
    Expect(saw700 == 1, "frame1 probe report (Custom 700)");
    Expect(w.Saves().GetLen("probe") == 7 && w.Saves().GetLen("raw") == 4,
           "C# Save.SetString/Set landed in World.Saves");
    bool slotOk = w.RtUi().Count() == 1;
    if (slotOk) {
        const RtUiSlot& slot = w.RtUi().At(0);
        slotOk = std::strcmp(slot.key, "hp") == 0 && slot.color == 0xFF30B0F0u &&
                 std::fabs(slot.frac - 0.68f) < 1e-5f;
    }
    Expect(slotOk, "colored ui slot (key/color/frac); tmp row cleared");

    w.Step(0.25f); // 帧2：读回 + ShowCards
    Expect(saw800 == 1, "frame2 readback ok (Custom 801: string+bytes+HasKey)");
    Expect(w.Cards().active && std::strcmp(w.Cards().labels[1], "磁力 +25%") == 0,
           "ui cards shown (title/labels)");
    w.Cards().pick = 1; // 模拟 GameView 按钮/数字键选择（帧间回写；Show 已清旧值）
    InputState in{};
    in.buttons = 1ull << 5; // bit5 confirm（R 键语义）
    w.ApplyInput(in);

    w.Step(0.25f); // 帧3：CardPick 消费 + Confirm + Hide + Flush(no-op)
    Expect(saw900 == 1, "card pick consumed once (Custom 910: pick=1,again=-1)");
    Expect(saw960 == 1, "Input.Confirm bit5 reached C# (Custom 965)");
    Expect(!w.Cards().active, "cards hidden after pick");
    w.Step(0.25f); // 帧4：ShowDialog（批④后修④单按钮对话框——B/C 留空）
    Expect(w.Cards().active && std::strcmp(w.Cards().labels[0], "复活") == 0 &&
               w.Cards().labels[1][0] == '\0' && w.Cards().labels[2][0] == '\0',
           "ui dialog shown (single button; B/C empty)");
    w.Cards().pick = 0; // 对话框唯一按钮（点击/数字键 1 同通道）
    w.Step(0.25f); // 帧5：对话框 pick 消费 + Hide + 自毁
    Expect(saw980 == 1, "dialog pick consumed (Custom 980: pick=0)");
    Expect(!w.Cards().active, "dialog hidden after pick");
    w.Step(0.25f); // 销毁提交
}

// M5 批④后修（用户实测）：Stop→Play 后 Blade 每局递增——脚本域跨局残留。
// 根因：ExitPlay 弃 playWorld 时 C# 侧无人 Detach（Detach 只挂单实体 Destroy
// 命令路径），EnterPlay 对同实体 id（新世界确定性重排 = 同 id）再 Attach =
// 同实体双实例双 tick。修复 = lemon_play_reset（EnterPlay 期硬清实例/事件
// 订阅；Unity "Enter Play = 新域"同语义）。bench/回放不经该路径 = 金档零扰动。
void TestPlayDomainReset() {
    using namespace lemon::ecs;
    auto timeResetFn = (void (*)())GetExport("lemon_time_reset");
    auto attachedFn = (int (*)())GetExport("lemon_behaviours_attached");
    Expect(timeResetFn && attachedFn, "play-reset exports resolved");
    timeResetFn();

    WorldDesc d;
    d.threadCount = 1;
    World w(d);
    Scene& s = w.CreateScene("PDR");
    w.SetActiveScene(&s);
    w.SetScriptBackend(&g_sh);
    w.Pipeline().AddSystem(std::make_unique<DestroyCommitSystem>());
    w.Pipeline().AddSystem(std::make_unique<CSharpBatchSystem>());
    w.Pipeline().AddSystem(std::make_unique<ScriptEventDispatchSystem>());
    w.Pipeline().ResolveOrder();

    int saw700 = 0, saw801 = 0, saw42 = 0;
    w.SetEventSink([&](World&, const EventPacket& p) {
        if (p.type != GameEvent::Custom) return;
        if (p.user == 700) ++saw700;
        else if (p.user == 801) ++saw801;
        else if (p.user == 42) ++saw42;
    });

    // 局1：装配探针（M6a 批⓪ 起 AttachBehaviour = 追加路径，同实体同类型唯一）
    Entity e = s.Create();
    g_sh.AttachBehaviour(w, s, e, 6);
    w.Step(0.25f); // 帧1
    Expect(attachedFn() == 1 && saw700 == 1, "session1: single instance reports once");

    // 同实体同类型重复挂载 = 双层拒绝（M6a 批⓪ 决策 4）：C++ AttachBehaviour 按
    // typeId 查重不追加槽，C# Behaviours.Attach 断言跳过。原泄漏复现（残留实例 +
    // 同实体 id 再 Attach = 双实例双 tick）在新不变量下不可达——检测面从
    // "双实例可观测"升级为"重复挂载被拒"，本段断言其确被拒绝。
    g_sh.AttachBehaviour(w, s, e, 6);
    Expect(attachedFn() == 1, "duplicate attach rejected: instance count stays 1");
    w.Step(0.25f); // 帧2：单实例照常 tick（读回报告 ×1）
    Expect(saw801 == 1, "single instance ticks once (Custom 801 x1)");

    // 修复：EnterPlay 序（Time 归零 → 域复位 → 重挂）
    g_sh.ResetPlayDomain();
    Expect(attachedFn() == 0, "play reset clears instances");
    timeResetFn();
    g_sh.AttachBehaviour(w, s, e, 6);
    Expect(attachedFn() == 1, "re-play: single instance (leak fixed)");
    w.Step(0.25f); // 新局帧1
    Expect(saw700 == 2, "re-play adds exactly one report (2 = 1+1)");

    // 静态订阅跨局存活（P2 批 2026-09-25 修复回归）：Configure 期
    // Events.Subscribe(Hit→Custom42) 不随 lemon_play_reset 清（原 Events.Reset
    // 误清 = Stop→Play 后静态链全哑）；实例级订阅由 ClearInstances 逐实例退订
    //（M15 Subscribe 助手路径另有专测）
    {
        EventPacket hit{};
        hit.type = GameEvent::Hit;
        w.Events().Push(hit);
        w.Step(0.25f); // C# 静态 handler 收 Hit → push Custom 42 入 pending
        w.Step(0.25f); // #15 拉取入队 → sink 收 42
        Expect(saw42 == 1, "static subscription survives play reset (Custom 42 x1)");
    }

    g_sh.ResetPlayDomain(); // 收尾自清：不让本测试实例泄给后续（进程内域共享）
    Expect(attachedFn() == 0, "post-test domain clean");
}

// M11：Attach/Destroy 期 native 窗口（修复前窗口只盖 TickBatch/DispatchEvents，
// Awake/OnDestroy 内 Ui.Set 静默空转）+ M12：结构命令 AddComponent/AttachScript
// 的 get-or-create（修复前无条件 Emplace = entt 池损坏）。
void TestAwakeWindowAndDoubleAdd() {
    using namespace lemon::ecs;
    auto opsSubmit =
        (void (*)(unsigned char, unsigned char, uint64_t))GetExport("lemon_ops_submit");
    auto attachedFn = (int (*)())GetExport("lemon_behaviours_attached");
    auto timeResetFn = (void (*)())GetExport("lemon_time_reset");
    Expect(opsSubmit && attachedFn && timeResetFn, "awake-window exports resolved");
    timeResetFn();

    WorldDesc d;
    d.threadCount = 1;
    World w(d);
    Scene& s = w.CreateScene("AWN");
    w.SetActiveScene(&s);
    w.SetScriptBackend(&g_sh);
    w.Pipeline().AddSystem(std::make_unique<DestroyCommitSystem>());
    w.Pipeline().AddSystem(std::make_unique<CSharpBatchSystem>());
    w.Pipeline().AddSystem(std::make_unique<ScriptEventDispatchSystem>());
    w.Pipeline().ResolveOrder();

    // M11 attach：Awake 与 AttachBehaviour 同步执行 → 调用返回即应已写 RT UI
    //（修复前 g_world 未设 → NativeRtUiSet 早退，槽位为空）
    Entity e = s.Create();
    g_sh.AttachBehaviour(w, s, e, 7); // AwakeUiProbeBehaviour（表尾 typeId 7）
    bool aliveOk = w.RtUi().Count() == 1;
    if (aliveOk) {
        const RtUiSlot& slot = w.RtUi().At(0);
        aliveOk = std::strcmp(slot.key, "awake") == 0 &&
                  std::strcmp(slot.text, "alive") == 0 &&
                  std::fabs(slot.frac - 1.0f) < 1e-5f;
    }
    Expect(aliveOk, "Awake Ui.Set visible right after AttachBehaviour (M11 window)");
    const int attached0 = attachedFn();

    // M12 case4 → M6a 批⓪ 语义升级：AttachScript 结构命令对已带同类型 ScriptBox 的
    // 实体 = 幂等（槽层查重不追加 + C# Attach 断言拒双挂）。修复前的两个坑分别
    // 消解：无条件 Emplace = entt 池损坏（M12，TryGet 分支）；再挂一实例 = 双实例
    // 双 tick（决策 4，Attach 断言——实例数不增即证）
    opsSubmit(4, 7, e.id);
    w.Step(0.25f); // ApplyStructural 帧首应用
    Expect(s.TryGet<lemon::scripting::ScriptBox>(e) != nullptr &&
               attachedFn() == attached0,
           "AttachScript op idempotent on scripted entity: no double instance");

    // M12 case2：AddComponent 对已有组件重复提交 = no-op（修复前二次 emplace）
    const ComponentMeta* vel = ComponentRegistry::Instance().Find("Velocity");
    Expect(vel != nullptr, "Velocity meta found");
    opsSubmit(2, (unsigned char)vel->id, e.id);
    opsSubmit(2, (unsigned char)vel->id, e.id);
    w.Step(0.25f);
    bool poolOk = s.Has<Velocity>(e);
    uint32_t ents = 0;
    s.Each([&](Entity) { ++ents; });
    poolOk = poolOk && ents == 1;
    Expect(poolOk, "double AddComponent op: single component, pool intact (M12)");

    // M11 destroy：OnDestroy 内 Ui.Set 生效（alive 行覆写为 dead；同 key 槽位更新）。
    // Detach 清实体全部实例（直挂 + 命令挂共 2 个）→ 计数归零
    opsSubmit(1, 0, e.id);
    w.Step(0.25f); // 帧首应用销毁命令（OnDestroy 同步执行）
    Expect(attachedFn() == 0, "both instances detached after destroy op");
    bool deadOk = w.RtUi().Count() == 1;
    if (deadOk) {
        const RtUiSlot& slot = w.RtUi().At(0);
        deadOk = std::strcmp(slot.key, "awake") == 0 && std::strcmp(slot.text, "dead") == 0;
    }
    Expect(deadOk, "OnDestroy Ui.Set visible (M11 destroy window)");

    g_sh.ResetPlayDomain(); // 收尾自清（同上）
}

// M15：Subscribe 助手 + Detach 自动退订——挂载→销毁→再挂载后推一次 900，
// 恰一份 901 回执（旧实例订阅残留 = 双份；裸 Events.Subscribe 的旧行为）。
void TestSubscribeAutoUnsubscribe() {
    using namespace lemon::ecs;
    auto opsSubmit =
        (void (*)(unsigned char, unsigned char, uint64_t))GetExport("lemon_ops_submit");
    auto timeResetFn = (void (*)())GetExport("lemon_time_reset");
    Expect(opsSubmit && timeResetFn, "subscribe-probe exports resolved");
    timeResetFn();

    WorldDesc d;
    d.threadCount = 1;
    World w(d);
    Scene& s = w.CreateScene("SUB");
    w.SetActiveScene(&s);
    w.SetScriptBackend(&g_sh);
    w.Pipeline().AddSystem(std::make_unique<DestroyCommitSystem>());
    w.Pipeline().AddSystem(std::make_unique<CSharpBatchSystem>());
    w.Pipeline().AddSystem(std::make_unique<ScriptEventDispatchSystem>());
    w.Pipeline().ResolveOrder();

    int got = 0;
    w.SetEventSink([&](World&, const EventPacket& p) {
        if (p.type == GameEvent::Custom && p.user == 901) ++got;
    });

    // 局1：挂载 → 销毁（Detach 必须连带退订）
    Entity e1 = s.Create();
    g_sh.AttachBehaviour(w, s, e1, 8); // SubProbeBehaviour（表尾 typeId 8）
    opsSubmit(1, 0, e1.id);
    w.Step(0.25f); // 应用销毁 → Detach → ClearSubscriptions
    w.Step(0.25f);

    // 局2：同域再挂载新实例 → 推一次 900 → 恰一份回执
    Entity e2 = s.Create();
    g_sh.AttachBehaviour(w, s, e2, 8);
    EventPacket p{};
    p.type = GameEvent::Custom;
    p.user = 900;
    w.Events().Push(p);
    w.Step(0.25f); // 帧末派发：900 → handler → Push(901) 入托管 pending
    w.Step(0.25f); // 下帧派发头部拉 pending（派发期回推 = 下帧送达，#15 语义）
    Expect(got == 1, "single receipt after re-attach (stale subscription would double)");

    g_sh.ResetPlayDomain(); // 收尾自清（同上）
}

// ---- F-08.2（2026-09-24）：C++ 路径销毁的 OnDestroy 通知 ----
// 旧链：脚本命令路径 Destroy 在 ApplyStructural 通知；C++ 系统直接 scene.Destroy
// 入队后无人通知——托管实例/实例级订阅（M15 自动退订挂 OnDestroy）残留到换域。
// 新链：DestroyCommitSystem 提交前对待销毁队列 ∩ ScriptBox 补发（flag 去重恰好一次）。
void TestCppDestroyNotify() {
    using namespace lemon::ecs;
    auto behAttached = (int (*)())GetExport("lemon_behaviours_attached");
    auto listFn = (int (*)(char*, int))GetExport("lemon_behaviours_list");
    Expect(behAttached && listFn, "behaviour exports resolved");

    // typeId 按名解析（表尾注册 = 9；按名找免硬编码漂移）
    char names[2048];
    int nNames = listFn(names, sizeof(names));
    int probeId = -1;
    {
        char* ctx = nullptr;
        int idx = 0;
        for (char* tok = strtok_r(names, "\n", &ctx); tok;
             tok = strtok_r(nullptr, "\n", &ctx), ++idx)
            if (std::strcmp(tok, "CppDestroyProbeBehaviour") == 0) probeId = idx;
    }
    Expect(nNames > 0 && probeId >= 0, "CppDestroyProbeBehaviour registered");

    WorldDesc d;
    d.threadCount = 1;
    World w(d);
    Scene& s = w.CreateScene("CppDestruct");
    w.SetActiveScene(&s);
    w.SetScriptBackend(&g_sh);
    w.Pipeline().AddSystem(std::make_unique<DestroyCommitSystem>());
    w.Pipeline().AddSystem(std::make_unique<CSharpBatchSystem>());
    w.Pipeline().AddSystem(std::make_unique<ScriptEventDispatchSystem>());
    w.Pipeline().ResolveOrder();

    int onDestroyCpp = 0;
    w.SetEventSink([&](World&, const EventPacket& p) {
        if (p.type == GameEvent::Custom && p.user == 177) ++onDestroyCpp;
    });

    // C++ 侧建实体 + 挂脚本（不经命令流——正是 C++ 路径的形状）
    Entity e = s.Create();
    g_sh.AttachBehaviour(w, s, e, probeId);
    w.Step(0.25f); // Update 帧1
    Expect(behAttached() >= 1, "probe attached");

    s.Destroy(e);  // C++ 系统路径入队（战斗击杀/投射物到期同形状）
    w.Step(0.25f); // Essential 通知 → Detach → OnDestroy push（#15 同帧/下帧派发）
    w.Step(0.25f); // 送达余量（派发期回推 = 下帧送达，#15 语义）
    Expect(onDestroyCpp == 1, "C++-path destroy fires OnDestroy exactly once");
    Expect(!s.Alive(e), "entity committed");
    w.Step(0.25f);
    Expect(onDestroyCpp == 1, "no duplicate OnDestroy on later steps");

    g_sh.ResetPlayDomain(); // 收尾自清（同上）
}

// M6a 批⓪ T3：GameObject 统一门面双路由（C# DualRouteProbe typeId 10 订阅驱动）。
// 覆盖：AddComponent 值组件(op2)/脚本(op4 ×2 幂等 get-or-add)、GetComponent 帧边界
//（命令帧首应用——同帧脚本查 = null）与实例命中/值读回、RemoveComponent 脚本
//（op5：单槽 OnDestroy+退订+槽移除，空盒随卸）与值组件(op3)。回报码 15xx = ok。
void TestSdkDualRoute() {
    using namespace lemon::ecs;
    using lemon::scripting::ScriptBox;
    auto attachedFn = (int (*)())GetExport("lemon_behaviours_attached");
    auto timeResetFn = (void (*)())GetExport("lemon_time_reset");
    Expect(attachedFn && timeResetFn, "dual-route exports resolved");
    timeResetFn();

    WorldDesc d;
    d.threadCount = 1;
    World w(d);
    Scene& s = w.CreateScene("DualRoute");
    w.SetActiveScene(&s);
    w.SetScriptBackend(&g_sh);
    w.Pipeline().AddSystem(std::make_unique<DestroyCommitSystem>());
    w.Pipeline().AddSystem(std::make_unique<CSharpBatchSystem>());
    w.Pipeline().AddSystem(std::make_unique<ScriptEventDispatchSystem>());
    w.Pipeline().ResolveOrder();

    int ok1541 = 0, ok1542 = 0, ok1543 = 0, ok1544 = 0, bad = 0;
    w.SetEventSink([&](World&, const EventPacket& p) {
        if (p.type != GameEvent::Custom) return;
        if (p.user == 1541) ++ok1541;
        else if (p.user == 1542) ++ok1542;
        else if (p.user == 1543) ++ok1543;
        else if (p.user == 1544) ++ok1544;
        else if (p.user >= 2541 && p.user <= 2543) ++bad;
    });

    Entity e = s.Create();
    s.Emplace<Transform2D>(e);
    g_sh.AttachBehaviour(w, s, e, 10); // DualRouteProbe（表尾 typeId 10）
    w.Step(0.25f); // 帧1：订阅就位
    const int attached0 = attachedFn();

    auto push = [&](unsigned user) {
        EventPacket p{};
        p.type = GameEvent::Custom;
        p.user = (uint16_t)user;
        p.src = e;
        w.Events().Push(p);
    };

    // 相位1（1500）：值组件 + 脚本 ×2（门面去重）。帧2 分发回调入命令；
    // 帧3 帧首应用（Health + InputMover 槽/实例），#15 送达 1541。
    push(1500);
    w.Step(0.25f);
    w.Step(0.25f);
    Expect(attachedFn() == attached0 + 1,
           "AddComponent x2 deduped (get-or-add facade gate)");
    Expect(s.Has<Health>(e), "AddComponent value route emplaced Health");
    if (const ScriptBox* sb = s.TryGet<ScriptBox>(e))
        Expect(sb->count == 2 && sb->slots[1].typeId == 2,
               "script route appended one InputMover slot");
    else
        Expect(false, "ScriptBox present after script AddComponent");
    w.Step(0.25f); // 送达余量
    Expect(ok1541 == 1, "same-frame GetComponent after AddComponent = null (frame boundary)");
    Expect(bad == 0, "no failure markers from phase 1");

    // 相位2（1501）：实例命中 + 值读回 → 卸双份（op5 + op3，帧首应用）
    push(1501);
    w.Step(0.25f);
    w.Step(0.25f);
    Expect(!s.Has<Health>(e), "RemoveComponent value route removed Health");
    if (const ScriptBox* sb = s.TryGet<ScriptBox>(e))
        Expect(sb->count == 1 && sb->slots[0].typeId == 10,
               "op5 removed script slot in place (DualRoute remains)");
    else
        Expect(false, "ScriptBox remains while DualRoute attached");
    Expect(attachedFn() == attached0, "InputMover instance detached (attached back to base)");
    w.Step(0.25f);
    Expect(ok1542 == 1 && ok1543 == 1, "GetComponent hits: instance + value copy");
    Expect(bad == 0, "no failure markers from phase 2");

    // 相位3（1502）：自卸 → 空盒随卸（ScriptBox 组件整体移除）+ OnDestroy 恰一次
    push(1502);
    w.Step(0.25f);
    w.Step(0.25f);
    Expect(!s.Has<ScriptBox>(e), "empty ScriptBox removed after last detach");
    Expect(attachedFn() == attached0 - 1, "self-detach released the instance");
    w.Step(0.25f);
    Expect(ok1544 == 1, "self-detach fired OnDestroy exactly once");
    Expect(attachedFn() == attached0 - 1 && bad == 0, "final state consistent");
}

// M6a 批①：Lemon.Anim SDK 字段契约 + Lemon.Fx 通道（AnimFxProbeBehaviour
// typeId 11 表尾注册）。管线不含 AnimatorSystem——帧间无引擎消费扰动，
// 断言"SDK 写什么落什么"；队列消费语义在 engine-tests 全管线覆盖。
void TestAnimFxSdk() {
    using namespace lemon::ecs;
    auto timeResetFn = (void (*)())GetExport("lemon_time_reset");
    Expect(timeResetFn, "animfx: time reset export resolved");
    timeResetFn(); // 探针按 FrameCount 分段（新一局）

    WorldDesc d;
    d.threadCount = 1;
    World w(d);
    Scene& s = w.CreateScene("AnimFx");
    w.SetActiveScene(&s);
    w.SetScriptBackend(&g_sh);
    w.Pipeline().AddSystem(std::make_unique<DestroyCommitSystem>());
    w.Pipeline().AddSystem(std::make_unique<CSharpBatchSystem>());
    w.Pipeline().AddSystem(std::make_unique<ScriptEventDispatchSystem>());
    w.Pipeline().ResolveOrder();

    int saw[5] = {0, 0, 0, 0, 0};
    w.SetEventSink([&](World&, const EventPacket& p) {
        if (p.type != GameEvent::Custom) return;
        if (p.user == 1100) ++saw[0];
        else if (p.user == 1200) ++saw[1];
        else if (p.user == 1300) ++saw[2];
        else if (p.user == 1400) ++saw[3];
        else if (p.user == 1511) ++saw[4];
    });

    Entity e = s.Create();
    s.Emplace<Transform2D>(e);
    Animator2D& an = s.Emplace<Animator2D>(e);
    an.clipId = 0x77u; // 初始 walk 段
    g_sh.AttachBehaviour(w, s, e, 11); // AnimFxProbe（表尾 typeId 11）

    w.Step(0.25f); // 帧1：Play(hit,loop:false) + Queue(walk)
    Expect(saw[0] == 1, "animfx: frame1 marker");
    const Animator2D& a1 = s.Get<Animator2D>(e);
    Expect(a1.clipId == 0x88u && a1.loop == 0 && a1.time == 0.0f && a1.playOnStart == 1,
           "animfx: Play writes clip/loop/time/playOnStart");
    Expect(a1.nextClipId == 0x77u && a1.nextLoop == 1 && a1.fadeRemain == -1.0f,
           "animfx: Queue writes pending clip with -1 fade");

    w.Step(0.25f); // 帧2：Pause（队列不受扰）
    Expect(saw[1] == 1, "animfx: frame2 marker");
    const Animator2D& a2 = s.Get<Animator2D>(e);
    Expect(a2.playOnStart == 0 && a2.nextClipId == 0x77u && a2.fadeRemain == -1.0f,
           "animfx: Pause toggles playOnStart only");

    w.Step(0.25f); // 帧3：Resume + Fx.Text/Fx.Bar
    Expect(saw[2] == 1, "animfx: frame3 marker");
    Expect(s.Get<Animator2D>(e).playOnStart == 1, "animfx: Resume restores playOnStart");
    bool fxOk = w.Fx().TextCount() == 1 && w.Fx().BarCount() == 1;
    if (fxOk) {
        const FxText& t = w.Fx().TextAt(0);
        fxOk = std::strcmp(t.text, "12") == 0 && t.x == 10.0f && t.y == 20.0f &&
               t.color == 0xFF5060F0u;
    }
    Expect(fxOk, "animfx: Fx.Text pooled with text/pos/color");
    bool barOk = false;
    for (const FxBar& b : w.Fx().Bars())
        if (b.entity == e.id) barOk = b.frac == 0.5f && b.color == 0xFF30B0F0u && b.width == 40.0f;
    Expect(barOk, "animfx: Fx.Bar keyed by entity with frac/color/width");

    w.Step(0.25f); // 帧4：CrossFade(walk, 0.5)
    Expect(saw[3] == 1, "animfx: frame4 marker");
    const Animator2D& a4 = s.Get<Animator2D>(e);
    Expect(a4.nextClipId == 0x77u && a4.nextLoop == 1 && a4.fadeRemain == 0.5f,
           "animfx: CrossFade overwrites queue with positive fade");

    w.Step(0.25f); // 帧5：IsPlaying/Queued → 1511 + 自毁命令
    w.Step(0.25f); // 应用销毁
    Expect(saw[4] == 1, "animfx: IsPlaying+Queued poll codes 1/1 (1511)");
    Expect(!s.Alive(e), "animfx: probe self-destroyed");

    // 新 World 自清零（EnterPlay 同语义）
    World w2(d);
    Expect(w2.Fx().TextCount() == 0 && w2.Fx().BarCount() == 0,
           "animfx: fresh world fx channel clear");
}

// M6a 批② T3c：动画集按名解析端到端（World.Clips 预登记集 → vtable clipByName →
// C# Anim.Play(name)）。管线不含 AnimatorSystem——只断言解析结果落 Animator2D；
// 三分支：按名命中 / 集内无名 no-op（stderr 红字一次）/ GUID hex 回退（旧脚本
// 兼容）。ClipByNameProbeBehaviour typeId 13 表尾注册。
void TestClipByName() {
    using namespace lemon::ecs;
    auto timeResetFn = (void (*)())GetExport("lemon_time_reset");
    Expect(timeResetFn, "clipname: time reset export resolved");
    timeResetFn();

    WorldDesc d;
    d.threadCount = 1;
    World w(d);
    Scene& s = w.CreateScene("ClipByName");
    w.SetActiveScene(&s);
    w.SetScriptBackend(&g_sh);
    w.Pipeline().AddSystem(std::make_unique<DestroyCommitSystem>());
    w.Pipeline().AddSystem(std::make_unique<CSharpBatchSystem>());
    w.Pipeline().AddSystem(std::make_unique<ScriptEventDispatchSystem>());
    w.Pipeline().ResolveOrder();

    // 预登记集（桥只查名索引，段无需 Add 帧——管线无 AnimatorSystem 不消费帧表）
    w.Clips().RegisterSet(0xABu, {{"idle", 0x11u}, {"hit", 0x22u}});

    int saw[3] = {0, 0, 0};
    w.SetEventSink([&](World&, const EventPacket& p) {
        if (p.type != GameEvent::Custom) return;
        if (p.user == 1301) ++saw[0];
        else if (p.user == 1302) ++saw[1];
        else if (p.user == 1303) ++saw[2];
    });

    Entity e = s.Create();
    s.Emplace<Transform2D>(e);
    Animator2D& an = s.Emplace<Animator2D>(e);
    an.clipId = 0x11u; // 集成员 = 按名作用域锚点
    g_sh.AttachBehaviour(w, s, e, 13); // ClipByNameProbe（表尾 typeId 13）

    w.Step(0.25f); // 帧1：Play("hit") 按名命中
    Expect(saw[0] == 1, "clipname: frame1 marker");
    Expect(s.Get<Animator2D>(e).clipId == 0x22u, "clipname: Play(\"hit\") resolves in set");

    w.Step(0.25f); // 帧2：Play("nope") 集内无名 → no-op
    Expect(saw[1] == 1, "clipname: frame2 marker");
    Expect(s.Get<Animator2D>(e).clipId == 0x22u, "clipname: unknown name leaves clipId");

    w.Step(0.25f); // 帧3："0000000000000033" 非段名 → GUID hex 回退（旧脚本兼容）
    Expect(saw[2] == 1, "clipname: frame3 marker");
    Expect(s.Get<Animator2D>(e).clipId == 0x33u, "clipname: GUID hex fallback (legacy)");
    w.Step(0.25f); // 应用自毁
    Expect(!s.Alive(e), "clipname: probe self-destroyed");
}

// M6a 批② T3d：状态机通道端到端（ControllerTable + ClipTable 集/事件登记 →
// AnimGraphSystem 图评估 → 换段消费 → 帧事件/段末事件）。AnimGraphProbeBehaviour
// typeId 14 表尾注册。覆盖：绑定集按状态名解析、SetParam 条件边、Trigger 边 +
// 消费即清、exitTime 段末回归（非 loop 收尾）、GetParam 回读、AnimFinished/
// AnimFrame 事件入队。
void TestAnimGraphProbe() {
    using namespace lemon::ecs;
    auto timeResetFn = (void (*)())GetExport("lemon_time_reset");
    Expect(timeResetFn, "animgraph: time reset export resolved");
    timeResetFn();

    WorldDesc d;
    d.threadCount = 1;
    World w(d);
    Scene& s = w.CreateScene("AnimGraph");
    w.SetActiveScene(&s);
    w.SetScriptBackend(&g_sh);
    // 注册序 = 执行序：Animator（帧映射+段末）→ CSharpBatch（脚本写参数）→
    // AnimGraph（图评估，读当 tick 参数写段）→ 事件派发 → 销毁提交
    w.Pipeline().AddSystem(std::make_unique<AnimatorSystem>());
    w.Pipeline().AddSystem(std::make_unique<CSharpBatchSystem>());
    w.Pipeline().AddSystem(std::make_unique<AnimGraphSystem>());
    w.Pipeline().AddSystem(std::make_unique<ScriptEventDispatchSystem>());
    w.Pipeline().AddSystem(std::make_unique<DestroyCommitSystem>());
    w.Pipeline().ResolveOrder();

    // 集绑定：状态名 → clip（Attack 非 loop 2 帧 @10fps = 0.2s；帧 1 打点事件 id 9）
    w.Clips().Add(0x11u, {1, 1, 1, 1}, 10.0f, true);
    w.Clips().Add(0x22u, {2, 2, 2, 2}, 10.0f, true);
    w.Clips().Add(0x33u, {3, 3}, 10.0f, false, {{1, 9}});
    w.Clips().RegisterSet(0xABu, {{"Idle", 0x11u}, {"Walk", 0x22u}, {"Attack", 0x33u}});
    // controller 0x77：Idle↔Walk（speed 条件）、Walk→Attack（trigger）、
    // Attack→Idle（exitTime）
    ControllerDef def;
    def.states = {"Idle", "Walk", "Attack"};
    def.params = {{"speed", AnimParamKind::Float, 0.0f},
                  {"atk", AnimParamKind::Trigger, 0.0f}};
    def.transitions.push_back({0, 1, false, {{0, AnimCondOp::Gt, 0.1f}}});
    def.transitions.push_back({1, 0, false, {{0, AnimCondOp::Le, 0.1f}}});
    def.transitions.push_back({1, 2, false, {{1, AnimCondOp::Trigger, 0.0f}}});
    def.transitions.push_back({2, 0, true, {}}); // exitTime（聚合初始化：from,to,exitTime,conds）
    w.Controllers().Add(0x77u, std::move(def));

    int saw[8] = {0};
    int animFinished = 0, animFrame9 = 0;
    w.SetEventSink([&](World&, const EventPacket& p) {
        if (p.type == GameEvent::Custom) {
            int idx = p.user == 1401 ? 0 : p.user == 1402 ? 1 : p.user == 1403 ? 2
                     : p.user == 1404   ? 3 : p.user == 1405 ? 4 : p.user == 1406 ? 5
                                                                : p.user == 1420 ? 6 : 7;
            ++saw[idx];
        } else if (p.type == GameEvent::AnimFinished && p.userArg == 0x33u) {
            ++animFinished;
        } else if (p.type == GameEvent::AnimFrame && p.user == 9 && p.userArg == 0x33u) {
            ++animFrame9;
        }
    });

    Entity e = s.Create();
    s.Emplace<Transform2D>(e);
    Animator2D& an = s.Emplace<Animator2D>(e); // clipId=0：图初始化 tick 应 Play(entry Idle)
    (void)an;
    AnimGraph& gr = s.Emplace<AnimGraph>(e);
    gr.controllerGuid = 0x77u;
    gr.setGuid = 0xABu;
    s.Emplace<AnimParams>(e);
    g_sh.AttachBehaviour(w, s, e, 14); // AnimGraphProbe（表尾 typeId 14）

    const float dt = 1.0f / 60.0f;
    for (int i = 0; i < 22; ++i) w.Step(dt);
    Expect(saw[0] == 1 && saw[1] == 1 && saw[3] == 1 && saw[4] == 1, "animgraph: 帧标记齐");
    Expect(saw[2] == 1, "animgraph: SetParam(speed) → 条件边切 Walk");
    Expect(saw[5] == 1, "animgraph: GetParam 回读");
    Expect(saw[6] == 1, "animgraph: exitTime 段末回 Idle");
    Expect(saw[7] == 0, "animgraph: 无失败标记");
    Expect(animFinished >= 1, "animgraph: AnimFinished(Attack) 事件");
    Expect(animFrame9 >= 1, "animgraph: AnimFrame(帧1 打点) 事件");
    if (s.Alive(e)) { // 帧 20 自毁，销毁提交下一 tick 生效
        w.Step(dt);
        Expect(!s.Alive(e), "animgraph: probe self-destroyed");
    }
}

// A 档补间通道端到端（2026-09-28 用户插入项；vtable 尾加 4 项 → C# Lemon.Tween
// 全 API 面 → TweenSystem 推进 → 组件字段精确值/事件/所有权语义）。管线：
// 脚本批 → Tween（脚本后跑 = 存活补间拥有字段、当 tick 首写）→ 事件派发 →
// 销毁提交。TweenProbeBehaviour typeId 15 表尾注册。
void TestTweenSdk() {
    using namespace lemon::ecs;
    auto timeResetFn = (void (*)())GetExport("lemon_time_reset");
    Expect(timeResetFn, "tween: time reset export resolved");
    timeResetFn(); // 探针按 FrameCount 分段——本测试 = 新一局

    WorldDesc d;
    d.threadCount = 1;
    World w(d);
    Scene& s = w.CreateScene("Tween");
    w.SetActiveScene(&s);
    w.SetScriptBackend(&g_sh);
    // 注册序 = 执行序：脚本批 → 补间推进 → 事件派发（完成事件当帧可见）→ 销毁提交
    w.Pipeline().AddSystem(std::make_unique<CSharpBatchSystem>());
    w.Pipeline().AddSystem(std::make_unique<TweenSystem>());
    w.Pipeline().AddSystem(std::make_unique<ScriptEventDispatchSystem>());
    w.Pipeline().AddSystem(std::make_unique<DestroyCommitSystem>());
    w.Pipeline().ResolveOrder();

    // 标记位：1601=建 1611=拒建双探 1701=句柄活 1620=Yoko 起 1631=Kill(pos)
    // 1640=冲突起 1651=轮询(亡/活) 1661=KillAll
    int saw[8] = {0};
    int finished = 0;
    w.SetEventSink([&](World&, const EventPacket& p) {
        if (p.type == GameEvent::Custom) {
            int idx = p.user == 1601 ? 0 : p.user == 1611 ? 1 : p.user == 1701 ? 2
                     : p.user == 1620   ? 3 : p.user == 1631 ? 4 : p.user == 1640 ? 5
                     : p.user == 1651   ? 6 : p.user == 1661 ? 7 : -1;
            if (idx >= 0) ++saw[idx];
        } else if (p.type == GameEvent::TweenFinished) {
            ++finished;
        }
    });

    Entity e = s.Create();
    s.Emplace<Transform2D>(e); // scale=(1,1)、pos=(0,0) 默认
    SpriteRenderer& sr = s.Emplace<SpriteRenderer>(e);
    sr.colorRGBA = 0xFF0000FFu; // 不透明红（r=255,g=b=0）
    g_sh.AttachBehaviour(w, s, e, 15); // TweenProbe（表尾 typeId 15）

    const float dt = 0.25f;
    w.Step(dt); // 帧1：建 Scale(1→3,1s,Linear) + 拒建双探 + 存活轮询
    Expect(saw[0] == 1 && saw[1] == 1 && saw[2] == 1, "tween: 帧标记齐（建/拒建双探/活）");
    {
        const Transform2D& t = s.Get<Transform2D>(e);
        Expect(t.scale.x == 1.5f && t.scale.y == 1.5f, "tween: 当 tick 首写（0.25 → 1.5）");
    }
    Expect(w.Tweens().Count() == 1, "tween: 单条存活");

    w.Step(dt); // 帧2-3：线性中值
    Expect(s.Get<Transform2D>(e).scale.x == 2.0f, "tween: 线性 0.5 → 2.0");
    w.Step(dt);
    Expect(s.Get<Transform2D>(e).scale.x == 2.5f, "tween: 线性 0.75 → 2.5");

    w.Step(dt); // 帧4：Once 完成
    {
        const Transform2D& t = s.Get<Transform2D>(e);
        Expect(t.scale.x == 3.0f && t.scale.y == 3.0f, "tween: 终值精确 3.0");
    }
    Expect(finished == 1, "tween: TweenFinished 恰一次");
    Expect(w.Tweens().Count() == 0, "tween: 完成即移除");

    w.Step(dt); // 帧5：Yoyo pos 0→10（1s）
    Expect(saw[3] == 1, "tween: Yoyo 起标");
    Expect(s.Get<Transform2D>(e).pos.x == 2.5f, "tween: Yoyo 上行 0.25 → 2.5");
    w.Step(dt);
    w.Step(dt);
    Expect(s.Get<Transform2D>(e).pos.x == 7.5f, "tween: Yoyo 上行 0.75 → 7.5");
    w.Step(dt); // 帧8：峰
    Expect(s.Get<Transform2D>(e).pos.x == 10.0f, "tween: Yoyo 峰值 10");

    w.Step(dt); // 帧9：Kill(pos) + Color(白,0.5s)
    Expect(saw[4] == 1, "tween: Kill(pos) 移除 1");
    {
        const Transform2D& t = s.Get<Transform2D>(e);
        Expect(t.pos.x == 10.0f && t.pos.y == 0.0f, "tween: Kill 后字段冻结峰值");
    }
    Expect(s.Get<SpriteRenderer>(e).colorRGBA == 0xFF8080FFu, "tween: 颜色字节中值 0x80");
    Expect(w.Tweens().Count() == 1, "tween: 颜色补间存活");

    w.Step(dt); // 帧10：颜色完成
    Expect(s.Get<SpriteRenderer>(e).colorRGBA == 0xFFFFFFFFu, "tween: 颜色终值白");
    Expect(finished == 2, "tween: 第二次完成事件");
    Expect(w.Tweens().Count() == 0, "tween: 表清空");

    w.Step(dt); // 帧11：字段所有权（脚本写 99 从下帧起，本帧 tween 独写）
    Expect(saw[5] == 1, "tween: 冲突起标");
    Expect(s.Get<Transform2D>(e).pos.x == 15.0f, "tween: 脚本后建 tween 当帧生效（10→30 的 0.25 → 15）");

    w.Step(dt); // 帧12：轮询 + KillAll + 自毁
    Expect(saw[6] == 1, "tween: 轮询（scale 句柄亡/pos 句柄活）");
    Expect(saw[7] == 1, "tween: KillAll 移除 1");
    Expect(s.Get<Transform2D>(e).pos.x == 99.0f, "tween: Kill 归还字段（脚本 99 站住）");
    Expect(w.Tweens().Count() == 0, "tween: KillAll 后清空");
    w.Step(dt); // 应用销毁
    Expect(!s.Alive(e), "tween: probe self-destroyed");

    // 新 World 自清零（EnterPlay 同语义）
    World w2(d);
    Expect(w2.Tweens().Count() == 0, "tween: fresh world table clear");
}

// M6a 批② T2：配置表通道端到端（TableStore 登记 → vtable 尾加 3 项 →
// C# Lemon.Table 全 API 面 + 无表降级 + 越界格 + 容错数值 → RtUi 回读断言；
// TableProbeBehaviour typeId 12 表尾注册）
void TestTableChannel() {
    using namespace lemon::ecs;
    auto opsSubmit = (void (*)(unsigned char, unsigned char, uint64_t))GetExport("lemon_ops_submit");
    auto timeResetFn = (void (*)())GetExport("lemon_time_reset");
    Expect(opsSubmit && timeResetFn, "ops/time exports resolved");
    timeResetFn(); // 探针按 FrameCount 分段——本测试 = 新一局

    WorldDesc d;
    d.threadCount = 1;
    World w(d);
    Scene& s = w.CreateScene("Tbl");
    w.SetActiveScene(&s);
    w.SetScriptBackend(&g_sh);
    w.Pipeline().AddSystem(std::make_unique<DestroyCommitSystem>());
    w.Pipeline().AddSystem(std::make_unique<CSharpBatchSystem>());
    w.Pipeline().AddSystem(std::make_unique<ScriptEventDispatchSystem>());
    w.Pipeline().ResolveOrder();

    // ---- 引擎侧单元：TableStore 登记/查格/越界/坏参拒绝 ----
    {
        TableStore ts;
        Expect(!ts.Add(0, {{"x"}}), "id 0 rejected");
        Expect(!ts.Add(7, {}), "empty grid rejected");
        Expect(ts.Add(0x0000beefu, {{"id", "label", "count", "rate"},
                                     {"shoot", "直射", "12", "0.5"},
                                     {"pierce", "穿透", "7", "bad"}}),
               "table registered");
        Expect(ts.Count() == 1 && ts.Find(0x0000beefu) && !ts.Find(1), "find hit/miss");
        const std::string* c = ts.Cell(0x0000beefu, 1, 1);
        Expect(c && *c == "直射", "cell utf8 readback");
        Expect(!ts.Cell(0x0000beefu, 9, 9) && !ts.Cell(0x0000beefu, 0, 4) &&
                   !ts.Cell(0x1234u, 0, 0),
               "oob/unknown-table cell null");
        ts.Clear();
        Expect(ts.Count() == 0 && !ts.Find(0x0000beefu), "clear");
    }

    // 预登记（guid "123456780000beef" 低 32 = 0x0000beef；编辑器 BuildPlayTableCache 同构）
    Expect(w.Tables().Add(0x0000beefu, {{"id", "label", "count", "rate"},
                                        {"shoot", "直射", "12", "0.5"},
                                        {"pierce", "穿透", "7", "bad"}}),
           "world table registered");

    int sawDone = 0;
    w.SetEventSink([&](World&, const EventPacket& p) {
        if (p.type == GameEvent::Custom && p.user == 1260) ++sawDone;
    });

    opsSubmit(0, 0, 0x800000000000000Cull); // Create + Attach TableProbeBehaviour（typeId 12 表尾）
    opsSubmit(4, 12, 0x800000000000000Cull);

    w.Step(0.25f); // 帧1：全 API 读 + Ui.Set("tbl") + Custom 1260 + 自毁命令
    w.Step(0.25f); // 应用销毁
    Expect(sawDone == 1, "table: probe done (Custom 1260)");

    bool slotOk = w.RtUi().Count() == 1;
    if (slotOk) {
        const RtUiSlot& slot = w.RtUi().At(0);
        std::printf("script-tests: [diag] table slot: key='%s' text='%s'\n", slot.key,
                    slot.text);
        slotOk = std::strcmp(slot.key, "tbl") == 0 &&
                 std::strcmp(slot.text, "3x4 直射 i12 f5 b0 m-1 FFalse TTrue") == 0;
    }
    Expect(slotOk, "table: Rows/Cols/Str/Int/Float/Has + degrade + oob roundtrip");

    // 新 World 自清零（EnterPlay 同语义——编辑器每局新建 playWorld）
    {
        World w2(d);
        Expect(w2.Tables().Count() == 0, "table: fresh world store clear");
    }

    g_sh.ResetPlayDomain(); // 收尾自清（同上）
}

// M6a 批② T5：存档分档端到端（World 三通道 → vtable 尾加 Ex 3 项 → C#
// Lemon.Save × Chan 全 API 面 + 档间隔离 + 越界 chan 回落 slot → 原生通道对拍；
// SaveChanProbeBehaviour typeId 16 表尾注册）
void TestSaveChannels() {
    using namespace lemon::ecs;
    auto opsSubmit = (void (*)(unsigned char, unsigned char, uint64_t))GetExport("lemon_ops_submit");
    auto timeResetFn = (void (*)())GetExport("lemon_time_reset");
    Expect(opsSubmit && timeResetFn, "ops/time exports resolved");
    timeResetFn(); // 探针按 FrameCount 分段——本测试 = 新一局

    WorldDesc d;
    d.threadCount = 1;
    World w(d);
    Scene& s = w.CreateScene("Sav");
    w.SetActiveScene(&s);
    w.SetScriptBackend(&g_sh);
    w.Pipeline().AddSystem(std::make_unique<DestroyCommitSystem>());
    w.Pipeline().AddSystem(std::make_unique<CSharpBatchSystem>());
    w.Pipeline().AddSystem(std::make_unique<ScriptEventDispatchSystem>());
    w.Pipeline().ResolveOrder();

    // ---- 引擎侧单元：三通道隔离 + Saves() 默认 = slot + 越界钳位 + 新 World 自清 ----
    {
        Expect(&w.Saves() == &w.Saves(kSaveSlot), "saves() default param = slot");
        w.Saves(kSaveSlot).Set("a", "1", 1);
        w.Saves(kSaveSettings).Set("a", "2", 1);
        w.Saves(kSaveMeta).Set("a", "3", 1);
        char buf[2] = {};
        Expect(w.Saves(kSaveSlot).Get("a", buf, 1) == 1 && buf[0] == '1', "slot isolation");
        Expect(w.Saves(kSaveSettings).Get("a", buf, 1) == 1 && buf[0] == '2',
               "settings isolation");
        Expect(w.Saves(kSaveMeta).Get("a", buf, 1) == 1 && buf[0] == '3', "meta isolation");
        Expect(&w.Saves(99) == &w.Saves(kSaveSlot), "oob ch clamps to slot");
        World w2(d); // 新 World 三通道全空（EnterPlay 每局新建 playWorld 同语义）
        Expect(w2.Saves(kSaveSlot).Count() == 0 && w2.Saves(kSaveSettings).Count() == 0 &&
                   w2.Saves(kSaveMeta).Count() == 0,
               "fresh world three channels clear");
    }

    int sawMark = 0, sawOk = 0;
    w.SetEventSink([&](World&, const EventPacket& p) {
        if (p.type != GameEvent::Custom) return;
        if (p.user == 1281) ++sawMark;
        else if (p.user == 1291) ++sawOk; // 1290+ok：C# 侧九项回读全过才回报 1291
    });

    opsSubmit(0, 0, 0x8000000000000010ull); // Create + Attach SaveChanProbeBehaviour（typeId 16 表尾）
    opsSubmit(4, 16, 0x8000000000000010ull);

    w.Step(0.25f); // 帧1：三档写入（settings 版本键/meta 收集键约定）+ 越界回落
    w.Step(0.25f); // 帧2：档间隔离回读 + Ui.Set("svch")
    w.Step(0.25f); // 帧3：自毁
    w.Step(0.25f); // 应用销毁
    Expect(sawMark == 1 && sawOk == 1, "save-chan: probe marks (1281/1291)");

    // C++ 侧原生通道对拍（探针写入落点——Ex vtable 真到了 World 三通道）
    {
        char buf[16] = {};
        auto str = [&](uint8_t ch, const char* k) {
            std::string out;
            const int len = w.Saves(ch).GetLen(k);
            if (len > 0 && len < 15 && w.Saves(ch).Get(k, buf, 14) == len)
                out.assign(buf, (size_t)len);
            return out;
        };
        Expect(str(kSaveSettings, "version") == "1" && str(kSaveSettings, "volume") == "0.8",
               "save-chan: settings versioned kv lands");
        Expect(str(kSaveMeta, "vs.best") == "77" && str(kSaveMeta, "col.sword.count") == "3" &&
                   str(kSaveMeta, "col.sword.state") == "owned",
               "save-chan: meta collection keys land");
        Expect(str(kSaveSlot, "run.kills") == "5" && str(kSaveSlot, "bad.chan") == "x",
               "save-chan: slot default param + oob fallback");
        Expect(w.Saves(kSaveSlot).GetLen("volume") < 0 &&
                   w.Saves(kSaveMeta).GetLen("run.kills") < 0,
               "save-chan: no cross-channel leak");
    }
    bool uiOk = w.RtUi().Count() == 1;
    if (uiOk) {
        const RtUiSlot& slot = w.RtUi().At(0);
        uiOk = std::strcmp(slot.key, "svch") == 0 && std::strcmp(slot.text, "ok") == 0;
    }
    Expect(uiOk, "save-chan: C# readback verdict via RtUi");

    g_sh.ResetPlayDomain(); // 收尾自清（同上）
}

} // namespace

// ---------------------------------------------------------------------------
// M6b 批③c：Lemon.UI 桥面双测（线格式字节对拍 + 事件反向直灌）——不依赖 RmlUi
// （引擎文档应用面由 --smoke-uirml 全链覆盖；本测钉死 SDK 编码 ↔ UiBridge 契约）。
// ---------------------------------------------------------------------------
static std::vector<lemon::ui::UiOpC> s_uiCapOps;
static std::vector<char> s_uiCapArena;
static lemon::ui::UiEventC s_uiInjectEvent;
static bool s_uiInjectPending = false;

static void UiCapApplyOps(const lemon::ui::UiOpC* ops, uint32_t n, const char* arena,
                          uint32_t bytes) {
    s_uiCapOps.assign(ops, ops + n);
    s_uiCapArena.assign(arena, arena + bytes);
}
static uint32_t UiCapDrainEvents(lemon::ui::UiEventC* dst, uint32_t cap) {
    if (!s_uiInjectPending || cap == 0) return 0;
    s_uiInjectPending = false;
    dst[0] = s_uiInjectEvent;
    return 1;
}

static const char* CapStr(uint32_t off) {
    return off < s_uiCapArena.size() ? s_uiCapArena.data() + off : "";
}

void TestUiSdk() {
    using namespace lemon::ecs;
    auto timeResetFn = (void (*)())GetExport("lemon_time_reset");
    Expect(timeResetFn, "ui: time reset export resolved");
    timeResetFn(); // 探针按 FrameCount==1 播种——本测试 = 新一局

    // 桥钩子：ops 捕获 + 事件注入（一次 Click cards/opt0）
    lemon::scripting::UiHooks hooks{UiCapApplyOps, UiCapDrainEvents};
    lemon::scripting::SetUiHooks(hooks);
    s_uiInjectEvent = {};
    s_uiInjectEvent.kind = (uint8_t)lemon::ui::UiEventKind::Click;
    std::snprintf(s_uiInjectEvent.doc, sizeof(s_uiInjectEvent.doc), "Assets/UI/uirml.rml");
    std::snprintf(s_uiInjectEvent.key, sizeof(s_uiInjectEvent.key), "cards/opt0");
    std::snprintf(s_uiInjectEvent.ev, sizeof(s_uiInjectEvent.ev), "pick");
    s_uiInjectPending = true;

    WorldDesc d;
    d.threadCount = 1;
    World w(d);
    Scene& s = w.CreateScene("UiSdk");
    w.SetActiveScene(&s);
    w.SetScriptBackend(&g_sh);
    w.Pipeline().AddSystem(std::make_unique<CSharpBatchSystem>());
    w.Pipeline().AddSystem(std::make_unique<ScriptEventDispatchSystem>());
    w.Pipeline().AddSystem(std::make_unique<DestroyCommitSystem>());
    w.Pipeline().ResolveOrder();

    Entity e = s.Create();
    g_sh.AttachBehaviour(w, s, e, 17); // UiProbe（表尾 typeId 17）

    s_uiCapOps.clear();
    w.Step(0.25f);

    // ---- ① ops 线格式对拍：UiRefill() = Show+SetText+SetItems+SetClass（主文档）+
    //      Show+SetText（批③d 前置通道 B 动态屏 dyn——Show 落空兜底 + 同批可达） ----
    Expect(s_uiCapOps.size() == 6, "ui: Apply 产出 6 op（主文档 4 + dyn 通道 B 2）");
    if (s_uiCapOps.size() == 6) {
        const auto& opShow = s_uiCapOps[0];
        Expect(opShow.type == (uint8_t)lemon::ui::UiOpType::Show && opShow.strCount == 1,
               "ui: op0 = Show(doc)");
        Expect(std::strcmp(CapStr(opShow.s0), "Assets/UI/uirml.rml") == 0,
               "ui: op0 doc 字符串命中（NUL 终止契约）");
        const auto& opText = s_uiCapOps[1];
        Expect(opText.type == (uint8_t)lemon::ui::UiOpType::SetText && opText.strCount == 3,
               "ui: op1 = SetText(doc,key,text)");
        Expect(std::strcmp(CapStr(opText.s1), "title") == 0 &&
                   std::strcmp(CapStr(opText.s2), "升级！三选一") == 0,
               "ui: op1 key/text 字节命中（UTF-8 零乱码）");
        const auto& opItems = s_uiCapOps[2];
        Expect(opItems.type == (uint8_t)lemon::ui::UiOpType::SetItems && opItems.i0 == 2,
               "ui: op2 = SetItems(2 行)");
        Expect(std::strcmp(CapStr(opItems.s1), "cards") == 0 &&
                   std::strcmp(CapStr(opItems.s2), "card") == 0,
               "ui: op2 容器/模板名命中");
        // 行块解码（引擎 UiSubsystem::SetItems 同款口径）：每行 = key + 字段。
        // 2026-10-03 收紧为字节精确对拍：key/值均以多字节字符结尾（"选项乙"9B、
        // "移速加成"/"磁力提升"12B）——批① #22/#60 的编码回退若误剪完整码点或留
        // 悬空导引字节，长度即不等，此处必红（原仅查 valLen 范围，坏串漏网）
        const uint8_t* rp = (const uint8_t*)CapStr(opItems.s3);
        const struct { const char* key; const char* val; } rows[2] = {
            {"opt0", "移速加成"}, {"选项乙", "磁力提升"}};
        for (int r = 0; r < 2; ++r) {
            const uint8_t keyLen = *rp++;
            const size_t keyBytes = std::strlen(rows[r].key);
            Expect(keyLen == keyBytes && std::memcmp(rp, rows[r].key, keyBytes) == 0,
                   "ui: 行 key 字节精确命中（含 CJK key）");
            rp += keyLen;
            uint16_t fieldCount;
            std::memcpy(&fieldCount, rp, 2);
            rp += 2;
            Expect(fieldCount == 1, "ui: 行字段数 = 1");
            const uint8_t nameLen = *rp++;
            Expect(nameLen == 5 && std::memcmp(rp, "label", 5) == 0, "ui: 字段名命中");
            rp += nameLen;
            uint16_t valLen;
            std::memcpy(&valLen, rp, 2);
            rp += 2;
            const size_t valBytes = std::strlen(rows[r].val);
            Expect(valLen == valBytes && std::memcmp(rp, rows[r].val, valBytes) == 0,
                   "ui: 字段值 CJK 字节精确（不得截尾/产悬空导引字节）");
            rp += valLen;
        }
        Expect((size_t)(rp - (const uint8_t*)s_uiCapArena.data()) <= s_uiCapArena.size(),
               "ui: 行块解码未越界（自描述长度闭合）");
        const auto& opCls = s_uiCapOps[3];
        Expect(opCls.type == (uint8_t)lemon::ui::UiOpType::SetClass &&
                   (opCls.flags & 1) != 0 && opCls.strCount == 3,
               "ui: op3 = SetClass(add)");
        Expect(std::strcmp(CapStr(opCls.s1), "cards/opt0") == 0 &&
                   std::strcmp(CapStr(opCls.s2), "rare") == 0,
               "ui: op3 路径 key/class 命中");
        // 批③d 前置：通道 B 动态屏两条（未装载文档的 Show = 落空兜底面；SetText
        // 同批可达 = 装载在 Show op 处完成的顺序契约）
        const auto& opShowDyn = s_uiCapOps[4];
        Expect(opShowDyn.type == (uint8_t)lemon::ui::UiOpType::Show && opShowDyn.strCount == 1,
               "ui: op4 = Show(dyn)");
        Expect(std::strcmp(CapStr(opShowDyn.s0), "Assets/UI/dyn.rml") == 0,
               "ui: op4 动态屏文档名命中");
        const auto& opTextDyn = s_uiCapOps[5];
        Expect(opTextDyn.type == (uint8_t)lemon::ui::UiOpType::SetText && opTextDyn.strCount == 3,
               "ui: op5 = SetText(dyn,dyntitle)");
        Expect(std::strcmp(CapStr(opTextDyn.s1), "dyntitle") == 0,
               "ui: op5 动态屏 key 命中");
    }

    // ---- ② 事件反向直灌：drainEvents → lemon_ui_events_dispatch → 静态订阅 →
    //      计数经 Lemon.Ui.Set 落 World.RtUi（同帧 #16 窗口内 native 可用） ----
    const RtUiChannel& rtui = w.RtUi();
    bool sawUiev = false;
    char uievText[48] = {};
    for (uint32_t i = 0; i < rtui.Count(); ++i)
        if (std::strcmp(rtui.At(i).key, "uiev") == 0) {
            sawUiev = true;
            std::snprintf(uievText, sizeof uievText, "%s", rtui.At(i).text);
        }
    Expect(sawUiev && std::strcmp(uievText, "c1r0") == 0,
           "ui: 注入 Click → C# 订阅回执 → RtUi uiev=c1r0（全链反向）");

    // ---- ③ 批③c-2 staging 同值去重：帧 2 UiDedupProbe 三写（A/同A/B）→
    //      恰 2 op（同值跳 / 异值过）。DocumentReloaded 复位由 --smoke-uirml
    //      终帧 title 断言端到端覆盖（fixture 静态值 ≠ 重灌值，误吞必红）----
    w.Step(0.25f);
    Expect(s_uiCapOps.size() == 2, "ui-dedup: 三写恰 2 op（同值跳过/异值通过）");
    if (s_uiCapOps.size() == 2) {
        const auto& d0 = s_uiCapOps[0];
        Expect(d0.type == (uint8_t)lemon::ui::UiOpType::SetText, "ui-dedup: op0 = SetText");
        Expect(std::strcmp(CapStr(d0.s1), "body") == 0 &&
                   std::strcmp(CapStr(d0.s2), "dedupA") == 0,
               "ui-dedup: op0 = 首写 A（缓存空必过）");
        const auto& d1 = s_uiCapOps[1];
        Expect(d1.type == (uint8_t)lemon::ui::UiOpType::SetText &&
                   std::strcmp(CapStr(d1.s1), "body") == 0 &&
                   std::strcmp(CapStr(d1.s2), "dedupB") == 0,
               "ui-dedup: op1 = 异值 B（同值 A 未入列）");
    }

    // ---- ④ 批③c-2 DocumentReloaded 复位契约：注入重装载事件 → OnUiEvent →
    //      UiRefill 同值重写——缓存已清 = 6 op 全发（含被去重过的同值 SetText）；
    //      缓存未清 = 只发 3（Show×2 + SetItems——永不去重的三条）→ 此处必红 ----
    s_uiInjectEvent = {};
    s_uiInjectEvent.kind = (uint8_t)lemon::ui::UiEventKind::DocumentReloaded;
    std::snprintf(s_uiInjectEvent.doc, sizeof s_uiInjectEvent.doc, "Assets/UI/uirml.rml");
    s_uiInjectPending = true;
    // 派发（#16 插 CSharpBatch 后）与本轮 ops 拉取点的先后：事件驱动的重灌入
    // ready 晚于当轮拉取 → 再步进一步取到（引擎侧事件驱动 UI 写 = 下一帧可见，
    // 与既有行为一致，非去重引入）
    w.Step(0.25f);
    w.Step(0.25f);
    Expect(s_uiCapOps.size() == 6, "ui-dedup: 重装载后重灌 6 op 全发（缓存复位）");
    if (s_uiCapOps.size() == 6) {
        const auto& r1 = s_uiCapOps[1];
        Expect(r1.type == (uint8_t)lemon::ui::UiOpType::SetText &&
                   std::strcmp(CapStr(r1.s1), "title") == 0 &&
                   std::strcmp(CapStr(r1.s2), "升级！三选一") == 0,
               "ui-dedup: 同值 SetText 在重装载后重新入列（复位契约核心位）");
    }

    lemon::scripting::SetUiHooks({nullptr, nullptr}); // 后续测试零扰动
    std::printf("script-tests: TestUiSdk OK（ops 6 条字节对拍（含通道 B dyn 2 条） + 事件反向 c1r0 + 去重 3写2过 + 重装载复位 6 op）\n");
}

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
    lemonRngRange = (uint32_t (*)(uint32_t, uint32_t))GetExport("lemon_rng_range");
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
    TestApiHandshake();
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
    TestTimeScaleAndUiChannel();
    TestWaveStartToUi();
    TestSaveChannelAndUiCards();
    TestPlayDomainReset();
    TestAwakeWindowAndDoubleAdd();
    TestSubscribeAutoUnsubscribe();
    TestCppDestroyNotify();
    TestSdkDualRoute();
    TestAnimFxSdk();
    TestTableChannel(); // M6a 批② T2：Lemon.Table 配置表通道端到端
    TestClipByName();   // M6a 批② T3c：动画集按名解析通道端到端
    TestAnimGraphProbe(); // M6a 批② T3d：状态机通道（绑定/参数/trigger/exitTime/帧事件）
    TestTweenSdk();     // A 档补间（2026-09-28 用户插入项）：Lemon.Tween 通道端到端
    TestSaveChannels(); // M6a 批② T5：Lemon.Save × Chan 分档通道端到端
    TestUiSdk();        // M6b 批③c：Lemon.UI 线格式对拍 + 事件反向直灌
    TestAudioSdk();     // M6c 批②：Lemon.Audio 全 API 面 + 哈希免疫反例
    TestFxSdk();        // M7c 批①：Lemon.Fx 表现升级面 + 哈希免疫反例
    TestSceneSdk();     // M7c 批⑦：SceneManager 全链（四跳/事件序/DDOL/红字拒）
    TestSceneAsyncSdk(); // M7c 批⑧：LoadSceneAsync 全链（契约/门控/await/取代取消）

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
