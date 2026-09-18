// Lemon 引擎 — 精灵合批器（02 §1 ③ 合批 / §3 批键；路径 A 实例化）
// 输入：RenderableManager::Extract 的有序包视图
// 职责：连续同批键 → 写实例进本帧环形 SSBO 段 → 生成批表 → 按混合模式录制 draw
// 实例缓冲弹性扩容：超容量时 WaitIdle 重建 2×（Luma EnsureInstanceBufferCapacity 同款语义）
#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "Renderer/Atlas.h"
#include "Renderer/Renderable.h"
#include "Renderer/RHI.h"
#include "Renderer/SpriteTypes.h"

namespace lemon::renderer {

class SpriteBatcher {
public:
    static constexpr uint32_t kRingFrames = 3;         // 02 §5：环形 SSBO 3 段
    static constexpr uint32_t kInitialCapacity = 4096; // 实例数/段（弹性扩容）

    /// preheat：全部混合模式管线一次建齐（启动期；管线磁盘缓存加速二启）
    void Init(rhi::Device& device, uint32_t samplerLinearSlot, uint32_t samplerPointSlot);

    /// Bake：有序包段（精灵 / 粒子 / 文本，各段内有序、段间层序递增）→ 实例 + 批表。
    /// 跨段同键不合并（批数上限误差 +1，语义无损）
    void Bake(const AtlasRegistry& atlas, std::span<const SpritePacket> packets,
              std::span<const SpritePacket> particlePackets = {},
              std::span<const SpritePacket> textPackets = {});

    /// Record：按批录制命令（每批一次 push constant + 一次 draw）
    void Record(rhi::CommandList& cl, const Mat3x2& viewProj);

    /// 帧推进（EndFrameAndPresent 之后调用；下帧 Bake 写下一段）
    void AdvanceFrame();

    uint32_t LastBatchCount() const { return (uint32_t)batches_.size(); }
    uint32_t LastInstanceCount() const { return lastInstanceCount_; }

    struct Stats {
        double bakeMs = 0;
        double recordMs = 0;
    };
    const Stats& LastStats() const { return stats_; }

private:
    void CreateGeometry();
    void CreatePipelines();
    void EnsureCapacity(uint32_t neededInstances);

    struct Batch {
        SpriteBatchKey key;
        uint32_t instanceOffset; // 本帧段内实例基
        uint32_t instanceCount;
    };

    rhi::Device* device_ = nullptr;
    uint32_t samplerSlots_[2] = {0, 1}; // Linear/Point → bindless 采样器槽
    rhi::Buffer cornerVB_, indexIB_;
    rhi::Buffer instanceRing_;
    SpriteInstance* ringMapped_ = nullptr;
    uint32_t capacity_ = kInitialCapacity;
    uint32_t ringFrame_ = 0;

    rhi::Pipeline pipelines_[4] = {}; // 按 BlendKind 索引（Opaque/Alpha/Additive/Multiply）
    std::vector<Batch> batches_;
    uint32_t lastInstanceCount_ = 0;
    Stats stats_;
};

} // namespace lemon::renderer
