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
constexpr float kStealReleaseSec = 0.005f; // 同 clip 偷声部释放：短到听不出迟滞，长到不咔哒

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
        uint64_t lastStartFrame = UINT64_MAX; // 重触发节流锚（混音帧域；MAX=从未播）
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
        // 音高微扰（听感验收 2026-10-01）：rate==1 = 整数游标位精确路径（既有契约）；
        // rate≠1 = 分数游标 1-tap lerp（仅非循环整载声部——循环/流式恒 1）
        float rate = 1.0f;
        float frac = 0.0f;

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

    // 同 clip 重触发治理（听感验收 2026-10-01）：节流窗（混音帧域时钟）+ 音高微扰
    uint64_t mixFrames_ = 0;      // 混音帧时钟（MixVoices/AdvanceLocked 累加；锁内）
    uint32_t retriggerCdFrames_ = uint32_t(0.045f * kMixSampleRate); // 默认 45ms ≈22Hz
    float pitchJitter_ = 0.02f;   // ±2%（Sfx/Ui 非循环整载；0 = 关）
    uint32_t rngState_ = 0x2545F491u; // 固定种子：微扰序列可复现（表现层装饰，不入哈希）
    uint32_t Xorshift() {
        uint32_t x = rngState_;
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        return rngState_ = x;
    }

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
    // outFeedJob：流式声部需挂填充队列时置为其 feed（调用方在 mtx **外**入队——
    // review 2026-10-02 #4：mtx 内取 fillMtx_ 会与填充线程 PumpFeed 的阻塞 fread
    // 成链，设备回调等 mtx 即被磁盘 IO 间接卡；预填整环 256KiB ≈1.4s 余量，
    // job 迟到微秒级无害）
    uint32_t PlayLocked(uint32_t clipId, const PlayParams& p, FeedRef feed,
                        FeedRef& outFeedJob) {
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
        // 重触发节流（听感验收 2026-10-01"放鞭炮"）：同 clip 一次性播放距上次起播
        // 节流窗内的新请求丢弃（返回 0——机枪效应治密度；循环声部豁免：BGM/环境
        // 声的重触发语义是"重启"非"叠发"）。窗锚 = 上次**被接受**的起播。
        const bool oneshot = !(p.loop && c.loopEnd > c.loopStart);
        if (oneshot && retriggerCdFrames_ > 0 &&
            c.lastStartFrame != UINT64_MAX &&
            mixFrames_ - c.lastStartFrame < retriggerCdFrames_)
            return 0;
        int slot = -1;
        for (int i = 0; i < kMaxVoices; ++i) {
            if (voices[i].id == 0 || voices[i].done) { // 空槽或完成未回收的槽直接复用
                slot = i;
                break;
            }
        }
        if (slot < 0) {
            // 池满：偷最旧一次性声部；全是循环声部则拒绝（ADR-015 M4）。
            // review 2026-10-02 #15 交底：偷 = 槽位即刻被新声部整体覆盖，被偷声部
            //（含其在途释放包络）无法像 per-clip 超限路径那样走 5ms 释放——固定
            // 池下无槽容纳释放尾巴，残余为硬切；常态饱和由下方同 clip 上限路径
            //（kMaxVoicesPerClip=4 ≪ 64）以释放治理，真池尽属病态场景
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
        // 同 clip 并发上限（听感验收 2026-10-01：叠音第二源头——同 clip 重触发无限
        // 叠，相干求和最坏 +6dB/份）：业界 per-sound voice limit 同构，超限偷最老；
        // 释放复用 D4 包络（硬停切波前有咔哒）。释放中（stopAtFadeEnd）不计活跃
        // ——突发连发下"活"声部恒 ≤ 上限。
        // review 2026-10-02 #15：victim 跳过被偷槽位——池满偷槽在先，若 victim 恰为
        // 该槽，刚设的释放包络随即被新声部字段整体覆盖（释放彻底失效），改选次老
        {
            int live = 0;
            uint64_t oldest = UINT64_MAX;
            Voice* victim = nullptr;
            for (Voice& o : voices) {
                if (&o == &voices[slot])
                    continue; // 被偷槽位即将整体覆盖（其包络无处安放，见池满注释）
                if (o.id == 0 || o.done || o.clipRef != clipId || o.stopAtFadeEnd)
                    continue;
                ++live;
                if (o.startSeq < oldest) {
                    oldest = o.startSeq;
                    victim = &o;
                }
            }
            if (live >= kMaxVoicesPerClip && victim) {
                victim->fadeFrom = victim->EffectiveVolume(); // 从当前可闻音量起释放
                victim->fadeTo = 0.0f;
                victim->fadeElapsed = 0.0f;
                victim->fadeDur = kStealReleaseSec;
                victim->stopAtFadeEnd = true;
            }
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
        v.rate = 1.0f;
        v.frac = 0.0f;
        if (pitchJitter_ > 0.0f && !v.loop && !v.stream && group != Group::Bgm) {
            // 音高微扰：同素材连发去相干（拍频/梳状叠加的根治项）。±range 均匀；
            // 序列固定种子可复现。BGM 组不扰（乐律精确的音乐性一次性素材）
            // review 2026-10-02 #3：24 位随机值（(2^32-1)>>8 = 2^24-1）除以 2^23-1
            // 曾使 u∈[0,2]（扰动区间 −range..+3range 整体偏尖）——除数对齐满幅
            const float u = float(Xorshift() >> 8) / 16777215.0f; // [0,1]
            v.rate = 1.0f + (u * 2.0f - 1.0f) * pitchJitter_;
        }
        if (v.stream) { // 流式：loop 语义落位；填充队列由调用方在 mtx 外挂（#4）
            v.stream->loop = v.loop;
            if (fillThread_.joinable())
                outFeedJob = v.stream;
        }
        // D4 起播淡入：包络从 0 爬到 volume（fadeDur<=0 = 直起，包络退役态）
        v.fadeFrom = 0.0f;
        v.fadeTo = v.volume;
        v.fadeDur = std::max(p.fadeInSec, 0.0f);
        v.fadeElapsed = 0.0f;
        v.stopAtFadeEnd = false;
        clips[clipId - 1].lastStartFrame = mixFrames_; // 节流窗锚（仅被接受的起播）
        return v.id;
    }

    // ---- 混音核心（调用方持锁；设备回调与 MixOffline 共用）----
    void MixVoices(float* out, uint32_t frames) {
        std::memset(out, 0, sizeof(float) * frames * kMixChannels);
        mixFrames_ += frames; // 混音帧时钟（节流窗锚域；设备回调与离线同径）
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
                    if (v.rate == 1.0f) { // 整数游标：位精确既有路径（jitter 关闭/循环/流式）
                        if (c.channels == 1) {
                            sL = sR = frm[0] * kInv32768;
                        } else {
                            sL = frm[0] * kInv32768;
                            sR = frm[1] * kInv32768;
                        }
                    } else { // 音高微扰：分数游标 1-tap lerp（尾帧持住防越界）
                        const int16_t* nxt = v.cursor + 1 < c.frameCount
                                                 ? frm + c.channels : frm;
                        const auto lerped = [&](int i) {
                            return (frm[i] + float(nxt[i] - frm[i]) * v.frac) * kInv32768;
                        };
                        if (c.channels == 1) {
                            sL = sR = lerped(0);
                        } else {
                            sL = lerped(0);
                            sR = lerped(1);
                        }
                    }
                }
                out[f * 2 + 0] += sL * gL;
                out[f * 2 + 1] += sR * gR;
                if (v.rate == 1.0f) {
                    ++v.cursor;
                } else {
                    v.frac += v.rate;
                    while (v.frac >= 1.0f) { // rate∈[0.75,1.25] 至多一步，while 兜底
                        v.frac -= 1.0f;
                        ++v.cursor;
                    }
                }
                ++f;
            }
            if (!v.loop && v.cursor >= end)
                v.done = true; // 恰好混到末帧的一次性声部当场终止（不待下一块）
            if (v.stream && streamUnderrun > 0) { // 块末发布（观测/验收"欠载静音 ≤ 单次"）
                v.stream->underruns.fetch_add(streamUnderrun, std::memory_order_relaxed);
                streamUnderruns_.fetch_add(streamUnderrun, std::memory_order_relaxed);
            }
        }
        // 母带软限幅（听感验收 2026-10-01：多 kill.wav 同帧叠加 → 求和顶满被硬钳
        // 斩成方波 = 破音）：膝下位零增益位精确透传（常态路径无超越函数）；过载段
        // tanh 渐近压回（膝点 C1 连续，无斜率跳变）；L/R 共用同帧峰值增益（联动
        // 限幅——独立限幅过载时会拉偏声像）。膝点 0.8：常规混音位不变，只驯顶。
        constexpr float kKnee = 0.8f;
        for (uint32_t i = 0; i < frames; ++i) {
            float* fr = out + i * kMixChannels;
            const float peak = std::max(std::fabs(fr[0]), std::fabs(fr[1]));
            if (peak <= kKnee)
                continue;
            const float limited =
                kKnee + (1.0f - kKnee) * std::tanh((peak - kKnee) / (1.0f - kKnee));
            const float gain = limited / peak;
            fr[0] *= gain;
            fr[1] *= gain;
        }
    }

    void AdvanceLocked(uint32_t frames) {
        mixFrames_ += frames; // 静音/离线同径走帧时钟（节流窗锚域一致）
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
    // review 2026-10-02 #14：会话边界复位兜底——pausedAll 跨 Shutdown/Init 周期
    // 残留会使新会话起播即挂起变哑（此前只在宿主侧 MountPlayAudio 打补丁）；
    // 观测计数（欠载帧/告警旗）随周期归零
    impl_->pausedAll = false;
    impl_->streamUnderruns_.store(0, std::memory_order_relaxed);
    impl_->underrunWarned_ = false;
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
    Impl::FeedRef feedJob;
    uint32_t voiceId = 0;
    {
        std::lock_guard<std::mutex> lk(impl_->mtx);
        voiceId = impl_->PlayLocked(clipId, p, std::move(feed), feedJob);
    }
    if (feedJob) { // 填充队列在 mtx 外挂（#4：设备回调不得经 fillMtx_ 间接受阻）
        std::lock_guard<std::mutex> flk(impl_->fillMtx_);
        impl_->fillJobs_.push_back(std::move(feedJob));
        impl_->fillCv_.notify_one();
    }
    return voiceId;
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
    // review 2026-10-02 #14：全停 = 会话清场，暂停意图一并复位——宿主「暂停中停
    // 场/退 Play」后残留 pausedAll 会使下局 BGM 生而挂起（引擎层兜底，替代宿主
    // 侧逐点 SetPaused(false) 补丁）
    impl_->pausedAll = false;
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

void AudioEngine::SetRetriggerCooldown(float cooldownSec) {
    std::lock_guard<std::mutex> lk(impl_->mtx);
    constexpr float kMaxCd = 2.0f; // 上限仅防误用（秒级静默 = 配置错误信号）
    const float s = std::clamp(cooldownSec, 0.0f, kMaxCd);
    impl_->retriggerCdFrames_ = uint32_t(s * kMixSampleRate);
}

float AudioEngine::RetriggerCooldown() const {
    std::lock_guard<std::mutex> lk(impl_->mtx);
    return float(impl_->retriggerCdFrames_) / kMixSampleRate;
}

void AudioEngine::SetPitchJitter(float range) {
    std::lock_guard<std::mutex> lk(impl_->mtx);
    impl_->pitchJitter_ = std::clamp(range, 0.0f, 0.25f); // ±25% 封顶（半音外即走调）
}

float AudioEngine::PitchJitter() const {
    std::lock_guard<std::mutex> lk(impl_->mtx);
    return impl_->pitchJitter_;
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

bool AudioEngine::IsPaused() const {
    std::lock_guard<std::mutex> lk(impl_->mtx);
    return impl_->pausedAll;
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
