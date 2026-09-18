// Lemon M2 终验收官程序 bench-sim（08 §3 验收场）
// 1 万怪全系统模拟，无渲染（不开窗口/不初始化 Vulkan）。
// 验收：≤8ms/步；确定性回放（同输入逐帧哈希一致，--threads 1 与多线程双档）。
// 负载构成：怪群 Chase 玩家 + 同队分离力 + 玩家环绕射手弹幕 + 命中/死亡/
//           Spawner 补怪 + 投射物寿命回收——16 系统全链路（4 个里程碑占位空跑）。
// 用法：lemon-bench-sim [--n N] [--frames N] [--threads N] [--seed S]
//                       [--stats] [--record F] [--replay F]
//   --threads 1  单线程诊断档（确定性回放基准档）
//   --record/--replay  录制/回放输入流+状态哈希（文本格式，可 diff）
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "Components/BehaviorComponents.h"
#include "Components/CoreComponents.h"
#include "Core/Log.h"
#include "Core/Math.h"
#include "ECS/Scene.h"
#include "ECS/StateHash.h"
#include "ECS/World.h"
#include "Systems/Systems.h"

using namespace lemon;
using namespace lemon::ecs;

namespace {

constexpr float kDt = 1.0f / 60.0f;
constexpr uint64_t kMagic = 0x4C5245504C415931ull; // "LREPLAY1"

struct Config {
    uint32_t monsters = 10000;
    uint32_t frames = 3600; // 60s @60Hz（回放验收用 --frames 18000 = 5 分钟）
    int threads = 0;
    uint64_t seed = 20260919ull;
    bool stats = false;
    const char* recordFile = nullptr;
    const char* replayFile = nullptr;
};

// ------------------------------------------------------------ 场景工厂 ----
// prefab 1 = 怪（Chase+Health+Knockback），prefab 2 = 玩家弹（Projectile）
Entity SpawnFactory(Scene& s, uint32_t prefabId, Vec2 pos, uint32_t team) {
    Entity e = s.Create();
    s.Emplace<Transform2D>(e, Transform2D{pos});
    s.Emplace<Meta>(e).team = team;
    s.Emplace<Velocity>(e);
    if (prefabId == 1) {
        s.Emplace<Health>(e, Health{30, 30, 0});
        s.Emplace<Knockback>(e);
    } else if (prefabId == 2) {
        s.Emplace<Projectile>(e, Projectile{320, 2.5f, 12, 0, 0, 0});
    } else {
        return Entity::Null();
    }
    return e;
}

void BuildScene(World& world, Scene& s, const Config& cfg, Entity& playerOut) {
    world.SetBounds(Rect::FromMinSize({-1600, -1200}, {3200, 2400}));

    // 玩家（team 0）：环绕射手（覆盖 Shooter/HUD 侧弹幕生成）
    playerOut = s.Create();
    s.Emplace<Transform2D>(playerOut, Transform2D{{0, 0}});
    s.Emplace<Meta>(playerOut).team = 0;
    s.Emplace<Velocity>(playerOut);
    s.Emplace<Health>(playerOut, Health{500, 500, 0});
    Shooter& sh = s.Emplace<Shooter>(playerOut);
    sh.interval = 0.05f;
    sh.range = 400;
    sh.targetTeam = 1;
    sh.projectileId = 2;
    sh.cooldown = 0.0f;

    Rng rng(cfg.seed, 0x51u); // 场景布置子流（与系统子流空间分离）

    // 怪群（team 1）：Chase 玩家 + 同队分离（soft-collide）
    for (uint32_t i = 0; i < cfg.monsters; ++i) {
        Entity m = SpawnFactory(s, 1, {0, 0}, 1);
        float ang = rng.Angle();
        float rad = 60.0f + 1400.0f * std::sqrt(rng.Float01()); // 环形分布
        s.Get<Transform2D>(m).pos = {std::cos(ang) * rad, std::sin(ang) * rad};
        Chase& ch = s.Emplace<Chase>(m);
        ch.speed = 60.0f + rng.Float01() * 40.0f;
        ch.aggroRange = 2000.0f; // 竞技场内恒有目标
        ch.keepRange = 24.0f;
        ch.targetTeam = 0;
    }

    // 补怪口（死后维持怪量：销毁两阶段/池回收/Spawn 系统全链路）
    Entity spawner = s.Create();
    s.Emplace<Transform2D>(spawner, Transform2D{{0, 0}});
    s.Emplace<Meta>(spawner).team = 1;
    Spawner& sp = s.Emplace<Spawner>(spawner);
    sp.prefabId = 1;
    sp.interval = 0.05f;
    sp.burst = 4;
    sp.range = 1500;
    sp.maxAlive = cfg.monsters; // 配额：怪量维持 n（压测红线）
    sp.spawnTeam = 1;
    sp.cooldown = 0.0f;
    s.Emplace<Chase>(spawner) = Chase{55, 2000, 24, 0};
    s.Emplace<Health>(spawner, Health{30, 30, 0});
    s.Emplace<Knockback>(spawner);
    s.Emplace<Velocity>(spawner);
}

// 输入脚本：玩家绕圈走（独立确定性子流；录制时生成并写盘，回放时纯读盘）
InputState ScriptedInput(uint64_t seed, uint64_t tick) {
    Rng rng(seed, 0x1Fu);
    for (uint64_t i = 0; i < tick; ++i) rng.Next(); // 跳到本帧（顺序生成，免存全表）
    InputState in;
    in.ax = rng.Float01() * 2.0f - 1.0f;
    in.ay = rng.Float01() * 2.0f - 1.0f;
    return in;
}

} // namespace

