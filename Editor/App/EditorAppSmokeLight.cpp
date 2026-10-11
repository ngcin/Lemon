// Lemon 编辑器 — --smoke-light 光贴图冒烟（M8 批②；ADR-019 M1/M3 + 02 §8）
// 帧链状态机（N≥240）：
//   f60  基线捕获（灯全禁用 = 关灯路径）→ f61 取回存 baseline
//   f70  使能全灯（Global+Point×2 混合+Spot Subtract+blob 提取在场）
//   f100 开灯帧捕获 → f101 断言：三段式在跑（vs 基线有差）/ Plan（drawLights==3 +
//        ambient≠白）/ lightRT 全分辨率档尺寸
//   f102 lightRT 捕获 → f103 光斑 GPU 断言：lightRT 灯中心 vs 远角 maxΔ>30
//        （lightRT 无场景内容——判据免疫场景污染；gameRT 同点位量的是精灵内容差，弃用）
//   f110/126/142 SetTier Low/Med/High → f118/134/150 lightRT 尺寸实测（¼/½/全）
//   f160 选中 Spot + Ctrl 加选 blob → f185 sceneRT 捕获 → f186 gizmo 圈/楔形/虚框像素
//   f200 全灯禁用 → f220 关灯帧捕获 → f221 **与基线逐位同**（关灯零成本机器证）
// 读回口 = ViewportRenderer Game/Scene/LightRenderTarget + DebugRecordTextureCapture
//（ViewportRenderer.h:151-154 口径）；裁决只回退出码（证据行帧内打印）。
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "App/EditorApp.h"
#include "App/EditorAppSmoke.h"
#include "Components/CoreComponents.h"
#include "Components/LightComponents.h"
#include "Core/Log.h"
#include "Interaction/ViewportRenderer.h"

