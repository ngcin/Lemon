// Lemon M3.5 集成冒烟 anim-smoke —— 首次打通 "ECS 场景 → Extract 阶段 → RenderableManager → 窗口"。
// SystemPipeline 的 Extract 阶段自 M2 预留以来首次被真实消费（M4 SceneView 的地基预验证）；
// 同时对帧动画机制（spriteId 切换 → UV 换帧）做视觉级验收。程序化生成全部像素，零外部素材。
//
// 画面构成（世界坐标 = 像素，1280×720 基准）：
//   左组 6 实体 —— C++ 换帧通路：AnimatorSystem 推进 time → 冒烟本地 FrameMapSystem 写
//         SpriteRenderer.spriteId（M5 clip 资产表落地前的占位形态，明确不进引擎）。
//   右组 6 实体 —— C# 换帧通路：档① FrameScript 经 NativeApi Read/Write SpriteRenderer
//         （M3 脚本通道的视觉级验收）。两侧 10fps 循环，相位错开成追逐队形。
//   底部 16 帧静止胶片条 —— 帧映射正确性的对照尺（动画体应逐帧对齐胶片条格子）。
// 验收：两组各自循环换帧且互不干扰；胶片条静止；--validate 零报错；ESC 退出。
// 用法：lemon-anim-smoke [--frames N] [--immediate] [--validate] [--no-script]
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Components/CoreComponents.h"
#include "Components/RenderComponents.h"
#include "Core/Log.h"
#include "Core/Math.h"
#include "ECS/Scene.h"
#include "ECS/SystemPipeline.h"
#include "ECS/World.h"
#include "Platform/Window.h"
#include "Renderer/Atlas.h"
#include "Renderer/BitmapFont.h"
#include "Renderer/Renderable.h"
#include "Renderer/RHI.h"
#include "Renderer/SpriteBatcher.h"
#include "Scripting/ScriptHost.h"
#include "Systems/Systems.h"

using namespace lemon;
using namespace lemon::ecs;
using namespace lemon::renderer;

