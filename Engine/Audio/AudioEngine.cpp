// Lemon 引擎 — 音频核心实现（M6c 批⓪，ADR-015）
// miniaudio 类型只出现在本 .cpp（头文件零 miniaudio，同 Window/RHI Pimpl 纪律）。
// 线程模型（ADR-015 M4）：主线程独占改声部/音量（短临界区），设备回调线程锁内混音；
// 静音模式无设备，游标由 Tick(dt)/AdvanceSilentFrames 在主线程推进——生命周期记账
// 与有声模式一致（冒烟断言可用）。
#include "Audio/AudioEngine.h"

#include "Core/Log.h"

// 宏视角统一（review 2026-09-30）：本 TU 虽只碰 ma_context/ma_device（布局不依赖
// stb 守卫），仍先取 stb_vorbis 声明再含 miniaudio——与 MiniAudioImpl.c/BakedClip.cpp
// 保持同视角，杜绝日后在此 TU 使用 ma_decoder 时的布局漂移隐患
#define STB_VORBIS_HEADER_ONLY
#include "thirdparty/stb_vorbis.c"
#include "thirdparty/miniaudio.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <vector>

namespace lemon::audio {

namespace {

constexpr float kInv32768 = 1.0f / 32768.0f;

// 等功率声像：pan ∈ [-1,1] → θ ∈ [0,π/2]；中心 -3dB（常数功率，扫像无爆点）
void PanGains(float pan, float& outL, float& outR) {
    pan = std::clamp(pan, -1.0f, 1.0f);
    const float theta = (pan + 1.0f) * 0.7853981633974483f; // (pan+1) * π/4
    outL = std::cos(theta);
    outR = std::sin(theta);
}

} // namespace

struct AudioEngine::Impl {
    struct Clip {
        std::vector<int16_t> pcm; // 交错 PCM16（48k 契约）
        uint32_t frameCount = 0;
        uint16_t channels = 1;
        uint32_t loopStart = 0;
        uint32_t loopEnd = 0; // 0 = 尾
    };

    struct Voice {
        uint32_t id = 0;       // 0 = 空槽；单调发号不复用（Stop(id) 后 id 永久退役）
        uint32_t clipRef = 0;  // clips 下标 + 1
        uint32_t cursor = 0;   // 帧游标（设备模式归音频线程，静音模式归主线程）
        uint64_t startSeq = 0; // 偷声部判据：最旧一次性声部
        float volume = 1.0f;
        float panL = 1.0f, panR = 1.0f;
        Group group = Group::Sfx;
        bool loop = false;
        bool done = false;     // 一次性播完 / 被停 / clip 注销，待 Tick 回收
        bool paused = false;
    };

    mutable std::mutex mtx;
    std::vector<Clip> clips;          // clipId = 下标 + 1，只增不减（批① .baked 装载消费）
    Voice voices[kMaxVoices] = {};
    uint32_t nextVoiceId = 1;
    uint64_t nextStartSeq = 1;
    float groupVol[kGroupCount] = {1, 1, 1};
    float masterVol = 1.0f;
    bool pausedAll = false;
    bool silent_ = false;
    bool inited_ = false;

    // 设备面（仅设备模式存在）
    ma_context ctx{};
    ma_device device{};
    bool ctxOk = false, deviceOk = false;

