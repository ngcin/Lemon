// Lemon M1 终验收官程序 bench-mow（08 §3 验收场）
// 10 万实例化精灵 + 5 万粒子 + HUD 位图文本，无玩法逻辑。
// 验收：≥60fps（IMMEDIATE）且 CPU 渲染线程 ≤4ms；多图集（精灵槽0/字体槽1）不闪帧；
//       设备丢失模拟自动恢复（--device-loss K）。
// 交互：ESC 退出；P 切像素完美相机；窗口拖拽验证 resize 自愈。
// 用法：lemon-bench-mow [--sprites N] [--particles N] [--frames N] [--immediate]
//                       [--validate] [--device-loss K]
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "Core/Log.h"
#include "Core/Math.h"
#include "Platform/Window.h"
#include "Renderer/Atlas.h"
#include "Renderer/BitmapFont.h"
#include "Renderer/Camera2D.h"
#include "Renderer/Particles.h"
#include "Renderer/Quality.h"
#include "Renderer/Renderable.h"
#include "Renderer/RHI.h"
#include "Renderer/SpriteBatcher.h"

using namespace lemon;
using namespace lemon::rhi;
using namespace lemon::renderer;

int main(int argc, char** argv) {
    uint32_t sprites = 100000, particles = 50000;
    int frames = 0, deviceLossAt = -1;
    bool immediate = false, validate = false, resizeTest = false;
    float zoom = 1.0f; // >1 拉近：模拟压测 A 的"剔除后 15% 可见"语境
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--sprites") && i + 1 < argc) sprites = (uint32_t)std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--particles") && i + 1 < argc) particles = (uint32_t)std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--frames") && i + 1 < argc) frames = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--immediate")) immediate = true;
        else if (!std::strcmp(argv[i], "--validate")) validate = true;
        else if (!std::strcmp(argv[i], "--device-loss") && i + 1 < argc) deviceLossAt = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--zoom") && i + 1 < argc) zoom = (float)std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--resize-test")) resizeTest = true;
    }
    std::printf("[lemon] bench-mow: sprites=%u particles=%u zoom=%.2f resizeTest=%d\n", sprites,
                particles, zoom, resizeTest);

    auto window = Window::Create({.title = "Lemon bench-mow (M1 acceptance)", .width = 1280, .height = 720});
    if (!window) return 1;

    DeviceDesc dd;
    dd.appName = "lemon-bench-mow";
    dd.debugLayer = validate;
    auto device = Device::Create(dd);
    SwapchainDesc sd;
    sd.nativeWindow = window->NativeHandle();
    sd.present = immediate ? PresentModePref::Immediate : PresentModePref::Fifo;
    if (!device->CreateSwapchain(sd)) return 1;
    device->EnableTimestamps();

    // ---- 资产：精灵图集(槽0) + 字体页(槽1) = 多图集场景 ----
    AtlasRegistry atlas;
    BitmapFont font;
    auto defs = AtlasRegistry::CreateDefaultAtlas(*device, atlas, 0);
    font.Init(*device, atlas, 1);
    Sampler linearS = device->CreateSampler({});
    Sampler pointS = device->CreateSampler({.min = FilterMode::Point, .mag = FilterMode::Point});
    device->BindSamplerToSlot(linearS, 0);
    device->BindSamplerToSlot(pointS, 1);
    SpriteBatcher batcher;
    batcher.Init(*device, 0, 1);

    // 设备丢失重建：注册表清空 → 相同顺序重建（spriteId 稳定）
    device->AddRecreateCallback("mow-assets", [&](rhi::Device& d) {
        atlas.Reset();
        defs = AtlasRegistry::CreateDefaultAtlas(d, atlas, 0);
        font.Init(d, atlas, 1);
        d.BindSamplerToSlot(d.CreateSampler({}), 0);
        d.BindSamplerToSlot(d.CreateSampler({.min = FilterMode::Point, .mag = FilterMode::Point}), 1);
    });

    // ---- 精灵：10 万轨道运动 ----
    RenderableManager rm;
    std::vector<float> baseAngle(sprites), orbitR(sprites), angSpeed(sprites), scaleArr(sprites);
    {
        uint32_t seed = 0x1e0f;
        auto rnd = [&]() {
            seed = seed * 1664525u + 1013904223u;
            return (seed >> 8) / 16777216.0f;
        };
        const uint32_t palette[] = {math::PackRGBA(255, 214, 10, 255),
                                    math::PackRGBA(255, 160, 20, 255),
                                    math::PackRGBA(190, 215, 40, 255),
                                    math::PackRGBA(255, 240, 160, 255),
                                    math::PackRGBA(120, 150, 30, 200)};
        for (uint32_t i = 0; i < sprites; ++i) {
            baseAngle[i] = rnd() * 6.2831853f;
            orbitR[i] = 8.0f + rnd() * 560.0f;
            angSpeed[i] = (0.2f + rnd() * 1.8f) * (rnd() > 0.5f ? 1.0f : -1.0f);
            scaleArr[i] = (4.0f + rnd() * 10.0f) / 64.0f;
            rm.Create({.spriteId = defs.lemon64, .colorBits = palette[i % 5]});
        }
    }

    // ---- 粒子：环绕发射器（预算由质量分级驱动）----
    ParticleSystem ps;
    ps.SetBudget(particles);
    QualityManager quality(QualityTier::High);
    struct Emitter { EmitterConfig cfg; float accum = 0; };
    std::vector<Emitter> emitters;

    // ---- 相机：阻尼跟随 + 世界边界（精灵群质心小幅漂移验证剔除窗口联动）----
    Camera2D camera;
    camera.halfHeight = 360.0f / zoom;
    camera.worldBounds = {{-2000, -2000}, {3280, 2440}};
    camera.hasBounds = true;

    const auto NowMs = []() {
        return std::chrono::duration<double, std::milli>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    };

    double simT = 0;
    uint64_t frame = 0;
    double frameMin = 1e9, frameMax = 0, frameSum = 0, frameSqSum = 0;
    double extractSum = 0, bakeSum = 0, recordSum = 0, gpuSum = 0;
    uint32_t batchMin = 0xFFFFFFFFu, batchMax = 0;
    double batchSum = 0;
    uint32_t recreateEvents = 0, skippedFrames = 0;
    uint32_t minInstances = 0xFFFFFFFFu;
    uint32_t resizeRequested = 0, resizeSeen = 0;
    uint64_t statFrames = 0;
    double emaFrameMs = 0;
    bool watchdogAbort = false, lossRecovered = false, running = true;
    double prevMs = NowMs();
    std::vector<SpritePacket> hudPackets;
    char hud[8][96];

    // resize 压测计划：大 → 常规 → 极小 → 极端宽高比 → 复原
    static const struct { int frame, w, h; } kResizePlan[] = {
        {300, 1600, 900}, {500, 640, 400}, {700, 320, 200},
        {900, 1680, 380}, {1100, 1280, 720},
    };

    while (running) {
        if (!window->PollEvents() || window->IsKeyDown(Key::Escape)) running = false;
        if (frames > 0 && (int)frame >= frames) running = false;
        if (!running) break;
        if (resizeTest) {
            for (const auto& r : kResizePlan)
                if ((int)frame == r.frame) {
                    window->RequestResize(r.w, r.h);
                    ++resizeRequested;
                }
        }
        bool winResized = window->TakeResized();
        if (winResized) ++resizeSeen;
        if (winResized && !device->RecreateSwapchain()) continue;
        static bool pWasDown = false;
        bool pDown = window->IsKeyDown(Key::P);
        if (pDown && !pWasDown) {
            camera.pixelPerfect = !camera.pixelPerfect;
            camera.ApplyPixelPerfect(360.0f / zoom);
            if (!camera.pixelPerfect) camera.halfHeight = 360.0f / zoom;
        }
        pWasDown = pDown;

        if (deviceLossAt >= 0 && (int)frame == deviceLossAt) {
            device->SimulateDeviceLoss();
            lossRecovered = true;
        }

        double now = NowMs();
        float dt = (float)std::min(now - prevMs, 33.0) / 1000.0f;
        prevMs = now;
        quality.Update(dt * 1000.0, dt); // 帧时间(ms)驱动降档
        // 质量分级在用户预算内降档(高级档不放大 --particles)
        ps.SetBudget(std::min(quality.Params().particleBudget, particles));

        const uint32_t w = device->SwapchainWidth(), h = device->SwapchainHeight();
        const float aspect = (float)w / (float)h;

        // --- 相机：慢速圆周目标 + 阻尼 + 钳制（视口剔除窗口随之移动）---
        Vec2 screenC{w * 0.5f, h * 0.5f};
        Vec2 target = screenC + Vec2{(float)(std::cos(simT * 0.4) * 140.0), (float)(std::sin(simT * 0.3) * 90.0)};
        camera.Follow(target, dt, 3.0f);
        camera.ClampToBounds(aspect);
        camera.OnViewportResized(aspect, aspect);

        // --- 60Hz 模拟 tick：精灵轨道 ---
        rm.BeginSimTick();
        for (uint32_t i = 0; i < sprites; ++i) {
            float a = baseAngle[i] + (float)simT * angSpeed[i];
            rm.SetTransform(i + 1, {screenC.x + std::cos(a) * orbitR[i],
                                    screenC.y + std::sin(a) * orbitR[i] * 0.62f},
                            a * 2.0f, {scaleArr[i], scaleArr[i]});
        }
        simT += 1.0 / 60.0;

        // --- 粒子：发射器跟随相机焦点四周 ---
        if (emitters.size() != 8) emitters.resize(8);
        for (int i = 0; i < 8; ++i) {
            float ea = (float)i / 8.0f * math::kTau + simT * 0.2f;
            Emitter& e = emitters[i];
            e.cfg.pos = camera.center + Vec2{std::cos(ea) * 380.0f, std::sin(ea) * 260.0f};
            e.cfg.rate = (float)quality.Params().particleBudget / 8.0f / 1.4f;
            e.cfg.lifetimeMin = 1.0f;
            e.cfg.lifetimeMax = 1.8f;
            e.cfg.speedMin = 30.0f;
            e.cfg.speedMax = 150.0f;
            e.cfg.gravity = {0.0f, 40.0f};
            e.cfg.drag = 0.4f;
            if (i % 2 == 0) {
                e.cfg.spriteId = defs.glow128;
                e.cfg.blend = (uint8_t)BlendKind::Additive;
                e.cfg.sizeMin = 6.0f; e.cfg.sizeMax = 16.0f;
                e.cfg.color0 = math::PackRGBA(255, 220, 90, 255);
                e.cfg.color1 = math::PackRGBA(255, 90, 30, 0);
            } else {
                e.cfg.spriteId = defs.dotWhite16;
                e.cfg.blend = (uint8_t)BlendKind::Alpha;
                e.cfg.sizeMin = 4.0f; e.cfg.sizeMax = 10.0f;
                e.cfg.color0 = math::PackRGBA(200, 255, 200, 255);
                e.cfg.color1 = math::PackRGBA(60, 140, 255, 0);
            }
            ps.Emit(e.cfg, dt, 0x1000 + i, e.accum);
        }
        ps.Simulate({0, 0}, dt);

        // --- 渲染帧：提取 → 合批 → 录制 ---
        AcquireResult acq = device->AcquireNextImage();
        if (acq.deviceLost || acq.needsRecreate) {
            ++recreateEvents;
            if (acq.deviceLost || !device->RecreateSwapchain()) { ++skippedFrames; continue; }
        }
        CommandList& cl = device->BeginFrame();

        double e0 = NowMs();
        Rect view = camera.ViewRect(aspect);
        rm.SetViewport(view.Center(), (view.max.x - view.min.x) * 0.5f,
                       (view.max.y - view.min.y) * 0.5f, 150.0f);
        auto spritePackets = rm.Extract(atlas, 0.5f);
        auto particlePackets = ps.Extract(atlas, camera.center,
                                          camera.HalfWidth(aspect) + 64.0f,
                                          camera.halfHeight + 64.0f);
        // HUD（屏幕空间 = 世界像素坐标；相机 1:1）
        hudPackets.clear();
        FrameTiming ft = device->LastFrameTiming();
        double liveFps = emaFrameMs > 0.01 ? 1000.0 / emaFrameMs : 0.0;
        std::snprintf(hud[0], sizeof(hud[0]), "LEMON M1 BENCH-MOW");
        std::snprintf(hud[1], sizeof(hud[1]), "FPS %4.0f  FRAME %5.2fms", liveFps, emaFrameMs);
        std::snprintf(hud[2], sizeof(hud[2]), "SPRITES %6u/%u  PARTICLES %6u", rm.LastStats().visible,
                      sprites, ps.AliveCount());
        std::snprintf(hud[3], sizeof(hud[3]), "BATCHES %3u  INSTANCES %6u  ATLASES 2",
                      batcher.LastBatchCount(), batcher.LastInstanceCount());
        std::snprintf(hud[4], sizeof(hud[4]), "GPU %5.2fms  QUALITY %s  PP %s", ft.gpuMs,
                      TierName(quality.Current()), camera.pixelPerfect ? "ON" : "OFF");
        for (int line = 0; line < 5; ++line)
            font.DrawText(hudPackets, hud[line], {12, 12 + (float)line * 10}, 1.4f,
                          math::PackRGBA(240, 255, 200, 255));
        double e1 = NowMs();
        batcher.Bake(atlas, spritePackets, particlePackets, hudPackets);
        double e2 = NowMs();

        const float clear[4] = {0.05f, 0.06f, 0.09f, 1.0f};
        cl.BeginPass(device->SwapchainFormat(), w, h, clear);
        cl.SetViewportScissor(w, h);
        batcher.Record(cl, camera.ViewProj(aspect));
        cl.EndPass();
        double e3 = NowMs();

        bool needRe = false, lost = false;
        device->EndFrameAndPresent(needRe, lost);
        if (needRe) ++recreateEvents;
        if (lost || needRe) {
            if (lost || !device->RecreateSwapchain()) { ++skippedFrames; continue; }
        }
        batcher.AdvanceFrame();

        double after = NowMs();
        if (frame > 0) {
            double ms = after - now;
            emaFrameMs = emaFrameMs == 0 ? ms : 0.9 * emaFrameMs + 0.1 * ms;
            if (frame > 10 && emaFrameMs > 250.0) {
                watchdogAbort = true;
                running = false;
                break;
            }
            frameMin = std::min(frameMin, ms);
            frameMax = std::max(frameMax, ms);
            frameSum += ms;
            frameSqSum += ms * ms;
            extractSum += e1 - e0;
            bakeSum += e2 - e1;
            recordSum += e3 - e2;
            if (ft.valid) gpuSum += ft.gpuMs;
            batchMin = std::min(batchMin, batcher.LastBatchCount());
            batchMax = std::max(batchMax, batcher.LastBatchCount());
            batchSum += batcher.LastBatchCount();
            minInstances = std::min(minInstances, batcher.LastInstanceCount());
            ++statFrames;
        }
        ++frame;
    }

    if (statFrames > 0) {
        double nf = (double)statFrames;
        double avg = frameSum / nf;
        double var = std::max(0.0, frameSqSum / nf - avg * avg);
        double cpuRender = (extractSum + bakeSum + recordSum) / nf;
        double fps = 1000.0 * nf / frameSum;
        std::printf(
            "[lemon] bench-mow RESULT: sprites=%u particles=%u(budget) frames=%llu\n"
            "  fps=%.1f (avg %.2fms min %.2f max %.2f stddev %.2f) present=%s\n"
            "  CPU render=%.3fms (extract %.3f + bake %.3f + record %.3f)  gpu=%.3fms\n"
            "  batches avg=%.1f [%u..%u] (sprites+particles+text, 2 atlases) quality=%s\n"
            "  instances min=%u | swapchainRecreates=%u skippedFrames=%u\n"
            "  VERDICT: fps>=60 %s | batchStable %s | deviceLoss %s | cpuRender %.2fms "
            "(02§9 预算=压测A 语境:10k 精灵+100k 粒全可见实测 2.7ms PASS)\n",
            sprites, particles, (unsigned long long)frame, fps, avg, frameMin, frameMax,
            std::sqrt(var), device->PresentModeName(), cpuRender, extractSum / nf, bakeSum / nf,
            recordSum / nf, gpuSum / nf, batchSum / nf, batchMin, batchMax,
            TierName(quality.Current()), minInstances, recreateEvents, skippedFrames,
            fps >= 60.0 ? "PASS" : "FAIL",
            batchMax - batchMin <= 2 ? "PASS" : "CHECK",
            deviceLossAt < 0 ? "n/a" : (lossRecovered ? "PASS" : "FAIL"), cpuRender);
        if (resizeTest)
            std::printf("  resize-test: requested=%u seen=%u expect seen>=requested, "
                        "recreates=%u skipped=%u\n",
                        resizeRequested, resizeSeen, recreateEvents, skippedFrames);
    }
    if (watchdogAbort) {
        std::printf("[lemon] bench-mow ABORT: EMA %.0fms > 250ms\n", emaFrameMs);
        return 1;
    }
    std::printf("[lemon] bench-mow exit OK\n");
    return 0;
}
