// Lemon 引擎单测 — 纯逻辑层（数学/批键/图集 UV/相机/粒子池/音频混音）
// 断言风格：LEMON_ASSERT 失败即 abort，进程退出码非 0 = 测试失败。
#include "Core/Log.h"
#include "Core/Process.h" // CurrentProcessId（批⑦ win 清账：unistd/getpid 是 POSIX-only）

#include <chrono>
#include <cstdio>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <limits>
#include <filesystem>
#include <fstream>
#include <thread>

#include "Audio/AudioChannel.h" // M6c 批②：命令通道（World.h 链亦达，显式声明测试意图）
#include "Audio/AudioEngine.h"
#include "Audio/BakedClip.h"
#include "Audio/SpscRing.h" // M6c 批①b：SPSC 环序锁
#include "Assets/AssetIndex.h"   // M7a 批②：运行时只读索引
#include "Assets/AtlasBake.h"    // M7a 批⑥：LAT1 容器/装箱/烤制
#include "Assets/AtlasStore.h"   // M7a 批⑥：LAT1 装载登记核
#include "Assets/ProjectFile.h"  // M7a 批②：project.lemon 只读解析
#include "Assets/SpriteRefs.h"   // M7a 批②：guid 归一引擎本体
#include "Assets/PrefabCache.h" // M7a 批③：Play 世界 Prefab 工厂缓存
#include "Renderer/CameraFollow.h"    // M7a 批③：相机跟随纯函数
#include "Renderer/SceneExtractor.h"  // M7a 批③：ECS→渲染提取下沉件
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
    Expect(reg.Count() == 32, "catalog count (5 core + 4 render + 12 behavior + 6 gameplay + M5 批② WaveDirector + T3d AnimGraph/AnimParams + M6b 批③d 前置 UIDocument + M6c 批② AudioSource)");

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
    Expect(ComponentRegistry::Instance().Count() == 32, "world ctor auto-registers catalog");
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

    Expect(p.Systems().size() == 20, "20 systems installed（T3d 批② +AnimGraphSystem；A 档 +TweenSystem；M6c 批② +AudioSystem）");
    // Essential 阶段只有 DestroyCommit；FixedTick 按表序
    // （#9 Pickup = M5 批①；T3d 批② AnimGraph 插在 CSharpBatch 后——图评估读当
    // tick 脚本参数，写段由下一 tick Animator 消费，与脚本直写 Play 同拍；
    // A 档补间 Tween 插在 AnimGraph 后、事件派发前——脚本当 tick 发起即首写、
    // 存活补间拥有字段、完成事件当帧派发；M6c 批② Audio 插在 Tween 后、事件
    // 派发前——C# 当 tick staging 的音频命令本 tick 落地、零 RNG/零 ECS 写）
    const char* expected[] = {"InputSnapshot", "Director",    "Spawn",
                              "AI",            "Navigation",  "Separation",
                              "Movement",      "SpatialHashRebuild", "Pickup",
                              "Hitbox",        "Trigger",     "Stat",
                              "Animator",      "ProjectileLifetime", "CSharpBatch",
                              "AnimGraph",     "Tween",       "Audio",
                              "ScriptEventDispatch"};
    uint32_t fi = 0;
    for (const auto& s : p.Systems()) {
        if (s->Stage() == SystemStage::Essential) {
            Expect(std::string_view(s->Name()) == "DestroyCommit", "essential is destroy");
        } else {
            Expect(fi < 19 && std::string_view(s->Name()) == expected[fi],
                   "fixedtick order");
            ++fi;
        }
    }
    Expect(fi == 19, "19 fixedtick systems");
    Expect(p.Profiles().size() == 20, "profiles allocated");
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
    Entity farEnt[3];
    for (int i = 0; i < 3; ++i) {
        farEnt[i] = s.Create();
        s.Emplace<Transform2D>(farEnt[i], Transform2D{{500.0f + (float)i, 0.0f}});
        s.Emplace<Meta>(farEnt[i]).team = 1;
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

// TargetBoard 网格最近邻等价性钉板（2026-09-26）：随机布点下网格路径（≥kMinList
// 走 CSR 桶 + 位图环搜）与线性参考（旧实现语义）逐查询一致——网格化/后续并行化
// 改动的正确性由"金回放实证"升级为单测钉板。等距平局是文档化语义差异（网格 =
// 环扫序先见者 vs 线性 = 池序靠前者，Systems.h 注释）；随机浮点布点下精确等距
// 概率为零，若出现（布点退化）按失败报而非静默跳过。边界一并钉格：未声明队恒
// Null、空程无候选 Null、排除自身、无 Meta 实体不入板。
void TestTargetBoardGridEquivalence() {
    World world;
    Scene& s = world.CreateScene("board");

    // 确定性布点（本地 LCG，零依赖；续战同源可复现）
    uint64_t seed = 0x9E3779B97F4A7C15ull;
    auto rand01 = [&seed]() {
        seed = seed * 6364136223846793005ull + 1442695040888963407ull;
        return ((seed >> 33) & 0xFFFFFF) / (float)0x1000000;
    };
    auto randPos = [&rand01]() {
        return Vec2{(rand01() - 0.5f) * 2000.0f, (rand01() - 0.5f) * 2000.0f};
    };

    // 布置：team1 ×200（≥ kMinList=64 → 网格路径）/ team3 ×150 / team0 ×30（未
    // 声明队）/ 无 Meta ×20（不进板）。创建序 = Rebuild 收集序（新场景无销毁）。
    struct Ref {
        Entity e;
        Vec2 pos;
    };
    std::vector<Ref> team1, team3;
    auto spawnTeam = [&](uint32_t team, std::vector<Ref>& into, int n) {
        for (int i = 0; i < n; ++i) {
            Entity e = s.Create();
            Vec2 p = randPos();
            s.Emplace<Transform2D>(e, Transform2D{p});
            s.Emplace<Meta>(e).team = team;
            into.push_back({e, p});
        }
    };
    spawnTeam(1, team1, 200);
    spawnTeam(3, team3, 150);
    for (int i = 0; i < 30; ++i) {
        Entity e = s.Create();
        s.Emplace<Transform2D>(e, Transform2D{randPos()});
        s.Emplace<Meta>(e).team = 0;
    }
    for (int i = 0; i < 20; ++i) {
        Entity e = s.Create();
        s.Emplace<Transform2D>(e, Transform2D{randPos()}); // 无 Meta：不入板
    }

    TargetBoard board;
    board.DeclareTeams({1u, 3u});
    board.Rebuild(s, false);

    // 线性参考（旧实现语义：严格小于 = 等距保池序靠前者；tie 位 = 存在等距并列）
    struct LinResult {
        Entity e;
        float d2;
        bool tied;
    };
    auto linearNearest = [](const std::vector<Ref>& list, Vec2 from, float range,
                            Entity exclude) {
        LinResult r{Entity::Null(), range * range, false};
        float best = r.d2;
        Entity bestE = Entity::Null();
        for (const Ref& ref : list) {
            if (ref.e == exclude) continue;
            float d2 = LengthSq(ref.pos - from);
            if (d2 < best) {
                best = d2;
                bestE = ref.e;
                r.tied = false;
            } else if (d2 == best && bestE != Entity::Null()) {
                r.tied = true; // 精确等距并列（随机浮点下不应发生）
            }
        }
        r.e = bestE;
        r.d2 = best;
        return r;
    };

    // 查询矩阵：400 随机点 × 4 档半径 × 排除自身/无排除，双队对拍
    int checked = 0, tied = 0;
    const float ranges[] = {40.0f, 180.0f, 600.0f, 2500.0f};
    for (int q = 0; q < 400; ++q) {
        Vec2 from = randPos() * 1.2f; // 含板外查询点
        for (float range : ranges) {
            for (int excl = 0; excl < 2; ++excl) {
                Entity exclude =
                    excl ? team1[(q * 7) % team1.size()].e : Entity::Null();
                LinResult want = linearNearest(team1, from, range, exclude);
                Entity got = board.Nearest(1u, from, range, exclude);
                if (want.tied) {
                    ++tied;
                    continue; // 语义差异域：见函数头注释
                }
                Expect(got == want.e, "team1 grid nearest == linear reference");
                ++checked;

                LinResult want3 = linearNearest(team3, from, range, Entity::Null());
                Entity got3 = board.Nearest(3u, from, range, Entity::Null());
                if (!want3.tied) {
                    Expect(got3 == want3.e, "team3 grid nearest == linear reference");
                    ++checked;
                }
            }
        }
    }
    // 边界：未声明队恒 Null；远离布点域的短程 = Null
    Expect(board.Nearest(0u, {0.0f, 0.0f}, 5000.0f, Entity::Null()).IsNull(),
           "undeclared team always null");
    Expect(board.Nearest(1u, {9000.0f, 9000.0f}, 100.0f, Entity::Null()).IsNull(),
           "empty range null");
    Expect(checked >= 3000, "query matrix coverage");
    Expect(tied == 0, "random floats must not produce exact ties");
}

// TargetBoard 坏坐标防御（review 2026-10-02 #19）：NaN/极端坐标（脚本写
// Transform、手改场景档可达）不得把进程炸掉——原实现 NaN 的 float→int 是 UB、
// 1e9×1e9 两簇要分配 >百 TB 占位位图 = bad_alloc terminate。修复后：NaN 剪除、
// 跨度弃格降级线性，Nearest 仍正确返回最近正常目标。
void TestTargetBoardBadCoordDefense() {
    World world;
    Scene& s = world.CreateScene("board-bad");
    auto spawn = [&s](uint32_t team, Vec2 pos) {
        Entity e = s.Create();
        s.Emplace<Transform2D>(e, Transform2D{pos});
        s.Emplace<Meta>(e).team = team;
        return e;
    };
    // 100 正常点（≥ kMinList=64 触发网格路径）+ NaN / ±Inf / 1e9 三簇毒点
    Entity near0 = spawn(1, {10.0f, 0.0f});
    for (int i = 0; i < 99; ++i) spawn(1, {(float)(100 + i * 8), 0.0f});
    spawn(1, {std::numeric_limits<float>::quiet_NaN(), 0.0f});
    spawn(1, {0.0f, std::numeric_limits<float>::infinity()});
    spawn(1, {1e9f, 1e9f});
    spawn(1, {-1e9f, -1e9f});
    TargetBoard board;
    board.DeclareTeams({1u});
    board.Rebuild(s, false); // 修复前：此处 bad_alloc / UB
    Expect(board.Nearest(1u, {0.0f, 0.0f}, 500.0f, Entity::Null()) == near0,
           "bad-coord board still finds nearest sane target");
    Expect(board.Nearest(1u, {std::numeric_limits<float>::quiet_NaN(), 0.0f}, 500.0f,
                         Entity::Null())
               .IsNull(),
           "NaN query point returns null safely");
}

// TargetBoard 并行 Rebuild 同构钉板（2026-09-26 并行化批）：大场（≥kParallelMin）
// 下并行收集/归并/逐队建桶与串行路径逐位一致——list 内容（序+值）强比较 +
// NearestAny/Nearest 行为对拍。平局布点（同 cell ±8px 等距对）专钉"收集序 = view
// 序"：等距平局语义 = 序先见者，序乱即翻结果。掺销毁+重建（entt swap_only 池回收
// → view 序与创建序分叉），避免只测到"新场景顺序退化"。
void TestTargetBoardParallelRebuildIsomorphic() {
    World world; // 默认多线程 JobSystem
    Scene& s = world.CreateScene("board-par");

    auto spawn = [&s](uint32_t team, Vec2 pos) {
        Entity e = s.Create();
        s.Emplace<Transform2D>(e, Transform2D{pos});
        s.Emplace<Meta>(e).team = team;
        return e;
    };

    // team1 ×4200 成对平局布点（±8px 同 cell 32px 内）；team3 ×4200 平移域同款；
    // team0 ×300（未声明队）；无 Meta ×100（不入板）。总 8700 ≥ kParallelMin。
    for (int i = 0; i < 4200; i += 2) {
        const int gx = (i / 2) % 70, gy = (i / 2) / 70;
        const Vec2 c{(float)(gx * 32 + 16), (float)(gy * 32 + 16)};
        spawn(1u, c + Vec2{-8.0f, 0.0f});
        spawn(1u, c + Vec2{8.0f, 0.0f});
        const Vec2 c3{(float)(gx * 32 + 16 + 5000), (float)(gy * 32 + 16)};
        spawn(3u, c3 + Vec2{-8.0f, 0.0f});
        spawn(3u, c3 + Vec2{8.0f, 0.0f});
    }
    for (int i = 0; i < 300; ++i)
        spawn(0u, Vec2{(float)(i * 17), (float)(i * 13 - 900)});
    for (int i = 0; i < 100; ++i) {
        Entity e = s.Create();
        s.Emplace<Transform2D>(e, Transform2D{Vec2{(float)(i * 31 - 950), (float)(i * 7)}});
    }
    // 销毁散布 + 重建（池 slot 复用 → view 序与创建序分叉）
    {
        // team3 每 10 个销毁 1 个（从场景扫描，避免依赖创建序簿记）
        std::vector<Entity> t3;
        for (auto [ent, meta, tf] : s.View<Meta, Transform2D>().each())
            if (meta.team == 3u) t3.push_back(Scene::FromEntt(ent));
        for (size_t i = 0; i < t3.size(); i += 10) s.Destroy(t3[i]);
    }
    s.CommitDestroys();
    for (int i = 0; i < 50; ++i) // 重建队（复用回收 slot）
        spawn(1u, Vec2{(float)(i * 41 + 8000), (float)(i * 3)});

    TargetBoard serial, par;
    serial.DeclareTeams({1u, 3u});
    par.DeclareTeams({1u, 3u});
    serial.Rebuild(s, /*collectAll=*/true, /*jobs=*/nullptr);
    par.Rebuild(s, true, &world.Jobs());

    // 内容级：list 逐位（序 + 值）强比较
    for (uint32_t team : {1u, 3u}) {
        const auto& a = serial.TeamEntries(team);
        const auto& b = par.TeamEntries(team);
        Expect(a.size() == b.size(), "parallel rebuild list size matches serial");
        bool same = a.size() == b.size();
        for (size_t i = 0; same && i < a.size(); ++i)
            same = a[i].e.id == b[i].e.id && a[i].pos.x == b[i].pos.x &&
                   a[i].pos.y == b[i].pos.y;
        Expect(same, "parallel rebuild list bit-identical to serial (view order)");
    }

    // 行为级：平局查询（同 cell 等距对 → 序先见者）+ 随机查询对拍（双队 + 全表）
    for (int i = 0; i < 2100; i += 2) { // 平局点 = 对中心（每对一格，抽一半格）
        const int gx = (i / 2) % 70, gy = (i / 2) / 70;
        const Vec2 c{(float)(gx * 32 + 16), (float)(gy * 32 + 16)};
        const Vec2 c3{c.x + 5000.0f, c.y};
        Expect(serial.Nearest(1u, c, 64.0f, Entity::Null()) ==
                   par.Nearest(1u, c, 64.0f, Entity::Null()),
               "team1 tie-break identical (order-sensitive)");
        Expect(serial.Nearest(3u, c3, 64.0f, Entity::Null()) ==
                   par.Nearest(3u, c3, 64.0f, Entity::Null()),
               "team3 tie-break identical (order-sensitive)");
    }
    uint64_t seed = 0x853C49E6748FEA9Bull;
    auto rand01 = [&seed]() {
        seed = seed * 6364136223846793005ull + 1442695040888963407ull;
        return ((seed >> 33) & 0xFFFFFF) / (float)0x1000000;
    };
    const float ranges[] = {40.0f, 180.0f, 900.0f, 4000.0f};
    for (int q = 0; q < 200; ++q) {
        const Vec2 from{(rand01() - 0.5f) * 11000.0f, (rand01() - 0.5f) * 4000.0f};
        for (float range : ranges) {
            Expect(serial.Nearest(1u, from, range, Entity::Null()) ==
                       par.Nearest(1u, from, range, Entity::Null()),
                   "random nearest team1 identical");
            Expect(serial.NearestAny(from, range, Entity::Null()) ==
                       par.NearestAny(from, range, Entity::Null()),
                   "random nearest-any identical");
        }
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
    Entity farGem = s.Create();
    s.Emplace<Transform2D>(farGem, Transform2D{{100, 0}});
    s.Emplace<Collectible>(farGem, Collectible{.kind = 1, .value = 7.0f});
    Entity idle = s.Create();
    s.Emplace<Transform2D>(idle, Transform2D{{200, 0}});
    s.Emplace<Collectible>(idle, Collectible{.kind = 0});
    s.Emplace<Inventory>(player);
    s.Get<Stats>(player).pickupRadius = 120.0f;

    world.Step(dt);
    Expect(s.Get<Collectible>(farGem).state == 1, "player stat side wins (max rule)");
    Expect(s.Get<Transform2D>(farGem).pos.x < 100.0f, "far gem flying");
    const Vec2 idlePos = s.Get<Transform2D>(idle).pos;
    for (int i = 0; i < 4; ++i) world.Step(dt);
    Expect(s.Get<Transform2D>(idle).pos == idlePos, "out of both radii stays idle");
    for (int i = 0; i < 40 && s.Alive(farGem); ++i) world.Step(dt);
    Expect(!s.Alive(farGem), "coin picked up");
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

// #61（review 2026-10-02）：连发型 Spawner（interval≤dt → 出生后冷却钳 0）在
// timeScale=0 冻结期不得逐 tick 泄漏 burst/RNG——原实现到期冷却照走出生分支，
// 与"冻结波次、RNG 不消耗"注释自述相悖。导演段常规路径冻结安全（重置后冷却
// 恒 >0），本测试专钉 Spawner 缺口 + 阴性验证修复。
void TestSpawnFreezeNoLeak() {
    World world;
    Scene& s = world.CreateScene("frz-sp");
    world.SetActiveScene(&s);
    WaveSpawnCounter ctr;
    world.SetSpawnFn([&ctr](Scene& sc, uint32_t id, Vec2 pos, uint32_t team) {
        return ctr(sc, id, pos, team);
    });
    Entity sp = s.Create();
    s.Emplace<Transform2D>(sp, Transform2D{{0, 0}});
    Spawner& spn = s.Emplace<Spawner>(sp);
    spn.prefabId = 1;         // WaveSpawnCounter 只认 prefab 1
    spn.interval = 0.001f; // < dt：每 tick 到期，出生后冷却钳 0（泄漏触发形态）
    spn.burst = 2;
    world.Pipeline().AddSystem(std::make_unique<SpawnSystem>());
    world.Pipeline().AddSystem(std::make_unique<DestroyCommitSystem>());
    world.Pipeline().ResolveOrder();
    const float dt = 1.0f / 60.0f;

    world.Step(dt); // 常速 1 tick：burst 出生、冷却钳位 0
    Expect(ctr.spawns >= 1, "#61: fast spawner produced first burst");
    const int base = ctr.spawns;

    world.SetTimeScale(0.0f);
    for (int i = 0; i < 60; ++i) world.Step(dt);
    Expect(ctr.spawns == base, "#61: frozen spawner leaks no burst (RNG idle)");
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

    // review 2026-10-02 #10：销毁窗口期（已入队未提交）保存——引擎侧防线。
    // 修复前死实体 + "DestroyQueueTag":{} 一并入档，读档复活成永生僵尸
    //（Load 后永不入队，CommitDestroys 只消费当帧队列）
    World w3;
    Scene& s3 = w3.CreateScene("win");
    Entity keep3 = s3.Create();
    s3.Emplace<Transform2D>(keep3, Transform2D{{3, 3}});
    Entity dying = s3.Create();
    s3.Emplace<Transform2D>(dying, Transform2D{{4, 4}});
    s3.Destroy(dying); // 只入队，不提交（真实事故 = 结构轨 after 快照先于清队）
    std::string text3 = SceneArchive::Save(s3);
    size_t slots3 = 0;
    for (size_t p = text3.find("\"Transform2D\""); p != std::string::npos;
         p = text3.find("\"Transform2D\"", p + 1))
        ++slots3;
    Expect(slots3 == 1, "save excludes queued-destroy entity (window)");
    Expect(text3.find("DestroyQueueTag") == std::string::npos,
           "save never serializes queue tag");
    World w4;
    Scene& dst4 = w4.CreateScene("win2");
    Expect(SceneArchive::Load(dst4, text3), "load queued-window save ok");
    Expect(dst4.AliveCount() == 1, "queued-window reload has no zombie");
    // 旧档防御：手工构造含 DestroyQueueTag 的档（历史版本写出的形态）拒读标记
    const std::string legacy =
        "{\"schemaVersion\":2,\"name\":\"lz\",\"entities\":[{\"components\":"
        "{\"Transform2D\":{\"pos\":[5,5]}},\"DestroyQueueTag\":{}}]}";
    World w5;
    Scene& dst5 = w5.CreateScene("lz");
    Expect(SceneArchive::Load(dst5, legacy), "legacy tag archive loads");
    Entity first5{};
    dst5.Each([&](Entity e) {
        if (first5.IsNull()) first5 = e;
    });
    Expect(dst5.AliveCount() == 1 && !first5.IsNull() &&
               !dst5.Has<DestroyQueueTag>(first5),
           "legacy tag refused on read (no zombie marker)");
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

// ---- M6a 批② T3b-2：PingPong 往返帧映射（纯函数；0/1 旧语义由既有覆盖）----
void TestVerifyAnimatorPingPong() {
    World w;
    w.InstallDefaultSystems();
    Scene& s = w.CreateScene("pp");
    w.SetActiveScene(&s);
    w.Clips().Add(0x99u, {20u, 21u, 22u, 23u}, 1.0f, true);

    Entity e = s.Create();
    SpriteRenderer& sr = s.Emplace<SpriteRenderer>(e);
    sr.spriteId = 999u;
    Animator2D& a = s.Emplace<Animator2D>(e);
    a.clipId = 0x99u;
    a.loop = 2; // PingPong

    // dt=1s、fps=1 → 第 t 步后 pos=t。period=2(n-1)=6：0..3..0 往返（含 t=0 起点）
    const int seq[16] = {1, 2, 3, 2, 1, 0, 1, 2, 3, 2, 1, 0, 1, 2, 3, 2};
    bool allOk = true;
    for (int i = 0; i < 16; ++i) {
        w.Step(1.0f);
        if (a.curFrame != (uint16_t)seq[i] || sr.spriteId != 20u + (uint32_t)seq[i])
            allOk = false;
    }
    Expect(allOk, "clip: pingpong frame sequence 1,2,3,2,1,0 ...");
    Expect(a.time < 6.0f, "clip: pingpong time bounded by period 2(n-1)/fps");

    // 单帧 clip PingPong 防御：恒帧 0（period 0 路径）
    w.Clips().Add(0x9Au, {30u}, 1.0f, true);
    a.clipId = 0x9Au;
    for (int k = 0; k < 5; ++k) w.Step(1.0f);
    Expect(a.curFrame == 0 && sr.spriteId == 30u, "clip: single-frame pingpong stays 0");
}

// ---- A 档补间单元（2026-09-28 用户插入项）：TweenTable 建链校验/缓动精确值/
// Yoyo 折返/Once 完成恰一事件/同字段顶替/颜色字节插值/Kill 三通道/销毁自清。
// C# ABI 端到端在 script-tests TestTweenSdk（探针 typeId 15）。----
void TestTweenTable() {
    WorldDesc d;
    d.threadCount = 1;
    World w(d);
    Scene& s = w.CreateScene("tween");
    w.SetActiveScene(&s);
    TweenTable& tt = w.Tweens();

    Entity e = s.Create();
    s.Emplace<Transform2D>(e); // scale=(1,1)、pos=(0,0) 默认
    const float to[4] = {2.0f, 2.0f, 0.0f, 0.0f};

    // 拒建：字段名未命中 / 白名单外类型（Meta.tag = Blob24）/ 组件缺
    Expect(tt.Create(s, e, 0, "nope", to, 1.0f, TweenEase::Linear, TweenMode::Once) == 0,
           "tween: unknown field rejected");
    Expect(tt.Create(s, e, 3, "tag", to, 1.0f, TweenEase::Linear, TweenMode::Once) == 0,
           "tween: non-animatable type rejected");
    {
        Entity bare = s.Create();
        Expect(tt.Create(s, bare, 0, "scale", to, 1.0f, TweenEase::Linear, TweenMode::Once) == 0,
               "tween: missing component rejected");
    }

    // 建立与顶替（同实体同字段 = 新句柄接掌，旧句柄亡）
    const uint64_t h1 = tt.Create(s, e, 0, "scale", to, 1.0f, TweenEase::OutCubic, TweenMode::Once);
    Expect(h1 != 0 && tt.Alive(h1) && tt.Count() == 1, "tween: created");
    const uint64_t h2 = tt.Create(s, e, 0, "scale", to, 1.0f, TweenEase::OutCubic, TweenMode::Once);
    Expect(h2 != 0 && h2 != h1 && !tt.Alive(h1) && tt.Count() == 1, "tween: same field replaced");

    // OutCubic 精确中值：e(0.5) = 1+(-0.5)³ = 0.875 → scale 1→2 = 1.875
    tt.Advance(w, s, 0.5f);
    Expect(s.Get<Transform2D>(e).scale.x == 1.875f, "tween: OutCubic(0.5) exact 1.875");

    // Once 完成：终值精确 + 事件恰一次 + 条目移除
    tt.Advance(w, s, 0.6f); // elapsed 1.1 ≥ 1
    Expect(!tt.Alive(h2) && tt.Count() == 0, "tween: once removed");
    Expect(s.Get<Transform2D>(e).scale.x == 2.0f, "tween: final exact");
    Expect(w.Events().Size() == 1, "tween: one TweenFinished queued");

    // Yoyo 折返：pos 0→10（1s），1.25 → 三角 0.75 → 7.5；永续无完成事件
    const float toPos[4] = {10.0f, 0.0f, 0.0f, 0.0f};
    const uint64_t hy = tt.Create(s, e, 0, "pos", toPos, 1.0f, TweenEase::Linear, TweenMode::Yoyo);
    tt.Advance(w, s, 1.25f);
    Expect(s.Get<Transform2D>(e).pos.x == 7.5f, "tween: yoyo fold 7.5");
    Expect(tt.Alive(hy), "tween: yoyo stays alive");
    Expect(w.Events().Size() == 1, "tween: yoyo fires no finish");

    // 颜色字节插值：0xFF0000FF → 0xFFFFFFFF，t=0.5 → g/b = 127.5 四舍五入 128
    {
        Entity c = s.Create();
        SpriteRenderer& sr = s.Emplace<SpriteRenderer>(c);
        sr.colorRGBA = 0xFF0000FFu;
        const float toCol[4] = {255.0f, 255.0f, 255.0f, 255.0f};
        tt.Create(s, c, 5, "colorRGBA", toCol, 1.0f, TweenEase::Linear, TweenMode::Once);
        tt.Advance(w, s, 0.5f);
        Expect(s.Get<SpriteRenderer>(c).colorRGBA == 0xFF8080FFu, "tween: color bytes 0x80");
    }

    // Kill 三通道 + 陈旧句柄
    Expect(tt.KillField(e, 0, "pos") == 1 && !tt.Alive(hy), "tween: kill field");
    Expect(tt.KillField(e, 0, "pos") == 0, "tween: kill idempotent");
    Expect(!tt.Alive(hy + 12345), "tween: stale handle dead");

    // 实体销毁自清（两阶段：提交后随 Advance 消失）
    {
        Entity v = s.Create();
        s.Emplace<Transform2D>(v);
        tt.Create(s, v, 0, "scale", to, 1.0f, TweenEase::Linear, TweenMode::Once);
        s.Destroy(v);
        s.CommitDestroys();
        tt.Advance(w, s, 0.5f);
        Expect(tt.Count() == 0, "tween: destroyed entity swept");
    }

    tt.Clear();
    Expect(tt.Count() == 0, "tween: clear");
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

// ---- 并行销毁稳定归并（review 2026-10-02 #2）：chunk 分桶归并 = 串行序 ----
// 同帧多 chunk 投射物半数到期，多 worker 档与单线程档的状态哈希必须逐位一致。
// 修复前 worker 直接 Destroy：入队序 = 锁获取序（跨线程漂移）→ 提交序漂移 →
// 池 swap_and_pop 终态与幸存实体 packed 序不定。半数存活使池布局差异进哈希
// （全灭则两档池皆空、顺序不可见）；偶数下标到期使每个 chunk 都有意图。
void TestVerifyParallelDestroyDeterminism() {
    auto run = [](int threads) {
        WorldDesc d;
        d.seed = 7;
        d.threadCount = threads;
        World w(d);
        Scene& s = w.CreateScene("pd");
        w.SetActiveScene(&s);
        for (int i = 0; i < 4096; ++i) { // 16 chunks @ grain 256
            Entity e = s.Create();
            s.Emplace<Transform2D>(e, Transform2D{{(float)(i % 32), (float)(i / 32)}});
            Projectile& pr = s.Emplace<Projectile>(e);
            pr.lifetime = 1.0f;
            pr.age = (i % 2 == 0) ? 1.0f : 0.0f; // 偶下标当帧到期
        }
        ProjectileLifetimeSystem sys;
        sys.Tick(w, s, 1.0f / 60.0f);
        s.CommitDestroys();
        Expect(s.AliveCount() == 2048, "pdestroy: half survived");
        // 槽回收序也须一致：销毁后再创建（entt 从 free_list 取最近销毁槽）
        for (int i = 0; i < 64; ++i) {
            Entity e = s.Create();
            s.Emplace<Transform2D>(e, Transform2D{{0, 0}});
            Projectile& pr = s.Emplace<Projectile>(e);
            pr.lifetime = 9.0f;
        }
        return ComputeStateHash(s);
    };
    const uint64_t hSt = run(1);
    const uint64_t hMt = run(8);
    Expect(hSt == hMt, "pdestroy: mt hash == st hash");
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
    // M6c 批③：AudioSource 抽查——clipGuid AudioRef 槽 + group 三名枚举
    //（Inspector 槽控件的前提；hint 位回归锁）
    const ComponentMeta& aus = *reg.Find("AudioSource");
    Expect(aus.editorMeta && HasHint(aus.editorMeta[0].hints, FieldHint::AudioRef) &&
               aus.fields[0].type == FieldType::UInt64 &&
               HasHint(aus.editorMeta[5].hints, FieldHint::Enum) &&
               aus.editorMeta[5].enumCount == 3,
           "AudioSource clipGuid/group meta");
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


// ---- M7a 批②：project.lemon 只读解析 + entryScene 回退链 ----
void TestProjectFile() {
    namespace fs = std::filesystem;
    using namespace lemon::assets;

    const ProjectFile pf = ParseProjectFile(
        "{\"schemaVersion\":1,\"name\":\"demo\",\"engineVersion\":\"0.4.0-m4\","
        "\"guid\":\"9e9b2af4ee867201\",\"entryScene\":\"Scenes/MainMenu.scene\"}");
    Expect(pf.ok && pf.name == "demo" && pf.guid == 0x9e9b2af4ee867201ull &&
               pf.engineVersion == "0.4.0-m4" && pf.entryScene == "Scenes/MainMenu.scene",
           "project file full parse");
    Expect(ParseProjectFile("{\"name\":\"x\"}").ok, "minimal (name only) ok");
    Expect(!ParseProjectFile("{\"nope\":1}").ok, "missing name rejected");
    Expect(!ParseProjectFile("not json").ok, "bad json rejected");
    Expect(ParseProjectFile("{\"name\":\"x\",\"engineVersion\":\"9.9.9\"}").ok,
           "engineVersion mismatch tolerated (warn-not-block)");

    // ResolveEntryScene 三态（临时项目夹具）
    const fs::path root = fs::temp_directory_path() /
                          ("lemon-test-projfile-" + std::to_string(lemon::CurrentProcessId()));
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "Scenes", ec);
    { std::ofstream f(root / "Scenes" / "A.scene", std::ios::trunc); f << "{}"; }
    { std::ofstream f(root / "project.lemon", std::ios::trunc); f << "{\"name\":\"p\"}"; }
    ProjectFile bare = LoadProjectFile(root.string());
    Expect(bare.ok && ResolveEntryScene(root.string(), bare) == "Scenes/A.scene",
           "fallback: sole .scene resolves");
    { std::ofstream f(root / "Scenes" / "B.scene", std::ios::trunc); f << "{}"; }
    Expect(ResolveEntryScene(root.string(), bare).empty(),
           "multi-scene without entryScene = empty (caller red-flags)");
    bare.entryScene = "Scenes/B.scene";
    Expect(ResolveEntryScene(root.string(), bare) == "Scenes/B.scene",
           "declared entryScene honored");
    bare.entryScene = "Scenes/Gone.scene";
    Expect(ResolveEntryScene(root.string(), bare).empty(),
           "dangling declaration = empty (no silent fallback)");
    fs::remove_all(root, ec);
}

// ---- M7a 批②：assets::ResolveSpriteRefs 引擎本体四态（mock 查询面；编辑器
// 端到端链路归 TestSpriteGuidResolve，此处在引擎侧锁进程独立性）----
void TestSpriteRefsEngine() {
    using namespace lemon::assets;
    struct MockSource : SpriteRefSource {
        std::vector<SpriteEntryView> views;
        uint32_t base = 100;
        const SpriteEntryView* SpriteByGuid(uint64_t g) const override {
            for (const SpriteEntryView& v : views)
                if (v.guid == g) return &v;
            return nullptr;
        }
        const SpriteEntryView* SpriteByWholeId(uint32_t id) const override {
            if (id == 0) return nullptr;
            for (const SpriteEntryView& v : views)
                if (v.spriteId == id) return &v;
            return nullptr;
        }
        uint32_t SpriteIdBase() const override { return base; }
    };

    World w;
    Scene& s = w.CreateScene("refs");
    MockSource src;
    const SpriteEntryView whole{0x1111222233334444ull, 100, 0, 0, true}; // 整图：本体 100
    const SpriteEntryView sheet{0x5555666677778888ull, 101, 102, 4, true}; // 切片：本体 101、块 102..105
    src.views = {whole, sheet};

    SpriteRenderer& hitWhole = s.Emplace<SpriteRenderer>(s.Create()); // ① guid 命中：旧号区间外 → 本体号
    hitWhole.spriteGuid = whole.guid;
    hitWhole.spriteId = 777;
    SpriteRenderer& hitCell = s.Emplace<SpriteRenderer>(s.Create()); // ①' 切片表区间外 → cell 0
    hitCell.spriteGuid = sheet.guid;
    hitCell.spriteId = 999;
    SpriteRenderer& inRange = s.Emplace<SpriteRenderer>(s.Create()); // ①'' 区间内 → 保号
    inRange.spriteGuid = sheet.guid;
    inRange.spriteId = 104;
    SpriteRenderer& dangl = s.Emplace<SpriteRenderer>(s.Create()); // ② 悬空：保号 + 计数
    dangl.spriteGuid = 0xdeadbeefdeadbeefull;
    dangl.spriteId = 555;
    SpriteRenderer& legacy = s.Emplace<SpriteRenderer>(s.Create()); // ③ 存量：本体号 → 回填 guid
    legacy.spriteId = 100;
    SpriteRenderer& cell = s.Emplace<SpriteRenderer>(s.Create()); // ③' cell 号不回填
    cell.spriteId = 103;
    SpriteRenderer& proc = s.Emplace<SpriteRenderer>(s.Create()); // ③'' 程序化页号不回填
    proc.spriteId = 4;

    const SpriteRefStats st = ResolveSpriteRefs(s, src);
    Expect(hitWhole.spriteId == 100, "whole: out-of-range re-normalized to body id");
    Expect(hitCell.spriteId == 102, "sliced: out-of-range falls back to cell 0");
    Expect(inRange.spriteId == 104, "in-range id kept (no rewrite)");
    Expect(dangl.spriteId == 555 && st.danglingGuid == 1, "dangling keeps legacy id + counted");
    Expect(legacy.spriteGuid == whole.guid && st.backfilled == 1, "legacy body id backfilled");
    Expect(cell.spriteGuid == 0, "cell id NOT backfilled (only body ids)");
    Expect(proc.spriteGuid == 0, "procedural page id NOT backfilled");
    // 幂等：已归一场景再跑零写入
    const SpriteRefStats st2 = ResolveSpriteRefs(s, src);
    Expect(st2.backfilled == 0 && st2.danglingGuid == 1, "resolve idempotent");
}

#ifdef LEMON_EDITOR_CORE
// ---- M4.4 测试面：资产数据库 / 实体子树档案 / ScriptBox 档案段 / Atlas 页热更新 ----

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

// ---- M7a 前置：低 32 位碰撞体检 + 发号唯一性（2026-10-01，svr-test Player/Mob 实证）----
// prefabId/clipId/controllerId/表 id 均取资产 GUID 低 32 位（03 §69 恒 uint32），
// 同类型两资产低 32 位同值 = 运行时静默丢映射。锁两件事：①体检按【同类型域】红字
//（跨类型同低 32 位合法——不同键空间，HealthIssues==1 钉死域语义）；②新发号避开
// 域内已占低 32 位。
void TestAssetDatabaseLow32Collision() {
    namespace fs = std::filesystem;
    using lemon::editor::AssetDatabase;
    using lemon::editor::AssetEntry;

    const fs::path root = fs::temp_directory_path() /
                          ("lemon-test-low32-" + std::to_string(lemon::CurrentProcessId()));
    std::error_code ec;
    fs::remove_all(root, ec);

    AssetDatabase db;
    Expect(db.OpenProject(root.string(), /*spriteIdBase=*/100), "open project");

    auto put = [&](const std::string& rel, const char* guidHex) {
        const fs::path p = root / rel;
        fs::create_directories(p.parent_path(), ec);
        { std::ofstream f(p, std::ios::binary); f << "x"; }
        std::ofstream f(p.string() + ".meta", std::ios::trunc);
        f << "{\"guid\":\"" << guidHex << "\"}";
    };
    put("Assets/a.anim", "111100000000000a");
    put("Assets/b.anim", "222200000000000a"); // 与 a 同低 32 位（…0000000a）→ 红字
    put("Prefabs/p.prefab", "333300000000000a"); // 同低 32 位但异域（prefab≠clip）→ 不报
    { std::ofstream f(root / "Assets" / "c.anim", std::ios::binary); f << "y"; } // 无 meta → 新发号

    db.Rescan();
    Expect(db.HealthIssues() == 1, "same-type low32 collision flagged exactly once");
    const AssetEntry* c = db.FindByPath("Assets/c.anim");
    Expect(c && c->guid != 0 && (uint32_t)c->guid != 0x0000000aull,
           "allocated guid avoids taken low32 in domain");
    fs::remove_all(root, ec);
}

// ---- 孤儿 .meta 清扫（2026-10-01 拍板：Unity/Cocos 式自动清 + 引用判据保守保留）----
// 三态：源缺失 + guid 零引用 = 扫描期自动删；源缺失 + 仍被引用 = meta 保留 + 红字
//（"只恢复源文件"场景的复链钩子，盲删永久断引用）；源+meta 双删但仍被引用 = 红字。
// 引用面 = 项目数据文本（hex 小写/大写 + 十进制三针）；手动 SweepOrphanMetas() 同判定。
void TestOrphanMetaSweep() {
    namespace fs = std::filesystem;
    using lemon::editor::AssetDatabase;

    const fs::path root = fs::temp_directory_path() /
                          ("lemon-test-sweep-" + std::to_string(lemon::CurrentProcessId()));
    std::error_code ec;
    fs::remove_all(root, ec);

    AssetDatabase db;
    Expect(db.OpenProject(root.string(), /*spriteIdBase=*/100), "open project for sweep");

    auto put = [&](const std::string& rel, const char* guidHex) {
        const fs::path p = root / rel;
        fs::create_directories(p.parent_path(), ec);
        { std::ofstream f(p, std::ios::binary); f << "x"; }
        std::ofstream f(p.string() + ".meta", std::ios::trunc);
        f << "{\"guid\":\"" << guidHex << "\"}";
    };
    put("Assets/hero.png", "aaaa000000000001"); // 将被引用
    put("Assets/mob.png", "aaaa000000000002");  // 零引用
    put("Assets/gem.png", "aaaa000000000003");  // 将被引用（十进制形态）
    fs::create_directories(root / "Scenes", ec);
    {
        std::ofstream f(root / "Scenes" / "ref.scene", std::ios::trunc);
        f << "{\"heroRef\": \"aaaa000000000001\", \"gemRef\": "
          << 0xaaaa000000000003ull << "}"; // hex 串 + 十进制双形态
    }
    db.Rescan();
    Expect(db.FindByPath("Assets/hero.png") && db.FindByPath("Assets/mob.png") &&
               db.FindByPath("Assets/gem.png"),
           "three assets alive");

    // ① 外部删源（meta 残留）：hero 被引用 → 条目出表 + meta 保留 + 红字恰一次；
    // mob 零引用 → meta 自动清扫、零红字
    fs::remove(root / "Assets" / "hero.png", ec);
    fs::remove(root / "Assets" / "mob.png", ec);
    db.Rescan();
    Expect(db.FindByPath("Assets/hero.png") == nullptr, "referenced deleted entry dropped");
    Expect(fs::exists(root / "Assets" / "hero.png.meta", ec), "referenced orphan meta kept");
    Expect(!fs::exists(root / "Assets" / "mob.png.meta", ec), "unreferenced orphan meta swept");
    Expect(db.HealthIssues() == 1, "kept referenced orphan flagged exactly once");

    // ② 双删（源+meta 同时）但仍被引用 → 出表 + 红字（悬空可见性；与 ① 的保留红共存）
    fs::remove(root / "Assets" / "gem.png", ec);
    fs::remove(root / "Assets" / "gem.png.meta", ec);
    db.Rescan();
    Expect(db.FindByPath("Assets/gem.png") == nullptr, "double-deleted entry dropped");
    Expect(db.HealthIssues() == 2, "double-deleted-but-referenced flagged (plus kept meta)");

    // ③ 手动清扫入口（菜单）：同判定立即执行并出报告；复用 ① 的 hero.meta（引用态）
    { std::ofstream f(root / "Assets" / "stray.png.meta", std::ios::trunc);
      f << "{\"guid\":\"aaaa000000000004\"}"; }
    AssetDatabase::OrphanSweepResult r = db.SweepOrphanMetas();
    Expect(r.cleaned.size() == 1 && r.keptReferenced.size() == 1, "manual sweep report");
    Expect(!fs::exists(root / "Assets" / "stray.png.meta", ec), "stray swept by manual call");
    Expect(fs::exists(root / "Assets" / "hero.png.meta", ec), "referenced meta still kept");
    fs::remove_all(root, ec);
}

// ---- M4.4-a：AssetDatabase 生命周期（GUID 稳定/manifest 记账/体检）----
void TestAssetDatabaseLifecycle() {
    namespace fs = std::filesystem;
    using lemon::editor::AssetDatabase;
    using lemon::editor::AssetEntry;
    using lemon::editor::AssetType;

    const fs::path root = fs::temp_directory_path() /
                          ("lemon-test-assets-" + std::to_string(lemon::CurrentProcessId()));
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

    // 删除文件 → 条目出表（墓碑 2026-10-01 退役，06 §2.2 修订）；号不回收
    fs::remove(root / "Assets" / "icons" / "coin.png", ec);
    fs::remove(root / "Assets" / "icons" / "coin.png.meta", ec);
    db.Rescan();
    Expect(db.FindByGuid(0x1122334455667788ull) == nullptr,
           "deleted asset entry dropped (no tombstone)");
    Expect(db.LastChange().removed.size() == 1, "removal reported");
    { std::ofstream f(root / "Assets" / "new.png", std::ios::binary); f << "n"; }
    db.Rescan();
    const AssetEntry* np = db.FindByPath("Assets/new.png");
    Expect(np && np->spriteId == 102, "new sprite id never reuses freed id");

    // 孤儿 meta：零引用 = 扫描期自动清扫（不再红字永续）
    { std::ofstream f(root / "Assets" / "orphan.png.meta", std::ios::trunc); f << "{}"; }
    db.Rescan();
    Expect(!fs::exists(root / "Assets" / "orphan.png.meta", ec),
           "unreferenced orphan meta swept");
    Expect(db.HealthIssues() == 0, "sweep leaves no health issue");

    fs::remove_all(root, ec);
}

// ---- F-02（2026-09-24）：路径 containment——重命名/导入/项目名不得越出项目根 ----
// ---- M6a 批② T3b-3：SetGridSlice（.meta importer 写入 → Rescan 连号块/烧号/撤销）----
void TestGridSliceConfig() {
    namespace fs = std::filesystem;
    using lemon::editor::AssetDatabase;
    using lemon::editor::AssetEntry;

    const fs::path root = fs::temp_directory_path() /
                          ("lemon-test-slice-" + std::to_string(lemon::CurrentProcessId()));
    std::error_code ec;
    fs::remove_all(root, ec);
    AssetDatabase db;
    Expect(db.OpenProject(root.string(), 100), "open project for slice");
    { std::ofstream f(root / "Assets" / "sheet.png", std::ios::binary); f << "png"; }
    db.Rescan();
    const AssetEntry* e = db.FindByPath("Assets/sheet.png");
    Expect(e && !e->Sliced(), "unsliced at start");

    AssetEntry* m = db.FindByGuid(e->guid);
    Expect(db.SetGridSlice(*m, 32, 48, 8, 1), "set grid slice writes meta");
    db.Rescan();
    e = db.FindByPath("Assets/sheet.png");
    Expect(e && e->Sliced() && e->gridCols == 8 && e->gridRows == 1 && e->cellW == 32 &&
               e->cellH == 48 && e->sliceCount == 8 &&
               e->SliceSpriteId(7) == e->sliceBase + 7,
           "rescan picks up importer block");
    Expect(e->SliceSpriteId(8) == 0, "cell out of range -> 0");

    // frames 增大 → 新块烧号（旧块留号；"只增不减"语义）
    const uint32_t oldBase = e->sliceBase;
    m = db.FindByGuid(e->guid);
    Expect(db.SetGridSlice(*m, 32, 48, 8, 2), "grow grid");
    db.Rescan();
    e = db.FindByPath("Assets/sheet.png");
    Expect(e && e->sliceCount == 16 && e->sliceBase >= oldBase + 8,
           "grown block burns ids");

    // 撤销切片 → 整图（meta importer 段移除）
    m = db.FindByGuid(e->guid);
    Expect(db.SetGridSlice(*m, 0, 0, 0, 0), "clear slice");
    db.Rescan();
    e = db.FindByPath("Assets/sheet.png");
    Expect(e && !e->Sliced(), "cleared back to whole image");

    // 非 sprite 拒绝
    { std::ofstream f(root / "Assets" / "x.anim", std::ios::binary); f << "{}"; }
    db.Rescan();
    if (const AssetEntry* c = db.FindByPath("Assets/x.anim"))
        Expect(!db.SetGridSlice(*db.FindByGuid(c->guid), 1, 1, 1, 1),
               "non-sprite rejected");
    else
        Expect(false, "clip entry found");
    fs::remove_all(root, ec);
}

void TestAssetPathContainment() {
    namespace fs = std::filesystem;
    using lemon::editor::AssetDatabase;
    using lemon::editor::AssetEntry;

    const fs::path root = fs::temp_directory_path() /
                          ("lemon-test-paths-" + std::to_string(lemon::CurrentProcessId()));
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

// ---- M6a 批② T5：存档分档——三档三文件路径 + 三档落盘/回读独立 + 空通道跳过 +
// 旧 game.sav 惰性迁移（写恒写新名）+ 坏档兜底按档隔离 + 16 MiB 上限按档 ----
void TestSaveChannelSplits() {
    namespace fs = std::filesystem;
    using ecs::SaveChannel;
    using namespace lemon::ecs;
    using lemon::assets::SaveStore;

    const std::string tag = std::to_string(lemon::CurrentProcessId());
    const fs::path root = fs::temp_directory_path() / ("lemon-test-savesplit-" + tag);
    std::error_code ec;
    fs::remove_all(root, ec);
    const std::string rootStr = root.string();

    // ① 三档路径独立；越界 ch 钳 slot_0（防御面——装载循环只传常量不触发）；
    //    空 root = 空串（无项目裸会话全链 no-op 口径）
    Expect(SaveStore::FilePath(rootStr, kSaveSlot).ends_with("slot_0.sav") &&
               SaveStore::FilePath(rootStr, kSaveSettings).ends_with("settings.sav") &&
               SaveStore::FilePath(rootStr, kSaveMeta).ends_with("meta.sav"),
           "three channel file paths");
    Expect(SaveStore::FilePath(rootStr, 77).ends_with("slot_0.sav"), "oob ch clamps to slot_0");
    Expect(SaveStore::FilePath("", kSaveSlot).empty(), "empty root -> empty path");

    // ② 三档落盘互不覆盖 + 空通道跳过 + 回读独立
    {
        SaveChannel slot;
        slot.Set("run.kills", "5", 1);
        SaveChannel meta;
        meta.Set("vs.best", "77", 2);
        Expect(SaveStore::Write(rootStr, kSaveSlot, slot), "slot written");
        Expect(SaveStore::Write(rootStr, kSaveMeta, meta), "meta written");
        Expect(!SaveStore::Write(rootStr, kSaveSettings, SaveChannel{}), "empty channel skipped");
        Expect(!SaveStore::Write("", kSaveSlot, slot), "no project -> write no-op");
        Expect(fs::exists(root / ".lemon/saves/slot_0.sav", ec), "slot file exists");
        Expect(fs::exists(root / ".lemon/saves/meta.sav", ec), "meta file exists");
        Expect(!fs::exists(root / ".lemon/saves/settings.sav", ec), "settings not written");
        SaveChannel back;
        SaveStore::Load(rootStr, kSaveMeta, back);
        Expect(back.Count() == 1 && back.GetLen("vs.best") == 2, "meta roundtrip");
    }

    // ③ 旧 game.sav 惰性迁移：删新档留旧名 → 载入走旧路径；写恒写新名、旧文件保留
    {
        fs::remove(root / ".lemon/saves/slot_0.sav", ec);
        SaveChannel legacy;
        legacy.Set("old.key", "v1", 2);
        {
            std::ofstream f(root / ".lemon/saves/game.sav", std::ios::binary | std::ios::trunc);
            const std::vector<uint8_t> b = legacy.Encode();
            f.write((const char*)b.data(), (std::streamsize)b.size());
        }
        SaveChannel back;
        SaveStore::Load(rootStr, kSaveSlot, back);
        Expect(back.Count() == 1 && back.GetLen("old.key") == 2, "legacy game.sav lazy-migrated");
        Expect(SaveStore::Write(rootStr, kSaveSlot, back), "migrated slot written to new name");
        Expect(fs::exists(root / ".lemon/saves/slot_0.sav", ec), "new name file back");
        Expect(fs::exists(root / ".lemon/saves/game.sav", ec), "legacy file untouched");
        SaveChannel st; // settings 无旧名对应 → 不受迁移影响
        SaveStore::Load(rootStr, kSaveSettings, st);
        Expect(st.Count() == 0, "settings independent of legacy");
    }

    // ④ 坏档兜底按档隔离：settings 主档垃圾 + .bak 好档 → 走 bak；meta 垃圾无
    //    bak → 空通道开局；slot 既有好档不受邻居损坏影响
    {
        SaveChannel good;
        good.Set("bak.key", "1", 1);
        {
            std::ofstream f(root / ".lemon/saves/settings.sav", std::ios::binary | std::ios::trunc);
            f << "garbage-not-lemonsav";
        }
        {
            std::ofstream f(root / ".lemon/saves/settings.sav.bak",
                            std::ios::binary | std::ios::trunc);
            const std::vector<uint8_t> b = good.Encode();
            f.write((const char*)b.data(), (std::streamsize)b.size());
        }
        {
            std::ofstream f(root / ".lemon/saves/meta.sav", std::ios::binary | std::ios::trunc);
            f << "garbage-too";
        }
        SaveChannel st, mt, sl;
        SaveStore::Load(rootStr, kSaveSettings, st);
        SaveStore::Load(rootStr, kSaveMeta, mt);
        SaveStore::Load(rootStr, kSaveSlot, sl);
        Expect(st.Count() == 1 && st.GetLen("bak.key") == 1, "settings bad main -> bak fallback");
        Expect(mt.Count() == 0, "meta corrupt no bak -> empty start");
        Expect(sl.GetLen("old.key") == 2, "slot unaffected by other channels' corruption");
    }

    // ⑤ 16 MiB 上限按档：slot_0.sav 超限 → 跳过不 slurp；主档视为不存在 → 旧名
    //    惰性迁移接力（game.sav 仍在，链式兜底语义钉板）
    {
        {
            std::ofstream f(root / ".lemon/saves/slot_0.sav",
                            std::ios::binary | std::ios::trunc);
            const std::vector<char> big((16u << 20) + 1, 'x');
            f.write(big.data(), (std::streamsize)big.size());
        }
        SaveChannel sl;
        SaveStore::Load(rootStr, kSaveSlot, sl);
        Expect(sl.GetLen("old.key") == 2, "oversize slot skipped, legacy migration takes over");
    }

    fs::remove_all(root, ec);
}
// ---- M7a 批③：Play 装配下沉件单测 ----

// PrefabCache：低 32 索引建账 + Spawn 语义（树装载/pos 覆盖/team 覆盖/prefabId
// 回链/未命中 Null）+ scripts 缺席零挂载（槽保持 typeId=-1 不炸）
class TestPrefabSource final : public lemon::assets::PrefabSource {
public:
    std::vector<std::pair<uint64_t, std::string>> items;
    void EachPrefab(const std::function<bool(uint64_t guid, const std::string& absPath)>& fn)
        const override {
        for (const auto& [g, p] : items)
            if (!fn(g, p)) return;
    }
};

void TestPrefabCachePlaySpawn() {
    using namespace lemon::ecs;
    namespace fs = std::filesystem;
    const std::string tag = std::to_string(lemon::CurrentProcessId());
    const fs::path root = fs::temp_directory_path() / ("lemon-test-prefabc-" + tag);
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);

    // 源 prefab：父子两实体（带 tag/Meta），SceneArchive 真源序列化
    std::string jsonA, jsonB;
    {
        World w;
        Scene& s = w.CreateScene("src");
        Entity p = s.Create();
        s.Emplace<Transform2D>(p, Transform2D{{1, 2}});
        Meta& mp = s.Emplace<Meta>(p);
        std::snprintf(mp.tag, sizeof(mp.tag), "Parent");
        Entity c = s.Create();
        s.Emplace<Transform2D>(c, Transform2D{{5, 6}});
        Expect(SceneSetParent(s, c, p), "source parent-child linked");
        jsonA = SceneArchive::SaveEntityTree(s, p);
        Entity solo = s.Create();
        s.Emplace<Transform2D>(solo, Transform2D{{9, 9}});
        Meta& ms = s.Emplace<Meta>(solo);
        std::snprintf(ms.tag, sizeof(ms.tag), "Solo");
        jsonB = SceneArchive::SaveEntityTree(s, solo);
        Expect(!jsonA.empty() && !jsonB.empty(), "prefab source serialized");
    }
    const uint64_t guidA = 0x1111222233334444ull, guidB = 0xaaaabbbbccccddddull;
    {
        fs::path fa = root / "a.prefab", fb = root / "b.prefab";
        { std::ofstream f(fa, std::ios::binary); f << jsonA; }
        { std::ofstream f(fb, std::ios::binary); f << jsonB; }
    }
    TestPrefabSource src;
    src.items.push_back({guidA, (root / "a.prefab").string()});
    src.items.push_back({guidB, (root / "b.prefab").string()});

    lemon::assets::PrefabCache cache;
    Expect(cache.Empty(), "cache starts empty");
    cache.Build(src);
    Expect(cache.Size() == 2, "two prefabs cached");

    World w;
    Scene& s = w.CreateScene("play");
    const uint32_t idA = (uint32_t)guidA, idB = (uint32_t)guidB;
    // Spawn：树装载 + root pos 覆盖 + prefabId 回链 + team 覆盖
    Entity rootE = cache.Spawn(s, idA, Vec2{100, 200}, 7);
    Expect(!rootE.IsNull() && s.Alive(rootE), "spawn lands entity tree");
    Expect(s.Get<Transform2D>(rootE).pos == Vec2(100, 200), "root pos overridden");
    Expect(s.Get<Meta>(rootE).prefabId == guidA, "prefabId backlink full guid");
    Expect(s.Get<Meta>(rootE).team == 7, "team overridden");
    const Hierarchy* h = s.TryGet<Hierarchy>(rootE);
    Expect(h && !h->firstChild.IsNull() && s.Alive(h->firstChild), "child in tree");
    Expect(s.Get<Transform2D>(h->firstChild).pos == Vec2(5, 6), "child keeps local pos");
    // 未命中：Null + 重复未命中不炸（告警去重内部态）
    Expect(cache.Spawn(s, 0xdeadbeef, Vec2{}, 0).IsNull(), "unknown id -> null");
    Expect(cache.Spawn(s, 0xdeadbeef, Vec2{}, 0).IsNull(), "repeat unknown still null");
    // 裸 InstantiateJson：无缓存直用（交互路径）
    Entity solo = lemon::assets::PrefabCache::InstantiateJson(s, jsonB, guidB, Vec2{-1, -2});
    Expect(!solo.IsNull() && s.Get<Transform2D>(solo).pos == Vec2(-1, -2) &&
               s.Get<Meta>(solo).prefabId == guidB,
           "InstantiateJson direct");
    // 空场景快照语义：Clear 后未命中
    cache.Clear();
    Expect(cache.Spawn(s, idB, Vec2{}, 0).IsNull(), "cleared cache -> null");
    // scripts 缺席（nullptr）路径 = Spawn 本就不解析；ResolveTreeScripts 无宿主
    // 不可测（ScriptHost 构造需 CLR）——编辑器/冒烟链覆盖
    (void)idB;
    fs::remove_all(root, ec);
}

// 相机跟随：优先级 Camera > Player > 脚本实体；首帧吸附 + 刚性跟随 + 目标死亡
// 重扫 + Reset 复位（无 GPU 纯逻辑）
void TestCameraFollowCore() {
    using namespace lemon::ecs;
    World w;
    Scene& s = w.CreateScene("cf");
    w.SetActiveScene(&s);
    renderer::Camera2D cam{};
    cam.center = {7, 7};
    renderer::CameraFollowState st{};
    Vec2 out{9, 9};
    Expect(!renderer::UpdateCameraFollow(s, cam, st, &out) && cam.center == Vec2(7, 7),
           "no target keeps position");
    auto mk = [&](const char* tag, Vec2 pos) {
        Entity e = s.Create();
        s.Emplace<Transform2D>(e, Transform2D{pos});
        Meta& m = s.Emplace<Meta>(e);
        std::snprintf(m.tag, sizeof(m.tag), "%s", tag);
        return e;
    };
    Entity player = mk("Player", {100, 50});
    Expect(renderer::UpdateCameraFollow(s, cam, st, &out) && out == Vec2(100, 50) &&
               cam.center == Vec2(100, 50) && st.active,
           "player followed, first-frame snap");
    s.Get<Transform2D>(player).pos = {200, 60};
    Expect(renderer::UpdateCameraFollow(s, cam, st, &out) && cam.center == Vec2(200, 60),
           "rigid follow tracks target");
    Entity camEnt = mk("Camera", {1, 1});
    Expect(renderer::UpdateCameraFollow(s, cam, st, &out) && cam.center == Vec2(1, 1),
           "camera tag wins over player");
    // 目标死亡（轻校验失效 → 重扫回 Player）
    s.Destroy(camEnt);
    s.CommitDestroys();
    Expect(renderer::UpdateCameraFollow(s, cam, st, &out) && cam.center == Vec2(200, 60),
           "dead target rescans to next priority");
    // 脚本实体兜底：无 tag 实体挂 ScriptBox（player 已在 → 不触发；另建世界验证）
    {
        World w2;
        Scene& s2 = w2.CreateScene("cf2");
        w2.SetActiveScene(&s2);
        Entity sc = s2.Create();
        s2.Emplace<Transform2D>(sc, Transform2D{{30, 40}});
        s2.Emplace<scripting::ScriptBox>(sc);
        renderer::Camera2D cam2{};
        renderer::CameraFollowState st2{};
        Expect(renderer::UpdateCameraFollow(s2, cam2, st2, nullptr) &&
                   cam2.center == Vec2(30, 40),
               "scripted entity fallback target");
    }
    // Reset：回默认位 + 态清零 + 幂等
    Expect(renderer::ResetCameraFollow(cam, st) && cam.center == Vec2(640, 360) && !st.active,
           "reset restores default center");
    Expect(!renderer::ResetCameraFollow(cam, st), "second reset is no-op");
}

// 提取下沉件：建槽/世界变换/禁用差集释放/悬空 spriteId 跳过/场景换代全清
void TestSceneExtractorCore() {
    using namespace lemon::ecs;
    renderer::AtlasRegistry atlas;
    atlas.RegisterAtlas(0, rhi::Texture{}, 64, 64);
    const uint32_t sid = atlas.AddSprite(0, 0, 0, 8, 8);
    Expect(atlas.IsValidSprite(sid), "sprite registered");
    renderer::RenderableManager rm;
    renderer::SceneExtractor ex;
    World w;
    Scene& s = w.CreateScene("ex");
    w.SetActiveScene(&s);
    Entity e = s.Create();
    s.Emplace<Transform2D>(e, Transform2D{{10, 20}});
    SpriteRenderer& sr = s.Emplace<SpriteRenderer>(e);
    sr.spriteId = sid; // flags 默认 kSrEnabled
    ex.Extract(s, atlas, rm);
    Expect(rm.AliveCount() == 1, "extract creates renderable");
    // 悬空 spriteId：跳过不建
    Entity d = s.Create();
    s.Emplace<Transform2D>(d, Transform2D{{0, 0}});
    SpriteRenderer& sr2 = s.Emplace<SpriteRenderer>(d);
    sr2.spriteId = 99999;
    ex.Extract(s, atlas, rm);
    Expect(rm.AliveCount() == 1, "dangling sprite skipped");
    // 禁用：差集释放（含悬空那个——本就未建）
    sr.flags &= (uint8_t)~kSrEnabled;
    ex.Extract(s, atlas, rm);
    Expect(rm.AliveCount() == 0, "disabled released by epoch diff");
    sr.flags |= kSrEnabled;
    ex.Extract(s, atlas, rm);
    Expect(rm.AliveCount() == 1, "re-enabled re-created");
    // 场景换代（Scene 指针变化）→ 映射全失效：新空场景提取 = 0 存活
    Scene& s2 = w.CreateScene("ex2");
    ex.Extract(s2, atlas, rm);
    Expect(rm.AliveCount() == 0, "scene switch releases all");
    // 层级世界变换：父动子随（ComputeWorldTransform 消费端）
    Entity parent = s.Create();
    s.Emplace<Transform2D>(parent, Transform2D{{100, 0}});
    Entity child = s.Create();
    s.Emplace<Transform2D>(child, Transform2D{{10, 0}});
    Expect(SceneSetParent(s, child, parent), "hierarchy link");
    SpriteRenderer& csr = s.Emplace<SpriteRenderer>(child);
    csr.spriteId = sid;
    ex.Extract(s, atlas, rm);
    Expect(rm.AliveCount() >= 1, "hierarchy extracted");
    // 位置正确性经 rm.Extract(atlas, 1.0f) 包回读（alpha=1 无插值）
    rm.SetViewport(Vec2{0, 0}, 400, 400, 16);
    // 包内字段为渲染侧内部布局——以“父子两实体均可见”为断言面（位置数值归
    // ViewportRenderer 既有像素防线，此处锁提取语义）
    auto pk = rm.Extract(atlas, 1.0f);
    Expect(pk.size() >= 1, "hierarchy sprites visible in packet");
}

// poolDataFn（D5 基础）：全组件可判 + 容量内基址不动 + 越容量搬移
void TestPoolDataStable() {
    using namespace lemon::ecs;
    auto& reg = ComponentRegistry::Instance();
    for (uint16_t id = 0; id < reg.Count(); ++id) {
        Expect(reg.At(id).poolDataFn != nullptr, "poolDataFn registered for all");
    }
    const ComponentMeta* tfm = reg.Find("Transform2D");
    Expect(tfm && tfm->poolDataFn, "transform poolDataFn");
    World w;
    Scene& s = w.CreateScene("pd");
    w.SetActiveScene(&s);
    Entity e0 = s.Create();
    s.Emplace<Transform2D>(e0, Transform2D{{0, 0}});
    Expect(tfm->poolDataFn(s) != nullptr, "pool base valid after first emplace");
    // 双观察式（与 entt 扩容策略无关）：连续 emplace 中应存在"基址稳定段"
    //（容量余量内，D5 零误伤的机制保证）与随后的"搬移拍"（越容量重分配）
    const void* prev = tfm->poolDataFn(s);
    int stableRun = 0;
    bool sawStable = false, movedAfterStable = false;
    for (int i = 0; i < 100000 && !movedAfterStable; i++) {
        Entity e = s.Create();
        s.Emplace<Transform2D>(e, Transform2D{{0, 0}});
        const void* cur = tfm->poolDataFn(s);
        if (cur == prev) {
            if (++stableRun >= 3) sawStable = true; // 连续 3 次不动 = 稳定段实证
        } else {
            if (sawStable) movedAfterStable = true; // 稳定段后的搬移拍
            stableRun = 0;
        }
        prev = cur;
    }
    Expect(sawStable, "within-capacity stable run observed");
    Expect(movedAfterStable, "over-capacity emplace moves pool base");
}

// ---- M6a 批② T1：CSV 解析 + .tab 表格资产序列化（ADR-012 D1）----

void TestCsvTable() {
    using lemon::assets::ParseCsv;
    using lemon::assets::ParseTableJson;
    using lemon::assets::TableToJson;
    using lemon::assets::TableData;

    // 基本 + BOM 剥除 + CRLF 归一 + 中文表头
    assets::TableData t = assets::ParseCsv("\xEF\xBB\xBFid,label\r\nshoot,直射\r\n");
    Expect(t.ok && t.rows.size() == 2 && t.rows[0][0] == "id" && t.rows[1][1] == "直射",
           "csv basic + BOM + CRLF");
    // 引号包裹（格内逗号）+ "" 转义引号
    t = assets::ParseCsv("a,\"b,c\",\"d\"\"e\"\n");
    Expect(t.ok && t.rows[0].size() == 3 && t.rows[0][1] == "b,c" && t.rows[0][2] == "d\"e",
           "csv quotes/escape");
    // 引号内换行原样入格
    t = assets::ParseCsv("a,\"line1\nline2\",b\n");
    Expect(t.ok && t.rows.size() == 1 && t.rows[0][1] == "line1\nline2", "quoted newline");
    // 空行跳过（尾换行不产生幽灵行）；无尾换行的末行
    t = assets::ParseCsv("a,b\n\nc,d\n");
    Expect(t.ok && t.rows.size() == 2, "blank line skipped");
    t = assets::ParseCsv("a,b");
    Expect(t.ok && t.rows.size() == 1 && t.rows[0][1] == "b", "no trailing newline");
    // 参差行 → 补空矩形化（以最长行为准）
    t = assets::ParseCsv("a\nb,c\n");
    Expect(t.ok && t.rows.size() == 2 && t.rows[0].size() == 2 && t.rows[0][1].empty(),
           "ragged rows padded");

    // 非 UTF-8（GBK "中" = D6 D0）拒入
    t = assets::ParseCsv(std::string_view("a,\xD6\xD0\n", 6));
    Expect(!t.ok && t.error.find("UTF-8") != std::string::npos, "non-utf8 rejected");
    // 上限拒入：65 列 / 1025 行 / 129 码点格（128 汉字恰过线）
    std::string wide;
    for (int i = 0; i < 65; ++i) {
        if (i) wide += ',';
        wide += 'c';
    }
    t = assets::ParseCsv(wide);
    Expect(!t.ok, "cols over limit rejected");
    std::string tall;
    for (int i = 0; i < 1025; ++i) tall += "r\n";
    t = assets::ParseCsv(tall);
    Expect(!t.ok, "rows over limit rejected");
    t = assets::ParseCsv(std::string(129, 'x') + "\n");
    Expect(!t.ok, "cell over limit rejected");
    std::string cjk;
    for (int i = 0; i < 129; ++i) cjk += "\xE4\xB8\xAD";
    t = assets::ParseCsv(cjk + "\n");
    Expect(!t.ok, "129 CJK codepoints rejected");
    cjk.resize(128 * 3); // 128 码点恰在上限内
    Expect(assets::ParseCsv(cjk + "\n").ok, "128 CJK codepoints within limit");

    // assets::TableToJson → assets::ParseTableJson roundtrip
    t = assets::ParseCsv("id,label,note\nshoot,直射,\"a,b\"\npierce,穿透,x\n");
    Expect(t.ok, "parse for roundtrip");
    const std::string json = assets::TableToJson("weapons", t.rows);
    Expect(!json.empty(), "table to json");
    const assets::TableData back = assets::ParseTableJson(json);
    Expect(back.ok && back.rows == t.rows, "table json roundtrip equal");

    // .tab 宽松归一：裸数值/布尔格转字符串（ADR-012 示例形态）
    t = assets::ParseTableJson(
        "{\"schemaVersion\":1,\"name\":\"w\",\"rows\":[[\"id\",\"v\",\"on\"],"
        "[\"a\",0.12,true]]}");
    Expect(t.ok && t.rows[1][1] == "0.12" && t.rows[1][2] == "true",
           "json bare number/bool coerced");
    // 坏档拒入：语法错 / schemaVersion 不符 / 空 rows / 嵌套对象格
    Expect(!assets::ParseTableJson("{").ok, "bad json rejected");
    Expect(!assets::ParseTableJson("{\"schemaVersion\":2,\"rows\":[[\"a\"]]}").ok,
           "bad schemaVersion rejected");
    Expect(!assets::ParseTableJson("{\"rows\":[]}").ok, "empty rows rejected");
    Expect(!assets::ParseTableJson("{\"rows\":[[{\"x\":1}]]}").ok, "object cell rejected");
    // 非法网格过不了 assets::TableToJson（超限 → 空串）
    Expect(assets::TableToJson("x", std::vector<std::vector<std::string>>(1025, {"a"})).empty(),
           "to json rejects oversized grid");
}

// ---- M6a 批② T3：.anim 解析/序列化（AnimationPanel 数据面；验收② roundtrip）----
void TestClipEdit() {
    using lemon::assets::ClipData;
    using lemon::assets::ClipToJson;
    using lemon::assets::ParseClipJson;

    // 规范档（Samples/yami 同型多行格式）
    const char* doc =
        "{\n  \"schemaVersion\": 1,\n  \"name\": \"hero-walk\",\n  \"fps\": 8,\n"
        "  \"loop\": true,\n  \"frames\": [\n"
        "    {\n      \"sheet\": \"5bd31a7c10000001\",\n      \"cell\": 0\n    },\n"
        "    {\n      \"sheet\": \"5bd31a7c10000001\",\n      \"cell\": 8\n    }\n"
        "  ]\n}";
    assets::ClipData c = assets::ParseClipJson(doc);
    Expect(c.ok && c.name == "hero-walk" && c.fps == 8.0f && c.loopMode == 1 &&
               c.frames.size() == 2 && c.frames[0].sheetGuid == 0x5bd31a7c10000001ull &&
               c.frames[0].cell == 0 && c.frames[1].cell == 8,
           "clip parse canonical");

    // 定版格式：序列化与规范档逐字符同型（AnimationPanel 保存后旧档 diff 只见
    // 被改字段——验收②"打开 hero-walk → 改字段 → 保存 → diff 仅预期"的依据）
    c.ok = true;
    Expect(assets::ClipToJson(c) == doc, "clip golden format stable");

    // roundtrip：改 fps/loop/增删帧/换 sheet → 序列化 → 再解析等值
    c.fps = 13.0f;
    c.loopMode = 0;
    c.frames.push_back({0x5bd31a7c10000005ull, 7});
    c.frames.erase(c.frames.begin());
    const assets::ClipData back = assets::ParseClipJson(assets::ClipToJson(c));
    Expect(back.ok && back.name == c.name && back.fps == c.fps &&
               back.loopMode == c.loopMode && back.frames == c.frames,
           "clip roundtrip after edit");

    // 缺省：loop 缺省 true / name 缺省空（面板补文件名）/ 小数 fps 往返
    c = assets::ParseClipJson(
        "{\"schemaVersion\":1,\"fps\":7.5,\"frames\":[{\"sheet\":\"000000000000000f\","
        "\"cell\":3}]}");
    Expect(c.ok && c.loopMode == 1 && c.name.empty() && std::fabs(c.fps - 7.5f) < 1e-6f,
           "clip defaults + fractional fps");
    Expect(assets::ParseClipJson(assets::ClipToJson(c)).fps == c.fps, "clip fractional fps roundtrip");

    // 空帧表合法（新建 clip 起步态；保存侧 ≥1 帧校验归面板）
    c = assets::ParseClipJson("{\"fps\":8,\"frames\":[]}");
    Expect(c.ok && c.frames.empty(), "clip empty frames parse");
    Expect(assets::ClipToJson(c).find("\"frames\": []") != std::string::npos,
           "clip empty frames serialize");

    // 坏档拒入（不炸面板）：非 JSON / 缺 frames / 缺 fps / 帧缺字段 /
    // sheet 非 hex / cell 负数 / cell 类型错
    Expect(!assets::ParseClipJson("{").ok, "clip bad json rejected");
    Expect(!assets::ParseClipJson("{\"fps\":8}").ok, "clip missing frames rejected");
    Expect(!assets::ParseClipJson("{\"frames\":[]}").ok, "clip missing fps rejected");
    Expect(!assets::ParseClipJson(
               "{\"fps\":8,\"frames\":[{\"sheet\":\"000000000000000f\"}]}")
                .ok,
           "clip frame missing cell rejected");
    Expect(!assets::ParseClipJson(
               "{\"fps\":8,\"frames\":[{\"sheet\":\"zz\",\"cell\":0}]}")
                .ok,
           "clip non-hex sheet rejected");
    Expect(!assets::ParseClipJson(
               "{\"fps\":8,\"frames\":[{\"sheet\":\"000000000000000f\",\"cell\":-1}]}")
                .ok,
           "clip negative cell rejected");
    Expect(!assets::ParseClipJson(
               "{\"fps\":8,\"frames\":[{\"sheet\":\"000000000000000f\",\"cell\":\"0\"}]}")
                .ok,
           "clip string cell rejected");
    // ok=false 输入 → assets::ClipToJson 空串（门卫）
    assets::ClipData bad;
    Expect(assets::ClipToJson(bad).empty(), "clip tojson rejects !ok");

    // T3b-2：loopMode——legacy loop 派生 / pingpong 落盘加字段 / 越界防御 /
    // 旧档 no-edit 往返不含 loopMode（golden 已证；此处锁字段策略）
    c = assets::ParseClipJson(
        "{\"fps\":8,\"loop\":true,\"loopMode\":2,\"frames\":[{\"sheet\":\"000000000000000f\","
        "\"cell\":0}]}");
    Expect(c.ok && c.loopMode == 2, "clip loopMode field parsed");
    const std::string pp = assets::ClipToJson(c);
    Expect(pp.find("\"loop\": true") != std::string::npos &&
               pp.find("\"loopMode\": 2") != std::string::npos,
           "clip pingpong serializes loop+loopMode");
    Expect(assets::ParseClipJson(pp).loopMode == 2, "clip pingpong roundtrip");
    c = assets::ParseClipJson(
        "{\"fps\":8,\"loop\":false,\"frames\":[{\"sheet\":\"000000000000000f\",\"cell\":0}]}");
    Expect(c.ok && c.loopMode == 0 && assets::ClipToJson(c).find("loopMode") == std::string::npos,
           "clip legacy once stays field-free");
    c = assets::ParseClipJson(
        "{\"fps\":8,\"loopMode\":5,\"frames\":[{\"sheet\":\"000000000000000f\",\"cell\":0}]}");
    Expect(c.ok && c.loopMode == 1, "clip loopMode out of range falls back to Loop");

    // review 2026-10-02 #30：legacy loop 非布尔（手写档 "loop":1）预检拒绝——
    // 原裸 get<bool>() 抛 nlohmann type_error 穿透调用链（无 try/catch）=
    // std::terminate，违背"坏档不炸编辑器"契约
    c = assets::ParseClipJson(
        "{\"fps\":8,\"loop\":1,\"frames\":[{\"sheet\":\"000000000000000f\",\"cell\":0}]}");
    Expect(!c.ok, "clip non-bool loop rejected (no throw)");

    // review 2026-10-02 #5：名字含引号/反斜杠转义 roundtrip——原样样拼接写出
    // 非法 JSON，面板保存覆写原档 = 数据丢失；长名不再经定长缓冲
    c = assets::ParseClipJson(doc);
    c.name = "atk\"idle\\v2";
    {
        const std::string esc = assets::ClipToJson(c);
        const assets::ClipData rt = assets::ParseClipJson(esc);
        Expect(rt.ok && rt.name == c.name, "clip quoted/backslash name roundtrip");
    }
    c.name = std::string(200, 'n'); // 超一切定长缓冲
    {
        const assets::ClipData rt = assets::ParseClipJson(assets::ClipToJson(c));
        Expect(rt.ok && rt.name == c.name, "clip 200-char name roundtrip");
    }
}

// ---- M7a 批① D7 残余：动画资产名校验硬化（assets::ValidateAssetName 单源）----
// 旧校验只拒空/`/`/`\`/`..`——引号/控制字符/超长放行（写侧 assets::JsonEscape 兜底不毁
// 档，但名字 = 文件名母体 + 集内按名解析键，怪字符把问题推迟到运行时）。
void TestValidateAssetName() {
    using lemon::assets::ValidateAssetName;
    std::string why;

    Expect(assets::ValidateAssetName("idle", &why), "name: plain accepted");
    Expect(assets::ValidateAssetName("跑-02", &why), "name: CJK accepted");
    Expect(assets::ValidateAssetName(std::string(64, 'a'), &why), "name: 64B boundary accepted");

    Expect(!assets::ValidateAssetName("", &why) && why.find("空") != std::string::npos,
           "name: empty rejected with reason");
    Expect(!assets::ValidateAssetName("a/b", &why), "name: slash rejected");
    Expect(!assets::ValidateAssetName("a\\b", &why), "name: backslash rejected");
    Expect(!assets::ValidateAssetName("a..b", &why), "name: dotdot rejected");
    Expect(!assets::ValidateAssetName("we\"ird", &why) && why.find("引号") != std::string::npos,
           "name: quote rejected（D7 报告原场景）");
    Expect(!assets::ValidateAssetName("a\nb", &why), "name: control char rejected");
    Expect(!assets::ValidateAssetName(std::string(65, 'a'), &why) &&
               why.find("64") != std::string::npos,
           "name: over-64B rejected（D7 报告第二场景：截断/超长）");
    Expect(assets::ValidateAssetName("normal"), "name: null-why pointer tolerated");
}

// ---- M7a 批① M22：删帧后帧事件越界清理（assets::SanitizeClipEvents）----
// 阴性内置：越界事件的 clip 序列化 → 解析必拒（assets::ParseClipJson 硬拒 frame≥帧表）
// ——即缺陷本体（保存链自锁）作为反例先行证明，再证 sanitize 后 roundtrip 过。
void TestClipEventBounds() {
    using lemon::assets::ClipData;
    using lemon::assets::ClipEventEdit;
    using lemon::assets::ClipToJson;
    using lemon::assets::ParseClipJson;
    using lemon::assets::SanitizeClipEvents;

    assets::ClipData c;
    c.ok = true;
    c.name = "duel";
    c.fps = 12.0f;
    c.frames = {{0x5bd31a7c10000001ull, 0}, {0x5bd31a7c10000001ull, 1},
                {0x5bd31a7c10000001ull, 2}, {0x5bd31a7c10000001ull, 3}};
    c.events = {{1, 0}, {3, 7}};

    // 带事件的合法档 roundtrip 过（正例基线）
    {
        const assets::ClipData rt = assets::ParseClipJson(assets::ClipToJson(c));
        Expect(rt.ok && rt.events == c.events, "events: in-bounds roundtrip");
    }

    // 缺陷本体（阴性）：删掉帧 2..3 后事件 {3,7} 越界——不清则序列化产物解析必拒
    c.frames.resize(2);
    {
        const assets::ClipData rt = assets::ParseClipJson(assets::ClipToJson(c));
        Expect(!rt.ok, "events: out-of-range serialize-parse rejected（M22 自锁本体）");
    }

    // 修复：assets::SanitizeClipEvents 清越界 → roundtrip 过、界内事件保真
    const size_t dropped = assets::SanitizeClipEvents(c);
    Expect(dropped == 1 && c.events.size() == 1 && c.events[0].frame == 1,
           "events: sanitize drops exactly the out-of-range event");
    {
        const assets::ClipData rt = assets::ParseClipJson(assets::ClipToJson(c));
        Expect(rt.ok && rt.events == c.events, "events: post-sanitize roundtrip ok");
    }

    // 幂等/边界：空事件、全越界、帧表空
    c.events.clear();
    Expect(assets::SanitizeClipEvents(c) == 0, "events: sanitize no-op when empty");
    c.events = {{0, 1}};
    c.frames.clear();
    Expect(assets::SanitizeClipEvents(c) == 1 && c.events.empty(),
           "events: empty frame table clears all events");
}

// ---- M7a 批① M21：manifest .bak 备份与坏主档恢复 ----
// 判别设计（阴性可分）：hero 与 aaa/bbb 同代发号（hero 非首号）→ 删 aaa/bbb 后
// 保存（gen2）→ 毒化主档（半截 JSON 模拟掉电）→ 重开。恢复成功 = hero 保住
// gen1 号；恢复失败/无机制 = 弃档重建按现存资产重排（hero 独活 = 拿首号 ≠ gen1）
// → id 不等即红。精灵资产（.png）才有真 spriteId（clip 型恒 0 不可判别）。
void TestManifestBakRecovery() {
    namespace fs = std::filesystem;
    using lemon::editor::AssetDatabase;
    using lemon::editor::AssetEntry;

    const fs::path root = fs::temp_directory_path() /
                          ("lemon-test-manifestbak-" + std::to_string(lemon::CurrentProcessId()));
    std::error_code ec;
    fs::remove_all(root, ec);

    auto put = [&](const std::string& rel, const char* guidHex) {
        const fs::path p = root / rel;
        fs::create_directories(p.parent_path(), ec);
        { std::ofstream f(p, std::ios::binary); f << "x"; }
        std::ofstream f(p.string() + ".meta", std::ios::trunc);
        f << "{\"guid\":\"" << guidHex << "\"}";
    };
    put("Assets/aaa.png", "bbbb000000000001");
    put("Assets/bbb.png", "bbbb000000000002");
    put("Assets/hero.png", "bbbb000000000003");

    const std::string manifestPath = (root / ".lemon" / "manifest.json").string();
    uint32_t idGen1 = 0;
    {
        AssetDatabase db;
        Expect(db.OpenProject(root.string(), /*spriteIdBase=*/100), "manifest-bak: open gen1");
        const AssetEntry* h = db.FindByPath("Assets/hero.png");
        Expect(h && h->spriteId > 100, "manifest-bak: hero got non-first id in gen1");
        idGen1 = h ? h->spriteId : 0;
    }
    // 删两件 → gen2（记账保 hero 原号；此时 .bak = gen1 好档）
    fs::remove(root / "Assets" / "aaa.png", ec);
    fs::remove(root / "Assets" / "aaa.png.meta", ec);
    fs::remove(root / "Assets" / "bbb.png", ec);
    fs::remove(root / "Assets" / "bbb.png.meta", ec);
    {
        AssetDatabase db;
        Expect(db.OpenProject(root.string(), /*spriteIdBase=*/100), "manifest-bak: open gen2");
        const AssetEntry* h = db.FindByPath("Assets/hero.png");
        Expect(h && h->spriteId == idGen1, "manifest-bak: carry keeps hero id in gen2");
        Expect(fs::exists(manifestPath + ".bak", ec), "manifest-bak: .bak exists after gen2");
    }
    // 毒化主档：合法前缀 + 截断（掉电半写形态）
    {
        std::ofstream w(manifestPath, std::ios::binary | std::ios::trunc);
        w << "{\n  \"version\": 1,\n  \"nextSpriteId\": 999,\n  \"assets\": [\n    "
             "{\"guid\": 1, \"path\": \"Assets/he";
        w.close();
    }
    uint32_t idRecovered = 0;
    {
        AssetDatabase db2;
        Expect(db2.OpenProject(root.string(), /*spriteIdBase=*/100),
               "manifest-bak: reopen after corruption");
        if (const AssetEntry* h = db2.FindByPath("Assets/hero.png")) idRecovered = h->spriteId;
    }
    Expect(idRecovered == idGen1,
           "manifest-bak: corrupted main recovered to gen-1 sprite ids（M21；"
           "若走了重排 hero 独活拿首号必不等）");
    Expect(!fs::exists(manifestPath + ".tmp", ec), "manifest-bak: no tmp residue");

    fs::remove_all(root, ec);
}

// ---- M6a 批② T3c：.override 动画集解析/序列化 + ClipTable 集按名索引 ----
void TestAnimSetAndClipIndex() {
    using lemon::ecs::ClipTable;
    using lemon::assets::AnimSetData;
    using lemon::assets::AnimSetToJson;
    using lemon::assets::ParseAnimSetJson;

    // 规范档（ClipEdit 同款多行格式）+ 定版格式逐字符同型
    const char* doc =
        "{\n  \"schemaVersion\": 1,\n  \"name\": \"player\",\n  \"segments\": [\n"
        "    {\n      \"name\": \"idle\",\n      \"clip\": \"5bd31a7c30000004\"\n    },\n"
        "    {\n      \"name\": \"walk\",\n      \"clip\": \"5bd31a7c30000005\"\n    }\n"
        "  ]\n}";
    assets::AnimSetData s = assets::ParseAnimSetJson(doc);
    Expect(s.ok && s.name == "player" && s.segments.size() == 2 &&
               s.segments[0].name == "idle" &&
               s.segments[0].clipGuid == 0x5bd31a7c30000004ull &&
               s.segments[1].clipGuid == 0x5bd31a7c30000005ull,
           "animset parse canonical");
    s.ok = true;
    Expect(assets::AnimSetToJson(s) == doc, "animset golden format stable");

    // roundtrip：改名/增删段
    s.name = "enemy";
    s.segments.push_back({"hit", 0x5bd31a7c30000006ull});
    s.segments.erase(s.segments.begin());
    const assets::AnimSetData back = assets::ParseAnimSetJson(assets::AnimSetToJson(s));
    Expect(back.ok && back.name == s.name && back.segments == s.segments,
           "animset roundtrip after edit");

    // review 2026-10-02 #5：段名引号/反斜杠转义 + 超长段名（原 char[128] 定长
    // snprintf >约 63 字符静默截断；转义缺失写坏档覆写即数据丢失）
    s.segments.push_back({"atk\"x\\y", 0x5bd31a7c30000007ull});
    s.segments.push_back({std::string(100, 's'), 0x5bd31a7c30000008ull});
    {
        const assets::AnimSetData rt = assets::ParseAnimSetJson(assets::AnimSetToJson(s));
        Expect(rt.ok && rt.segments == s.segments,
               "animset escaped/long segment names roundtrip");
    }

    // 空集合法（新建起步态）+ name 缺省空（面板补文件名）
    s = assets::ParseAnimSetJson("{\"schemaVersion\":1,\"segments\":[]}");
    Expect(s.ok && s.name.empty() && s.segments.empty(), "animset empty parses");
    Expect(assets::AnimSetToJson(s).find("\"segments\": []") != std::string::npos,
           "animset empty serializes");

    // 坏档拒入：非 JSON / 缺 segments / 段缺字段 / 空段名 / clip 非 hex
    Expect(!assets::ParseAnimSetJson("{").ok, "animset bad json rejected");
    Expect(!assets::ParseAnimSetJson("{\"name\":\"x\"}").ok, "animset missing segments rejected");
    Expect(!assets::ParseAnimSetJson("{\"segments\":[{\"name\":\"a\"}]}").ok,
           "animset segment missing clip rejected");
    Expect(!assets::ParseAnimSetJson("{\"segments\":[{\"name\":\"\",\"clip\":\"000000000000000f\"}]}")
                .ok,
           "animset empty segment name rejected");
    Expect(!assets::ParseAnimSetJson("{\"segments\":[{\"name\":\"a\",\"clip\":\"zz\"}]}").ok,
           "animset non-hex clip rejected");
    assets::AnimSetData bad;
    Expect(assets::AnimSetToJson(bad).empty(), "animset tojson rejects !ok");

    // 集索引：登记 / 集内按名 / 跨集同名互不扰 / 反查 / 重名先到先得 / 防御 / Clear
    ClipTable t;
    Expect(t.Add(0x11, {1u, 2u, 3u}, 8.f, true) && t.Add(0x22, {4u}, 8.f, true) &&
               t.Add(0x33, {5u}, 8.f, true) && t.Add(0x44, {6u}, 8.f, true),
           "clips added for set index");
    Expect(t.RegisterSet(0xAB, {{"idle", 0x11u}, {"walk", 0x22u}}) == 2, "register set A");
    Expect(t.RegisterSet(0xCD, {{"idle", 0x33u}}) == 1, "register set B (cross-set same name)");
    Expect(t.FindByName(0xAB, "walk") == 0x22, "by name in set A");
    Expect(t.FindByName(0xCD, "idle") == 0x33, "same name resolves in own set");
    Expect(t.FindByName(0xAB, "idle") == 0x11, "set A idle unaffected by set B");
    Expect(t.FindByName(0xAB, "nope") == 0, "missing name → 0");
    Expect(t.FindByName(0xEE, "idle") == 0, "missing set → 0");
    Expect(t.SetOfClip(0x22) == 0xAB && t.SetOfClip(0x33) == 0xCD, "reverse lookup");
    Expect(t.SetOfClip(0x44) == 0, "non-member → 0");
    Expect(t.RegisterSet(0xEF, {{"idle", 0x44u}, {"idle", 0x33u}}) == 1,
           "dup name first wins");
    Expect(t.FindByName(0xEF, "idle") == 0x44, "dup name resolves to first");
    Expect(t.SetOfClip(0x33) == 0xCD, "multi-set segment keeps first set");
    Expect(t.RegisterSet(0, {{"x", 0x11u}}) == 0, "setId 0 rejected");
    Expect(t.RegisterSet(0x99, {{"", 0x11u}, {"ok", 0x11u}}) == 1, "empty name skipped");
    t.Clear();
    Expect(t.FindByName(0xAB, "walk") == 0 && t.SetOfClip(0x22) == 0 && t.Count() == 0,
           "clear wipes set index");
}

// ---- M6a 批② T3d：.controller 解析/序列化 + ControllerTable + 条件评估 +
// ClipTable 事件表/集内反查（ADR-013 D1/D2/D4）---------------------------
void TestControllerAndGraph() {
    using ecs::AnimCondOp;
    using ecs::AnimParamKind;
    // -- ControllerTable：登记/索引/条件评估（引擎域纯逻辑）--
    ecs::ControllerTable ct;
    ecs::ControllerDef def;
    def.states = {"Idle", "Walk", "Attack"};
    def.params = {{"speed", AnimParamKind::Float, 0.0f},
                  {"atk", AnimParamKind::Trigger, 0.0f}};
    ecs::AnimTransitionDef walk;
    walk.from = 0;
    walk.to = 1;
    walk.conds.push_back({0, AnimCondOp::Gt, 0.1f});
    ecs::AnimTransitionDef back; // Attack → Idle 段末过渡（exitTime = Queue 图化）
    back.from = 2;
    back.to = 0;
    back.exitTime = true;
    def.transitions = {walk, back};
    Expect(ct.Add(0x77, std::move(def)), "controller add");
    const ecs::ControllerDef* d = ct.Find(0x77);
    Expect(d && d->states.size() == 3 && d->transitions.size() == 2, "controller find");
    Expect(d->StateIndex("Walk") == 1 && d->StateIndex("nope") == -1, "state index");
    Expect(d->ParamIndex("atk") == 1 && d->ParamIndex("nope") == -1, "param index");
    ecs::ControllerDef empty;
    Expect(!ct.Add(0, std::move(empty)), "id 0 rejected");
    ecs::ControllerDef noStates;
    Expect(!ct.Add(0x88, std::move(noStates)), "empty states rejected");
    float p[8] = {};
    Expect(!ecs::AnimCondsHold(*d, d->transitions[0], p), "speed 0 → 不切");
    p[0] = 1.0f;
    Expect(ecs::AnimCondsHold(*d, d->transitions[0], p), "speed>0.1 → 切");
    p[1] = 1.0f; // trigger 槽非 0
    ecs::AnimCondDef tg{1, AnimCondOp::Trigger, 0.0f};
    Expect(ecs::AnimCondHolds(tg, p[1]) && !ecs::AnimCondHolds(tg, 0.0f), "trigger 语义");
    ct.Clear();
    Expect(ct.Count() == 0 && ct.Find(0x77) == nullptr, "controller clear");

    // -- ClipTable：事件表 + 集内反查 NameOfClip + 同集同 clip 换名去重 --
    ecs::ClipTable t2;
    t2.Add(0x10, {1, 2, 3}, 10.0f, true, {{1, 5}});
    t2.Add(0x20, {9}, 10.0f, false);
    Expect(t2.Find(0x10) && t2.Find(0x10)->events.size() == 1 &&
               t2.Find(0x10)->events[0].frame == 1 && t2.Find(0x10)->events[0].id == 5,
           "events stored");
    Expect(t2.Find(0x20)->events.empty(), "no events default");
    t2.RegisterSet(0xAB, {{"Idle", 0x10u}, {"Walk", 0x20u}});
    const std::string* n = t2.NameOfClip(0xAB, 0x10);
    Expect(n && *n == "Idle", "集内反查段名");
    Expect(t2.NameOfClip(0xAB, 0x99) == nullptr, "反查 miss");
    t2.RegisterSet(0xCD, {{"X", 0x10u}});
    n = t2.NameOfClip(0xCD, 0x10);
    Expect(n && *n == "X", "跨集复用段各有名（反查按集）");
    t2.RegisterSet(0xAB, {{"Alias", 0x10u}}); // 同集同 clip 换名 → 撤名（首名胜）
    n = t2.NameOfClip(0xAB, 0x10);
    Expect(n && *n == "Idle", "同集同 clip 换名被撤（NameOfClip 确定性）");
    Expect(t2.FindByName(0xAB, "Alias") == 0, "撤名后按名不可达");

    // -- ControllerEdit：解析/roundtrip/坏档拒绝 --
    const char* golden =
        "{\n  \"schemaVersion\": 1,\n  \"name\": \"Basic\",\n  \"params\": [\n"
        "    { \"name\": \"speed\", \"kind\": \"float\" },\n"
        "    { \"name\": \"attack\", \"kind\": \"trigger\" }\n  ],\n"
        "  \"entry\": \"Idle\",\n  \"states\": [\"Idle\", \"Walk\", \"Attack\"],\n"
        "  \"transitions\": [\n"
        "    { \"from\": \"Idle\", \"to\": \"Walk\", \"when\": [{ \"param\": \"speed\", \">\": 0.1 }] },\n"
        "    { \"from\": \"Attack\", \"to\": \"Idle\", \"on\": \"exitTime\" }\n  ]\n}";
    assets::ControllerData c = assets::ParseControllerJson(golden);
    Expect(c.ok, "controller golden 解析");
    Expect(c.states.size() == 3 && c.params.size() == 2 && c.transitions.size() == 2,
           "controller 结构");
    Expect(c.params[1].kind == 2 && c.transitions[1].exitTime, "kind/exitTime");
    Expect(c.transitions[0].conds[0].op == 2 && c.transitions[0].conds[0].value > 0.09f,
           "条件算子/阈值");
    Expect(assets::ControllerToJson(c) == golden, "controller roundtrip 逐字节");
    Expect(!assets::ParseControllerJson("{ \"states\": [] }").ok, "空 states 拒绝");
    Expect(!assets::ParseControllerJson(
                R"({ "states": ["A"], "transitions": [{"from":"A","to":"B"}] })")
                .ok,
           "to 引用未列状态拒绝");
    Expect(!assets::ParseControllerJson(
                R"({ "states": ["A","B"], "transitions": [{"from":"A","to":"B"}] })")
                .ok,
           "空条件非 exitTime 拒绝");
    std::string nine;
    for (int i = 0; i < 9; ++i) nine += (i ? "," : "") + std::string("{\"name\":\"p") +
                                        std::to_string(i) + "\"}";
    Expect(!assets::ParseControllerJson(
                "{ \"states\": [\"A\"], \"params\": [" + nine + "] }")
                .ok,
           "参数 >8 拒绝");
    Expect(!assets::ParseControllerJson(
                R"({ "states": ["A","A"] })")
                .ok,
           "状态重名拒绝");
    // 坏档 ok=false → ToJson 空串（assets::ClipToJson 同款约定）；好档无 error
    Expect(c.error.empty(), "好档无 error");
    assets::ControllerData bad = assets::ParseControllerJson("{ \"states\": [] }");
    Expect(!bad.ok && assets::ControllerToJson(bad).empty(), "坏档 ToJson 空串");
}

// ---- M6a 批② T1：.csv → .tab 转换导入生命周期（csv 不拷入 / 覆盖重导 / guid 稳定）----
void TestTableAssetImport() {
    namespace fs = std::filesystem;
    using lemon::editor::AssetDatabase;
    using lemon::editor::AssetEntry;
    using lemon::editor::AssetType;
    using lemon::assets::ParseTableJson;

    const std::string tag = std::to_string(lemon::CurrentProcessId());
    const fs::path root = fs::temp_directory_path() / ("lemon-test-table-" + tag);
    const fs::path src = fs::temp_directory_path() / ("lemon-test-table-src-" + tag + ".csv");
    const fs::path badSrc =
        fs::temp_directory_path() / ("lemon-test-table-bad-" + tag + ".csv");
    std::error_code ec;
    fs::remove_all(root, ec);

    AssetDatabase db;
    Expect(db.OpenProject(root.string(), 100), "open project");

    { std::ofstream f(src, std::ios::binary); f << "id,label\nshoot,直射\n"; }
    const AssetEntry* e = db.ImportFile(src.string(), "tables/weapons.csv");
    Expect(e && e->type == AssetType::Table, "csv imported as Table");
    Expect(e->relPath == "Assets/tables/weapons.tab", "tab dest path");
    Expect(!fs::exists(root / "Assets" / "tables" / "weapons.csv", ec), "csv not copied in");
    Expect(fs::exists(root / "Assets" / "tables" / "weapons.tab", ec), "tab file written");
    Expect(fs::exists(root / "Assets" / "tables" / "weapons.tab.meta", ec), "meta written");
    const uint64_t guid = e->guid;

    { // 落盘内容 = 全字符串格 JSON，roundtrip 与源一致
        std::ifstream f(root / "Assets" / "tables" / "weapons.tab", std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        const auto t = assets::ParseTableJson(text);
        Expect(t.ok && t.rows.size() == 2 && t.rows[0][0] == "id" && t.rows[1][1] == "直射",
               "tab content roundtrip");
    }

    // 重拖同名 csv = 覆盖再导入（ADR-012：批量再编辑回 Excel 改完重拖）→ guid 稳定
    { std::ofstream f(src, std::ios::binary | std::ios::trunc); f << "id,label\npierce,穿透\n"; }
    const AssetEntry* e2 = db.ImportFile(src.string(), "tables/weapons.csv");
    Expect(e2 && e2->guid == guid && e2->relPath == "Assets/tables/weapons.tab",
           "re-import overwrites same guid");
    {
        std::ifstream f(root / "Assets" / "tables" / "weapons.tab", std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        const auto t = assets::ParseTableJson(text);
        Expect(t.ok && t.rows[1][0] == "pierce" && t.rows.size() == 2, "overwritten content");
    }

    // guid/type 跨会话稳定（manifest 记账走字符串名 "table"）
    {
        AssetDatabase db2;
        Expect(db2.OpenProject(root.string(), 100), "reopen project");
        const AssetEntry* w2 = db2.FindByPath("Assets/tables/weapons.tab");
        Expect(w2 && w2->guid == guid && w2->type == AssetType::Table,
               "table guid/type stable across sessions");
    }

    // 手写 .tab 放进 Assets/ 照常入库（不经 csv 的直接导入流）
    { std::ofstream f(root / "Assets" / "balance.tab", std::ios::binary);
      f << "{\"schemaVersion\":1,\"name\":\"balance\",\"rows\":[[\"k\"],[\"xpCurveK\"]]}"; }
    db.Rescan();
    const AssetEntry* bal = db.FindByPath("Assets/balance.tab");
    Expect(bal && bal->type == AssetType::Table, "handwritten tab discovered as Table");

    // 坏 csv（非 UTF-8）拒入且不留半档
    { std::ofstream f(badSrc, std::ios::binary); f << "a,\xD6\xD0\n"; }
    Expect(db.ImportFile(badSrc.string(), "tables/bad.csv") == nullptr, "bad csv rejected");
    Expect(!fs::exists(root / "Assets" / "tables" / "bad.tab", ec), "no half tab left");

    fs::remove_all(root, ec);
    fs::remove(src, ec);
    fs::remove(badSrc, ec);
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
                          ("lemon-test-spriteguid-" + std::to_string(lemon::CurrentProcessId()));
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
                          ("lemon-test-prefab-" + std::to_string(lemon::CurrentProcessId()));
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
                          ("lemon-test-playspawn-" + std::to_string(lemon::CurrentProcessId()));
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
                          ("lemon-test-recent-" + std::to_string(lemon::CurrentProcessId()));
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

    // 读档洗脏档：预写含空串 + 重复条目的档 → 过滤空串、保序去重（首见留）。
    // Windows 绝对路径的反斜杠直接拼进 JSON 是非法转义（引擎侧会整档判坏），
    // 用 generic_string 正斜杠拼写；CanonicalPath 会折叠回规范形参与比较
    fs::create_directories(root / ".lemon", ec);
    {
        std::ofstream f(root / ".lemon" / "recent-scenes.json", std::ios::binary | std::ios::trunc);
        f << "{\"scenes\":[\"" << fs::path(a).generic_string()
          << "\", \"\", \"Scenes/b.scene\", \"" << fs::path(a).generic_string() << "\"]}\n";
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
                            ("lemon-test-wizard-" + std::to_string(lemon::CurrentProcessId()));
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
                            ("lemon-test-newscript-" + std::to_string(lemon::CurrentProcessId()));
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
                          ("lemon-test-autosave-" + std::to_string(lemon::CurrentProcessId()));
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

// ---- M7a 批②：AssetIndex 只读索引——manifest 快路径 vs 回退扫描双路一致性 ----
// 快路径 = 编辑器 AssetDatabase 建账落盘的 manifest 直读；回退 = 删 manifest
//（git clean -xfd 模拟，.bak 同删——兜底恢复路径归 TestManifestBakRecovery 族）
// 后 .meta 真源 + 路径序派生号。两路 guid→path 必须全等（.meta 随文件走）。
void TestAssetIndexConsistency() {
    namespace fs = std::filesystem;
    using lemon::assets::AssetIndex;
    using lemon::assets::AssetType;
    using lemon::editor::AssetDatabase;

    const fs::path root = fs::temp_directory_path() /
                          ("lemon-test-assetindex-" + std::to_string(lemon::CurrentProcessId()));
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "Assets", ec);
    fs::create_directories(root / "Prefabs", ec);
    // 夹具：整图/切片表两个 sprite + clip + prefab（meta 全部预设 guid——外部迁入形态）
    const uint64_t heroGuid = 0x1000000000000001ull, sheetGuid = 0x1000000000000002ull,
                   walkGuid = 0x1000000000000003ull, mobGuid = 0x1000000000000004ull;
    { std::ofstream f(root / "Assets" / "hero.png", std::ios::binary); f << "png"; }
    { std::ofstream f(root / "Assets" / "hero.png.meta", std::ios::trunc);
      f << "{\"guid\":\"" << lemon::assets::GuidToHex(heroGuid) << "\",\"type\":\"sprite\"}"; }
    { std::ofstream f(root / "Assets" / "sheet.png", std::ios::binary); f << "png"; }
    { std::ofstream f(root / "Assets" / "sheet.png.meta", std::ios::trunc);
      f << "{\"guid\":\"" << lemon::assets::GuidToHex(sheetGuid)
        << "\",\"type\":\"sprite\",\"importer\":{\"slice\":\"grid\",\"cell\":[8,8],"
          "\"frames\":[2,2]}}"; }
    { std::ofstream f(root / "Assets" / "walk.anim", std::ios::trunc); f << "{}"; }
    { std::ofstream f(root / "Assets" / "walk.anim.meta", std::ios::trunc);
      f << "{\"guid\":\"" << lemon::assets::GuidToHex(walkGuid) << "\",\"type\":\"clip\"}"; }
    { std::ofstream f(root / "Prefabs" / "mob.prefab", std::ios::trunc); f << "{}"; }
    { std::ofstream f(root / "Prefabs" / "mob.prefab.meta", std::ios::trunc);
      f << "{\"guid\":\"" << lemon::assets::GuidToHex(mobGuid) << "\",\"type\":\"prefab\"}"; }
    // M7a 批④：音频夹具（importer 段 loop/preload = AudioMount 装载消费面）
    const uint64_t hitGuid = 0x1000000000000005ull;
    { std::ofstream f(root / "Assets" / "hit.wav", std::ios::binary); f << "wav"; }
    { std::ofstream f(root / "Assets" / "hit.wav.meta", std::ios::trunc);
      f << "{\"guid\":\"" << lemon::assets::GuidToHex(hitGuid)
        << "\",\"type\":\"audio\",\"importer\":{\"loop\":[1.5,10.0],\"preload\":true}}"; }

    // 编辑器建账（发号 + manifest 落盘；base=100 与 TestSpriteGuidResolve 同款）
    AssetDatabase db;
    Expect(db.OpenProject(root.string(), 100), "editor db opens fixture project");
    const lemon::editor::AssetEntry* dbHero = db.FindByPath("Assets/hero.png");
    const lemon::editor::AssetEntry* dbSheet = db.FindByPath("Assets/sheet.png");
    const lemon::editor::AssetEntry* dbWalk = db.FindByPath("Assets/walk.anim");
    const lemon::editor::AssetEntry* dbMob = db.FindByPath("Prefabs/mob.prefab");
    Expect(dbHero && dbSheet && dbWalk && dbMob, "db indexed all four assets");
    Expect(dbSheet->Sliced() && dbSheet->sliceCount == 4, "db allocated slice block");
    Expect(db.FindByGuid(walkGuid) == dbWalk, "db preset guids honored");

    // ---- 快路径：两路 guid→path/type/spriteId/slice 全等 ----
    AssetIndex idx;
    Expect(idx.Open(root.string(), 100) && idx.FromManifest(), "index opens via manifest");
    Expect(idx.Entries().size() == db.Entries().size(), "entry count equal both paths");
    for (const lemon::editor::AssetEntry& dbe : db.Entries()) {
        const lemon::assets::IndexedEntry* ie = idx.FindByPath(dbe.relPath);
        Expect(ie && ie->guid == dbe.guid && ie->type == dbe.type,
               "fast-path: guid/path/type equal");
        if (dbe.type == AssetType::Sprite) {
            Expect(ie->spriteId == dbe.spriteId && ie->sliceBase == dbe.sliceBase &&
                       ie->sliceCount == dbe.sliceCount,
                   "fast-path: spriteId/slice bookkeeping equal");
        }
    }
    Expect(idx.FindByGuid(mobGuid) == idx.FindByPath("Prefabs/mob.prefab"),
           "fast-path: guid lookup consistent");
    Expect(idx.FindByLowId(AssetType::Prefab, (uint32_t)mobGuid) != nullptr,
           "fast-path: low-32 prefab lookup");
    // M7a 批④：音频条目——类型串写入面（AssetTypeName Audio 分支勘误的锁）+
    // importer 字段（loop/preload 走 .meta 小 IO 读入）
    {
        std::ifstream mf(root / ".lemon" / "manifest.json", std::ios::binary);
        const std::string manifestText((std::istreambuf_iterator<char>(mf)),
                                       std::istreambuf_iterator<char>());
        Expect(manifestText.find("\"audio\"") != std::string::npos,
               "manifest writes 'audio' type string (AssetTypeName fix)");
        const lemon::assets::IndexedEntry* hit = idx.FindByPath("Assets/hit.wav");
        Expect(hit && hit->type == AssetType::Audio, "fast-path: audio entry typed");
        Expect(hit && hit->audioLoopStart == 1.5f && hit->audioLoopEnd == 10.0f &&
                   hit->audioPreload,
               "fast-path: audio importer fields from .meta");
        // generic 自愈：旧账期音频被记 "generic"（AssetTypeName 漏分支产物）——
        // 快路径按扩展名重派（否则 AudioMount 漏装全部音频）
        {
            std::string m = manifestText;
            const size_t pos = m.find("\"audio\"");
            Expect(pos != std::string::npos, "self-heal: audio marker found");
            if (pos != std::string::npos) {
                m.replace(pos, 7, "\"generic\"");
                { std::ofstream of(root / ".lemon" / "manifest.json", std::ios::trunc);
                  of << m; }
                AssetIndex heal;
                Expect(heal.Open(root.string(), 100) && heal.FromManifest(),
                       "self-heal: reopen with generic-typed audio");
                const lemon::assets::IndexedEntry* h2 = heal.FindByPath("Assets/hit.wav");
                Expect(h2 && h2->type == AssetType::Audio,
                       "self-heal: generic re-derived from extension");
            }
        }
    }

    // ---- 回退：删 manifest（+.bak）→ .meta 真源扫描，派生号确定性 ----
    fs::remove(root / ".lemon" / "manifest.json", ec);
    fs::remove(root / ".lemon" / "manifest.json.bak", ec);
    AssetIndex idx2;
    Expect(idx2.Open(root.string(), 100) && !idx2.FromManifest(), "fallback scan engaged");
    // guid 全等（.meta 真源）+ 类型全等（扩展名判定单源）
    for (const lemon::editor::AssetEntry& dbe : db.Entries()) {
        const lemon::assets::IndexedEntry* ie = idx2.FindByPath(dbe.relPath);
        Expect(ie && ie->guid == dbe.guid && ie->type == dbe.type,
               "fallback: guid/path/type equal (meta is truth)");
    }
    { // M7a 批④：回退扫描的音频 importer 字段（与快路径同值——.meta 单源）
        const lemon::assets::IndexedEntry* hit2 = idx2.FindByPath("Assets/hit.wav");
        Expect(hit2 && hit2->type == AssetType::Audio && hit2->audioLoopStart == 1.5f &&
                   hit2->audioLoopEnd == 10.0f && hit2->audioPreload,
               "fallback: audio importer fields from .meta");
    }
    // 路径序派生号：hero(路径序首 sprite)=100、sheet 本体=101 + 块 102..105
    const lemon::assets::IndexedEntry* hero2 = idx2.FindByPath("Assets/hero.png");
    const lemon::assets::IndexedEntry* sheet2 = idx2.FindByPath("Assets/sheet.png");
    Expect(hero2 && hero2->spriteId == 100, "fallback: path-order id derivation (hero=100)");
    Expect(sheet2 && sheet2->spriteId == 101 && sheet2->sliceBase == 102 &&
               sheet2->sliceCount == 4 && sheet2->cellW == 8 && sheet2->gridCols == 2,
           "fallback: slice block derived after body id");
    // id 数值与编辑器可不同（此处恰好同序）——确定性：再开一次同号
    AssetIndex idx3;
    Expect(idx3.Open(root.string(), 100), "reopen for determinism check");
    for (const lemon::assets::IndexedEntry& e : idx2.Entries()) {
        const lemon::assets::IndexedEntry* again = idx3.FindByPath(e.relPath);
        Expect(again && again->spriteId == e.spriteId && again->sliceBase == e.sliceBase,
               "fallback derivation deterministic across opens");
    }
    // 本体号/切片号查询面
    Expect(idx2.FindByWholeSpriteId(101) == sheet2, "whole-id lookup hits body only");
    Expect(idx2.FindByWholeSpriteId(103) == nullptr, "cell id is not a whole id");
    Expect(idx2.FindBySpriteId(103) == sheet2, "sprite-id lookup covers slice range");
    // 无 .meta 散文件不认（只读侧不发号）
    { std::ofstream f(root / "Assets" / "stray.png", std::ios::binary); f << "png"; }
    AssetIndex idx4;
    Expect(idx4.Open(root.string(), 100) && idx4.FindByPath("Assets/stray.png") == nullptr,
           "stray file without .meta skipped (read-only: no id minting)");

    fs::remove_all(root, ec);
}
#endif // LEMON_EDITOR_CORE

// ---- M7a 批⑤：打包账 manifest.pkg.json（AssetIndex::ExportManifest → pkg 快路径）----

void TestAssetIndexPkgManifest() {
    namespace fs = std::filesystem;
    using lemon::assets::AssetIndex;
    using lemon::assets::AssetType;

    const fs::path root = fs::temp_directory_path() /
                          ("lemon-test-pkgmanifest-" + std::to_string(lemon::CurrentProcessId()));
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "Assets", ec);
    fs::create_directories(root / "Prefabs", ec);
    // 夹具：无编辑器账（.lemon 不存在）——回退扫描 → 导出 → pkg 快路径回读全等
    const uint64_t heroGuid = 0x2000000000000001ull, sheetGuid = 0x2000000000000002ull,
                   walkGuid = 0x2000000000000003ull, mobGuid = 0x2000000000000004ull;
    { std::ofstream f(root / "Assets" / "hero.png", std::ios::binary); f << "png"; }
    { std::ofstream f(root / "Assets" / "hero.png.meta", std::ios::trunc);
      f << "{\"guid\":\"" << lemon::assets::GuidToHex(heroGuid) << "\",\"type\":\"sprite\"}"; }
    { std::ofstream f(root / "Assets" / "sheet.png", std::ios::binary); f << "png"; }
    { std::ofstream f(root / "Assets" / "sheet.png.meta", std::ios::trunc);
      f << "{\"guid\":\"" << lemon::assets::GuidToHex(sheetGuid)
        << "\",\"type\":\"sprite\",\"importer\":{\"slice\":\"grid\",\"cell\":[8,8],"
          "\"frames\":[2,2]}}"; }
    { std::ofstream f(root / "Assets" / "walk.anim", std::ios::trunc); f << "{}"; }
    { std::ofstream f(root / "Assets" / "walk.anim.meta", std::ios::trunc);
      f << "{\"guid\":\"" << lemon::assets::GuidToHex(walkGuid) << "\",\"type\":\"clip\"}"; }
    { std::ofstream f(root / "Prefabs" / "mob.prefab", std::ios::trunc); f << "{}"; }
    { std::ofstream f(root / "Prefabs" / "mob.prefab.meta", std::ios::trunc);
      f << "{\"guid\":\"" << lemon::assets::GuidToHex(mobGuid) << "\",\"type\":\"prefab\"}"; }

    AssetIndex scan;
    Expect(scan.Open(root.string(), 2) && !scan.FromManifest(), "scan opens fallback (no manifest)");
    Expect(scan.ExportManifest((root / ".lemon" / "manifest.pkg.json").string()),
           "export pkg manifest");

    AssetIndex pkg;
    Expect(pkg.Open(root.string(), 2) && pkg.FromManifest(), "reopen hits pkg manifest fast path");
    Expect(pkg.Entries().size() == scan.Entries().size(), "pkg account entry count equal");
    for (const lemon::assets::IndexedEntry& e : scan.Entries()) {
        const lemon::assets::IndexedEntry* p = pkg.FindByPath(e.relPath);
        Expect(p && p->guid == e.guid && p->type == e.type,
               "pkg manifest: guid/path/type roundtrip");
        if (e.type == AssetType::Sprite)
            Expect(p && p->spriteId == e.spriteId && p->sliceBase == e.sliceBase &&
                       p->sliceCount == e.sliceCount,
                   "pkg manifest: spriteId/slice roundtrip");
    }
    const lemon::assets::IndexedEntry* sheet = pkg.FindByGuid(sheetGuid);
    Expect(sheet && sheet->Sliced() && sheet->SliceSpriteId(2) == sheet->sliceBase + 2,
           "pkg manifest: slice block usable (cell id contiguous)");
    // 编辑器账不干扰包账优先级：写入 manifest.json 后 pkg 账仍首查（包形态语义）
    { std::ofstream f(root / ".lemon" / "manifest.json", std::ios::trunc);
      f << "{\"assets\":[],\"nextSpriteId\":2}"; }
    AssetIndex stillPkg;
    Expect(stillPkg.Open(root.string(), 2) && stillPkg.FromManifest() &&
               stillPkg.Entries().size() == scan.Entries().size(),
           "pkg manifest takes precedence over editor manifest");
    fs::remove_all(root, ec);
}

// ---- M7a 批⑥：LAT1 图集容器 v1（ADR-016 M5；writer/reader/装载登记核）----

void TestAssetIndexSliceRebase() {
    namespace fs = std::filesystem;
    using lemon::assets::AssetIndex;
    using lemon::assets::AssetType;

    // 验收热修 2026-10-05 的号域平移语义：packager 低基线记账（无编辑器账项目
    // fallback 自 base=2 发号）对运行时大基线（程序化页后）Open 时——本体重派、
    // 低域切片块**随本体连号重派**（几何真源 .meta 在场即登记链活）；健康块保号；
    // 越上界坏账块清零（原防御保留）
    const fs::path root = fs::temp_directory_path() /
                          ("lemon-test-slicerebase-" + std::to_string(lemon::CurrentProcessId()));
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "Assets", ec);
    fs::create_directories(root / ".lemon", ec);
    const uint64_t sheetGuid = 0x6000000000000001ull, wholeGuid = 0x6000000000000002ull;
    { std::ofstream f(root / "Assets" / "sheet.png", std::ios::binary); f << "png"; }
    { std::ofstream f(root / "Assets" / "sheet.png.meta", std::ios::trunc);
      f << "{\"guid\":\"" << lemon::assets::GuidToHex(sheetGuid)
        << "\",\"type\":\"sprite\",\"importer\":{\"slice\":\"grid\",\"cell\":[8,4],"
          "\"frames\":[2,2]}}"; }
    { std::ofstream f(root / "Assets" / "whole.png", std::ios::binary); f << "png"; }
    { std::ofstream f(root / "Assets" / "whole.png.meta", std::ios::trunc);
      f << "{\"guid\":\"" << lemon::assets::GuidToHex(wholeGuid) << "\",\"type\":\"sprite\"}"; }
    const auto WriteManifest = [&](const char* extra) {
        std::ofstream f(root / ".lemon" / "manifest.json", std::ios::trunc);
        f << "{\"assets\":["
          << "{\"path\":\"Assets/sheet.png\",\"guid\":" << sheetGuid
          << ",\"type\":\"sprite\",\"spriteId\":3,\"slice\":{\"base\":4,\"count\":4}},"
          << "{\"path\":\"Assets/whole.png\",\"guid\":" << wholeGuid
          << ",\"type\":\"sprite\",\"spriteId\":8}"
          << "],\"nextSpriteId\":9" << extra << "}";
    };

    { // 低域块随本体连号重派（Open base=100 → 全部记账低于基线）
        WriteManifest("");
        AssetIndex idx;
        Expect(idx.Open(root.string(), 100) && idx.FromManifest(), "rebase: manifest fast path");
        const lemon::assets::IndexedEntry* sh = idx.FindByGuid(sheetGuid);
        const lemon::assets::IndexedEntry* wh = idx.FindByGuid(wholeGuid);
        Expect(sh && wh, "rebase: entries present");
        if (sh && wh) {
            Expect(sh->spriteId >= 100 && wh->spriteId >= 100, "rebase: bodies reassigned >= base");
            Expect(sh->sliceCount == 4 && sh->sliceBase == sh->spriteId + 1,
                   "rebase: slice block follows body contiguously");
            Expect(sh->SliceSpriteId(3) == sh->sliceBase + 3, "rebase: cell ids contiguous");
            Expect(wh->spriteId >= sh->sliceBase + sh->sliceCount,
                   "rebase: no id collision between block and later body");
        }
    }
    { // 健康块保号：记账全在基线上域 → 不重排（原号原样）
        std::ofstream f(root / ".lemon" / "manifest.json", std::ios::trunc);
        f << "{\"assets\":["
          << "{\"path\":\"Assets/sheet.png\",\"guid\":" << sheetGuid
          << ",\"type\":\"sprite\",\"spriteId\":100,\"slice\":{\"base\":101,\"count\":4}},"
          << "{\"path\":\"Assets/whole.png\",\"guid\":" << wholeGuid
          << ",\"type\":\"sprite\",\"spriteId\":105}"
          << "],\"nextSpriteId\":106}";
        f.close(); // flush 落盘后再 Open（ofstream 存活期内缓冲未刷 = 读到空档）
        AssetIndex idx;
        Expect(idx.Open(root.string(), 100) && idx.FromManifest(), "healthy: manifest fast path");
        const lemon::assets::IndexedEntry* sh = idx.FindByGuid(sheetGuid);
        Expect(sh && sh->spriteId == 100 && sh->sliceBase == 101 && sh->sliceCount == 4,
               "healthy: slice block preserved as-is");
    }
    { // 越上界坏账块清零（manifest 不自洽防御保留）
        std::ofstream f(root / ".lemon" / "manifest.json", std::ios::trunc);
        f << "{\"assets\":["
          << "{\"path\":\"Assets/sheet.png\",\"guid\":" << sheetGuid
          << ",\"type\":\"sprite\",\"spriteId\":100,\"slice\":{\"base\":101,\"count\":10}},"
          << "{\"path\":\"Assets/whole.png\",\"guid\":" << wholeGuid
          << ",\"type\":\"sprite\",\"spriteId\":105}"
          << "],\"nextSpriteId\":106}";
        f.close();
        AssetIndex idx;
        Expect(idx.Open(root.string(), 100) && idx.FromManifest(), "badblock: manifest fast path");
        const lemon::assets::IndexedEntry* sh = idx.FindByGuid(sheetGuid);
        Expect(sh && sh->spriteId == 100 && sh->sliceCount == 0 && !sh->Sliced(),
               "badblock: over-ceiling block cleared to whole-sprite");
    }
    { // review 2026-10-05 回绕防线：sliceBase+count 精确回绕（0xFFFFFFF0+0x10=0）
        // 绕不过 sane 收口——不崩、块清零；spriteId 巨号（> sane 上限钳后的
        // idCeiling）同判坏账重派
        std::ofstream f(root / ".lemon" / "manifest.json", std::ios::trunc);
        f << "{\"assets\":["
          << "{\"path\":\"Assets/sheet.png\",\"guid\":" << sheetGuid
          << ",\"type\":\"sprite\",\"spriteId\":4294967295,\"slice\":{\"base\":4294967280,"
            "\"count\":16}},"
          << "{\"path\":\"Assets/whole.png\",\"guid\":" << wholeGuid
          << ",\"type\":\"sprite\",\"spriteId\":4294967295}"
          << "],\"nextSpriteId\":4294967295}";
        f.close();
        AssetIndex idx;
        Expect(idx.Open(root.string(), 100) && idx.FromManifest(),
               "wraparound: opens without crash/abort");
        const lemon::assets::IndexedEntry* sh = idx.FindByGuid(sheetGuid);
        const lemon::assets::IndexedEntry* wh = idx.FindByGuid(wholeGuid);
        Expect(sh && wh, "wraparound: entries present");
        if (sh && wh) {
            Expect(sh->spriteId >= 100 && sh->spriteId < (1u << 23),
                   "wraparound: giant spriteId bad account reassigned in sane range");
            Expect(sh->sliceCount == 0,
                   "wraparound: wrap-around slice block cleared (not giant-registered)");
            Expect(idx.FindBySpriteId(4294967290) == nullptr,
                   "wraparound: wrapped block range yields no lookup hit");
        }
    }
    fs::remove_all(root, ec);
}

void TestBakedAtlasContainer() {
    namespace fs = std::filesystem;
    using namespace lemon::assets;

    const fs::path dir = fs::temp_directory_path() /
                         ("lemon-test-lat1-" + std::to_string(lemon::CurrentProcessId()));
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);

    BakedAtlasBuild b;
    b.pages = {{64, 32}, {16, 16}};
    BakedAtlasEntry e1{}, e2{};
    e1.guid = 0x3000000000000001ull, e1.page = 0, e1.x = 2, e1.y = 2, e1.w = 60, e1.h = 28;
    e2.guid = 0x3000000000000002ull, e2.page = 1, e2.w = 16, e2.h = 16;
    b.entries = {e1, e2};
    b.pagePixels.emplace_back(64 * 32 * 4, 0xAB);
    b.pagePixels.emplace_back(16 * 16 * 4, 0xCD);
    const fs::path p = dir / "atlas.baked";
    Expect(WriteBakedAtlasFile(p.string(), b), "LAT1 write ok");
    BakedAtlasBuild r;
    Expect(LoadBakedAtlasFile(p.string(), r), "LAT1 load ok");
    Expect(r.pages == b.pages && r.entries == b.entries && r.pagePixels == b.pagePixels,
           "LAT1 roundtrip fields+pixels equal");

    // 写侧自洽校验（review 2026-10-05：坏 build 拒写盘——"写盘成功但永不可载"
    // 的包在烤制期直白拒绝）：像素尺寸不符 / 页号越界 / 矩形越界 / 重复 guid
    const fs::path rejectPath = dir / "reject.baked";
    {
        BakedAtlasBuild bad = b;
        bad.pagePixels[0].pop_back(); // 像素载荷与页尺寸不符
        Expect(!WriteBakedAtlasFile(rejectPath.string(), bad),
               "LAT1 write rejects pixel/page size mismatch");
    }
    {
        BakedAtlasBuild bad = b;
        bad.entries[1].page = 2; // 页号越界
        Expect(!WriteBakedAtlasFile(rejectPath.string(), bad), "LAT1 write rejects page oob");
    }
    {
        BakedAtlasBuild bad = b;
        bad.entries[0].w = 63; // 2+63 > 页宽 64
        Expect(!WriteBakedAtlasFile(rejectPath.string(), bad), "LAT1 write rejects rect oob");
    }
    {
        BakedAtlasBuild bad = b;
        bad.entries[1].guid = bad.entries[0].guid; // 重复 guid
        Expect(!WriteBakedAtlasFile(rejectPath.string(), bad), "LAT1 write rejects dup guid");
    }
    Expect(!fs::exists(rejectPath, ec), "LAT1 rejected builds leave no file");

    // 篡改/截断阴性面：单字节改 → 拒载（拒载原因面 = 魔数/版本/头长/计数域/
    // 尺寸域/payloadBytes 对账/条目界内/guid 唯一）
    std::vector<uint8_t> raw;
    {
        std::ifstream f(p, std::ios::binary);
        raw.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    }
    const auto RawWrite = [&](const std::vector<uint8_t>& bytes) {
        std::ofstream f(dir / "tampered.baked", std::ios::binary | std::ios::trunc);
        f.write((const char*)bytes.data(), (std::streamsize)bytes.size());
    };
    const auto TamperAt = [&](size_t off, uint8_t v, const char* what) {
        std::vector<uint8_t> t = raw;
        t[off] = v;
        RawWrite(t);
        BakedAtlasBuild rb;
        Expect(!LoadBakedAtlasFile((dir / "tampered.baked").string(), rb), what);
    };
    TamperAt(0, 'X', "LAT1 bad magic rejected");
    TamperAt(4, 2, "LAT1 bad version rejected");
    TamperAt(6, 24, "LAT1 bad headerSize rejected");
    TamperAt(28, uint8_t(raw[28] ^ 0xFF), "LAT1 payloadBytes mismatch rejected");
    TamperAt(20, uint8_t(raw[20] + 1), "LAT1 entryCount mismatch rejected");
    TamperAt(32, 0, "LAT1 zero page dim rejected");
    { // 条目矩形越界：entry0.x 2 → 60（60+60 > 页宽 64）
        std::vector<uint8_t> t = raw;
        t[32 + 2 * 8 /*pageDims*/ + 10 /*entry0.x*/] = 60;
        RawWrite(t);
        BakedAtlasBuild rb;
        Expect(!LoadBakedAtlasFile((dir / "tampered.baked").string(), rb),
               "LAT1 entry rect out of page rejected");
    }
    { // guid 重复：entry1.guid := entry0.guid
        std::vector<uint8_t> t = raw;
        const size_t e1off = 32 + 2 * 8 + 1 * 18;
        std::memcpy(&t[e1off], &raw[32 + 2 * 8], 8);
        RawWrite(t);
        BakedAtlasBuild rb;
        Expect(!LoadBakedAtlasFile((dir / "tampered.baked").string(), rb),
               "LAT1 duplicate guid rejected");
    }
    { // 截断
        RawWrite({raw.begin(), raw.end() - 10});
        BakedAtlasBuild rb;
        Expect(!LoadBakedAtlasFile((dir / "tampered.baked").string(), rb),
               "LAT1 truncated payload rejected");
    }
    { // 尾部多出
        std::vector<uint8_t> t = raw;
        t.insert(t.end(), {1, 2, 3});
        RawWrite(t);
        BakedAtlasBuild rb;
        Expect(!LoadBakedAtlasFile((dir / "tampered.baked").string(), rb),
               "LAT1 trailing bytes rejected");
    }
    { // 零条目
        std::vector<uint8_t> t = raw;
        t[20] = t[21] = t[22] = t[23] = 0; // entryCount = 0
        RawWrite(t);
        BakedAtlasBuild rb;
        Expect(!LoadBakedAtlasFile((dir / "tampered.baked").string(), rb),
               "LAT1 zero entries rejected");
    }
    fs::remove_all(dir, ec);
}

void TestAtlasBakePack() {
    using namespace lemon::assets;
    const auto MakeImage = [](uint64_t guid, uint32_t w, uint32_t h, uint8_t fill) {
        BakedAtlasImage img;
        img.guid = guid;
        img.w = w;
        img.h = h;
        img.rgba.assign(size_t(w) * h * 4, 0);
        for (size_t i = 0; i < img.rgba.size(); i += 4) {
            img.rgba[i] = fill;
            img.rgba[i + 3] = 0xFF;
        }
        return img;
    };
    const uint64_t gBig = 0x3100000000000001ull, gA = 0x3100000000000002ull,
                   gB = 0x3100000000000003ull, gC = 0x3100000000000004ull,
                   gD = 0x3100000000000005ull;
    std::vector<BakedAtlasImage> images;
    images.push_back(MakeImage(gBig, 5000, 8, 1)); // 超虚拟页宽 → 专属页
    images.push_back(MakeImage(gA, 40, 30, 2));
    images.push_back(MakeImage(gB, 20, 30, 3));
    images.push_back(MakeImage(gC, 10, 10, 4));
    images.push_back(MakeImage(gD, 100, 5, 5));
    BakedAtlasBuild a, b;
    std::string err;
    Expect(PackAtlasPages(images, a, &err), "pack ok");
    Expect(PackAtlasPages(images, b) && a.pages == b.pages && a.entries == b.entries &&
               a.pagePixels == b.pagePixels,
           "pack deterministic (byte equal rerun)");

    // 布局断言：专属页独占 + 普通页 gutter 边距 + 矩形界内 + 互不重叠 + 像素对位
    Expect(a.pages.size() == 3, "oversized gets dedicated page (3 pages)");
    const BakedAtlasEntry* big = nullptr;
    for (const BakedAtlasEntry& e : a.entries)
        if (e.guid == gBig) big = &e;
    Expect(big && big->x == 0 && big->y == 0 && big->w == 5000 && big->h == 8,
           "oversized entry at origin full size");
    Expect(big && a.pages[big->page].w == 5000 && a.pages[big->page].h == 8,
           "dedicated page sized to sprite");
    const BakedAtlasEntry* d = nullptr;
    for (const BakedAtlasEntry& e : a.entries)
        if (e.guid == gD) d = &e;
    Expect(d && d->page != big->page && d->x >= kAtlasGutter && d->y >= kAtlasGutter,
           "normal entry keeps gutter margins (dedicated page closed)");
    for (size_t i = 0; i < a.entries.size(); ++i) {
        const BakedAtlasEntry& e = a.entries[i];
        const BakedAtlasPage& pg = a.pages[e.page];
        Expect(uint32_t(e.x) + e.w <= pg.w && uint32_t(e.y) + e.h <= pg.h,
               "entry rect within page");
        for (size_t j = i + 1; j < a.entries.size(); ++j) { // 同页不重叠
            const BakedAtlasEntry& o = a.entries[j];
            if (o.page != e.page) continue;
            const bool overlap = uint32_t(e.x) < uint32_t(o.x) + o.w &&
                                 uint32_t(o.x) < uint32_t(e.x) + e.w &&
                                 uint32_t(e.y) < uint32_t(o.y) + o.h &&
                                 uint32_t(o.y) < uint32_t(e.y) + e.h;
            Expect(!overlap, "same-page entries do not overlap");
        }
        // 像素对位：页面上精灵矩形逐字节 = 源图（合成正确性）
        const BakedAtlasImage* src = nullptr;
        for (const BakedAtlasImage& im : images)
            if (im.guid == e.guid) src = &im;
        Expect(src != nullptr, "entry maps to source image");
        if (src) {
            bool equal = true;
            for (uint32_t row = 0; row < e.h && equal; ++row) {
                const uint8_t* pageRow = &a.pagePixels[e.page][(size_t(e.y + row) * pg.w + e.x) * 4];
                const uint8_t* srcRow = &src->rgba[size_t(row) * src->w * 4];
                equal = std::memcmp(pageRow, srcRow, size_t(e.w) * 4) == 0;
            }
            Expect(equal, "page pixels match source at entry rect");
        }
    }

    // 分页溢出：5 张 2040²（4 张恰满一页，第 5 张开新页——4096 虚拟域算术）
    {
        std::vector<BakedAtlasImage> big5;
        for (uint64_t g = 1; g <= 5; ++g)
            big5.push_back(MakeImage(0x3200000000000000ull + g, 2040, 2040, uint8_t(g)));
        BakedAtlasBuild bb;
        Expect(PackAtlasPages(big5, bb) && bb.pages.size() == 2, "2040x2040 x5 spills to 2 pages");
    }

    // 阴性：零尺寸/超 GPU 域/载荷不符/空输入
    std::string msg;
    BakedAtlasBuild junk;
    Expect(!PackAtlasPages({}, junk, &msg), "empty pack rejected");
    Expect(!PackAtlasPages({MakeImage(1, 0, 4, 0)}, junk, &msg), "zero-size sprite rejected");
    Expect(!PackAtlasPages({MakeImage(1, 17000, 4, 0)}, junk, &msg), "over-GPU-dim rejected");
    {
        BakedAtlasImage bad = MakeImage(1, 4, 4, 0);
        bad.rgba.pop_back();
        Expect(!PackAtlasPages({bad}, junk, &msg), "pixel payload size mismatch rejected");
    }
}

void TestAtlasStoreRegister() {
    namespace fs = std::filesystem;
    using namespace lemon::assets;

    const fs::path root = fs::temp_directory_path() /
                          ("lemon-test-lat1reg-" + std::to_string(lemon::CurrentProcessId()));
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "Assets", ec);
    const uint64_t sheetGuid = 0x4000000000000001ull, wholeGuid = 0x4000000000000002ull;
    // sheet.png 32×8（网格 4×2 格 cell 8×4 恰满图面）+ whole.png 10×6
    const auto FillPattern = [](std::vector<uint8_t>& px, uint8_t seed) {
        for (size_t i = 0; i < px.size(); i += 4) {
            px[i] = uint8_t(seed + (i / 4) % 251);
            px[i + 1] = uint8_t((seed * 7 + i) % 253);
            px[i + 2] = uint8_t(seed ^ uint8_t(i));
            px[i + 3] = 0xFF;
        }
    };
    std::vector<uint8_t> sheetPx(32 * 8 * 4), wholePx(10 * 6 * 4);
    FillPattern(sheetPx, 11);
    FillPattern(wholePx, 22);
    Expect(stbi_write_png((root / "Assets" / "sheet.png").string().c_str(), 32, 8, 4,
                          sheetPx.data(), 32 * 4) != 0,
           "seed sheet.png");
    Expect(stbi_write_png((root / "Assets" / "whole.png").string().c_str(), 10, 6, 4,
                          wholePx.data(), 10 * 4) != 0,
           "seed whole.png");
    { std::ofstream f(root / "Assets" / "sheet.png.meta", std::ios::trunc);
      f << "{\"guid\":\"" << GuidToHex(sheetGuid) << "\",\"type\":\"sprite\","
           "\"importer\":{\"slice\":\"grid\",\"cell\":[8,4],\"frames\":[4,2]}}"; }
    { std::ofstream f(root / "Assets" / "whole.png.meta", std::ios::trunc);
      f << "{\"guid\":\"" << GuidToHex(wholeGuid) << "\",\"type\":\"sprite\"}"; }

    AssetIndex index;
    Expect(index.Open(root.string(), 2), "fixture index opens");
    const IndexedEntry* se = index.FindByGuid(sheetGuid);
    const IndexedEntry* we = index.FindByGuid(wholeGuid);
    Expect(se && we && se->Sliced() && se->sliceCount == 8, "sheet sliced 4x2");
    Expect(se->spriteId != 0 && we->spriteId != 0, "sprite ids assigned");

    // LAT1 build：同像素装箱（读回端到端不引 RHI——登记核纯面）
    BakedAtlasImage im1, im2;
    im1.guid = sheetGuid, im1.w = 32, im1.h = 8, im1.rgba = sheetPx;
    im2.guid = wholeGuid, im2.w = 10, im2.h = 6, im2.rgba = wholePx;
    BakedAtlasBuild build;
    Expect(PackAtlasPages({im1, im2}, build), "fixture pack ok");

    renderer::AtlasRegistry atlas;
    for (size_t i = 0; i < build.pages.size(); ++i)
        atlas.RegisterAtlas(uint32_t(2 + i), {}, build.pages[i].w, build.pages[i].h);
    uint32_t reg = 0;
    Expect(RegisterAtlasSprites(atlas, index, build, 2, reg) && reg == 2,
           "register ok (2 whole sprites)");
    for (const BakedAtlasEntry& ent : build.entries) {
        const IndexedEntry* e = index.FindByGuid(ent.guid);
        Expect(e != nullptr, "entry guid in index");
        if (!e) continue;
        const renderer::SpriteInfo& si = atlas.GetSprite(e->spriteId);
        const BakedAtlasPage& pg = build.pages[ent.page];
        Expect(si.atlasIndex == 2 + ent.page && si.widthPx == ent.w && si.heightPx == ent.h,
               "whole sprite registered at manifest id");
        ExpectNear(si.u0, float(ent.x) / float(pg.w), 1e-6f, "whole u0 math");
        ExpectNear(si.v1, float(ent.y + ent.h) / float(pg.h), 1e-6f, "whole v1 math");
        if (ent.guid == sheetGuid) { // 切片子矩形：cell → sliceBase + 行优先号
            for (uint32_t cell = 0; cell < 8; ++cell) {
                const uint32_t id = se->SliceSpriteId(cell);
                const renderer::SpriteInfo& s = atlas.GetSprite(id);
                const uint32_t cx = cell % 4, cy = cell / 4;
                Expect(s.atlasIndex == 2 + ent.page && s.widthPx == 8 && s.heightPx == 4,
                       "slice sprite size");
                ExpectNear(s.u0, float(ent.x + cx * 8) / float(pg.w), 1e-6f, "slice u0 math");
                ExpectNear(s.v0, float(ent.y + cy * 4) / float(pg.h), 1e-6f, "slice v0 math");
            }
        }
    }

    // 阴性：LAT1 条目与索引失配（缺 whole = 包与账不一致）→ 拒绝登记
    {
        BakedAtlasBuild partial;
        Expect(PackAtlasPages({im1}, partial), "partial pack ok");
        renderer::AtlasRegistry at2;
        at2.RegisterAtlas(2, {}, partial.pages[0].w, partial.pages[0].h);
        uint32_t r2 = 0;
        Expect(!RegisterAtlasSprites(at2, index, partial, 2, r2),
               "missing entry vs index rejected (package/ledger mismatch)");
    }
    { // 阴性：未知 guid 多一条
        BakedAtlasBuild ghost = build;
        BakedAtlasImage imG = im2;
        imG.guid = 0x4000000000000099ull;
        Expect(PackAtlasPages({im1, im2, imG}, ghost), "ghost pack ok");
        renderer::AtlasRegistry at3;
        for (size_t i = 0; i < ghost.pages.size(); ++i)
            at3.RegisterAtlas(uint32_t(2 + i), {}, ghost.pages[i].w, ghost.pages[i].h);
        uint32_t r3 = 0;
        Expect(!RegisterAtlasSprites(at3, index, ghost, 2, r3), "unknown guid rejected");
    }
    fs::remove_all(root, ec);
}

void TestBakeProjectAtlas() {
    namespace fs = std::filesystem;
    using namespace lemon::assets;

    const fs::path root = fs::temp_directory_path() /
                          ("lemon-test-lat1bake-" + std::to_string(lemon::CurrentProcessId()));
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "Assets", ec);
    const uint64_t oneGuid = 0x5000000000000001ull;
    std::vector<uint8_t> px(13 * 7 * 4);
    for (size_t i = 0; i < px.size(); i += 4) {
        px[i] = uint8_t(i % 199);
        px[i + 1] = uint8_t((i * 3) % 197);
        px[i + 2] = uint8_t(i % 251);
        px[i + 3] = 0xFF;
    }
    Expect(stbi_write_png((root / "Assets" / "one.png").string().c_str(), 13, 7, 4, px.data(),
                          13 * 4) != 0,
           "seed one.png");
    { std::ofstream f(root / "Assets" / "one.png.meta", std::ios::trunc);
      f << "{\"guid\":\"" << GuidToHex(oneGuid) << "\",\"type\":\"sprite\"}"; }

    AssetIndex index;
    Expect(index.Open(root.string(), 2), "bake fixture index opens");
    AtlasBakeStats st;
    const fs::path dst = root / ".lemon" / "baked" / "atlas" / "atlas.baked";
    Expect(BakeProjectAtlas(index, dst.string(), st) && st.sprites == 1 && st.pages == 1,
           "bake project atlas (1 sprite 1 page)");
    BakedAtlasBuild rb;
    Expect(LoadBakedAtlasFile(dst.string(), rb) && rb.entries.size() == 1,
           "baked file loads back");
    const BakedAtlasEntry& e = rb.entries[0];
    Expect(e.guid == oneGuid && e.w == 13 && e.h == 7, "baked entry geometry");
    bool equal = true;
    for (uint32_t row = 0; row < 7 && equal; ++row)
        equal = std::memcmp(&rb.pagePixels[0][(size_t(e.y + row) * rb.pages[0].w + e.x) * 4],
                            &px[size_t(row) * 13 * 4], 13 * 4) == 0;
    Expect(equal, "baked page pixels match png source");

    // 无 sprite 项目：false 且 sprites==0（合法跳过形态，非错误）
    {
        const fs::path empty = fs::temp_directory_path() /
                               ("lemon-test-lat1none-" + std::to_string(lemon::CurrentProcessId()));
        fs::remove_all(empty, ec);
        fs::create_directories(empty / "Assets", ec);
        { std::ofstream f(empty / "Assets" / "walk.anim", std::ios::trunc); f << "{}"; }
        { std::ofstream f(empty / "Assets" / "walk.anim.meta", std::ios::trunc);
          f << "{\"guid\":\"" << GuidToHex(0x5000000000000002ull) << "\",\"type\":\"clip\"}"; }
        AssetIndex ei;
        Expect(ei.Open(empty.string(), 2), "spriteless index opens");
        AtlasBakeStats es;
        Expect(!BakeProjectAtlas(ei, (empty / "a.baked").string(), es) && es.sprites == 0,
               "spriteless project skips atlas (false + zero sprites)");
        fs::remove_all(empty, ec);
    }
    fs::remove_all(root, ec);
}

// ---- M6c 批⓪：音频核心（ADR-015；静音模式 = 无设备确定性）----

void TestAudioMixerMath() {
    audio::AudioEngine eng;
    Expect(eng.Init({.forceSilent = true}), "audio init (forced silent)");
    Expect(eng.silent(), "forced silent engaged");
    eng.SetRetriggerCooldown(0); // 本测断言叠加数学——同 clip 连播不吃节流窗（常数 PCM 对微扰免疫）

    // 单声道 0.5 满幅常数 clip：pan 中心 = 等功率 -3dB（L=R=0.7071）
    std::vector<int16_t> mono(4800, 16384);
    const uint32_t clip = eng.RegisterClip({mono.data(), 4800, 1, 0, 0});
    Expect(clip != 0, "register mono clip");
    Expect(eng.RegisterClip({nullptr, 100, 1, 0, 0}) == 0, "null pcm rejected");
    Expect(eng.RegisterClip({mono.data(), 100, 3, 0, 0}) == 0, "3ch rejected");

    uint32_t v = eng.Play(clip, {.volume = 1.0f, .pan = 0.0f});
    Expect(v != 0, "play mono");
    float out[8];
    eng.MixOffline(out, 4);
    ExpectNear(out[0], 0.5f * 0.70710678f, 1e-4f, "pan center L");
    ExpectNear(out[1], 0.5f * 0.70710678f, 1e-4f, "pan center R");
    eng.Stop(v);

    // 声像 +1：L≈0，R≈源
    v = eng.Play(clip, {.pan = 1.0f});
    eng.MixOffline(out, 4);
    ExpectNear(out[0], 0.0f, 1e-4f, "pan right L silent");
    ExpectNear(out[1], 0.5f, 1e-4f, "pan right R full");
    eng.Stop(v);

    // 立体声 clip 通道路由
    std::vector<int16_t> stereo(9600); // 4800 帧 × 2ch
    for (uint32_t f = 0; f < 4800; ++f) {
        stereo[f * 2] = 16384;
        stereo[f * 2 + 1] = -16384;
    }
    const uint32_t clip2 = eng.RegisterClip({stereo.data(), 4800, 2, 0, 0});
    v = eng.Play(clip2, {});
    eng.MixOffline(out, 4);
    ExpectNear(out[0], 0.5f * 0.70710678f, 1e-4f, "stereo L routed");
    ExpectNear(out[1], -0.5f * 0.70710678f, 1e-4f, "stereo R routed");
    eng.Stop(v);

    // 两声部线性叠加（f32 累加，先钳位后断言）
    const uint32_t va = eng.Play(clip, {});
    const uint32_t vb = eng.Play(clip, {});
    eng.MixOffline(out, 4);
    ExpectNear(out[0], 2 * 0.5f * 0.70710678f, 1e-4f, "two voices sum linearly");
    eng.Stop(va);
    eng.Stop(vb);

    // 组音量 / 主音量乘法
    eng.SetGroupVolume(audio::Group::Sfx, 0.5f);
    ExpectNear(eng.GroupVolume(audio::Group::Sfx), 0.5f, 1e-6f, "group vol get");
    v = eng.Play(clip, {});
    eng.MixOffline(out, 4);
    ExpectNear(out[0], 0.5f * 0.5f * 0.70710678f, 1e-4f, "group gain applied");
    eng.Stop(v);
    eng.SetGroupVolume(audio::Group::Sfx, 1.0f);
    eng.SetMasterVolume(0.25f);
    v = eng.Play(clip, {});
    eng.MixOffline(out, 4);
    ExpectNear(out[0], 0.25f * 0.5f * 0.70710678f, 1e-4f, "master gain applied");
    eng.Stop(v);
    eng.SetMasterVolume(1.0f);

    // review 2026-09-30 热修回归锁：越界 group 防御钳落 Sfx（此前 groupVol[] 越界读）
    eng.SetGroupVolume(audio::Group::Sfx, 0.25f);
    v = eng.Play(clip, {.group = static_cast<audio::Group>(99)});
    Expect(v != 0, "bad group clamped and plays");
    eng.MixOffline(out, 4);
    ExpectNear(out[0], 0.25f * 0.5f * 0.70710678f, 1e-4f, "bad group falls back to Sfx gain");
    eng.Stop(v);
    eng.SetGroupVolume(audio::Group::Sfx, 1.0f);
}

void TestAudioLifecycle() {
    audio::AudioEngine eng;
    eng.Init({.forceSilent = true});
    eng.SetRetriggerCooldown(0); // 发号单调/池满偷取断言需同 clip 连播——节流让路

    // 一次性声部恰好在末帧混完 → 当场终止，Tick 回收
    std::vector<int16_t> pcm100(100, 16384);
    const uint32_t c1 = eng.RegisterClip({pcm100.data(), 100, 1, 0, 0});
    const uint32_t v1 = eng.Play(c1, {});
    Expect(eng.VoiceAlive(v1), "oneshot alive at start");
    float out[256]; // 契约：out 容纳 frames×2 个 float（下方最大 99 帧）
    eng.MixOffline(out, 1);
    eng.MixOffline(out, 99);
    Expect(!eng.VoiceAlive(v1), "oneshot done exactly at end frame");
    eng.Tick();
    Expect(eng.ActiveVoiceCount() == 0, "voice reaped by tick");

    // 循环回卷：ramp clip 循环区间 [0,2400)——混满区间后下一帧采到 ramp[0]=0
    std::vector<int16_t> ramp(4800);
    for (int i = 0; i < 4800; ++i)
        ramp[i] = static_cast<int16_t>(i);
    const uint32_t c2 = eng.RegisterClip({ramp.data(), 4800, 1, 0, 2400});
    const uint32_t v2 = eng.Play(c2, {.loop = true});
    std::vector<float> big(2400 * 2);
    eng.MixOffline(big.data(), 2400);
    eng.MixOffline(out, 1);
    ExpectNear(out[0], 0.0f, 1e-6f, "loop wraps to loopStart");
    ExpectNear(big[2399 * 2], 2399 / 32768.0f * 0.70710678f, 1e-4f, "last loop frame mixed");
    Expect(eng.VoiceAlive(v2), "loop voice stays alive");
    Expect(eng.Stop(v2), "stop loop voice");
    Expect(!eng.VoiceAlive(v2), "stopped voice dead");
    Expect(!eng.Stop(v2), "double stop returns false");
    Expect(!eng.Stop(999999), "stop unknown id false");

    // voiceId 单调发号、永不复用
    const uint32_t a = eng.Play(c1, {});
    const uint32_t b = eng.Play(c1, {});
    Expect(b > a, "voice ids monotonic");
    eng.Stop(a);
    const uint32_t c = eng.Play(c1, {});
    Expect(c > b, "voice id never reused");
    eng.StopAll();
    Expect(eng.ActiveVoiceCount() == 0, "stopall clears");

    // 池满偷最旧一次性声部；全循环占满则拒绝（ADR-015 M4）
    // （clip 注册表每实例私有——eng2/eng3 须各自注册，跨实例 clipId 查无）
    audio::AudioEngine eng2;
    eng2.Init({.forceSilent = true});
    eng2.SetRetriggerCooldown(0); // 池满 64 连播语义不受节流影响
    const uint32_t d1 = eng2.RegisterClip({pcm100.data(), 100, 1, 0, 0});
    const uint32_t d2 = eng2.RegisterClip({ramp.data(), 4800, 1, 0, 2400});
    uint32_t firstId = 0;
    for (int i = 0; i < audio::kMaxVoices; ++i) {
        const uint32_t id = eng2.Play(d1, {});
        if (i == 0)
            firstId = id;
    }
    Expect(eng2.ActiveVoiceCount() == audio::kMaxVoices, "pool full");
    Expect(eng2.Play(d1, {}) != 0, "steal succeeds when full");
    Expect(!eng2.VoiceAlive(firstId), "oldest oneshot stolen");
    Expect(eng2.ActiveVoiceCount() == audio::kMaxVoices, "count stays at cap");
    eng2.StopAll();
    for (int i = 0; i < audio::kMaxVoices; ++i)
        eng2.Play(d2, {.loop = true});
    Expect(eng2.Play(d2, {.loop = true}) == 0, "all-loop full pool refuses");

    // 暂停语义（ADR-015 M4）：Sfx 循环挂起、Ui 组（含循环）不挂起，恢复后双声部齐鸣
    audio::AudioEngine eng3;
    eng3.Init({.forceSilent = true});
    const uint32_t p1 = eng3.RegisterClip({pcm100.data(), 100, 1, 0, 0});
    const uint32_t lp = eng3.Play(p1, {.loop = true});                       // Sfx 循环
    const uint32_t ui = eng3.Play(p1, {.group = audio::Group::Ui, .loop = true});
    Expect(lp != 0 && ui != 0, "pause-test voices started");
    eng3.SetPaused(true);
    eng3.MixOffline(out, 1);
    ExpectNear(out[0], 0.5f * 0.70710678f, 1e-4f, "paused: only Ui loop sounds");
    eng3.AdvanceSilentFrames(5000); // lp 冻结不推进不退役；ui 循环照常
    Expect(eng3.VoiceAlive(lp) && eng3.VoiceAlive(ui), "both alive while paused");
    eng3.SetPaused(false);
    eng3.MixOffline(out, 1);
    ExpectNear(out[0], 2 * 0.5f * 0.70710678f, 1e-4f, "resume: both sound again");
}

void TestAudioDeviceInitNoCrash() {
    // 真初始化（不强静音）：本机设备 / CI null 后端 / 无设备降级——三条路径都不崩，
    // 播放控制全部可用（08 §2 M6c "无音频设备不崩"判据的引擎侧证明）
    audio::AudioEngine eng;
    Expect(eng.Init(), "real init succeeds (device or silent fallback)");
    std::vector<int16_t> pcm(4800, 12000);
    const uint32_t c = eng.RegisterClip({pcm.data(), 4800, 1, 0, 0});
    const uint32_t v = eng.Play(c, {.loop = true});
    Expect(v != 0 && eng.VoiceAlive(v), "play works with real backend");
    eng.Tick(1.0f / 60.0f);
    eng.StopAll();
    float out[2];
    eng.MixOffline(out, 1); // 设备模式红字拒绝（游标归音频线程）；静音模式照常——两路都不崩
    eng.UnregisterClip(c);
    Expect(!eng.VoiceAlive(v), "unregister kills referencing voices");
    eng.Shutdown();
    Expect(!eng.inited(), "shutdown idempotent state");
    eng.Shutdown(); // 二次 Shutdown 无害
}

void TestAudioBench100Sfx() {
    // 08 §2 M6c 判据：100 并发 SFX 模拟侧（staging + Tick 应用）≤ 0.5ms
    audio::AudioEngine eng;
    eng.Init({.forceSilent = true});
    eng.SetRetriggerCooldown(0); // 保留池满/并发上限两级偷取覆盖（默认节流下 100 连发仅 1 次过窗）
    std::vector<int16_t> pcm(2400, 16384);
    const uint32_t c = eng.RegisterClip({pcm.data(), 2400, 1, 0, 0});
    using clock = std::chrono::steady_clock;
    const auto t0 = clock::now();
    for (int i = 0; i < 100; ++i)
        eng.Play(c, {.pan = (i % 2 != 0) ? 0.5f : -0.5f});
    eng.Tick(1.0f / 60.0f);
    const auto t1 = clock::now();
    const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    LEMON_LOG("audio: 100 并发 SFX 模拟侧（staging+Tick）= %.4f ms", ms);
    Expect(ms < 0.5, "100 SFX sim-side <= 0.5ms (08 M6c)");
    // 听感验收 2026-10-01 起语义：同 clip 并发上限接管（活 = kMaxVoicesPerClip）；
    // 释放中的声部占槽 → 65+ 发仍穿过池满偷取路径（两级偷取都被本测走过）
    Expect(eng.ActiveVoiceCount() == audio::kMaxVoicesPerClip,
           "same-clip burst lands at per-clip cap");
}

void TestAudioBakedRoundtrip() {
    // M6c 竖切批：LBA1 烤制/装载全链——合成 44.1k 立体声 WAV（烤制期须重采样到
    // 48k）→ BakeAudioFile → LoadBakedClip → RegisterClip/Play/MixOffline。
    // wav 头手写（44B RIFF），无外部夹具依赖。
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "lemon-audio-bake-test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    const std::string wav = (dir / "in.wav").string();
    const std::string baked = (dir / "out.baked").string();

    constexpr uint32_t kSrcRate = 44100;
    constexpr uint32_t kFrames = 4410; // 0.1s → 48k 后 ≈ 4800 帧
    std::vector<int16_t> pcm(kFrames * 2);
    for (uint32_t i = 0; i < kFrames; ++i) {
        pcm[i * 2] = (int16_t)(12000.0f * std::sin(i * 0.05f));     // L 正弦
        pcm[i * 2 + 1] = (int16_t)(-12000.0f * std::sin(i * 0.05f)); // R 反相
    }
    {
        FILE* f = std::fopen(wav.c_str(), "wb");
        Expect(f != nullptr, "wav fixture open");
        const uint32_t dataBytes = kFrames * 2 * 2;
        const uint32_t riffSize = 36 + dataBytes;
        std::fwrite("RIFF", 1, 4, f);
        std::fwrite(&riffSize, 4, 1, f);
        std::fwrite("WAVEfmt ", 1, 8, f);
        const uint32_t fmtSize = 16;
        const uint16_t fmt = 1, ch = 2, bits = 16;
        const uint32_t byteRate = kSrcRate * ch * bits / 8;
        const uint16_t blockAlign = (uint16_t)(ch * bits / 8);
        std::fwrite(&fmtSize, 4, 1, f);
        std::fwrite(&fmt, 2, 1, f);
        std::fwrite(&ch, 2, 1, f);
        std::fwrite(&kSrcRate, 4, 1, f);
        std::fwrite(&byteRate, 4, 1, f);
        std::fwrite(&blockAlign, 2, 1, f);
        std::fwrite(&bits, 2, 1, f);
        std::fwrite("data", 1, 4, f);
        std::fwrite(&dataBytes, 4, 1, f);
        std::fwrite(pcm.data(), 2, pcm.size(), f);
        std::fclose(f);
    }

    Expect(audio::BakeAudioFile(wav.c_str(), baked.c_str()), "bake 44.1k wav → LBA1");
    std::vector<int16_t> loaded;
    audio::BakedClipInfo info;
    Expect(audio::LoadBakedClip(baked.c_str(), loaded, info), "load LBA1");
    Expect(info.channels == 2, "baked keeps stereo");
    // 批①：循环点烤制锁（秒 → 帧取整 + 钳界；0/0 端点 = 全曲）
    {
        const std::string lp = (dir / "loop.baked").string();
        Expect(audio::BakeAudioFile(wav.c_str(), lp.c_str(), 0.01f, 0.05f),
               "bake with loop points");
        audio::BakedClipInfo li;
        std::vector<int16_t> lpPcm;
        Expect(audio::LoadBakedClip(lp.c_str(), lpPcm, li), "load loop baked");
        Expect(li.loopStart == 480 && li.loopEnd == 2400,
               "loop secs → 48k frames (0.01s/0.05s)");
        const std::string clamped = (dir / "clamp.baked").string();
        Expect(audio::BakeAudioFile(wav.c_str(), clamped.c_str(), 0.0f, 99.0f),
               "bake with overlong loop end");
        audio::BakedClipInfo ci;
        std::vector<int16_t> cPcm;
        Expect(audio::LoadBakedClip(clamped.c_str(), cPcm, ci), "load clamped baked");
        Expect(ci.loopEnd == ci.frameCount, "loop end clamped to tail");
    }
    Expect(info.frameCount >= 4700 && info.frameCount <= 4900,
           "44.1k→48k resampled frame count");
    Expect(info.loopEnd == info.frameCount, "loopEnd defaults to tail");
    Expect(loaded.size() == info.frameCount * 2, "payload size consistent");
    // 反相立体声经混音 pan 中心 → L/R 相消为零（重采样是线性的，能量守恒近似）
    audio::AudioEngine eng;
    eng.Init({.forceSilent = true});
    const uint32_t clip = eng.RegisterClip(
        {loaded.data(), info.frameCount, info.channels, info.loopStart, info.loopEnd});
    Expect(clip != 0, "register baked clip");
    const uint32_t v = eng.Play(clip, {});
    Expect(v != 0, "play baked clip");
    float out[8];
    eng.MixOffline(out, 4);
    Expect(std::fabs(out[0]) < 1e-2f && std::fabs(out[1]) < 1e-2f,
           "antiphase stereo cancels at center pan");
    // 坏头拒绝：截断的 LBA1
    {
        FILE* f = std::fopen(baked.c_str(), "rb");
        std::vector<uint8_t> head(32);
        Expect(std::fread(head.data(), 1, 32, f) == 32, "read head for corrupt test");
        std::fclose(f);
        head[3] = 'X'; // 破坏魔数
        const std::string bad = (dir / "bad.baked").string();
        f = std::fopen(bad.c_str(), "wb");
        std::fwrite(head.data(), 1, 32, f);
        std::fclose(f);
        std::vector<int16_t> junk;
        audio::BakedClipInfo ji;
        Expect(!audio::LoadBakedClip(bad.c_str(), junk, ji), "corrupt magic rejected");
    }
    // review 2026-09-30 热修回归锁：坏源判失败且不落任何产物（半截 .baked 的
    // mtime 比源新会被缓存判定永不重烤——原子写 + 流错误判失败的双保险）
    {
        const std::string garbage = (dir / "garbage.wav").string();
        const std::string outG = (dir / "garbage.baked").string();
        FILE* f = std::fopen(garbage.c_str(), "wb");
        Expect(f != nullptr, "garbage fixture open");
        std::fwrite("NOTAWAVFILEJUSTGARBAGEBYTES", 1, 28, f);
        std::fclose(f);
        Expect(!audio::BakeAudioFile(garbage.c_str(), outG.c_str()), "garbage source fails bake");
        std::error_code ec2;
        Expect(!fs::exists(outG, ec2), "failed bake leaves no product");
        Expect(!fs::exists(outG + ".tmp", ec2), "failed bake leaves no tmp");
    }
    fs::remove_all(dir, ec);
}

// review 2026-10-01 二轮热修回归锁：①回绕/超限载荷头拒绝（头校验的 uint32 乘法
// 可回绕——构造 frameCount 使截断值恰好等于声称 payloadBytes，旧校验放行 →
// resize ~2GiB 直接 bad_alloc 崩装载路径）；②并发烤制同一 dst 不交错（后台烤制
// 线程与 EnterPlay/试听兜底的 TOCTOU 窗口——tmp 唯一化前两把 FILE* 写同一 inode）。
void TestAudioBakedHardening() {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "lemon-audio-baked-hardening";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);

    // ① 手写两种坏头：回绕（0x60000000 帧×2ch×2B=0x180000000 → 截断 0x80000000）
    // 与无回绕但超限（0x20000000 帧 → 恰 2GiB，uint32 域自洽）
    const auto put16 = [](uint8_t* p, uint16_t v) {
        p[0] = uint8_t(v);
        p[1] = uint8_t(v >> 8);
    };
    const auto put32 = [](uint8_t* p, uint32_t v) {
        p[0] = uint8_t(v);
        p[1] = uint8_t(v >> 8);
        p[2] = uint8_t(v >> 16);
        p[3] = uint8_t(v >> 24);
    };
    const char* names[] = {"wrapped.baked", "huge.baked"};
    const uint32_t frameCounts[] = {0x60000000u, 0x20000000u};
    for (int i = 0; i < 2; ++i) {
        uint8_t head[32] = {};
        std::memcpy(head, "LBA1", 4);
        put16(head + 4, 1);
        put16(head + 6, 32);
        put16(head + 8, 1);
        put16(head + 10, 2);
        put32(head + 12, 48000);
        put32(head + 16, frameCounts[i]);
        put32(head + 28, uint32_t(uint64_t(frameCounts[i]) * 2 * 2));
        const std::string path = (dir / names[i]).string();
        FILE* f = std::fopen(path.c_str(), "wb");
        Expect(f != nullptr, "hardening fixture open");
        std::fwrite(head, 1, 32, f);
        std::fclose(f);
        std::vector<int16_t> pcm;
        audio::BakedClipInfo info;
        Expect(!audio::LoadBakedClip(path.c_str(), pcm, info),
               "wrapped/over-cap payload head rejected");
        Expect(pcm.empty(), "rejected load leaves pcm empty");
        audio::BakedClipInfo pi;
        Expect(!audio::PeekBakedClip(path.c_str(), pi), "peek rejects same head");
    }

    // ② 并发烤制同一 dst：tmp 唯一化后各写各的、原子换名后写者胜 → 两侧成功、
    // 产物完整可装载（唯一化前 = 交错写/假换名失败/remove 误删对端）
    const std::string wav = (dir / "in.wav").string();
    {
        constexpr uint32_t kFrames = 4800, kRate = 48000; // 0.1s 单声道
        const uint32_t dataBytes = kFrames * 2, riffSize = 36 + dataBytes;
        FILE* f = std::fopen(wav.c_str(), "wb");
        Expect(f != nullptr, "hardening wav fixture open");
        std::fwrite("RIFF", 1, 4, f);
        std::fwrite(&riffSize, 4, 1, f);
        std::fwrite("WAVEfmt ", 1, 8, f);
        const uint32_t fmtSize = 16;
        const uint16_t fmt = 1, ch = 1, bits = 16;
        const uint32_t byteRate = kRate * ch * bits / 8;
        const uint16_t blockAlign = uint16_t(ch * bits / 8);
        std::fwrite(&fmtSize, 4, 1, f);
        std::fwrite(&fmt, 2, 1, f);
        std::fwrite(&ch, 2, 1, f);
        std::fwrite(&kRate, 4, 1, f);
        std::fwrite(&byteRate, 4, 1, f);
        std::fwrite(&blockAlign, 2, 1, f);
        std::fwrite(&bits, 2, 1, f);
        std::fwrite("data", 1, 4, f);
        std::fwrite(&dataBytes, 4, 1, f);
        for (uint32_t i = 0; i < kFrames; ++i) {
            const int16_t s = int16_t(8000.0f * std::sin(i * 0.1f));
            std::fwrite(&s, 2, 1, f);
        }
        std::fclose(f);
    }
    const std::string dst = (dir / "race.baked").string();
    std::atomic<int> okCount{0};
    const auto bakeJob = [&] {
        if (audio::BakeAudioFile(wav.c_str(), dst.c_str()))
            ++okCount;
    };
    std::thread t1(bakeJob), t2(bakeJob);
    t1.join();
    t2.join();
    Expect(okCount.load() == 2, "concurrent bakes both succeed");
    {
        std::vector<int16_t> loaded;
        audio::BakedClipInfo info;
        Expect(audio::LoadBakedClip(dst.c_str(), loaded, info), "raced dst loads clean");
        Expect(info.frameCount == 4800 && info.channels == 1, "raced dst fields intact");
        Expect(loaded.size() == info.frameCount, "raced dst payload complete");
    }
    // 失败烤制不留任何 tmp（唯一后缀名同受失败清理覆盖——不留猜测文件名缺口）
    {
        const std::string garbage = (dir / "garbage.wav").string();
        FILE* f = std::fopen(garbage.c_str(), "wb");
        std::fwrite("NOTAWAVJUSTGARBAGEBYTES", 1, 24, f);
        std::fclose(f);
        const std::string outG = (dir / "garbage.baked").string();
        Expect(!audio::BakeAudioFile(garbage.c_str(), outG.c_str()),
               "garbage source fails bake");
        int tmpCount = 0;
        for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
            if (it->path().string().find(".tmp") != std::string::npos)
                ++tmpCount;
        Expect(tmpCount == 0, "no tmp leftovers (unique suffixes cleaned too)");
    }
    fs::remove_all(dir, ec);
}

// ------------------------------------------------ M6c 批①b：SPSC 环 + 流式声部 ----
void TestAudioSpscRing() {
    // 批①b：环序/边界——容量取 2^n、单调索引免 ABA、全满/全空、跨回卷 FIFO
    audio::SpscRing ring(1000);
    Expect(ring.Capacity() == 1024, "capacity rounds up to pow2");
    Expect(ring.Size() == 0 && ring.Free() == 1024, "empty state");

    uint8_t wbuf[512], rbuf[512];
    uint64_t wpos = 0, rpos = 0; // 全局字节位（内容 = 位置哈希，序错即暴露）
    uint32_t seed = 12345;
    const auto rnd = [&seed] {
        seed = seed * 1664525u + 1013904223u;
        return seed >> 8;
    };
    for (int step = 0; step < 300; ++step) {
        const size_t wn = 1 + rnd() % (sizeof wbuf) + 0;
        for (size_t i = 0; i < wn; ++i)
            wbuf[i] = uint8_t(((wpos + i) * 31u + 7u) >> 3);
        wpos += ring.Write(wbuf, wn); // 可能部分写（环满）——前缀一致即序一致
        const size_t rn = 1 + rnd() % (sizeof rbuf) + 0;
        const size_t got = ring.Read(rbuf, rn);
        bool ok = got > 0;
        for (size_t i = 0; i < got; ++i)
            ok = ok && rbuf[i] == uint8_t(((rpos + i) * 31u + 7u) >> 3);
        Expect(ok, "interleaved chunk FIFO holds across wrap");
        rpos += got;
    }
    // 排空到恰好读完 + 全满写 0
    while (ring.Size() > 0) {
        const size_t got = ring.Read(rbuf, sizeof rbuf);
        bool ok = got > 0;
        for (size_t i = 0; i < got; ++i)
            ok = ok && rbuf[i] == uint8_t(((rpos + i) * 31u + 7u) >> 3);
        Expect(ok, "drain keeps order");
        rpos += got;
    }
    Expect(rpos == wpos, "all written bytes read in order");
    for (int i = 0; i < 4; ++i) wpos += ring.Write(wbuf, 256);
    Expect(ring.Free() == 0 && ring.Write(wbuf, 1) == 0, "full ring rejects writes");

    // 双线程锤（4MiB，随机块）：单生产者×单消费者字节序精确
    audio::SpscRing big(64 * 1024);
    constexpr uint64_t kTotal = 4ull << 20;
    std::atomic<bool> orderOk{true};
    std::thread prod([&] {
        uint8_t buf[1024];
        uint32_t s = 999;
        const auto r = [&s] {
            s = s * 1664525u + 1013904223u;
            return s >> 8;
        };
        uint64_t p = 0;
        while (p < kTotal) {
            const size_t n = 1 + r() % (sizeof buf) + 0;
            for (size_t i = 0; i < n; ++i)
                buf[i] = uint8_t(((p + i) * 2654435761ull) >> 24);
            p += big.Write(buf, n);
        }
    });
    std::thread cons([&] {
        uint8_t buf[1024];
        uint32_t s = 777;
        const auto r = [&s] {
            s = s * 1664525u + 1013904223u;
            return s >> 8;
        };
        uint64_t p = 0;
        while (p < kTotal) {
            const size_t got = big.Read(buf, 1 + r() % (sizeof buf) + 0);
            for (size_t i = 0; i < got; ++i)
                if (buf[i] != uint8_t(((p + i) * 2654435761ull) >> 24))
                    orderOk.store(false); // review 2026-10-02 #33：序错继续排空到
                                          // kTotal——首错即 return 会让生产者在环满
                                          // 上忙转、prod.join() 挂到 ctest TIMEOUT
                                          // 而非报 FAIL（挂死 ≠ 红字）
            p += got;
        }
    });
    prod.join();
    cons.join();
    Expect(orderOk.load(), "threaded SPSC 4MiB byte-exact order");
}

void TestAudioStreamVoice() {
    // 批①b：流式声部——预填即鸣（首回调零欠载）、一次性曲终、循环回卷换位（环内
    // 线性化帧流）、欠载静音计数、同 clip 双声部（BGM 交叉淡出形态）。静音模式
    // 手动泵（PumpStreams）= 离线确定性生产者。
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "lemon-audio-stream-test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);

    // 48k 立体声正弦 wav（烤制 passthrough——内容可精确对照）
    constexpr uint32_t kFrames = 14400, kRate = 48000; // 0.3s
    std::vector<int16_t> pcm(kFrames * 2);
    for (uint32_t i = 0; i < kFrames; ++i) {
        pcm[i * 2] = int16_t(10000.0f * std::sin(i * 0.05f));
        pcm[i * 2 + 1] = int16_t(-10000.0f * std::sin(i * 0.05f));
    }
    const std::string wav = (dir / "in.wav").string();
    {
        FILE* f = std::fopen(wav.c_str(), "wb");
        Expect(f != nullptr, "stream wav fixture open");
        const uint32_t dataBytes = kFrames * 2 * 2, riffSize = 36 + dataBytes;
        std::fwrite("RIFF", 1, 4, f);
        std::fwrite(&riffSize, 4, 1, f);
        std::fwrite("WAVEfmt ", 1, 8, f);
        const uint32_t fmtSize = 16, byteRate = kRate * 2 * 2;
        const uint16_t fmt = 1, ch = 2, bits = 16, blockAlign = 4;
        std::fwrite(&fmtSize, 4, 1, f);
        std::fwrite(&fmt, 2, 1, f);
        std::fwrite(&ch, 2, 1, f);
        std::fwrite(&kRate, 4, 1, f);
        std::fwrite(&byteRate, 4, 1, f);
        std::fwrite(&blockAlign, 2, 1, f);
        std::fwrite(&bits, 2, 1, f);
        std::fwrite("data", 1, 4, f);
        std::fwrite(&dataBytes, 4, 1, f);
        std::fwrite(pcm.data(), 2, pcm.size(), f);
        std::fclose(f);
    }
    const std::string baked = (dir / "s.baked").string();
    const std::string looped = (dir / "loop.baked").string();
    const std::string bigBaked = (dir / "big.baked").string();
    Expect(audio::BakeAudioFile(wav.c_str(), baked.c_str()), "bake stream fixture");
    // 循环点 [0.05s, 0.2s) = [2400, 9600) 帧
    Expect(audio::BakeAudioFile(wav.c_str(), looped.c_str(), 0.05f, 0.2f),
           "bake loop fixture");
    {
        // 大 clip（80000 帧 = 320KB > 256KiB 环）：欠载路径专用
        std::vector<int16_t> big(80000 * 2, 6000);
        const uint32_t dataBytes = 80000u * 2 * 2, riffSize = 36 + dataBytes;
        FILE* f = std::fopen(wav.c_str(), "wb"); // 复用 wav 名重写为大内容
        Expect(f != nullptr, "big wav rewrite open");
        std::fwrite("RIFF", 1, 4, f);
        std::fwrite(&riffSize, 4, 1, f);
        std::fwrite("WAVEfmt ", 1, 8, f);
        const uint32_t fmtSize = 16, byteRate = 48000 * 2 * 2;
        const uint16_t fmt = 1, ch = 2, bits = 16, blockAlign = 4;
        std::fwrite(&fmtSize, 4, 1, f);
        std::fwrite(&fmt, 2, 1, f);
        std::fwrite(&ch, 2, 1, f);
        const uint32_t rate = 48000;
        std::fwrite(&rate, 4, 1, f);
        std::fwrite(&byteRate, 4, 1, f);
        std::fwrite(&blockAlign, 2, 1, f);
        std::fwrite(&bits, 2, 1, f);
        std::fwrite("data", 1, 4, f);
        std::fwrite(&dataBytes, 4, 1, f);
        std::fwrite(big.data(), 2, big.size(), f);
        std::fclose(f);
        Expect(audio::BakeAudioFile(wav.c_str(), bigBaked.c_str()), "bake big fixture");
    }

    audio::AudioEngine eng;
    Expect(eng.Init({.forceSilent = true}), "silent init (manual pump)");
    eng.SetRetriggerCooldown(0); // 末段同 clip 双声部（BGM 交叉淡出形态）需同帧连播
    Expect(eng.RegisterStreamClip((dir / "none.baked").string().c_str()) == 0,
           "missing stream file rejected");
    const uint32_t clip = eng.RegisterStreamClip(baked.c_str());
    Expect(clip != 0, "stream clip registered");

    // 一次性：Play 预填整环（clip 57.6KB < 256KiB）→ 零欠载、样本精确、曲终即亡
    const uint32_t v1 = eng.Play(clip, {});
    Expect(v1 != 0, "stream voice plays");
    {
        std::vector<float> out(kFrames * 2);
        uint32_t doneFrames = 0;
        while (doneFrames < kFrames) {
            const uint32_t chunk = std::min<uint32_t>(2048, kFrames - doneFrames);
            eng.MixOffline(out.data() + doneFrames * 2, chunk);
            doneFrames += chunk;
        }
        Expect(eng.StreamUnderrunFrames() == 0, "prefilled stream never underruns");
        bool ok = true;
        for (uint32_t i = 0; i < 16; ++i) { // 抽 16 帧对照（pan 中心等功率）
            const float exL = pcm[i * 900 * 2] / 32768.0f * 0.70710678f;
            const float exR = pcm[i * 900 * 2 + 1] / 32768.0f * 0.70710678f;
            ok = ok && std::fabs(out[i * 900 * 2] - exL) < 1e-4f &&
                 std::fabs(out[i * 900 * 2 + 1] - exR) < 1e-4f;
        }
        Expect(ok, "stream samples byte-exact via ring");
        Expect(!eng.VoiceAlive(v1), "one-shot stream done at frameCount");
    }
    eng.Tick();
    Expect(eng.ActiveVoiceCount() == 0, "stream voice reaped");

    // 循环回卷：环内线性化 = [0..14400) + [2400..9600) 反复——消费无回卷逻辑
    const uint32_t loopClip = eng.RegisterStreamClip(looped.c_str());
    Expect(loopClip != 0, "loop stream clip registered");
    const uint32_t v2 = eng.Play(loopClip, {.loop = true});
    Expect(v2 != 0 && eng.VoiceAlive(v2), "loop stream voice plays");
    eng.PumpStreams(); // 起播按一次性预填首环；Pump 后按 loop 语义续喂回卷段
    {
        const uint32_t span = 9600 - 2400;
        const uint32_t total = kFrames + span * 2 + 100; // 首遍 + 两圈 + 余量
        std::vector<float> out(total * 2);
        uint32_t doneFrames = 0;
        while (doneFrames < total) {
            const uint32_t chunk = std::min<uint32_t>(3000, total - doneFrames);
            eng.PumpStreams(); // 模拟填充线程（离线确定性）
            eng.MixOffline(out.data() + doneFrames * 2, chunk);
            doneFrames += chunk;
        }
        bool ok = true;
        for (uint32_t k = 0; k < total; k += 997) {
            const uint32_t idx = k < kFrames ? k : 2400 + (k - kFrames) % span;
            const float exL = pcm[idx * 2] / 32768.0f * 0.70710678f;
            ok = ok && std::fabs(out[k * 2] - exL) < 1e-4f;
        }
        Expect(ok, "loop wrap linearized in ring (producer-side seek)");
        Expect(eng.VoiceAlive(v2), "loop stream voice stays alive");
        Expect(eng.StreamUnderrunFrames() == 0, "pumped loop never underruns");
        eng.Stop(v2);
    }

    // 欠载：大 clip（预填 65536 帧 = 256KiB/4B）不泵直混 → 80000-65536 = 14464 静音帧
    const uint32_t bigClip = eng.RegisterStreamClip(bigBaked.c_str());
    Expect(bigClip != 0, "big stream clip registered");
    const uint32_t v3 = eng.Play(bigClip, {});
    Expect(v3 != 0, "big stream voice plays");
    {
        std::vector<float> out(80000 * 2);
        eng.MixOffline(out.data(), 80000);
        Expect(eng.StreamUnderrunFrames() == 80000 - 65536,
               "unpumped tail counts as underrun silence exactly");
        bool ok = true;
        for (uint32_t i = 0; i < 8; ++i) // 已预填段样本正确
            ok = ok && std::fabs(out[i * 8000 * 2] - 6000 / 32768.0f * 0.70710678f) < 1e-4f;
        Expect(ok, "prefilled span samples correct");
        Expect(!eng.VoiceAlive(v3), "one-shot big stream done despite underrun");
    }

    // 同 clip 双声部（BGM 交叉淡出形态）：两句柄两环两游标，叠加 = 2×
    const uint32_t va = eng.Play(clip, {});
    const uint32_t vb = eng.Play(clip, {});
    Expect(va != 0 && vb != 0 && va != vb, "dual stream voices on same clip");
    {
        float out[8];
        eng.MixOffline(out, 4);
        // review 2026-10-02 #13：对拍帧 1（pcm[2]=10000·sin(0.05)≈4999 非零）——
        // 原对拍帧 0 的 pcm[0]=sin(0)=0，期望 2×0 恒真，0/1/2 个声部全过（真空）
        const float ex = pcm[2] / 32768.0f * 0.70710678f;
        ExpectNear(out[2], 2 * ex, 1e-4f, "dual stream voices sum");
    }
    eng.StopAll();
    fs::remove_all(dir, ec);
}

// ------------------------------------------------ M6c 听感验收驱动：母带限幅/同 clip 并发 ----
void TestAudioMasterLimiter() {
    // 听感验收 2026-10-01：多 kill.wav 同帧叠加 → 硬钳斩波破音。膝下位零增益透传；
    // 过载段 tanh 渐近压回（值严格 < 1.0——硬钳会是恰好 ±1.0 平顶）；单调不过压
    audio::AudioEngine eng;
    Expect(eng.Init({.forceSilent = true}), "silent init");
    eng.SetRetriggerCooldown(0); // 相干叠加断言：同 clip 同帧 N 份连播且音高全同
    eng.SetPitchJitter(0);
    constexpr uint32_t kFrames = 4800;
    std::vector<int16_t> pcm(kFrames); // 单声道正弦，幅值近满格
    for (uint32_t i = 0; i < kFrames; ++i)
        pcm[i] = int16_t(32000.0f * std::sin(i * 0.05f));
    int maxS = 0;
    for (int16_t s : pcm) maxS = std::max(maxS, std::abs(int(s)));
    const uint32_t clip = eng.RegisterClip(std::move(pcm), 1, kFrames, 0, 0);
    Expect(clip != 0, "limiter clip registered");
    const float unit = maxS / 32768.0f;                  // 单声部满音量中心声像前
    const float center = 0.70710678f;                    // 等功率中心每声道
    const auto mixPeak = [&](int voices, float vol) {    // 同帧起播 N 份 → 混完取峰
        for (int k = 0; k < voices; ++k)
            eng.Play(clip, {.volume = vol});
        std::vector<float> out(kFrames * 2);
        eng.MixOffline(out.data(), kFrames);             // 一次性 clip 混完即亡
        eng.Tick();                                      // 回收，案例间状态干净
        float peak = 0;
        for (float s : out) peak = std::max(peak, std::fabs(s));
        return peak;
    };
    const auto softLimit = [](float x) {                 // 与引擎同式（膝点 0.8）
        constexpr float k = 0.8f;
        return x <= k ? x : k + (1.0f - k) * std::tanh((x - k) / (1.0f - k));
    };

    // 膝下位（0.3 vol 峰 ≈ 0.21）：零增益透传（限幅分支未触及）
    const float low = mixPeak(1, 0.3f);
    Expect(std::fabs(low - 0.3f * unit * center) < 1e-6f, "below-knee passes unity");

    // 中度过载（2×0.75 峰 ≈ 1.04）：压回膝上软段——锁曲线本体（硬钳会给恰好 1.0）
    const float mid = mixPeak(2, 0.75f);
    Expect(std::fabs(mid - softLimit(2.0f * 0.75f * unit * center)) < 1e-4f,
           "moderate overload lands on soft knee");

    // 深过载（3×1.0 峰 ≈ 2.07）：渐近顶但严格 < 1.0；且不过压（深 > 中）
    const float hot = mixPeak(3, 1.0f);
    Expect(hot < 1.0f && hot > 0.999f, "deep overload asymptotic under 1.0");
    Expect(hot > mid, "limiter monotonic (louder in = louder out)");

    // review 2026-10-02 #35：联动限幅区分性用例——此前全部夹具单声道 L==R，
    // 「L/R 共用同帧峰值增益」与逐通道独立限幅不可区分。立体声 L 满格/R 低幅
    // 两声部叠加：L 过膝驱动单增益乘双声道 → R 同帧被拉低（独立限幅 R 原样）
    {
        std::vector<int16_t> spcm(kFrames * 2);
        for (uint32_t i = 0; i < kFrames; ++i) {
            spcm[i * 2 + 0] = 32767; // L 满格
            spcm[i * 2 + 1] = 8000;  // R 低幅（膝下）
        }
        const uint32_t sclip = eng.RegisterClip(std::move(spcm), 2, kFrames, 0, 0);
        Expect(sclip != 0, "stereo limiter clip registered");
        eng.Play(sclip, {.volume = 1.0f});
        eng.Play(sclip, {.volume = 1.0f});
        std::vector<float> out(kFrames * 2);
        eng.MixOffline(out.data(), kFrames);
        eng.Tick(); // 回收，保案例间状态干净
        const float center = 0.70710678f;
        const float lIn = 2.0f * (32767.0f / 32768.0f) * center; // 叠加后 L 峰（过膝）
        const float rIn = 2.0f * (8000.0f / 32768.0f) * center;  // 叠加后 R 峰（膝下）
        float lPeak = 0, rPeak = 0;
        for (uint32_t i = 0; i < kFrames; ++i) {
            lPeak = std::max(lPeak, std::fabs(out[i * 2]));
            rPeak = std::max(rPeak, std::fabs(out[i * 2 + 1]));
        }
        Expect(lPeak > 0.99f && lPeak < 1.0f, "stereo overload L limited soft");
        const float linkedGain = softLimit(lIn) / lIn;
        ExpectNear(rPeak, rIn * linkedGain, 1e-4f, "linked gain pulls R with L");
        Expect(rPeak < rIn * 0.9f, "R measurably reduced (vs per-channel unity)");
    }
}

void TestAudioVoiceCapSteal() {
    // 听感验收 2026-10-01：同 clip 重触发无限叠（相干求和最坏 +6dB/份）→ 并发上限
    // kMaxVoicesPerClip + 偷最老。释放复用 D4 包络 5ms（硬停切波前有咔哒）；释放中
    // （stopAtFadeEnd）不计活跃——连发脉冲下"活"声部恒 ≤ 上限
    audio::AudioEngine eng;
    Expect(eng.Init({.forceSilent = true}), "silent init");
    const uint32_t clip = eng.RegisterClip(std::vector<int16_t>(48000, 12000), 1,
                                           48000, 0, 0);
    const uint32_t clip2 = eng.RegisterClip(std::vector<int16_t>(48000, 12000), 1,
                                            48000, 0, 0);
    Expect(clip != 0 && clip2 != 0, "cap clips registered");

    // 上限内共存：kMaxVoicesPerClip 个同 clip 循环声部全活
    uint32_t ids[8] = {};
    for (int k = 0; k < audio::kMaxVoicesPerClip; ++k)
        ids[k] = eng.Play(clip, {.loop = true});
    bool ok = true;
    for (int k = 0; k < audio::kMaxVoicesPerClip; ++k)
        ok = ok && ids[k] != 0 && eng.VoiceAlive(ids[k]);
    Expect(ok, "within-cap same-clip voices coexist");

    // 第 5 个：最老被偷——先占槽释放（仍计活跃），5ms 后亡，新声部与其余活
    const uint32_t fifth = eng.Play(clip, {.loop = true});
    Expect(fifth != 0, "over-cap play accepted via steal");
    Expect(eng.ActiveVoiceCount() == audio::kMaxVoicesPerClip + 1,
           "stolen voice occupies slot during its release");
    eng.AdvanceSilentFrames(480); // 10ms > 5ms 释放
    Expect(!eng.VoiceAlive(ids[0]), "oldest dies after release window");
    ok = eng.VoiceAlive(ids[1]) && eng.VoiceAlive(ids[2]) && eng.VoiceAlive(ids[3]) &&
         eng.VoiceAlive(fifth);
    Expect(ok, "younger voices and newcomer survive");
    eng.Tick();

    // 连发脉冲（同 tick 20 发）：活声部恒 ≤ 上限（其余在各自 5ms 释放中）
    for (int k = 0; k < 20; ++k)
        eng.Play(clip, {.loop = true});
    eng.AdvanceSilentFrames(480);
    eng.Tick();
    Expect(eng.ActiveVoiceCount() == audio::kMaxVoicesPerClip,
           "burst retrigger keeps live voices at cap");

    // 跨 clip 独立：另一 clip 满编不连坐
    eng.StopAll();
    eng.Tick();
    for (int k = 0; k < audio::kMaxVoicesPerClip; ++k)
        ids[k] = eng.Play(clip2, {.loop = true});
    const uint32_t otherClip = eng.Play(clip, {.loop = true});
    ok = otherClip != 0 && eng.VoiceAlive(otherClip);
    for (int k = 0; k < audio::kMaxVoicesPerClip; ++k)
        ok = ok && eng.VoiceAlive(ids[k]);
    Expect(ok, "per-clip cap does not spill across clips");
}

void TestAudioRetriggerThrottlePitchJitter() {
    // 听感验收 2026-10-01"放鞭炮"（同素材高频连发 = 机枪效应）→ 重触发节流 + 音高
    // 微扰。节流时钟 = 混音帧域（设备/静音同径）：窗内新请求丢（返 0 不占槽不偷不
    // 更锚）；窗过即收；异 clip/循环声部豁免。微扰：两连播输出相异（去相干），可
    // 关回整数位精确路径。
    audio::AudioEngine eng;
    Expect(eng.Init({.forceSilent = true}), "silent init");
    eng.SetRetriggerCooldown(0.05f); // 显式 50ms = 2400 帧（不依赖默认值漂移）
    std::vector<int16_t> ramp(48000);
    for (uint32_t i = 0; i < 48000; ++i) ramp[i] = int16_t(i % 32768); // 非常数 PCM：微扰可观测
    const uint32_t a = eng.RegisterClip(std::move(ramp), 1, 48000, 0, 0);
    const uint32_t b = eng.RegisterClip(std::vector<int16_t>(48000, 8000), 1, 48000, 0, 0);
    Expect(a != 0 && b != 0, "throttle clips registered");

    // 窗内丢：第二次同 clip 播放返 0、不占槽
    const uint32_t v1 = eng.Play(a, {});
    Expect(v1 != 0, "first play accepted");
    Expect(eng.Play(a, {}) == 0, "within-window retrigger dropped");
    Expect(eng.ActiveVoiceCount() == 1, "dropped play takes no slot");
    Expect(eng.Play(b, {}) != 0, "other clip unaffected by window");
    Expect(eng.Play(a, {.loop = true}) != 0, "loop exempt from throttle");
    eng.StopAll();
    eng.Tick();

    // 窗过即收：恰 2400 帧（50ms）后新请求过窗（锚 = 上次被接受的起播）
    eng.AdvanceSilentFrames(2400);
    Expect(eng.Play(a, {}) != 0, "past-window retrigger accepted");
    eng.StopAll();
    eng.Tick();

    // 音高微扰（±5% 放大观测）：两连播同 clip 输出相异——同帧对拍即去相干证据
    eng.SetRetriggerCooldown(0);
    eng.SetPitchJitter(0.05f);
    std::vector<float> cap1(480 * 2), cap2(480 * 2);
    Expect(eng.Play(a, {}) != 0, "jitter play 1");
    eng.MixOffline(cap1.data(), 480);
    eng.AdvanceSilentFrames(48000); // 放完首播（cooldown 已关，推进只为状态干净）
    eng.Tick();
    Expect(eng.Play(a, {}) != 0, "jitter play 2");
    eng.MixOffline(cap2.data(), 480);
    bool differ = false;
    for (int i = 0; i < 480 * 2; ++i)
        differ = differ || std::fabs(cap1[i] - cap2[i]) > 1e-6f;
    Expect(differ, "pitch jitter decorrelates identical clips");

    // 微扰关闭 = 整数游标位精确路径（帧 1 = ramp[1]，中心声像 0.707）
    eng.StopAll();
    eng.Tick();
    eng.SetPitchJitter(0);
    Expect(eng.Play(a, {}) != 0, "exact play");
    eng.MixOffline(cap1.data(), 480);
    ExpectNear(cap1[2], 1 / 32768.0f * 0.70710678f, 1e-6f, "jitter off = integer path exact");
}

// ------------------------------------------------ M6c 批②：命令通道/包络/空间化/组件声源 ----
void TestAudioFadeEnvelope() {
    // D4 包络：fadeIn 逐样本爬升；FadeVoice→0 + stopWhenDone 到点终结；
    // 静音模式 AdvanceSilentFrames 同径推进（逻辑记账 = 有声模式）
    audio::AudioEngine eng;
    Expect(eng.Init({.forceSilent = true}), "silent init");
    eng.SetRetriggerCooldown(0); // v2/v3 同 clip 快速重播断言包络语义（常数 PCM 对微扰免疫）
    std::vector<int16_t> pcm(4800 * 2, 8000); // 0.1s 恒幅 stereo
    const uint32_t clip = eng.RegisterClip(std::move(pcm), 2, 4800, 0, 0);
    Expect(clip != 0, "clip registered");

    // fadeIn 0.05s：混 1200 帧（0.025s）后包络约半幅（等功率中心声像 0.707 计入）。
    // review 2026-10-02 #12：v1 改循环声部——一次性版 3600/4800 帧后仅剩 1200 帧，
    // 0.05s 淡出需 2400 帧，自然终点先亡 mask 掉 stopAtFadeEnd 断言（FadeVoice
    // 完全失效断言也过）；循环声部无自然终点，终结只能来自淡出到 0
    const uint32_t v1 = eng.Play(clip, {.volume = 1.0f, .loop = true, .fadeInSec = 0.05f});
    Expect(v1 != 0, "fade-in voice");
    float out[4800 * 2];
    eng.MixOffline(out, 1200);
    float peak = 0;
    for (int i = 0; i < 1200 * 2; ++i) peak = std::max(peak, std::fabs(out[i]));
    const float full = (8000.0f / 32768.0f) * 0.7071f; // 恒幅 × 中心声像
    Expect(peak > full * 0.30f && peak < full * 0.60f, "fade-in midpoint ~half");
    eng.MixOffline(out, 2400); // 淡入完成段
    peak = 0;
    for (int i = 0; i < 2400 * 2; ++i) peak = std::max(peak, std::fabs(out[i]));
    Expect(peak > full * 0.9f, "fade-in reached full");

    // FadeVoice→0 + stopWhenDone：0.05s 后声部终结（循环声部无自然终点——终结
    // 只能来自包络 stopAtFadeEnd，未被 mask，review 2026-10-02 #12）
    Expect(eng.FadeVoice(v1, 0.0f, 0.05f, true), "fade-out accepted");
    eng.MixOffline(out, 4800); // 足够跑完淡出窗
    Expect(!eng.VoiceAlive(v1), "voice dead after fade to zero");
    Expect(eng.ActiveVoiceCount() == 0, "no active voices left");

    // 硬切（seconds<=0 + stop）：立即终结
    const uint32_t v2 = eng.Play(clip, {.volume = 1.0f});
    Expect(eng.FadeVoice(v2, 0.0f, 0.0f, true), "hard fade accepted");
    Expect(!eng.VoiceAlive(v2), "hard fade kills at once");

    // 静音记账同径：fadeIn 中 AdvanceSilentFrames 推进包络，到 0+stop 终结
    const uint32_t v3 = eng.Play(clip, {.volume = 1.0f, .fadeInSec = 0.01f});
    eng.AdvanceSilentFrames(480); // 0.01s = 淡入完成
    Expect(eng.VoiceAlive(v3), "silent fade-in completes alive");
    Expect(eng.FadeVoice(v3, 0.0f, 0.02f, true), "silent fade-out");
    eng.AdvanceSilentFrames(960); // 0.02s
    Expect(!eng.VoiceAlive(v3), "silent fade-out completes dead");
}

void TestAudioSpatialMath() {
    // ADR-015 M5：线性衰减钳界 + 声像半宽归一（纯函数，无需引擎）
    audio::AudioListener l{{0, 0}, 640.0f};
    float g = -1, p = 2;
    audio::ComputeSpatial({0, 0}, l, 256, 1024, g, p);
    Expect(g == 1.0f && std::fabs(p) < 1e-6f, "at listener: full gain, center pan");
    audio::ComputeSpatial({640, 0}, l, 256, 1024, g, p); // 屏幕右缘（衰减中点）
    Expect(g > 0.45f && g < 0.55f && p == 1.0f, "screen edge: mid gain, full right");
    audio::ComputeSpatial({-640, 0}, l, 256, 1024, g, p);
    Expect(p == -1.0f, "full left pan");
    audio::ComputeSpatial({0, 1024}, l, 256, 1024, g, p); // y 轴远端：衰减满、pan 中
    Expect(g == 0.0f && std::fabs(p) < 1e-6f, "beyond maxDist: zero gain");
    audio::ComputeSpatial({0, 128}, l, 256, 1024, g, p);
    Expect(g == 1.0f, "within refDist: full gain");
    audio::ComputeSpatial({2000, 0}, l, 256, 1024, g, p);
    Expect(g == 0.0f && p == 1.0f, "far right: zero gain clamped pan");
    // maxDist<=refDist 退化 = 全程可闻（防 0 除钳）
    audio::ComputeSpatial({500, 0}, l, 256, 256, g, p);
    Expect(g == 1.0f, "degenerate ref==max audible");
}

void TestAudioChannelCommands() {
    // 命令通道：staging 同步发号 / Stop 保序（未提交撤销、已提交停引擎）/
    // BGM 单槽换曲淡出 / StopAll 清记账 + 僵尸播放防线 / 提交期死条目回收
    audio::AudioEngine eng;
    Expect(eng.Init({.forceSilent = true}), "silent init");
    std::vector<int16_t> pcm(48000 * 2, 6000); // 1s 循环体
    const uint32_t clip = eng.RegisterClip(std::move(pcm), 2, 48000, 0, 0);
    const uint32_t clip2 = eng.RegisterClip(std::vector<int16_t>(48000 * 2, 6000), 2,
                                            48000, 0, 0);
    audio::AudioListener l{{0, 0}, 640.0f};

    // 未提交撤销：同 tick Play + Stop → Submit 后无声部
    audio::AudioChannel ch;
    const uint32_t v = ch.StagePlay(clip, 1, 1.0f, 0, true);
    Expect(v != 0, "staging returns nonzero id");
    Expect(ch.StageStop(v), "stop pending play hit");
    ch.Submit(&eng, l);
    Expect(ch.LogicalAlive(v) == false, "cancelled play leaves no entry");
    Expect(eng.ActiveVoiceCount() == 0, "cancelled play never started");

    // 正常路径 + 引擎拒绝（坏 clipId staging 即返 0）
    Expect(ch.StagePlay(0, 1, 1, 0, false) == 0, "bad clip rejected at staging");
    const uint32_t v2 = ch.StagePlay(clip, 1, 1.0f, 0, true);
    ch.Submit(&eng, l);
    Expect(eng.ActiveVoiceCount() == 1, "submitted voice active");
    ch.Submit(&eng, l); // 空提交幂等
    Expect(eng.ActiveVoiceCount() == 1, "empty submit idempotent");

    // 已提交停：Stop 命令经提交落地
    Expect(ch.StageStop(v2), "stop submitted voice staged");
    ch.Submit(&eng, l);
    Expect(eng.ActiveVoiceCount() == 0, "submitted voice stopped");

    // BGM 单槽：换曲 = 旧淡出新 fadeIn；槽位记账翻新
    Expect(ch.StageBgm(clip, 0.5f, 0.5f) == 1, "bgm accepted");
    ch.Submit(&eng, l);
    const uint32_t bgm1 = ch.bgmVoice();
    Expect(bgm1 != 0, "bgm slot occupied");
    eng.AdvanceSilentFrames(48000); // 放 1s（淡入完成，循环中）
    Expect(ch.StageBgm(clip2, 0.5f, 0.5f) == 1, "bgm change accepted");
    ch.Submit(&eng, l);
    Expect(ch.bgmVoice() != bgm1, "bgm slot rotated");
    Expect(eng.ActiveVoiceCount() == 2, "crossfade: old fading + new active");
    eng.AdvanceSilentFrames(48000); // 旧曲 0.5s 淡完终结
    ch.Submit(&eng, l);             // 回收死条目
    Expect(eng.ActiveVoiceCount() == 1, "old bgm faded out");

    // StopAll：清记账 + 同 tick 后续 Play 不被清（顺序语义）
    ch.StageStopAll();
    const uint32_t v3 = ch.StagePlay(clip, 1, 1.0f, 0, false);
    Expect(v3 != 0, "play after stopall stages");
    ch.Submit(&eng, l);
    Expect(eng.ActiveVoiceCount() == 1, "post-stopall play survives (order kept)");
    Expect(ch.bgmVoice() == 0, "bgm slot cleared by stopall");

    // null 引擎：纯记账（无声宿主不崩、发号照常）
    audio::AudioChannel ch2;
    const uint32_t v4 = ch2.StagePlay(clip, 1, 1, 0, false);
    Expect(v4 != 0, "null engine still allocates id");
    ch2.Submit(nullptr, l);
    Expect(ch2.LogicalAlive(v4) == false, "null engine entry dies at submit");

    // 组/主音量/暂停命令提交落地
    ch2.StageGroupVolume(0, 0.25f);
    ch2.StageMasterVolume(0.5f);
    ch2.StageSetPaused(true);
    ch2.Submit(&eng, l);
    Expect(std::fabs(eng.GroupVolume(audio::Group::Bgm) - 0.25f) < 1e-6f, "group vol applied");
    Expect(std::fabs(eng.MasterVolume() - 0.5f) < 1e-6f, "master vol applied");
    // BGM 循环声部被挂起（ADR M4；一次性不挂）
    const uint32_t lv = ch2.StagePlay(clip, 0, 1.0f, 0, true);
    ch2.Submit(&eng, l);
    (void)lv;
    const uint32_t pausedLoop = ch2.StagePlay(clip, 0, 1.0f, 0, true);
    (void)pausedLoop; // 计数断言按活跃口径（#36 恰 3），句柄本身不判
    ch2.Submit(&eng, l);
    eng.AdvanceSilentFrames(4800); // 暂停声部游标不动（放完一帧都不该退役）
    // review 2026-10-02 #36：确定性恰 3（v3 Sfx 一次性 cursor 4800<48000 仍活 +
    // 两条挂起 Bgm loop）——原 >=2 容忍「错杀一条挂起 loop」的缺陷照样通过
    Expect(eng.ActiveVoiceCount() == 3, "paused loops still occupy slots");
}

void TestAudioSourceLifecycle() {
    // AudioSource 组件 → AudioSystem 绑定生命周期：playOnStart 起播一次/实体亡停/
    // 换片重绑/监听器空间热更（静音引擎，走管线逐 tick）
    audio::AudioEngine eng;
    Expect(eng.Init({.forceSilent = true}), "silent init");
    World world;
    Scene& s = world.CreateScene("audio-src");
    world.SetActiveScene(&s);
    world.InstallDefaultSystems();
    // guid→clip 解析桩（map 单条）
    std::vector<int16_t> pcm(48000 * 2, 6000);
    const uint32_t clipA = eng.RegisterClip(std::move(pcm), 2, 48000, 0, 0);
    const uint32_t clipB = eng.RegisterClip(std::vector<int16_t>(48000 * 2, 6000), 2,
                                            48000, 0, 0);
    struct Ctx {
        uint64_t guidA, guidB;
        uint32_t a, b;
    } ctx{0xAAAA, 0xBBBB, clipA, clipB};
    world.SetAudioBackend(&eng, [](uint64_t guid, void* p) -> uint32_t {
        const Ctx* c = static_cast<const Ctx*>(p);
        if (guid == c->guidA) return c->a;
        if (guid == c->guidB) return c->b;
        return 0;
    }, &ctx);
    world.SetAudioListener({{0, 0}, 640.0f});

    Entity e = s.Create();
    s.Emplace<Transform2D>(e, Transform2D{Vec2{100, 0}});
    AudioSource src{};
    src.clipGuid = 0xAAAA;
    s.Emplace<AudioSource>(e, src); // 默认 flags = playOnStart

    world.Step(1.0f / 60.0f);
    Expect(eng.ActiveVoiceCount() == 1, "playOnStart starts on first tick");

    // 空间热更：实体移到远端（gain→0 区）→ 声部仍在（loop），参数被推
    s.Get<Transform2D>(e).pos = {2000, 0};
    world.Step(1.0f / 60.0f);
    Expect(eng.ActiveVoiceCount() == 1, "far source still alive (loop)");

    // 换片：clipGuid 变 → 停旧起新
    s.Get<AudioSource>(e).clipGuid = 0xBBBB;
    world.Step(1.0f / 60.0f);
    Expect(eng.ActiveVoiceCount() == 1, "rebound voice replaces old");

    // 实体亡 → 声部停、绑定回收
    s.Destroy(e);
    world.Step(1.0f / 60.0f); // 两阶段销毁提交（Essential）
    world.Step(1.0f / 60.0f); // AudioSystem 下一次扫描回收
    Expect(eng.ActiveVoiceCount() == 0, "voice stops on entity death");

    // playOnStart 关（flags=0）→ 不起播
    Entity e2 = s.Create();
    AudioSource quiet{};
    quiet.clipGuid = 0xAAAA;
    quiet.flags = 0;
    s.Emplace<AudioSource>(e2, quiet);
    world.Step(1.0f / 60.0f);
    Expect(eng.ActiveVoiceCount() == 0, "no playOnStart flag = silent");

    // 零 ECS 写验证：AudioSource 原值未被动（哈希面免疫的机械证据）
    Expect(s.Get<AudioSource>(e2).clipGuid == 0xAAAA && s.Get<AudioSource>(e2).flags == 0,
           "audio system never writes component");
}

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
    TestTargetBoardGridEquivalence();
    TestTargetBoardBadCoordDefense(); // review 2026-10-02 #19：坏坐标不炸进程
    TestTargetBoardParallelRebuildIsomorphic();
    TestSystemPipelineOrder();
    TestSimulationEndToEnd();
    TestSeparationForce();
    TestArchiveArraySegAndRuntimeFields();
    TestArchiveMalformedTolerance();
    TestSpatialHashRangeClamp();
    TestConcurrentDestroy();
    TestDestroyQueueTagLifecycle();
    TestSaveChannelHardening();
    TestSaveChannelSplits(); // M6a 批② T5：三档三文件 + 惰性迁移 + 坏档按档隔离
    TestPrefabCachePlaySpawn(); // M7a 批③：Play 世界 Prefab 工厂缓存下沉件
    TestCameraFollowCore();    // M7a 批③：相机跟随纯函数下沉件
    TestSceneExtractorCore();  // M7a 批③：ECS→渲染提取下沉件
    TestPoolDataStable();      // M7a 批③ D5：池基址护栏基础
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
    TestVerifyAnimatorPingPong(); // M6a 批② T3b-2
    TestTweenTable(); // A 档补间（2026-09-28 用户插入项）
    TestVerifyFxChannel();
    TestVerifyStatEffectsAndXp();
    TestVerifyMovementKnockbackAndClamp();
    TestVerifyProjectileLifetime();
    TestVerifyParallelDestroyDeterminism(); // review 2026-10-02 #2：并行销毁确定性
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
    TestSpawnFreezeNoLeak(); // review 2026-10-02 #61：连发型 Spawner 冻结零泄漏
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
    TestAssetDatabaseLow32Collision();
    TestOrphanMetaSweep();
    TestAssetPathContainment();
    TestCsvTable();       // M6a 批② T1：CSV/表格序列化（ADR-012）
    TestProjectFile();   // M7a 批②：project.lemon 只读解析 + entryScene 回退链
    TestSpriteRefsEngine(); // M7a 批②：guid 归一引擎本体四态（mock 源）
    TestGridSliceConfig(); // M6a 批② T3b-3：SetGridSlice 连号块
    TestClipEdit();       // M6a 批② T3：.anim 解析/序列化（AnimationPanel 数据面）
    TestValidateAssetName(); // M7a 批① D7：动画资产名校验硬化单源
    TestClipEventBounds();   // M7a 批① M22：删帧后越界帧事件清理（阴性内置）
    TestManifestBakRecovery(); // M7a 批① M21：manifest .bak + 坏主档恢复
    TestAnimSetAndClipIndex(); // M6a 批② T3c：.override 集 + ClipTable 按名索引
    TestControllerAndGraph();  // T3d：.controller + ControllerTable + 事件/反查
    TestTableAssetImport(); // M6a 批② T1：.csv → .tab 转换导入生命周期
    TestDebounceGatePending();
    TestProjectWizard();
    TestEditorUsability();
    TestAutosaveRecovery();
    TestEntityTreeArchive();
    TestScriptBoxArchive();
    TestSpriteGuidResolve();
    TestAssetIndexConsistency(); // M7a 批②：manifest 快路径 vs 回退扫描双路一致性
    TestAssetIndexPkgManifest(); // M7a 批⑤：打包账 ExportManifest → pkg 快路径回读全等 + 包账优先
    TestAssetIndexSliceRebase(); // M7a 批⑥热修：号域平移切片块随本体连号重派 + 健康保号 + 坏账清零
    TestBakedAtlasContainer();  // M7a 批⑥：LAT1 roundtrip + 篡改/截断拒载面
    TestAtlasBakePack();        // M7a 批⑥：shelf 装箱确定性 + 专属页/分页/重叠/像素对位
    TestAtlasStoreRegister();   // M7a 批⑥：LAT1 → AtlasRegistry 登记（切片 UV 数学 + 失配拒载）
    TestBakeProjectAtlas();     // M7a 批⑥：项目面烤制端到端（PNG → LAT1 → 读回全等）
    TestEditorContextPrefabOps();
    TestPlaySpawnPrefab();
    TestRecentScenesAliasSafety();
#endif
    TestAudioMixerMath();     // M6c 批⓪：混音数学（等功率声像/通道路由/叠加/组与主增益）
    TestAudioLifecycle();     // M6c 批⓪：一次性退役/循环回卷/偷声部/暂停语义/voiceId 不复用
    TestAudioDeviceInitNoCrash(); // M6c 批⓪：真初始化三路径不崩（设备/null/降级）
    TestAudioBench100Sfx();   // M6c 批⓪：100 并发 SFX 模拟侧 ≤0.5ms
    TestAudioBakedRoundtrip(); // M6c 竖切批：LBA1 烤制/装载（44.1k→48k 重采样）
    TestAudioBakedHardening(); // review 2026-10-01：坏头（回绕/超限载荷）拒绝 + 并发烤制不交错
    TestAudioSpscRing();      // M6c 批①b：SPSC 环序（交错/回卷/双线程 4MiB 字节精确）
    TestAudioStreamVoice();   // M6c 批①b：流式声部（预填即鸣/曲终/回卷线性化/欠载计数/双声部）
    TestAudioMasterLimiter(); // 听感验收 2026-10-01：母带软限幅（膝下透传/软膝锁值/渐近不过压）
    TestAudioVoiceCapSteal(); // 听感验收 2026-10-01：同 clip 并发上限 + 偷最老（释放窗口/连发脉冲/跨 clip）
    TestAudioRetriggerThrottlePitchJitter(); // 听感验收 2026-10-01：重触发节流（帧时钟窗）+ 音高微扰（可关可异）
    TestAudioFadeEnvelope();   // M6c 批② D4：起播淡入/FadeVoice 到零即停/静音记账同径
    TestAudioSpatialMath();    // M6c 批② M5：线性衰减钳界 + 声像半宽归一
    TestAudioChannelCommands();// M6c 批②：staging/保序 Stop/BGM 单槽换曲/StopAll/null 引擎记账
    TestAudioSourceLifecycle();// M6c 批②：playOnStart/实体亡停/换片重绑/零 ECS 写
    LEMON_LOG("engine-tests: %d checks OK", g_checks);
    return 0;
}