namespace {

constexpr float kDt = 1.0f / 60.0f;
constexpr uint32_t kFrameCount = 16;
constexpr uint32_t kFramePx = 64;
constexpr uint32_t kAtlasW = kFrameCount * kFramePx; // 1024×64 单页
constexpr uint32_t kAtlasH = kFramePx;
constexpr float kFps = 10.0f;                        // 换帧率（C++/C# 两侧一致）
// kSrEnabled / kSrFlipMask：RenderComponents.h 单一来源（flags 位常量）

// 程序化动画帧 f：脉动球（色相随 f 推移）+ 轨道标记点（看旋转方向/连续性）+ 底部进度条
//（宽度直读帧号）。最外圈 1px 保持透明：图集帧格紧密排布，线性采样会在 UV 边缘采到
// 邻格半纹素，留透明边防串色（默认图集同款处置）。
void PutAnimFrame(std::vector<uint8_t>& px, uint32_t f) {
    const float t = (float)f / (float)kFrameCount; // [0,1)
    const float ang = t * 6.2831853f;
    const float rBall = 19.0f + 6.0f * std::sin(ang);
    const int g = 214 - (int)(130 * t);   // 柠檬黄 → 橙红
    const int b = 10 + (int)(40 * t);
    const uint32_t barW = 2 + (uint32_t)((float)(f + 1) * 60.0f / (float)kFrameCount);
    for (uint32_t y = 0; y < kAtlasH; ++y)
        for (uint32_t x = 0; x < kFramePx; ++x) {
            uint8_t* p = &px[((size_t)y * kAtlasW + (size_t)f * kFramePx + x) * 4];
            const float dx = (float)x + 0.5f - 32.0f, dy = (float)y + 0.5f - 32.0f;
            const float d = std::sqrt(dx * dx + dy * dy);
            if (x == 1 || x == kFramePx - 2 || y == 1 || y == kAtlasH - 2) { // 格子描边
                p[0] = p[1] = p[2] = 255; p[3] = 60; continue;
            }
            const float mx = dx - std::cos(ang) * 25.0f, my = dy - std::sin(ang) * 25.0f;
            if (mx * mx + my * my < 16.0f) { // 轨道标记点
                p[0] = p[1] = p[2] = 255; p[3] = 255; continue;
            }
            if (d < rBall) { // 脉动球
                const float lit = 1.0f - d / rBall;
                p[0] = (uint8_t)(235 + 20 * lit);
                p[1] = (uint8_t)((float)g * (0.75f + 0.25f * lit));
                p[2] = (uint8_t)((float)b * (0.75f + 0.25f * lit));
                p[3] = 255; continue;
            }
            if (y >= 56 && y <= 60 && x >= 2 && x < barW) { // 进度条（帧号直读）
                p[0] = p[1] = p[2] = 255; p[3] = 230; continue;
            }
            p[0] = p[1] = p[2] = 0; p[3] = 0;
        }
}

// ---------------------------------------------------- 冒烟本地系统（不进引擎）----

// 轨道运动：绕组心慢轨道，让提取通路每帧吃到变化的 Transform2D。rot 兼作轨道相位
//（装配期布好初相位，实体间错开）。单实例管全部组——管线系统名 = 身份键（profile/
// RNG 子流/After 依赖），同逻辑注册两个同名实例会被 "duplicate system" 断言拒绝。
class OrbitMotionSystem final : public ISystem {
public:
    struct Group {
        uint32_t team;
        Vec2 center;
        float radius, angSpeed;
    };
    explicit OrbitMotionSystem(std::vector<Group> groups) : groups_(std::move(groups)) {}
    const char* Name() const override { return "OrbitMotion"; }
    void Tick(World& world, Scene& scene, float dt) override {
        (void)world;
        for (auto [ent, mt, tf, an] : scene.View<Meta, Transform2D, Animator2D>().each()) {
            (void)ent, (void)an;
            for (const Group& g : groups_) {
                if (mt.team != g.team) continue;
                tf.rot += g.angSpeed * dt;
                tf.pos = g.center + Vec2{std::cos(tf.rot), std::sin(tf.rot)} * g.radius;
            }
        }
    }

private:
    std::vector<Group> groups_;
};

// 帧映射（M5 clip 资产表落地前的占位替身）：AnimatorSystem 已推进 time ∈ [0,1)，
// 这里线性映射到帧号写 spriteId。冒烟约定：Animator2D.speed==0 = 脚本自管帧（跳过）；
// clipId 暂存动画页基 spriteId（正式语义随 M5 clip 表重新定义）。
class FrameMapSystem final : public ISystem {
public:
    const char* Name() const override { return "CppFrameMap"; }
    const char* After() const override { return "Animator"; }
    void Tick(World& world, Scene& scene, float dt) override {
        (void)world, (void)dt;
        for (auto [ent, an, sr] : scene.View<Animator2D, SpriteRenderer>().each()) {
            (void)ent;
            if (an.speed <= 0.0f) continue;
            const uint16_t f = (uint16_t)(an.time * (float)kFrameCount) % kFrameCount;
            an.curFrame = f;
            sr.spriteId = an.clipId + f;
        }
    }
};

// ECS → Renderable 同步（Extract 阶段首次真实消费，M4 SceneView 地基）。
// 简单全量刷新：每渲染帧 BeginSimTick + 逐实体写变换/颜色/排序/换帧。实体池静态（无销毁
// 路径），renderable 惰性 Create 一次。60 实体量级微秒级；增量/脏标记优化留 M4 按需做。
class SpriteExtractSystem final : public ISystem {
public:
    explicit SpriteExtractSystem(RenderableManager& rm) : rm_(rm) {}
    const char* Name() const override { return "SpriteExtract"; }
    SystemStage Stage() const override { return SystemStage::Extract; }
    void Tick(World& world, Scene& scene, float dt) override {
        (void)world, (void)dt;
        rm_.BeginSimTick();
        for (auto [ent, tf, sr] : scene.View<Transform2D, SpriteRenderer>().each()) {
            (void)ent;
            if (!(sr.flags & kSrEnabled)) continue;
            auto [it, fresh] = ids_.try_emplace(Scene::FromEntt(ent).id, 0);
            if (fresh)
                it->second = rm_.Create({.spriteId = sr.spriteId,
                                         .colorBits = sr.colorRGBA,
                                         .sortingLayer = sr.sortingLayer,
                                         .order = sr.sortOrder,
                                         .blend = (uint8_t)BlendKind::Alpha,
                                         .filter = (uint8_t)FilterKind::Linear,
                                         .flags = (uint8_t)(sr.flags & kSrFlipMask)});
            const uint32_t rid = it->second;
            rm_.SetSprite(rid, sr.spriteId); // 动画换帧通路（Renderable.h:85 预留接口）
            rm_.SetColor(rid, sr.colorRGBA);
            rm_.SetSort(rid, sr.sortingLayer, sr.sortOrder);
            rm_.SetTransform(rid, tf.pos, tf.rot, tf.scale);
        }
    }

private:
    RenderableManager& rm_;
    std::unordered_map<uint64_t, uint32_t> ids_; // Entity.id → renderable id
};

} // namespace

