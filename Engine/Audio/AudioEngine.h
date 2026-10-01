// Lemon 引擎 — 音频核心（M6c 批⓪，ADR-015）
// miniaudio 后端封装：设备回调线程混音（f32 累加）、voice 池、组/主音量、
// 静音降级一等公民。纪律：本头文件零 miniaudio 类型（Pimpl，同 Window/RHI）；
// 全引擎音频域恒定 48kHz / stereo / PCM16 源（烤制期归一，运行时零解码零重采样）。
#pragma once

#include <cstdint>
#include <memory>
#include <vector>

namespace lemon::audio {

inline constexpr int kMixSampleRate = 48000; // ADR-015 M2：全引擎音频域恒定
inline constexpr int kMixChannels = 2;
inline constexpr int kMaxVoices = 64;        // 声部池上限；满时偷最旧一次性声部
inline constexpr int kMaxVoicesPerClip = 4;  // 同 clip 并发上限；重触发超限偷最老（5ms 释放）
inline constexpr uint32_t kStreamThresholdBytes = 1u << 20; // >1MiB 走流式（批①b 消费：
                                                            // 装载侧分流判据，audioPreload 覆盖）

enum class Group : uint8_t { Bgm = 0, Sfx, Ui, Count };
inline constexpr int kGroupCount = static_cast<int>(Group::Count);

// 已解码 PCM 声源（批⓪ 测试直灌；批① 起 = .baked 装载产物。契约：48k 交错 PCM16）
struct ClipData {
    const int16_t* pcm = nullptr;
    uint32_t frameCount = 0;
    uint16_t channels = 1;      // 1 | 2
    uint32_t loopStart = 0;     // 帧（loop 时游标回卷点）
    uint32_t loopEnd = 0;       // 帧；0 = 尾（== frameCount）
};

struct PlayParams {
    float volume = 1.0f;
    float pan = 0.0f;           // -1..1，等功率声像
    Group group = Group::Sfx;
    bool loop = false;          // true 时循环区间 = clip 的 [loopStart, loopEnd)
    float fadeInSec = 0.0f;     // M6c 批②（D4）：起播 0 增益线性爬到 volume；<=0 = 直起
};

struct InitOptions {
    bool forceSilent = false; // 单测/CI 确定性；LEMON_AUDIO=off 环境变量等效
};

class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();
    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    // 恒返回 true：设备初始化失败/强制静音 = 降级为静音模式（红字 Warn，不阻断）——
    // "无音频设备不崩"判据（08 §2 M6c）。重复 Init 幂等。
    bool Init(const InitOptions& opts = {});
    void Shutdown();

    // 声源注册（数据拷入引擎自有存储；线程安全）。返回 clipId，0 = 参数非法。
    uint32_t RegisterClip(const ClipData& data);
    /// 移动重载（批①：装载 vector 直迁——21MB BGM 免双拷贝瞬时翻峰）
    uint32_t RegisterClip(std::vector<int16_t>&& pcm, uint16_t channels, uint32_t frameCount,
                          uint32_t loopStart, uint32_t loopEnd);
    /// 流式注册（批①b，ADR-015 M2）：长 clip 免整载——文件保持 .baked 原样，
    /// 声部起播时开载荷句柄 + 256KiB SPSC 环（设备模式专用线程填充；起播主线程
    /// open + 头解析 + 预填环，保首回调零欠载）。返回 clipId，0 = 头校验失败。
    uint32_t RegisterStreamClip(const char* path);
    void UnregisterClip(uint32_t clipId); // 引用中的声部当场终止
    void ResetClips();                    // 全清 + 停声（EnterPlay 重装前调——注册表只增不减，防跨局累积）

    // 播放控制（主线程调用；内部短临界区）。返回 voiceId，0 = 池满且无可偷/clip 无效。
    uint32_t Play(uint32_t clipId, const PlayParams& p = {});
    bool Stop(uint32_t voiceId);
    void StopAll();

    // M6c 批②（ADR-015 M4/D4）：声部音量包络——seconds 内从当前有效音量线性到
    // targetVolume；stopWhenDone 且到 0 = 声部终结（BGM 交叉淡出走此径）。
    // seconds<=0 = 硬切立即到位。在途包络期间 SetVoiceParams 的 volume 于包络
    // 终态后生效（BGM 不吃空间更新，实际无交互）。
    bool FadeVoice(uint32_t voiceId, float targetVolume, float seconds, bool stopWhenDone);
    /// M6c 批②：活声部参数热更（AudioSystem 逐 tick 空间化：volume*衰减 + 声像）
    bool SetVoiceParams(uint32_t voiceId, float volume, float pan);

    void SetGroupVolume(Group g, float v);
    void SetMasterVolume(float v);
    float GroupVolume(Group g) const;
    float MasterVolume() const;

    // ---- 同 clip 重触发治理（听感验收 2026-10-01：pickup 密集"放鞭炮"——机枪效应）----
    /// 重触发节流：同 clip 非循环播放在距上次起播 cooldownSec 内的新请求被丢弃
    /// （返回 0）。时钟 = 混音帧域（设备/静音同径）。默认 0.045s（≈22Hz 上限）；
    /// 0 = 关闭。业界 throttle 同构；per-资产覆写归 meta（批③）。
    void SetRetriggerCooldown(float cooldownSec);
    float RetriggerCooldown() const;
    /// 音高微扰：Sfx/Ui 组非循环整载声部起播时随机 ±range（1-tap 线性插值，非
    /// 采样率转换重采样器——ADR-015 M2"零重采样"按性能口径不变；BGM 组/循环/
    /// 流式不扰保乐律精确）。去同素材连发的相干叠加/拍频。默认 0.02；0 = 关闭。
    /// 微扰序列固定种子（表现层装饰，不入状态哈希/金回放）。
    void SetPitchJitter(float range);
    float PitchJitter() const;

    // 暂停语义（ADR-015 M4）：循环声部与 BGM 组声部级挂起；一次性 SFX 自然放完；
    // UI 组永不挂起（暂停菜单按钮音仍可响）。设备级 pause 不用（会连 UI 音一起哑）。
    void SetPaused(bool paused);

    // 每帧一次（主线程）：声部回收记账；静音模式下按 dt 推进逻辑游标。
    void Tick(float dtSeconds = 0.0f);

    // 观测（静音模式同样有效——逻辑声部记账是降级路径的一部分）
    int ActiveVoiceCount() const;
    bool VoiceAlive(uint32_t voiceId) const;

    // 离线混音（单测专用）：把活跃声部混 outFrames 帧进 out（交错 stereo f32）并推进
    // 游标。仅静音模式可用（设备模式下游标归音频线程所有，双写违规红字拒绝）。
    void MixOffline(float* outInterleaved, uint32_t outFrames);

    // 静音模式推进逻辑游标（单测确定性驱动；设备模式 no-op）
    void AdvanceSilentFrames(uint32_t frames);

    /// 单测/离线专用：同步喂满全部活跃流式声部的环（设备模式由专用线程承担，
    /// 调用即红字拒绝——双生产者违规）。静音/离线测试手动驱动生产者侧。
    void PumpStreams();

    /// 流式声部累计欠载帧数（观测/验收"欠载静音 ≤ 单次"判据；批①b）
    uint64_t StreamUnderrunFrames() const;

    bool silent() const;  // 静音模式（降级或强制）——逻辑声部仍记账
    bool inited() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace lemon::audio
