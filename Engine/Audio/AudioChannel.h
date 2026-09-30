// Lemon 引擎 — 音频命令通道（M6c 批②，ADR-015 M3）
// World 持有非 ECS 通道（零重录通道族第八员：Clips/Controllers/Saves/RtUi/Fx/
// Tables/Tweens 之后）——C# 侧所有播放/控制命令当帧 staging（g_world 直达，
// Save 同款域 tick 窗口约定），AudioSystem #20 统一提交 AudioEngine；不入
// StateHash（表现层，指令态）。语义：
//   * Play/PlayAt staging 期同步返回逻辑 voiceId（单调 u32 不回收，Tween 句柄
//     同款）；提交期映射引擎声部。Stop 亦为命令（保序）：同 tick 先 Play 后 Stop
//     = 提交期起后即停；StopAll 即时清记账 + 命令清引擎；
//   * guid→clipId 在 staging 期解析（World::ResolveAudioClip；0 = 未注册即返 0，
//     作者错误当场暴露）；
//   * BGM 单槽在表内（bgmId_）：新 PlayBgm = 旧曲 FadeVoice→0 后停 + 新曲
//     fadeIn（D4 交叉淡出，fadeSec<=0 硬切）；槽随 World 存活——热重载换 C# 域
//     不丢（竖切 C# 静态 _bgm 孤儿声部根除）；
//   * 暂停 = 显式 SetPaused 命令（D5：引擎不自动映射 TimeScale；挂起范围 =
//     AudioEngine::SetPaused 的 ADR M4 口径）。
#pragma once

#include <cstdint>
#include <vector>

#include "Audio/Spatial2D.h"

namespace lemon::audio {

class AudioEngine;

enum class AudioCmdKind : uint8_t {
    Play, PlayAt, Stop, Bgm, BgmStop, GroupVol, MasterVol, SetPaused, StopAll
};

struct AudioCmd {
    AudioCmdKind kind = AudioCmdKind::Play;
    uint8_t group = 1;    // Group 原始值（staging 期防御钳：越界落 Sfx）
    uint8_t loop = 0;
    uint8_t on = 0;       // SetPaused 参数
    uint32_t clipId = 0;  // staging 期已解析；0 = 无效
    uint32_t voice = 0;   // Play*/Bgm = 新发逻辑句柄；Stop = 目标句柄
    float volume = 1.0f;
    float pan = 0.0f;
    float x = 0.0f, y = 0.0f;            // PlayAt 世界位
    float refDist = 256.0f, maxDist = 1024.0f;
    float fadeSec = 0.5f;                // Bgm/BgmStop（D4）
};

/// 逻辑声部记账（staging 期发号；提交期绑定引擎声部；死声部逐次提交回收）
struct AudioLogicalVoice {
    uint32_t id = 0;
    uint32_t engineId = 0; // 0 = 待提交
    bool bgm = false;      // BGM 槽占用（换曲后旧条目翻 false，淡完自然回收）
};

class AudioChannel {
public:
    // ---- staging（主线程；C# 桥 = 域 tick 窗口内）----
    /// 返回逻辑 voiceId（0 = clipId 无效）
    uint32_t StagePlay(uint32_t clipId, int group, float volume, float pan, bool loop);
    /// PlayAt：一次性/循环空间声源（D7：提交期按当帧监听器快照定衰减/声像）
    uint32_t StagePlayAt(uint32_t clipId, float x, float y, float volume, int group,
                         float refDist, float maxDist, bool loop);
    /// BGM 单槽换曲（1 = 受理；0 = clipId 无效）
    int32_t StageBgm(uint32_t clipId, float volume, float fadeSec);
    void StageBgmStop(float fadeSec);
    /// 停逻辑声部（未提交 = 提交期起后即停；已死 = false）
    bool StageStop(uint32_t voiceId);
    void StageGroupVolume(int group, float v);
    void StageMasterVolume(float v);
    void StageSetPaused(bool paused);
    /// 全停：即时清全部记账 + 命令清引擎（场景切换/结算清场）
    void StageStopAll();

    // ---- 提交（AudioSystem 每 tick 一调；engine null = 纯记账，声部即记为亡）----
    void Submit(AudioEngine* engine, const AudioListener& listener);

    // ---- 观测（单测/smoke；记账口径 = 表内存在）----
    uint32_t PendingCount() const { return (uint32_t)pending_.size(); }
    uint32_t bgmVoice() const { return bgmId_; }
    bool LogicalAlive(uint32_t id) const;
    uint32_t LogicalCount() const { return (uint32_t)voices_.size(); }
    bool pausedStaged() const { return paused_; }

    void Clear();

private:
    static uint8_t ClampGroup(int group);
    uint32_t AllocLogical();
    AudioLogicalVoice* FindLogical(uint32_t id);

    std::vector<AudioCmd> pending_;
    std::vector<AudioLogicalVoice> voices_;
    uint32_t nextId_ = 1; // 逻辑句柄单调发号不回收
    uint32_t bgmId_ = 0;  // BGM 槽（0 = 无曲）
    bool paused_ = false;
};

} // namespace lemon::audio
