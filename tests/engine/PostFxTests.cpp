// Lemon 引擎单测 — PostFxTests — 后处理链 CPU 面（M8 批④：纯函数镜像 +
// 冲击波生命机 + bloom 尺寸派生 + 质量档 fxChain 位）。GPU 链路（五离屏块 +
// fxFinal）归两壳 --smoke-fx 像素断言（编辑器 gameRT / lemon-game 交换链）。
#include "TestFramework.h"

#include <cmath>

#include "Renderer/Atlas.h"
#include "Renderer/LightTypes.h"
#include "Renderer/PostFx.h"
#include "Renderer/Quality.h"

using lemon::Vec2;
using lemon::renderer::BloomMipSize;
using lemon::renderer::PostFx;
using lemon::renderer::PostFxParams;
using lemon::renderer::ShockwaveOffset;
using lemon::renderer::ToneApply;

// 冲击波环形偏移：波前峰值 / 远场趋零 / amp=0 恒零（fx_final.frag 同式锚）
void TestShockwaveOffset() {
    ExpectNear(ShockwaveOffset(0.30f, 0.30f, 0.02f), 0.02f, 1e-4f,
               "wavefront (dist==radius) peaks at amp");
    Expect(ShockwaveOffset(0.30f, 0.30f, 0.02f) > ShockwaveOffset(0.34f, 0.30f, 0.02f),
           "offset decays beyond wavefront");
    Expect(ShockwaveOffset(0.30f, 0.30f, 0.02f) > ShockwaveOffset(0.26f, 0.30f, 0.02f),
           "offset decays inside wavefront (gaussian ring)");
    Expect(std::fabs(ShockwaveOffset(2.0f, 0.3f, 0.02f)) < 1e-6f,
           "far field offset ~0");
    Expect(ShockwaveOffset(0.1f, 0.3f, 0.0f) == 0.0f, "amp=0 -> zero offset");
    // 对称性：波前内外等距同profile
    const float out = ShockwaveOffset(0.36f, 0.30f, 0.02f);
    const float in = ShockwaveOffset(0.24f, 0.30f, 0.02f);
    ExpectNear(out, in, 1e-5f, "gaussian ring symmetric around wavefront");
}

// 色调曲线 CPU 镜像：中点带保持 / 高低端拉伸 / 钳域 / 暖偏方向
void TestToneApply() {
    float mid[3] = {0.5f, 0.5f, 0.5f};
    ToneApply(mid, 1.06f, 0.10f);
    // 暖偏后：r 升 b 降（luma=0.5 → +0.05 / −0.025）
    Expect(mid[0] > 0.5f && mid[2] < 0.5f, "mid grey: warm tint r up / b down");
    ExpectNear(mid[0] - 0.5f, 0.05f, 1e-4f, "warm r delta = warmth*luma");
    ExpectNear(0.5f - mid[2], 0.025f, 1e-4f, "warm b delta = warmth*0.5*luma");

    float hi[3] = {1.0f, 1.0f, 1.0f};
    ToneApply(hi, 1.06f, 0.10f);
    Expect(hi[0] == 1.0f && hi[1] <= 1.0f, "white clamps (LDR)");

    float lo[3] = {0.0f, 0.0f, 0.0f};
    ToneApply(lo, 1.06f, 0.10f);
    Expect(lo[0] == 0.0f, "black stays black (S-curve anchored + luma=0 no tint)");

    // 对比度：0.5+ 与 0.5− 两侧远离中点（contrast > 1）
    float up[3] = {0.7f, 0.7f, 0.7f}, dn[3] = {0.3f, 0.3f, 0.3f};
    ToneApply(up, 1.06f, 0.0f);
    ToneApply(dn, 1.06f, 0.0f);
    Expect(up[0] > 0.7f && dn[0] < 0.3f, "contrast>1 pushes both sides from mid");
}

// bloom 链 RT 尺寸派生（1/4·1/8·1/16 + ≥4px 钳）
void TestBloomMipSize() {
    Expect(BloomMipSize(1280, 4) == 320 && BloomMipSize(720, 4) == 180,
           "1/4 of 1280x720 = 320x180");
    Expect(BloomMipSize(1280, 8) == 160 && BloomMipSize(720, 8) == 90, "1/8 mip");
    Expect(BloomMipSize(1280, 16) == 80 && BloomMipSize(720, 16) == 45, "1/16 mip");
    Expect(BloomMipSize(8, 4) == 4 && BloomMipSize(0, 16) == 4, "tiny targets clamp to 4px");
    Expect(BloomMipSize(40, 8) == 5 && BloomMipSize(32, 16) == 4, "floor division clamp");
}

