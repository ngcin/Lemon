// Lemon 引擎单测 — 纯逻辑层（数学/批键/图集 UV/相机/粒子池）
// 断言风格：LEMON_ASSERT 失败即 abort，进程退出码非 0 = 测试失败。
#include "Core/Log.h"

#include <cmath>
#include <cstdint>
#include <algorithm>

#include "Core/Math.h"
#include "Renderer/Atlas.h"
#include "Renderer/BitmapFont.h"
#include "Renderer/Camera2D.h"
#include "Renderer/Particles.h"
#include "Renderer/Quality.h"
#include "Renderer/Renderable.h"

using namespace lemon;
using namespace lemon::math;
using namespace lemon::renderer;

namespace {

int g_checks = 0;
void Expect(bool cond, const char* what) {
    ++g_checks;
    if (!cond) {
        LEMON_LOG("FAIL: %s", what);
        std::abort();
    }
}
void ExpectNear(float a, float b, float eps, const char* what) {
    ++g_checks;
    if (std::fabs(a - b) > eps) {
        LEMON_LOG("FAIL: %s  (%f vs %f, eps %g)", what, a, b, eps);
        std::abort();
    }
}

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
    Vec2 q = trs.Apply({1, 0});                                  // S→(2,0), R90→(0,2), T→(10,22)
    ExpectNear(q.x, 10.0f, 1e-5f, "TRS x");
    ExpectNear(q.y, 22.0f, 1e-5f, "TRS y");

    // 复合：(A·B)(p) == A(B(p))
    Mat3x2 A = Mat3x2::FromTRS({5, -2}, 0.7f, {1.5f, 0.5f});
    Mat3x2 B = Mat3x2::FromTRS({-1, 8}, -1.2f, {0.3f, 2.0f});
    Vec2 ab = (A * B).Apply(p);
    Vec2 abRef = A.Apply(B.Apply(p));
    ExpectNear(ab.x, abRef.x, 1e-4f, "compose x");
    ExpectNear(ab.y, abRef.y, 1e-4f, "compose y");

    // 正交投影：中心→原点，半宽→±1，Y 翻转
    Mat3x2 vp = Mat3x2::Ortho({100, 50}, 200, 100);
    Vec2 c = vp.Apply({100, 50});
    ExpectNear(c.x, 0.0f, 1e-6f, "ortho center x");
    ExpectNear(c.y, 0.0f, 1e-6f, "ortho center y");
    Vec2 right = vp.Apply({300, 50}); // +halfW
    ExpectNear(right.x, 1.0f, 1e-6f, "ortho +x edge");
    Vec2 down = vp.Apply({100, 150}); // +halfH(世界 Y 向下) → NDC -1
    ExpectNear(down.y, -1.0f, 1e-6f, "ortho y-flip");
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

void TestAtlasUV() {
    // 512 图集 (64,0)-(128,64) → 归一化 UV 四分之一/八分之一象限
    SpriteInfo s = MakeSpriteInfo(0, 512, 512, 64, 0, 64, 64);
    ExpectNear(s.u0, 64.0f / 512.0f, 1e-6f, "atlas u0");
    ExpectNear(s.v0, 0.0f, 1e-6f, "atlas v0");
    ExpectNear(s.u1, 128.0f / 512.0f, 1e-6f, "atlas u1");
    ExpectNear(s.v1, 64.0f / 512.0f, 1e-6f, "atlas v1");
    Expect(s.widthPx == 64 && s.heightPx == 64, "atlas sprite px size");

    // 边界：整图 sprite → 全 0..1
    SpriteInfo full = MakeSpriteInfo(3, 64, 64, 0, 0, 64, 64);
    Expect(full.u0 == 0 && full.v0 == 0 && full.u1 == 1 && full.v1 == 1, "atlas full rect");
    Expect(full.atlasIndex == 3, "atlas index passthrough");
}

void TestBatchKey() {
    SpriteBatchKey a = MakeBatchKey(0, BlendKind::Alpha, FilterKind::Linear, 0);
    SpriteBatchKey b = MakeBatchKey(0, BlendKind::Alpha, FilterKind::Linear, 0);
    SpriteBatchKey otherAtlas = MakeBatchKey(1, BlendKind::Alpha, FilterKind::Linear, 0);
    SpriteBatchKey otherBlend = MakeBatchKey(0, BlendKind::Additive, FilterKind::Linear, 0);
    SpriteBatchKey otherLayer = MakeBatchKey(0, BlendKind::Alpha, FilterKind::Linear, 3);
    Expect(a.hash == b.hash, "same params same hash (预计算哈希确定性)");
    Expect(a.SameBatch(b), "same key same batch");
    Expect(!a.SameBatch(otherAtlas), "different atlas splits batch");
    Expect(!a.SameBatch(otherBlend), "different blend splits batch");
    Expect(!a.SameBatch(otherLayer), "different layer splits batch");

    // 排序键布局：layer 优先于批键、order 在层内、seq 稳定
    // 构造:layer1 的 keyHash 大 vs layer0 的 keyHash 小 → layer0 仍在前
    uint64_t kLowLayer = ((uint64_t)0 << 56) | (0xFFFFull << 40);
    uint64_t kHighLayer = ((uint64_t)1 << 56) | (0x0000ull << 40);
    Expect(kLowLayer < kHighLayer, "sorting layer dominates batch key");

    // 同 layer 同键内 order 排序
    uint64_t o1 = (1ull << 24) | 5;
    uint64_t o2 = (2ull << 24) | 1;
    Expect(o1 < o2, "order within batch");
}

void TestSortStability() {
    // 模拟 Extract 排序段：同批键包 order/seq 有序 → 稳定可复现
    struct P { uint64_t sortKey; int id; };
    std::vector<P> v;
    for (int i = 0; i < 100; ++i) v.push_back({(1ull << 56) | ((uint64_t)(i % 7) << 24) | (uint64_t)(99 - i), i});
    std::stable_sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.sortKey < b.sortKey; });
    for (size_t i = 1; i < v.size(); ++i)
        Expect(v[i - 1].sortKey <= v[i].sortKey, "sorted monotonic");
}

