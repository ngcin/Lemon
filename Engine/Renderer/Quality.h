// Lemon 引擎 — 质量分级（02 §2 纪律表：Low/Med/High 映射粒子预算倍率等）
// 自动降级：帧时间 EMA 超阈值持续 N 秒 → 降一档（只降不升，避免抖动；手动可拉回）
// M8 批② 兑现头注"M8 扩展"（ADR-019 M5）：lightRtScale + lightBudget——编辑器壳
// 首接；批③ 游戏壳接入（GameEntry 渲染帧钟 EMA 同款——两壳齐）。M5+ 扩展：
// MSAA on/off、后处理链长度（批④ Med+ 档）；粒子预算联动 02 §6 全局预算
#pragma once

#include <cstdint>

#include "Core/Log.h"

namespace lemon::renderer {

enum class QualityTier : uint8_t { Low = 0, Med = 1, High = 2 };

struct QualityParams {
    uint32_t particleBudget = 50000;
    float lightRtScale = 1.0f;  // M8 批②（ADR-019 M5/D8）：lightRT = gameRT × 此系数
                                //（Low=1/4 / Med=1/2 / High=全分辨率）
    uint32_t lightBudget = 64;  // 光源数上限/帧（Low=32；超出截断 + 一次性 WARN）
    // M5+：msaa / fxChain / shadowQuality ...
};

constexpr QualityParams TierParams(QualityTier t) {
    switch (t) {
        case QualityTier::Low: return {20000, 0.25f, 32};
        case QualityTier::Med: return {50000, 0.5f, 64};
        default: return {100000, 1.0f, 64};
    }
}
inline const char* TierName(QualityTier t) {
    switch (t) {
        case QualityTier::Low: return "Low";
        case QualityTier::Med: return "Med";
        default: return "High";
    }
}

class QualityManager {
public:
    static constexpr double kSlowFrameMs = 20.0;   // 低于 50fps 视为过载
    static constexpr double kDowngradeSeconds = 2.0;

    explicit QualityManager(QualityTier start = QualityTier::High)
        : tier_(start), params_(TierParams(start)) {}

    QualityTier Current() const { return tier_; }
    const QualityParams& Params() const { return params_; }
    double EmaFrameMs() const { return emaMs_; }

    /// 每帧调用；降级回调（如粒子 SetBudget）由调用方在 OnTierChanged 执行
    void Update(double frameMs, double dt) {
        emaMs_ = emaMs_ < 0 ? frameMs : 0.9 * emaMs_ + 0.1 * frameMs;
        if (tier_ == QualityTier::Low) return; // 已到底
        if (emaMs_ > kSlowFrameMs) {
            overSeconds_ += dt;
            if (overSeconds_ >= kDowngradeSeconds) {
                tier_ = (QualityTier)((int)tier_ - 1);
                params_ = TierParams(tier_);
                overSeconds_ = 0;
                LEMON_WARN("quality downgrade -> %s (ema %.1fms)", TierName(tier_), emaMs_);
            }
        } else {
            overSeconds_ = 0;
        }
    }

    void SetTier(QualityTier t) {
        tier_ = t;
        params_ = TierParams(t);
        overSeconds_ = 0;
    }

private:
    QualityTier tier_;
    QualityParams params_ = TierParams(QualityTier::High);
    double emaMs_ = -1;
    double overSeconds_ = 0;
};

} // namespace lemon::renderer