int main(int argc, char** argv) {
    uint32_t perGroup = 6;
    int frames = 0;
    bool immediate = false, validate = false, noScript = false;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--frames") && i + 1 < argc) frames = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--immediate")) immediate = true;
        else if (!std::strcmp(argv[i], "--validate")) validate = true;
        else if (!std::strcmp(argv[i], "--no-script")) noScript = true;
    }
    std::printf("[lemon] anim-smoke: perGroup=%u frames=%d script=%s\n", perGroup, frames,
                noScript ? "off" : "on");

    // ---- C# 档①宿主（先于窗口启动，失败即退——右组验收依赖它，静默降级会假绿）----
    scripting::ScriptHost host;
    bool script = false;
    if (!noScript) {
        script = host.Initialize(nullptr, LEMON_SCRIPT_DIR "/Lemon.Entry.runtimeconfig.json",
                                 LEMON_SCRIPT_DIR "/Lemon.Entry.dll") &&
                 host.LoadUserAssembly(LEMON_SCRIPT_DIR_ANIM "/AnimSmoke.dll");
        if (!script) {
            LEMON_WARN("C# 宿主初始化失败（dotnet 产物缺失？先 cmake --build "
                       "lemon-anim-smoke）；或用 --no-script 跑纯 C++ 档");
            return 1;
        }
    }

    auto window = Window::Create({.title = "Lemon anim-smoke (M3.5 integration)", .width = 1280,
                                  .height = 720});
    if (!window) return 1;

    rhi::DeviceDesc dd;
    dd.appName = "lemon-anim-smoke";
    dd.debugLayer = validate;
    auto device = rhi::Device::Create(dd);
    rhi::SwapchainDesc sd;
    sd.nativeWindow = window->NativeHandle();
    sd.present = immediate ? rhi::PresentModePref::Immediate : rhi::PresentModePref::Fifo;
    if (!device->CreateSwapchain(sd)) return 1;

    // ---- 资产：动画页(槽0，16 帧连续 spriteId) + 字体页(槽1)；设备丢失按同序重建 ----
    AtlasRegistry atlas;
    BitmapFont font;
    uint32_t baseSprite = 0; // 帧 f 的 spriteId = baseSprite + f（AddSprite 登记序连续）
    auto BuildAssets = [&](rhi::Device& d) {
        atlas.Reset();
        std::vector<uint8_t> px((size_t)kAtlasW * kAtlasH * 4, 0);
        for (uint32_t f = 0; f < kFrameCount; ++f) PutAnimFrame(px, f);
        rhi::Texture tex = d.CreateTexture({.width = kAtlasW, .height = kAtlasH,
                                            .debugName = "animPage"});
        d.UploadTexture(tex, px.data(), px.size());
        d.BindTextureToSlot(tex, 0);
        atlas.RegisterAtlas(0, tex, kAtlasW, kAtlasH);
        baseSprite = atlas.AddSprite(0, 0, 0, kFramePx, kFramePx);
        for (uint32_t f = 1; f < kFrameCount; ++f)
            atlas.AddSprite(0, f * kFramePx, 0, kFramePx, kFramePx);
        font.Init(d, atlas, 1);
        d.BindSamplerToSlot(d.CreateSampler({}), 0);
        d.BindSamplerToSlot(
            d.CreateSampler({.min = rhi::FilterMode::Point, .mag = rhi::FilterMode::Point}), 1);
    };
    BuildAssets(*device);
    device->AddRecreateCallback("anim-assets", [&](rhi::Device& d) { BuildAssets(d); });

    SpriteBatcher batcher;
    batcher.Init(*device, 0, 1);
    device->SavePipelineCache();

    // ---- 场景：左组 C++ 换帧 / 右组 C# 换帧 / 底部胶片条静止对照 ----
    RenderableManager rm;
    WorldDesc wd;
    wd.seed = 20260919ull;
    World world(wd);
    if (script) world.SetScriptBackend(&host);

    auto& pipe = world.Pipeline();
    pipe.AddSystem(std::make_unique<DestroyCommitSystem>());       // Essential（脚本挂载点）
    pipe.AddSystem(std::make_unique<AnimatorSystem>());            // #12 time 推进
    pipe.AddSystem(std::make_unique<OrbitMotionSystem>(
        std::vector<OrbitMotionSystem::Group>{{1, Vec2{384, 288}, 90.0f, 0.5f},
                                              {2, Vec2{896, 288}, 90.0f, -0.7f}}));
    pipe.AddSystem(std::make_unique<FrameMapSystem>());
    pipe.AddSystem(std::make_unique<CSharpBatchSystem>());         // #14（档① Update 走域线程）
    pipe.AddSystem(std::make_unique<ScriptEventDispatchSystem>()); // #15
    pipe.AddSystem(std::make_unique<SpriteExtractSystem>(rm));     // Extract 阶段（渲染侧驱动）
    pipe.ResolveOrder();

    Scene& s = world.CreateScene("anim-smoke");
    world.SetActiveScene(&s);

    const float kTau = 6.2831853f;
    Entity cppProbe = Entity::Null(), csProbe = Entity::Null(); // 换帧自检采样点（每组首实体）
    for (uint32_t g = 0; g < 2; ++g) { // g=0 左组(C++,team1) g=1 右组(C#,team2)
        const uint32_t team = g + 1;
        for (uint32_t i = 0; i < perGroup; ++i) {
            Entity e = s.Create();
            const float a0 = (float)i / (float)perGroup * kTau;
            Transform2D tf;
            tf.pos = (g ? Vec2{896, 288} : Vec2{384, 288}) +
                     Vec2{std::cos(a0), std::sin(a0)} * 90.0f;
            tf.rot = a0;
            s.Emplace<Transform2D>(e, tf);
            s.Emplace<Meta>(e).team = team;
            Animator2D an;
            an.clipId = baseSprite;              // 冒烟契约：基 spriteId 存 clipId
            an.time = (float)i / (float)kFrameCount; // 相位错开 → 追逐队形
            an.speed = g == 0 ? kFps / (float)kFrameCount : 0.0f; // 0 = 脚本自管帧
            an.loop = 1;
            s.Emplace<Animator2D>(e, an);
            SpriteRenderer sr;
            sr.spriteId = baseSprite + i % kFrameCount;
            sr.flags = kSrEnabled;
            s.Emplace<SpriteRenderer>(e, sr);
            if (i == 0) (g == 0 ? cppProbe : csProbe) = e;
            if (g == 1 && script) host.AttachBehaviour(world, s, e, 0); // FrameScript（typeId 0）
        }
    }
    for (uint32_t f = 0; f < kFrameCount; ++f) { // 胶片条：无 Animator2D/Meta → 不动不被映射
        Entity e = s.Create();
        Transform2D tf;
        tf.pos = {640.0f - 15.0f * 48.0f + (float)f * 48.0f, 600.0f};
        tf.scale = {0.625f, 0.625f}; // 64px 帧 → 40px 格
        s.Emplace<Transform2D>(e, tf);
        SpriteRenderer sr;
        sr.spriteId = baseSprite + f;
        sr.flags = kSrEnabled;
        s.Emplace<SpriteRenderer>(e, sr);
    }

    // ---- 主循环：60Hz 固定步模拟 → Extract 阶段（渲染侧驱动）→ 提取/合批/录制 ----
    const auto NowMs = []() {
        return std::chrono::duration<double, std::milli>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    };
    uint64_t frame = 0;
    double frameSum = 0, stepSum = 0, extractSum = 0;
    uint32_t statFrames = 0;
    double prevMs = NowMs();
    bool running = true;
    std::vector<SpritePacket> textPackets;
    textPackets.reserve(512);

    // 换帧自检：每秒采样两侧 probe 的 spriteId（渲染通路消费的就是这个组件值；
    // 出现多个不同值 = 组件 → UV 换帧链路在推进，headless 也可验收）
    std::vector<uint32_t> cppSamples, csSamples;

    while (running) {
        if (!window->PollEvents() || window->IsKeyDown(Key::Escape)) running = false;
        if (frames > 0 && (int)frame >= frames) running = false;
        if (!running) break;
        if (window->TakeResized() && !device->RecreateSwapchain()) continue;

        const double t0 = NowMs();
        world.Step(kDt); // Essential + FixedTick（动画推进/帧映射/C# Update）
        const double t1 = NowMs();

        rhi::AcquireResult acq = device->AcquireNextImage();
        if (acq.deviceLost || acq.needsRecreate) {
            if (!acq.deviceLost) device->RecreateSwapchain(); // 重建后跳过本帧（评审 D2）
            continue;
        }
        rhi::CommandList& cl = device->BeginFrame();
        const uint32_t w = device->SwapchainWidth(), h = device->SwapchainHeight();
        const float cx = w * 0.5f, cy = h * 0.5f;

        rm.SetViewport({cx, cy}, w * 0.5f, h * 0.5f, 150.0f);
        world.Pipeline().RunStage(world, s, SystemStage::Extract, kDt);
        const double t2 = NowMs();
        auto packets = rm.Extract(atlas, 1.0f); // 刚 Step 完取本 tick 精确态（vsync 1:1）

        textPackets.clear();
        const uint32_t ink = math::PackRGBA(240, 255, 200, 255);
        const char* labelL = "C++ Animator+FrameMap";
        const char* labelR = script ? "C# FrameScript (NativeApi Write)" : "C# off (--no-script)";
        font.DrawText(textPackets, labelL,
                      {384.0f - font.TextWidth(labelL, 1.6f) * 0.5f, 128.0f}, 1.6f, ink);
        font.DrawText(textPackets, labelR,
                      {896.0f - font.TextWidth(labelR, 1.6f) * 0.5f, 128.0f}, 1.6f, ink);
        font.DrawText(textPackets, "filmstrip f0..f15",
                      {640.0f - font.TextWidth("filmstrip f0..f15", 1.6f) * 0.5f, 552.0f}, 1.6f,
                      ink);

        batcher.Bake(atlas, packets, {}, textPackets);

        const float clear[4] = {0.06f, 0.07f, 0.10f, 1.0f};
        cl.BeginPass(device->SwapchainFormat(), w, h, clear);
        cl.SetViewportScissor(w, h);
        batcher.Record(cl, Mat3x2::Ortho({cx, cy}, w * 0.5f, h * 0.5f));
        cl.EndPass();

        bool needRe = false, lost = false;
        device->EndFrameAndPresent(needRe, lost);
        if (lost || needRe) {
            if (lost || !device->RecreateSwapchain()) continue;
        }
        batcher.AdvanceFrame();

        const double now = NowMs();
        if (frame > 0) {
            frameSum += now - prevMs;
            stepSum += t1 - t0;
            extractSum += t2 - t1;
            ++statFrames;
        }
        prevMs = now;
        ++frame;
        // 30 帧一采样：默认 --frames 120 采 4 点 ≥ Distinct>=3 判据下限（09 §9
        // 门禁分流②，M7a 批⑧ 修——原 60 间隔下 120 帧仅 2 采样 = 确定性误报）
        if (frame % 30 == 0 && cppProbe.id) {
            cppSamples.push_back(s.Get<SpriteRenderer>(cppProbe).spriteId);
            if (csProbe.id && script) csSamples.push_back(s.Get<SpriteRenderer>(csProbe).spriteId);
        }
    }

    const auto Distinct = [](const std::vector<uint32_t>& v) {
        return (uint32_t)std::unordered_set<uint32_t>(v.begin(), v.end()).size();
    };
    bool framesAdvanced = Distinct(cppSamples) >= 3 && (!script || Distinct(csSamples) >= 3);
    std::printf("[lemon] anim-smoke frame-advance check: cpp=[%u,%u,%u,...] cs=[%u,%u,%u,...] "
                "=> %s\n",
                cppSamples.size() ? cppSamples[0] : 0, cppSamples.size() > 1 ? cppSamples[1] : 0,
                cppSamples.size() > 2 ? cppSamples[2] : 0, csSamples.size() ? csSamples[0] : 0,
                csSamples.size() > 1 ? csSamples[1] : 0, csSamples.size() > 2 ? csSamples[2] : 0,
                framesAdvanced ? "ADVANCING" : "STALLED");
    if (!framesAdvanced) {
        std::printf("[lemon] anim-smoke FAIL: spriteId not advancing\n");
        return 1;
    }

    if (statFrames > 0) {
        const double nf = (double)statFrames;
        std::printf("[lemon] anim-smoke: frames=%llu fps=%.1f (avg %.2fms)\n"
                    "        step=%.3fms (sim+C#) extractStage=%.3fms | visible=%u batches=%u\n",
                    (unsigned long long)frame, 1000.0 * nf / frameSum, frameSum / nf,
                    stepSum / nf, extractSum / nf, rm.LastStats().visible,
                    batcher.LastBatchCount());
        for (const char* name : {"Animator", "CppFrameMap", "CSharpBatch", "SpriteExtract"}) {
            if (const SystemProfile* p = world.Pipeline().FindProfile(name))
                std::printf("        %-14s %7.4fms (max %7.4f)\n", p->name, p->lastMs, p->maxMs);
        }
    }
    std::printf("[lemon] anim-smoke exit OK\n");
    return 0;
}