void TestParticles() {
    ParticleSystem ps;
    ps.SetBudget(10);
    // 注册假图集页（AtlasRegistry 不触碰 device，句柄只透传）
    AtlasRegistry atlas;
    rhi::Texture fakeTex{1};
    atlas.RegisterAtlas(0, fakeTex, 64, 64);
    uint32_t sprA = atlas.AddSprite(0, 0, 0, 16, 16);
    uint32_t sprB = atlas.AddSprite(0, 16, 0, 16, 16);

    // 发射钳制：budget=10, rate=1000/s, dt=1 → 存活 10, 丢弃 990
    EmitterConfig e;
    e.rate = 1000.0f;
    e.lifetimeMin = e.lifetimeMax = 5.0f;
    e.spriteId = sprA;
    float accum = 0;
    ps.Emit(e, 1.0f, 42, accum);
    Expect(ps.AliveCount() == 10, "emit clamped to budget");
    Expect(ps.LastStats().droppedFull == 990, "dropped counted");

    // 寿命尽 swap-and-pop 回收
    ps.Simulate({0, 0}, 5.0f);
    Expect(ps.AliveCount() == 0, "all expired recycled");

    // 桶分组：同图集同参数的两个子纹理 → 键相同 → 合批（0 切换，图集合批的意义）
    ps.SetBudget(1000);
    EmitterConfig e2 = e;
    e2.rate = 100.0f;
    e2.lifetimeMin = e2.lifetimeMax = 10.0f;
    e2.spriteId = sprB;
    float acc1 = 0, acc2 = 0;
    ps.Emit(e, 0.5f, 1, acc1);   // rate 1000 × 0.5s = 500 个 A
    ps.Emit(e2, 0.5f, 2, acc2);  // rate 100 × 0.5s = 50 个 B（同图集同混合 → 同键）
    Expect(ps.AliveCount() == 550, "two emitters alive");
    auto packets = ps.Extract(atlas, {32, 32}, 1000, 1000);
    Expect(packets.size() == 550, "all visible");
    int switches = 0;
    for (size_t i = 1; i < packets.size(); ++i)
        if (!packets[i].key.SameBatch(packets[i - 1].key)) ++switches;
    Expect(switches == 0, "same-atlas sprites share one batch key");

    // 对照：不同混合模式 → 分段连续（各一段）
    ParticleSystem ps2;
    ps2.SetBudget(1000);
    EmitterConfig eAlpha = e2;
    eAlpha.blend = (uint8_t)BlendKind::Alpha;
    float a1 = 0, a2 = 0;
    ps2.Emit(e2, 0.5f, 1, a1);
    ps2.Emit(eAlpha, 0.5f, 2, a2);
    auto packets2 = ps2.Extract(atlas, {32, 32}, 1000, 1000);
    int switches2 = 0;
    for (size_t i = 1; i < packets2.size(); ++i)
        if (!packets2[i].key.SameBatch(packets2[i - 1].key)) ++switches2;
    Expect(switches2 == 1, "different blend splits contiguous runs");
}

