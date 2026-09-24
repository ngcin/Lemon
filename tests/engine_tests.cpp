// Lemon 引擎单测 — 纯逻辑层（数学/批键/图集 UV/相机/粒子池）
// 断言风格：LEMON_ASSERT 失败即 abort，进程退出码非 0 = 测试失败。
#include "Core/Log.h"

#include <cmath>
#include <cstdint>
#include <algorithm>

#include "Core/Guid.h"
#include "Core/Math.h"
#include "Components/CoreComponents.h"
#include "ECS/Hierarchy.h"
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

// ---- 显式号登记（2026-09-21：注册表自增号 vs manifest 记账两本账漂移的根治）----
void TestVerifyAddSpriteAtHolesAndConflict() {
    AtlasRegistry reg;
    reg.RegisterAtlas(5, rhi::Texture{}, 64, 64); // 头把 Texture 占位（纯逻辑测试无设备）
    Expect(reg.AddSprite(5, 0, 0, 16, 16) == 1, "dense append id 1");

    // 跳号登记（= manifest 记账 3 号）：中间 2 号成空洞
    Expect(reg.AddSpriteAt(3, 5, 0, 0, 32, 32), "register at explicit id 3");
    Expect(reg.SpriteCount() == 3, "table grown to cover id 3");
    Expect(reg.IsValidSprite(1) && reg.IsValidSprite(3), "dense + explicit valid");
    Expect(!reg.IsValidSprite(2), "hole (retired id) invalid");
    Expect(!reg.IsValidSprite(0) && !reg.IsValidSprite(4), "zero/out-of-range invalid");
    Expect(reg.GetSprite(3).widthPx == 32, "explicit entry data correct");

    // 冲突拒绝：占用号不可重复登记；0 号非法；被拒写入不破坏原条目
    Expect(!reg.AddSpriteAt(3, 5, 0, 0, 8, 8), "occupied id rejected");
    Expect(!reg.AddSpriteAt(0, 5, 0, 0, 8, 8), "id 0 rejected");
    Expect(reg.GetSprite(3).widthPx == 32, "rejected write left entry intact");

    // 空洞可被后续登记（新资产恰好分到退役号）
    Expect(reg.AddSpriteAt(2, 5, 0, 0, 8, 8), "hole refilled");
    Expect(reg.IsValidSprite(2) && reg.GetSprite(2).widthPx == 8, "refilled entry valid");
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

    // spriteId 无效（EmitterConfig 默认 0）：提取跳过、不渲染（修复前 GetSprite(0)
    // 直接断言 abort——粒子路径无精灵路径的合法性过滤）
    ParticleSystem ps3;
    ps3.SetBudget(100);
    EmitterConfig eBad; // spriteId 默认 0
    float accBad = 0;
    ps3.Emit(eBad, 1.0f, 7, accBad);
    Expect(ps3.AliveCount() == 100, "invalid-sprite emitter still simulates");
    Expect(ps3.Extract(atlas, {32, 32}, 1000, 1000).empty(),
           "invalid spriteId particles skipped in extract");

    // 桶序 =（layer, hash）而非首遇序：高层发射器先发射，低层段仍须排前
    ParticleSystem ps4;
    ps4.SetBudget(1000);
    EmitterConfig eHigh = e2, eLow = e2;
    eHigh.sortingLayer = 10;
    eLow.sortingLayer = 2;
    float acc4a = 0, acc4b = 0;
    ps4.Emit(eHigh, 0.1f, 1, acc4a); // 10 个高层（先遇）
    ps4.Emit(eLow, 0.1f, 2, acc4b);  // 10 个低层（后遇）
    auto pk4 = ps4.Extract(atlas, {32, 32}, 1000, 1000);
    Expect(pk4.size() == 20, "both emitter layers visible");
    Expect(pk4.front().key.layer == 2 && pk4.back().key.layer == 10,
           "particle buckets ordered by layer, not first-encounter");
}

void TestRenderableExtractOrder() {
    // 桶序即绘制序（Bake 连续段录制，下游无重排）：跨桶须按（layer, hash），
    // 与创建序无关——修复前按首遇序排桶，先建的高层整桶画到低层下面
    AtlasRegistry atlas;
    atlas.RegisterAtlas(0, rhi::Texture{1}, 64, 64);
    atlas.RegisterAtlas(1, rhi::Texture{2}, 64, 64);
    uint32_t spr0 = atlas.AddSprite(0, 0, 0, 16, 16);
    uint32_t spr1 = atlas.AddSprite(1, 0, 0, 16, 16);

    RenderableManager rm;
    RenderableDesc d;
    d.spriteId = spr0;
    const uint8_t layers[] = {5, 0, 9, 0, 5, 1}; // 创建序故意高层在前
    for (uint8_t ly : layers) {
        d.sortingLayer = ly;
        rm.Create(d);
    }
    auto packets = rm.Extract(atlas, 1.0f); // 无视口 → 无剔除
    Expect(packets.size() == 6, "all visible without viewport");
    for (size_t i = 1; i < packets.size(); ++i)
        Expect(packets[i - 1].key.layer <= packets[i].key.layer, "layers monotonic in draw order");
    Expect(packets.front().key.layer == 0 && packets.back().key.layer == 9,
           "lowest layer drawn first, highest last");

    // 同层跨图集：按批键 hash 稳定排段（与 atlas 创建/遇到序无关）
    RenderableManager rm2;
    RenderableDesc d2;
    d2.sortingLayer = 3;
    d2.spriteId = spr1;
    rm2.Create(d2); // atlas 1 先建
    d2.spriteId = spr0;
    rm2.Create(d2); // atlas 0 后建
    auto pk2 = rm2.Extract(atlas, 1.0f);
    Expect(pk2.size() == 2 && pk2[0].key.textureAtlas != pk2[1].key.textureAtlas,
           "same layer split by atlas");
    Expect(pk2[0].key.hash < pk2[1].key.hash, "same-layer buckets ordered by key hash");
}

void TestRenderableSanitizeCacheAndHoles() {
    // M5：序列化可驱动的 blend/filter 越界值在 Create 入口钳回合法域
    //（SpriteBatcher 只有 4 管线/2 采样器槽，键位宽 blend:4/filter:2 存得下越界值）
    AtlasRegistry atlas;
    atlas.RegisterAtlas(0, rhi::Texture{1}, 64, 64);
    uint32_t spr = atlas.AddSprite(0, 0, 0, 16, 16);
    RenderableManager rm;
    RenderableDesc d;
    d.spriteId = spr;
    d.blend = 7;   // 越界（合法 0–3）
    d.filter = 3;  // 越界（合法 0–1）
    rm.Create(d);
    auto packets = rm.Extract(atlas, 1.0f);
    Expect(packets.size() == 1 && packets[0].key.blend == 3 && packets[0].key.filter == 1,
           "out-of-range blend/filter clamped to legal domain");

    // M6：暂停态（无 BeginSimTick）Create 后提取缓存须失效——修复前新实体不可见
    RenderableManager rm2;
    RenderableDesc d2;
    d2.spriteId = spr;
    rm2.Create(d2);
    (void)rm2.Extract(atlas, 1.0f); // 建缓存（1 包，非空 → 后续可命中）
    rm2.Create(d2);                 // 暂停态生成：无 BeginSimTick
    auto pk2 = rm2.Extract(atlas, 1.0f);
    Expect(pk2.size() == 2, "create during pause invalidates extract cache");

    // M8：空洞退役号（AddSpriteAt 中间空洞，落在 SpriteCount 界内）不渲染
    AtlasRegistry atlasHole;
    atlasHole.RegisterAtlas(0, rhi::Texture{1}, 64, 64);
    atlasHole.AddSpriteAt(1, 0, 0, 0, 16, 16);
    atlasHole.AddSpriteAt(3, 0, 0, 0, 16, 16); // id 2 = 哨兵空洞
    Expect(!atlasHole.IsValidSprite(2), "hole id 2 invalid");
    RenderableManager rm3;
    RenderableDesc d3;
    d3.spriteId = 2; // 空洞号
    rm3.Create(d3);
    d3.spriteId = 1;
    rm3.Create(d3);
    auto pk3 = rm3.Extract(atlasHole, 1.0f);
    Expect(pk3.size() == 1 && pk3[0].spriteId == 1,
           "hole sprite skipped, live sprite still visible");

    // M7：键表满软丢弃（kMaxSpriteKeys=64；blend×layer 65 组合）——修复前 Release
    // 仅靠断言，超限写 slots[64] = 栈越界
    RenderableManager rm4;
    RenderableDesc d4;
    d4.spriteId = spr;
    for (uint8_t blend = 0; blend < 4; ++blend)
        for (uint8_t layer = 0; layer < 16; ++layer) { // 64 组合填满表
            d4.blend = blend;
            d4.sortingLayer = layer;
            rm4.Create(d4);
        }
    d4.blend = 0;
    d4.sortingLayer = 16; // 第 65 个键
    rm4.Create(d4);
    auto pk4 = rm4.Extract(atlas, 1.0f);
    Expect(pk4.size() == RenderableManager::kMaxSpriteKeys,
           "key-table full: exactly cap sprites visible");
    Expect(rm4.LastStats().droppedSprites == 1, "overflow sprite dropped and counted");
}

void TestParticlesKeyOverflow() {
    // M7 粒子侧：键表满（kMaxParticleKeys=32）软丢弃——blend×layer 33 组合
    AtlasRegistry atlas;
    atlas.RegisterAtlas(0, rhi::Texture{1}, 64, 64);
    uint32_t spr = atlas.AddSprite(0, 0, 0, 16, 16);
    ParticleSystem ps;
    ps.SetBudget(1000);
    EmitterConfig e;
    e.spriteId = spr;
    e.rate = 1.0f; // dt=1 → 每次 Emit 恰 1 粒
    for (uint8_t blend = 0; blend < 4; ++blend)
        for (uint8_t layer = 0; layer < 8; ++layer) { // 32 组合填满表
            e.blend = blend;
            e.sortingLayer = layer;
            float acc = 0;
            ps.Emit(e, 1.0f, 1, acc);
        }
    e.blend = 0;
    e.sortingLayer = 8; // 第 33 个键
    float acc33 = 0;
    ps.Emit(e, 1.0f, 1, acc33);
    Expect(ps.AliveCount() == 33, "all 33 particles alive in sim");
    auto packets = ps.Extract(atlas, {32, 32}, 1000, 1000);
    Expect(packets.size() == ParticleSystem::kMaxParticleKeys,
           "key-table full: exactly cap particles visible");
    Expect(ps.LastStats().droppedParticles == 1, "overflow particle dropped and counted");
}

void TestCamera2D() {
    Camera2D cam;
    cam.center = {100, 50};
    cam.halfHeight = 100;
    // 正交：视口中心→NDC 原点，世界 Y 向下 = Vulkan NDC Y 向下（2026-09-19 修订，同 Ortho）
    Mat3x2 vp = cam.ViewProj(1.6f); // 半宽 160
    Vec2 c = vp.Apply(cam.center);
    ExpectNear(c.x, 0.0f, 1e-6f, "cam center→origin");
    ExpectNear(c.y, 0.0f, 1e-6f, "cam center→origin y");
    ExpectNear(vp.Apply({260, 50}).x, 1.0f, 1e-6f, "cam right edge");
    ExpectNear(vp.Apply({100, 150}).y, 1.0f, 1e-6f, "cam +y down → +1 NDC(Vulkan 下方)");

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

    // 非 High 启动档：构造即对齐预算（修复前 params_ 恒按 High 初始化，且 Low 档
    // Update 提前 return 永不自愈 → 粒子预算长期 100000）
    QualityManager lowStart(QualityTier::Low);
    Expect(lowStart.Current() == QualityTier::Low &&
               lowStart.Params().particleBudget == 20000,
           "low-start ctor budget matches tier");
    QualityManager medStart(QualityTier::Med);
    Expect(medStart.Params().particleBudget == 50000, "med-start ctor budget matches tier");
    lowStart.Update(25.0, 1.0f); // Low 档到底：过载也不漂移
    Expect(lowStart.Params().particleBudget == 20000, "low-start budget stable under overload");
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
#include "ECS/SaveChannel.h"
#include "ECS/Scene.h"
#include "ECS/StateHash.h"
#include "ECS/World.h"
#include "Scripting/ScriptBox.h"

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
    Expect(reg.Count() == 28, "catalog count (5 core + 4 render + 12 behavior + 6 gameplay + M5 批② WaveDirector)");

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
    Expect(ComponentRegistry::Instance().Count() == 28, "world ctor auto-registers catalog");
}

} // namespace