// 质量档 fxChain 位（Low 关 / Med+ 开——"关 FX 零成本"档位语义）
void TestQualityFxChainTier() {
    using lemon::renderer::QualityParams;
    using lemon::renderer::QualityTier;
    using lemon::renderer::TierParams;
    Expect(!TierParams(QualityTier::Low).fxChain, "Low tier: fx chain off");
    Expect(TierParams(QualityTier::Med).fxChain, "Med tier: fx chain on");
    Expect(TierParams(QualityTier::High).fxChain, "High tier: fx chain on");
    // 参数结构默认（调参档基线；实际开关由 TierParams.fxChain 驱动）
    Expect(PostFxParams{}.bloomThreshold > 0.5f, "bloom threshold near-white (LDR ruling)");
}

// 冲击波生命机：半径推进 / 幅度衰减 / 寿终回收 / 满员顶替（纯 CPU——PostFx 的
// 波池管理无 GPU 依赖，可直接驱动）
void TestShockwaveLifecycle() {
    PostFx fx; // 无 Init（设备面未触）：波池 API 纯 CPU
    fx.SpawnShockwave(Vec2{0.5f, 0.5f}, 0.5f, 0.02f, 0.5f);
    Expect(fx.ActiveWaves() == 1, "one active wave after spawn");
    fx.Simulate(0.25f); // 走完一半：radius 0.25，amp = 0.02×(1−0.5) = 0.01
    Expect(fx.ActiveWaves() == 1, "wave alive at half travel");
    fx.Simulate(0.25f); // 走完全程：radius ≥ maxRadius → 回收
    Expect(fx.ActiveWaves() == 0, "wave recycled at full travel");

    fx.SpawnShockwave(Vec2{0.2f, 0.2f}, 0.4f, 0.01f, 1.0f);
    fx.SpawnShockwave(Vec2{0.4f, 0.2f}, 0.4f, 0.01f, 1.0f);
    fx.SpawnShockwave(Vec2{0.6f, 0.2f}, 0.4f, 0.01f, 1.0f);
    fx.SpawnShockwave(Vec2{0.8f, 0.2f}, 0.4f, 0.01f, 1.0f);
    fx.SpawnShockwave(Vec2{0.5f, 0.8f}, 0.4f, 0.01f, 1.0f); // 第 5 波顶替最老
    Expect(fx.ActiveWaves() == PostFx::kMaxWaves, "pool caps at kMaxWaves");

    // 幅度随剩余程线性衰减：半程幅度 = 一半
    fx.Simulate(0.5f); // 每波半程（speed = 0.4，0.5s → radius 0.2 = maxR/2）
    Expect(fx.ActiveWaves() == PostFx::kMaxWaves, "all still alive at half travel");
    fx.Simulate(0.5f);
    Expect(fx.ActiveWaves() == 0, "all recycled after full travel");

    // 非法参数零副作用
    fx.SpawnShockwave(Vec2{}, 0.0f, 0.01f, 1.0f);
    fx.SpawnShockwave(Vec2{}, 0.4f, 0.0f, 1.0f);
    fx.SpawnShockwave(Vec2{}, 0.4f, 0.01f, 0.0f);
    Expect(fx.ActiveWaves() == 0, "invalid spawn args are no-ops");
}

// bindless 槽位对账：FX 段 4 槽与顶部保留段/图集上限自洽（#L20 记账锚）
void TestFxSlotLayout() {
    using lemon::renderer::kAtlasSlotCeiling;
    using lemon::renderer::kFxBloomATexSlot;
    using lemon::renderer::kFxBloomBTexSlot;
    using lemon::renderer::kFxBloomCTexSlot;
    using lemon::renderer::kFxSceneTexSlot;
    using lemon::renderer::kGlowTexSlot;
    using lemon::renderer::kLightRtTexSlot;
    using lemon::renderer::kSceneRtTexSlot;
    Expect(kSceneRtTexSlot == 250 && kLightRtTexSlot == 251 && kGlowTexSlot == 249,
           "existing top reserve intact (glow/scene/light)");
    Expect(kFxSceneTexSlot == 248 && kFxBloomATexSlot == 247 && kFxBloomBTexSlot == 246 &&
               kFxBloomCTexSlot == 245,
           "fx slots packed below existing reserve");
    Expect(kAtlasSlotCeiling == kFxBloomCTexSlot,
           "atlas page ceiling = lowest fx reserve slot (hard guard)");
}

void RunPostFxTests() {
    TestShockwaveOffset();
    TestToneApply();
    TestBloomMipSize();
    TestQualityFxChainTier();
    TestShockwaveLifecycle();
    TestFxSlotLayout();
}