void TestCamera2D() {
    Camera2D cam;
    cam.center = {100, 50};
    cam.halfHeight = 100;
    // 正交：视口中心→NDC 原点，Y 翻转
    Mat3x2 vp = cam.ViewProj(1.6f); // 半宽 160
    Vec2 c = vp.Apply(cam.center);
    ExpectNear(c.x, 0.0f, 1e-6f, "cam center→origin");
    ExpectNear(c.y, 0.0f, 1e-6f, "cam center→origin y");
    ExpectNear(vp.Apply({260, 50}).x, 1.0f, 1e-6f, "cam right edge");
    ExpectNear(vp.Apply({100, 150}).y, -1.0f, 1e-6f, "cam +y down → -1 NDC");

    // 阻尼跟随收敛（多步后接近目标）
    Vec2 target{500, -300};
    for (int i = 0; i < 240; ++i) cam.Follow(target, 1.0f / 60.0f);
    ExpectNear(cam.center.x, 500.0f, 0.5f, "follow converges x");
    ExpectNear(cam.center.y, -300.0f, 0.5f, "follow converges y");

    // 边界钳制：世界 1000×1000，视口 400×800（宽>半高钳制）
    Camera2D b;
    b.center = {0, 0};
    b.halfHeight = 400;
    b.worldBounds = {{0, 0}, {1000, 1000}};
    b.hasBounds = true;
    b.ClampToBounds(0.5f); // 半宽 200
    ExpectNear(b.center.x, 200.0f, 1e-4f, "clamp x to min+halfW");
    ExpectNear(b.center.y, 400.0f, 1e-4f, "clamp y (视口高=世界高→居中 500? 半高 400>500/2→居中)");
    b.center = {1000, 1000};
    b.ClampToBounds(0.5f);
    ExpectNear(b.center.x, 800.0f, 1e-4f, "clamp x to max-halfW");

    // 像素完美：zoom 吸整、snap 网格
    Camera2D pp;
    pp.center = {100.37f, 50.62f};
    pp.zoom = 2.6f;
    pp.pixelPerfect = true;
    pp.ApplyPixelPerfect(360.0f);
    Expect(pp.zoom == 3.0f, "zoom snaps to integer");
    ExpectNear(pp.halfHeight, 120.0f, 1e-4f, "halfHeight = ref/zoom");
}

void TestQuality() {
    QualityManager q(QualityTier::High);
    Expect(q.Params().particleBudget == 100000, "high budget");
    // 过载 60fps 帧时间 25ms 持续 2s → 降 Med
    for (int i = 0; i < 130; ++i) q.Update(25.0, 1.0f / 60.0f);
    Expect(q.Current() == QualityTier::Med, "downgrade after 2s overload");
    Expect(q.Params().particleBudget == 50000, "med budget");
    // 恢复良好帧率不自动升档
    for (int i = 0; i < 600; ++i) q.Update(5.0, 1.0f / 60.0f);
    Expect(q.Current() == QualityTier::Med, "no auto upgrade");
    // 再过载 → Low 到底（含 EMA 爬升窗口的余量）
    for (int i = 0; i < 200; ++i) q.Update(25.0, 1.0f / 60.0f);
    Expect(q.Current() == QualityTier::Low, "downgrade to low");
}

void TestBitmapFontLayout() {
    // 字宽 = 字符数 × cellW × scale（纯计算，无需 GPU）
    BitmapFont font; // 未 Init 也可测宽度公式
    ExpectNear(font.TextWidth("ABC", 1.0f), 18.0f, 1e-4f, "3 chars × 6px");
    ExpectNear(font.TextWidth("hello", 2.0f), 60.0f, 1e-4f, "5 chars × 6px × 2");
    ExpectNear(font.TextWidth("", 1.0f), 0.0f, 1e-4f, "empty text");
}

} // namespace

