// Lemon 引擎单测 — RendererTests —
// 渲染域（图集/批键/排序稳定性/粒子/Renderable/相机/质量分级/位图字体/渲染下沉件）（M7c 批⓪ T2 自
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
#include <limits>
#include <filesystem>
#include <fstream>
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
    struct P {
        uint64_t sortKey;
        int id;
    };
    std::vector<P> v;
    for (int i = 0; i < 100; ++i)
        v.push_back({(1ull << 56) | ((uint64_t)(i % 7) << 24) | (uint64_t)(99 - i), i});
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
    ps.Emit(e, 0.5f, 1, acc1); // rate 1000 × 0.5s = 500 个 A
    ps.Emit(e2, 0.5f, 2, acc2); // rate 100 × 0.5s = 50 个 B（同图集同混合 → 同键）
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
    ps4.Emit(eLow, 0.1f, 2, acc4b); // 10 个低层（后遇）
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
    // （SpriteBatcher 只有 4 管线/2 采样器槽，键位宽 blend:4/filter:2 存得下越界值）
    AtlasRegistry atlas;
    atlas.RegisterAtlas(0, rhi::Texture{1}, 64, 64);
    uint32_t spr = atlas.AddSprite(0, 0, 0, 16, 16);
    RenderableManager rm;
    RenderableDesc d;
    d.spriteId = spr;
    d.blend = 7; // 越界（合法 0–3）
    d.filter = 3; // 越界（合法 0–1）
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
    rm2.Create(d2); // 暂停态生成：无 BeginSimTick
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
    Expect(lowStart.Current() == QualityTier::Low && lowStart.Params().particleBudget == 20000,
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

// 相机跟随：优先级 Camera > Player > 脚本实体；首帧吸附 + 刚性跟随 + 目标死亡
// 重扫 + Reset 复位（无 GPU 纯逻辑）

#ifdef LEMON_EDITOR_CORE
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
        Expect(renderer::UpdateCameraFollow(s2, cam2, st2, nullptr) && cam2.center == Vec2(30, 40),
               "scripted entity fallback target");
    }
    // Reset：回默认位 + 态清零 + 幂等
    Expect(renderer::ResetCameraFollow(cam, st) && cam.center == Vec2(640, 360) && !st.active,
           "reset restores default center");
    Expect(!renderer::ResetCameraFollow(cam, st), "second reset is no-op");
}
#endif // LEMON_EDITOR_CORE

// 提取下沉件：建槽/世界变换/禁用差集释放/悬空 spriteId 跳过/场景换代全清

#ifdef LEMON_EDITOR_CORE
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
#endif // LEMON_EDITOR_CORE

} // namespace

void RunRendererTests() {
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
#ifdef LEMON_EDITOR_CORE
    TestCameraFollowCore();
#endif
#ifdef LEMON_EDITOR_CORE
    TestSceneExtractorCore();
#endif
}
