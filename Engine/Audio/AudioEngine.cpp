// Lemon 引擎 — 音频核心实现（M6c 批⓪，ADR-015）
// miniaudio 类型只出现在本 .cpp（头文件零 miniaudio，同 Window/RHI Pimpl 纪律）。
// 线程模型（ADR-015 M4）：主线程独占改声部/音量（短临界区），设备回调线程锁内混音；
// 静音模式无设备，游标由 Tick(dt)/AdvanceSilentFrames 在主线程推进——生命周期记账
// 与有声模式一致（冒烟断言可用）。
#include "Audio/AudioEngine.h"

#include "Audio/BakedClip.h"
#include "Audio/SpscRing.h"
#include "Core/Log.h"

// 宏视角统一（review 2026-09-30）：本 TU 虽只碰 ma_context/ma_device（布局不依赖
// stb 守卫），仍先取 stb_vorbis 声明再含 miniaudio——与 MiniAudioImpl.c/BakedClip.cpp
// 保持同视角，杜绝日后在此 TU 使用 ma_decoder 时的布局漂移隐患
#define STB_VORBIS_HEADER_ONLY
#include "thirdparty/stb_vorbis.c"
#include "thirdparty/miniaudio.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
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
        std::vector<int16_t> pcm; // 交错 PCM16（48k 契约）；流式 clip 为空
        std::string streamPath;   // 非空 = 流式（批①b：.baked 句柄随声部开闭）
        uint32_t frameCount = 0;
        uint16_t channels = 1;
        uint32_t loopStart = 0;
        uint32_t loopEnd = 0; // 0 = 尾
    };

    // 流式声部喂送状态（批①b，ADR-015 M2）：每声部一份——同 clip 多声部（BGM 交叉
    // 淡出）各自持句柄与游标。环 = 生产者(填充线程/离线泵)×消费者(设备回调/
    // MixOffline)SPSC；回卷换位在生产者侧（环内是线性化帧流，消费者无回卷逻辑）。
    struct StreamFeed {
        static constexpr size_t kRingBytes = 256 * 1024; // ADR-015 M2

        SpscRing ring{kRingBytes};
        std::FILE* file = nullptr;
        uint16_t channels = 2;
        uint32_t frameCount = 0;
        uint32_t loopStart = 0, loopEnd = 0; // 已归一（loopEnd = 尾帧数）
        bool loop = false;                   // 生效语义（退化区间已视同不循环）
        std::atomic<bool> dead{false};   // 声部弃养（回收/复用/停）→ 生产者撤 job
        std::atomic<bool> failed{false}; // 载荷 IO 错 → 消费者环空即终结
        std::atomic<uint64_t> consumed{0}; // 消费帧累计（观测；一次性曲终判据走 cursor）
        std::atomic<uint32_t> underruns{0};
        uint64_t nextFrame = 0; // 生产者私有（回卷换位后的文件帧位）
        ~StreamFeed() {
            if (file) std::fclose(file);
        }
    };
    using FeedRef = std::shared_ptr<StreamFeed>;

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
        FeedRef stream;        // 非 null = 流式声部（批①b；环/句柄随声部生命周期）
        // M6c 批②（D4）：音量包络——fadeDur 内 fadeFrom 线性到 fadeTo（绝对音量域，
        // 逐样本推进）；到点后 volume=fadeTo、包络退役；stopAtFadeEnd 且到 0 = 终结
        float fadeFrom = 1.0f, fadeTo = 1.0f;
        float fadeElapsed = 0.0f, fadeDur = 0.0f; // fadeDur<=0 = 无在途包络
        bool stopAtFadeEnd = false;

        /// 当前有效音量（在途包络取插值；终态化由推进侧调用 FinalizeFadeLocked）
        float EffectiveVolume() const {
            if (fadeDur <= 0.0f) return volume;
            const float t = fadeElapsed / fadeDur;
            return fadeFrom + (fadeTo - fadeFrom) * std::clamp(t, 0.0f, 1.0f);
        }
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

    // 流式填充线程（批①b：设备模式唯一生产者；静音/离线 = PumpStreams 手动驱动）
    std::thread fillThread_;
    std::mutex fillMtx_;                       // 只护 fillJobs_/fillStop_（不与 mtx 嵌套反向）
    std::condition_variable fillCv_;
    std::vector<FeedRef> fillJobs_;            // 活跃流（dead/failed/曲终即撤）
    bool fillStop_ = false;
    std::atomic<uint64_t> streamUnderruns_{0}; // 累计欠载帧（观测/验收）
    bool underrunWarned_ = false;

    // 生产者步进（填充线程 / 起播预填 / 离线泵共用）：喂到环满/曲终/死亡/IO 错。
    // staging 由调用方持有复用（填充线程循环免反复分配）。
    static void PumpFeed(StreamFeed& fd, std::vector<uint8_t>& staging) {
        if (fd.dead.load(std::memory_order_relaxed) ||
            fd.failed.load(std::memory_order_relaxed))
            return;
        for (;;) {
            const size_t frameBytes = size_t(fd.channels) * 2;
            const size_t freeBytes = fd.ring.Free();
            if (freeBytes < frameBytes)
                return; // 环满（或剩余不足一帧）
            if (fd.loop) {
                if (fd.nextFrame >= fd.loopEnd) { // 回卷换位（producer 私有）
                    if (!SeekBakedFrame(fd.file, fd.loopStart, fd.channels)) {
                        fd.failed.store(true, std::memory_order_relaxed);
                        return;
                    }
                    fd.nextFrame = fd.loopStart;
                }
            } else if (fd.nextFrame >= fd.frameCount) {
                return; // 一次性曲终（job 由填充线程撤；环内余量够消费者放完）
            }
            const uint64_t end = fd.loop ? fd.loopEnd : fd.frameCount;
            const uint64_t frames = std::min<uint64_t>(
                {4096ull, end - fd.nextFrame, freeBytes / frameBytes});
            if (frames == 0)
                return;
            staging.resize(size_t(frames) * frameBytes);
            const size_t got = std::fread(staging.data(), frameBytes, size_t(frames), fd.file);
            if (got == 0) { // EOF 早到/IO 错（头校验过载荷长度——正常路径不可达）
                fd.failed.store(true, std::memory_order_relaxed);
                return;
            }
            fd.ring.Write(staging.data(), got * frameBytes);
            fd.nextFrame += got;
            if (got < frames) { // 半读 = 载荷被外部截断
                fd.failed.store(true, std::memory_order_relaxed);
                return;
            }
        }
    }

    void FillThreadLoop() {
        std::vector<uint8_t> staging;
        for (;;) {
            std::unique_lock<std::mutex> lk(fillMtx_);
            if (fillJobs_.empty())
                fillCv_.wait(lk, [this] { return fillStop_ || !fillJobs_.empty(); });
            else // 有活跃流：短周期轮询补环（256KiB ≈ 1.4s 立体声余量，4ms 绰绰有余）
                fillCv_.wait_for(lk, std::chrono::milliseconds(4),
                                 [this] { return fillStop_; });
            if (fillStop_)
                return;
            for (size_t i = 0; i < fillJobs_.size();) {
                StreamFeed& fd = *fillJobs_[i];
                PumpFeed(fd, staging);
                const bool gone = fd.dead.load(std::memory_order_relaxed) ||
                                  fd.failed.load(std::memory_order_relaxed) ||
                                  (!fd.loop && fd.nextFrame >= fd.frameCount);
                if (gone)
                    fillJobs_.erase(fillJobs_.begin() + i);
                else
                    ++i;
            }
        }
    }

    // ---- 主线程：声部/音量变更（锁内）----
    uint32_t PlayLocked(uint32_t clipId, const PlayParams& p, FeedRef feed) {
        if (clipId == 0 || clipId > clips.size() || clips[clipId - 1].frameCount == 0)
            return 0;
        const Clip& c = clips[clipId - 1];
        if (!c.streamPath.empty() != (feed != nullptr))
            return 0; // 流式/整载类型失配（feed 由 Play 按 clip 类型开好）
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
        if (v.stream) // 复用槽弃养旧流（偷声部/done 未回收同理——生产者撤 job）
            v.stream->dead.store(true, std::memory_order_relaxed);
        v.id = nextVoiceId++;
        v.clipRef = clipId;
        v.cursor = 0;
        v.startSeq = nextStartSeq++;
        v.volume = std::clamp(p.volume, 0.0f, 4.0f);
        PanGains(p.pan, v.panL, v.panR);
        v.group = group;
        // 循环语义归一（批①b）：退化区间（loopEnd<=loopStart）视同不循环——与
        // MixVoices 回卷守卫口径一致（整载路径原先播到 loopEnd 截断，现对齐全曲）
        v.loop = p.loop && c.loopEnd > c.loopStart;
        v.done = false;
        v.paused = pausedAll && group != Group::Ui && (v.loop || group == Group::Bgm);
        v.stream = std::move(feed);
        if (v.stream) { // 设备模式挂填充队列（静音/离线无线程——PumpStreams 手动驱动）
            v.stream->loop = v.loop;
            if (fillThread_.joinable()) {
                std::lock_guard<std::mutex> flk(fillMtx_);
                fillJobs_.push_back(v.stream);
                fillCv_.notify_one();
            }
        }
        // D4 起播淡入：包络从 0 爬到 volume（fadeDur<=0 = 直起，包络退役态）
        v.fadeFrom = 0.0f;
        v.fadeTo = v.volume;
        v.fadeDur = std::max(p.fadeInSec, 0.0f);
        v.fadeElapsed = 0.0f;
        v.stopAtFadeEnd = false;
        return v.id;
    }

    // ---- 混音核心（调用方持锁；设备回调与 MixOffline 共用）----
    void MixVoices(float* out, uint32_t frames) {
        std::memset(out, 0, sizeof(float) * frames * kMixChannels);
        constexpr float kInvRate = 1.0f / static_cast<float>(kMixSampleRate);
        for (Voice& v : voices) {
            if (v.id == 0 || v.done || v.paused)
                continue;
            const Clip& c = clips[v.clipRef - 1];
            const uint32_t loopEnd = c.loopEnd ? std::min(c.loopEnd, c.frameCount) : c.frameCount;
            // 终点语义：整载循环 = 回卷点（消费者侧回卷）；流式循环 = 无穷（回卷在
            // 生产者侧，环内是线性化帧流）；一次性（两路）= 全曲帧数
            const uint32_t end = v.stream ? (v.loop ? UINT32_MAX : c.frameCount)
                                          : (v.loop ? loopEnd : c.frameCount);
            const float gv = groupVol[static_cast<int>(v.group)] * masterVol;
            const bool fading = v.fadeDur > 0.0f;
            float gL0 = v.volume * gv * v.panL, gR0 = v.volume * gv * v.panR;
            if (fading) { // 包络期首帧取当前插值（后续逐帧推进）
                const float ve = v.EffectiveVolume();
                gL0 = ve * gv * v.panL;
                gR0 = ve * gv * v.panR;
            }
            float gL = gL0, gR = gR0;
            uint32_t streamUnderrun = 0; // 批①b：块末一次发布（实时路径免逐帧 RMW）
            for (uint32_t f = 0; f < frames;) {
                // review 2026-10-01：流式循环声部游标 u32 会在 2^32 帧（≈24.8h 连续
                // 循环）抵达 UINT32_MAX——守卫跳过终点检查让其回卷续播（RAM 路径回卷
                // 模运算无此边界）
                if (v.cursor >= end && !(v.stream && v.loop)) {
                    if (v.loop && !v.stream && loopEnd > c.loopStart) {
                        v.cursor = c.loopStart;
                        continue;
                    }
                    v.done = true; // 一次性播完（区间退化 loopEnd<=loopStart 视同不循环）
                    break;
                }
                if (fading) { // D4：逐样本推进包络；到点终态化（到 0 且 stop = 终结）
                    v.fadeElapsed += kInvRate;
                    if (v.fadeElapsed >= v.fadeDur) {
                        v.volume = v.fadeTo;
                        v.fadeDur = 0.0f;
                        if (v.stopAtFadeEnd && v.fadeTo <= 0.0f) {
                            v.done = true;
                            break;
                        }
                    }
                    const float ve = v.EffectiveVolume();
                    gL = ve * gv * v.panL;
                    gR = ve * gv * v.panR;
                }
                float sL, sR;
                if (v.stream) { // 批①b：流式取帧——环空 = 欠载静音（failed 且枯竭 = 终结）
                    uint8_t fb[4] = {};
                    const size_t fbBytes = size_t(c.channels) * 2;
                    if (v.stream->ring.Read(fb, fbBytes) != fbBytes) {
                        if (v.stream->failed.load(std::memory_order_acquire)) {
                            v.done = true;
                            break;
                        }
                        ++streamUnderrun;
                    }
                    const auto le16 = [](const uint8_t* p) {
                        return int16_t(uint16_t(p[0]) | (uint16_t(p[1]) << 8)) * kInv32768;
                    };
                    sL = le16(fb);
                    sR = c.channels == 2 ? le16(fb + 2) : sL;
                } else {
                    const int16_t* frm = c.pcm.data() + static_cast<size_t>(v.cursor) * c.channels;
                    if (c.channels == 1) {
                        sL = sR = frm[0] * kInv32768;
                    } else {
                        sL = frm[0] * kInv32768;
                        sR = frm[1] * kInv32768;
                    }
                }
                out[f * 2 + 0] += sL * gL;
                out[f * 2 + 1] += sR * gR;
                ++v.cursor;
                ++f;
            }
            if (!v.loop && v.cursor >= end)
                v.done = true; // 恰好混到末帧的一次性声部当场终止（不待下一块）
            if (v.stream && streamUnderrun > 0) { // 块末发布（观测/验收"欠载静音 ≤ 单次"）
                v.stream->underruns.fetch_add(streamUnderrun, std::memory_order_relaxed);
                streamUnderruns_.fetch_add(streamUnderrun, std::memory_order_relaxed);
            }
        }
        for (uint32_t i = 0; i < frames * kMixChannels; ++i)
            out[i] = std::clamp(out[i], -1.0f, 1.0f);
    }

    void AdvanceLocked(uint32_t frames) {
        const float dt = static_cast<float>(frames) / static_cast<float>(kMixSampleRate);
        for (Voice& v : voices) {
            if (v.id == 0 || v.done || v.paused)
                continue;
            // D4：静音模式包络同径推进（逻辑记账与有声模式一致——降级路径的一部分）
            if (v.fadeDur > 0.0f) {
                v.fadeElapsed += dt;
                if (v.fadeElapsed >= v.fadeDur) {
                    v.volume = v.fadeTo;
                    v.fadeDur = 0.0f;
                    if (v.stopAtFadeEnd && v.fadeTo <= 0.0f) {
                        v.done = true;
                        continue;
                    }
                }
            }
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
                // review 2026-10-01：2026-09-30 热修③ 自称"silent_ 置位"但从未落地——
                // silent_ 恒假 = Tick 不推逻辑游标 → 声部永 done=false → AudioChannel
                // 死条目回收（VoiceAlive 判据）失效 = 每次播放泄漏一条记账
                LogMsg(LogLevel::Warn, "audio: ma_device_init 失败 → 静音模式（降级不阻断）");
                impl_->silent_ = true;
            } else if (ma_device_start(&impl_->device) != MA_SUCCESS) {
                // review 2026-09-30：start 失败（设备被独占等）此前无红字且不落降级
                ma_device_uninit(&impl_->device);
                LogMsg(LogLevel::Warn, "audio: ma_device_start 失败 → 静音模式（降级不阻断）");
                impl_->silent_ = true;
            } else {
                impl_->deviceOk = true;
                // 批①b：流式填充线程（设备模式唯一生产者；音频回调消费 SPSC 环）
                impl_->fillStop_ = false;
                impl_->fillThread_ = std::thread([this] { impl_->FillThreadLoop(); });
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
        ma_device_stop(&impl_->device);   // 同步等回调退出（不再碰环消费侧）
        ma_device_uninit(&impl_->device);
    }
    if (impl_->fillThread_.joinable()) {  // 批①b：先收填充线程再弃养 feed
        {
            std::lock_guard<std::mutex> lk(impl_->fillMtx_);
            impl_->fillStop_ = true;
        }
        impl_->fillCv_.notify_all();
        impl_->fillThread_.join();
        std::lock_guard<std::mutex> lk(impl_->fillMtx_);
        impl_->fillJobs_.clear();
    }
    // review 2026-10-01：deviceOk 与填充线程共存亡（join 后才落 false）——否则窗口期
    // PumpStreams 见 false 放行手动泵 = 与填充线程同环双生产者
    impl_->deviceOk = false;
    if (impl_->ctxOk) {
        ma_context_uninit(&impl_->ctx);
        impl_->ctxOk = false;
    }
    std::lock_guard<std::mutex> lk(impl_->mtx);
    for (auto& v : impl_->voices) {
        if (v.stream)
            v.stream->dead.store(true, std::memory_order_relaxed);
        v = {}; // 原生数组不能整体赋值；号不复用（nextVoiceId 只增）
    }
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

uint32_t AudioEngine::RegisterClip(std::vector<int16_t>&& pcm, uint16_t channels,
                                   uint32_t frameCount, uint32_t loopStart, uint32_t loopEnd) {
    if (pcm.empty() || frameCount == 0 || (channels != 1 && channels != 2) ||
        pcm.size() != size_t(frameCount) * channels)
        return 0;
    std::lock_guard<std::mutex> lk(impl_->mtx);
    Impl::Clip c;
    c.pcm = std::move(pcm);
    c.frameCount = frameCount;
    c.channels = channels;
    c.loopStart = std::min(loopStart, frameCount);
    c.loopEnd = loopEnd ? std::min(loopEnd, frameCount) : frameCount;
    impl_->clips.push_back(std::move(c));
    return static_cast<uint32_t>(impl_->clips.size());
}

uint32_t AudioEngine::RegisterStreamClip(const char* path) {
    if (!path || !path[0])
        return 0;
    BakedClipInfo info;
    if (!PeekBakedClip(path, info)) // 头校验单源（拒坏文件，与整载同口径）
        return 0;
    std::lock_guard<std::mutex> lk(impl_->mtx);
    Impl::Clip c;
    c.streamPath = path;
    c.frameCount = info.frameCount;
    c.channels = info.channels;
    c.loopStart = std::min(info.loopStart, info.frameCount);
    c.loopEnd = info.loopEnd ? std::min(info.loopEnd, info.frameCount) : info.frameCount;
    impl_->clips.push_back(std::move(c));
    return static_cast<uint32_t>(impl_->clips.size());
}

void AudioEngine::UnregisterClip(uint32_t clipId) {    std::lock_guard<std::mutex> lk(impl_->mtx);
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
    // 流式 clip 两阶段（批①b）：锁内取路径 → 锁外 open+头解析+预填（ADR-015 M2
    // "主线程只做 open + 头解析"；预填整环保首回调零欠载——256KiB 顺序读 ≈ 亚毫秒）
    std::string streamPath;
    {
        std::lock_guard<std::mutex> lk(impl_->mtx);
        if (clipId == 0 || clipId > impl_->clips.size())
            return 0;
        streamPath = impl_->clips[clipId - 1].streamPath;
    }
    Impl::FeedRef feed;
    if (!streamPath.empty()) {
        BakedClipInfo info;
        std::FILE* f = nullptr;
        if (!OpenBakedStream(streamPath.c_str(), info, f)) {
            LogMsg(LogLevel::Warn, "audio: 流式声部打开失败（.baked 缺失/头坏）：%s",
                   streamPath.c_str());
            return 0;
        }
        feed = std::make_shared<Impl::StreamFeed>();
        feed->file = f;
        feed->channels = info.channels;
        feed->frameCount = info.frameCount;
        feed->loopStart = std::min(info.loopStart, info.frameCount);
        feed->loopEnd = info.loopEnd ? std::min(info.loopEnd, info.frameCount) : info.frameCount;
        std::vector<uint8_t> staging;
        Impl::PumpFeed(*feed, staging); // 预填（loop 语义在 PlayLocked 落位后由 job 线程
                                        // 按 v.loop 续喂——预填期 loop 未定，按一次性喂
                                        // 首环；循环声部起播即 v.loop 路径只多喂环尾）
        if (feed->failed.load(std::memory_order_relaxed)) {
            LogMsg(LogLevel::Warn, "audio: 流式声部预填失败（载荷 IO 错）：%s",
                   streamPath.c_str());
            return 0;
        }
    }
    std::lock_guard<std::mutex> lk(impl_->mtx);
    return impl_->PlayLocked(clipId, p, std::move(feed));
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

bool AudioEngine::FadeVoice(uint32_t voiceId, float targetVolume, float seconds,
                            bool stopWhenDone) {
    if (voiceId == 0)
        return false;
    std::lock_guard<std::mutex> lk(impl_->mtx);
    for (auto& v : impl_->voices) {
        if (v.id != voiceId || v.done)
            continue;
        const float to = std::clamp(targetVolume, 0.0f, 4.0f);
        if (seconds <= 0.0f) { // 硬切：立即到位（D4 fadeSec=0 语义）
            v.volume = to;
            v.fadeDur = 0.0f;
            if (stopWhenDone && to <= 0.0f)
                v.done = true;
            return true;
        }
        // 起点 = 当前有效音量（接续在途包络不跳变）
        v.fadeFrom = v.EffectiveVolume();
        v.fadeTo = to;
        v.fadeElapsed = 0.0f;
        v.fadeDur = seconds;
        v.stopAtFadeEnd = stopWhenDone;
        return true;
    }
    return false;
}

bool AudioEngine::SetVoiceParams(uint32_t voiceId, float volume, float pan) {
    if (voiceId == 0)
        return false;
    std::lock_guard<std::mutex> lk(impl_->mtx);
    for (auto& v : impl_->voices) {
        if (v.id != voiceId || v.done)
            continue;
        v.volume = std::clamp(volume, 0.0f, 4.0f);
        PanGains(pan, v.panL, v.panR);
        return true;
    }
    return false;
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
    for (auto& v : impl_->voices) {
        if (!v.done)
            continue;
        if (v.stream) // 批①b：回收即弃养（填充线程撤 job；环/句柄随末引用释放）
            v.stream->dead.store(true, std::memory_order_relaxed);
        v = {}; // 回收：id 归零（号不复用——nextVoiceId 只增）
    }
    if (!impl_->underrunWarned_ && impl_->streamUnderruns_.load(std::memory_order_relaxed) > 0) {
        impl_->underrunWarned_ = true; // 验收证据面："欠载静音 ≤ 单次"判据的观测钩
        LogMsg(LogLevel::Warn, "audio: 流式声部出现欠载（累计 %llu 帧——环 256KiB/填充 4ms 周期下不应发生）",
               static_cast<unsigned long long>(impl_->streamUnderruns_.load()));
    }
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

void AudioEngine::PumpStreams() {
    if (impl_->deviceOk) {
        LogMsg(LogLevel::Error,
               "audio: PumpStreams 仅静音/离线模式（设备模式由专用线程喂数——双生产者违规）");
        return;
    }
    std::lock_guard<std::mutex> lk(impl_->mtx);
    std::vector<uint8_t> staging;
    for (auto& v : impl_->voices)
        if (v.id != 0 && !v.done && v.stream)
            Impl::PumpFeed(*v.stream, staging);
}

uint64_t AudioEngine::StreamUnderrunFrames() const {
    return impl_->streamUnderruns_.load(std::memory_order_relaxed);
}

bool AudioEngine::silent() const {
    return impl_->silent_;
}

bool AudioEngine::inited() const {
    return impl_->inited_;
}

} // namespace lemon::audio