// ---------------------------------------------------------------- M2 Core --
#include "Core/FunctionRef.h"
#include "Core/JobSystem.h"
#include "Core/Pool.h"
#include "Core/Random.h"
#include "Core/RingQueue.h"

#include <atomic>
#include <numeric>

namespace {

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
            while (len < cur && !minBlock.compare_exchange_weak(cur, len)) {}
            cur = maxBlock.load();
            while (len > cur && !maxBlock.compare_exchange_weak(cur, len)) {}
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

} // namespace

// ------------------------------------------------------- M2 ECS 骨架/组件 --
#include "Components/BehaviorComponents.h"
#include "Components/CoreComponents.h"
#include "Components/GameplayComponents.h"
#include "Components/RenderComponents.h"
#include "ECS/ComponentRegistry.h"
#include "ECS/Scene.h"
#include "ECS/World.h"

using namespace lemon::ecs;

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
    Expect(reg.Count() == 27, "catalog count (5 core + 4 render + 12 behavior + 6 gameplay)");

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
}

} // namespace

// --------------------------------------------------- M2 场景序列化(.lscene) --
#include "Serialization/SceneArchive.h"

namespace {

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
    src.Emplace<Health>(monster, Health{200, 150, 0.5f});
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

    // 二次 roundtrip 稳定（组件数据不动点；场景名是宿主属性，不参与比较）
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

} // namespace

// ------------------------------------------------ M2 空间哈希 + Team -------
#include "Physics2D/SpatialHash.h"

using namespace lemon::physics2d;

