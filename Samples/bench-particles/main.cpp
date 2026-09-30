// Lemon M1 验收程序 bench-particles —— 10 万存活粒子压测（02 §6）
// 验收（08 总表）：粒子 10 万 ≤ 4ms GPU。发射器环绕持续发射，混合模式两条管线
// （additive 光晕 + alpha 圆点）+ 预算钳制 + 看门狗。
// 用法：lemon-bench-particles [--n 预算] [--frames N] [--immediate] [--validate]
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "Core/Log.h"
#include "Core/Math.h"
#include "Platform/Window.h"
#include "Renderer/Atlas.h"
#include "Renderer/Particles.h"
#include "Renderer/Renderable.h"
#include "Renderer/RHI.h"
#include "Renderer/SpriteBatcher.h"

using namespace lemon;
using namespace lemon::rhi;
using namespace lemon::renderer;

int main(int argc, char** argv) {
    uint32_t budget = 100000;
    int frames = 0;
    bool immediate = false, validate = false;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--n") && i + 1 < argc) budget = (uint32_t)std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--frames") && i + 1 < argc) frames = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--immediate")) immediate = true;
        else if (!std::strcmp(argv[i], "--validate")) validate = true;
    }
    std::printf("[lemon] bench-particles: budget=%u\n", budget);

    auto window = Window::Create({.title = "Lemon bench-particles", .width = 1280, .height = 720});
    if (!window) return 1;

    DeviceDesc dd;
    dd.appName = "lemon-bench-particles";
    dd.debugLayer = validate;
    auto device = Device::Create(dd);
    SwapchainDesc sd;
    sd.nativeWindow = window->NativeHandle();
    sd.present = immediate ? PresentModePref::Immediate : PresentModePref::Fifo;
    if (!device->CreateSwapchain(sd)) return 1;
    device->EnableTimestamps();

    AtlasRegistry atlas;
    auto defs = AtlasRegistry::CreateDefaultAtlas(*device, atlas, 0);
    Sampler linearS = device->CreateSampler({});
    Sampler pointS = device->CreateSampler({.min = FilterMode::Point, .mag = FilterMode::Point});
    device->BindSamplerToSlot(linearS, 0);
    device->BindSamplerToSlot(pointS, 1);
    SpriteBatcher batcher;
    batcher.Init(*device, 0, 1);

    // 发射器：屏周 8 个（一半 additive 光晕、一半 alpha 圆点）
    ParticleSystem ps;
    ps.SetBudget(budget);
    struct Emitter {
        EmitterConfig cfg;
        float accum = 0;
    };
    std::vector<Emitter> emitters;
    {
        const uint32_t w = 1280, h = 720;
        for (int i = 0; i < 8; ++i) {
            float a = (float)i / 8.0f * math::kTau;
            Emitter e;
            e.cfg.pos = {w * 0.5f + std::cos(a) * 380.0f, h * 0.5f + std::sin(a) * 260.0f};
            e.cfg.rate = (float)budget / 8.0f / 1.4f; // 池稳态 ~预算（寿命 1.4s）
            e.cfg.lifetimeMin = 1.0f;
            e.cfg.lifetimeMax = 1.8f;
            e.cfg.speedMin = 30.0f;
            e.cfg.speedMax = 150.0f;
            e.cfg.gravity = {0.0f, 40.0f};
            e.cfg.drag = 0.4f;
            if (i % 2 == 0) {
                e.cfg.spriteId = defs.glow128;
                e.cfg.blend = (uint8_t)BlendKind::Additive;
                e.cfg.sizeMin = 6.0f;
                e.cfg.sizeMax = 16.0f;
                e.cfg.color0 = math::PackRGBA(255, 220, 90, 255);
                e.cfg.color1 = math::PackRGBA(255, 90, 30, 0);
            } else {
                e.cfg.spriteId = defs.dotWhite16;
                e.cfg.blend = (uint8_t)BlendKind::Alpha;
                e.cfg.sizeMin = 4.0f;
                e.cfg.sizeMax = 10.0f;
                e.cfg.color0 = math::PackRGBA(200, 255, 200, 255);
                e.cfg.color1 = math::PackRGBA(60, 140, 255, 0);
            }
            emitters.push_back(e);
        }
    }

    const auto NowMs = []() {
        return std::chrono::duration<double, std::milli>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    };

    double lastNow = NowMs();
    uint64_t frame = 0;
    double frameMin = 1e9, frameMax = 0, frameSum = 0;
    double emitSum = 0, simSum = 0, extractSum = 0, bakeSum = 0, recordSum = 0, gpuSum = 0;
    uint64_t statFrames = 0, aliveSum = 0;
    double emaFrameMs = 0;
    bool watchdogAbort = false, running = true;

    while (running) {
        if (!window->PollEvents() || window->IsKeyDown(Key::Escape)) running = false;
        if (frames > 0 && (int)frame >= frames) running = false;
        if (!running) break;
        if (window->TakeResized() && !device->RecreateSwapchain()) continue;

        double now = NowMs();
        float dt = (float)std::min(now - lastNow, 33.0) / 1000.0f; // 钳制 ≤ 33ms
        lastNow = now;

        // --- 粒子模拟（渲染帧 dt，表现层语义）---
        double t0 = NowMs();
        for (uint32_t i = 0; i < emitters.size(); ++i)
            ps.Emit(emitters[i].cfg, dt, 0x1000 + i, emitters[i].accum);
        double t1 = NowMs();
        ps.Simulate({0, 0}, dt);
        double t2 = NowMs();

        AcquireResult acq = device->AcquireNextImage();
        if (acq.deviceLost || acq.needsRecreate) {
            if (!acq.deviceLost) device->RecreateSwapchain(); // 重建后跳过本帧（评审 D2）
            continue;
        }
        CommandList& cl = device->BeginFrame();

        const uint32_t w = device->SwapchainWidth(), h = device->SwapchainHeight();
        double t3 = NowMs();
        auto packets = ps.Extract(atlas, {w * 0.5f, h * 0.5f}, w * 0.5f, h * 0.5f);
        double t4 = NowMs();
        batcher.Bake(atlas, {}, packets);
        double t5 = NowMs();

        const float clear[4] = {0.05f, 0.05f, 0.08f, 1.0f};
        cl.BeginPass(device->SwapchainFormat(), w, h, clear);
        cl.SetViewportScissor(w, h);
        Mat3x2 vp = Mat3x2::Ortho({w * 0.5f, h * 0.5f}, w * 0.5f, h * 0.5f);
        batcher.Record(cl, vp);
        cl.EndPass();
        double t6 = NowMs();

        bool needRe = false, lost = false;
        device->EndFrameAndPresent(needRe, lost);
        if (lost || needRe) {
            if (lost || !device->RecreateSwapchain()) continue;
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
            emitSum += t1 - t0;
            simSum += t2 - t1;
            extractSum += t4 - t3;
            bakeSum += t5 - t4;
            recordSum += t6 - t5;
            FrameTiming ft = device->LastFrameTiming();
            if (ft.valid) gpuSum += ft.gpuMs;
            aliveSum += ps.AliveCount();
            ++statFrames;
        }
        ++frame;
    }

    if (statFrames > 0) {
        double nf = (double)statFrames;
        std::printf(
            "[lemon] bench-particles: budget=%u alive=%llu frames=%llu fps=%.1f (avg %.2fms)\n"
            "        emit=%.3fms sim=%.3fms extract=%.3fms bake=%.3fms record=%.3fms | gpu=%.3fms\n"
            "        batches=%u droppedFull=%u present=%s\n",
            budget, (unsigned long long)(aliveSum / nf), (unsigned long long)frame,
            1000.0 * nf / frameSum, frameSum / nf, emitSum / nf, simSum / nf, extractSum / nf,
            bakeSum / nf, recordSum / nf, gpuSum / nf, batcher.LastBatchCount(),
            ps.LastStats().droppedFull, device->PresentModeName());
    }
    if (watchdogAbort) {
        std::printf("[lemon] bench-particles ABORT: EMA %.0fms > 250ms\n", emaFrameMs);
        return 1;
    }
    std::printf("[lemon] bench-particles exit OK\n");
    return 0;
}
