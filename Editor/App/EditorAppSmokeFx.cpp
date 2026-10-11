// Lemon 编辑器 — --smoke-fx 后处理链冒烟（M8 批④，02 §4 FX Pass）
// 帧链状态机（N≥240；波窗口为状态驱动——帧率无关）：
//   Seed 播种 FX 断言素材（白亮块/暗对照块/棋盘条带）+ SetTier(Low)（链关基线态）
//   f60  基线捕获（Low 档原路径）→ f61 取回存 baseline
//   f70  SetTier(High)（fxChain 开——Med+ 档语义）
//   f100 链开帧捕获 → f101 断言：全帧有差（tone 在跑）+ chainPasses==5（全链五
//        离屏块）→ 帧像素存 chained（波差分对照）
//   f104 SetParams(bloomIntensity=0) → f106 无 bloom 帧捕获 → f107 断言：全效帧
//        probe − 无 bloom 帧 probe > 8（同 tone 两帧差分 = 纯 bloom 光晕信号，
//        tone 对暗背景的微降不干扰）→ 参数恢复
//   f120 SpawnShockwave（棋盘带中心）→ 半径首入 [0.08,0.25] 窗口帧捕获 → 次帧
//        断言：vs chained 差分像素数（扰动可测——内容静止，差分纯由位移产生）
//   状态驱动 波寿终（曾活跃→归零）→ waveGoneOk
//   f160 SetTier(Low)（链关——质量档关 FX 零成本）
//   f220 关链帧捕获 → f221 **与基线逐位同**（memcmp；"关 FX 零成本"机器证）
// 读回口 = ViewportRenderer GameRenderTarget + DebugRecordTextureCapture（smoke-light
// 同款）；编辑态无 Play 无 UI = gameRT 为 fxFinal 纯输出（断言面干净）。
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "App/EditorApp.h"
#include "App/EditorAppSmoke.h"
#include "Components/CoreComponents.h"
#include "Components/RenderComponents.h"
#include "Core/Log.h"
#include "Interaction/ViewportRenderer.h"

