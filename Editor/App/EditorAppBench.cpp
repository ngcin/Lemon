// Lemon 编辑器 — --bench-survivor / --bench-scene 压测族（批③c-6 自
// EditorApp.cpp Run 外迁：帧段累计 / 停跑证据采集 / 两裁决。模式同批③c-1..5：
// BenchState 收敛（本 TU 匿名 ns）+ 挂点原位、口径逐位不变。段界 time_point
// 是 Run 帧局部（每帧 epoch 重置——结构体化会跨帧残留，故 BenchSample 十参数
// 直传；段界戳赋值语句零改动）。

#include "App/EditorApp.h"
#include "App/EditorAppSmoke.h"
#include <algorithm>
#include <cstdio>
#include <vector>
#include "Assets/AssetDatabase.h"
#include "Components/BehaviorComponents.h"
#include "Components/CoreComponents.h"
#include "Components/GameplayComponents.h"
#include "Components/RenderComponents.h"
#include "EditorContext.h"
#include "Panels/BuiltInPanels.h" // UiPanelProbe/HierarchyPanelProbe（ui 段归因探针；标记表漏 PanelProbe 族故删后编译器抓住）

namespace lemon::editor {

namespace {

using BenchClock = std::chrono::steady_clock; // 段界戳类型（Run 帧局部同型）
constexpr uint64_t kBenchWarmup = 240; // 预热帧剔除（Spawner ~156 帧涨满 1 万 + 稳态余量）
constexpr int kSegN = 8;
// --bench-survivor 帧时统计（M5 清障③）：全帧耗时含渲染提交与 present 等待——
// Immediate 呈现下 = 真实负载；Play Step 分段计时同步累计（诊断细分）。
// M5 性能批：帧段分解（把缺口拆到环节）。段界 = 帧内打时间戳、帧末统一累计——
// resize/acquire 失败走 continue 的帧整帧不参与（与全帧口径一致）。
// 性能批②：尖刺归因原料——每段 max（带帧号）+ 历史最坏帧的八段快照 + 尖刺帧
//（>25ms）分段和；批③c-6 并入停跑证据（survivor 专属口径）。
struct BenchState {
    double benchFrameSum = 0.0, benchFrameMax = 0.0;
    double benchPumpSum = 0.0, benchSimSum = 0.0, benchGlueSum = 0.0, benchUiSum = 0.0,
           benchAcqSum = 0.0, benchSceneSum = 0.0, benchUiDrawSum = 0.0, benchPresentSum = 0.0;
    uint64_t benchFrameN = 0;
    double benchSegMax[kSegN] = {};
    uint64_t benchSegMaxF[kSegN] = {};
    double benchMaxSeg[kSegN] = {}; // frameMax 刷新时刻的八段值
    double benchSpikeSeg[kSegN] = {};
    uint64_t benchSpikeN = 0;
    bool benchSimProfileZeroed = false;
    std::vector<ecs::SystemProfile> benchPlayProfiles; // Stop 前捕获（Play 世界随 ExitPlay 析构）
    // 停跑证据（批③c-6 自 Run 循环后局部收敛；bench-scene 只取 profiles + alive）
    uint32_t benchTeam1Alive = 0, benchWavesStarted = 0;
    uint32_t benchAnimHit = 0, benchAnimTotal = 0;
    uint32_t benchFxTexts = 0, benchFxBars = 0; // 批① fx 饱和证据（停跑时通道计数）
    float benchPlayerHp = -1.0f; // Hazard 化证据（方案 A 批）：玩家（收集者）掉血 =
                                 // 万怪 Hazard tick 真实发生（<1e6 即证）
};
BenchState g_bench;

} // namespace

// ---- --bench 帧段累计（M5 清障③/M6a 批①：fx 饱和灌入 + 八段累计 + 尖刺归因；
// 批③c-6 自 Run 外迁，挂点原位（firstFrameMs 块后））----
void EditorApp::BenchSample(uint64_t frame,
                            std::chrono::steady_clock::time_point benchT0,
                            std::chrono::steady_clock::time_point bPump,
                            std::chrono::steady_clock::time_point bSim,
                            std::chrono::steady_clock::time_point bUi0,
                            std::chrono::steady_clock::time_point bUi1,
                            std::chrono::steady_clock::time_point bAcq,
                            std::chrono::steady_clock::time_point bScene,
                            std::chrono::steady_clock::time_point bUiDraw,
                            std::chrono::steady_clock::time_point bPresent) {
    if ((Launch().benchSurvivor || Launch().benchScene) && ctx_.Playing() &&
        frame >= kBenchWarmup) {
        // M6a 批①：fx 饱和灌入（survivor 专属口径：验收④ = 池满最坏情形，
        // 256 飘字 + 128 血条每帧全量在场）。飘字确定性网格撒玩家周边（渲染
        // 视口内）；血条挂前 128 只动画怪（each 早退收集；怪被击杀 = 渲染侧
        // resolve 跳过、槽位 sticky 到期次帧收集补位——计数恒 128）
        if (Launch().benchSurvivor) {
            ecs::FxChannel& fx = ctx_.ActiveWorld().Fx();
            for (uint32_t i = 0; i < ecs::FxChannel::kMaxTexts; ++i)
                fx.PopupText("12", -300.0f + (float)(i % 16) * 40.0f,
                             -300.0f + (float)(i / 16) * 40.0f, 0xFF5060F0u);
            uint32_t bars = 0;
            ctx_.ActiveScene().View<ecs::Animator2D>().each(
                [&](auto ent, ecs::Animator2D&) {
                    if (bars >= ecs::FxChannel::kMaxBars) return;
                    fx.Bar(ecs::Scene::FromEntt(ent).id, 0.5f, 0xFF30B0F0u, 32.0f);
                    ++bars;
                });
        }
        const auto segMs = [](BenchClock::time_point a, BenchClock::time_point b) {
            return std::chrono::duration<double, std::milli>(b - a).count();
        };
        const double segs[kSegN] = {
            segMs(benchT0, bPump),   segMs(bPump, bSim),  segMs(bSim, bUi0),
            segMs(bUi0, bUi1),       segMs(bUi1, bAcq),   segMs(bAcq, bScene),
            segMs(bScene, bUiDraw),  segMs(bUiDraw, bPresent),
        };
        if (!g_bench.benchSimProfileZeroed) { // 测量窗口起点：sim 每系统计数清零
            g_bench.benchSimProfileZeroed = true;
            ctx_.ActiveWorld().Pipeline().ZeroProfiles();
        }
        g_bench.benchPumpSum += segs[0];
        g_bench.benchSimSum += segs[1];
        g_bench.benchGlueSum += segs[2];
        g_bench.benchUiSum += segs[3];
        g_bench.benchAcqSum += segs[4];
        g_bench.benchSceneSum += segs[5];
        g_bench.benchUiDrawSum += segs[6];
        g_bench.benchPresentSum += segs[7];
        for (int i = 0; i < kSegN; ++i)
            if (segs[i] > g_bench.benchSegMax[i]) {
                g_bench.benchSegMax[i] = segs[i];
                g_bench.benchSegMaxF[i] = frame;
            }
        const double ms = std::chrono::duration<double, std::milli>(
                              std::chrono::steady_clock::now() - benchT0)
                              .count();
        g_bench.benchFrameSum += ms;
        if (ms > g_bench.benchFrameMax) { // 最坏帧的八段快照（frameMax 归因原料）
            g_bench.benchFrameMax = ms;
            for (int i = 0; i < kSegN; ++i) g_bench.benchMaxSeg[i] = segs[i];
        }
        if (ms > 25.0) { // 尖刺帧（avg≈17，+45% 起）：分段和看集体偏向
            ++g_bench.benchSpikeN;
            for (int i = 0; i < kSegN; ++i) g_bench.benchSpikeSeg[i] += segs[i];
        }
        ++g_bench.benchFrameN;
    }
}

// ---- --bench 停跑证据采集（ExitPlay 弃 Play 世界前留证；批③c-6 自 Run 外迁。
// 返回 playAliveAtStop 等价增量：bench-scene = AliveCount，其余 0（原局部初值））----
uint32_t EditorApp::BenchCaptureStop() {
    if (!((Launch().benchSurvivor || Launch().benchScene) && ctx_.Playing())) return 0;
    uint32_t alive = 0;
    g_bench.benchPlayProfiles = ctx_.ActiveWorld().Pipeline().Profiles(); // ExitPlay 弃世界前留证
    if (Launch().benchScene)
        alive = ctx_.ActiveScene().AliveCount();
    if (Launch().benchSurvivor) { // 以下证据采集 = survivor 专属（bench-scene 只取 profiles + alive）
    ctx_.ActiveScene().View<ecs::XpProgress>().each([&](auto ent, ecs::XpProgress&) {
        if (const ecs::Health* hp = ctx_.ActiveScene().TryGet<ecs::Health>(
                ecs::Scene::FromEntt(ent)))
            g_bench.benchPlayerHp = hp->cur;
    });
    // 导演化证据（M5 批②）：waveIndex=已生效波数；team1 存活突破 Spawner 8000
    // 闸门即导演出生实证（两通道同队，闸门语义见 03 §8 修订注）
    ctx_.ActiveScene().View<ecs::Meta>().each([&](auto, ecs::Meta& m) {
        if (m.team == 1) ++g_bench.benchTeam1Alive;
    });
    ctx_.ActiveScene().View<ecs::WaveDirector>().each(
        [&](auto, ecs::WaveDirector& w) { g_bench.benchWavesStarted += w.waveIndex; });
    // 批① fx 饱和证据：停跑时通道计数（帧循环每帧灌满 → 期望 = 池容量）
    g_bench.benchFxTexts = ctx_.ActiveWorld().Fx().TextCount();
    g_bench.benchFxBars = ctx_.ActiveWorld().Fx().BarCount();
    // 动画化证据（M5 批③）：Animator2D 实体总数 + spriteId 落切片连号区间数
    // （帧映射每 tick 无条件写 → 命中 = 表达 + 切片解析全通；全数应命中）
    const AssetEntry* sh = ctx_.Assets().FindByGuid(kAnimSheetGuid);
    if (sh && sh->Sliced()) {
        ctx_.ActiveScene().View<ecs::Animator2D>().each([&](auto ent, ecs::Animator2D& a) {
            (void)a;
            ++g_bench.benchAnimTotal;
            if (const ecs::SpriteRenderer* sr = ctx_.ActiveScene().TryGet<ecs::SpriteRenderer>(
                    ecs::Scene::FromEntt(ent)))
                if (sr->spriteId >= sh->sliceBase && sr->spriteId < sh->sliceBase + sh->sliceCount)
                    ++g_bench.benchAnimHit;
        });
    }
    } // survivor 专属证据到此
    return alive;
}

// ---- --bench-survivor / --bench-scene 裁决（M5 清障③ + 2026-09-25 工具化；
// 批③c-6 自 Run 外迁。survivor 的 exitCode 写改聚合返回（两模式互斥）；
// scene 恒 0（测量工具，判读归调用方）。printf 顺序不变）----
bool EditorApp::BenchVerdict(uint64_t frame, uint32_t playAliveAtStop) {
    bool ok = true;
    if (Launch().benchSurvivor) {
    const double avg = g_bench.benchFrameN ? g_bench.benchFrameSum / (double)g_bench.benchFrameN : 0.0;
    const double fps = avg > 0.0 ? 1000.0 / avg : 0.0;
    const bool aliveOk = playAliveAtStop >= 10000;
    // 导演化批（M5 批②）：波次 ≥3 生效 + team1 突破 Spawner 8000 闸门
    const bool directorOk = g_bench.benchWavesStarted >= 3 && g_bench.benchTeam1Alive > 8000;
    // 动画化批（M5 批③）：万怪帧映射生效（全数命中切片区间）
    const bool animOk = g_bench.benchAnimTotal >= 10000 && g_bench.benchAnimHit == g_bench.benchAnimTotal;
    // Hazard 化批（2026-09-24 方案 A）：玩家掉血 = Hazard tick 进压测口径
    const bool hazardOk = g_bench.benchPlayerHp >= 0.0f && g_bench.benchPlayerHp < 1'000'000.0f;
    // fx 批（M6a 批① 验收④）：飘字/血条开启（池满饱和渲染）——≥45fps 门槛不动的
    // 前提下通道饱和在场 = 表现层成本进压测口径
    const bool fxOk = g_bench.benchFxTexts == ecs::FxChannel::kMaxTexts &&
                      g_bench.benchFxBars == ecs::FxChannel::kMaxBars;
    const bool pass = aliveOk && avg > 0.0 && avg <= 1000.0 / 45.0 && directorOk &&
                      animOk && hazardOk && fxOk;
    const double segN = g_bench.benchFrameN ? (double)g_bench.benchFrameN : 1.0;
    std::printf("[bench-survivor] 分段avg ms: pump=%.2f sim=%.2f glue=%.2f ui=%.2f "
                "acquire=%.2f scene=%.2f uidraw=%.2f present=%.2f | segSum=%.2f\n",
                g_bench.benchPumpSum / segN, g_bench.benchSimSum / segN, g_bench.benchGlueSum / segN,
                g_bench.benchUiSum / segN, g_bench.benchAcqSum / segN, g_bench.benchSceneSum / segN,
                g_bench.benchUiDrawSum / segN, g_bench.benchPresentSum / segN,
                (g_bench.benchPumpSum + g_bench.benchSimSum + g_bench.benchGlueSum + g_bench.benchUiSum + g_bench.benchAcqSum +
                 g_bench.benchSceneSum + g_bench.benchUiDrawSum + g_bench.benchPresentSum) /
                    segN);
    std::printf("[bench-survivor] frames=%u warmup=%u alive=%u stepAvg=%.2fms "
                "frameAvg=%.2fms frameMax=%.2fms fps=%.0f present=IMMEDIATE(请求)"
                " director(waves=%u teamAlive=%u/闸8000) anim(%u/%u 切片命中)"
                " hazard(playerHp=%.0f<1e6 掉血实证)"
                " fx(texts=%u bars=%u 饱和)"
                " => %s\n",
                (unsigned)frame, (unsigned)kBenchWarmup, playAliveAtStop,
                g_bench.benchSimSum / segN, avg,
                g_bench.benchFrameMax, fps, g_bench.benchWavesStarted, g_bench.benchTeam1Alive,
                g_bench.benchAnimHit, g_bench.benchAnimTotal, g_bench.benchPlayerHp,
                g_bench.benchFxTexts, g_bench.benchFxBars,
                pass ? "PASS" : "FAIL");
    // 性能批②①：sim 系统级分解（测量窗口 = 预热后 ZeroProfiles 起；avg=totalMs/runs）
    {
        std::vector<ecs::SystemProfile> rows;
        for (const ecs::SystemProfile& p : g_bench.benchPlayProfiles)
            if (p.runs > 0) rows.push_back(p);
        std::sort(rows.begin(), rows.end(), [](const ecs::SystemProfile& a,
                                               const ecs::SystemProfile& b) {
            return a.totalMs > b.totalMs;
        });
        double sysSum = 0.0;
        for (const ecs::SystemProfile& p : rows) sysSum += p.totalMs / (double)p.runs;
        std::printf("[bench-survivor] sim系统分解 (Σ=%.2fms vs seg sim=%.2fms):\n",
                    sysSum, g_bench.benchSimSum / segN);
        for (const ecs::SystemProfile& p : rows)
            std::printf("    %-24s avg=%7.3fms max=%7.3fms runs=%llu\n", p.name,
                        p.totalMs / (double)p.runs, (double)p.maxMs,
                        (unsigned long long)p.runs);
    }
    // 性能批②②：尖刺归因——最坏帧八段快照 + 尖刺帧（>25ms）分段均值 + 每段 max
    {
        const char* segNames[kSegN] = {"pump", "sim", "glue", "ui",
                                       "acquire", "scene", "uidraw", "present"};
        std::printf("[bench-survivor] frameMax=%.2fms 帧八段:", g_bench.benchFrameMax);
        for (int i = 0; i < kSegN; ++i) std::printf(" %s=%.2f", segNames[i], g_bench.benchMaxSeg[i]);
        std::printf("\n");
        std::printf("[bench-survivor] 每段max:");
        for (int i = 0; i < kSegN; ++i)
            std::printf(" %s=%.2f@%llu", segNames[i], g_bench.benchSegMax[i],
                        (unsigned long long)g_bench.benchSegMaxF[i]);
        std::printf("\n");
        if (g_bench.benchSpikeN > 0) {
            std::printf("[bench-survivor] 尖刺帧>25ms: %llu 个，其分段均值:",
                        (unsigned long long)g_bench.benchSpikeN);
            for (int i = 0; i < kSegN; ++i)
                std::printf(" %s=%.2f", segNames[i], g_bench.benchSpikeSeg[i] / (double)g_bench.benchSpikeN);
            std::printf("\n");
        } else {
            std::printf("[bench-survivor] 尖刺帧>25ms: 0 个\n");
        }
    }
    // 性能批②③：ui 段内部归因（探针在 HierarchyPanel，LEMON_BENCH_UI_PROBE 开）
    if (const UiPanelProbe hp = HierarchyPanelProbe(); hp.frames > 0)
        std::printf("[bench-survivor] ui段探针: hierarchy=%.2fms（占 ui %.0f%%，"
                    "LEMON_BENCH_UI_PROBE 口径含打点开销）\n",
                    hp.totalMs / (double)hp.frames,
                    g_bench.benchUiSum > 0.0 ? 100.0 * (hp.totalMs / (double)hp.frames) /
                                           (g_bench.benchUiSum / segN)
                                     : 0.0);
        ok = pass;
    }
    // --bench-scene 裁决（2026-09-25）：无场景特定判据——只报数，退出码恒 0
    //（测量工具；判读归调用方/README 口径）。分解块与 survivor 同构、前缀独立。
    if (Launch().benchScene) {
    const double bAvg = g_bench.benchFrameN ? g_bench.benchFrameSum / (double)g_bench.benchFrameN : 0.0;
    const double bFps = bAvg > 0.0 ? 1000.0 / bAvg : 0.0;
    const double bSegN = g_bench.benchFrameN ? (double)g_bench.benchFrameN : 1.0;
    std::printf("[bench-scene] 分段avg ms: pump=%.2f sim=%.2f glue=%.2f ui=%.2f "
                "acquire=%.2f scene=%.2f uidraw=%.2f present=%.2f | segSum=%.2f\n",
                g_bench.benchPumpSum / bSegN, g_bench.benchSimSum / bSegN, g_bench.benchGlueSum / bSegN,
                g_bench.benchUiSum / bSegN, g_bench.benchAcqSum / bSegN, g_bench.benchSceneSum / bSegN,
                g_bench.benchUiDrawSum / bSegN, g_bench.benchPresentSum / bSegN,
                (g_bench.benchPumpSum + g_bench.benchSimSum + g_bench.benchGlueSum + g_bench.benchUiSum + g_bench.benchAcqSum +
                 g_bench.benchSceneSum + g_bench.benchUiDrawSum + g_bench.benchPresentSum) / bSegN);
    std::printf("[bench-scene] RESULT frames=%u warmup=%u alive=%u frameAvg=%.2fms "
                "fps=%.0f => REPORT\n",
                (unsigned)frame, (unsigned)kBenchWarmup, playAliveAtStop, bAvg, bFps);
    {
        std::vector<ecs::SystemProfile> rows;
        for (const ecs::SystemProfile& p : g_bench.benchPlayProfiles)
            if (p.runs > 0) rows.push_back(p);
        std::sort(rows.begin(), rows.end(), [](const ecs::SystemProfile& a,
                                               const ecs::SystemProfile& b) {
            return a.totalMs > b.totalMs;
        });
        double sysSum = 0.0;
        for (const ecs::SystemProfile& p : rows) sysSum += p.totalMs / (double)p.runs;
        std::printf("[bench-scene] sim系统分解 (Σ=%.2fms vs seg sim=%.2fms):\n",
                    sysSum, g_bench.benchSimSum / bSegN);
        for (const ecs::SystemProfile& p : rows)
            std::printf("    %-24s avg=%7.3fms max=%7.3fms runs=%llu\n", p.name,
                        p.totalMs / (double)p.runs, (double)p.maxMs,
                        (unsigned long long)p.runs);
    }
    {
        const char* segNames[kSegN] = {"pump", "sim", "glue", "ui",
                                       "acquire", "scene", "uidraw", "present"};
        std::printf("[bench-scene] frameMax=%.2fms 帧八段:", g_bench.benchFrameMax);
        for (int i = 0; i < kSegN; ++i) std::printf(" %s=%.2f", segNames[i], g_bench.benchMaxSeg[i]);
        std::printf("\n");
        std::printf("[bench-scene] 每段max:");
        for (int i = 0; i < kSegN; ++i)
            std::printf(" %s=%.2f@%llu", segNames[i], g_bench.benchSegMax[i],
                        (unsigned long long)g_bench.benchSegMaxF[i]);
        std::printf("\n");
        if (g_bench.benchSpikeN > 0) {
            std::printf("[bench-scene] 尖刺帧>25ms: %llu 个，其分段均值:",
                        (unsigned long long)g_bench.benchSpikeN);
            for (int i = 0; i < kSegN; ++i)
                std::printf(" %s=%.2f", segNames[i], g_bench.benchSpikeSeg[i] / (double)g_bench.benchSpikeN);
            std::printf("\n");
        } else {
            std::printf("[bench-scene] 尖刺帧>25ms: 0 个\n");
        }
    }
    }
    return ok;
}

} // namespace lemon::editor
