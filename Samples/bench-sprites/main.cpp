// Lemon M1 验收程序 bench-sprites —— 10 万精灵走完整"提取-排序-合批-录制"流水线
// 与 spike-02（裸实例化对照）同负载；验收：≥60fps（IMMEDIATE），渲染侧 CPU ≤4ms 结构参考。
// 用法：lemon-bench-sprites [--n N] [--frames N] [--immediate] [--validate]
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "Core/Log.h"
#include "Core/Math.h"
#include "Platform/Window.h"
#include "Renderer/Atlas.h"
#include "Renderer/Renderable.h"
#include "Renderer/RHI.h"
#include "Renderer/SpriteBatcher.h"

using namespace lemon;
using namespace lemon::rhi;
using namespace lemon::renderer;

int main(int argc, char** argv) {
    uint32_t n = 100000;
    int frames = 0;
    bool immediate = false, validate = false;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--n") && i + 1 < argc) n = (uint32_t)std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--frames") && i + 1 < argc) frames = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--immediate")) immediate = true;
        else if (!std::strcmp(argv[i], "--validate")) validate = true;
    }
    std::printf("[lemon] bench-sprites: n=%u\n", n);

    auto window = Window::Create({.title = "Lemon bench-sprites", .width = 1280, .height = 720});
    if (!window) return 1;

    DeviceDesc dd;
    dd.appName = "lemon-bench-sprites";
    dd.debugLayer = validate;
    auto device = Device::Create(dd);
    SwapchainDesc sd;
    sd.nativeWindow = window->NativeHandle();
    sd.present = immediate ? PresentModePref::Immediate : PresentModePref::Fifo;
    if (!device->CreateSwapchain(sd)) return 1;
    device->EnableTimestamps();

    // 资产：默认图集 + 采样器 + 合批器
    AtlasRegistry atlas;
    auto defs = AtlasRegistry::CreateDefaultAtlas(*device, atlas, 0);
    Sampler linearS = device->CreateSampler({});
    Sampler pointS = device->CreateSampler({.min = FilterMode::Point, .mag = FilterMode::Point});
    device->BindSamplerToSlot(linearS, 0);
    device->BindSamplerToSlot(pointS, 1);

    SpriteBatcher batcher;
    batcher.Init(*device, 0, 1);
    device->SavePipelineCache(); // preheat 落盘

    // Renderable 池 + 轨道参数（固定种子可复现；lemon64 = 64px 子纹理）
    constexpr float kSpritePx = 64.0f;
    RenderableManager rm;
    std::vector<float> baseAngle(n), orbitR(n), angSpeed(n), scaleArr(n);
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
        for (uint32_t i = 0; i < n; ++i) {
            baseAngle[i] = rnd() * 6.2831853f;
            orbitR[i] = 8.0f + rnd() * 560.0f;
            angSpeed[i] = (0.2f + rnd() * 1.8f) * (rnd() > 0.5f ? 1.0f : -1.0f);
            scaleArr[i] = (4.0f + rnd() * 10.0f) / kSpritePx; // 像素直径 → 倍率（防填充率爆炸）
            rm.Create({.spriteId = defs.lemon64,
                       .colorBits = palette[i % 5],
                       .blend = (uint8_t)BlendKind::Alpha,
                       .filter = (uint8_t)FilterKind::Linear});
        }
    }

    const auto NowMs = []() {
        return std::chrono::duration<double, std::milli>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    };

    double simT = 0;
    uint64_t frame = 0;
    double emaFrameMs = 0;
    bool watchdogAbort = false;
    double frameMin = 1e9, frameMax = 0, frameSum = 0;
    double extractSum = 0, bakeSum = 0, recordSum = 0, gpuSum = 0;
    uint64_t statFrames = 0;
    double prevMs = NowMs();
    bool running = true;

    while (running) {
        if (!window->PollEvents() || window->IsKeyDown(Key::Escape)) running = false;
        if (frames > 0 && (int)frame >= frames) running = false;
        if (!running) break;
        if (window->TakeResized() && !device->RecreateSwapchain()) continue;

        // --- 60Hz 固定模拟 tick（渲染快于 tick 时 alpha 推进插值）---
        // bench 单线程：每帧推 1/60s 模拟时间，alpha 恒 0.5（满帧率插值路径）
        rm.BeginSimTick();
        const uint32_t w = device->SwapchainWidth(), h = device->SwapchainHeight();
        float cx = w * 0.5f, cy = h * 0.5f;
        for (uint32_t i = 0; i < n; ++i) {
            float a = baseAngle[i] + (float)simT * angSpeed[i];
            rm.SetTransform(i + 1, {cx + std::cos(a) * orbitR[i], cy + std::sin(a) * orbitR[i] * 0.62f},
                            a * 2.0f, {scaleArr[i], scaleArr[i]});
        }
        simT += 1.0 / 60.0;

        // --- 渲染帧 ---
        AcquireResult acq = device->AcquireNextImage();
        if (acq.deviceLost || acq.needsRecreate) {
            if (acq.deviceLost || !device->RecreateSwapchain()) continue;
        }
        CommandList& cl = device->BeginFrame();

        rm.SetViewport({cx, cy}, w * 0.5f, h * 0.5f, 150.0f); // 02 §1 kMargin=150px
        double e0 = NowMs();
        auto packets = rm.Extract(atlas, 0.5f);
        double e1 = NowMs();
        batcher.Bake(atlas, packets);
        double e2 = NowMs();

        const float clear[4] = {0.06f, 0.07f, 0.10f, 1.0f};
        cl.BeginPass(device->SwapchainFormat(), w, h, clear);
        cl.SetViewportScissor(w, h);
        Mat3x2 vp = Mat3x2::Ortho({cx, cy}, w * 0.5f, h * 0.5f);
        batcher.Record(cl, vp);
        cl.EndPass();
        double e3 = NowMs();

        bool needRe = false, lost = false;
        device->EndFrameAndPresent(needRe, lost);
        if (lost || needRe) {
            if (lost || !device->RecreateSwapchain()) continue;
        }
        batcher.AdvanceFrame();

        double now = NowMs();
        if (frame > 0) {
            double ms = now - prevMs;
            emaFrameMs = emaFrameMs == 0 ? ms : 0.9 * emaFrameMs + 0.1 * ms;
            if (frame > 10 && emaFrameMs > 250.0) { // 填充率失控保护：宁可中止也不拖死桌面
                watchdogAbort = true;
                running = false;
                break;
            }
            frameMin = std::min(frameMin, ms);
            frameMax = std::max(frameMax, ms);
            frameSum += ms;
            extractSum += e1 - e0;
            bakeSum += e2 - e1;
            recordSum += e3 - e2;
            FrameTiming ft = device->LastFrameTiming();
            if (ft.valid) gpuSum += ft.gpuMs;
            ++statFrames;
        }
        prevMs = now;
        ++frame;
    }

    if (statFrames > 0) {
        double nf = (double)statFrames;
        std::printf(
            "[lemon] bench-sprites: n=%u frames=%llu fps=%.1f (avg %.2fms min %.2f max %.2f)\n"
            "        extract+sort=%.3fms bake=%.3fms record=%.3fms | renderCPU=%.3fms gpu=%.3fms\n"
            "        visible=%u/%u batches=%u present=%s\n",
            n, (unsigned long long)frame, 1000.0 * nf / frameSum, frameSum / nf, frameMin, frameMax,
            extractSum / nf, bakeSum / nf, recordSum / nf,
            (extractSum + bakeSum + recordSum) / nf, gpuSum / nf, rm.LastStats().visible, n,
            batcher.LastBatchCount(), device->PresentModeName());
    }
    if (watchdogAbort) {
        std::printf("[lemon] bench-sprites ABORT: EMA frame %.0fms > 250ms (fill-rate overrun?)\n",
                    emaFrameMs);
        return 1;
    }
    std::printf("[lemon] bench-sprites exit OK\n");
    return 0;
}
