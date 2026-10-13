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

    /// preheat：全部混合模式管线一次建齐（启动期；管线磁盘缓存加速二启）。
    /// colorFormat = 目标附件格式（交换链 pass 用交换链格式；离屏视口用 RT 格式——
    /// 动态渲染下管线唯一格式依赖，格式不符 = 验证层 VUID 拦截）。
    /// ringSlot = 实例环 SSBO 数组槽（多视口合批器各占一槽，帧内不改写；M4.7-P0）
    void Init(rhi::Device& device, uint32_t samplerLinearSlot, uint32_t samplerPointSlot,
              rhi::Format colorFormat = rhi::Format::BGRA8Unorm,
              uint32_t ringSlot = 0);

    /// Bake：有序包段（精灵 / 粒子 / 文本，各段内有序、段间层序递增）→ 实例 + 批表。
    /// 跨段同键不合并（批数上限误差 +1，语义无损）。tailPackets = 尾段（视口专属
    /// overlay/血条等，段序 = 原"合成一个 vector"的拼接序）——大场免每帧堆分配
    /// 与全量拷贝（2026-09-26 渲染提取批）。
    /// append（M8 批② 光贴图分段）：不清批表、实例偏移自本帧已烘数续接——开灯帧
    /// 两段烘焙两段录制（sceneRT 段 + 合成后豁免段）。**分段帧必须先 Reserve
    /// 两段总量**（段间扩容会重建实例环，首段已录命令引用旧 buffer = 提交期 UAF）。
    void Bake(const AtlasRegistry& atlas, std::span<const SpritePacket> packets,
              std::span<const SpritePacket> particlePackets = {},
              std::span<const SpritePacket> textPackets = {},
              std::span<const SpritePacket> tailPackets = {}, bool append = false);

    /// Record：按批录制命令（每批一次 push constant + 一次 draw）
    void Record(rhi::CommandList& cl, const Mat3x2& viewProj);

    /// RecordRange：只录批表区间 [beginBatch, beginBatch+batchCount)（M8 批② 光贴图
    /// 分段——豁免段在 gameRT 块内二段录制；批表按段追加序，区间 = 段边界）
    void RecordRange(rhi::CommandList& cl, const Mat3x2& viewProj, size_t beginBatch,
                     size_t batchCount);

    /// 分段帧前置容量声明（两段总量一次 EnsureCapacity——见 Bake append 注）
    void Reserve(uint32_t totalInstances) { EnsureCapacity(totalInstances); }

    /// 帧推进（EndFrameAndPresent 之后调用；下帧 Bake 写下一段）
    void AdvanceFrame();

    /// 析构反注册设备丢失回调（M9）：本类须先于 Device 销毁（编辑器/样例成员序
    /// 天然满足；回调捕获裸 this，不摘除则设备丢失重建 = UAF）
    ~SpriteBatcher() {
        if (device_) device_->RemoveRecreateCallback(recreateCbId_);
    }

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
    rhi::Device::RecreateCallbackId recreateCbId_ = 0; // Init 登记（M9 反注册用）
    rhi::Format colorFormat_ = rhi::Format::BGRA8Unorm;
    uint32_t samplerSlots_[2] = {0, 1}; // Linear/Point → bindless 采样器槽
    uint32_t ringSlot_ = 0;             // 实例环 SSBO 数组槽（binding 2 下标）
    rhi::Buffer cornerVB_, indexIB_;
    rhi::Buffer instanceRing_;
    SpriteInstance* ringMapped_ = nullptr;
    uint32_t capacity_ = kInitialCapacity;
    uint32_t ringFrame_ = 0;

    rhi::Pipeline pipelines_[4] = {}; // 按 BlendKind 索引（Opaque/Alpha/Additive/Multiply）
    std::vector<Batch> batches_;
    uint32_t appendWritten_ = 0;      // 本帧已烘实例数（append Bake 续接基；帧/非追加复位）
    uint32_t lastInstanceCount_ = 0;
    Stats stats_;
};

} // namespace lemon::renderer