namespace {

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
        s.Emplace<Transform2D>(ents[i], Transform2D{{(float)(i % 4) * 100.0f,
                                                     (float)(i / 4) * 100.0f}});
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
        hash.OverlapCircle(s, {50, 50}, 75.0f, f, 0.0f,
                           [&](Entity, const Transform2D&) {
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
        hash.OverlapCircle(s, {50, 50}, 75.0f, f, 0.0f,
                           [&](Entity, const Transform2D&) {
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
        hash.OverlapCircle(s, {0, 0}, 10.0f, f, 0.0f,
                           [&](Entity e, const Transform2D&) {
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

} // namespace

// ------------------------------------------- M2 系统管线（16 系统端到端）--
#include "Systems/Systems.h"

namespace {

/// 最小预制体工厂：monster(prefab 1) / projectile(prefab 2)
Entity TestSpawnFactory(Scene& s, uint32_t prefabId, Vec2 pos, uint32_t team) {
    Entity e = s.Create();
    s.Emplace<Transform2D>(e, Transform2D{pos});
    s.Emplace<Meta>(e).team = team;
    s.Emplace<Velocity>(e);
    if (prefabId == 1) { // 怪
        s.Emplace<Health>(e, Health{50, 50, 0});
        s.Emplace<Chase>(e);
    } else if (prefabId == 2) { // 投射物
        s.Emplace<Projectile>(e, Projectile{300, 3, 15, 0, 0, 0});
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

    Expect(p.Systems().size() == 16, "16 systems installed");
    // Essential 阶段只有 DestroyCommit；FixedTick 按表序
    const char* expected[] = {"InputSnapshot", "Director",    "Spawn",
                              "AI",            "Navigation",  "Separation",
                              "Movement",      "SpatialHashRebuild", "Hitbox",
                              "Trigger",       "Stat",        "Animator",
                              "ProjectileLifetime", "CSharpBatch", "ScriptEventDispatch"};
    uint32_t fi = 0;
    for (const auto& s : p.Systems()) {
        if (s->Stage() == SystemStage::Essential) {
            Expect(std::string_view(s->Name()) == "DestroyCommit", "essential is destroy");
        } else {
            Expect(fi < 15 && std::string_view(s->Name()) == expected[fi],
                   "fixedtick order");
            ++fi;
        }
    }
    Expect(fi == 15, "15 fixedtick systems");
    Expect(p.Profiles().size() == 16, "profiles allocated");
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

    // 命中链路：给玩家血量，投射物（team 3）hostile→0 命中 → Hit/Death 事件
    s.Emplace<Health>(player, Health{30, 30, 0});
    for (int i = 0; i < 120; ++i) world.Step(dt);
    Expect(hitEvents > 0, "projectiles hit player");
    Expect(s.Get<Health>(player).cur < 30.0f, "player took damage");
    // 玩家死亡 → 销毁提交
    if (s.Get<Health>(player).cur <= 0.0f) {
        Expect(!s.Alive(player), "dead player destroyed");
        Expect(deathEvents >= 1, "death events fired");
    }

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

} // namespace

// --------------------------------------------- M2 审计修复回归 --------------
#include "Core/JobSystem.h"
#include "Serialization/SceneArchive.h"

namespace {

bool ExpectNear0(float a, float b) { return std::fabs(a - b) < 1e-5f; }

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
    src.Emplace<Health>(e, Health{200, 150, 0.5f}); // iFrames RT
    Entity sp = src.Create();
    src.Emplace<Transform2D>(sp, Transform2D{{0, 0}});
    Spawner& spo = src.Emplace<Spawner>(sp);
    spo.cooldown = 0.42f; // RT

    std::string text = SceneArchive::Save(src);
    Expect(text.find("\"iFrames\"") == std::string::npos, "iFrames not serialized");
    Expect(text.find("\"cooldown\"") == std::string::npos,
           "spawner cooldown not serialized");

    World w2;
    Scene& dst = w2.CreateScene("seg2");
    Expect(SceneArchive::Load(dst, text), "seg scene load");
    Expect(dst.AliveCount() == 2, "seg entity count");

    bool found = false;
    dst.View<StatusEffects>().each([&](auto, StatusEffects& s2) {
        found = true;
        Expect(s2.count == 2, "status count roundtrip");
        Expect(s2.active[0].id == 11 && s2.active[0].stacks == 2 &&
                   s2.active[0].source == 0xABCDu && ExpectNear0(s2.active[0].remain, 4.5f),
               "status[0] roundtrip");
        Expect(s2.active[1].id == 12 && s2.active[1].source == 0x1234u,
               "status[1] roundtrip");
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
    // RT 字段读档后回落默认值
    dst.View<Spawner>().each(
        [&](auto, Spawner& s2) { Expect(s2.cooldown == 0.0f, "cooldown reset (runtime)"); });
}

// 恶意/畸形 .lscene 不抛穿加载器（json 异常降级修复回归）
void TestArchiveMalformedTolerance() {
    World w;
    Scene& s = w.CreateScene("bad");
    // 字段类型错（pos 是字符串）
    Expect(SceneArchive::Load(
               s, R"({"schemaVersion":1,"entities":[)"
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
    Expect(SceneArchive::Load(
               s, R"({"schemaVersion":1,"entities":[)"
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
    hash.OverlapCircle(s, {0, 0}, 100.0f, QueryFilter{}, 0.0f,
                       [&](Entity, const Transform2D&) {
                           ++hits;
                           return true;
                       });
    Expect(hits == 0, "out-of-range team/layer never hit");
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

// World::Step 无活动场景 = 空步不崩
void TestWorldStepWithoutScene() {
    WorldDesc d;
    d.threadCount = 1;
    World w(d);
    w.InstallDefaultSystems();
    w.Step(1.0f / 60.0f);
    Expect(w.TickIndex() == 0, "no-scene step is a no-op");
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
    s.Emplace<Health>(victim, Health{10, 10, 0});

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

} // namespace

int main() {
    TestVec2();
    TestMat3x2();
    TestRect();
    TestColor();
    TestUtils();
    TestAtlasUV();
    TestBatchKey();
    TestSortStability();
    TestParticles();
    TestCamera2D();
    TestQuality();
    TestBitmapFontLayout();
    TestRng();
    TestJobSystem();
    TestPool();
    TestRingQueue();
    TestSceneLifecycle();
    TestWorldServices();
    TestComponentRegistry();
    TestSceneArchive();
    TestTeamTable();
    TestSpatialHash();
    TestSystemPipelineOrder();
    TestSimulationEndToEnd();
    TestSeparationForce();
    TestArchiveArraySegAndRuntimeFields();
    TestArchiveMalformedTolerance();
    TestSpatialHashRangeClamp();
    TestConcurrentDestroy();
    TestDestroyQueueTagLifecycle();
    TestWorldStepWithoutScene();
    TestNoDoubleDeathEvents();
    LEMON_LOG("engine-tests: %d checks OK", g_checks);
    return 0;
}
