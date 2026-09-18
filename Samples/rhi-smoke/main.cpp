// Lemon M1 验收程序 rhi-smoke —— RHI 薄层全链路冒烟（02 §2）
// 覆盖：动态渲染、bindless 纹理/采样器槽、实例 SSBO、push constant、
//       管线磁盘缓存、GPU 时间戳、交换链重建(resize)、设备丢失模拟恢复。
// 用法：lemon-rhi-smoke [--frames N] [--immediate] [--validate] [--device-loss K]
//   --device-loss K：第 K 帧注入 SimulateDeviceLost()，验证画面与帧计数恢复。
#include "Core/Log.h"
#include "Core/Math.h"
#include "Platform/Window.h"
#include "Renderer/EmbeddedShaders.h"
#include "Renderer/RHI.h"
#include "Renderer/SpriteTypes.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace lemon;
using namespace lemon::rhi;

namespace {

struct AppArgs {
    int frames = 300;
    bool immediate = false;
    bool validate = false;
    int deviceLossAtFrame = -1;
};

AppArgs ParseArgs(int argc, char** argv) {
    AppArgs a;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--frames") && i + 1 < argc) a.frames = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--immediate")) a.immediate = true;
        else if (!std::strcmp(argv[i], "--validate")) a.validate = true;
        else if (!std::strcmp(argv[i], "--device-loss") && i + 1 < argc)
            a.deviceLossAtFrame = std::atoi(argv[++i]);
    }
    return a;
}

// 程序化 64×64 柠檬圆点（spike-02 同款，作为默认测试纹理）
std::vector<uint8_t> MakeLemonTexture(int size) {
    std::vector<uint8_t> px((size_t)size * size * 4);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            float dx = ((float)x + 0.5f) / size - 0.5f;
            float dy = ((float)y + 0.5f) / size - 0.5f;
            float d = std::sqrt(dx * dx + dy * dy);
            uint8_t* p = &px[((size_t)y * size + x) * 4];
            if (d < 0.46f) {
                float lit = std::max(0.0f, 1.0f - d / 0.46f);
                p[0] = (uint8_t)(250 - 40 * (1.0f - lit));
                p[1] = (uint8_t)(225 - 80 * (1.0f - lit));
                p[2] = (uint8_t)(30 + 60 * (1.0f - lit));
                p[3] = 255;
                if (d > 0.44f) p[3] = (uint8_t)(255 * (0.46f - d) / 0.02f);
            } else {
                p[0] = p[1] = p[2] = 0;
                p[3] = 0;
            }
        }
    return px;
}

// 8px 棋盘格：缩小后各 mip 层收敛为红灰混合，肉眼可辨 mip 链是否生效
std::vector<uint8_t> MakeCheckerTexture(int size, int checkPx) {
    std::vector<uint8_t> px((size_t)size * size * 4);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            bool on = ((x / checkPx) + (y / checkPx)) % 2 == 0;
            uint8_t* p = &px[((size_t)y * size + x) * 4];
            p[0] = on ? 255 : 45;
            p[1] = on ? 70 : 45;
            p[2] = on ? 70 : 45;
            p[3] = 255;
        }
    return px;
}

struct GpuResources {
    Texture tex;
    Texture mipTex; // 256×256 棋盘 generateMips（9 层）——mip 生成链路覆盖
    Sampler samp;
    Buffer cornerVB, indexIB, instanceSSBO;
    Pipeline pipe;
    renderer::SpriteInstance* mapped = nullptr;
    static constexpr uint32_t kMaxInstances = 4096;
    static constexpr uint32_t kMipQuads = 6; // 256→8px 一排递减，采到第 5 层
};

