// Lemon 引擎单测 — CoreTests —
// 数学/核心容器（Vec2/Mat3x2/Rect/Color/Utils/Rng/JobSystem/Pool/RingQueue）（M7c 批⓪ T2 自
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
#include <filesystem>
#include <fstream>
#include <string>
#include <limits>
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

void TestVec2() {
    Vec2 a{3, 4};
    ExpectNear(Length(a), 5.0f, 1e-6f, "Vec2 length");
    Vec2 n = Normalize(a);
    ExpectNear(Dot(n, n), 1.0f, 1e-6f, "Vec2 normalize");
    Expect(Normalize(Vec2::Zero()) == Vec2::Zero(), "normalize zero safe");
    Expect(Lerp(Vec2{0, 0}, Vec2{10, 20}, 0.5f) == Vec2(5, 10), "Vec2 lerp");
}

void TestMat3x2() {
    // 单位
    Mat3x2 id = Mat3x2::Identity();
    Vec2 p{7, -3};
    Expect(id.Apply(p) == p, "identity apply");

    // TRS：先缩放再旋转后平移 —— 手算一组
    Mat3x2 trs = Mat3x2::FromTRS({10, 20}, kPi / 2.0f, {2, 4}); // 旋转 90°(顺时针,Y 向下)
    Vec2 q = trs.Apply({1, 0}); // S→(2,0), R90→(0,2), T→(10,22)
    ExpectNear(q.x, 10.0f, 1e-5f, "TRS x");
    ExpectNear(q.y, 22.0f, 1e-5f, "TRS y");

    // 复合：(A·B)(p) == A(B(p))
    Mat3x2 A = Mat3x2::FromTRS({5, -2}, 0.7f, {1.5f, 0.5f});
    Mat3x2 B = Mat3x2::FromTRS({-1, 8}, -1.2f, {0.3f, 2.0f});
    Vec2 ab = (A * B).Apply(p);
    Vec2 abRef = A.Apply(B.Apply(p));
    ExpectNear(ab.x, abRef.x, 1e-4f, "compose x");
    ExpectNear(ab.y, abRef.y, 1e-4f, "compose y");

    // 正交投影：中心→原点，半宽→±1，世界 Y 向下 = Vulkan NDC Y 向下（屏幕下方 = +1；
    // 2026-09-19 修订：原按 GL 语义断言 -1，MoltenVK 上整体镜像——anim-smoke 截图实锤）
    Mat3x2 vp = Mat3x2::Ortho({100, 50}, 200, 100);
    Vec2 c = vp.Apply({100, 50});
    ExpectNear(c.x, 0.0f, 1e-6f, "ortho center x");
    ExpectNear(c.y, 0.0f, 1e-6f, "ortho center y");
    Vec2 right = vp.Apply({300, 50}); // +halfW
    ExpectNear(right.x, 1.0f, 1e-6f, "ortho +x edge");
    Vec2 down = vp.Apply({100, 150}); // +halfH(世界 Y 向下) → NDC +1（Vulkan 屏幕下方）
    ExpectNear(down.y, 1.0f, 1e-6f, "ortho y-down");
}

void TestRect() {
    Rect r = Rect::FromCenterHalf({0, 0}, 10, 5);
    Expect(r.Contains({0, 0}), "rect contains center");
    Expect(!r.Contains({10.1f, 0}), "rect excludes outside");
    Rect big = r.Expanded(5);
    Expect(big.Contains({14, 0}), "rect expanded");
    Expect(r.Overlaps(Rect::FromCenterHalf({9, 0}, 1, 1)), "rect overlap");
    Expect(!r.Overlaps(Rect::FromCenterHalf({20, 0}, 1, 1)), "rect disjoint");
    Rect clamped = Rect{{-100, -100}, {100, 100}}.ClampedTo(r);
    Expect(clamped.min == Vec2(-10, -5) && clamped.max == Vec2(10, 5), "rect clamped");
}

void TestColor() {
    uint32_t p = PackRGBA(255, 128, 0, 255);
    Expect((p & 0xFF) == 255 && ((p >> 8) & 0xFF) == 128 && ((p >> 16) & 0xFF) == 0 &&
               ((p >> 24) & 0xFF) == 255,
           "PackRGBA layout r|g<<8|b<<16|a<<24");
    Color c = Color::FromRGBA8(p);
    ExpectNear(c.r, 1.0f, 0.001f, "color r");
    ExpectNear(c.g, 128.0f / 255.0f, 0.01f, "color g");
    uint32_t back = c.ToRGBA8();
    Expect(back == p, "color roundtrip");
}

