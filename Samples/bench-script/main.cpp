// Lemon M3 终验收官程序 bench-script（08 §3 验收场，ADR-010 修订版）
// C# 驱动"环绕弹幕"：档② BoomerangSystem 推 N 弹绕玩家轨道 + 档① PlayerBehaviour
// 走位 + 毒脚本 PoisonSystem（异常隔离活体）。无渲染。
// 验收判据：
//   1) 整步 ≤ 8ms；C# 批量净时间 ≤ C++ 等效 × 1.5（--cpp-compare）
//   2) 确定性回放双档 PASS（--record/--replay，StateHash 逐帧）
//   3) 毒脚本不崩引擎、60 帧自动禁用（stderr 红字计数）
//   4) 示例脚本托管分配 = 0（GC 采样窗 16 帧逐窗核对，硬 0 无豁免。可行性前提：
//      main 起点关 tiered compilation——分层记账会注入 ~8.2KB×N 次分配（30k 实测
//      3 次/300 帧；TC=0 时 0/10）；关分层不降稳态码质（Tier1 全优化从头编译，
//      120 帧预热后与分层稳态等价））
//   5) C# 批量净时 ≤ C++ 等效 × 1.5（--cpp-compare；净时只统计测量段——预热段
//      Tier0 慢帧会污染均值，M3-7 实测噪声源）
// 用法：lemon-bench-script [--n N] [--frames N] [--threads N] [--seed S]
//                          [--stats] [--cpp-compare] [--record F] [--replay F]
// 回放格式：LREPLAY1 文本（无输入行——场景确定性驱动，仅逐帧哈希行，可 diff）。
#include <chrono>
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "Components/BehaviorComponents.h"
#include "Components/CoreComponents.h"
#include "Core/Log.h"
#include "Core/Math.h"
#include "ECS/ComponentRegistry.h"
#include "ECS/Scene.h"
#include "ECS/StateHash.h"
#include "ECS/World.h"
#include "Scripting/ScriptHost.h"
#include "Systems/Systems.h"

using namespace lemon;
using namespace lemon::ecs;

namespace {

constexpr float kDt = 0.25f; // 与 script-tests/验收口径一致（固定步，非 60Hz）
constexpr char kMagic[] = "LREPLAY1";

struct Config {
    uint32_t bullets = 5000;
    uint32_t frames = 3600;
    int threads = 0;
    uint64_t seed = 20260919ull;
    bool stats = false;
    bool cppCompare = false;
    const char* recordFile = nullptr;
    const char* replayFile = nullptr;
};

} // namespace