namespace lemon::editor {

namespace {
// 拍照帧/使能帧/断言帧（见头注；改帧号同步回归脚本与批文件）
constexpr uint64_t kFBaseCap = 60, kFBaseFetch = 61;
constexpr uint64_t kFLightsOn = 70;
constexpr uint64_t kFOnCap = 100, kFOnFetch = 101;
constexpr uint64_t kFLightRtCap = 102, kFLightRtFetch = 103;
constexpr uint64_t kFTierLow = 110, kFTierLowChk = 118;
constexpr uint64_t kFTierMed = 126, kFTierMedChk = 134;
constexpr uint64_t kFTierHigh = 142, kFTierHighChk = 150;
constexpr uint64_t kFSelect = 160;
constexpr uint64_t kFGizmoCap = 185, kFGizmoFetch = 186;
constexpr uint64_t kFLightsOff = 200;
constexpr uint64_t kFOffCap = 220, kFOffFetch = 221;

// 灯布置（gameCam 中心 640,360 zoom1 半高 360 → 视界 [0,1280]×[0,720]）
constexpr Vec2 kPosPointAdd{640, 360}; // 光斑断言点（lightRT 无场景内容，灯心即取）
constexpr Vec2 kPosPointScr{880, 500};
constexpr Vec2 kPosSpot{400, 180};
constexpr Vec2 kPosBlob{760, 420};
constexpr Vec2 kPosOutside{1240, 80}; // 远离全部光源（断言对照点）

inline uint32_t PxAt(const std::vector<uint8_t>& px, uint32_t w, uint32_t x, uint32_t y,
                     int c) {
    return px[((uint64_t)y * w + x) * 4 + (uint32_t)c];
}
} // namespace

void EditorApp::SmokeLightSeed() {
    if (!Launch().smokeLight) return;
    // 播种于 SeedSmokeScene 之后（基础冒烟场景 = 精灵背景；launch.smoke 已置位）。
    // 全灯 flags=0 落场：基线帧走关灯路径（提取滤禁用 → AliveLightCount==0）
    auto& s = ctx_.ActiveScene();
    auto addLight = [&](const char* tag, Vec2 pos) -> ecs::Entity {
        ecs::Entity e = ctx_.CreateEntity(tag);
        s.Get<ecs::Transform2D>(e).pos = pos;
        return e;
    };
    light_.global = addLight("GlobalLight", kPosPointAdd);
    {
        ecs::Light2D& lt = s.Emplace<ecs::Light2D>(light_.global);
        lt.type = ecs::kLightTypeGlobal;
        lt.color = 0xFFD6F4FFu; // 暖白（r255 g244 b214 a255；r|g<<8|b<<16|a<<24 打包）
        lt.intensity = 0.35f; // 留暗：ambient r 不得饱和（LDR 钳 1）——红光池需要余量
        lt.flags = 0;
    }
    light_.pointAdd = addLight("PointAdd", kPosPointAdd);
    {
        ecs::Light2D& lt = s.Emplace<ecs::Light2D>(light_.pointAdd);
        lt.type = ecs::kLightTypePoint;
        lt.blend = ecs::kLightBlendAdditive;
        lt.color = 0xFF0000FFu; // 纯红
        lt.intensity = 2.0f;
        lt.range = 220.0f;
        lt.flags = 0;
    }
    light_.pointScr = addLight("PointScr", kPosPointScr);
    {
        ecs::Light2D& lt = s.Emplace<ecs::Light2D>(light_.pointScr);
        lt.type = ecs::kLightTypePoint;
        lt.blend = ecs::kLightBlendScreen;
        lt.color = 0xFFFFFF00u; // 青
        lt.intensity = 1.5f;
        lt.range = 260.0f;
        lt.flags = 0;
    }
    light_.spotSub = addLight("SpotSub", kPosSpot);
    {
        ecs::Light2D& lt = s.Emplace<ecs::Light2D>(light_.spotSub);
        lt.type = ecs::kLightTypeSpot;
        lt.blend = ecs::kLightBlendSubtract;
        lt.color = 0xFFFFFFFFu;
        lt.intensity = 0.6f;
        lt.range = 320.0f;
        lt.angle = 1.57079632f; // 90° 张角，朝 +X（rot 0）
        lt.innerRatio = 0.5f;
        lt.flags = 0;
    }
    light_.blob = addLight("Blob", kPosBlob);
    {
        ecs::BlobShadow2D& bs = s.Emplace<ecs::BlobShadow2D>(light_.blob);
        bs.radius = 48.0f;
        bs.aspect = 1.4f;
        bs.opacity = 0.5f;
    }
    smokeSeeded_ = (uint32_t)s.AliveCount(); // 基础冒烟守恒断言基数（SeedSmokeScene 已置 5——灯播种后重定）
    LEMON_LOG("smoke-light: 播种 5 实体（Global/PointAdd/PointScr/SpotSub/Blob，全禁用）");
}

void EditorApp::SmokeLightFrame(uint64_t frame) {
    if (!Launch().smokeLight) return;
    auto& s = ctx_.ActiveScene();
    auto setLights = [&](uint16_t flags) {
        for (ecs::Entity e : {light_.global, light_.pointAdd, light_.pointScr, light_.spotSub})
            if (s.Alive(e))
                if (ecs::Light2D* lt = s.TryGet<ecs::Light2D>(e)) lt->flags = flags;
    };
    if (frame == kFBaseCap) {
        light_.capReq = 1; // gameRT 基线（关灯）
        light_.capFrame = frame;
    } else if (frame == kFLightsOn) {
        setLights(ecs::kLightEnabled);
        LEMON_LOG("smoke-light: f%llu 使能全灯（三段式开跑）", (unsigned long long)frame);
    } else if (frame == kFOnCap) {
        light_.capReq = 1; // gameRT 开灯帧
        light_.capFrame = frame;
    } else if (frame == kFLightRtCap) {
        light_.capReq = 3; // lightRT（光斑 GPU 断言源——无场景内容）
        light_.capFrame = frame;
    } else if (frame == kFTierLow) {
        Viewport().Quality().SetTier(renderer::QualityTier::Low);
    } else if (frame == kFTierMed) {
        Viewport().Quality().SetTier(renderer::QualityTier::Med);
    } else if (frame == kFTierHigh) {
        Viewport().Quality().SetTier(renderer::QualityTier::High);
    } else if (frame == kFSelect) {
        // 选中 Spot（主选）+ Ctrl 加选 blob：SceneView 灯圈/楔形/椭圆虚框同帧在场
        ctx_.Select(light_.spotSub, false);
        ctx_.Select(light_.blob, true);
    } else if (frame == kFGizmoCap) {
        light_.capReq = 2; // sceneRT（gizmo 像素源）
        light_.capFrame = frame;
    } else if (frame == kFLightsOff) {
        ctx_.ClearSelection();
        setLights(0);
        LEMON_LOG("smoke-light: f%llu 全灯禁用（关灯路径复原）", (unsigned long long)frame);
    } else if (frame == kFOffCap) {
        light_.capReq = 1; // gameRT 关灯帧（逐位同断言源）
        light_.capFrame = frame;
    }
}

void EditorApp::SmokeLightCapture(rhi::CommandList& cl, uint64_t frame) {
    // 仅捕获请求所属帧录制：fetch 帧（f61/f101/f103/f186/f221）capReq 仍置位，
    // 若重录会让取回缓冲是 fetch 帧内容而非声明的捕获帧（静态场景同像素 = 隐性；
    // 组合 --smoke-anim 即假红）
    if (!Launch().smokeLight || light_.capReq == 0 || frame != light_.capFrame) return;
    const rhi::Texture tex = light_.capReq == 1   ? Viewport().GameRenderTarget()
                             : light_.capReq == 2 ? Viewport().SceneRenderTarget()
                                                  : Viewport().LightRenderTarget();
    if (tex.IsValid()) cl.DebugRecordTextureCapture(tex);
    // capReq 清零挪到 Sample（fetch 后）——防本帧末被复用
}

void EditorApp::SmokeLightSample(uint64_t frame) {
    if (!Launch().smokeLight) return;
    auto fetch = [&](std::vector<uint8_t>& px, uint32_t& w, uint32_t& h) {
        px.clear();
        return device_->DebugFetchTextureCapture(px, w, h) && !px.empty();
    };
    if (frame == kFBaseFetch && light_.capReq == 1) {
        light_.baseOk = fetch(light_.baseline, light_.baseW, light_.baseH);
        light_.capReq = 0;
        LEMON_LOG("smoke-light: 基线帧 %s (%ux%u)", light_.baseOk ? "OK" : "FAIL",
                  light_.baseW, light_.baseH);
    } else if (frame == kFOnFetch && light_.capReq == 1) {
        std::vector<uint8_t> px;
        uint32_t w = 0, h = 0;
        const bool ok = fetch(px, w, h);
        light_.capReq = 0;
        if (ok && light_.baseOk && w == light_.baseW && h == light_.baseH) {
            // 三段式在跑：开灯帧 vs 基线有差（ambient 全局染色 + 局部光）
            size_t diff = 0;
            for (size_t i = 0; i < px.size(); ++i)
                if (px[i] != light_.baseline[i]) ++diff;
            light_.onVsBaseOk = diff > 0;
            // 编排决策：3 个可绘光源（Global 进 ambient、SpotSub 在场）+ ambient 非白
            const auto& plan = Viewport().Light().LastPlan();
            light_.planOk = plan.drawLights == 3 &&
                            (plan.ambient[0] != 1.0f || plan.ambient[1] != 1.0f ||
                             plan.ambient[2] != 1.0f);
            LEMON_LOG("smoke-light: 开灯帧 diff=%zu bytes drawLights=%u "
                      "ambient=(%.2f,%.2f,%.2f)",
                      diff, plan.drawLights, plan.ambient[0], plan.ambient[1],
                      plan.ambient[2]);
        }
        // lightRT 尺寸（High 档 = 全分辨率；此刻未动档）
        light_.tierOk = Viewport().LightRenderTarget().IsValid() &&
                        Viewport().LightRenderTargetWidth() == light_.baseW &&
                        Viewport().LightRenderTargetHeight() == light_.baseH;
    } else if (frame == kFLightRtFetch && light_.capReq == 3) {
        // 光斑 GPU 断言：lightRT 灯心 vs 远角。lightRT 只含 ambient+光源四边形
        //（无场景内容）——差值纯由局部光四边形产生，光源段整体不渲则 Δ=0 必红。
        // 期望：红光心 r = clamp(0.35 + 2.0·att) ≈ 1.0 vs 远角 ambient 0.35 → Δ≈166
        std::vector<uint8_t> px;
        uint32_t w = 0, h = 0;
        const bool ok = fetch(px, w, h);
        light_.capReq = 0;
        if (ok && w > 0 && h > 0) {
            const Vec2 under =
                Viewport().WorldToScreen(Viewport().GameCam(), kPosPointAdd, w, h);
            const Vec2 out =
                Viewport().WorldToScreen(Viewport().GameCam(), kPosOutside, w, h);
            const int ux = std::clamp((int)under.x, 0, (int)w - 1);
            const int uy = std::clamp((int)under.y, 0, (int)h - 1);
            const int ox = std::clamp((int)out.x, 0, (int)w - 1);
            const int oy = std::clamp((int)out.y, 0, (int)h - 1);
            int maxDelta = 0;
            for (int c = 0; c < 3; ++c)
                maxDelta = std::max(maxDelta,
                                    std::abs((int)PxAt(px, w, ux, uy, c) -
                                             (int)PxAt(px, w, ox, oy, c)));
            light_.onDiffOk = maxDelta > 30;
            LEMON_LOG("smoke-light: lightRT 灯心/远角 maxΔ=%d（光斑 GPU 在渲）", maxDelta);
        }
    } else if (frame == kFTierLowChk || frame == kFTierMedChk || frame == kFTierHighChk) {
        const float scale = frame == kFTierLowChk ? 0.25f : frame == kFTierMedChk ? 0.5f : 1.0f;
        const uint32_t ew = std::max(8u, (uint32_t)std::lroundf(light_.baseW * scale));
        const uint32_t eh = std::max(8u, (uint32_t)std::lroundf(light_.baseH * scale));
        const uint32_t lw = Viewport().LightRenderTargetWidth(), lh = Viewport().LightRenderTargetHeight();
        const bool ok = Viewport().LightRenderTarget().IsValid() &&
                        std::abs((int)lw - (int)ew) <= 2 && std::abs((int)lh - (int)eh) <= 2;
        light_.tierOk = light_.tierOk && ok;
        LEMON_LOG("smoke-light: 档位 %.2f 期望 %ux%u 实测 %ux%u → %s", scale, ew, eh, lw, lh,
                  ok ? "OK" : "FAIL");
    } else if (frame == kFGizmoFetch && light_.capReq == 2) {
        std::vector<uint8_t> px;
        uint32_t w = 0, h = 0;
        const bool ok = fetch(px, w, h);
        light_.capReq = 0;
        if (ok) {
            // 灯圈/楔形（kLightGizmoColor 255,235,130）与 blob 虚框（200,200,210）；
            // tol 压窄避手柄黄（250,220,90：b 距 40）与网格/标签色
            light_.gizmoN = CountPixelsNear(px, w, h, 255, 235, 130, 20);
            const int blobN = CountPixelsNear(px, w, h, 200, 200, 210, 20);
            light_.gizmoN = (light_.gizmoN >= 40 && blobN >= 20) ? light_.gizmoN : 0;
            LEMON_LOG("smoke-light: gizmo 圈/楔形像素=%d blob 虚框像素=%d", light_.gizmoN,
                      blobN);
        }
    } else if (frame == kFOffFetch && light_.capReq == 1) {
        std::vector<uint8_t> px;
        uint32_t w = 0, h = 0;
        const bool ok = fetch(px, w, h);
        light_.capReq = 0;
        if (ok && light_.baseOk && w == light_.baseW && h == light_.baseH) {
            light_.offIdenticalOk = std::memcmp(px.data(), light_.baseline.data(), px.size()) == 0;
        }
        LEMON_LOG("smoke-light: 关灯帧逐位同 → %s", light_.offIdenticalOk ? "OK" : "FAIL");
    }
}

bool EditorApp::SmokeLightVerdict() {
    const bool ok = light_.baseOk && light_.onVsBaseOk && light_.onDiffOk && light_.planOk &&
                    light_.tierOk && light_.gizmoN > 0 && light_.offIdenticalOk;
    std::printf("[smoke-light] base=%d onVsBase=%d onDiff=%d plan=%d tier=%d gizmo=%d "
                "offIdentical=%d => %s\n",
                (int)light_.baseOk, (int)light_.onVsBaseOk, (int)light_.onDiffOk,
                (int)light_.planOk, (int)light_.tierOk, light_.gizmoN,
                (int)light_.offIdenticalOk, ok ? "PASS" : "FAIL");
    return ok;
}

} // namespace lemon::editor