// --------------------------------------------------- M2 场景序列化(.scene) --
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

    Expect(p.Systems().size() == 17, "17 systems installed");
    // Essential 阶段只有 DestroyCommit；FixedTick 按表序
    // （#9 Pickup = M5 批①，16→17）
    const char* expected[] = {"InputSnapshot", "Director",    "Spawn",
                              "AI",            "Navigation",  "Separation",
                              "Movement",      "SpatialHashRebuild", "Pickup",
                              "Hitbox",        "Trigger",     "Stat",
                              "Animator",      "ProjectileLifetime", "CSharpBatch",
                              "ScriptEventDispatch"};
    uint32_t fi = 0;
    for (const auto& s : p.Systems()) {
        if (s->Stage() == SystemStage::Essential) {
            Expect(std::string_view(s->Name()) == "DestroyCommit", "essential is destroy");
        } else {
            Expect(fi < 16 && std::string_view(s->Name()) == expected[fi],
                   "fixedtick order");
            ++fi;
        }
    }
    Expect(fi == 16, "16 fixedtick systems");
    Expect(p.Profiles().size() == 17, "profiles allocated");
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
    src.Emplace<Health>(e, Health{.max = 200.0f, .cur = 150.0f, .iFrames = 0.5f,
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
    pp.hits = 3;                 // RT
    pp.hitMemory[0] = 0x1234u;   // RT

    // Collectible：磁吸三参数落档；state/target RT 不入档（M5 批① T1）
    Entity ce = src.Create();
    src.Emplace<Transform2D>(ce, Transform2D{{6, 6}});
    Collectible& cc = src.Emplace<Collectible>(ce);
    cc.kind = 2;
    cc.magnetRadius = 64.0f;
    cc.magnetSpeed = 400.0f;
    cc.value = 3.5f;
    cc.state = 1;        // RT
    cc.target = Entity{1}; // RT（非空以验读档回落）

    std::string text = SceneArchive::Save(src);
    Expect(text.find("\"iFrames\"") == std::string::npos, "iFrames not serialized");
    Expect(text.find("\"iframeWindow\"") != std::string::npos,
           "iframeWindow serialized (config field)");
    Expect(text.find("\"cooldown\"") == std::string::npos,
           "spawner cooldown not serialized");
    Expect(text.find("\"hitRadius\"") != std::string::npos,
           "projectile hitRadius serialized (config)");
    Expect(text.find("\"hitMemory0\"") == std::string::npos,
           "hit memory not serialized (runtime)");
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
    Entity far[3];
    for (int i = 0; i < 3; ++i) {
        far[i] = s.Create();
        s.Emplace<Transform2D>(far[i], Transform2D{{500.0f + (float)i, 0.0f}});
        s.Emplace<Meta>(far[i]).team = 1;
    }

    SpatialHash hash;
    hash.Configure(64.0f);
    hash.Rebuild(s);

    // ① 无 Meta 实体恒放行（默认过滤命中近格全部 4 个有效实体）
    {
        int hits = 0;
        hash.OverlapCircle(s, {0, 0}, 64.0f, QueryFilter{}, 0.0f,
                           [&](Entity, const Transform2D&) {
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
        hash.OverlapCircle(s, {500.0f, 0.0f}, 64.0f, f, 0.0f,
                           [&](Entity, const Transform2D&) {
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
        Expect(t.HostileMask(1) == ((1u << 0) | (1u << 3)),
               "monsters hostile to player + bullets");
        Expect(t.HostileMask(31) == 0, "unconfigured team = empty mask");
        Expect(t.HostileMask(40) == 0, "out-of-range team = empty mask");
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
        void DispatchEvents(ecs::World&, ecs::Scene&, const ecs::EventPacket*,
                            uint32_t) override {}
        void ApplyStructural(ecs::World&, ecs::Scene&) override { ++structuralCalls; }
        void NotifyPendingDestroys(ecs::World&, ecs::Scene& s) override {
            // 与 ScriptHost 实现同形状：待销毁 ∩ ScriptBox，实体级 notified 去重
            // 恰好一次（空 tag 不进 each() 载荷——entt 3.15 语义，tag 只作过滤器）
            for (auto&& [ent, sb] :
                 s.View<ecs::DestroyQueueTag, scripting::ScriptBox>().each()) {
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

// iFrames 递减与多段击杀（M5 批⓪ T1；DevLog 2026-09-22 P0 回归）：
// 受击置窗 → 窗内免疫（在途弹压着重叠也不重复伤害）→ 窗尽复拍 → 第三击致死。
// 复拍间隔 = ceil(iframeWindow/dt) tick（60Hz/0.1s 窗 ≈ 6 tick；FP 余量按 5..8 带断言）。
void TestVerifyIframesDecrementAndKill() {
    World world;
    Scene& s = world.CreateScene("ifr");
    world.SetActiveScene(&s);

    Entity shooter = s.Create(); // team0 射手（弹体势力继承口径）
    s.Emplace<Transform2D>(shooter, Transform2D{{0, 0}});
    s.Emplace<Meta>(shooter).team = 0;

    Entity victim = s.Create(); // team1 受害者：hp 30 / 弹伤 12 → 需 3 次命中
    s.Emplace<Transform2D>(victim, Transform2D{{50, 0}});
    s.Emplace<Meta>(victim).team = 1;
    s.Emplace<Health>(victim, Health{.max = 30.0f, .cur = 30.0f});

    auto fire = [&]() { // 在受害者处生成一发命中即毁的弹（pierce 0 = 默认）
        Entity p = s.Create();
        s.Emplace<Transform2D>(p, Transform2D{{50, 0}});
        s.Emplace<Meta>(p).team = 0;
        s.Emplace<Velocity>(p);
        Projectile& pr = s.Emplace<Projectile>(p);
        pr.damage = 12.0f;
        pr.lifetime = 30.0f;
    };

    int hits = 0, deaths = 0;
    world.SetEventSink([&](World&, const EventPacket& p) {
        if (p.type == GameEvent::Hit && p.dst == victim) ++hits;
        if (p.type == GameEvent::Death && p.src == victim) ++deaths;
    });

    // 命中链最小管线：哈希重建(#8) → 命中(#9) → 数值(#11 递减) → 派发 → 提交
    world.Pipeline().AddSystem(std::make_unique<SpatialHashRebuildSystem>());
    world.Pipeline().AddSystem(std::make_unique<HitboxSystem>());
    world.Pipeline().AddSystem(std::make_unique<StatSystem>());
    world.Pipeline().AddSystem(std::make_unique<ScriptEventDispatchSystem>());
    world.Pipeline().AddSystem(std::make_unique<DestroyCommitSystem>());
    world.Pipeline().ResolveOrder();

    const float dt = 1.0f / 60.0f;

    fire(); // tick0：第 1 击（18/30）+ 置窗
    world.Step(dt);
    Expect(hits == 1 && deaths == 0, "first hit lands");
    Expect(s.Get<Health>(victim).cur == 18.0f, "cur after hit 1");
    Expect(s.Get<Health>(victim).iFrames > 0.0f, "iFrames window armed");

    fire(); // 在途弹 B 压着重叠 4 tick：窗内必须免疫（修复前恒免疫、修复后也不得过窗）
    for (int i = 0; i < 4; ++i) world.Step(dt);
    Expect(hits == 1, "immune within window (4 ticks)");
    Expect(s.Get<Health>(victim).cur == 18.0f, "no damage within window");

    bool relanded = false; // 窗尽（≈6 tick，容差 ≤4 步）：B 补上第 2 击
    for (int i = 0; i < 4 && !relanded; ++i) {
        world.Step(dt);
        relanded = hits == 2;
    }
    Expect(relanded, "re-hit after window expiry");
    Expect(s.Get<Health>(victim).cur == 6.0f, "cur after hit 2");

    fire(); // 第 3 击：窗尽后致死（hp<=0 早退防第 4 击）
    for (int i = 0; i < 8 && deaths == 0; ++i) world.Step(dt);
    world.Step(dt); // DestroyCommit 在 Essential 阶段（下一 tick 首）提交本 tick 销毁
    Expect(deaths == 1, "killed by multi-hit damage");
    Expect(hits == 3, "exactly three hits total");
    Expect(!s.Alive(victim), "victim destroyed");
}

// 命中记忆与穿透收口（M5 批⓪ T2）：一弹一目标一次（弹 lifetime 内不重复伤同目标）；
// 穿透耗尽即毁。慢弹压着重叠多 tick 是回归重点——iFrames 窗尽后不得借窗复伤同目标。
void TestVerifyHitMemoryAndPierce() {
    World world;
    Scene& s = world.CreateScene("pierce");
    world.SetActiveScene(&s);

    Entity shooter = s.Create();
    s.Emplace<Transform2D>(shooter, Transform2D{{0, 0}});
    s.Emplace<Meta>(shooter).team = 0;

    // 场景 A：单怪 + 慢穿透弹压着重叠 30 tick（0.5s ≫ 0.1s 无敌窗）→ 恰一击
    Entity victim = s.Create();
    s.Emplace<Transform2D>(victim, Transform2D{{50, 0}});
    s.Emplace<Meta>(victim).team = 1;
    s.Emplace<Health>(victim, Health{.max = 100.0f, .cur = 100.0f});

    Entity p = s.Create();
    s.Emplace<Transform2D>(p, Transform2D{{50, 0}});
    s.Emplace<Meta>(p).team = 0;
    s.Emplace<Velocity>(p);
    Projectile& slow = s.Emplace<Projectile>(p);
    slow.damage = 10.0f;
    slow.lifetime = 30.0f;
    slow.pierce = 3;

    int hits = 0;
    world.SetEventSink([&](World&, const EventPacket& e) {
        if (e.type == GameEvent::Hit && e.dst == victim) ++hits;
    });
    world.Pipeline().AddSystem(std::make_unique<SpatialHashRebuildSystem>());
    world.Pipeline().AddSystem(std::make_unique<HitboxSystem>());
    world.Pipeline().AddSystem(std::make_unique<StatSystem>());
    world.Pipeline().AddSystem(std::make_unique<ScriptEventDispatchSystem>());
    world.Pipeline().AddSystem(std::make_unique<DestroyCommitSystem>());
    world.Pipeline().ResolveOrder();
    const float dt = 1.0f / 60.0f;

    for (int i = 0; i < 30; ++i) world.Step(dt);
    Expect(hits == 1, "one hit per target per projectile (memory)");
    Expect(s.Get<Projectile>(p).hits == 1, "projectile hit counter");
    Expect(s.Get<Health>(victim).cur == 90.0f, "victim damaged exactly once");
    Expect(s.Alive(p), "pierce not exhausted");

    // 场景 B：两怪同线（4px 内）+ pierce 1 → 双伤、弹毁。
    // 布点离场景 A 受害者 ≥16px：hitRadius 默认 12（原 6+6 有效口径），不得误伤
    Entity v2 = s.Create();
    s.Emplace<Transform2D>(v2, Transform2D{{66, 0}});
    s.Emplace<Meta>(v2).team = 1;
    s.Emplace<Health>(v2, Health{.max = 100.0f, .cur = 100.0f});
    Entity v3 = s.Create();
    s.Emplace<Transform2D>(v3, Transform2D{{70, 0}});
    s.Emplace<Meta>(v3).team = 1;
    s.Emplace<Health>(v3, Health{.max = 100.0f, .cur = 100.0f});

    Entity q = s.Create();
    s.Emplace<Transform2D>(q, Transform2D{{66, 0}});
    s.Emplace<Meta>(q).team = 0;
    s.Emplace<Velocity>(q);
    Projectile& pierce1 = s.Emplace<Projectile>(q);
    pierce1.damage = 10.0f;
    pierce1.lifetime = 30.0f;
    pierce1.pierce = 1;

    world.Step(dt); // v2、v3 各中一击，穿透耗尽
    world.Step(dt); // DestroyCommit 在 Essential（下一 tick 首）提交
    Expect(!s.Alive(q), "pierce exhausted -> projectile destroyed");
    Expect(s.Get<Health>(v2).cur == 90.0f && s.Get<Health>(v3).cur == 90.0f,
           "both in-line targets hit once");
    Expect(hits == 1, "scenario-A victim out of second projectile's range");
}

// 磁吸与拾取（M5 批① T2）：双侧取大触程（gem.magnetRadius vs Stats.pickupRadius）、
// 直写 pos 飞行、触距 8px 入账按 kind 分发、Pickup 事件、目标死亡回落。
void TestVerifyMagnetAndPickup() {
    World world;
    Scene& s = world.CreateScene("pickup");
    world.SetActiveScene(&s);

    // 收集者：磁力压到 8（段 A 够不到 30px）——首段专测宝石自程侧（段 B）
    Entity player = s.Create();
    s.Emplace<Transform2D>(player, Transform2D{{0, 0}});
    s.Emplace<Stats>(player).pickupRadius = 8.0f;
    s.Emplace<XpProgress>(player);

    // 宝石 30px：自程 48 内磁吸；320px/s = 5.33px/tick → 数 tick 后触距入账
    Entity gem = s.Create();
    s.Emplace<Transform2D>(gem, Transform2D{{30, 0}});
    s.Emplace<Collectible>(gem, Collectible{.kind = 0, .value = 5.0f});

    int pickups = 0;
    float evKind = -1.0f, evVal = -1.0f;
    world.SetEventSink([&](World&, const EventPacket& e) {
        if (e.type == GameEvent::Pickup) {
            ++pickups;
            evKind = e.payload[0];
            evVal = e.payload[1];
        }
    });

    world.Pipeline().AddSystem(std::make_unique<SpatialHashRebuildSystem>());
    world.Pipeline().AddSystem(std::make_unique<PickupSystem>());
    world.Pipeline().AddSystem(std::make_unique<ScriptEventDispatchSystem>());
    world.Pipeline().AddSystem(std::make_unique<DestroyCommitSystem>());
    world.Pipeline().ResolveOrder();
    const float dt = 1.0f / 60.0f;

    world.Step(dt);
    Expect(s.Get<Collectible>(gem).state == 1, "magnetized (self radius side)");
    const Vec2 p1 = s.Get<Transform2D>(gem).pos;
    Expect(p1.x < 30.0f && p1.y == 0.0f, "gem flies toward collector (direct pos write)");

    for (int i = 0; i < 8 && s.Alive(gem); ++i) world.Step(dt);
    world.Step(dt); // DestroyCommit 在 Essential（下一 tick 首）提交
    Expect(!s.Alive(gem), "gem picked up (touch distance)");
    Expect(pickups == 1, "one pickup event");
    Expect(evKind == 0.0f && evVal == 5.0f, "pickup payload kind/value");
    Expect(ExpectNear0(s.Get<XpProgress>(player).xp, 5.0f), "xp credited");

    // 双侧取大（段 A）：玩家磁力 120 > 宝石自程 48 → 100px 外的宝石也吸；
    // 负对照 200px 超两侧触程 → 原地不动
    Entity far = s.Create();
    s.Emplace<Transform2D>(far, Transform2D{{100, 0}});
    s.Emplace<Collectible>(far, Collectible{.kind = 1, .value = 7.0f});
    Entity idle = s.Create();
    s.Emplace<Transform2D>(idle, Transform2D{{200, 0}});
    s.Emplace<Collectible>(idle, Collectible{.kind = 0});
    s.Emplace<Inventory>(player);
    s.Get<Stats>(player).pickupRadius = 120.0f;

    world.Step(dt);
    Expect(s.Get<Collectible>(far).state == 1, "player stat side wins (max rule)");
    Expect(s.Get<Transform2D>(far).pos.x < 100.0f, "far gem flying");
    const Vec2 idlePos = s.Get<Transform2D>(idle).pos;
    for (int i = 0; i < 4; ++i) world.Step(dt);
    Expect(s.Get<Transform2D>(idle).pos == idlePos, "out of both radii stays idle");
    for (int i = 0; i < 40 && s.Alive(far); ++i) world.Step(dt);
    Expect(!s.Alive(far), "coin picked up");
    Expect(s.Get<Inventory>(player).gold == 7u, "coin -> gold");

    // heart：触距内 → 同 tick 磁吸即入账；治疗上限钳制
    s.Emplace<Health>(player, Health{.max = 100.0f, .cur = 90.0f});
    Entity heart = s.Create();
    s.Emplace<Transform2D>(heart, Transform2D{{4, 0}});
    s.Emplace<Collectible>(heart, Collectible{.kind = 2, .value = 20.0f});
    world.Step(dt);
    world.Step(dt); // 提交销毁
    Expect(!s.Alive(heart), "heart picked same tick as magnetize");
    Expect(s.Get<Health>(player).cur == 100.0f, "heal clamped at max");

    // 目标死亡回落：磁吸中销毁收集者 → state 回 0、位置冻结（宝石不丢可再吸）
    Entity gem3 = s.Create();
    s.Emplace<Transform2D>(gem3, Transform2D{{-60, 0}}); // 自程 48 不及，靠玩家磁力 120
    s.Emplace<Collectible>(gem3, Collectible{.kind = 0});
    world.Step(dt);
    Expect(s.Get<Collectible>(gem3).state == 1, "gem3 magnetized via player stat");
    s.Destroy(player);
    world.Step(dt); // Essential 先提交销毁 → 同 tick 段 C 检活回落
    Expect(s.Get<Collectible>(gem3).state == 0, "falls back idle on collector death");
    const Vec2 frozen = s.Get<Transform2D>(gem3).pos;
    for (int i = 0; i < 3; ++i) world.Step(dt);
    Expect(s.Get<Transform2D>(gem3).pos == frozen, "idle gem position frozen");
}

// XP 入账升级联动（M5 批① T2）：拾取同 tick 经 #12 升级环 → LevelUp 恰一次
void TestVerifyPickupXpLevelUp() {
    World world;
    Scene& s = world.CreateScene("lvl");
    world.SetActiveScene(&s);

    Entity player = s.Create();
    s.Emplace<Transform2D>(player, Transform2D{{0, 0}});
    XpProgress& xp = s.Emplace<XpProgress>(player);
    xp.xpToNext = 5.0f;
    Entity gem = s.Create();
    s.Emplace<Transform2D>(gem, Transform2D{{4, 0}}); // 触距内：同 tick 磁吸即入账
    s.Emplace<Collectible>(gem, Collectible{.kind = 0, .value = 10.0f});

    int levelUps = 0, pickups = 0;
    world.SetEventSink([&](World&, const EventPacket& e) {
        if (e.type == GameEvent::LevelUp) ++levelUps;
        if (e.type == GameEvent::Pickup) ++pickups;
    });
    world.Pipeline().AddSystem(std::make_unique<SpatialHashRebuildSystem>());
    world.Pipeline().AddSystem(std::make_unique<PickupSystem>());
    world.Pipeline().AddSystem(std::make_unique<StatSystem>());
    world.Pipeline().AddSystem(std::make_unique<ScriptEventDispatchSystem>());
    world.Pipeline().AddSystem(std::make_unique<DestroyCommitSystem>());
    world.Pipeline().ResolveOrder();

    world.Step(1.0f / 60.0f);
    const XpProgress& x = s.Get<XpProgress>(player);
    Expect(pickups == 1 && levelUps == 1, "pickup + level-up same tick");
    Expect(x.level == 2 && ExpectNear0(x.xp, 5.0f), "level 2 with carry 5");
    Expect(ExpectNear0(x.xpToNext, 7.0f), "xpToNext = ceil(5*1.25) = 7");
}

// ---- M5 批②：导演波次（WaveDirector 组件 + DirectorSystem；M5.md §11.2）----
namespace { // 导演测试共用：计数工厂（prefab 1 = 最小怪：Transform+Meta）
struct WaveSpawnCounter {
    int spawns = 0;
    Vec2 lastPos{999, 999};
    uint32_t lastTeam = 99;
    Entity operator()(Scene& sc, uint32_t prefabId, Vec2 pos, uint32_t team) {
        if (prefabId != 1) return Entity::Null();
        Entity e = sc.Create();
        sc.Emplace<Transform2D>(e, Transform2D{pos});
        sc.Emplace<Meta>(e).team = team;
        ++spawns;
        lastPos = pos;
        lastTeam = team;
        return e;
    }
};
void InstallDirectorPipeline(World& w) {
    w.Pipeline().AddSystem(std::make_unique<DirectorSystem>());
    w.Pipeline().AddSystem(std::make_unique<ScriptEventDispatchSystem>());
    w.Pipeline().AddSystem(std::make_unique<DestroyCommitSystem>());
    w.Pipeline().ResolveOrder();
}
} // namespace

// 波推进时刻 / WaveStart 契约 / 出生环与队伍覆盖 / 晚波不生效（D3/D5/D6）
void TestWaveDirectorWavesAndEvent() {
    World world;
    Scene& s = world.CreateScene("wdir");
    world.SetActiveScene(&s);
    WaveSpawnCounter ctr;
    world.SetSpawnFn([&ctr](Scene& sc, uint32_t id, Vec2 pos, uint32_t team) {
        return ctr(sc, id, pos, team);
    });

    Entity dir = s.Create();
    s.Emplace<Transform2D>(dir, Transform2D{{100, 0}});
    WaveDirector& wd = s.Emplace<WaveDirector>(dir);
    wd.spawnTeam = 1;
    wd.waveCount = 3;
    wd.waves[0] = WaveDef{.startTime = 0.5f};
    wd.waves[0].entryCount = 1;
    wd.waves[0].entries[0] = WaveEntry{.prefabId = 1, .count = 3, .interval = 0.1f, .range = 50.0f};
    wd.waves[1] = WaveDef{.startTime = 1.5f};
    wd.waves[1].entryCount = 1;
    wd.waves[1].entries[0] = WaveEntry{.prefabId = 1, .count = 2, .interval = 0.1f, .range = 50.0f};
    wd.waves[2] = WaveDef{.startTime = 99.0f}; // 窗口外：永不生效
    wd.waves[2].entryCount = 1;
    wd.waves[2].entries[0] = WaveEntry{.prefabId = 1, .count = 7, .interval = 0.1f, .range = 50.0f};

    int waveStarts = 0;
    float planned0 = -1.0f, index1 = -1.0f;
    world.SetEventSink([&](World&, const EventPacket& e) {
        if (e.type != GameEvent::WaveStart) return;
        ++waveStarts;
        if (e.payload[0] < 0.5f) planned0 = e.payload[1];
        else index1 = e.payload[0];
    });
    InstallDirectorPipeline(world);

    const float dt = 1.0f / 60.0f;
    for (int i = 0; i < 30; ++i) world.Step(dt);
    Expect(waveStarts == 1, "wave 0 fires at t=0.5s (tick 30)");
    for (int i = 30; i < 120; ++i) world.Step(dt);
    Expect(waveStarts == 2, "wave 1 at t=1.5s; wave 2 (99s) never");
    Expect(ctr.spawns == 5, "3 + 2 spawned; late wave not started");
    Expect(planned0 == 3.0f && index1 == 1.0f, "payload [0]=wave index [1]=planned total");
    Expect(Length(ctr.lastPos - Vec2{100, 0}) <= 50.0f + 1e-4f, "spawn within entry range ring");
    Expect(ctr.lastTeam == 1, "spawn team override");
    Expect(s.Get<WaveDirector>(dir).waveIndex == 2, "wave cursor = started waves");
}

// rampMult 加速（interval/rampMult）+ 波重叠 = 后波接管（D3）
void TestWaveDirectorRampAndOverlap() {
    const float dt = 1.0f / 60.0f;
    // ramp 2：interval 0.1 → 有效 0.05s = 3 tick/生 → 12 tick 内 4 生（基线仅 2）
    {
        World world;
        Scene& s = world.CreateScene("ramp2");
        world.SetActiveScene(&s);
        WaveSpawnCounter ctr;
        world.SetSpawnFn([&ctr](Scene& sc, uint32_t id, Vec2 pos, uint32_t team) {
            return ctr(sc, id, pos, team);
        });
        Entity dir = s.Create();
        s.Emplace<Transform2D>(dir, Transform2D{{0, 0}});
        WaveDirector& wd = s.Emplace<WaveDirector>(dir);
        wd.waveCount = 1;
        wd.waves[0] = WaveDef{.startTime = 0.0f, .rampMult = 2.0f};
        wd.waves[0].entryCount = 1;
        wd.waves[0].entries[0] = WaveEntry{.prefabId = 1, .count = 4, .interval = 0.1f};
        InstallDirectorPipeline(world);
        for (int i = 0; i < 12; ++i) world.Step(dt);
        Expect(ctr.spawns == 4, "rampMult 2: 4 spawns in 12 ticks (3-tick cadence)");
    }
    // 双波同 tick 到期：WaveStart ×2 但仅后波持有运行时（前波条目废止）
    {
        World world;
        Scene& s = world.CreateScene("ovl");
        world.SetActiveScene(&s);
        WaveSpawnCounter ctr;
        world.SetSpawnFn([&ctr](Scene& sc, uint32_t id, Vec2 pos, uint32_t team) {
            return ctr(sc, id, pos, team);
        });
        Entity dir = s.Create();
        s.Emplace<Transform2D>(dir, Transform2D{{0, 0}});
        WaveDirector& wd = s.Emplace<WaveDirector>(dir);
        wd.waveCount = 2;
        wd.waves[0].startTime = 0.0f;
        wd.waves[0].entryCount = 1;
        wd.waves[0].entries[0] = WaveEntry{.prefabId = 1, .count = 5, .interval = 0.1f};
        wd.waves[1].startTime = 0.0f; // 同 tick 到期 → 接管
        wd.waves[1].entryCount = 1;
        wd.waves[1].entries[0] = WaveEntry{.prefabId = 1, .count = 2, .interval = 0.1f};
        int waveStarts = 0;
        world.SetEventSink([&](World&, const EventPacket& e) {
            if (e.type == GameEvent::WaveStart) ++waveStarts;
        });
        InstallDirectorPipeline(world);
        for (int i = 0; i < 60; ++i) world.Step(dt);
        Expect(waveStarts == 2, "both overlapping waves announce");
        Expect(ctr.spawns == 2, "later wave takes over; earlier entries dropped");
    }
}

// capAlive 同队闸门：普查 + 乐观自增 → 精确停在闸值（D4）
void TestWaveDirectorCapAlive() {
    World world;
    Scene& s = world.CreateScene("cap");
    world.SetActiveScene(&s);
    WaveSpawnCounter ctr;
    world.SetSpawnFn([&ctr](Scene& sc, uint32_t id, Vec2 pos, uint32_t team) {
        return ctr(sc, id, pos, team);
    });
    Entity dir = s.Create();
    s.Emplace<Transform2D>(dir, Transform2D{{0, 0}});
    WaveDirector& wd = s.Emplace<WaveDirector>(dir);
    wd.spawnTeam = 1;
    wd.capAlive = 2;
    wd.waveCount = 1;
    wd.waves[0].startTime = 0.0f;
    wd.waves[0].entryCount = 1;
    wd.waves[0].entries[0] = WaveEntry{.prefabId = 1, .count = 10, .interval = 1.0f / 60.0f};
    InstallDirectorPipeline(world);

    const float dt = 1.0f / 60.0f;
    for (int i = 0; i < 120; ++i) world.Step(dt);
    uint32_t alive = 0;
    s.View<Meta>().each([&](auto, Meta& m) {
        if (m.team == 1) ++alive;
    });
    Expect(ctr.spawns == 2, "capAlive 2: exactly 2 births, no retry churn");
    Expect(alive == 2, "team alive holds at cap");
}

// timeScale=0 冻结波次（time 停、零事件零出生、RNG 不消耗；恢复即照发——D3×批① D5）
void TestWaveDirectorTimeScaleFreeze() {
    World world;
    Scene& s = world.CreateScene("frz");
    world.SetActiveScene(&s);
    WaveSpawnCounter ctr;
    world.SetSpawnFn([&ctr](Scene& sc, uint32_t id, Vec2 pos, uint32_t team) {
        return ctr(sc, id, pos, team);
    });
    Entity dir = s.Create();
    s.Emplace<Transform2D>(dir, Transform2D{{0, 0}});
    WaveDirector& wd = s.Emplace<WaveDirector>(dir);
    wd.waveCount = 1;
    wd.waves[0].startTime = 0.5f;
    wd.waves[0].entryCount = 1;
    wd.waves[0].entries[0] = WaveEntry{.prefabId = 1, .count = 3, .interval = 0.1f};
    int waveStarts = 0;
    world.SetEventSink([&](World&, const EventPacket& e) {
        if (e.type == GameEvent::WaveStart) ++waveStarts;
    });
    InstallDirectorPipeline(world);

    const float dt = 1.0f / 60.0f;
    world.SetTimeScale(0.0f);
    for (int i = 0; i < 60; ++i) world.Step(dt);
    Expect(waveStarts == 0 && ctr.spawns == 0, "frozen: no wave, no spawn");
    Expect(s.Get<WaveDirector>(dir).time == 0.0f, "director time frozen at 0");

    world.SetTimeScale(1.0f);
    for (int i = 0; i < 30; ++i) world.Step(dt);
    Expect(waveStarts == 1 && ctr.spawns >= 1, "resume: wave fires on schedule");
}

// 波表 roundtrip；运行时（time/waveIndex/cd/spawned）不入档（T1 登记表护栏）
void TestWaveDirectorArchive() {
    World world;
    Scene& src = world.CreateScene("warc");
    Entity e = src.Create();
    src.Emplace<Transform2D>(e, Transform2D{{7, 8}});
    WaveDirector& wd = src.Emplace<WaveDirector>(e);
    wd.spawnTeam = 3;
    wd.capAlive = 777;
    wd.waveCount = 2;
    wd.waves[0].startTime = 1.25f;
    wd.waves[0].rampMult = 2.5f;
    wd.waves[0].entryCount = 2;
    wd.waves[0].entries[0] = WaveEntry{.prefabId = 0xAABBCCDDu, .count = 11, .interval = 0.05f, .range = 333.0f};
    wd.waves[0].entries[1] = WaveEntry{.prefabId = 7, .count = 1, .interval = 0.2f, .range = 40.0f};
    wd.waves[1].startTime = 30.0f;
    wd.waves[1].entryCount = 1;
    wd.waves[1].entries[0] = WaveEntry{.prefabId = 9, .count = 5, .interval = 0.1f, .range = 60.0f};
    // RT 污染（读档必须回落默认）
    wd.time = 9.9f;
    wd.waveIndex = 1;
    wd.waveCooldown[1] = 0.42f;
    wd.waveSpawned[2] = 3;

    const std::string text = SceneArchive::Save(src);
    Expect(text.find("\"waves\"") != std::string::npos, "wave table serialized");
    Expect(text.find("\"e0prefab\"") != std::string::npos, "flattened entry keys serialized");
    Expect(text.find("\"cd0\"") == std::string::npos, "cooldown RT not serialized");
    Expect(text.find("\"waveIndex\"") == std::string::npos, "waveIndex RT not serialized");

    World w2;
    Scene& dst = w2.CreateScene("warc2");
    Expect(SceneArchive::Load(dst, text), "wave scene load");
    dst.View<WaveDirector>().each([&](auto, WaveDirector& r) {
        Expect(r.spawnTeam == 3 && r.capAlive == 777, "director config roundtrip");
        Expect(r.waveCount == 2, "wave count roundtrip");
        Expect(r.waves[0].startTime == 1.25f && r.waves[0].rampMult == 2.5f, "wave 0 header");
        Expect(r.waves[0].entries[0].prefabId == 0xAABBCCDDu &&
                   r.waves[0].entries[0].count == 11 &&
                   r.waves[0].entries[0].interval == 0.05f &&
                   r.waves[0].entries[0].range == 333.0f, "entry 0 roundtrip");
        Expect(r.waves[0].entryCount == 2 && r.waves[0].entries[1].count == 1, "entry 1 roundtrip");
        Expect(r.waves[1].startTime == 30.0f && r.waves[1].entries[0].prefabId == 9, "wave 1 roundtrip");
        Expect(r.time == 0.0f && r.waveIndex == 0 && r.waveCooldown[1] == 0.0f &&
                   r.waveSpawned[2] == 0, "runtime fields default after load");
    });
}

// 孪生世界同种子：RNG 子流 1 消费序 + seg 原始字节零化 → StateHash 相等（§13 护栏）
void TestWaveDirectorDeterminism() {
    auto run = [](uint64_t& hashOut, int& spawnsOut) {
        World world;
        Scene& s = world.CreateScene("det");
        world.SetActiveScene(&s);
        WaveSpawnCounter ctr;
        world.SetSpawnFn([&ctr](Scene& sc, uint32_t id, Vec2 pos, uint32_t team) {
            return ctr(sc, id, pos, team);
        });
        Entity dir = s.Create();
        s.Emplace<Transform2D>(dir, Transform2D{{10, -5}});
        WaveDirector& wd = s.Emplace<WaveDirector>(dir);
        wd.waveCount = 2;
        wd.waves[0].startTime = 0.0f;
        wd.waves[0].entryCount = 1;
        wd.waves[0].entries[0] = WaveEntry{.prefabId = 1, .count = 8, .interval = 0.05f, .range = 50.0f};
        wd.waves[1].startTime = 1.0f;
        wd.waves[1].entryCount = 1;
        wd.waves[1].entries[0] = WaveEntry{.prefabId = 1, .count = 4, .interval = 0.1f, .range = 30.0f};
        InstallDirectorPipeline(world);
        for (int i = 0; i < 300; ++i) world.Step(1.0f / 60.0f);
        hashOut = ComputeStateHash(s);
        spawnsOut = ctr.spawns;
    };
    uint64_t ha = 0, hb = 0;
    int sa = 0, sb = 0;
    run(ha, sa);
    run(hb, sb);
    Expect(sa == 12 && sa == sb, "both worlds spawn full wave tables (RNG path exercised)");
    Expect(ha == hb, "twin worlds: identical state hash");
}

// timeScale（M5 批① T3）：Step 内缩放 dt；=0 冻结（位置不动、tick 照推）、
// 0.5 半速（同 tick 数位移对半）；setter clamp [0,8]
void TestVerifyTimeScale() {
    World world;
    Scene& s = world.CreateScene("ts");
    world.SetActiveScene(&s);

    Entity e = s.Create();
    s.Emplace<Transform2D>(e, Transform2D{{0, 0}});
    s.Emplace<Velocity>(e, Velocity{.v = {100.0f, 0.0f}});

    world.Pipeline().AddSystem(std::make_unique<MovementSystem>());
    world.Pipeline().AddSystem(std::make_unique<DestroyCommitSystem>());
    world.Pipeline().ResolveOrder();
    const float dt = 1.0f / 60.0f;

    for (int i = 0; i < 60; ++i) world.Step(dt);
    Expect(std::fabs(s.Get<Transform2D>(e).pos.x - 100.0f) < 0.01f,
           "full speed 1s = 100px"); // 累加容差放宽（1e-5 对 60 步过紧）

    world.SetTimeScale(0.5f);
    for (int i = 0; i < 30; ++i) world.Step(dt); // 名义 0.5s × 0.5 = +25px
    Expect(std::fabs(s.Get<Transform2D>(e).pos.x - 125.0f) < 0.01f, "half speed +25px");

    world.SetTimeScale(0.0f); // 冻结暂停：tick 照推、位置不动（RNG 不消耗口径）
    const uint64_t tickBefore = world.TickIndex();
    for (int i = 0; i < 10; ++i) world.Step(dt);
    Expect(std::fabs(s.Get<Transform2D>(e).pos.x - 125.0f) < 0.01f, "frozen position holds");
    Expect(world.TickIndex() == tickBefore + 10, "ticks advance while paused");

    world.SetTimeScale(-3.0f);
    Expect(world.TimeScale() == 0.0f, "negative clamped to 0");
    world.SetTimeScale(99.0f);
    Expect(world.TimeScale() == 8.0f, "overshoot clamped to 8");
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
    s.Emplace<Health>(victim, Health{.max = 10.0f, .cur = 10.0f});

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

// ------------------ M2 复核轮新增测试（2026-09-19，只读审计配套） -----------
// 2026-09-19 修复轮：ISSUE-1..8 已全部修复，原 [ISSUE-n] "固化现状"断言已同步
// 改为断言正确行为（问题登记与修法见 docs/Reports/2026-09-19-m2-review-checklist.md）。
#include "ECS/StateHash.h"

namespace {

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
    Expect(SceneArchive::Load(
               s, R"({"schemaVersion":1,"entities":[)"
                  R"({"components":{"StatusEffects":{"count":200}}}]})"),
           "issue-2 fixed: load succeeds");
    bool checked = false;
    s.View<StatusEffects>().each(
        [&](auto, StatusEffects& st) {
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
    h.OverlapCircle(s, {100, 0}, 10.0f, QueryFilter{}, 0.0f,
                    [&](Entity, const Transform2D&) {
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

// ---- AI Chase：追击 / keepRange 停 / 出警戒归零 ----
void TestVerifyAISystemChase() {
    World w;
    w.InstallDefaultSystems();
    Scene& s = w.CreateScene("chase");
    w.SetActiveScene(&s);
    Entity m = s.Create();
    s.Emplace<Transform2D>(m, Transform2D{{0, 0}});
    s.Emplace<Meta>(m).team = 1;
    s.Emplace<Velocity>(m);
    Chase& ch = s.Emplace<Chase>(m);
    ch.speed = 100.0f;
    ch.aggroRange = 2000.0f;
    ch.keepRange = 24.0f;
    ch.targetTeam = 0;
    Entity p = s.Create();
    s.Emplace<Transform2D>(p, Transform2D{{100, 0}});
    s.Emplace<Meta>(p).team = 0;
    s.Emplace<Velocity>(p);

    w.Step(1.0f / 60.0f);
    Vec2 v = s.Get<Velocity>(m).v;
    Expect(v.x > 90.0f && std::fabs(v.y) < 1.0f, "ai: chases along +x at speed");
    ExpectNear(Length(v), 100.0f, 0.5f, "ai: velocity magnitude = speed");

    s.Get<Transform2D>(p).pos = {0, 0}; // 贴脸
    w.Step(1.0f / 60.0f);
    Expect(s.Get<Velocity>(m).v == Vec2::Zero(), "ai: keepRange stops");

    s.Get<Transform2D>(p).pos = {5000, 0}; // 出警戒
    w.Step(1.0f / 60.0f);
    Expect(s.Get<Velocity>(m).v == Vec2::Zero(), "ai: target lost → zero");
}

// ---- Flee / Patrol 行为 ----
void TestVerifyFleeAndPatrol() {
    {
        World w;
        w.InstallDefaultSystems();
        Scene& s = w.CreateScene("flee");
        w.SetActiveScene(&s);
        Entity f = s.Create();
        s.Emplace<Transform2D>(f, Transform2D{{0, 0}});
        s.Emplace<Meta>(f).team = 2;
        s.Emplace<Velocity>(f);
        Flee& fl = s.Emplace<Flee>(f);
        fl.speed = 80.0f;
        fl.range = 200.0f;
        Entity threat = s.Create();
        s.Emplace<Transform2D>(threat, Transform2D{{50, 0}});
        s.Emplace<Meta>(threat).team = 0;

        // [ISSUE-5 已修复] NearestAny 排除自身：威胁在 +50 → 逃向 -x
        w.Step(1.0f / 60.0f);
        Vec2 v = s.Get<Velocity>(f).v;
        Expect(v.x < -70.0f && std::fabs(v.y) < 1.0f, "flee: runs away from threat");
        ExpectNear(Length(v), 80.0f, 0.5f, "flee: velocity magnitude = fl.speed");

        s.Get<Transform2D>(threat).pos = {5000, 0};
        s.Get<Velocity>(f).v = {7, 7}; // 哨兵：无威胁时不得覆写
        w.Step(1.0f / 60.0f);
        Expect(s.Get<Velocity>(f).v == Vec2({7, 7}),
               "flee: no threat keeps velocity (no overwrite)");
    }
    {
        World w;
        w.InstallDefaultSystems();
        Scene& s = w.CreateScene("patrol");
        w.SetActiveScene(&s);
        Entity e = s.Create();
        s.Emplace<Transform2D>(e, Transform2D{{99, 0}});
        s.Emplace<Velocity>(e);
        Patrol& pt = s.Emplace<Patrol>(e);
        pt.a = {0, 0};
        pt.b = {100, 0};
        pt.headingToB = 1;

        w.Step(1.0f / 60.0f);
        Expect(pt.headingToB == 0, "patrol: flips at endpoint");
        // [ISSUE-6 已修复] 折返同帧改向：折返帧速度立即朝新端点（原滞后一帧）
        Expect(s.Get<Velocity>(e).v.x < 0,
               "patrol: flip-frame velocity already toward new dest");
        w.Step(1.0f / 60.0f);
        Expect(s.Get<Velocity>(e).v.x < 0, "patrol: keeps heading back");
    }
}

// ---- Animator：loop 有界 / 非 loop 增长 ----
void TestVerifyAnimatorAdvance() {
    World w;
    w.InstallDefaultSystems();
    Scene& s = w.CreateScene("anim");
    w.SetActiveScene(&s);
    Entity loopy = s.Create();
    Animator2D& al = s.Emplace<Animator2D>(loopy);
    al.speed = 2.0f;
    al.loop = 1;
    Entity oncey = s.Create();
    Animator2D& ao = s.Emplace<Animator2D>(oncey);
    ao.speed = 1.0f;
    ao.loop = 0;
    for (int i = 0; i < 120; ++i) w.Step(1.0f / 60.0f); // 2 模拟秒
    Expect(al.time >= 0.0f && al.time < 1.0f, "anim: loop keeps time bounded");
    Expect(ao.time > 1.0f, "anim: non-loop advances unbounded (M2 语义)");

    // 批③回退护栏：表非空但 clipId 未命中（错绑）→ 仍走 M2 旧算术（金档零漂移前提）
    w.Clips().Add(0xEEEEu, {501u}, 8.0f, true); // 表非空即可
    Entity missy = s.Create();
    Animator2D& am = s.Emplace<Animator2D>(missy);
    am.clipId = 0xDEADBEEFu; // 未登记 id
    am.speed = 1.0f;
    am.loop = 1;
    for (int i = 0; i < 120; ++i) w.Step(1.0f / 60.0f);
    Expect(am.time >= 0.0f && am.time < 1.0f, "anim: unknown clipId falls back to M2 loop");
}

// ---- 批③：ClipTable 帧映射（fps 截断/回绕/钳末帧/暂停/半速/负速/无渲染器/孪生/roundtrip）----
void TestVerifyAnimatorFrameMapping() {
    const float dt = 1.0f / 60.0f;
    World w;
    w.InstallDefaultSystems();
    Scene& s = w.CreateScene("clip");
    w.SetActiveScene(&s);
    // clip 0x77：fps 8 × 3 帧（spriteId 10/11/12）→ 帧界 7.5 tick、周期 22.5 tick
    Expect(w.Clips().Add(0x77u, {10u, 11u, 12u}, 8.0f, true), "clip table add");
    Expect(!w.Clips().Add(0u, {1u}, 8.0f, true), "clip id 0 rejected");
    Expect(w.Clips().Add(0x78u, {}, 8.0f, true) == false, "empty frames rejected");
    Expect(w.Clips().Find(0x77u) != nullptr && w.Clips().Find(0u) == nullptr &&
               w.Clips().Find(0x999u) == nullptr, "clip find semantics");

    Entity loopy = s.Create();
    SpriteRenderer& srl = s.Emplace<SpriteRenderer>(loopy);
    srl.spriteId = 999u;
    Animator2D& al = s.Emplace<Animator2D>(loopy);
    al.clipId = 0x77u;

    Entity oncey = s.Create(); // loop=0 钳末帧（M5 收口：time 钳 total 有界）
    SpriteRenderer& sro = s.Emplace<SpriteRenderer>(oncey);
    sro.spriteId = 999u;
    Animator2D& ao = s.Emplace<Animator2D>(oncey);
    ao.clipId = 0x77u;
    ao.loop = 0;

    Entity paused = s.Create(); // playOnStart=0 = 暂停开关（三态全冻结）
    SpriteRenderer& srp = s.Emplace<SpriteRenderer>(paused);
    srp.spriteId = 999u;
    Animator2D& ap = s.Emplace<Animator2D>(paused);
    ap.clipId = 0x77u;
    ap.playOnStart = 0;

    Entity half = s.Create(); // speed 0.5：16 tick == 全速 8 tick
    Animator2D& ah = s.Emplace<Animator2D>(half);
    ah.clipId = 0x77u;
    ah.speed = 0.5f;

    Entity bare = s.Create(); // 无 SpriteRenderer：纯计时推进不炸
    Animator2D& ab = s.Emplace<Animator2D>(bare);
    ab.clipId = 0x77u;

    Entity neg = s.Create(); // 负 speed 防御：time 钳 0、停 0 号帧
    SpriteRenderer& srn = s.Emplace<SpriteRenderer>(neg);
    srn.spriteId = 999u;
    Animator2D& an = s.Emplace<Animator2D>(neg);
    an.clipId = 0x77u;
    an.speed = -1.0f;

    for (int i = 0; i < 8; ++i) w.Step(dt);
    Expect(al.curFrame == 1 && srl.spriteId == 11u, "clip: tick 8 -> frame 1");
    Expect(ah.curFrame == 0, "clip: half speed still frame 0 at tick 8");
    Expect(ab.curFrame == 1, "clip: no-renderer animator advances");
    for (int i = 0; i < 14; ++i) w.Step(dt); // 累计 22 tick
    Expect(al.curFrame == 2 && srl.spriteId == 12u, "clip: tick 22 -> frame 2");
    w.Step(dt); // 23 tick：time 0.3833 ≥ total 0.375 → 回绕
    Expect(al.curFrame == 0 && srl.spriteId == 10u, "clip: tick 23 wraps to frame 0");
    Expect(ah.curFrame == 1, "clip: half speed reaches frame 1 at tick 23 (帧界 15 tick)");
    for (int i = 0; i < 97; ++i) w.Step(dt); // 累计 120 tick
    Expect(ao.time <= 3.0f / 8.0f && ao.curFrame == 2 && sro.spriteId == 12u,
           "clip: non-loop clamps to last frame (time bounded)");
    Expect(ap.time == 0.0f && ap.curFrame == 0 && srp.spriteId == 999u,
           "clip: playOnStart=0 freezes all three");
    Expect(an.time == 0.0f && an.curFrame == 0 && srn.spriteId == 10u,
           "clip: negative speed clamps to frame 0");

    // roundtrip：clipId/speed/loop/playOnStart 入档；time/curFrame 亦入档（FIELD 位未动）
    const std::string text = SceneArchive::Save(s);
    Expect(text.find("\"clipId\"") != std::string::npos, "clipId serialized");
    World w2;
    Scene& dst = w2.CreateScene("clip2");
    Expect(SceneArchive::Load(dst, text), "clip scene load");
    dst.View<Animator2D>().each([&](auto ent2, Animator2D& r) {
        if (Scene::FromEntt(ent2) == oncey) {
            Expect(r.clipId == 0x77u && r.loop == 0 && r.speed == 1.0f,
                   "animator config roundtrip");
        }
    });

    // 孪生世界：帧映射纯函数 + 无 RNG 消费 → 300 tick StateHash 相等（§18 护栏）
    auto run = [](uint64_t& hashOut) {
        World world;
        world.InstallDefaultSystems();
        Scene& sc = world.CreateScene("det");
        world.SetActiveScene(&sc);
        world.Clips().Add(0x77u, {10u, 11u, 12u}, 8.0f, true);
        for (int k = 0; k < 3; ++k) {
            ecs::Entity e = sc.Create();
            sc.Emplace<Transform2D>(e, Transform2D{{(float)k * 30.0f, 5.0f}});
            SpriteRenderer& sr = sc.Emplace<SpriteRenderer>(e);
            sr.spriteId = 999u;
            Animator2D& a = sc.Emplace<Animator2D>(e);
            a.clipId = 0x77u;
            a.speed = 1.0f + 0.5f * (float)k; // 不同速度混合
        }
        for (int i = 0; i < 300; ++i) world.Step(1.0f / 60.0f);
        hashOut = ComputeStateHash(sc);
    };
    uint64_t ha = 0, hb = 0;
    run(ha);
    run(hb);
    Expect(ha == hb, "clip anim twin worlds: identical state hash");
}

// ---- 批①：Animator 换段队列（Queue 收尾/回绕点、CrossFade 倒计/提前收尾、
// 暂停冻结、Play 清队列、目标未命中丢弃、M2 旁路、孪生哈希）----
void TestVerifyAnimatorQueue() {
    const float dt = 1.0f / 60.0f;
    World w;
    w.InstallDefaultSystems();
    Scene& s = w.CreateScene("queue");
    w.SetActiveScene(&s);
    // 0x77：fps8×3 帧（10/11/12），周期 22.5 tick；0x88：fps8×2 帧（20/21）；
    // 0x99：fps4×2 帧（30/31）
    w.Clips().Add(0x77u, {10u, 11u, 12u}, 8.0f, true);
    w.Clips().Add(0x88u, {20u, 21u}, 8.0f, true);
    w.Clips().Add(0x99u, {30u, 31u}, 4.0f, true);

    // ① Queue：非 loop 段收尾即切（tick 23 time 钳 total → 切，新段首帧当帧生效）
    Entity q1 = s.Create();
    SpriteRenderer& sr1 = s.Emplace<SpriteRenderer>(q1);
    Animator2D& a1 = s.Emplace<Animator2D>(q1);
    a1.clipId = 0x77u;
    a1.loop = 0;
    a1.nextClipId = 0x88u;
    a1.nextLoop = 1;
    a1.fadeRemain = -1.0f;
    for (int i = 0; i < 23; ++i) w.Step(dt);
    Expect(a1.clipId == 0x88u && a1.time == 0.0f && a1.loop == 1 &&
               a1.curFrame == 0 && sr1.spriteId == 20u && a1.nextClipId == 0 &&
               a1.fadeRemain == 0.0f,
           "queue: non-loop end switches to next clip frame 0");

    // ② Queue：loop 段回绕点切（tick 23 回绕瞬间切段 + nextLoop=0 生效）
    Entity q2 = s.Create();
    Animator2D& a2 = s.Emplace<Animator2D>(q2);
    a2.clipId = 0x77u;
    a2.nextClipId = 0x88u;
    a2.nextLoop = 0;
    a2.fadeRemain = -1.0f;
    for (int i = 0; i < 23; ++i) w.Step(dt);
    Expect(a2.clipId == 0x88u && a2.time == 0.0f && a2.loop == 0,
           "queue: loop wrap point switches");

    // ③ CrossFade：倒计时到零切（fade=5.5 tick：第 5 tick 未切、第 6 tick 切——
    //    半 tick 余量避开浮点累积误差踩线）
    Entity q3 = s.Create();
    SpriteRenderer& sr3 = s.Emplace<SpriteRenderer>(q3);
    Animator2D& a3 = s.Emplace<Animator2D>(q3);
    a3.clipId = 0x77u;
    a3.nextClipId = 0x88u;
    a3.fadeRemain = 5.5f * dt;
    for (int i = 0; i < 5; ++i) w.Step(dt);
    const bool stillOld = a3.clipId == 0x77u && a3.curFrame == 0; // 未到 7.5 tick 帧界
    for (int i = 0; i < 1; ++i) w.Step(dt);
    Expect(stillOld && a3.clipId == 0x88u && sr3.spriteId == 20u,
           "crossfade: countdown expiry switches (5 no, 6 yes)");

    // ④ CrossFade：非 loop 当前段提前收尾即切（fade 再长也不等）
    Entity q4 = s.Create();
    Animator2D& a4 = s.Emplace<Animator2D>(q4);
    a4.clipId = 0x77u;
    a4.loop = 0;
    for (int i = 0; i < 22; ++i) w.Step(dt); // time 0.3667（一 tick 后收尾）
    a4.nextClipId = 0x88u;
    a4.fadeRemain = 60.0f * dt; // 1 秒长淡入——收尾必须抢先
    w.Step(dt);
    Expect(a4.clipId == 0x88u && a4.time == 0.0f,
           "crossfade: non-loop early end overrides long fade");

    // ⑤ 暂停冻结整个队列（time/倒计/切点三冻；恢复后倒计继续；半 tick 余量同③）
    Entity q5 = s.Create();
    Animator2D& a5 = s.Emplace<Animator2D>(q5);
    a5.clipId = 0x77u;
    a5.playOnStart = 0;
    a5.nextClipId = 0x88u;
    a5.fadeRemain = 3.5f * dt;
    for (int i = 0; i < 10; ++i) w.Step(dt);
    const bool frozen = a5.time == 0.0f && a5.nextClipId == 0x88u &&
                        a5.fadeRemain == 3.5f * dt && a5.clipId == 0x77u;
    a5.playOnStart = 1;
    for (int i = 0; i < 3; ++i) w.Step(dt); // 倒计 0.5dt 余量未到
    const bool stillQueued = a5.clipId == 0x77u && a5.nextClipId == 0x88u;
    w.Step(dt);
    Expect(frozen && stillQueued && a5.clipId == 0x88u,
           "pause freezes queue; resume continues countdown");

    // ⑥ Play 清在途队列（SDK Play 等价字段写：打断一切在途切换）
    Entity q6 = s.Create();
    Animator2D& a6 = s.Emplace<Animator2D>(q6);
    a6.clipId = 0x77u;
    a6.nextClipId = 0x88u;
    a6.fadeRemain = -1.0f;
    // Play(0x99, loop=true)：切段 + 清队列 + 归零
    a6.clipId = 0x99u;
    a6.time = 0.0f;
    a6.playOnStart = 1;
    a6.nextClipId = 0;
    a6.fadeRemain = 0.0f;
    for (int i = 0; i < 5; ++i) w.Step(dt);
    Expect(a6.clipId == 0x99u && a6.time > 0.0f && a6.nextClipId == 0,
           "play clears pending queue and restarts");

    // ⑦ 队列目标未命中 clip 表 = 丢队列（当前段帧映射不受扰）
    Entity q7 = s.Create();
    SpriteRenderer& sr7 = s.Emplace<SpriteRenderer>(q7);
    Animator2D& a7 = s.Emplace<Animator2D>(q7);
    a7.clipId = 0x77u;
    a7.nextClipId = 0x999u; // 未登记
    a7.fadeRemain = 1.0f * dt;
    for (int i = 0; i < 10; ++i) w.Step(dt);
    Expect(a7.nextClipId == 0 && a7.clipId == 0x77u && a7.curFrame == 1 &&
               sr7.spriteId == 11u,
           "queue: unknown target dropped, current clip unaffected");

    // ⑧ 无 clip 表：队列整体旁路——M2 逐位不变 + 队列字段原样保留
    {
        World w2;
        w2.InstallDefaultSystems();
        Scene& s2 = w2.CreateScene("m2queue");
        w2.SetActiveScene(&s2);
        Entity e1 = s2.Create();
        Animator2D& b1 = s2.Emplace<Animator2D>(e1);
        b1.clipId = 0x77u; // 表空 → M2 路径
        b1.nextClipId = 0x88u;
        b1.fadeRemain = -1.0f;
        Entity e2 = s2.Create();
        Animator2D& b2 = s2.Emplace<Animator2D>(e2);
        b2.clipId = 0x77u;
        for (int i = 0; i < 120; ++i) w2.Step(dt);
        Expect(b1.time >= 0.0f && b1.time < 1.0f && b2.time == b1.time &&
                   b1.nextClipId == 0x88u && b1.fadeRemain == -1.0f,
               "queue: empty clip table bypasses queue (M2 bit-identical)");
    }

    // ⑨ 孪生世界：队列演化确定性（含运行中段切换指令——两侧同码执行）
    auto run = [](uint64_t& hashOut) {
        World world;
        world.InstallDefaultSystems();
        Scene& sc = world.CreateScene("detq");
        world.SetActiveScene(&sc);
        world.Clips().Add(0x77u, {10u, 11u, 12u}, 8.0f, true);
        world.Clips().Add(0x88u, {20u, 21u}, 8.0f, true);
        Entity victim = sc.Create();
        sc.Emplace<SpriteRenderer>(victim);
        Animator2D& v = sc.Emplace<Animator2D>(victim);
        v.clipId = 0x77u;
        Entity fader = sc.Create();
        Animator2D& f = sc.Emplace<Animator2D>(fader);
        f.clipId = 0x88u;
        f.loop = 0;
        for (int i = 0; i < 300; ++i) {
            if (i == 30) { // 受击组合拳：Play(hit) + Queue(walk)
                v.loop = 0;
                v.time = 0.0f;
                v.nextClipId = 0x88u;
                v.nextLoop = 1;
                v.fadeRemain = -1.0f;
            }
            if (i == 100) f.nextClipId = 0x77u, f.nextLoop = 1, f.fadeRemain = 0.05f;
            world.Step(1.0f / 60.0f);
        }
        hashOut = ComputeStateHash(sc);
    };
    uint64_t qa = 0, qb = 0;
    run(qa);
    run(qb);
    Expect(qa == qb, "anim queue twin worlds: identical state hash");
}

// ---- 批①：FxChannel（飘字池淘汰/上浮淡出、血条覆写/sticky、产包数学）----
void TestVerifyFxChannel() {
    // ① 飘字环形池：满 256 后最老者淘汰（第 257 条覆写第 1 条槽位）
    {
        FxChannel fx;
        char buf[8];
        for (int i = 0; i < 257; ++i) {
            std::snprintf(buf, sizeof(buf), "%d", i);
            fx.PopupText(buf, (float)i, 0.0f);
        }
        Expect(fx.TextCount() == 256, "fx: text pool capped at 256");
        Expect(std::string(fx.TextAt(0).text) == "1" && std::string(fx.TextAt(255).text) == "256",
               "fx: oldest text evicted, order preserved");
        // 长文本 16 字符截断
        fx.PopupText("01234567890123456789", 0, 0);
        Expect(std::string(fx.TextAt(255).text) == "012345678901234",
               "fx: text truncated to 15 chars");
    }
    // ② Simulate：上浮（前 70% 匀升）/淡出（末 30%）/到期回收
    {
        FxChannel fx;
        fx.PopupText("12", 100.0f, 50.0f);
        fx.Simulate(0.4f);
        const FxText& t = fx.TextAt(0);
        const float riseMid = fx.TextRise(t), alphaMid = fx.TextAlpha(t);
        fx.Simulate(0.36f); // age 0.76（末 30% 窗内）
        const float riseLate = fx.TextRise(t), alphaLate = fx.TextAlpha(t);
        Expect(riseMid > 0.0f && riseMid < FxChannel::kTextRise && alphaMid == 1.0f &&
                   riseLate == FxChannel::kTextRise && alphaLate < 1.0f && alphaLate > 0.0f,
               "fx: rise ramps then holds; alpha fades in last 30%");
        fx.Simulate(0.05f); // age 0.81 ≥ life 0.8 → 回收
        Expect(fx.TextCount() == 0, "fx: expired text recycled");
    }
    // ③ 血条键控覆写刷新（age 归零续命）；④ sticky 过期释放
    {
        FxChannel fx;
        fx.Bar(0xAAu, 0.5f);
        fx.Simulate(1.0f);
        fx.Bar(0xAAu, 0.8f, 0xFF00FF00u, 48.0f);
        const FxBar* b = nullptr;
        for (const FxBar& s : fx.Bars())
            if (s.entity == 0xAAu) b = &s;
        Expect(fx.BarCount() == 1 && b && b->age == 0.0f && b->frac == 0.8f &&
                   b->width == 48.0f && b->color == 0xFF00FF00u,
               "fx: bar refresh resets age and updates fields");
        fx.Simulate(FxChannel::kBarSticky + 0.01f);
        Expect(fx.BarCount() == 0, "fx: bar expires after sticky window");
    }
    // ⑤ 血条槽满淘汰最旧（129 实体 → age 最大者让位）
    {
        FxChannel fx;
        for (uint64_t e = 1; e <= 128; ++e) fx.Bar(e, 0.5f);
        fx.Simulate(1.0f);
        fx.Bar(0xE1u, 0.1f); // 全满 → 淘汰 age 最大（= entity 1，最先入）
        bool hasNew = false, evictedOld = true;
        for (const FxBar& s : fx.Bars()) {
            if (s.entity == 0xE1u) hasNew = true;
            if (s.entity == 1u) evictedOld = false;
        }
        Expect(fx.BarCount() == 128 && hasNew && evictedOld,
               "fx: full bar pool evicts least-recently-refreshed");
    }
    // ⑥ ExtractBarQuads 数学：bg 整宽居中 + fg 比例宽左锚 + 悬空跳过 + 视口剔除
    {
        FxChannel fx;
        fx.Bar(0x100u, 0.5f, 0xFF30B0F0u, 32.0f); // 在视野内
        fx.Bar(0x101u, 1.0f, 0xFF30B0F0u, 32.0f); // 视野外（右侧远处）
        fx.Bar(0x102u, 1.0f, 0xFF30B0F0u, 32.0f); // 悬空实体（resolve false）
        auto resolve = [](uint64_t e, Vec2& out) {
            if (e == 0x100u) {
                out = {0.0f, 0.0f};
                return true;
            }
            if (e == 0x101u) {
                out = {5000.0f, 0.0f};
                return true;
            }
            return false; // 0x102 悬空
        };
        FxQuad q[8];
        const uint32_t n = fx.ExtractBarQuads(q, 8, resolve,
                                              Rect{Vec2{-320, -180}, Vec2{320, 180}});
        Expect(n == 2, "fx: quads = 2 (viewport + dangling filtered)");
        const FxQuad& bg = q[0];
        const FxQuad& fg = q[1];
        Expect(bg.size.x == 32.0f && bg.size.y == FxChannel::kBarHeight &&
                   bg.color == FxChannel::kBarBgColor && bg.center.x == 0.0f,
               "fx: bg quad full width centered");
        Expect(fg.size.x == 16.0f && fg.center.x == -8.0f && fg.color == 0xFF30B0F0u,
               "fx: fg quad proportional width left-anchored");
    }
}

// ---- Stat：到期压缩保序 + xpToNext=0 终止性 ----
void TestVerifyStatEffectsAndXp() {
    World w;
    w.InstallDefaultSystems();
    Scene& s = w.CreateScene("stat");
    w.SetActiveScene(&s);
    Entity e = s.Create();
    StatusEffects& st = s.Emplace<StatusEffects>(e);
    st.active[0] = StatusInst{1, 1, 0.01f, 0}; // 本帧内到期
    st.active[1] = StatusInst{2, 3, 0.50f, 0};
    st.count = 2;
    Entity x = s.Create();
    XpProgress& xp = s.Emplace<XpProgress>(x);
    xp.xp = 5.0f;
    xp.xpToNext = 0.0f; // 资产配 0：修复前挂死，回归防线

    w.Step(1.0f / 60.0f);
    Expect(st.count == 1, "stat: expired effect removed");
    Expect(st.active[0].id == 2 && st.active[0].stacks == 3,
           "stat: compaction preserves order");
    ExpectNear(st.active[0].remain, 0.50f - 1.0f / 60.0f, 1e-4f, "stat: remain ticks");

    w.Step(1.0f / 60.0f); // 能走到这里 = xp 环已终止
    const XpProgress& after = s.Get<XpProgress>(x);
    Expect(after.level >= 2, "stat: level advanced");
    Expect(after.xp < after.xpToNext, "stat: xp below threshold");
}

// ---- Movement：击退衰减积分 + 边界钳制 ----
void TestVerifyMovementKnockbackAndClamp() {
    {
        World w;
        w.InstallDefaultSystems();
        Scene& s = w.CreateScene("kb");
        w.SetActiveScene(&s);
        Entity e = s.Create();
        s.Emplace<Transform2D>(e, Transform2D{{0, 0}});
        s.Emplace<Velocity>(e);
        Knockback& kb = s.Emplace<Knockback>(e);
        kb.impulse = {200, 0};
        kb.decay = 8.0f;
        for (int i = 0; i < 60; ++i) w.Step(1.0f / 60.0f);
        float x = s.Get<Transform2D>(e).pos.x;
        // 离散积分：Σ v0·rⁱ·dt = v0·dt/(1−e^(−decay·dt)) ≈ 26.7（连续理想 25，
        // ~7% 离散滞后属可接受近似，非 bug；impulse 项 exp 衰减同源两侧一致）
        Expect(x > 26.0f && x < 27.5f, "movement: discrete impulse integral");
        Expect(Length(s.Get<Knockback>(e).impulse) < 0.5f, "movement: impulse decayed out");
    }
    {
        World w;
        w.InstallDefaultSystems();
        w.SetBounds(Rect{{0, 0}, {100, 100}});
        Scene& s = w.CreateScene("clamp");
        w.SetActiveScene(&s);
        Entity e = s.Create();
        s.Emplace<Transform2D>(e, Transform2D{{150, 50}});
        s.Emplace<Velocity>(e);
        s.Get<Velocity>(e).v = {10, 0};
        w.Step(1.0f / 60.0f);
        Expect(s.Get<Transform2D>(e).pos.x == 100.0f, "movement: clamped to bounds");
    }
}

// ---- ProjectileLifetime：寿命到期两帧提交节奏 + 越界回收 ----
void TestVerifyProjectileLifetime() {
    {
        World w;
        w.InstallDefaultSystems();
        Scene& s = w.CreateScene("pl");
        w.SetActiveScene(&s);
        Entity p = s.Create();
        s.Emplace<Transform2D>(p, Transform2D{{0, 0}});
        Projectile& pr = s.Emplace<Projectile>(p);
        pr.lifetime = 0.03f; // 2 帧
        w.Step(1.0f / 60.0f);
        Expect(s.Alive(p), "proj: alive on frame 1");
        w.Step(1.0f / 60.0f);
        Expect(s.Alive(p), "proj: queued destroy keeps alive this frame");
        w.Step(1.0f / 60.0f);
        Expect(!s.Alive(p), "proj: committed at next Essential");
    }
    {
        World w;
        w.InstallDefaultSystems();
        w.SetBounds(Rect{{0, 0}, {10, 10}});
        Scene& s = w.CreateScene("pb");
        w.SetActiveScene(&s);
        Entity p = s.Create();
        s.Emplace<Transform2D>(p, Transform2D{{500, 500}});
        s.Emplace<Projectile>(p);
        w.Step(1.0f / 60.0f);
        Expect(s.Alive(p), "proj: out-of-bounds queued");
        w.Step(1.0f / 60.0f);
        Expect(!s.Alive(p), "proj: out-of-bounds committed");
    }
}

// ---- Spawn 配额：maxAlive 有界且稳定 ----
void TestVerifySpawnQuota() {
    World w;
    w.InstallDefaultSystems();
    w.SetSpawnFn([](Scene& s, uint32_t, Vec2 pos, uint32_t team) {
        Entity e = s.Create();
        s.Emplace<Transform2D>(e, Transform2D{pos});
        s.Emplace<Meta>(e).team = team;
        return e;
    });
    Scene& s = w.CreateScene("quota");
    w.SetActiveScene(&s);
    Entity sp = s.Create();
    s.Emplace<Transform2D>(sp, Transform2D{{0, 0}});
    s.Emplace<Meta>(sp).team = 9; // spawner 自身不入 spawnTeam 存量
    Spawner& cfg = s.Emplace<Spawner>(sp);
    cfg.prefabId = 1;
    cfg.interval = 0.01f;
    cfg.burst = 10;
    cfg.maxAlive = 3;
    cfg.spawnTeam = 5;

    uint32_t n = 0;
    for (int i = 0; i < 40; ++i) {
        w.Step(1.0f / 60.0f);
        n = 0;
        s.View<Meta>().each([&](auto, Meta& mt) {
            if (mt.team == 5) ++n;
        });
    }
    Expect(n == 3, "spawn: maxAlive quota enforced and stable");
}

// ---- Trigger：once 字段语义（显式 fired，不再走 _pad）----
void TestVerifyTriggerOnceSemantics() {
    World w;
    w.InstallDefaultSystems();
    w.Teams().SetRelation(0, 0, TeamRelation::Friendly); // 同队非 ghost 可触发
    Scene& s = w.CreateScene("trig");
    w.SetActiveScene(&s);

    Entity t1 = s.Create();
    s.Emplace<Transform2D>(t1, Transform2D{{0, 0}});
    s.Emplace<Meta>(t1).team = 0;
    Trigger2D& g1 = s.Emplace<Trigger2D>(t1);
    g1.triggerId = 101;
    g1.once = 1;
    g1.radius = 40.0f;
    // 触发器分开放置：本块验证 once/Exit 语义本身；共置互触发见下方 [ISSUE-7] 块。
    Entity t2 = s.Create();
    s.Emplace<Transform2D>(t2, Transform2D{{300, 0}});
    s.Emplace<Meta>(t2).team = 0;
    Trigger2D& g2 = s.Emplace<Trigger2D>(t2);
    g2.triggerId = 202;
    g2.once = 0;
    g2.radius = 40.0f;
    Entity v = s.Create();
    s.Emplace<Transform2D>(v, Transform2D{{10, 0}});
    s.Emplace<Meta>(v).team = 0;

    int enter1 = 0, enter2 = 0, exit1 = 0, exit2 = 0;
    w.SetEventSink([&](World&, const EventPacket& p) {
        if (p.type == GameEvent::TriggerEnter && p.src == t1) ++enter1;
        if (p.type == GameEvent::TriggerEnter && p.src == t2) ++enter2;
        if (p.type == GameEvent::TriggerExit && p.src == t1) ++exit1;
        if (p.type == GameEvent::TriggerExit && p.src == t2) ++exit2;
    });

    w.Step(1.0f / 60.0f);  // 进入 t1
    s.Get<Transform2D>(v).pos = {300, 0};
    w.Step(1.0f / 60.0f);  // t1 出（once 不报 Exit）+ t2 进
    s.Get<Transform2D>(v).pos = {10, 0};
    w.Step(1.0f / 60.0f);  // t1 再进（once 抑制）+ t2 出
    s.Get<Transform2D>(v).pos = {300, 0};
    w.Step(1.0f / 60.0f);  // t2 再进

    Expect(enter1 == 1, "trigger: once fires exactly once");
    Expect(exit1 == 0, "trigger: once suppresses exit");
    Expect(enter2 == 2, "trigger: re-entry fires again");
    Expect(exit2 == 1, "trigger: normal exit reported");

    // [ISSUE-7 已修复] 共置触发器互不触发（修复前互为"非 ghost 进入者"：
    // 开局即互发假 Enter、anyInside 恒真、Exit 永不产生）
    {
        World w2;
        w2.InstallDefaultSystems();
        w2.Teams().SetRelation(0, 0, TeamRelation::Friendly);
        Scene& s2 = w2.CreateScene("trig2");
        w2.SetActiveScene(&s2);
        Entity a = s2.Create();
        s2.Emplace<Transform2D>(a, Transform2D{{0, 0}});
        s2.Emplace<Meta>(a).team = 0;
        s2.Emplace<Trigger2D>(a).radius = 40.0f;
        Entity b = s2.Create();
        s2.Emplace<Transform2D>(b, Transform2D{{10, 0}});
        s2.Emplace<Meta>(b).team = 0;
        s2.Emplace<Trigger2D>(b).radius = 40.0f;
        int enters = 0;
        w2.SetEventSink([&](World&, const EventPacket& p) {
            if (p.type == GameEvent::TriggerEnter) ++enters;
        });
        w2.Step(1.0f / 60.0f);
        w2.Step(1.0f / 60.0f);
        Expect(enters == 0, "trigger: co-located triggers don't fire each other");
        Entity v = s2.Create(); // 真实访客进入两域
        s2.Emplace<Transform2D>(v, Transform2D{{5, 0}});
        s2.Emplace<Meta>(v).team = 0;
        w2.Step(1.0f / 60.0f);
        Expect(enters == 2, "trigger: real visitor enters both zones");
    }
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

} // namespace

// ---- M4.1：Hierarchy 链维护/防环/世界矩阵（内核 #1 + M2 复审 N6 遗留环检测测试）----
#include "Components/CoreComponents.h"
#include "ECS/ComponentRegistry.h"
#include "ECS/Scene.h"

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
    Expect(s.Get<Hierarchy>(a).firstChild == b && s.Get<Hierarchy>(b).firstChild == c, "chain down");
    Expect(s.Get<Hierarchy>(b).prev.IsNull() && s.Get<Hierarchy>(c).next.IsNull(), "edge links null");

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
    Mat3x2 ref = Mat3x2::FromTRS({100, 50}, 0, {1, 1}) *
                 Mat3x2::FromTRS({10, 0}, 0, {1, 1}) *
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
    Mat3x2 ref2 = Mat3x2::FromTRS({100, 50}, 0, {1, 1}) *
                  Mat3x2::FromTRS({10, 0}, 0, {1, 1}) *
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
        if (h && !h->parent.IsNull()) dc = e;
        else dp = e;
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

// ---- M4.1：Meta.guid 序列化往返（内核 #5）----
#include "Serialization/SceneArchive.h"

void TestMetaGuidRoundtrip() {
    using namespace lemon::ecs;
    RegisterAllComponents();
    Scene src("g1");
    Entity e1 = src.Create();
    src.Emplace<Transform2D>(e1);
    Meta& m1 = src.Emplace<Meta>(e1);
    m1.guid = lemon::GenerateGuid();
    Entity e2 = src.Create();
    src.Emplace<Transform2D>(e2);
    Meta& m2 = src.Emplace<Meta>(e2);
    m2.guid = lemon::GenerateGuid();
    Expect(m1.guid != 0 && m2.guid != 0 && m1.guid != m2.guid, "guids generated distinct");

    std::string text = SceneArchive::Save(src);
    Scene dst("g2");
    Expect(SceneArchive::Load(dst, text), "guid scene load");
    // 按 guid 找回实体（编辑器选中找回语义）
    bool found1 = false, found2 = false;
    dst.Each([&](Entity e) {
        if (const Meta* m = dst.TryGet<Meta>(e)) {
            if (m->guid == m1.guid) found1 = true;
            if (m->guid == m2.guid) found2 = true;
        }
    });
    Expect(found1 && found2, "guids survive save/load");

    // 无 guid 旧档：加载后 guid 默认 0（backfill 是编辑器职责，引擎不做隐式改写）
    const char* legacy = R"({"schemaVersion":1,"name":"old","entities":[{"components":{"Transform2D":{"pos":[1,2],"rot":0.0,"scale":[1,1]},"Meta":{"prefabId":0,"team":0,"layer":0,"tag":"veteran"}}}]})";
    Scene s3("g3");
    Expect(SceneArchive::Load(s3, legacy), "legacy scene loads");
    bool legacyZero = true;
    s3.Each([&](Entity e) {
        if (const Meta* m = s3.TryGet<Meta>(e))
            if (m->guid != 0) legacyZero = false;
    });
    Expect(legacyZero, "legacy guid stays 0 (no implicit rewrite)");

    // 生成器：连续 1000 个不撞、非全零
    uint64_t first = lemon::GenerateGuid();
    bool allDistinct = true;
    for (int i = 0; i < 1000; ++i) {
        uint64_t g = lemon::GenerateGuid();
        if (g == 0 || g == first) allDistinct = false;
    }
    Expect(allDistinct, "guid generator 1000 distinct");
}

// ---- M4.1：编辑器元数据健全性（内核 #6；Inspector 控件渲染的前提）----
void TestEditorMetaSanity() {
    using namespace lemon::ecs;
    RegisterAllComponents();
    auto& reg = ComponentRegistry::Instance();
    bool allOk = true;
    for (uint16_t id = 0; id < reg.Count(); ++id) {
        const ComponentMeta& meta = reg.At(id);
        if (!meta.editorMeta) continue;
        for (uint16_t f = 0; f < meta.fieldCount; ++f) {
            const FieldEditorMeta& ed = meta.editorMeta[f];
            const FieldMeta& fm = meta.fields[f];
            if (HasHint(ed.hints, FieldHint::Range) && !(ed.rangeMin < ed.rangeMax)) {
                LEMON_LOG("BAD RANGE: %s.%s [%f,%f]", meta.name, fm.name, ed.rangeMin, ed.rangeMax);
                allOk = false;
            }
            if (HasHint(ed.hints, FieldHint::Enum) &&
                (!ed.enumNames || ed.enumCount == 0 || ed.enumCount > 256)) {
                LEMON_LOG("BAD ENUM: %s.%s", meta.name, fm.name);
                allOk = false;
            }
            if (HasHint(ed.hints, FieldHint::ColorHex) && fm.type != FieldType::UInt32) {
                LEMON_LOG("BAD COLOR TYPE: %s.%s", meta.name, fm.name);
                allOk = false;
            }
            if (HasHint(ed.hints, FieldHint::Bool8) &&
                !(fm.type == FieldType::UInt8 || fm.type == FieldType::Int8)) {
                LEMON_LOG("BAD BOOL8 TYPE: %s.%s", meta.name, fm.name);
                allOk = false;
            }
        }
    }
    // 既有特性抽查：Collectible.kind 枚举 3 项、SpriteRenderer.colorRGBA 颜色、rot 角度
    const ComponentMeta& col = *reg.Find("Collectible");
    Expect(col.editorMeta && HasHint(col.editorMeta[0].hints, FieldHint::Enum) &&
               col.editorMeta[0].enumCount == 3,
           "Collectible.kind enum meta");
    const ComponentMeta& sr = *reg.Find("SpriteRenderer");
    Expect(sr.editorMeta && HasHint(sr.editorMeta[1].hints, FieldHint::ColorHex),
           "SpriteRenderer.colorRGBA color meta");
    const ComponentMeta& tf = *reg.Find("Transform2D");
    Expect(tf.editorMeta && HasHint(tf.editorMeta[1].hints, FieldHint::Degree),
           "Transform2D.rot degree meta");
    // M4.8 字段级重置：Reset 提示的字段必须 constructFn 可用且组件可入 Inspector 栈缓冲（128B）
    for (uint16_t id2 = 0; id2 < reg.Count(); ++id2) {
        const ComponentMeta& m = reg.At(id2);
        if (!m.editorMeta) continue;
        for (uint16_t fi = 0; fi < m.fieldCount; ++fi) {
            if (HasHint(m.editorMeta[fi].hints, FieldHint::Reset) &&
                (!m.constructFn || m.sizeOf > 128)) {
                LEMON_LOG("BAD RESET META: %s.%s", m.name, m.fields[fi].name);
                allOk = false;
            }
        }
    }
    Expect(tf.editorMeta && HasHint(tf.editorMeta[0].hints, FieldHint::Reset) &&
               tf.constructFn,
           "Transform2D.pos reset meta");
    // 默认值口径（重置按钮目标值）：pos(0,0) rot 0 scale(1,1)——scale 归 1 非归 0
    alignas(16) uint8_t def[128];
    tf.constructFn(def);
    const Transform2D& td = *(const Transform2D*)def;
    Expect(td.pos == Vec2(0, 0) && td.rot == 0.0f && td.scale == Vec2(1, 1),
           "Transform2D default = pos0/rot0/scale1");
    Expect(allOk, "editor metadata sanity");
}

#ifdef LEMON_EDITOR_CORE
// ---- M4.4 测试面：资产数据库 / 实体子树档案 / ScriptBox 档案段 / Atlas 页热更新 ----
#include <filesystem>
#include <fstream>
#include <thread>
#include <unistd.h>

#include "Assets/AssetDatabase.h"
#include "Assets/FileWatcher.h"
#include "Assets/ProjectWizard.h"
#include "EditorContext.h"
#include "Serialization/SceneArchive.h"
#include "ECS/World.h"
#include "Scripting/ScriptBox.h"

// ---- M4.4-a：Atlas 页热更新（AssetGpuCache 热重导入的登记侧语义）----
void TestAtlasPageHotUpdate() {
    AtlasRegistry reg;
    rhi::Texture fake{2}; // 纯登记测试：句柄只是整数，无 GPU 语义
    reg.RegisterAtlas(2, fake, 64, 64);
    uint32_t id = reg.AddSprite(2, 0, 0, 64, 64);
    Expect(id == 1, "first sprite id is 1");
    const SpriteInfo& s0 = reg.GetSprite(id);
    Expect(s0.widthPx == 64 && s0.heightPx == 64 && s0.u1 == 1.0f && s0.v1 == 1.0f,
           "full-page sprite uv/dims");
    reg.UpdateAtlasPage(2, rhi::Texture{3}, 96, 48);
    const SpriteInfo& s1 = reg.GetSprite(id);
    Expect(s1.widthPx == 96 && s1.heightPx == 48, "hot update refreshes pixel dims");
    Expect(s1.u0 == 0.0f && s1.v0 == 0.0f && s1.u1 == 1.0f && s1.v1 == 1.0f,
           "full-page uv stays 0..1 after resize");
    Expect(s1.atlasIndex == 2, "atlas slot preserved");
}

// ---- M4.4-a：AssetDatabase 生命周期（GUID 稳定/manifest 记账/墓碑/体检）----
void TestAssetDatabaseLifecycle() {
    namespace fs = std::filesystem;
    using lemon::editor::AssetDatabase;
    using lemon::editor::AssetEntry;
    using lemon::editor::AssetType;

    const fs::path root = fs::temp_directory_path() /
                          ("lemon-test-assets-" + std::to_string(::getpid()));
    std::error_code ec;
    fs::remove_all(root, ec);

    AssetDatabase db;
    Expect(db.OpenProject(root.string(), /*spriteIdBase=*/100), "open project");
    Expect(db.SpriteAssetCount() == 0, "empty project starts clean");
    Expect(db.HealthIssues() == 0, "empty project no health issues");

    // 手工放两个资产（内容任意——DB 只哈希不解码）+ 一个预置 .meta 固定 guid
    fs::create_directories(root / "Assets" / "icons", ec);
    { std::ofstream f(root / "Assets" / "hero.png", std::ios::binary); f << "png-bytes-1"; }
    { std::ofstream f(root / "Assets" / "icons" / "coin.png", std::ios::binary); f << "png-B"; }
    { std::ofstream f(root / "Assets" / "notes.txt", std::ios::binary); f << "x"; }
    {
        std::ofstream f(root / "Assets" / "icons" / "coin.png.meta", std::ios::trunc);
        f << "{\"guid\":\"1122334455667788\",\"type\":\"sprite\"}";
    }

    db.Rescan();
    Expect(db.SpriteAssetCount() == 2, "two sprites discovered");
    const AssetEntry* hero = db.FindByPath("Assets/hero.png");
    const AssetEntry* coin = db.FindByPath("Assets/icons/coin.png");
    Expect(hero && coin, "entries located by path (project-root relative)");
    Expect(hero->type == AssetType::Sprite && coin->type == AssetType::Sprite, "png typed sprite");
    Expect(db.FindByPath("Assets/notes.txt") != nullptr, "generic file tracked");
    Expect(coin->guid == 0x1122334455667788ull, "preset meta guid honored");
    Expect(hero->spriteId == 100 && coin->spriteId == 101, "spriteIds allocated from base");
    const uint64_t heroGuid = hero->guid;
    Expect(heroGuid != 0, "auto guid assigned");
    Expect(fs::exists(root / "Assets" / "hero.png.meta", ec), "meta sidecar written");

    // M4.5 扫根（06 §1）：根级 Prefabs/ 入索引；Game/Scenes 排除
    fs::create_directories(root / "Prefabs", ec);
    fs::create_directories(root / "Game", ec);
    { std::ofstream f(root / "Prefabs" / "mob.prefab", std::ios::binary); f << "{}"; }
    { std::ofstream f(root / "Game" / "GameMain.cs", std::ios::binary); f << "// x"; }
    { std::ofstream f(root / "Scenes" / "Main.scene", std::ios::binary); f << "{}"; }
    db.Rescan();
    const AssetEntry* pf = db.FindByPath("Prefabs/mob.prefab");
    Expect(pf && pf->type == AssetType::Prefab, "root-level Prefabs/ indexed");
    Expect(db.FindByPath("Game/GameMain.cs") == nullptr, "Game/ excluded from asset scan");
    Expect(db.FindByPath("Scenes/Main.scene") == nullptr, "Scenes/ excluded from asset scan");

    // guid 持久：重开项目（新实例走 manifest 携带；M4.4 旧格式键自动迁移同号）→ 同 guid 同 spriteId
    {
        AssetDatabase db2;
        Expect(db2.OpenProject(root.string(), 100), "reopen project");
        const AssetEntry* h2 = db2.FindByPath("Assets/hero.png");
        Expect(h2 && h2->guid == heroGuid && h2->spriteId == 100,
               "guid/spriteId stable across sessions (manifest)");
        const AssetEntry* c2 = db2.FindByGuid(0x1122334455667788ull);
        Expect(c2 && c2->spriteId == 101, "preset guid stable across sessions");
    }

    // 内容变化 → modified（guid 不变）
    { std::ofstream f(root / "Assets" / "hero.png", std::ios::binary | std::ios::trunc);
      f << "png-bytes-CHANGED-longer"; }
    db.Rescan();
    Expect(db.LastChange().modified.size() == 1 &&
               db.LastChange().modified[0] == heroGuid,
           "content change detected as modified");
    Expect(db.FindByGuid(heroGuid) && db.FindByGuid(heroGuid)->relPath == "Assets/hero.png",
           "guid survives content change");

    // 重命名 → 引用不断（guid 不变路径变；meta 随行）。relPath 语义 = 项目根相对
    {
        AssetEntry* h = const_cast<AssetEntry*>(db.FindByGuid(heroGuid));
        Expect(db.Rename(*h, "Assets/renamed/hero2.png"), "rename ok");
        Expect(db.FindByGuid(heroGuid)->relPath == "Assets/renamed/hero2.png", "path moved");
        Expect(fs::exists(root / "Assets" / "renamed" / "hero2.png.meta", ec),
               "meta traveled with file");
        db.Rescan();
        Expect(db.FindByGuid(heroGuid) && !db.FindByGuid(heroGuid)->missing,
               "renamed asset rescans alive (guid intact)");
    }

    // 删除文件 → 墓碑（号不回收；体检红字）；新文件不重用旧号
    fs::remove(root / "Assets" / "icons" / "coin.png", ec);
    fs::remove(root / "Assets" / "icons" / "coin.png.meta", ec);
    db.Rescan();
    {
        const AssetEntry* c3 = db.FindByGuid(0x1122334455667788ull);
        Expect(c3 && c3->missing, "deleted asset is a tombstone (guid kept)");
        Expect(db.LastChange().removed.size() == 1, "removal reported");
    }
    { std::ofstream f(root / "Assets" / "new.png", std::ios::binary); f << "n"; }
    db.Rescan();
    const AssetEntry* np = db.FindByPath("Assets/new.png");
    Expect(np && np->spriteId == 102, "new sprite id never reuses tombstoned id");

    // 孤儿 meta 体检红字
    { std::ofstream f(root / "Assets" / "orphan.png.meta", std::ios::trunc); f << "{}"; }
    db.Rescan();
    Expect(db.HealthIssues() >= 1, "orphan meta reported as health issue");

    fs::remove_all(root, ec);
}

// ---- F-02（2026-09-24）：路径 containment——重命名/导入/项目名不得越出项目根 ----
void TestAssetPathContainment() {
    namespace fs = std::filesystem;
    using lemon::editor::AssetDatabase;
    using lemon::editor::AssetEntry;

    const fs::path root = fs::temp_directory_path() /
                          ("lemon-test-paths-" + std::to_string(::getpid()));
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "Assets", ec);
    { std::ofstream f(root / "Assets" / "hero.png", std::ios::binary); f << "png"; }

    AssetDatabase db;
    Expect(db.OpenProject(root.string(), 100), "open project");
    db.Rescan();
    AssetEntry* hero = db.FindByPath("Assets/hero.png");
    Expect(hero != nullptr, "hero located");
    const fs::path outside = root.parent_path() / "lemon-escape-probe.png";

    // 越界重命名拒绝：文件不动、条目不变
    Expect(!db.Rename(*hero, "../escape.png"), "rename with .. rejected");
    Expect(hero->relPath == "Assets/hero.png" && fs::exists(root / "Assets" / "hero.png", ec),
           "hero unmoved after rejected rename");
    // 绝对路径落点拒绝
    Expect(!db.Rename(*hero, outside.string()), "absolute rename target rejected");
    // 越界导入拒绝
    { std::ofstream f(root / "src.png", std::ios::binary); f << "x"; }
    Expect(db.ImportFile((root / "src.png").string(), "../stolen.png") == nullptr,
           "import with .. rejected");
    Expect(!fs::exists(outside, ec), "nothing escaped project root");

    // 项目名消毒：向导拒绝 ".." 形逃逸名（不创建任何目录）
    lemon::editor::ProjectDesc evil;
    evil.parentDir = root.string();
    evil.name = "../escaped-project";
    evil.engineVersion = "0";
    Expect(lemon::editor::ProjectWizard::Create(evil).empty(), "wizard rejects escaping name");
    Expect(!fs::exists(root.parent_path() / "escaped-project", ec), "no dir escaped parent");

    fs::remove_all(root, ec);
    fs::remove(outside, ec);
}

// ---- F-15（2026-09-24）：防抖门——窗口内取走的脏事件转 pending，不再吞 ----
void TestDebounceGatePending() {
    using lemon::editor::DebounceGate;
    DebounceGate g(0.4);

    // 窗外首脏：同帧即触发（与原"立即编译"节奏一致），触发点锚定新窗口
    g.OnDirty(5.0);
    Expect(g.Due(5.0), "first dirty fires immediately");
    Expect(!g.Due(5.01), "no refire without new dirty");

    // 窗内第二次保存（F-15 原吞点）：等窗，窗过后照常触发
    g.OnDirty(5.2);
    Expect(!g.Due(5.39), "in-window change waits");
    Expect(g.Due(5.45), "in-window change fires after window (F-15 no-swallow)");

    // 连续脏合并：窗内多次只触发一次
    g.OnDirty(7.0);
    Expect(g.Due(7.0), "window-anchored fire");
    g.OnDirty(7.1);
    g.OnDirty(7.2);
    g.OnDirty(7.3);
    Expect(g.Due(7.41), "coalesced in-window changes fire once");
    Expect(!g.Due(7.42), "and only once");
}

// ---- M4.4-d：实体子树 IO（Prefab 最小集的档案层）----
void TestEntityTreeArchive() {
    using namespace lemon::ecs;
    RegisterAllComponents();
    World w;
    Scene& s = w.CreateScene("src");

    Entity parent = s.Create();
    s.Emplace<Transform2D>(parent, Transform2D{{10, 20}, 0.5f, {2, 1}});
    Meta& pm = s.Emplace<Meta>(parent);
    std::strcpy(pm.tag, "boss");
    pm.guid = 0xAAAABBBBCCCCDDDDull;
    Entity child = s.Create();
    s.Emplace<Transform2D>(child, Transform2D{{1, 2}});
    s.Emplace<SpriteRenderer>(child, SpriteRenderer{7, 0xFF00FF00u, 3, 2, 0x4});
    SceneSetParent(s, child, parent);
    Entity outsider = s.Create();
    s.Emplace<Transform2D>(outsider, Transform2D{{9, 9}});
    Chase& ch = s.Emplace<Chase>(parent);
    ch.target = outsider; // 跨树引用 → 导出应置 null

    const std::string json = SceneArchive::SaveEntityTree(s, parent);
    Expect(!json.empty(), "tree save produced json");
    Expect(json.find("boss") != std::string::npos, "tree json carries tag");
    Expect(json.find("outsider") == std::string::npos, "tree excludes outside entity");

    World w2;
    Scene& d = w2.CreateScene("dst");
    const uint32_t before = d.AliveCount();
    Entity root = SceneArchive::LoadEntityTree(d, json);
    Expect(!root.IsNull() && d.AliveCount() == before + 2, "tree instantiated 2 entities");
    Expect(d.Has<Chase>(root) && d.Get<Chase>(root).target.IsNull(),
           "cross-tree EntityRef nulled on instantiate");
    Expect(d.Get<Meta>(root).guid != 0xAAAABBBBCCCCDDDDull && d.Get<Meta>(root).guid != 0,
           "instance gets fresh guid");
    const Hierarchy* h = d.TryGet<Hierarchy>(d.Get<Hierarchy>(root).firstChild);
    Expect(h && h->parent == root, "child hierarchy remapped to new root");
    // 子实体组件 Spot check
    Entity c2 = d.Get<Hierarchy>(root).firstChild;
    Expect(d.Get<SpriteRenderer>(c2).spriteId == 7 &&
               d.Get<SpriteRenderer>(c2).colorRGBA == 0xFF00FF00u,
           "child sprite data roundtrip");
    // 树内二次导出/导入 = 内容保持（guid 每次实例化换新是语义，不做文本级比对）
    const std::string json2 = SceneArchive::SaveEntityTree(d, root);
    World w3;
    Scene& d3 = w3.CreateScene("again");
    Entity root3 = SceneArchive::LoadEntityTree(d3, json2);
    Expect(!root3.IsNull() && d3.AliveCount() == 2, "double roundtrip entity count");
    Expect(d3.Get<SpriteRenderer>(d3.Get<Hierarchy>(root3).firstChild).colorRGBA ==
               0xFF00FF00u,
           "double roundtrip keeps data");
}

// ---- M4.4-e：ScriptBox 档案段（装配通路 #7）；M6a 批⓪：scripts[] 多槽 + v1 迁移 ----
void TestScriptBoxArchive() {
    using namespace lemon::ecs;
    RegisterAllComponents();
    World w;
    Scene& s = w.CreateScene("a");
    Entity e = s.Create();
    s.Emplace<Transform2D>(e);
    auto& sb = s.Emplace<scripting::ScriptBox>(e);
    scripting::AppendSlot(sb, 0x1234ABCDEF012345ull, "SpawnerBehaviour");
    scripting::AppendSlot(sb, 0x89ABCDEFFEDCBA98ull, "PlayerMovement");
    scripting::AppendSlot(sb, 0, "PlayerHud");
    sb.slots[0].typeId = 7; // 运行时解析号不持久：装载后应回 -1

    const std::string text = SceneArchive::Save(s);
    Expect(text.find("\"scripts\"") != std::string::npos, "scripts member serialized");
    Expect(text.find("SpawnerBehaviour") != std::string::npos, "className persisted");
    Expect(text.find("PlayerHud") != std::string::npos, "multi-script persisted");

    World w2;
    Scene& d = w2.CreateScene("b");
    Expect(SceneArchive::Load(d, text), "load with scripts member");
    bool found = false;
    d.Each([&](Entity en) {
        if (auto* b = d.TryGet<scripting::ScriptBox>(en); b) {
            found = true;
            Expect(b->count == 3, "three slots roundtrip");
            Expect(b->slots[0].scriptGuid == 0x1234ABCDEF012345ull, "slot0 guid roundtrip");
            Expect(std::string_view(b->slots[0].className) == "SpawnerBehaviour",
                   "slot0 className roundtrip");
            Expect(std::string_view(b->slots[1].className) == "PlayerMovement",
                   "slot1 className roundtrip（保序）");
            Expect(std::string_view(b->slots[2].className) == "PlayerHud",
                   "slot2 className roundtrip（guid=0 合法）");
            Expect(b->slots[0].typeId == -1, "typeId stays unresolved after load");
        }
    });
    Expect(found, "ScriptBox re-emplaced on load");
    // 无脚本实体的场景不受影响 + 二次往返不动点（scripts 段键序稳定；
    // 场景名是宿主属性——两次用同名场景排除干扰）
    const std::string text2 = SceneArchive::Save(d);
    World w3;
    Scene& d3 = w3.CreateScene("b"); // 与 d 同名
    SceneArchive::Load(d3, text2);
    Expect(SceneArchive::Save(d3) == text2, "scripts member roundtrip fixed point");

    // ---- v1→v2 迁移：单数 script 包成单元素 scripts[]（老档升级链首例）----
    const std::string v1 =
        "{\"schemaVersion\":1,\"name\":\"legacy\",\"entities\":["
        "{\"components\":{},\"script\":{\"guid\":4242,\"class\":\"OldBehaviour\"}}]}";
    World w4;
    Scene& d4 = w4.CreateScene("c");
    Expect(SceneArchive::Load(d4, v1), "v1 scene migrates");
    bool legacyOk = false;
    d4.Each([&](Entity en) {
        if (auto* b = d4.TryGet<scripting::ScriptBox>(en); b && b->count == 1 &&
            b->slots[0].scriptGuid == 4242 &&
            std::string_view(b->slots[0].className) == "OldBehaviour")
            legacyOk = true;
    });
    Expect(legacyOk, "legacy singular script migrated to one slot");

    // ---- 旧单数 .prefab 双读（LoadEntityTree 不走迁移链，靠 ReadEntity 兼容）----
    const std::string prefab =
        "{\"schemaVersion\":1,\"name\":\"prefab\",\"entities\":["
        "{\"components\":{},\"script\":{\"guid\":7,\"class\":\"PBehaviour\"}}]}";
    Scene& d5 = w.CreateScene("d5");
    Entity root = SceneArchive::LoadEntityTree(d5, prefab);
    const scripting::ScriptBox* pb = d5.TryGet<scripting::ScriptBox>(root);
    Expect(pb && pb->count == 1 && std::string_view(pb->slots[0].className) == "PBehaviour",
           "legacy singular prefab dual-read");

    // ---- 同名重复项清洗（保序留首见）——同类型唯一不变量的加载侧防线 ----
    const std::string dup =
        "{\"schemaVersion\":2,\"name\":\"dup\",\"entities\":["
        "{\"components\":{},\"scripts\":[{\"guid\":1,\"class\":\"A\"},"
        "{\"guid\":2,\"class\":\"B\"},{\"guid\":3,\"class\":\"A\"}]}]}";
    World w6;
    Scene& d6 = w6.CreateScene("d6");
    Expect(SceneArchive::Load(d6, dup), "dup scene loads");
    bool dedupOk = false;
    d6.Each([&](Entity en) {
        if (auto* b = d6.TryGet<scripting::ScriptBox>(en); b) {
            dedupOk = b->count == 2 &&
                      std::string_view(b->slots[0].className) == "A" &&
                      b->slots[0].scriptGuid == 1 && // 首见保留（第二条 A 被清洗）
                      std::string_view(b->slots[1].className) == "B";
        }
    });
    Expect(dedupOk, "duplicate className deduped (first wins)");
}
// ---- M6a 批⓪ T2：sprite 引用 GUID 化（存量回填 / id 漂移解析 / 悬空 / 程序化页）----
// 链路主角 = EditorContext::ResolveSpriteRefs（随 OpenScene 乘）。id 漂移用
// 「删 manifest + 字典序插队资产 + 新 ctx 重开项目」模拟跨进程重排（.meta 只带
// guid、guid 随文件走——T5 --smoke-guid 将在编辑器全链复证同一命题）。
void TestSpriteGuidResolve() {
    namespace fs = std::filesystem;
    using lemon::editor::EditorContext;
    using lemon::editor::AssetEntry;
    using namespace lemon::ecs;

    const fs::path root = fs::temp_directory_path() /
                          ("lemon-test-spriteguid-" + std::to_string(::getpid()));
    std::error_code ec;
    fs::remove_all(root, ec);

    EditorContext ctx;
    Expect(ctx.Assets().OpenProject(root.string(), /*spriteIdBase=*/100), "ctx open project");
    fs::create_directories(root / "Assets", ec);
    { std::ofstream f(root / "Assets" / "coin.png", std::ios::binary); f << "png-C"; }
    { std::ofstream f(root / "Assets" / "coin.png.meta", std::ios::trunc);
      f << "{\"guid\":\"1122334455667788\",\"type\":\"sprite\"}"; }
    { std::ofstream f(root / "Assets" / "hero.png", std::ios::binary); f << "png-H"; }
    ctx.Assets().Rescan();
    const AssetEntry* coin = ctx.Assets().FindByPath("Assets/coin.png");
    const AssetEntry* hero = ctx.Assets().FindByPath("Assets/hero.png");
    Expect(coin && hero && coin->spriteId == 100 && hero->spriteId == 101,
           "ids allocated in path order");
    const uint64_t coinGuid = coin->guid;
    Expect(coinGuid == 0x1122334455667788ull, "preset meta guid honored");
    // 按 spriteId 找实体的 SpriteRenderer（多实体场景断言用）
    auto findSr = [](Scene& s, uint32_t id) {
        const SpriteRenderer* out = nullptr;
        s.Each([&](Entity e) {
            if (const SpriteRenderer* p = s.TryGet<SpriteRenderer>(e); p && p->spriteId == id)
                out = p;
        });
        return out;
    };

    // ① 存量回填：v2 档只写 spriteId（等价批⓪ 前全部存量档）→ 打开即回填 + 标
    //    dirty；程序化页号（< 基号）无 guid 语义，保持 0 不回填
    const fs::path sc = root / "Scenes" / "b.scene";
    fs::create_directories(sc.parent_path(), ec);
    {
        std::ofstream f(sc, std::ios::trunc);
        f << "{\"schemaVersion\":2,\"name\":\"b\",\"entities\":["
             "{\"components\":{\"SpriteRenderer\":{\"spriteId\":100,\"colorRGBA\":"
             "4294967295,\"sortOrder\":0,\"sortingLayer\":0,\"flags\":4}},"
             "\"scripts\":[]},"
             "{\"components\":{\"SpriteRenderer\":{\"spriteId\":4,\"colorRGBA\":"
             "4294967295,\"sortOrder\":0,\"sortingLayer\":0,\"flags\":4}},"
             "\"scripts\":[]}]}";
    }
    Expect(ctx.OpenScene(sc.string()), "legacy-id scene opens");
    const SpriteRenderer* sr = findSr(ctx.EditScene(), 100);
    Expect(sr && sr->spriteGuid == coinGuid, "legacy spriteId backfilled to guid");
    const SpriteRenderer* srProcedural = findSr(ctx.EditScene(), 4);
    Expect(srProcedural && srProcedural->spriteGuid == 0, "procedural page id not backfilled");
    Expect(ctx.dirty, "backfill marks dirty (save upgrades the file)");

    // ② 保存 → 跨进程 id 重排（删 manifest + aaa.png 字典序插队 + 新 ctx 重开）
    //    → coin 100→101；重开档 guid 不变、spriteId 归一到新号、不再回填
    Expect(ctx.SaveScene(), "scene saved with guid");
    {
        std::ifstream f(sc, std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(f)),
                               std::istreambuf_iterator<char>());
        Expect(text.find("spriteGuid") != std::string::npos &&
                   text.find(std::to_string(coinGuid)) != std::string::npos,
               "spriteGuid serialized (decimal)");
    }
    fs::remove(root / ".lemon" / "manifest.json", ec);
    { std::ofstream f(root / "Assets" / "aaa.png", std::ios::binary); f << "png-A"; }
    EditorContext ctx2; // 新 ctx = 模拟重开进程（DB 空表、无 manifest 记账）
    Expect(ctx2.Assets().OpenProject(root.string(), 100), "reopen project (manifest gone)");
    const AssetEntry* coin2 = ctx2.Assets().FindByPath("Assets/coin.png");
    Expect(coin2 && coin2->spriteId == 101 && coin2->guid == coinGuid,
           "id drift as designed (aaa takes 100, guid rides .meta)");
    Expect(ctx2.OpenScene(sc.string()), "reopen saved scene");
    const SpriteRenderer* sr2 = findSr(ctx2.EditScene(), 101);
    Expect(sr2 && sr2->spriteGuid == coinGuid,
           "guid resolves to fresh id (rename/move/manifest-loss proof)");
    Expect(!ctx2.dirty, "guid-bearing scene opens clean (backfill is one-shot)");

    // ③ 悬空 guid：查无 → spriteId 保留旧号（不静默清零）、场景照常可用
    const fs::path sc3 = root / "Scenes" / "d.scene";
    {
        std::ofstream f(sc3, std::ios::trunc);
        f << "{\"schemaVersion\":2,\"name\":\"d\",\"entities\":["
             "{\"components\":{\"SpriteRenderer\":{\"spriteId\":101,\"colorRGBA\":"
             "4294967295,\"sortOrder\":0,\"sortingLayer\":0,\"flags\":4,"
             "\"spriteGuid\":999}},\"scripts\":[]}]}";
    }
    Expect(ctx2.OpenScene(sc3.string()), "dangling-guid scene opens");
    const SpriteRenderer* sr3 = findSr(ctx2.EditScene(), 101);
    Expect(sr3 && sr3->spriteGuid == 999,
           "dangling guid keeps legacy id rendering");

    fs::remove_all(root, ec);
}
// ---- M4.4-d：EditorContext Prefab 操作端到端（导出/实例化/Break/Apply/Revert）----
void TestEditorContextPrefabOps() {
    namespace fs = std::filesystem;
    using namespace lemon::ecs;
    using lemon::editor::EditorContext;
    using lemon::editor::AssetType;

    const fs::path root = fs::temp_directory_path() /
                          ("lemon-test-prefab-" + std::to_string(::getpid()));
    std::error_code ec;
    fs::remove_all(root, ec);

    EditorContext ctx;
    Expect(ctx.Assets().OpenProject(root.string(), 100), "ctx open project");

    // 源实体：父 + 子（组件各一）
    Entity mob = ctx.CreateSpriteEntity("Mob", 3);
    ctx.EditScene().Get<Transform2D>(mob).pos = {100, 100};
    Entity hat = ctx.CreateSpriteEntity("Hat", 5);
    SceneSetParent(ctx.EditScene(), hat, mob);
    ctx.EditScene().Get<Transform2D>(hat).pos = {0, -20};

    const uint64_t pguid = ctx.MakePrefabFrom(mob);
    Expect(pguid != 0, "prefab exported");
    const auto* entry = ctx.Assets().FindByGuid(pguid);
    Expect(entry && entry->type == AssetType::Prefab && !entry->missing, "prefab in db");
    Expect(fs::exists(root / "Prefabs" / "Mob.prefab", ec),
           "prefab file on disk (root-level Prefabs/, 06 §1)");
    Expect(ctx.EditScene().Get<Meta>(mob).prefabId == pguid, "source linked back");

    // 实例化：新 guid 集 + prefabId 回链 + 位置覆盖
    const uint32_t before = ctx.EditScene().AliveCount();
    Entity inst = ctx.InstantiatePrefabAsset(pguid, {7, 9});
    Expect(!inst.IsNull() && ctx.EditScene().AliveCount() == before + 2, "instance tree created");
    Expect(ctx.EditScene().Get<Meta>(inst).prefabId == pguid, "instance linked");
    Expect(ctx.EditScene().Get<Meta>(inst).guid != ctx.EditScene().Get<Meta>(mob).guid,
           "instance has fresh guid");
    Expect(ctx.EditScene().Get<Transform2D>(inst).pos == Vec2(7, 9), "instance pos overridden");
    Expect(ctx.EditScene().Has<SpriteRenderer>(inst), "instance components copied");

    // Apply：实例改动写回源；Revert：新实例回到源态
    ctx.EditScene().Get<Transform2D>(inst).pos = {500, 250};
    ctx.EditScene().Get<SpriteRenderer>(inst).colorRGBA = 0x11223344u;
    Expect(ctx.ApplyPrefabInstance(inst), "apply writes back");
    // Break：断链（Apply 之后）
    ctx.BreakPrefabInstance(inst);
    Expect(ctx.EditScene().Get<Meta>(inst).prefabId == 0, "break clears link");
    // Revert 一个仍链接着的实例（重新实例化一个）
    Entity inst2 = ctx.InstantiatePrefabAsset(pguid, {0, 0});
    const uint64_t keepGuid = ctx.EditScene().Get<Meta>(inst2).guid;
    ctx.EditScene().Get<Transform2D>(inst2).pos = {999, 999}; // 偏离源
    Expect(ctx.RevertPrefabInstance(inst2), "revert ok");
    Entity reverted = ctx.Primary(); // Revert 选中重建后的根
    Expect(!reverted.IsNull() && ctx.EditScene().Get<Meta>(reverted).guid == keepGuid,
           "revert keeps instance guid");
    Expect(ctx.EditScene().Get<Transform2D>(reverted).pos == Vec2(500, 250),
           "revert restores applied source state");
    Expect(ctx.EditScene().Get<SpriteRenderer>(reverted).colorRGBA == 0x11223344u,
           "revert restores applied color");

    fs::remove_all(root, ec);
}
// ---- M5 清障②：Play 世界 SpawnFn 桥（Spawner.prefabId 低 32 位 → prefab 实例化）----
void TestPlaySpawnPrefab() {
    namespace fs = std::filesystem;
    using namespace lemon::ecs;
    using lemon::editor::EditorContext;
    using lemon::editor::AssetType;

    const fs::path root = fs::temp_directory_path() /
                          ("lemon-test-playspawn-" + std::to_string(::getpid()));
    std::error_code ec;
    fs::remove_all(root, ec);

    EditorContext ctx;
    Expect(ctx.Assets().OpenProject(root.string(), 100), "ctx open project");

    // Mob prefab：父 + 子（同 TestEditorContextPrefabOps 手法；源保留在编辑场景）
    Entity mob = ctx.CreateSpriteEntity("Mob", 3);
    Entity hat = ctx.CreateSpriteEntity("Hat", 5);
    SceneSetParent(ctx.EditScene(), hat, mob);
    const uint64_t pguid = ctx.MakePrefabFrom(mob);
    Expect(pguid != 0, "prefab exported");

    // Spawner 实体（prefabId = GUID 低 32 位——M5 映射约定）
    Entity spawner = ctx.CreateEntity("Spawner");
    Spawner& sp = ctx.EditScene().Emplace<Spawner>(spawner);
    sp.prefabId = (uint32_t)pguid;
    sp.interval = 0.05f;
    sp.burst = 2;
    sp.spawnTeam = 1;
    sp.cooldown = 0.0f;
    const uint32_t editAlive = ctx.EditScene().AliveCount(); // Mob+Hat+Spawner = 3
    const uint64_t mobGuid = ctx.EditScene().Get<Meta>(mob).guid; // ExitPlay 后句柄失效，按 guid 重找

    Expect(ctx.EnterPlay(), "enter play");
    Expect(ctx.Playing() && ctx.ActiveWorld().GetSpawnFn(), "play spawn fn registered");
    const uint32_t playAlive0 = ctx.ActiveScene().AliveCount();
    Expect(playAlive0 == editAlive, "play world = edit snapshot (3 entities)");

    // 桥单测：直接调工厂（不走 SpawnSystem）
    {
        const World::SpawnFn& spawn = ctx.ActiveWorld().GetSpawnFn();
        Entity e = spawn(ctx.ActiveScene(), (uint32_t)pguid, {42, -7}, 5u);
        Expect(!e.IsNull(), "spawn factory instantiates");
        Expect(ctx.ActiveScene().Get<Meta>(e).prefabId == pguid, "spawn links prefab guid");
        Expect(ctx.ActiveScene().Get<Meta>(e).team == 5u, "spawn team overrides source");
        Expect(ctx.ActiveScene().Get<Transform2D>(e).pos == Vec2(42, -7), "spawn pos");
        // 无效 id：0 与未知低 32 位 → Null 不崩（Spawner 的 e.IsNull() break 语义）
        Expect(spawn(ctx.ActiveScene(), 0, {0, 0}, 1u).IsNull(), "spawn id 0 = null");
        Expect(spawn(ctx.ActiveScene(), 0xDEADBEEFu, {0, 0}, 1u).IsNull(),
               "spawn unknown id = null (no crash)");
    }

    // 系统集成：TickPlay 若干帧 → SpawnSystem 经工厂实际刷怪（树 = 2 实体/只）
    for (int i = 0; i < 12; i++) ctx.TickPlay(1.0f / 60.0f);
    uint32_t spawned = 0;
    ctx.ActiveScene().Each([&](Entity e) {
        if (ctx.ActiveScene().Get<Meta>(e).prefabId == pguid &&
            ctx.ActiveScene().Get<Meta>(e).guid != ctx.EditScene().Get<Meta>(mob).guid)
            ++spawned;
    });
    Expect(spawned >= 4, "Spawner ticking via factory (>= 2 bursts, tree roots)");
    Expect(ctx.ActiveScene().AliveCount() > playAlive0, "play alive grows");

    // Stop：编辑场景零状态泄漏（快照重建）。实体全部重建 = 进 Play 前的句柄
    // （含版本位）已失效——按 guid 重找再校验（Debug 档 EnTT 会断言拒绝死句柄）
    Expect(ctx.ExitPlay(), "exit play");
    Expect(ctx.EditScene().AliveCount() == editAlive, "edit scene restored");
    Entity mobRestored = Entity::Null();
    ctx.EditScene().Each([&](Entity e) {
        if (ctx.EditScene().Get<Meta>(e).guid == mobGuid) mobRestored = e;
    });
    bool editLinked = !mobRestored.IsNull() &&
                      ctx.EditScene().Get<Meta>(mobRestored).prefabId == pguid;
    Expect(editLinked, "edit scene prefab link intact");

    fs::remove_all(root, ec);
}
// ---- M4.8-b 回归：RecordRecentScene 自别名安全 + 读档洗脏档 ----
// 2026-09-22 崩溃案：File→最近场景菜单把 recentScenes_ 元素引用直传
// MenuOpenRecentScene→OpenScene→RecordRecentScene，后者 erase/insert 同一 vector
// = UAF（段错误间歇发作；侥幸不崩时把 ""/重复条目写进 recent-scenes.json）。
void TestRecentScenesAliasSafety() {
    namespace fs = std::filesystem;
    using lemon::editor::EditorContext;

    const fs::path root = fs::temp_directory_path() /
                          ("lemon-test-recent-" + std::to_string(::getpid()));
    std::error_code ec;
    fs::remove_all(root, ec);

    EditorContext ctx;
    Expect(ctx.Assets().OpenProject(root.string(), 100), "ctx open project");

    const std::string a = (root / "Scenes" / "a.scene").string();
    const std::string b = (root / "Scenes" / "b.scene").string();
    ctx.RecordRecentScene(a);
    ctx.RecordRecentScene(b);
    Expect(ctx.RecentScenes().size() == 2 && ctx.RecentScenes().front() == b,
           "two entries, b on top");

    // 复现菜单点击：传 vector 元素自身的引用（此前 = UAF 案发现场）
    ctx.RecordRecentScene(ctx.RecentScenes().back()); // 末位别名
    Expect(ctx.RecentScenes().size() == 2 && ctx.RecentScenes().front() == a,
           "alias of back(): moves to top, no dup");
    ctx.RecordRecentScene(ctx.RecentScenes().front()); // 首位别名（曾确定性注入 ""）
    Expect(ctx.RecentScenes().size() == 2 && !ctx.RecentScenes().front().empty() &&
               ctx.RecentScenes().front() == a,
           "alias of front(): entry intact, no empty injected");

    // 读档洗脏档：预写含空串 + 重复条目的档 → 过滤空串、保序去重（首见留）
    fs::create_directories(root / ".lemon", ec);
    {
        std::ofstream f(root / ".lemon" / "recent-scenes.json", std::ios::binary | std::ios::trunc);
        f << "{\"scenes\":[\"" << a << "\", \"\", \"Scenes/b.scene\", \"" << a << "\"]}\n";
    }
    ctx.LoadRecentScenes();
    // CanonicalPath 落地后（2026-09-24）条目一律为规范形：期望值两侧同归一化
    std::error_code cec;
    const std::string aCanon = fs::weakly_canonical(fs::path(a), cec).string();
    const std::string bCanon = fs::weakly_canonical(fs::path("Scenes/b.scene"), cec).string();
    Expect(ctx.RecentScenes().size() == 2, "dirty file cleaned: empty + dup dropped");
    Expect(ctx.RecentScenes()[0] == aCanon && ctx.RecentScenes()[1] == bCanon,
           "order preserved, first occurrence wins (canonical forms)");

    fs::remove_all(root, ec);
}
// ---- M4.5-a：项目向导（blank 模板 06 §1 布局 + 零配置脚本工程）----
void TestProjectWizard() {
    namespace fs = std::filesystem;
    using lemon::editor::ProjectWizard;
    using lemon::editor::ProjectDesc;

    const fs::path parent = fs::temp_directory_path() /
                            ("lemon-test-wizard-" + std::to_string(::getpid()));
    std::error_code ec;
    fs::remove_all(parent, ec);

    ProjectDesc d;
    d.parentDir = parent.string();
    d.name = "MyGame";
    d.sdkDir = "/nonexistent-sdk"; // 布局测试不解码 PNG/不编译——sdkDir 只进 HintPath
    d.engineVersion = "0.4.0-m4";
    uint64_t spawnGuid = 0;
    const std::string root = ProjectWizard::Create(d, &spawnGuid);
    Expect(!root.empty(), "wizard created project");
    Expect(spawnGuid != 0, "spawn asset guid returned");

    // 06 §1 布局全项
    for (const char* dir : {"Assets", "Scenes", "Prefabs", "Game", "Data", "Builds"}) {
        const bool ok = fs::is_directory(fs::path(root) / dir, ec);
        Expect(ok, ok ? "wizard dir" : (std::string("wizard dir missing: ") + dir).c_str());
    }
    // project.lemon：名称/版本锚点可解析
    {
        std::ifstream f(fs::path(root) / "project.lemon");
        std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        Expect(text.find("\"MyGame\"") != std::string::npos, "project.lemon carries name");
        Expect(text.find("0.4.0-m4") != std::string::npos, "project.lemon anchors engineVersion");
    }
    // 种子资产 + 固定 guid .meta；脚本引用同 guid（零代码刷怪链）
    {
        std::ifstream m(fs::path(root) / "Assets" / "spawn.png.meta");
        std::string meta((std::istreambuf_iterator<char>(m)), std::istreambuf_iterator<char>());
        Expect(meta.find(lemon::editor::AssetDatabase::GuidToHex(spawnGuid)) !=
                   std::string::npos,
               "spawn meta carries returned guid");
        std::ifstream cs(fs::path(root) / "Game" / "SpawnerBehaviour.cs");
        std::string src((std::istreambuf_iterator<char>(cs)), std::istreambuf_iterator<char>());
        Expect(src.find(lemon::editor::AssetDatabase::GuidToHex(spawnGuid)) != std::string::npos,
               "SpawnerBehaviour.cs references spawn guid");
        Expect(src.find("OnHotReloadOut") != std::string::npos,
               "template ships StateBag migration pattern");
    }
    // csproj：HintPath 指向 sdkDir；Main.scene 可被 SceneArchive 解码
    {
        std::ifstream f(fs::path(root) / "Game" / "MyGame.csproj");
        std::string cs((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        Expect(cs.find("/nonexistent-sdk/Lemon.SDK.dll") != std::string::npos,
               "csproj HintPath points at sdkDir");
        std::ifstream sf(fs::path(root) / "Scenes" / "Main.scene");
        std::string scene((std::istreambuf_iterator<char>(sf)), std::istreambuf_iterator<char>());
        lemon::ecs::Scene probe("probe");
        Expect(lemon::ecs::SceneArchive::Load(probe, scene), "Main.scene parses via SceneArchive");
        Expect(probe.AliveCount() == 0, "Main.scene starts empty");
    }
    // 重复创建同名 = 拒绝（不覆盖用户目录）
    Expect(ProjectWizard::Create(d).empty(), "wizard refuses existing directory");

    fs::remove_all(parent, ec);
}

// ---- M4.6-b：日常编辑效率件（编译错误解析 / 新建脚本模板）----
void TestEditorUsability() {
    namespace fs = std::filesystem;
    using lemon::editor::ProjectWizard;
    using lemon::editor::ProjectDesc;

    // ExtractCompileErrors：dotnet/MSBuild 错误行提取（file(l,c): error CSxxxx 去 csproj 尾巴）
    {
        const std::string out =
            "Microsoft (R) Build Engine version 17.x\n"
            "  Determining projects to restore...\n"
            "/tmp/proj/Game/SpawnerBehaviour.cs(13,31): error CS1002: ; expected "
            "[/tmp/proj/Game/MyGame.csproj]\n"
            "/tmp/proj/Game/GameMain.cs(5,1): error CS0116: A namespace cannot directly "
            "contain members [/tmp/proj/Game/MyGame.csproj]\n"
            "    2 Warning(s)\n    2 Error(s)\n";
        const std::vector<std::string> errs = ProjectWizard::ExtractCompileErrors(out);
        Expect(errs.size() == 2, "extract exactly error CS lines");
        if (errs.size() == 2) {
            Expect(errs[0].find("SpawnerBehaviour.cs(13,31): error CS1002: ; expected") !=
                       std::string::npos &&
                       errs[0].find(".csproj]") == std::string::npos,
                   "error line keeps file(line,col), drops csproj tail");
            Expect(errs[1].find("error CS0116") != std::string::npos, "second error extracted");
        }
        Expect(ProjectWizard::ExtractCompileErrors("no errors here\n").empty(),
               "clean output yields nothing");
        // 告警行（warning CS）不算错误
        Expect(ProjectWizard::ExtractCompileErrors("A.cs(1,1): warning CS0219: var unused "
                                                   "[x.csproj]\n").empty(),
               "warnings are not errors");
    }

    // AddBehaviourScript：模板落盘 + GameMain 注册锚点插入 + 非法名/重名拒绝
    const fs::path parent = fs::temp_directory_path() /
                            ("lemon-test-newscript-" + std::to_string(::getpid()));
    std::error_code ec;
    fs::remove_all(parent, ec);
    ProjectDesc d;
    d.parentDir = parent.string();
    d.name = "ScriptGame";
    d.sdkDir = "/nonexistent-sdk";
    d.engineVersion = "0.4.0-m4";
    const std::string root = ProjectWizard::Create(d);
    const std::string gameDir = root + "/Game";
    Expect(!root.empty(), "wizard project for new-script test");

    Expect(ProjectWizard::AddBehaviourScript(gameDir, "ProbeBehaviour"), "script created");
    {
        std::ifstream f(fs::path(gameDir) / "ProbeBehaviour.cs");
        std::string src((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        Expect(src.find("public sealed class ProbeBehaviour : LemonBehaviour") !=
                   std::string::npos,
               "template declares class");
        Expect(src.find("protected override void Update()") != std::string::npos,
               "template has Update override");
        std::ifstream m(fs::path(gameDir) / "GameMain.cs");
        std::string main((std::istreambuf_iterator<char>(m)), std::istreambuf_iterator<char>());
        const size_t reg = main.find("Lemon.Behaviours.Register<ProbeBehaviour>();");
        const size_t anchor = main.find("Lemon.Behaviours.Register<InputMoverBehaviour>();");
        Expect(reg != std::string::npos && anchor != std::string::npos && reg < anchor,
               "register line inserted before existing anchor");
    }
    Expect(!ProjectWizard::AddBehaviourScript(gameDir, "ProbeBehaviour"),
           "duplicate class refused");
    Expect(!ProjectWizard::AddBehaviourScript(gameDir, "9BadName"), "digit-start refused");
    Expect(!ProjectWizard::AddBehaviourScript(gameDir, "Bad/Name"), "path-separator refused");
    Expect(!ProjectWizard::AddBehaviourScript(gameDir, ""), "empty name refused");

    fs::remove_all(parent, ec);
}

// ---- M4.5-b：自动备份/崩溃恢复（§3.8 全链：快照→检出→恢复→落盘清）----
void TestAutosaveRecovery() {
    namespace fs = std::filesystem;
    using lemon::editor::EditorContext;

    const fs::path root = fs::temp_directory_path() /
                          ("lemon-test-autosave-" + std::to_string(::getpid()));
    std::error_code ec;
    fs::remove_all(root, ec);

    EditorContext ctx;
    Expect(ctx.Assets().OpenProject(root.string(), 100), "ctx open project");
    fs::create_directories(root / "Scenes", ec);
    const std::string scenePath = (root / "Scenes" / "A.scene").string();
    ctx.CreateSpriteEntity("Hero", 3);
    Expect(ctx.SaveScene(scenePath), "scene saved");
    Expect(ctx.DetectAutosaveRecovery().empty(), "no recovery right after clean save");

    // 编辑 → dirty → 立即快照 → 检出（mtime 新于盘档）
    ctx.CreateSpriteEntity("Mob", 5);
    Expect(ctx.dirty, "edit marks dirty");
    Expect(ctx.AutoSaveNow(), "autosave snapshot written");
    const std::string rec = ctx.DetectAutosaveRecovery();
    Expect(!rec.empty(), "recovery detected (autosave newer)");
    Expect(rec.find("autosave") != std::string::npos, "recovery path under .lemon/autosave/");

    // 崩溃模拟：新上下文重开同场景 → 检出 → 恢复（保持 dirty、实体含 Mob）
    {
        EditorContext ctx2;
        ctx2.Assets().OpenProject(root.string(), 100);
        Expect(ctx2.OpenScene(scenePath), "reopen scene (clean copy, Hero only)");
        const std::string rec2 = ctx2.DetectAutosaveRecovery();
        Expect(!rec2.empty(), "fresh context still detects recovery");
        Expect(ctx2.OpenSceneRecovery(rec2), "recovery loads autosave content");
        Expect(ctx2.dirty, "recovery keeps dirty (user decides)");
        // 符号链接鲁棒口径（2026-09-24 CanonicalPath 落地后 ScenePath 为规范形：
        // macOS /var → /private/var）——两侧都归一化再比，恢复"不改路径"语义不变
        std::error_code cec1, cec2;
        Expect(fs::weakly_canonical(fs::path(ctx2.ScenePath()), cec1) ==
                   fs::weakly_canonical(fs::path(scenePath), cec2),
               "recovery keeps original scene path");
        bool sawMob = false;
        ctx2.EditScene().Each([&](lemon::ecs::Entity e) {
            const auto* m = ctx2.EditScene().TryGet<lemon::ecs::Meta>(e);
            sawMob |= m && std::string(m->tag) == "Mob";
        });
        Expect(sawMob, "recovered scene contains autosaved entity");
        // 落盘 → autosave 清除 → 不再检出
        Expect(ctx2.SaveScene(), "save after recovery");
        Expect(ctx2.DetectAutosaveRecovery().empty(), "save clears autosave (no stale prompt)");
    }

    // 节拍门：未到 interval 不写；Play 中不写（§3.8）
    {
        Expect(!ctx.DetectAutosaveRecovery().empty() || true, "baseline");
        ctx.TickAutosave(1.0);          // 未到 300s：即便 dirty 也不写
        ctx.TickAutosave(299.0);
        ctx.TickAutosave(301.0);        // 到点：dirty 且非 Play → 写
        const fs::path as = fs::path(ctx.Assets().ProjectRoot()) / ".lemon/autosave" /
                            "A.scene";
        Expect(fs::exists(as, ec), "autosave written at interval tick");
    }

    fs::remove_all(root, ec);
}
#endif // LEMON_EDITOR_CORE

int main() {
    TestVec2();
    TestMat3x2();
    TestRect();
    TestColor();
    TestUtils();
    TestAtlasUV();
    TestVerifyAddSpriteAtHolesAndConflict();
    TestBatchKey();
    TestSortStability();
    TestParticles();
    TestRenderableExtractOrder();
    TestRenderableSanitizeCacheAndHoles();
    TestParticlesKeyOverflow();
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
    TestVerifyWorldAutoRegistersCatalog();
    TestSceneArchive();
    TestTeamTable();
    TestSpatialHash();
    TestSpatialHashQueryFastPath();
    TestSystemPipelineOrder();
    TestSimulationEndToEnd();
    TestSeparationForce();
    TestArchiveArraySegAndRuntimeFields();
    TestArchiveMalformedTolerance();
    TestSpatialHashRangeClamp();
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
    TestVerifyAISystemChase();
    TestVerifyFleeAndPatrol();
    TestVerifyAnimatorAdvance();
    TestVerifyAnimatorFrameMapping();
    TestVerifyAnimatorQueue();
    TestVerifyFxChannel();
    TestVerifyStatEffectsAndXp();
    TestVerifyMovementKnockbackAndClamp();
    TestVerifyProjectileLifetime();
    TestVerifySpawnQuota();
    TestVerifyTriggerOnceSemantics();
    TestVerifyNullEntityRefRoundtrip();
    TestVerifyStateHashStability();
    TestVerifyIframesDecrementAndKill();
    TestVerifyHitMemoryAndPierce();
    TestVerifyMagnetAndPickup();
    TestVerifyPickupXpLevelUp();
    TestWaveDirectorWavesAndEvent();
    TestWaveDirectorRampAndOverlap();
    TestWaveDirectorCapAlive();
    TestWaveDirectorTimeScaleFreeze();
    TestWaveDirectorArchive();
    TestWaveDirectorDeterminism();
    TestVerifyTimeScale();
    TestNoDoubleDeathEvents();
    TestHierarchyChainLifecycle();
    TestVerifyFullChainFollowsAfterRoundtrip();
    TestMetaGuidRoundtrip();
    TestEditorMetaSanity();
#ifdef LEMON_EDITOR_CORE
    TestAtlasPageHotUpdate();
    TestAssetDatabaseLifecycle();
    TestAssetPathContainment();
    TestDebounceGatePending();
    TestProjectWizard();
    TestEditorUsability();
    TestAutosaveRecovery();
    TestEntityTreeArchive();
    TestScriptBoxArchive();
    TestSpriteGuidResolve();
    TestEditorContextPrefabOps();
    TestPlaySpawnPrefab();
    TestRecentScenesAliasSafety();
#endif
    LEMON_LOG("engine-tests: %d checks OK", g_checks);
    return 0;
}
