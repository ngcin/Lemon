// Lemon 引擎 — 后处理链（M8 批④，02 §4 FX Pass；编辑器 GameView 与 lemon-game
// 两壳共用，防第三套实现——LightCompositor/GameFx.h 同款纪律）。
// 链形态（Med+ 档开 = QualityParams.fxChain；Low 档整链跳过且 RT 不建——
// "关 FX 零成本"机械口径，关链帧与无 FX 版路径逐位同）：
//   [fxSceneRT]  合成/场景段终点（装配方 BeginOffscreenPass 渲入；RGBA8Unorm 拍板）
//   extract      亮部提取 max(c−threshold,0) → bloomA（1/4，线性采样即降采样）
//   down ×2      bloomA→B(1/8)→C(1/16)（双滤波 4-tap box）
//   up ×2        C→B→A（4-tap tent；读写不同 RT 无 hazard）
//   final        显示目标块内单 pass：冲击波 UV 扰动 + bloom 加色 + 色调曲线
//（合并式——bloom 提取基于无扰动图，扰动在最终采样施加，02 §4 顺序语义保持；
//  无活跃波时扰动项为零 = 零额外 pass）
// 职责边界（LightCompositor 同款）：持有本链 4 RT（fxScene/bloomA/B/C——两壳
// RT 生命周期重复 = 第三套温床，故本类自持；EnsureTargets 懒建 + 尺寸变重建）；
// 显示目标（编辑器 gameRT / lemon-game 交换链）归装配方——final 管线按其格式建。
// 冲击波 = 表现层效果（SpawnShockwave/Simulate，渲染帧钟驱动；不入 StateHash/
// 回放——ADR-019 M4"新命令走 World 非 ECS 通道"口径下的渲染侧等价物）。
#pragma once

#include <cstdint>

#include "Core/Math.h"
#include "Renderer/RHI.h"

namespace lemon::renderer {

/// 链参数（调参面；v1 默认值 = 像素风温和档——bloom 阈值贴近白、色调微暖）
struct PostFxParams {
    float bloomThreshold = 0.72f; // LDR 亮部阈值（02 §8 拍板：近白像素；HDR 列 v1.1）
    float bloomIntensity = 0.45f; // bloom 加色强度（final: scene + bloom×此值）
    float toneContrast = 1.06f;   // 0.5 锚 S 曲线对比
    float toneWarmth = 0.10f;     // luma 暖偏强度（r+ g+0.3w b−0.5w）
};

// ---- 纯函数（CPU 单测锚；fx_final.frag 内同式实现——改公式两处同步）----

/// 冲击波环宽（归一化单位；fx_final.frag 内 ringW 同值）
inline constexpr float kShockwaveRingW = 0.06f;
inline constexpr float kShockwaveRingWSq = kShockwaveRingW * kShockwaveRingW;

/// 冲击波环形偏移：波前（dist==radius）处峰值 amp，高斯环衰减。amp 语义 =
/// UV 偏移量上限
inline float ShockwaveOffset(float dist, float radius, float amp) {
    if (amp <= 0.0f) return 0.0f;
    const float t = dist - radius;
    return amp * std::exp(-(t * t) / kShockwaveRingWSq);
}

/// 色调曲线（LDR）：0.5 锚 S 曲线 + luma 暖偏（r+w·l / g+w·0.3·l / b−w·0.5·l）。
/// fx_final.frag 同式（引擎无 Vec3——通道数组签名，Math.h Vec2 同层朴素风格）
inline void ToneApply(float rgb[3], float contrast, float warmth) {
    auto cl = [](float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); };
    for (int i = 0; i < 3; ++i)
        rgb[i] = cl((rgb[i] - 0.5f) * contrast + 0.5f);
    const float luma = cl(0.299f * rgb[0] + 0.587f * rgb[1] + 0.114f * rgb[2]);
    rgb[0] = cl(rgb[0] + warmth * luma);
    rgb[1] = cl(rgb[1] + warmth * 0.3f * luma);
    rgb[2] = cl(rgb[2] - warmth * 0.5f * luma);
}

/// bloom 链 RT 边长派生（full ÷ div，≥4px 防极小目标；单测锚）
inline uint32_t BloomMipSize(uint32_t full, uint32_t div) {
    return full / div < 4u ? 4u : full / div;
}

class PostFx {
public:
    static constexpr uint32_t kMaxWaves = 4; // fxFinal push 常量波槽上限

