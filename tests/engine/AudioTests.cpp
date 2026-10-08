// Lemon 引擎单测 — AudioTests — 音频域（M6c
// 全链：混音/生命周期/.baked/流式/限幅/节流/命令通道）（M7c 批⓪ T2 自 engine_tests.cpp
// 按域拆出，函数体逐字节原样搬运； include/using 为全 TU 共享全集——跨域头依赖零编译风险，IWYU
// 精简不做）

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
    for (int i = 0; i < 4800; ++i) ramp[i] = static_cast<int16_t>(i);
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
        if (i == 0) firstId = id;
    }
    Expect(eng2.ActiveVoiceCount() == audio::kMaxVoices, "pool full");
    Expect(eng2.Play(d1, {}) != 0, "steal succeeds when full");
    Expect(!eng2.VoiceAlive(firstId), "oldest oneshot stolen");
    Expect(eng2.ActiveVoiceCount() == audio::kMaxVoices, "count stays at cap");
    eng2.StopAll();
    for (int i = 0; i < audio::kMaxVoices; ++i) eng2.Play(d2, {.loop = true});
    Expect(eng2.Play(d2, {.loop = true}) == 0, "all-loop full pool refuses");

    // 暂停语义（ADR-015 M4）：Sfx 循环挂起、Ui 组（含循环）不挂起，恢复后双声部齐鸣
    audio::AudioEngine eng3;
    eng3.Init({.forceSilent = true});
    const uint32_t p1 = eng3.RegisterClip({pcm100.data(), 100, 1, 0, 0});
    const uint32_t lp = eng3.Play(p1, {.loop = true}); // Sfx 循环
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
    for (int i = 0; i < 100; ++i) eng.Play(c, {.pan = (i % 2 != 0) ? 0.5f : -0.5f});
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
        pcm[i * 2] = (int16_t)(12000.0f * std::sin(i * 0.05f)); // L 正弦
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
        Expect(li.loopStart == 480 && li.loopEnd == 2400, "loop secs → 48k frames (0.01s/0.05s)");
        const std::string clamped = (dir / "clamp.baked").string();
        Expect(audio::BakeAudioFile(wav.c_str(), clamped.c_str(), 0.0f, 99.0f),
               "bake with overlong loop end");
        audio::BakedClipInfo ci;
        std::vector<int16_t> cPcm;
        Expect(audio::LoadBakedClip(clamped.c_str(), cPcm, ci), "load clamped baked");
        Expect(ci.loopEnd == ci.frameCount, "loop end clamped to tail");
    }
    Expect(info.frameCount >= 4700 && info.frameCount <= 4900, "44.1k→48k resampled frame count");
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
        if (audio::BakeAudioFile(wav.c_str(), dst.c_str())) ++okCount;
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
        Expect(!audio::BakeAudioFile(garbage.c_str(), outG.c_str()), "garbage source fails bake");
        int tmpCount = 0;
        for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
            if (it->path().string().find(".tmp") != std::string::npos) ++tmpCount;
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
        for (size_t i = 0; i < wn; ++i) wbuf[i] = uint8_t(((wpos + i) * 31u + 7u) >> 3);
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
            for (size_t i = 0; i < n; ++i) buf[i] = uint8_t(((p + i) * 2654435761ull) >> 24);
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
    Expect(audio::BakeAudioFile(wav.c_str(), looped.c_str(), 0.05f, 0.2f), "bake loop fixture");
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
    for (uint32_t i = 0; i < kFrames; ++i) pcm[i] = int16_t(32000.0f * std::sin(i * 0.05f));
    int maxS = 0;
    for (int16_t s : pcm) maxS = std::max(maxS, std::abs(int(s)));
    const uint32_t clip = eng.RegisterClip(std::move(pcm), 1, kFrames, 0, 0);
    Expect(clip != 0, "limiter clip registered");
    const float unit = maxS / 32768.0f; // 单声部满音量中心声像前
    const float center = 0.70710678f; // 等功率中心每声道
    const auto mixPeak = [&](int voices, float vol) { // 同帧起播 N 份 → 混完取峰
        for (int k = 0; k < voices; ++k) eng.Play(clip, {.volume = vol});
        std::vector<float> out(kFrames * 2);
        eng.MixOffline(out.data(), kFrames); // 一次性 clip 混完即亡
        eng.Tick(); // 回收，案例间状态干净
        float peak = 0;
        for (float s : out) peak = std::max(peak, std::fabs(s));
        return peak;
    };
    const auto softLimit = [](float x) { // 与引擎同式（膝点 0.8）
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
            spcm[i * 2 + 1] = 8000; // R 低幅（膝下）
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
        const float rIn = 2.0f * (8000.0f / 32768.0f) * center; // 叠加后 R 峰（膝下）
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
    const uint32_t clip = eng.RegisterClip(std::vector<int16_t>(48000, 12000), 1, 48000, 0, 0);
    const uint32_t clip2 = eng.RegisterClip(std::vector<int16_t>(48000, 12000), 1, 48000, 0, 0);
    Expect(clip != 0 && clip2 != 0, "cap clips registered");

    // 上限内共存：kMaxVoicesPerClip 个同 clip 循环声部全活
    uint32_t ids[8] = {};
    for (int k = 0; k < audio::kMaxVoicesPerClip; ++k) ids[k] = eng.Play(clip, {.loop = true});
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
    for (int k = 0; k < 20; ++k) eng.Play(clip, {.loop = true});
    eng.AdvanceSilentFrames(480);
    eng.Tick();
    Expect(eng.ActiveVoiceCount() == audio::kMaxVoicesPerClip,
           "burst retrigger keeps live voices at cap");

    // 跨 clip 独立：另一 clip 满编不连坐
    eng.StopAll();
    eng.Tick();
    for (int k = 0; k < audio::kMaxVoicesPerClip; ++k) ids[k] = eng.Play(clip2, {.loop = true});
    const uint32_t otherClip = eng.Play(clip, {.loop = true});
    ok = otherClip != 0 && eng.VoiceAlive(otherClip);
    for (int k = 0; k < audio::kMaxVoicesPerClip; ++k) ok = ok && eng.VoiceAlive(ids[k]);
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
    for (int i = 0; i < 480 * 2; ++i) differ = differ || std::fabs(cap1[i] - cap2[i]) > 1e-6f;
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
    const uint32_t clip2 = eng.RegisterClip(std::vector<int16_t>(48000 * 2, 6000), 2, 48000, 0, 0);
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
    ch.Submit(&eng, l); // 回收死条目
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
    const uint32_t clipB = eng.RegisterClip(std::vector<int16_t>(48000 * 2, 6000), 2, 48000, 0, 0);
    struct Ctx {
        uint64_t guidA, guidB;
        uint32_t a, b;
    } ctx{0xAAAA, 0xBBBB, clipA, clipB};
    world.SetAudioBackend(
        &eng,
        [](uint64_t guid, void* p) -> uint32_t {
            const Ctx* c = static_cast<const Ctx*>(p);
            if (guid == c->guidA) return c->a;
            if (guid == c->guidB) return c->b;
            return 0;
        },
        &ctx);
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

void TestAudioPerClipFxOverrides() {
    // M7c 批②：per-clip 听感覆写——ClipFx 声明优先（浮点 0 = 显式关），哨兵回退
    // 全局。三面：节流窗（覆写更长压过全局 / 0 = 关 / 无覆写继承）/ 并发上限
    //（cap=1 第二发偷第一发；无覆写仍 kMaxVoicesPerClip）/ 微扰（覆写 0 回整数
    // 位精确路径 = 两连播逐位相同；无覆写继承全局相异）
    audio::AudioEngine eng;
    Expect(eng.Init({.forceSilent = true}), "silent init");
    eng.SetRetriggerCooldown(0.05f); // 全局 50ms = 2400 帧（显式，不依赖默认漂移）
    std::vector<int16_t> ramp(48000);
    for (uint32_t i = 0; i < 48000; ++i) ramp[i] = int16_t(i % 32768); // 非常数 PCM：微扰可观测
    std::vector<int16_t> rampCopy = ramp;

    audio::ClipFx fxWide;  fxWide.retriggerCdSec = 0.1f; // 覆写 100ms（压过全局 50ms）
    audio::ClipFx fxOff;   fxOff.retriggerCdSec = 0.0f;  // 显式关（与"继承"可区分）
    const uint32_t wide = eng.RegisterClip(std::move(rampCopy), 1, 48000, 0, 0, fxWide);
    const uint32_t off = eng.RegisterClip(std::vector<int16_t>(48000, 9000), 1, 48000, 0, 0,
                                          fxOff);
    const uint32_t plain = eng.RegisterClip(std::move(ramp), 1, 48000, 0, 0); // 无覆写 = 继承
    Expect(wide && off && plain, "fx clips registered");

    // ---- 节流窗：全局窗过、覆写窗未过仍丢（覆写压过全局的证据位）----
    Expect(eng.Play(wide, {}) != 0, "wide: first accepted");
    eng.AdvanceSilentFrames(2400); // 恰过全局 50ms
    Expect(eng.Play(wide, {}) == 0, "wide: global window passed, override still drops");
    eng.AdvanceSilentFrames(2400); // 恰过覆写 100ms（锚 = 上次被接受的起播）
    Expect(eng.Play(wide, {}) != 0, "wide: past override window accepted");
    eng.StopAll();
    eng.Tick();

    // fx 0 = 该 clip 关节流（同帧连发两响都过）；无覆写 clip 继承全局（第二响丢）
    Expect(eng.Play(off, {}) != 0 && eng.Play(off, {}) != 0, "fx retrigger=0 disables throttle");
    Expect(eng.Play(plain, {}) != 0, "plain: first accepted");
    Expect(eng.Play(plain, {}) == 0, "plain: inherits global window");
    eng.StopAll();
    eng.Tick();

    // ---- 并发上限：cap=1 第二发偷第一发（释放复用 D4 包络，5ms 后亡）----
    audio::ClipFx fxSolo;
    fxSolo.voiceCap = 1;
    const uint32_t solo = eng.RegisterClip(std::vector<int16_t>(48000, 7000), 1, 48000, 0, 0,
                                           fxSolo);
    eng.SetRetriggerCooldown(0); // cap 断言需同 clip 连发（节流让路，TestAudioVoiceCapSteal 同款）
    const uint32_t s1 = eng.Play(solo, {.loop = true});
    const uint32_t s2 = eng.Play(solo, {.loop = true});
    Expect(s1 != 0 && s2 != 0, "solo cap=1: over-cap accepted via steal");
    Expect(eng.ActiveVoiceCount() == 2, "stolen occupies slot during release");
    eng.AdvanceSilentFrames(480); // 10ms > 5ms 释放
    Expect(!eng.VoiceAlive(s1) && eng.VoiceAlive(s2), "cap=1: oldest dies, newcomer survives");
    // 无覆写 clip 仍默认 kMaxVoicesPerClip 个共存（继承证据位）
    eng.StopAll();
    eng.Tick();
    uint32_t ids[audio::kMaxVoicesPerClip] = {};
    bool ok = true;
    for (int k = 0; k < audio::kMaxVoicesPerClip; ++k) {
        ids[k] = eng.Play(plain, {.loop = true});
        ok = ok && ids[k] != 0;
    }
    for (int k = 0; k < audio::kMaxVoicesPerClip; ++k) ok = ok && eng.VoiceAlive(ids[k]);
    Expect(ok, "no-override clip coexists up to kMaxVoicesPerClip");

    // ---- 微扰：全局 0.05 + 覆写 0 → 整数路径两连播逐位相同；无覆写继承相异 ----
    eng.StopAll();
    eng.Tick();
    eng.SetPitchJitter(0.05f);
    audio::ClipFx fxNoJit;
    fxNoJit.pitchJitter = 0.0f;
    std::vector<int16_t> ramp2(48000);
    for (uint32_t i = 0; i < 48000; ++i) ramp2[i] = int16_t(i % 32768);
    const uint32_t noJit = eng.RegisterClip(std::move(ramp2), 1, 48000, 0, 0, fxNoJit);
    std::vector<float> cap1(480 * 2), cap2(480 * 2);
    Expect(eng.Play(noJit, {}) != 0, "noJit play 1");
    eng.MixOffline(cap1.data(), 480);
    eng.AdvanceSilentFrames(48000);
    eng.Tick();
    Expect(eng.Play(noJit, {}) != 0, "noJit play 2");
    eng.MixOffline(cap2.data(), 480);
    bool equal = true;
    for (int i = 0; i < 480 * 2; ++i) equal = equal && cap1[i] == cap2[i];
    Expect(equal, "fx jitter=0 forces bit-exact integer path");
    eng.StopAll();
    eng.Tick();
    Expect(eng.Play(plain, {}) != 0, "plain jitter play 1");
    eng.MixOffline(cap1.data(), 480);
    eng.AdvanceSilentFrames(48000);
    eng.Tick();
    Expect(eng.Play(plain, {}) != 0, "plain jitter play 2");
    eng.MixOffline(cap2.data(), 480);
    bool differ = false;
    for (int i = 0; i < 480 * 2; ++i) differ = differ || std::fabs(cap1[i] - cap2[i]) > 1e-6f;
    Expect(differ, "no-override clip inherits global jitter");
}

} // namespace

void RunAudioTests() {
    TestAudioMixerMath();
    TestAudioLifecycle();
    TestAudioDeviceInitNoCrash();
    TestAudioBench100Sfx();
    TestAudioBakedRoundtrip();
    TestAudioBakedHardening();
    TestAudioSpscRing();
    TestAudioStreamVoice();
    TestAudioMasterLimiter();
    TestAudioVoiceCapSteal();
    TestAudioRetriggerThrottlePitchJitter();
    TestAudioPerClipFxOverrides();
    TestAudioFadeEnvelope();
    TestAudioSpatialMath();
    TestAudioChannelCommands();
    TestAudioSourceLifecycle();
}