namespace lemon::editor {

namespace {
// 拍照帧/档位帧（见头注；改帧号同步回归脚本与批文件）
constexpr uint64_t kFBaseCap = 60, kFBaseFetch = 61;
constexpr uint64_t kFTierHigh = 70;
constexpr uint64_t kFOnCap = 100, kFOnFetch = 101;
constexpr uint64_t kFNoBloomSet = 104, kFNoBloomCap = 106, kFNoBloomFetch = 107;
constexpr uint64_t kFSpawnWave = 120;
constexpr uint64_t kFTierLow = 160;
constexpr uint64_t kFOffCap = 220, kFOffFetch = 221;

// 素材布置（gameCam 编辑态中心 640,360 半高 360 → 视界 [0,1280]×[0,720]）
constexpr Vec2 kPosBright{400, 200};     // 白亮块中心（bloom 源，160×160）
constexpr Vec2 kPosBloomProbe{531, 200}; // 白块右缘外 3px（256 块缘 528；bloom 光晕
                                         // 采样点——1/4 bloom RT 的 1 texel 内未稀释）
constexpr Vec2 kWaveCenterUV{0.5f, 0.78f}; // 棋盘带中心（世界 ~640,560）
constexpr float kWaveMaxRadius = 0.35f, kWaveStrength = 0.025f, kWaveDuration = 0.5f;

inline uint32_t PxAt(const std::vector<uint8_t>& px, uint32_t w, uint32_t x, uint32_t y,
                     int c) {
    return px[((uint64_t)y * w + x) * 4 + (uint32_t)c];
}
} // namespace

void EditorApp::SmokeFxSeed() {
    if (!Launch().smokeFx) return;
    // 播种于 SeedSmokeScene 之后（launch.smoke 已置位——基础冒烟场景打底）。Low 档
    // 落场 = 基线帧走 FX 关原路径（QualityParams.fxChain=false）
    auto& s = ctx_.ActiveScene();
    const uint32_t white = Viewport().Assets().WhiteSprite();
    auto addQuad = [&](const char* tag, Vec2 pos, float wPx, float hPx, uint32_t rgba) {
        ecs::Entity e = ctx_.CreateSpriteEntity(tag, white);
        s.Get<ecs::Transform2D>(e).pos = pos;
        s.Get<ecs::Transform2D>(e).scale = {wPx / 64.0f, hPx / 64.0f}; // sprite 64px 基
        s.Get<ecs::SpriteRenderer>(e).colorRGBA = rgba;
        return e;
    };
    addQuad("FxBright", kPosBright, 256, 256, 0xFFFFFFFFu);       // 白亮块（bloom 源）
    addQuad("FxDark", {880, 200}, 256, 256, 0x46403C3Cu);         // 暗对照（阈值下）
    // 棋盘条带（冲击波位移对照）：y 480..640 交替白/深灰 32px 横条 ×6
    for (int i = 0; i < 6; ++i)
        addQuad("FxStripe", {640, 496.0f + (float)i * 32.0f}, 1280, 32,
                (i & 1) ? 0x46403C3Cu : 0xFFFFFFFFu);
    Viewport().Quality().SetTier(renderer::QualityTier::Low);
    smokeSeeded_ = (uint32_t)s.AliveCount(); // 守恒断言基数（播种后重定）
    LEMON_LOG("smoke-fx: 播种 FX 素材（白亮块/暗对照/棋盘条带，Low 档基线态）");
}

void EditorApp::SmokeFxFrame(uint64_t frame) {
    if (!Launch().smokeFx) return;
    auto& fx = Viewport().PostFx();
    if (frame == kFBaseCap) {
        fx_.capReq = 1; // gameRT 基线（Low 档 = FX 关原路径）
        fx_.capFrame = frame;
    } else if (frame == kFTierHigh) {
        Viewport().Quality().SetTier(renderer::QualityTier::High); // fxChain 开
    } else if (frame == kFOnCap) {
        fx_.capReq = 1; // 链开全效帧
        fx_.capFrame = frame;
    } else if (frame == kFNoBloomSet) {
        // 分离测量：bloomIntensity=0 的对照帧（同 tone 两帧差分 = 纯 bloom 信号）
        renderer::PostFxParams p = Viewport().PostFx().Params();
        p.bloomIntensity = 0.0f;
        Viewport().PostFx().SetParams(p);
    } else if (frame == kFNoBloomCap) {
        fx_.capReq = 1;
        fx_.capFrame = frame;
    } else if (frame == kFSpawnWave) {
        fx.SpawnShockwave(kWaveCenterUV, kWaveMaxRadius, kWaveStrength, kWaveDuration);
        fx_.sawWave = true;
        LEMON_LOG("smoke-fx: f%llu SpawnShockwave(%.2f,%.2f r=%.2f)", (unsigned long long)frame,
                  kWaveCenterUV.x, kWaveCenterUV.y, kWaveMaxRadius);
    } else if (frame == kFTierLow) {
        Viewport().Quality().SetTier(renderer::QualityTier::Low); // 链关（关 FX 零成本）
    } else if (frame == kFOffCap) {
        fx_.capReq = 1; // 关链帧（逐位同断言源）
        fx_.capFrame = frame;
    }
    // 状态驱动位：波寿终（曾活跃 → 归零；帧率无关）
    if (fx_.sawWave && !fx_.waveGoneOk && fx.ActiveWaves() == 0) {
        fx_.waveGoneOk = true;
        LEMON_LOG("smoke-fx: 波寿终回收（ActiveWaves=0）");
    }
}

void EditorApp::SmokeFxCapture(rhi::CommandList& cl, uint64_t frame) {
    if (!Launch().smokeFx) return;
    // 仅捕获请求所属帧录制（fetch 帧 capReq 仍置位时重录 = 取回缓冲晚一帧——
    // smoke-light 同款防御）
    // 波窗口：半径首入 [0.08,0.25] 且尚未差分过 → 置位捕获（链开态 + 波在场）
    if (fx_.sawWave && !fx_.waveProbed && Viewport().Quality().Params().fxChain) {
        renderer::PostFx::WaveView waves[renderer::PostFx::kMaxWaves];
        const uint32_t n = Viewport().PostFx().WavesSnapshot(waves);
        for (uint32_t i = 0; i < n; ++i)
            if (waves[i].radius >= 0.08f && waves[i].radius <= 0.25f) {
                fx_.capReq = 1;
                fx_.capFrame = frame;
                fx_.waveProbed = true; // 只捕一次
                break;
            }
    }
    if (fx_.capReq == 0 || frame != fx_.capFrame) return;
    const rhi::Texture tex = Viewport().GameRenderTarget();
    if (tex.IsValid()) cl.DebugRecordTextureCapture(tex);
}

void EditorApp::SmokeFxSample(uint64_t frame) {
    if (!Launch().smokeFx) return;
    auto fetch = [&](std::vector<uint8_t>& px, uint32_t& w, uint32_t& h) {
        px.clear();
        return device_->DebugFetchTextureCapture(px, w, h) && !px.empty();
    };
    if (frame == kFBaseFetch && fx_.capReq == 1) {
        fx_.baseOk = fetch(fx_.baseline, fx_.baseW, fx_.baseH);
        fx_.capReq = 0;
        LEMON_LOG("smoke-fx: 基线帧（Low 档）%s (%ux%u)", fx_.baseOk ? "OK" : "FAIL",
                  fx_.baseW, fx_.baseH);
    } else if (frame == kFOnFetch && fx_.capReq == 1) {
        std::vector<uint8_t> px;
        uint32_t w = 0, h = 0;
        const bool ok = fetch(px, w, h);
        fx_.capReq = 0;
        if (ok && fx_.baseOk && w == fx_.baseW && h == fx_.baseH) {
            // 链开 vs 基线全帧差（tone S 曲线对非 0.5 值全动 = 必有差）
            size_t diff = 0;
            for (size_t i = 0; i < px.size(); ++i)
                if (px[i] != fx_.baseline[i]) ++diff;
            fx_.chainOnOk = diff > 0;
            fx_.chainPassOk = Viewport().PostFx().LastFrameInfo().chainPasses == 5;
            fx_.chained = px; // 波差分对照（无波链开全效帧）
            LEMON_LOG("smoke-fx: 链开帧 diff=%zu chainPasses=%u",
                      diff, Viewport().PostFx().LastFrameInfo().chainPasses);
        }
    } else if (frame == kFNoBloomFetch && fx_.capReq == 1) {
        // bloom 分离断言：全效帧 vs 无 bloom 帧（同 tone）在白块右缘光晕带的差分。
        // tone 两帧同参 → 差分纯由 bloom 光晕产生（对暗背景的 tone 微降不干扰）
        std::vector<uint8_t> px;
        uint32_t w = 0, h = 0;
        const bool ok = fetch(px, w, h);
        fx_.capReq = 0;
        if (ok && !fx_.chained.empty() && px.size() == fx_.chained.size()) {
            const Vec2 scr = Viewport().WorldToScreen(Viewport().GameCam(), kPosBloomProbe,
                                                      w, h);
            const int px_ = std::clamp((int)scr.x, 0, (int)w - 1);
            const int py_ = std::clamp((int)scr.y, 0, (int)h - 1);
            int maxOn = 0, maxOff = 0;
            for (int c = 0; c < 3; ++c) {
                maxOn = std::max(maxOn, (int)PxAt(fx_.chained, w, px_, py_, c));
                maxOff = std::max(maxOff, (int)PxAt(px, w, px_, py_, c));
            }
            fx_.bloomOk = maxOn > maxOff + 8; // 光晕带净加色（tone 已抵消）
            LEMON_LOG("smoke-fx: bloomProbe 全效=%d 无bloom=%d（Δ>8）→ %s", maxOn, maxOff,
                      fx_.bloomOk ? "OK" : "FAIL");
        }
        // 参数恢复（后续波/关链帧回到全效默认）
        renderer::PostFxParams p = Viewport().PostFx().Params();
        p.bloomIntensity = renderer::PostFxParams{}.bloomIntensity;
        Viewport().PostFx().SetParams(p);
    } else if (fx_.waveProbed && !fx_.waveDiffDone && fx_.capReq == 1 &&
               frame == fx_.capFrame + 1) {
        // 波帧差分：vs chained（同链开态、内容静止——差分纯由 UV 扰动产生）。
        // waveDiffDone 一次位防通用 fetch 判据误吃后续捕获（f221 关链帧同构）
        std::vector<uint8_t> px;
        uint32_t w = 0, h = 0;
        const bool ok = fetch(px, w, h);
        fx_.capReq = 0;
        fx_.waveDiffDone = true;
        if (ok && !fx_.chained.empty() && px.size() == fx_.chained.size()) {
            size_t diff = 0;
            for (size_t i = 0; i < px.size(); ++i)
                if (px[i] != fx_.chained[i]) ++diff;
            fx_.waveOk = diff > 200; // 波环扫过棋盘带 = 大面积位移
            LEMON_LOG("smoke-fx: 波帧差分像素=%zu（阈值 200）→ %s", diff,
                      fx_.waveOk ? "OK" : "FAIL");
        }
    } else if (frame == kFOffFetch && fx_.capReq == 1) {
        std::vector<uint8_t> px;
        uint32_t w = 0, h = 0;
        const bool ok = fetch(px, w, h);
        fx_.capReq = 0;
        if (ok && fx_.baseOk && w == fx_.baseW && h == fx_.baseH) {
            fx_.offIdenticalOk =
                std::memcmp(px.data(), fx_.baseline.data(), px.size()) == 0;
        }
        LEMON_LOG("smoke-fx: 关链帧逐位同 → %s", fx_.offIdenticalOk ? "OK" : "FAIL");
    }
}

bool EditorApp::SmokeFxVerdict() {
    const bool ok = fx_.baseOk && fx_.chainOnOk && fx_.bloomOk && fx_.chainPassOk &&
                    fx_.waveOk && fx_.waveGoneOk && fx_.offIdenticalOk;
    std::printf("[smoke-fx] base=%d chainOn=%d bloom=%d chainPass=%d wave=%d waveGone=%d "
                "offIdentical=%d => %s\n",
                (int)fx_.baseOk, (int)fx_.chainOnOk, (int)fx_.bloomOk,
                (int)fx_.chainPassOk, (int)fx_.waveOk, (int)fx_.waveGoneOk,
                (int)fx_.offIdenticalOk, ok ? "PASS" : "FAIL");
    return ok;
}

} // namespace lemon::editor
