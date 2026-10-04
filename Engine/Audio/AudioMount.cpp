// Lemon 引擎 — 进 Play 音频装载实现（M7a 批③ 自 EditorAppScripts 搬家，逐行同源）
#include "Audio/AudioMount.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <vector>

#include "Audio/BakedClip.h"
#include "Core/Log.h"
#include "ECS/World.h"

namespace lemon::audio {
namespace fs = std::filesystem;

std::string AudioMount::BakedPath(const std::string& root, uint64_t guid) {
    char hex[17];
    std::snprintf(hex, sizeof(hex), "%016llx", (unsigned long long)guid);
    return root + "/.lemon/baked/audio/" + hex + ".baked";
}

bool AudioMount::BakeStale(const std::string& src, const std::string& dst) {
    std::error_code ec;
    if (!fs::exists(dst, ec)) return true;
    const auto dstT = fs::last_write_time(dst, ec);
    if (ec) return true;
    const auto srcT = fs::last_write_time(src, ec);
    if (!ec && srcT > dstT) return true;
    const auto metaT = fs::last_write_time(src + ".meta", ec);
    return !ec && metaT > dstT;
}

uint32_t AudioMount::MountAll(const AudioSource& src) {
    audio_.ResetClips();
    // 会话起点归位（2026-10-01 真人验收发现）：引擎 pausedAll 跨会话残留——上局
    // 游戏暂停中 StopPlay 再 Play，新 BGM 起播即挂起变哑。新 World 的 AudioChannel
    // 意图恒 false（游戏要起始暂停会显式再 SetPaused），此处对齐引擎侧。
    audio_.SetPaused(false);
    clipIds_.clear();
    const std::string root = src.ProjectRoot();
    if (root.empty()) return 0; // 无项目 = 零资产零装载
    std::error_code ec;
    fs::create_directories(fs::path(root) / ".lemon" / "baked" / "audio", ec);
    uint32_t ok = 0, failed = 0, streamed = 0;
    src.EachAudio([&](const AudioItem& item) {
        bool isStream = false;
        if (EnsureLoaded(item, root, &isStream)) {
            ++ok;
            if (isStream) ++streamed; // 批①b：>1MiB 未 preload = 流式（RAM 常驻证据）
        } else {
            ++failed;
        }
    });
    if (ok)
        LEMON_LOG("进 Play 音频装载：%u 成功（流式 %u）%s", ok, streamed,
                  failed ? "" : "，全部就绪");
    else if (failed)
        LEMON_WARN("进 Play 音频装载：0 成功 / %u 失败（详见上方红字）", failed);
    return ok;
}

bool AudioMount::EnsureLoaded(const AudioItem& item, const std::string& projectRoot,
                              bool* outStreamed) {
    if (outStreamed) *outStreamed = false;
    if (const auto it = clipIds_.find(item.guid); it != clipIds_.end()) return true;
    const std::string dst = BakedPath(projectRoot, item.guid);
    if (BakeStale(item.srcAbs, dst)) {
        const auto t0 = std::chrono::steady_clock::now();
        if (!audio::BakeAudioFile(item.srcAbs.c_str(), dst.c_str(), item.loopStart,
                                  item.loopEnd))
            return false;
        // 批①b：同步烤超 100ms 红字（后台预热未命中——首播顿挫面，量级证据）
        const double ms = std::chrono::duration<double, std::milli>(
                              std::chrono::steady_clock::now() - t0)
                              .count();
        if (ms > 100.0)
            LEMON_WARN("audio: 同步烤制耗时 %.0fms（后台预热未命中）：%s", ms,
                       item.srcAbs.c_str());
    }
    BakedClipInfo info;
    if (!PeekBakedClip(dst.c_str(), info)) return false;
    if (info.payloadBytes > kStreamThresholdBytes && !item.preload) {
        const uint32_t streamId = audio_.RegisterStreamClip(dst.c_str());
        if (streamId == 0) return false;
        clipIds_[item.guid] = streamId;
        if (outStreamed) *outStreamed = true;
        return true;
    }
    std::vector<int16_t> pcm;
    if (!LoadBakedClip(dst.c_str(), pcm, info)) return false;
    const uint32_t clipId = audio_.RegisterClip(std::move(pcm), info.channels,
                                                info.frameCount, info.loopStart,
                                                info.loopEnd);
    if (clipId == 0) return false;
    clipIds_[item.guid] = clipId;
    return true;
}

void AudioMount::WireBackend(ecs::World& world) {
    world.SetAudioBackend(&audio_, &AudioMount::ResolveThunk, this);
}

uint32_t AudioMount::ResolveThunk(uint64_t guid, void* ctx) {
    return static_cast<AudioMount*>(ctx)->ClipIdOfGuid(guid);
}

} // namespace lemon::audio