void TestUtils() {
    ExpectNear(Damp(4.0f, 0.25f), 1.0f - std::exp(-1.0f), 1e-6f, "damp closed form");
    Expect(Damp(4.0f, 0.0f) == 0.0f, "damp zero dt");
    ExpectNear(SnapTo(10.4f, 4.0f), 12.0f, 1e-6f, "snap 10.4→12 (grid 4)");
    ExpectNear(SnapTo(10.4f, 1.0f), 10.0f, 1e-6f, "snap 10.4→10 (grid 1)");
    ExpectNear(SnapTo(-0.3f, 1.0f), 0.0f, 1e-6f, "snap -0.3→0");
}

void TestRng() {
    // 同 seed 同 stream → 逐位一致（确定性回放的地基）
    Rng a(12345, 7), b(12345, 7);
    for (int i = 0; i < 1000; ++i) Expect(a.Next() == b.Next(), "rng same stream bit-exact");

    // 不同子流 → 序列不同（系统间互不干扰）
    Rng c(12345, 8);
    Expect(c.Next() != a.Next(), "rng different stream diverges");

    // Float01 值域 [0,1)
    Rng r(42, 0);
    for (int i = 0; i < 10000; ++i) {
        float f = r.Float01();
        Expect(f >= 0.0f && f < 1.0f, "float01 in [0,1)");
    }

    // 整数 Range：值域覆盖 + 闭区间（拒绝采样无偏差）
    bool seen[6] = {};
    for (int i = 0; i < 10000; ++i) seen[r.Range(5u, 10u) - 5] = true;
    for (int i = 0; i < 6; ++i) Expect(seen[i], "range covers all values");

    // F-10（2026-09-24）：单点/逆序区间契约——老实现 span==1 时 zone 截 0、
    // r>=zone 恒真，Range(x,x) 永久死循环（与 Pcg32.cs 双端同修，script-tests 对拍）
    Expect(r.Range(7u, 7u) == 7u, "range single point returns lo (no hang)");
    Expect(r.Range(9u, 3u) == 9u, "range inverted contract returns lo (no hang)");

    // span 为二次幂（整除 2^32、无拒绝区间）：老实现 zone 截 0、同样永久死循环
    // （Range(0,1) 抛硬币即中招——与 Pcg32.cs 双端同修，script-tests 对拍）
    bool seenPow2[4] = {};
    for (int i = 0; i < 10000; ++i) {
        uint32_t v01 = r.Range(0u, 1u);
        Expect(v01 <= 1u, "range pow2 span (0,1) in bounds (no hang)");
        seenPow2[r.Range(0u, 3u)] = true;
    }
    for (int i = 0; i < 4; ++i) Expect(seenPow2[i], "range pow2 span (0,3) covers all values");

    // UnitVec2 长度 ≈ 1（FastSinCos LUT 误差界内）
    for (int i = 0; i < 100; ++i) {
        Vec2 v = r.UnitVec2();
        ExpectNear(Length(v), 1.0f, 2e-3f, "unit vec2 length");
    }

    // golden 值：固定 seed 首 4 输出（防实现漂移静默破坏已录制回放）
    Rng g(0xDEADBEEFull, 1);
    Expect(g.Next() == 0xc05d8ee3u, "rng golden #0");
    Expect(g.Next() == 0x211721beu, "rng golden #1");
    Expect(g.Next() == 0x3a5791a9u, "rng golden #2");
    Expect(g.Next() == 0x29f0a1f7u, "rng golden #3");
}

void TestJobSystem() {
    // 单线程诊断档：Schedule 就地执行
    {
        JobSystem jobs(1);
        Expect(jobs.ThreadCount() == 1, "single-thread tier");
        std::atomic<int> ran{0};
        auto f = jobs.Schedule([&ran] { ran.fetch_add(1); });
        Expect(ran.load() == 1, "single-thread executes inline");
        JobSystem::Complete(f);
    }

    // 多线程 ParallelFor：区间完整覆盖、无重叠（每下标恰好一次）
    {
        JobSystem jobs(0); // 自动线程数
        const uint32_t kN = 10000, kGrain = 256;
        std::vector<std::atomic<uint32_t>> hits(kN);
        for (auto& h : hits) h.store(0);
        jobs.ParallelFor(kN, kGrain, [&](uint32_t begin, uint32_t end) {
            for (uint32_t i = begin; i < end; ++i) hits[i].fetch_add(1);
        });
        uint32_t total = 0;
        for (auto& h : hits) total += h.load();
        Expect(total == kN, "parallel-for covers exactly once");

        // 非整除 grain 的边界（10000/256 = 39.06 → 40 块，末块 80）
        std::atomic<uint32_t> blocks{0}, minBlock{kN}, maxBlock{0};
        jobs.ParallelFor(kN, 256, [&](uint32_t b, uint32_t e) {
            blocks.fetch_add(1);
            uint32_t len = e - b;
            uint32_t cur = minBlock.load();
            while (len < cur && !minBlock.compare_exchange_weak(cur, len)) {
            }
            cur = maxBlock.load();
            while (len > cur && !maxBlock.compare_exchange_weak(cur, len)) {
            }
        });
        Expect(blocks.load() == 40, "block count with remainder");
        Expect(minBlock.load() == 16, "last short block size (10000-39*256)");
        Expect(maxBlock.load() == 256, "full block size");
    }

    // 多任务 Schedule 乱序完成也不丢
    {
        JobSystem jobs(2);
        std::atomic<int> sum{0};
        std::vector<JobSystem::JobHandle> handles;
        for (int i = 0; i < 500; ++i)
            handles.emplace_back(jobs.Schedule([&sum] { sum.fetch_add(1); }));
        for (auto& h : handles) JobSystem::Complete(h);
        Expect(sum.load() == 500, "all scheduled tasks complete");
    }
}