    /// preheat：4 管线 + 角点几何 + 设备丢失回调（管线重建 + RT 句柄置空——懒建
    /// 口径下次开链帧重建重绑）。finalTargetFormat = 显示目标格式（编辑器
    /// gameRT RGBA8Unorm；lemon-game 交换链格式——LightCompositor 同款参数化）
    void Init(rhi::Device& device, uint32_t samplerSlot, rhi::Format finalTargetFormat);

    ~PostFx() {
        if (device_) device_->RemoveRecreateCallback(recreateCbId_);
    }

    void SetParams(const PostFxParams& p) { params_ = p; }
    const PostFxParams& Params() const { return params_; }

    /// 触发冲击波（uvCenter 归一化屏幕坐标；maxRadius 归一化——相对短边语义由
    /// shader 的 aspect 校正承担；strength = UV 偏移幅度；duration 秒）。满员
    /// （kMaxWaves）顶替最老 + 一次性 WARN
    void SpawnShockwave(Vec2 uvCenter, float maxRadius, float strength, float duration);

    /// 生命推进（渲染帧钟 dt）：半径匀速外扩、幅度随剩余程线性衰减、走完回收
    void Simulate(float dt);
    uint32_t ActiveWaves() const { return waveCount_; }
    /// 清空活跃波（场景切换/重置清表现层瞬态；smoke 帧机尾兜底同用）
    void ClearWaves() { waveCount_ = 0; }

    /// 波态快照（smoke/bench 断言面：状态驱动捕获窗口——帧率无关）
    struct WaveView {
        Vec2 center{};
        float radius = 0.0f;
        float maxRadius = 0.0f;
        float amp = 0.0f;
    };
    uint32_t WavesSnapshot(WaveView out[kMaxWaves]) const {
        for (uint32_t i = 0; i < waveCount_; ++i) {
            out[i] = WaveView{waves_[i].center, waves_[i].radius, waves_[i].maxRadius,
                              waves_[i].amp};
        }
        return waveCount_;
    }

    /// RT 懒建/重建（EnsureLightTargets 同款停稳语义）；fxScene 全分辨率 +
    /// bloomA/B/C = 1/4·1/8·1/16。建时绑槽（M4.7-P0：帧内不改写既有绑定）
    void EnsureTargets(uint32_t w, uint32_t h);

    rhi::Texture SceneRt() const { return sceneRt_.tex; }
    uint32_t SceneRtWidth() const { return sceneRt_.w; }
    uint32_t SceneRtHeight() const { return sceneRt_.h; }
    bool TargetsReady() const { return sceneRt_.tex.IsValid(); }

    /// bloom 链五离屏块（extract→down×2→up×2）。前置：fxSceneRT 本帧已 EndPass
    void RenderChain(rhi::CommandList& cl);

    /// 终段：当前已 Begin 的显示目标块内录 fxFinal（RenderComposite 同款式——
    /// 调用方负责块与豁免段/UI 的先后）
    void RenderFinal(rhi::CommandList& cl);

    /// smoke/bench 断言面（最近一次 FX 帧的实际录制状态；链关帧不更新——消费
    /// 语义 = "链曾全跑/波曾在场"的回显，非当帧渲染路径探测）
    struct FrameInfo {
        uint32_t chainPasses = 0; // RenderChain 本帧录制的离屏块数（5 = 全链）
        uint32_t waveCount = 0;   // fxFinal 消费的活跃波数
    };
    const FrameInfo& LastFrameInfo() const { return last_; }

private:
    struct Rt {
        rhi::Texture tex{};
        uint32_t w = 0, h = 0;
    };
    struct Wave {
        Vec2 center{};
        float radius = 0.0f;
        float maxRadius = 0.0f;
        float speed = 0.0f;
        float amp = 0.0f;
        float strength = 0.0f; // 衰减基线（amp = strength × 剩余程）
    };

    void CreateGeometry();
    void CreatePipelines();
    void EnsureRt(Rt& rt, uint32_t w, uint32_t h, const char* debugName, uint32_t slot);

    rhi::Device* device_ = nullptr;
    rhi::Device::RecreateCallbackId recreateCbId_ = 0;
    rhi::Format finalTargetFormat_ = rhi::Format::RGBA8Unorm;
    uint32_t samplerSlot_ = 0;

    rhi::Pipeline extractPipe_{}, downPipe_{}, upPipe_{}, finalPipe_{};
    rhi::Buffer cornerVB_{}, indexIB_;
    Rt sceneRt_, bloomA_, bloomB_, bloomC_;

    PostFxParams params_{};
    Wave waves_[kMaxWaves]{};
    uint32_t waveCount_ = 0;
    bool waveFullWarned_ = false;
    FrameInfo last_{};
};

} // namespace lemon::renderer
