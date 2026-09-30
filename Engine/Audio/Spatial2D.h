// Lemon 引擎 — 2D 空间化数学（M6c 批②，ADR-015 M5）
// 纯函数：衰减线性、声像监听器半宽归一（等功率增益对在 AudioEngine::PanGains）。
// 监听器 = 宿主每帧推的活动相机世界位（编辑器 Play = GameView 相机；一帧延迟
// 口径 = FeedGameUiInput 先例）。坚决不做 3D/滤波/混响（08 §4 砍单）。
#pragma once

#include <algorithm>
#include <cmath>

#include "Core/Math.h"

namespace lemon::audio {

/// 监听器状态（World 持有；编辑器/M7a 运行时每帧推）
struct AudioListener {
    Vec2 center{0, 0};
    float halfWidth = 640.0f; // 声像归一半宽（相机视口半宽；下限防 0 除）
};

/// 衰减 + 声像一次算（ADR M5）：d<=refDist 全增益 1 → d>=maxDist 线性到 0；
/// pan = 源 x 相对监听器半宽归一 [-1,1]（x 同轴、y 不参与声像——2D 俯视口径）。
inline void ComputeSpatial(Vec2 src, const AudioListener& listener, float refDist,
                           float maxDist, float& outGain, float& outPan) {
    const float dx = src.x - listener.center.x;
    const float dy = src.y - listener.center.y;
    const float d = std::sqrt(dx * dx + dy * dy);
    const float ref = refDist > 0.0f ? refDist : 0.0f;
    // max<=ref 退化（作者手误）= 全程可闻而非全哑（钳 0 除的友好侧）
    if (maxDist <= ref || d <= ref)
        outGain = 1.0f;
    else
        outGain = d >= maxDist ? 0.0f : 1.0f - (d - ref) / (maxDist - ref);
    const float halfW = listener.halfWidth > 1.0f ? listener.halfWidth : 1.0f;
    outPan = std::clamp(dx / halfW, -1.0f, 1.0f);
}

} // namespace lemon::audio
