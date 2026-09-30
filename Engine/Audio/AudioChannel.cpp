// Lemon 引擎 — 音频命令通道实现（M6c 批②，ADR-015 M3）
// 提交语义见 AudioChannel.h 头说明；引擎指针 null = 纯记账（无声宿主/测试），
// 播放类命令的声部即记为亡（逻辑发号仍消耗——调用方拿到非零句柄但即刻失效，
// 与"引擎拒绝 = 返回 0"的可听差异只在无引擎环境存在，smoke 断言按记账口径）。
#include "Audio/AudioChannel.h"

#include <algorithm>

#include "Audio/AudioEngine.h"

namespace lemon::audio {

uint8_t AudioChannel::ClampGroup(int group) {
    // 防御钳（review 2026-09-30 热修④ 口径）：越界（含负）落 Sfx
    if (group <= 0)
        return group == 0 ? 0 : 1;
    return group >= kGroupCount ? 1 : static_cast<uint8_t>(group);
}

uint32_t AudioChannel::AllocLogical() {
    voices_.push_back({nextId_, 0, false});
    return nextId_++;
}

AudioLogicalVoice* AudioChannel::FindLogical(uint32_t id) {
    for (auto& v : voices_)
        if (v.id == id) return &v;
    return nullptr;
}

namespace {
// 引擎拒绝（池满/clip 无效/无引擎）= 条目即亡（发号不回收）
void KillLogical(std::vector<AudioLogicalVoice>& voices, AudioLogicalVoice* lv) {
    if (lv) voices.erase(voices.begin() + (lv - voices.data()));
}
} // namespace

uint32_t AudioChannel::StagePlay(uint32_t clipId, int group, float volume, float pan,
                                 bool loop) {
    if (clipId == 0) return 0;
    AudioCmd c;
    c.kind = AudioCmdKind::Play;
    c.clipId = clipId;
    c.group = ClampGroup(group);
    c.volume = volume;
    c.pan = pan;
    c.loop = loop ? 1 : 0;
    c.voice = AllocLogical();
    pending_.push_back(c);
    return c.voice;
}

uint32_t AudioChannel::StagePlayAt(uint32_t clipId, float x, float y, float volume,
                                   int group, float refDist, float maxDist, bool loop) {
    if (clipId == 0) return 0;
    AudioCmd c;
    c.kind = AudioCmdKind::PlayAt;
    c.clipId = clipId;
    c.x = x;
    c.y = y;
    c.volume = volume;
    c.group = ClampGroup(group);
    c.refDist = refDist;
    c.maxDist = maxDist;
    c.loop = loop ? 1 : 0;
    c.voice = AllocLogical();
    pending_.push_back(c);
    return c.voice;
}

int32_t AudioChannel::StageBgm(uint32_t clipId, float volume, float fadeSec) {
    if (clipId == 0) return 0;
    AudioCmd c;
    c.kind = AudioCmdKind::Bgm;
    c.clipId = clipId;
    c.volume = volume;
    c.fadeSec = fadeSec;
    c.voice = AllocLogical();
    pending_.push_back(c);
    return 1;
}

void AudioChannel::StageBgmStop(float fadeSec) {
    AudioCmd c;
    c.kind = AudioCmdKind::BgmStop;
    c.fadeSec = fadeSec;
    pending_.push_back(c);
}

bool AudioChannel::StageStop(uint32_t voiceId) {
    if (voiceId == 0) return false;
    // 未提交的 Play*：直接撤命令 + 销记账（引擎从未起声部）
    for (size_t i = 0; i < pending_.size(); ++i) {
        const AudioCmd& c = pending_[i];
        if ((c.kind == AudioCmdKind::Play || c.kind == AudioCmdKind::PlayAt ||
             c.kind == AudioCmdKind::Bgm) &&
            c.voice == voiceId) {
            pending_.erase(pending_.begin() + i);
            if (AudioLogicalVoice* lv = FindLogical(voiceId))
                KillLogical(voices_, lv);
            if (bgmId_ == voiceId) bgmId_ = 0;
            return true;
        }
    }
    // 已提交（或前 tick 发号）：保序 Stop 命令，提交期停引擎声部
    if (FindLogical(voiceId) != nullptr) {
        AudioCmd c;
        c.kind = AudioCmdKind::Stop;
        c.voice = voiceId;
        pending_.push_back(c);
        return true;
    }
    return false;
}

void AudioChannel::StageGroupVolume(int group, float v) {
    AudioCmd c;
    c.kind = AudioCmdKind::GroupVol;
    c.group = ClampGroup(group);
    c.volume = v;
    pending_.push_back(c);
}

void AudioChannel::StageMasterVolume(float v) {
    AudioCmd c;
    c.kind = AudioCmdKind::MasterVol;
    c.volume = v;
    pending_.push_back(c);
}

void AudioChannel::StageSetPaused(bool paused) {
    AudioCmd c;
    c.kind = AudioCmdKind::SetPaused;
    c.on = paused ? 1 : 0;
    pending_.push_back(c);
}

void AudioChannel::StageStopAll() {
    voices_.clear(); // 记账即清：其后同 tick 的 Play 照常（表内新条目）
    bgmId_ = 0;
    AudioCmd c;
    c.kind = AudioCmdKind::StopAll;
    pending_.push_back(c);
}

void AudioChannel::Submit(AudioEngine* engine, const AudioListener& listener) {
    for (const AudioCmd& c : pending_) {
        switch (c.kind) {
        case AudioCmdKind::Play:
        case AudioCmdKind::PlayAt: {
            AudioLogicalVoice* lv = FindLogical(c.voice);
            if (!lv) break; // StopAll 后条目已清 = 命令作废（僵尸播放防线）
            PlayParams p;
            p.group = static_cast<Group>(c.group);
            p.loop = c.loop != 0;
            p.volume = c.volume;
            p.pan = c.pan;
            if (c.kind == AudioCmdKind::PlayAt) { // D7：当帧监听器快照定空间参数
                float gain = 1.0f, pan = 0.0f;
                ComputeSpatial(Vec2{c.x, c.y}, listener, c.refDist, c.maxDist, gain, pan);
                p.volume = c.volume * gain;
                p.pan = pan;
            }
            lv->engineId = engine ? engine->Play(c.clipId, p) : 0;
            if (lv->engineId == 0)
                KillLogical(voices_, lv); // 引擎拒绝 = 亡
            break;
        }
        case AudioCmdKind::Bgm: {
            // 换曲（D4 交叉淡出）：旧曲淡出后停；新曲 fadeIn 到位。fadeSec<=0 = 硬切
            if (bgmId_ != 0) {
                if (AudioLogicalVoice* old = FindLogical(bgmId_)) {
                    old->bgm = false; // 旧条目让出 BGM 槽，淡完自然回收
                    if (engine && old->engineId != 0) {
                        if (c.fadeSec > 0.0f)
                            engine->FadeVoice(old->engineId, 0.0f, c.fadeSec, true);
                        else
                            engine->Stop(old->engineId);
                    }
                }
            }
            AudioLogicalVoice* lv = FindLogical(c.voice);
            if (!lv) break;
            PlayParams p;
            p.group = Group::Bgm;
            p.loop = true;
            p.volume = c.volume;
            p.fadeInSec = std::max(c.fadeSec, 0.0f);
            lv->bgm = true;
            lv->engineId = engine ? engine->Play(c.clipId, p) : 0;
            bgmId_ = lv->engineId != 0 ? lv->id : 0;
            if (lv->engineId == 0)
                KillLogical(voices_, lv);
            break;
        }
        case AudioCmdKind::BgmStop: {
            if (bgmId_ != 0) {
                if (AudioLogicalVoice* cur = FindLogical(bgmId_)) {
                    cur->bgm = false;
                    if (engine && cur->engineId != 0) {
                        if (c.fadeSec > 0.0f)
                            engine->FadeVoice(cur->engineId, 0.0f, c.fadeSec, true);
                        else
                            engine->Stop(cur->engineId);
                    }
                }
                bgmId_ = 0;
            }
            break;
        }
        case AudioCmdKind::Stop: {
            if (AudioLogicalVoice* lv = FindLogical(c.voice)) {
                if (engine && lv->engineId != 0)
                    engine->Stop(lv->engineId);
                lv->engineId = 0; // 停后即亡，下次提交回收
            }
            break;
        }
        case AudioCmdKind::GroupVol:
            if (engine) engine->SetGroupVolume(static_cast<Group>(c.group), c.volume);
            break;
        case AudioCmdKind::MasterVol:
            if (engine) engine->SetMasterVolume(c.volume);
            break;
        case AudioCmdKind::SetPaused:
            paused_ = c.on != 0;
            if (engine) engine->SetPaused(paused_);
            break;
        case AudioCmdKind::StopAll:
            if (engine) engine->StopAll();
            break;
        }
    }
    pending_.clear();
    // 死条目回收（引擎声部亡 = 一次性放完/被停/淡出到 0；engineId==0 的存活条目
    // 只属"待发命令未及处理"——本函数处理完后不应存在，防御性同收）
    voices_.erase(std::remove_if(voices_.begin(), voices_.end(),
                                 [&](const AudioLogicalVoice& v) {
                                     return v.engineId == 0 ||
                                            (engine && !engine->VoiceAlive(v.engineId));
                                 }),
                  voices_.end());
}

bool AudioChannel::LogicalAlive(uint32_t id) const {
    for (const auto& v : voices_)
        if (v.id == id) return true;
    return false;
}

void AudioChannel::Clear() {
    pending_.clear();
    voices_.clear();
    bgmId_ = 0;
    paused_ = false;
    nextId_ = 1;
}

} // namespace lemon::audio
