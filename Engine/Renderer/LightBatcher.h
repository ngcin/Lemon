// Lemon 引擎 — 光贴图合批器（M8 批②，ADR-019 M1；SpriteBatcher 镜像结构）
// 职责：LightItem 列表 → 按 blend 分 3 桶（Additive→Screen→Subtract 固定绘序，
// Subtract 最后 = blob 暗斑从累积光里扣）写实例进本帧环形 SSBO 段 → 每桶一次
// push constant + draw。绘制序桶间固定、桶内稳定（提取序）——光照叠加无排序契约。
// 顶点/实例布局与着色器对应见 LightTypes.h 与 light.vert 头注。
#pragma once

#include <cstdint>
#include <span>

#include "Core/Math.h"
#include "Renderer/LightTypes.h"
#include "Renderer/RHI.h"

namespace lemon::renderer {

class LightBatcher {
public:
    static constexpr uint32_t kRingFrames = 3;        // 与 SpriteBatcher 同环形约定
    static constexpr uint32_t kInitialCapacity = 256; // 光数上限 64（Low 32）+ 余量
    static constexpr uint32_t kBlendCount = 3;        // Additive/Screen/Subtract

    /// preheat：3 混合管线一次建齐（启动期）。colorFormat = lightRT 格式（拍板
    /// RGBA8Unorm，02 §8）；ringSlot = 实例环数组槽（编辑器 = 2，sprite 0/1 已占）。
    void Init(rhi::Device& device, rhi::Format colorFormat, uint32_t ringSlot);

    /// Bake：items（compositor 已滤 Global/无效蒙版）+ blobs（批③：椭圆暗斑，
    /// 映射见 FillBlobInstance）→ 3 桶实例 + 批表。blob 计入 Subtract 桶尾部
    /// （暗斑最后扣）。blend 越界值钳回合法域（序列化数据驱动，Renderable
    /// sanitize 同款一次性 WARN）。atlas = 蒙版 UV/页槽解析源（无蒙版灯可为
    /// nullptr——但含 SpriteMask 灯时传 null = 该灯跳过 + WARN）
    void Bake(const AtlasRegistry* atlas, std::span<const LightItem> items,
              std::span<const BlobItem> blobs = {});

    /// Record：每非空桶一次绑定 + draw（桶序 = Additive→Screen→Subtract）
    void Record(rhi::CommandList& cl, const Mat3x2& viewProj);

    void AdvanceFrame();

    ~LightBatcher() {
        if (device_) device_->RemoveRecreateCallback(recreateCbId_);
    }

    uint32_t LastInstanceCount() const { return lastInstanceCount_; }

    // 四边形几何访问（LightCompositor 的 composite 全屏四边形复用——同 aCorner 输入）
    rhi::Buffer CornerVB() const { return cornerVB_; }
    rhi::Buffer IndexIB() const { return indexIB_; }
    // 实例环绑定态（composite 段显式化绑定用——不读实例，仅共享布局确定性）
    rhi::Buffer RingBuffer() const { return instanceRing_; }
    uint32_t RingSlot() const { return ringSlot_; }

private:
    void CreateGeometry();
    void CreatePipelines();
    void EnsureCapacity(uint32_t neededInstances);

    struct Bucket {
        uint32_t instanceOffset; // 本帧段内实例基
        uint32_t instanceCount;
    };

    rhi::Device* device_ = nullptr;
    rhi::Device::RecreateCallbackId recreateCbId_ = 0;
    rhi::Format colorFormat_ = rhi::Format::RGBA8Unorm;
    uint32_t ringSlot_ = 2;
    rhi::Buffer cornerVB_, indexIB_;
    rhi::Buffer instanceRing_;
    LightInstance* ringMapped_ = nullptr;
    uint32_t capacity_ = kInitialCapacity;
    uint32_t ringFrame_ = 0;

    rhi::Pipeline pipelines_[kBlendCount] = {};
    Bucket buckets_[kBlendCount] = {};
    uint32_t lastInstanceCount_ = 0;
    bool blendWarned_ = false;   // blend 钳制告警只响一次
    bool maskFailWarned_ = false; // 蒙版解析失败告警只响一次（批③）
};

} // namespace lemon::renderer