    // ---- 主线程：声部/音量变更（锁内）----
    uint32_t PlayLocked(uint32_t clipId, const PlayParams& p) {
        if (clipId == 0 || clipId > clips.size() || clips[clipId - 1].frameCount == 0)
            return 0;
        // review 2026-09-30：group 防御钳（越界 = groupVol[] 越界读；C# 路径上层已
        // 钳，此处兜直接 C++ 调用——测试/未来消费者）
        int gi = static_cast<int>(p.group);
        if (gi < 0 || gi >= kGroupCount) gi = 1; // 越界落 Sfx
        const Group group = static_cast<Group>(gi);
        int slot = -1;
        for (int i = 0; i < kMaxVoices; ++i) {
            if (voices[i].id == 0 || voices[i].done) { // 空槽或完成未回收的槽直接复用
                slot = i;
                break;
            }
        }
        if (slot < 0) {
            // 池满：偷最旧一次性声部；全是循环声部则拒绝（ADR-015 M4）
            uint64_t oldest = UINT64_MAX;
            int stealSlot = -1;
            for (int i = 0; i < kMaxVoices; ++i) {
                if (!voices[i].loop && voices[i].startSeq < oldest) {
                    oldest = voices[i].startSeq;
                    stealSlot = i;
                }
            }
            if (stealSlot < 0)
                return 0;
            slot = stealSlot;
        }
        Voice& v = voices[slot];
        v.id = nextVoiceId++;
        v.clipRef = clipId;
        v.cursor = 0;
        v.startSeq = nextStartSeq++;
        v.volume = std::clamp(p.volume, 0.0f, 4.0f);
        PanGains(p.pan, v.panL, v.panR);
        v.group = group;
        v.loop = p.loop;
        v.done = false;
        v.paused = pausedAll && group != Group::Ui && (p.loop || group == Group::Bgm);
        return v.id;
    }

    // ---- 混音核心（调用方持锁；设备回调与 MixOffline 共用）----
    void MixVoices(float* out, uint32_t frames) {
        std::memset(out, 0, sizeof(float) * frames * kMixChannels);
        for (Voice& v : voices) {
            if (v.id == 0 || v.done || v.paused)
                continue;
            const Clip& c = clips[v.clipRef - 1];
            const uint32_t loopEnd = c.loopEnd ? std::min(c.loopEnd, c.frameCount) : c.frameCount;
            const uint32_t end = v.loop ? loopEnd : c.frameCount;
            const float gL = v.volume * groupVol[static_cast<int>(v.group)] * masterVol * v.panL;
            const float gR = v.volume * groupVol[static_cast<int>(v.group)] * masterVol * v.panR;
            for (uint32_t f = 0; f < frames;) {
                if (v.cursor >= end) {
                    if (v.loop && loopEnd > c.loopStart) {
                        v.cursor = c.loopStart;
                        continue;
                    }
                    v.done = true; // 一次性播完（区间退化 loopEnd<=loopStart 视同不循环）
                    break;
                }
                const int16_t* frm = c.pcm.data() + static_cast<size_t>(v.cursor) * c.channels;
                if (c.channels == 1) {
                    const float s = frm[0] * kInv32768;
                    out[f * 2 + 0] += s * gL;
                    out[f * 2 + 1] += s * gR;
                } else {
                    out[f * 2 + 0] += frm[0] * kInv32768 * gL;
                    out[f * 2 + 1] += frm[1] * kInv32768 * gR;
                }
                ++v.cursor;
                ++f;
            }
            if (!v.loop && v.cursor >= end)
                v.done = true; // 恰好混到末帧的一次性声部当场终止（不待下一块）
        }
        for (uint32_t i = 0; i < frames * kMixChannels; ++i)
            out[i] = std::clamp(out[i], -1.0f, 1.0f);
    }

    void AdvanceLocked(uint32_t frames) {
        for (Voice& v : voices) {
            if (v.id == 0 || v.done || v.paused)
                continue;
            const Clip& c = clips[v.clipRef - 1];
            const uint32_t loopEnd = c.loopEnd ? std::min(c.loopEnd, c.frameCount) : c.frameCount;
            const uint32_t end = v.loop ? loopEnd : c.frameCount;
            const uint64_t reached = static_cast<uint64_t>(v.cursor) + frames;
            if (reached >= end) {
                if (v.loop && loopEnd > c.loopStart) {
                    const uint32_t span = loopEnd - c.loopStart;
                    v.cursor = c.loopStart + static_cast<uint32_t>((reached - c.loopStart) % span);
                } else {
                    v.done = true;
                }
            } else {
                v.cursor = static_cast<uint32_t>(reached);
            }
        }
    }