void TestPool() {
    struct Bullet {
        float x = 0, y = 0;
        int gen = 0;
    };
    Pool<Bullet> pool;

    uint32_t a = pool.Acquire();
    pool[a].x = 5;
    pool.Release(a);
    uint32_t b = pool.Acquire(); // 应复用同槽位
    Expect(b == a, "pool reuses released slot");
    Expect(pool.ReuseHits() == 1, "reuse hit counted");
    Expect(pool.LiveCount() == 1, "live count after reuse");

    // 延迟归还：flush 前对象仍可访问，flush 后才进复用
    uint32_t c = pool.Acquire();
    pool[c].gen = 3;
    pool.DeferredRelease(c);
    Expect(pool.LiveCount() == 2, "deferred release keeps live until flush");
    Expect(pool[c].gen == 3, "deferred object readable in-frame");
    pool.FlushReleases();
    Expect(pool.LiveCount() == 1, "flush commits deferred releases");
    uint32_t d = pool.Acquire();
    Expect(d == c, "flushed slot reusable");
    Expect(pool.LiveCount() == 2, "live count after reacquire");
}

void TestRingQueue() {
    RingQueue<int> q(8);
    for (int i = 0; i < 8; ++i) q.Push(i);
    Expect(q.Size() == 8, "queue fills");
    Expect(q.Push(8) == true, "queue grows instead of dropping");
    for (int i = 0; i < 4; ++i) q.Pop(); // pop 0,1,2,3
    q.Push(9);
    q.Push(10);
    Expect(q.Size() == 7, "queue size after wrap");
    for (int i = 0; i < 7; ++i) {
        Expect(q.At(0) == i + 4, "fifo order across wrap"); // At 偏移随 Pop 前进，恒取队头
        q.Pop();
    }
    Expect(q.Empty(), "queue empty");

    // 自动扩容：数据保持 FIFO 完整
    RingQueue<int> g(4);
    for (int i = 0; i < 100; ++i) g.Push(i);
    Expect(g.Size() == 100, "grown queue size");
    for (int i = 0; i < 100; ++i) {
        Expect(g.Front() == i, "grown queue fifo intact");
        g.Pop();
    }
    Expect(g.PeakSize() == 100, "peak tracked");
}


// --------------------------------------------------------------- 随机纪律 ----
// L15（review 2026-10-09）：ADR-010 D3 所称 grep 防线测试本体化——SDK/Entry 的
// C# 源码禁 System.Random 使用形态（全引擎唯一随机源 = PCG32 子流；Pcg32.cs:2
// 纪律注释本身不含这些使用形态，不误伤）。Fx.Crit 曾用 Random.Shared 破纪未被
// 拦（防线缺失实抓）。
void TestScriptingRandomDiscipline() {
    namespace fs = std::filesystem;
    const fs::path root = LEMON_SOURCE_DIR "/Engine/Scripting/dotnet";
    Expect(fs::exists(root), "dotnet source root exists");
    static const char* kBanned[] = {"new System.Random", "Random.Shared",
                                    ".NextDouble(", "new Random(",
                                    "System.Random "}; // R-b5（b11d review）：目标类型
                                    // new 形态（`System.Random r = new();`）；纪律注释
                                    // （Pcg32.cs:2）用全角括号不误伤
    uint32_t scanned = 0;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(root, ec), end; it != end; it.increment(ec)) {
        if (ec || !it->is_regular_file()) continue;
        if (it->path().extension() != ".cs") continue;
        std::ifstream f(it->path());
        std::string line;
        while (std::getline(f, line))
            for (const char* b : kBanned)
                if (line.find(b) != std::string::npos) {
                    char msg[512];
                    std::snprintf(msg, sizeof msg,
                                  "random discipline violation: %s contains '%s'",
                                  it->path().string().c_str(), b);
                    Expect(false, msg); // 响亮定位（每违规一处一条）
                    break;
                }
        ++scanned;
    }
    Expect(scanned > 0, "random discipline scan non-empty");
}

} // namespace

void RunCoreTests() {
    TestVec2();
    TestMat3x2();
    TestRect();
    TestColor();
    TestUtils();
    TestRng();
    TestJobSystem();
    TestPool();
    TestRingQueue();
    TestScriptingRandomDiscipline();
}