int main(int argc, char** argv) {
    // GC 验收确定性：先于 CoreCLR 初始化关分层编译（判据 4 前提，见文件头注）。
    // 实测矩阵：{默认分层, TC=0} × {GetExport 每调, 缓存} = {9/10 FAIL, 1/10, 10/10 PASS}
    // ——分层记账与 GetExport 分配是两个独立来源，后者已修（ScriptHost 缓存指针）。
    ::setenv("DOTNET_TieredCompilation", "0", 1);
    Config cfg;
    for (int i = 1; i < argc; ++i) {
        auto next = [&]() -> const char* { return (i + 1 < argc) ? argv[++i] : ""; };
        if (!std::strcmp(argv[i], "--n")) cfg.bullets = (uint32_t)std::atoi(next());
        else if (!std::strcmp(argv[i], "--frames")) cfg.frames = (uint32_t)std::max(1, std::atoi(next()));
        else if (!std::strcmp(argv[i], "--threads")) cfg.threads = std::atoi(next());
        else if (!std::strcmp(argv[i], "--seed")) cfg.seed = std::strtoull(next(), nullptr, 10);
        else if (!std::strcmp(argv[i], "--stats")) cfg.stats = true;
        else if (!std::strcmp(argv[i], "--cpp-compare")) cfg.cppCompare = true;
        else if (!std::strcmp(argv[i], "--record")) cfg.recordFile = next();
        else if (!std::strcmp(argv[i], "--replay")) cfg.replayFile = next();
    }

    scripting::ScriptHost host;
    if (!host.Initialize(nullptr, LEMON_SCRIPT_DIR "/Lemon.Entry.runtimeconfig.json",
                         LEMON_SCRIPT_DIR "/Lemon.Entry.dll")) {
        return 1;
    }
    if (!host.LoadUserAssembly(LEMON_SCRIPT_DIR_BENCH "/BenchScript.dll")) {
        LEMON_WARN("script host init failed (dotnet 构建产物缺失？cmake --build lemon-bench-script)");
        return 1;
    }

    const bool replaying = cfg.replayFile != nullptr;
    std::FILE* recFp = nullptr;
    std::FILE* repFp = nullptr;
    if (cfg.recordFile) {
        recFp = std::fopen(cfg.recordFile, "w");
        if (!recFp) return 1;
        std::fprintf(recFp, "%s\n", kMagic);
        std::fprintf(recFp, "seed %llu threads %d\n", (unsigned long long)cfg.seed, cfg.threads);
    }
    if (replaying) {
        repFp = std::fopen(cfg.replayFile, "r");
        if (!repFp) return 1;
        char magic[16] = {};
        uint64_t seed = 0;
        int threads = 0;
        if (std::fscanf(repFp, "%15s\n", magic) != 1 || std::strcmp(magic, kMagic) != 0 ||
            std::fscanf(repFp, "seed %llu threads %d\n", (unsigned long long*)&seed, &threads) != 2) {
            LEMON_WARN("replay header malformed");
            return 1;
        }
        cfg.seed = seed;
    }

    WorldDesc wd;
    wd.seed = cfg.seed;
    wd.threadCount = cfg.threads;
    World world(wd);
    world.SetScriptBackend(&host);
    // 验收管线（判据针对脚本净成本）：销毁提交 + #14 + #15（结构命令无消费者时为空跑）
    world.Pipeline().AddSystem(std::make_unique<DestroyCommitSystem>());
    world.Pipeline().AddSystem(std::make_unique<CSharpBatchSystem>());
    world.Pipeline().AddSystem(std::make_unique<ScriptEventDispatchSystem>());
    world.Pipeline().ResolveOrder();

    Scene& s = world.CreateScene("bench-script");
    world.SetActiveScene(&s);

    // 玩家（档① PlayerBehaviour，typeId 0）+ N 弹（Projectile + Transform2D + Velocity 形状对齐 bench-sim）
    Entity player = s.Create();
    s.Emplace<Transform2D>(player, Transform2D{{0, 0}});
    s.Emplace<Meta>(player).team = 0;
    s.Emplace<Velocity>(player);
    host.AttachBehaviour(s, player, 0);

    Rng rng(cfg.seed, 0x51u);
    for (uint32_t i = 0; i < cfg.bullets; ++i) {
        Entity e = s.Create();
        s.Emplace<Transform2D>(e, Transform2D{{0, 0}});
        s.Emplace<Meta>(e).team = 2;
        s.Emplace<Velocity>(e);
        Projectile& pr = s.Emplace<Projectile>(e, Projectile{300, 3.0f, 10, 0, 0, 0});
        pr.age = rng.Float01() * 6.0f; // 初相位（场景布置子流）
    }

    std::printf("[lemon] bench-script: n=%u frames=%u threads=%d%s%s\n", cfg.bullets, cfg.frames,
                world.Jobs().ThreadCount(), recFp ? " [record]" : "", replaying ? " [replay]" : "");

    // 预热（JIT 全量编译/域线程起步；不计时；毒脚本在前 60 帧完成"禁用"验收）
    for (int i = 0; i < 120; i++) world.Step(kDt);
    const uint64_t gcBase = host.GcAllocated();
    uint64_t gcLastSample = gcBase; // GC 采样窗（16 帧）上一读数
    std::vector<uint64_t> gcWin;    // 每窗增量（判据 4：硬 0，逐窗可诊断）

    // C# 净时测量段基线（预热段 Tier0 慢帧不计入——判据 5 噪声源）
    uint64_t csRuns0 = 0;
    double csMs0 = 0;
    for (const SystemProfile& p : world.Pipeline().Profiles())
        if (std::strcmp(p.name, "CSharpBatch") == 0) { csRuns0 = p.runs; csMs0 = p.totalMs; }

    auto t0 = std::chrono::steady_clock::now();
    double totalMs = 0, maxMs = 0;
    uint64_t mismatches = 0, firstMismatch = UINT64_MAX;
    const uint32_t warm = 120;

    for (uint32_t f = 0; f < cfg.frames; ++f) {
        auto step0 = std::chrono::steady_clock::now();
        world.Step(kDt);
        double ms = std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - step0).count();
        totalMs += ms;
        if (ms > maxMs) maxMs = ms;

        uint64_t hash = ComputeStateHash(s);
        if (recFp) std::fprintf(recFp, "h %llu %016llx\n", (unsigned long long)f,
                                (unsigned long long)hash);
        if (replaying) {
            unsigned long long frameNo = 0, want = 0;
            if (std::fscanf(repFp, "h %llu %llx\n", &frameNo, &want) != 2 || want != hash) {
                if (firstMismatch > f) firstMismatch = f;
                ++mismatches;
                if (mismatches <= 5)
                    std::printf("MISMATCH frame %u: want %016llx got %016llx\n", f,
                                (unsigned long long)want, (unsigned long long)hash);
            }
        }

        if (cfg.stats && (f + 1) % 3600 == 0) {
            std::printf("-- frame %u alive=%u gcDelta=%lluB\n", warm + f + 1, s.AliveCount(),
                        (unsigned long long)(host.GcAllocated() - gcBase));
            for (const SystemProfile& p : world.Pipeline().Profiles())
                std::printf("   %-22s %7.3fms (max %7.3f)\n", p.name, p.lastMs, p.maxMs);
        }
        if ((f + 1) % 16 == 0) { // GC 采样窗（16 帧）
            const uint64_t cur = host.GcAllocated();
            gcWin.push_back(cur - gcLastSample);
            gcLastSample = cur;
        }
    }
    (void)t0;

    const double avg = cfg.frames ? totalMs / cfg.frames : 0.0;

    // C# 批量净时间（#14 profile；测量段差分 = 排除预热段）与 C++ 等效对比
    double csNet = 0;
    for (const SystemProfile& p : world.Pipeline().Profiles())
        if (std::strcmp(p.name, "CSharpBatch") == 0 && p.runs > csRuns0)
            csNet = (p.totalMs - csMs0) / (double)(p.runs - csRuns0);

    double ratio = 0;
    if (cfg.cppCompare) {
        // C++ 等效：同负载（双组件池遍历 + 同款轨道数学）纯 C++ 单线程
        volatile float sink = 0;
        auto cpp0 = std::chrono::steady_clock::now();
        constexpr int kTicks = 1000;
        for (int k = 0; k < kTicks; ++k) {
            // 与 C# BoomerangSystem 同负载：双组件视图 + 读写两组件（同槽位 C++ 系统形态）
            float t = k * kDt;
            float cx = std::sin(t * 0.7f) * 180.0f, cy = std::cos(t * 1.1f) * 120.0f;
            auto view = s.View<Projectile, Transform2D>();
            uint32_t i = 0;
            for (auto [e, pr, tr] : view.each()) {
                (void)e;
                pr.age += kDt;
                float a = pr.age * 1.7f + (float)(i % 4096) * 0.0005f;
                float r = 42.0f + (float)(i % 7) * 11.0f;
                tr.pos = {cx + std::cos(a) * r, cy + std::sin(a) * r};
                ++i;
            }
            sink += cx;
        }
        double cppMs = std::chrono::duration<double, std::milli>(
                           std::chrono::steady_clock::now() - cpp0).count() / kTicks;
        ratio = cppMs > 0 ? csNet / cppMs : 0;
        std::printf("cpp-equiv %.3fms/tick  cs-net %.3fms/tick  ratio %.2fx\n", cppMs, csNet,
                    ratio);
    }

    const uint64_t gcDelta = host.GcAllocated() - gcBase;
    // 稳态判据：硬 0（TC 已关；任何非零窗都是真实脚本分配）。逐窗统计供诊断。
    int nzWins = 0;
    uint64_t nzMax = 0;
    for (uint64_t d : gcWin)
        if (d) { ++nzWins; nzMax = std::max(nzMax, d); }
    if (const uint64_t tail = host.GcAllocated() - gcLastSample; tail) { // 末窗余量
        ++nzWins;
        nzMax = std::max(nzMax, tail);
    }
    const bool gcOk = gcDelta == 0;
    std::printf("\n[lemon] ===== bench-script 结果 =====\n");
    std::printf("  步长        : avg %.3fms / max %.3fms（判据 ≤8ms：%s）\n", avg, maxMs,
                avg <= 8.0 ? "PASS" : "FAIL");
    std::printf("  C# 批量净时 : %.3fms%s\n", csNet,
                cfg.cppCompare ? (ratio <= 1.5 ? "（≤1.5x PASS）" : "（>1.5x FAIL）") : "");
    std::printf("  托管分配    : 总 %llu B，非零窗 %d 个（最大窗 %llu B）（稳态判据：%s）\n",
                (unsigned long long)gcDelta, nzWins, (unsigned long long)nzMax,
                gcOk ? "PASS" : "FAIL");
    if (replaying)
        std::printf("  回放        : %s (mismatches=%llu%s)\n",
                    mismatches == 0 ? "PASS" : "FAIL", (unsigned long long)mismatches,
                    firstMismatch != UINT64_MAX ? ", first at frame <warm+...>" : "");
    if (recFp) std::fclose(recFp);
    if (repFp) std::fclose(repFp);

    bool ok = avg <= 8.0 && gcOk && (!replaying || mismatches == 0) &&
              (!cfg.cppCompare || ratio <= 1.5);
    std::printf("RESULT bench-script %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