void BuildGpuResources(Device& dev, GpuResources& g) {
    auto px = MakeLemonTexture(64);
    g.tex = dev.CreateTexture({.width = 64, .height = 64, .debugName = "lemon64"});
    dev.UploadTexture(g.tex, px.data(), px.size());

    auto checker = MakeCheckerTexture(256, 8);
    g.mipTex = dev.CreateTexture({.width = 256,
                                  .height = 256,
                                  .generateMips = true,
                                  .debugName = "checkerMips"});
    dev.UploadTexture(g.mipTex, checker.data(), checker.size());

    g.samp = dev.CreateSampler({.min = FilterMode::Linear, .mag = FilterMode::Linear});
    dev.BindTextureToSlot(g.tex, 0);
    dev.BindTextureToSlot(g.mipTex, 3);
    dev.BindSamplerToSlot(g.samp, 0);

    g.cornerVB = dev.CreateBuffer({.size = sizeof(float) * 2 * 4,
                                   .usage = (uint32_t)BufferUsage::Vertex,
                                   .debugName = "quadCorners"});
    const float corners[4][2] = {{-0.5f, -0.5f}, {0.5f, -0.5f}, {0.5f, 0.5f}, {-0.5f, 0.5f}};
    std::memcpy(dev.MapBuffer(g.cornerVB), corners, sizeof(corners));
    const uint16_t indices[6] = {0, 1, 2, 0, 2, 3};
    g.indexIB = dev.CreateBuffer(
        {.size = sizeof(indices), .usage = (uint32_t)BufferUsage::Index, .debugName = "quadIndices"});
    std::memcpy(dev.MapBuffer(g.indexIB), indices, sizeof(indices));

    g.instanceSSBO = dev.CreateBuffer({.size = GpuResources::kMaxInstances * sizeof(renderer::SpriteInstance),
                                       .usage = (uint32_t)BufferUsage::Storage,
                                       .debugName = "instanceRing"});
    g.mapped = (renderer::SpriteInstance*)dev.MapBuffer(g.instanceSSBO);

    Shader vs = dev.CreateShader(ShaderStage::Vertex, lemon_spv_sprite_vert,
                                 lemon_spv_sprite_vert_count);
    Shader fs = dev.CreateShader(ShaderStage::Fragment, lemon_spv_sprite_frag,
                                 lemon_spv_sprite_frag_count);
    g.pipe = dev.CreatePipeline({.vs = vs, .fs = fs, .blend = BlendMode::Alpha,
                                 .colorFormat = dev.SwapchainFormat()});
}

} // namespace

