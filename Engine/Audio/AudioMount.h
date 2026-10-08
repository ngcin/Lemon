// Lemon 引擎 — 进 Play 音频装载（M7a 批③；M6c 竖切批/批①/批①b 随迁）
// 自 EditorAppScripts 下沉（搬家非复制，日志字符串逐字节保留）：扫全部 Audio
// 资产 → 缺烤/源新于产物现烤（.lemon/baked/audio/<guidHex>.baked，LBA1 = 48k
// PCM16；目录可写即建——判据允许运行期生成，不允许依赖预存在）→ 装载注册 →
// guid→clipId。失败红字跳过（无声不炸 Play）；重进 Play 全量重装（ResetClips
// 防注册表跨局累积——id 只增不减）。批①b：payload > 1MiB 且未显式 preload →
// 流式注册（RAM 常驻 < 阈值；句柄/环随声部开闭）。
// 编辑器（AssetDatabase 源 + 后台烤制线程复用 BakedPath/BakeStale）与独立运行时
//（AssetIndex 源）共用；批④ lemon-game 装配期消费。
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>

#include "Audio/AudioEngine.h"

namespace lemon::ecs {
class World;
}

namespace lemon::audio {

/// 音频资产条目（装载/烤制最小面——编辑器 AssetEntry / 运行时条目适配产出）
struct AudioItem {
    uint64_t guid = 0;
    std::string srcAbs;                       // 源文件绝对路径
    float loopStart = 0.0f, loopEnd = 0.0f;   // .meta importer 段（0/0 = 全曲循环）
    bool preload = false;                     // 显式整载（默认 >1MiB 走流式）
    ClipFx fx;                                // 听感覆写（哨兵 = 继承；M7c 批②）
};

/// 音频资产源（SpriteRefSource 同款纪律：编辑器 AssetDatabase / 运行时 AssetIndex
/// 双实现——实现侧过滤 type==Audio 且健康）
class AudioSource {
public:
    virtual ~AudioSource() = default;
    /// 项目根（空 = 无项目，装载恒 0 的既有口径）
    virtual std::string ProjectRoot() const = 0;
    virtual void EachAudio(const std::function<void(const AudioItem&)>& fn) const = 0;
};

class AudioMount {
public:
    explicit AudioMount(AudioEngine& engine) : audio_(engine) {}

    /// 进 Play 音频装载：ResetClips（重进全量重装）/pausedAll 会话起点归位（新
    /// World 的 AudioChannel 意图恒 false——游戏要起始暂停会显式再 SetPaused）/
    /// 清 guid 表 / 逐件 EnsureLoaded。返回成功数（流式数进日志）。
    uint32_t MountAll(const AudioSource& src);
    /// 按需装载单 clip：缺烤/陈旧现烤（同步；通常已被后台烤制预热）→ Peek →
    /// 流式分流 → 装载注册 → guid 表。烤制落位 = projectRoot 下
    /// .lemon/baked/audio/<guidHex>.baked。Edit 态试听与装载兜底共用此口。
    bool EnsureLoaded(const AudioItem& item, const std::string& projectRoot,
                      bool* outStreamed = nullptr);
    /// guid → clipId（0 = 未装载/无项目/烤制失败）
    uint32_t ClipIdOfGuid(uint64_t guid) const {
        const auto it = clipIds_.find(guid);
        return it != clipIds_.end() ? it->second : 0;
    }
    /// 已装载 guid 计数（冒烟探针位：smoke-template aud(mount=N) 的 N）
    size_t LoadedCount() const { return clipIds_.size(); }
    /// Play World 音频后端装配（命令表提交引擎 + guid 解析——World::SetAudioBackend
    /// 单点；交互侧 TryEnterPlay 与程序化 --play 双挂点同款纪律）
    void WireBackend(ecs::World& world);

    /// 烤制产物路径（.lemon/baked/audio/<guidHex>.baked；目录由调用方保证存在）
    static std::string BakedPath(const std::string& projectRoot, uint64_t guid);
    /// 缺烤/源新于产物（mtime；后台线程与装载兜底共用同一判定）。.meta（importer
    /// 段：loop/preload + M7c 批② fx 三键）新于产物同样算 stale——loop 冻结在
    /// .baked 头里，不重烤则热改永不生效于已烤 clip（review 2026-10-02 #8；
    /// fx 注册期消费本无需重烤，随段重烤无害）
    static bool BakeStale(const std::string& src, const std::string& dst);

private:
    static uint32_t ResolveThunk(uint64_t guid, void* ctx); // → ClipIdOfGuid

    AudioEngine& audio_;
    std::unordered_map<uint64_t, uint32_t> clipIds_; // guid → clipId（装载产物）
};

} // namespace lemon::audio