    static void DataCallback(ma_device* pDevice, void* pOutput, const void* /*pInput*/,
                             ma_uint32 frameCount) {
        auto* self = static_cast<Impl*>(pDevice->pUserData);
        std::lock_guard<std::mutex> lk(self->mtx);
        self->MixVoices(static_cast<float*>(pOutput), frameCount);
    }
};

AudioEngine::AudioEngine() : impl_(std::make_unique<Impl>()) {}
AudioEngine::~AudioEngine() { Shutdown(); }

bool AudioEngine::Init(const InitOptions& opts) {
    if (impl_->inited_)
        return true;
    const char* envOff = std::getenv("LEMON_AUDIO");
    const bool forced = opts.forceSilent || (envOff && std::strcmp(envOff, "off") == 0);
    impl_->silent_ = forced;
    if (!forced) {
        if (ma_context_init(nullptr, 0, nullptr, &impl_->ctx) != MA_SUCCESS) {
            LogMsg(LogLevel::Warn, "audio: ma_context_init 失败 → 静音模式（降级不阻断）");
            impl_->silent_ = true;
        } else {
            impl_->ctxOk = true;
            ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
            cfg.playback.format = ma_format_f32;      // 混音域 f32（ADR-015 M2）
            cfg.playback.channels = kMixChannels;
            cfg.sampleRate = kMixSampleRate;          // 设备率不符由 miniaudio 总线转换兜底
            cfg.dataCallback = &Impl::DataCallback;
            cfg.pUserData = impl_.get();
            if (ma_device_init(&impl_->ctx, &cfg, &impl_->device) != MA_SUCCESS) {
                LogMsg(LogLevel::Warn, "audio: ma_device_init 失败 → 静音模式（降级不阻断）");
            } else if (ma_device_start(&impl_->device) != MA_SUCCESS) {
                // review 2026-09-30：start 失败（设备被独占等）此前无红字且不落降级
                ma_device_uninit(&impl_->device);
                LogMsg(LogLevel::Warn, "audio: ma_device_start 失败 → 静音模式（降级不阻断）");
            } else {
                impl_->deviceOk = true;
            }
        }
    }
    impl_->inited_ = true;
    return true; // 静音 = 降级成功，不是失败
}

void AudioEngine::Shutdown() {
    if (!impl_->inited_)
        return;
    if (impl_->deviceOk) {
        ma_device_stop(&impl_->device);
        ma_device_uninit(&impl_->device);
        impl_->deviceOk = false;
    }
    if (impl_->ctxOk) {
        ma_context_uninit(&impl_->ctx);
        impl_->ctxOk = false;
    }
    std::lock_guard<std::mutex> lk(impl_->mtx);
    for (auto& v : impl_->voices)
        v = {}; // 原生数组不能整体赋值；号不复用（nextVoiceId 只增）
    impl_->silent_ = false;
    impl_->inited_ = false;
}

uint32_t AudioEngine::RegisterClip(const ClipData& data) {
    if (!data.pcm || data.frameCount == 0 || (data.channels != 1 && data.channels != 2))
        return 0;
    Impl::Clip c;
    c.pcm.assign(data.pcm, data.pcm + static_cast<size_t>(data.frameCount) * data.channels);
    c.frameCount = data.frameCount;
    c.channels = data.channels;
    c.loopStart = std::min(data.loopStart, data.frameCount);
    c.loopEnd = data.loopEnd ? std::min(data.loopEnd, data.frameCount) : data.frameCount;
    std::lock_guard<std::mutex> lk(impl_->mtx);
    impl_->clips.push_back(std::move(c));
    return static_cast<uint32_t>(impl_->clips.size());
}

void AudioEngine::UnregisterClip(uint32_t clipId) {
    std::lock_guard<std::mutex> lk(impl_->mtx);
    if (clipId == 0 || clipId > impl_->clips.size())
        return;
    impl_->clips[clipId - 1].frameCount = 0; // 保留槽位（id 不复用），Play 拒绝
    for (auto& v : impl_->voices)
        if (v.clipRef == clipId)
            v.done = true;
}

void AudioEngine::ResetClips() {
    std::lock_guard<std::mutex> lk(impl_->mtx);
    impl_->clips.clear();
    for (auto& v : impl_->voices)
        v.done = true;
}

uint32_t AudioEngine::Play(uint32_t clipId, const PlayParams& p) {
    std::lock_guard<std::mutex> lk(impl_->mtx);
    return impl_->PlayLocked(clipId, p);
}

bool AudioEngine::Stop(uint32_t voiceId) {
    if (voiceId == 0)
        return false;
    std::lock_guard<std::mutex> lk(impl_->mtx);
    for (auto& v : impl_->voices) {
        if (v.id == voiceId && !v.done) {
            v.done = true;
            return true;
        }
    }
    return false;
}

void AudioEngine::StopAll() {
    std::lock_guard<std::mutex> lk(impl_->mtx);
    for (auto& v : impl_->voices)
        if (v.id != 0)
            v.done = true;
}

void AudioEngine::SetGroupVolume(Group g, float v) {
    const int i = static_cast<int>(g);
    if (i < 0 || i >= kGroupCount)
        return;
    std::lock_guard<std::mutex> lk(impl_->mtx);
    impl_->groupVol[i] = std::clamp(v, 0.0f, 1.0f);
}

void AudioEngine::SetMasterVolume(float v) {
    std::lock_guard<std::mutex> lk(impl_->mtx);
    impl_->masterVol = std::clamp(v, 0.0f, 1.0f);
}

float AudioEngine::GroupVolume(Group g) const {
    const int i = static_cast<int>(g);
    std::lock_guard<std::mutex> lk(impl_->mtx);
    return (i < 0 || i >= kGroupCount) ? 0.0f : impl_->groupVol[i];
}

float AudioEngine::MasterVolume() const {
    std::lock_guard<std::mutex> lk(impl_->mtx);
    return impl_->masterVol;
}

void AudioEngine::SetPaused(bool paused) {
    std::lock_guard<std::mutex> lk(impl_->mtx);
    impl_->pausedAll = paused;
    for (auto& v : impl_->voices) {
        if (v.id == 0)
            continue;
        // ADR-015 M4：循环声部与 BGM 组挂起；一次性 SFX 自然放完；UI 组永不挂起（含其循环）
        v.paused = paused && v.group != Group::Ui && (v.loop || v.group == Group::Bgm);
    }
}

void AudioEngine::Tick(float dtSeconds) {
    std::lock_guard<std::mutex> lk(impl_->mtx);
    for (auto& v : impl_->voices)
        if (v.done)
            v = {}; // 回收：id 归零（号不复用——nextVoiceId 只增）
    if (impl_->silent_ && dtSeconds > 0.0f) {
        const auto frames = static_cast<uint32_t>(dtSeconds * kMixSampleRate);
        if (frames > 0)
            impl_->AdvanceLocked(frames);
    }
}

int AudioEngine::ActiveVoiceCount() const {
    std::lock_guard<std::mutex> lk(impl_->mtx);
    int n = 0;
    for (const auto& v : impl_->voices)
        if (v.id != 0 && !v.done) // 暂停声部仍占槽（会恢复），计入活跃
            ++n;
    return n;
}

bool AudioEngine::VoiceAlive(uint32_t voiceId) const {
    if (voiceId == 0)
        return false;
    std::lock_guard<std::mutex> lk(impl_->mtx);
    for (const auto& v : impl_->voices)
        if (v.id == voiceId)
            return !v.done;
    return false;
}

void AudioEngine::MixOffline(float* outInterleaved, uint32_t outFrames) {
    if (!outInterleaved || outFrames == 0)
        return;
    if (impl_->deviceOk) {
        LogMsg(LogLevel::Error, "audio: MixOffline 仅静音模式可用（设备模式游标归音频线程）");
        return;
    }
    std::lock_guard<std::mutex> lk(impl_->mtx);
    impl_->MixVoices(outInterleaved, outFrames);
}

void AudioEngine::AdvanceSilentFrames(uint32_t frames) {
    if (impl_->deviceOk || frames == 0)
        return;
    std::lock_guard<std::mutex> lk(impl_->mtx);
    impl_->AdvanceLocked(frames);
}

bool AudioEngine::silent() const {
    return impl_->silent_;
}

bool AudioEngine::inited() const {
    return impl_->inited_;
}

} // namespace lemon::audio