int main(int argc, char** argv) {
    Config cfg;
    for (int i = 1; i < argc; ++i) {
        auto next = [&]() -> const char* { return (i + 1 < argc) ? argv[++i] : ""; };
        if (!std::strcmp(argv[i], "--n")) cfg.monsters = (uint32_t)std::atoi(next());
        else if (!std::strcmp(argv[i], "--frames")) cfg.frames = (uint32_t)std::atoi(next());
        else if (!std::strcmp(argv[i], "--threads")) cfg.threads = std::atoi(next());
        else if (!std::strcmp(argv[i], "--seed")) cfg.seed = std::strtoull(next(), nullptr, 10);
        else if (!std::strcmp(argv[i], "--stats")) cfg.stats = true;
        else if (!std::strcmp(argv[i], "--record")) cfg.recordFile = next();
        else if (!std::strcmp(argv[i], "--replay")) cfg.replayFile = next();
    }

    const bool replaying = cfg.replayFile != nullptr;
    std::FILE* recFp = nullptr;
    std::FILE* repFp = nullptr;
    if (cfg.recordFile) {
        recFp = std::fopen(cfg.recordFile, "w");
        if (!recFp) {
            LEMON_WARN("cannot open record file: %s", cfg.recordFile);
            return 1;
        }
        std::fprintf(recFp, "LREPLAY1\n");
        std::fprintf(recFp, "seed %llu threads %d\n", (unsigned long long)cfg.seed,
                     cfg.threads);
    }
    if (replaying) {
        repFp = std::fopen(cfg.replayFile, "r");
        if (!repFp) {
            LEMON_WARN("cannot open replay file: %s", cfg.replayFile);
            return 1;
        }
        char magic[16] = {};
        uint64_t seed = 0;
        int threads = 0;
        if (std::fscanf(repFp, "%15s\n", magic) != 1 ||
            std::strcmp(magic, "LREPLAY1") != 0 ||
            std::fscanf(repFp, "seed %llu threads %d\n", (unsigned long long*)&seed,
                        &threads) != 2) {
            LEMON_WARN("replay header malformed");
            return 1;
        }
        cfg.seed = seed;
    }

    WorldDesc wd;
    wd.seed = cfg.seed;
    wd.threadCount = cfg.threads;
    World world(wd);
    world.SetSpawnFn(SpawnFactory);
    world.InstallDefaultSystems();

    Scene& s = world.CreateScene("bench-sim");
    world.SetActiveScene(&s);
    Entity player{};
    BuildScene(world, s, cfg, player);
    (void)player;

    // 预热：两步建哈希/目标缓存（不计时）
    world.Step(kDt);
    world.Step(kDt);

    std::printf("[lemon] bench-sim: n=%u frames=%u threads=%d%s%s\n", cfg.monsters,
                cfg.frames, world.Jobs().ThreadCount(), recFp ? " [record]" : "",
                replaying ? " [replay]" : "");

    auto t0 = std::chrono::steady_clock::now();
    double totalMs = 0, maxMs = 0, ema = 0;
    uint64_t hashMismatches = 0;
    uint64_t firstMismatch = UINT64_MAX;

    for (uint32_t f = 0; f < cfg.frames; ++f) {
        // 输入：录制=脚本生成+落盘；回放=纯读盘
        if (recFp) {
            InputState in = ScriptedInput(cfg.seed, f);
            world.ApplyInput(in);
            std::fprintf(recFp, "%llu %llx %a %a\n", (unsigned long long)f,
                         (unsigned long long)in.buttons, in.ax, in.ay);
        } else if (replaying) {
            unsigned long long frameNo = 0, buttons = 0;
            float ax = 0, ay = 0;
            if (std::fscanf(repFp, "%llu %llx %a %a\n", &frameNo, &buttons, &ax, &ay) != 4) {
                LEMON_WARN("replay stream truncated at frame %u", f);
                return 1;
            }
            if (frameNo != f) {
                LEMON_WARN("replay frame desync: got %llu want %u", frameNo, f);
                return 1;
            }
            InputState in;
            in.buttons = buttons;
            in.ax = ax;
            in.ay = ay;
            world.ApplyInput(in);
        }

        auto step0 = std::chrono::steady_clock::now();
        world.Step(kDt);
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - step0).count();
        totalMs += ms;
        if (ms > maxMs) maxMs = ms;
        ema = ema == 0 ? ms : ema * 0.95 + ms * 0.05;

        // 看门狗（09 §0.3：模拟卡死即中止，勿硬扛）
        if (ema > 250.0) {
            std::printf("ABORT step-ema %.1fms exceeds watchdog at frame %u\n", ema, f);
            return 1;
        }

        // 回放校验：逐帧状态哈希比对（录制侧哈希写盘在录制模式下进行）
        uint64_t hash = 0;
        if (recFp || replaying) hash = ComputeStateHash(s);
        if (recFp) std::fprintf(recFp, "h %llu %016llx\n", (unsigned long long)f, hash);
        if (replaying) {
            unsigned long long frameNo = 0;
            unsigned long long want = 0;
            if (std::fscanf(repFp, "h %llu %llx\n", &frameNo, &want) != 2) {
                LEMON_WARN("replay hash stream truncated at frame %u", f);
                return 1;
            }
            if (want != hash) {
                if (firstMismatch > f) firstMismatch = f;
                ++hashMismatches;
                if (hashMismatches <= 5)
                    std::printf("MISMATCH frame %u: want %016llx got %016llx\n", f,
                                want, hash);
            }
        }

        if (cfg.stats && (f + 1) % 3600 == 0) { // 每模拟分钟
            std::printf("-- frame %u alive=%u events=%u/%u\n", f + 1, s.AliveCount(),
                        world.Events().Size(), world.Events().PeakSize());
            for (const SystemProfile& p : world.Pipeline().Profiles())
                std::printf("   %-22s %7.3fms (max %7.3f)\n", p.name,
                            p.lastMs, p.maxMs);
        }
    }

    double avg = totalMs / cfg.frames;
    double wallMs = std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - t0).count();

    // RESULT 行（09 §2 判读约定）
    std::printf("RESULT sim avg=%.3fms max=%.3fms wall=%.0fms alive=%u created=%llu "
                "destroyed=%llu threads=%d",
                avg, maxMs, wallMs, s.AliveCount(), (unsigned long long)s.CreatedTotal(),
                (unsigned long long)s.DestroyedTotal(), world.Jobs().ThreadCount());
    if (replaying)
        std::printf(" replay=%s mismatches=%llu", hashMismatches == 0 ? "PASS" : "FAIL",
                    (unsigned long long)hashMismatches);
    std::printf("\n");

    if (recFp) std::fclose(recFp);
    if (repFp) std::fclose(repFp);
    return replaying ? (hashMismatches == 0 ? 0 : 1) : (avg <= 8.0 ? 0 : 1);
}