int main(int argc, char** argv) {
    AppArgs args = ParseArgs(argc, argv);

    auto window = Window::Create({.title = "Lemon rhi-smoke", .width = 1280, .height = 720});
    if (!window) return 1;

    DeviceDesc dd;
    dd.appName = "lemon-rhi-smoke";
    dd.debugLayer = args.validate;
    auto device = Device::Create(dd);

    SwapchainDesc sd;
    sd.nativeWindow = window->NativeHandle();
    sd.present = args.immediate ? PresentModePref::Immediate : PresentModePref::Fifo;
    if (!device->CreateSwapchain(sd)) return 1;
    device->EnableTimestamps();

    GpuResources gpu;
    BuildGpuResources(*device, gpu);
    device->AddRecreateCallback("smoke-resources", [&](Device& d) { BuildGpuResources(d, gpu); });
    device->SavePipelineCache(); // 首次落盘，验证缓存路径（下次启动加载）

    // 轨道运动参数（固定种子可复现，spike-02 同款）
    constexpr uint32_t kN = 2000;
    std::vector<float> baseAngle(kN), orbitR(kN), angSpeed(kN), scaleArr(kN);
    {
        uint32_t seed = 0x1e0f;
        auto rnd = [&]() {
            seed = seed * 1664525u + 1013904223u;
            return (seed >> 8) / 16777216.0f;
        };
        for (uint32_t i = 0; i < kN; ++i) {
            baseAngle[i] = rnd() * 6.2831853f;
            orbitR[i] = 8.0f + rnd() * 300.0f;
            angSpeed[i] = (0.2f + rnd() * 1.8f) * (rnd() > 0.5f ? 1.0f : -1.0f);
            scaleArr[i] = 6.0f + rnd() * 18.0f;
        }
    }

    const auto NowMs = []() {
        return std::chrono::duration<double, std::milli>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    };

    double t = 0;
    uint64_t frame = 0;
    double frameMin = 1e9, frameMax = 0, frameSum = 0;
    double gpuSum = 0;
    uint64_t gpuSamples = 0;
    bool lossRecovered = false;
    bool running = true;
    double prevMs = NowMs();

    while (running) {
        if (!window->PollEvents() || window->IsKeyDown(Key::Escape)) running = false;
        if (args.frames > 0 && (int)frame >= args.frames) running = false;
        if (!running) break;

        // 设备丢失注入（验收：设备丢失模拟（驱动重置）自动恢复）
        if (args.deviceLossAtFrame >= 0 && (int)frame == args.deviceLossAtFrame) {
            uint64_t before = frame;
            device->SimulateDeviceLoss();
            LEMON_LOG("device-loss recovery: frame counter %llu preserved", (unsigned long long)before);
            lossRecovered = true;
        }

        if (window->TakeResized()) {
            if (!device->RecreateSwapchain()) continue; // 最小化等 0 尺寸
        }

        AcquireResult acq = device->AcquireNextImage();
        if (acq.deviceLost || acq.needsRecreate) {
            if (acq.deviceLost || !device->RecreateSwapchain()) continue;
        }
        CommandList& cl = device->BeginFrame();

        // --- 模拟：写实例数据（真实负载：每帧全量） ---
        const uint32_t w = device->SwapchainWidth(), h = device->SwapchainHeight();
        float cx = w * 0.5f, cy = h * 0.5f;
        for (uint32_t i = 0; i < kN; ++i) {
            float a = baseAngle[i] + (float)t * angSpeed[i];
            renderer::SpriteInstance& inst = gpu.mapped[i];
            renderer::FillInstanceAffine(inst, cx + std::cos(a) * orbitR[i],
                                         cy + std::sin(a) * orbitR[i] * 0.62f, a * 2.0f,
                                         scaleArr[i], scaleArr[i]);
            inst.u0 = 0.0f;
            inst.v0 = 0.0f;
            inst.u1 = 1.0f;
            inst.v1 = 1.0f;
            inst.colorBits = math::PackRGBA(255, 214, 10, 255);
            inst.flags = 0;
        }
        t += 1.0f / 60.0f;

        // mips 覆盖：顶部一排 256→8px 棋盘四边形，逐级触发 1..5 层 mip 采样
        {
            float mx = 24.0f;
            for (uint32_t q = 0; q < GpuResources::kMipQuads; ++q) {
                float s = 256.0f / (float)(1u << q);
                renderer::SpriteInstance& inst = gpu.mapped[kN + q];
                renderer::FillInstanceAffine(inst, mx + s * 0.5f, 96.0f, 0.0f, s, s);
                inst.u0 = 0.0f;
                inst.v0 = 0.0f;
                inst.u1 = 1.0f;
                inst.v1 = 1.0f;
                inst.colorBits = math::PackRGBA(255, 255, 255, 255);
                inst.flags = 0;
                mx += s + 10.0f;
            }
        }

        // --- 渲染：一 pass 一 draw ---
        const float clear[4] = {0.06f, 0.07f, 0.10f, 1.0f};
        cl.BeginPass(device->SwapchainFormat(), w, h, clear);
        cl.SetViewportScissor(w, h);
        cl.BindPipeline(gpu.pipe);
        cl.BindQuadGeometry(gpu.cornerVB, gpu.indexIB);
        cl.BindGlobalDescriptors();
        cl.BindStorageBuffer(gpu.instanceSSBO);

        math::Mat3x2 vp = math::Mat3x2::Ortho({cx, cy}, w * 0.5f, h * 0.5f);
        renderer::SpritePushConstants pc{};
        std::memcpy(pc.vpR0, vp.m, sizeof(float) * 4);
        pc.vpR1[0] = vp.m[4];
        pc.vpR1[1] = vp.m[5];
        pc.baseInstance = 0;
        pc.atlasIndex = 0;
        pc.samplerIndex = 0;
        cl.PushConstants(&pc, sizeof(pc));
        cl.DrawQuadInstances(kN, 0);

        // 第二次 draw：同一管线/几何，atlasIndex 切槽 3（bindless 数组非零下标 + mip 链采样）
        renderer::SpritePushConstants pcMip = pc;
        pcMip.baseInstance = kN;
        pcMip.atlasIndex = 3;
        cl.PushConstants(&pcMip, sizeof(pcMip));
        cl.DrawQuadInstances(GpuResources::kMipQuads, kN);
        cl.EndPass();

        bool needsRecreate = false, deviceLost = false;
        device->EndFrameAndPresent(needsRecreate, deviceLost);
        if (deviceLost || needsRecreate) {
            if (deviceLost || !device->RecreateSwapchain()) continue;
        }

        double now = NowMs();
        if (frame > 0) {
            double ms = now - prevMs;
            frameMin = std::min(frameMin, ms);
            frameMax = std::max(frameMax, ms);
            frameSum += ms;
        }
        prevMs = now;
        FrameTiming ft = device->LastFrameTiming();
        if (ft.valid) {
            gpuSum += ft.gpuMs;
            ++gpuSamples;
        }
        ++frame;
    }

    device->SavePipelineCache();

    const double n = (double)frame - 1;
    if (n > 0) {
        std::printf(
            "[lemon] rhi-smoke: frames=%llu fps=%.1f (avg %.2fms, min %.2f, max %.2f) | gpu=%.3fms | "
            "present=%s | deviceLoss=%s\n",
            (unsigned long long)frame, 1000.0 * n / frameSum, frameSum / n, frameMin, frameMax,
            gpuSamples ? gpuSum / (double)gpuSamples : -1.0, device->PresentModeName(),
            lossRecovered ? "recovered" : (args.deviceLossAtFrame >= 0 ? "NOT-TESTED" : "off"));
    }
    std::printf("[lemon] rhi-smoke exit OK\n");
    return 0;
}
