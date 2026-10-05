// Lemon 引擎 — 2D 数学库（02/03 文档的公共地基）
// 约定：
//   * 世界坐标 = 像素坐标，Y 轴向下（与屏幕一致，2D 品类心智默认）
//   * Mat3x2 行主序存储 {a,b,c,d,e,f}，变换 p' = (a·x + c·y + e,  b·x + d·y + f)
//     即 m[0],m[1] 是第一行（旋转/缩放不影响平移分离性，TRS 展开便宜）
//   * 角度一律弧度（文档 04 门面层另提供 rotationDeg 视图，内核不存角度制）
// 全部 inline，无状态；禁止在此引入第三方数学库（07 移植矩阵纪律）。
#pragma once

#include <cmath>
#include <cstdint>

namespace lemon::math {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTau = 6.28318530717958647692f;

template <typename T>
constexpr T Clamp(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }
constexpr float SaturateClamp(float v) { return Clamp(v, 0.0f, 1.0f); }

// ---------------------------------------------------------------- Vec2 ----
struct Vec2 {
    float x = 0.0f, y = 0.0f;

    constexpr Vec2() = default;
    constexpr Vec2(float x_, float y_) : x(x_), y(y_) {}

    constexpr Vec2 operator+(Vec2 r) const { return {x + r.x, y + r.y}; }
    constexpr Vec2 operator-(Vec2 r) const { return {x - r.x, y - r.y}; }
    constexpr Vec2 operator-() const { return {-x, -y}; }
    constexpr Vec2 operator*(float s) const { return {x * s, y * s}; }
    constexpr Vec2 operator/(float s) const { return {x / s, y / s}; }
    Vec2& operator+=(Vec2 r) { x += r.x; y += r.y; return *this; }
    Vec2& operator-=(Vec2 r) { x -= r.x; y -= r.y; return *this; }

    constexpr bool operator==(const Vec2& r) const { return x == r.x && y == r.y; }

    static constexpr Vec2 Zero() { return {0.0f, 0.0f}; }
    static constexpr Vec2 One() { return {1.0f, 1.0f}; }
};

constexpr Vec2 operator*(float s, Vec2 v) { return {s * v.x, s * v.y}; }
constexpr float Dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
constexpr Vec2 Hadamard(Vec2 a, Vec2 b) { return {a.x * b.x, a.y * b.y}; }
inline float Length(Vec2 v) { return std::sqrt(Dot(v, v)); }
inline float LengthSq(Vec2 v) { return Dot(v, v); }
inline Vec2 Normalize(Vec2 v) {
    float len = Length(v);
    return len > 1e-8f ? v / len : Vec2::Zero();
}
/// 最近点参数 t ∈ [0,1]（点到线段），供碰撞查询层复用
inline float ClosestTOnSegment(Vec2 p, Vec2 a, Vec2 b) {
    Vec2 ab = b - a;
    float denom = LengthSq(ab);
    if (denom < 1e-12f) return 0.0f;
    return SaturateClamp(Dot(p - a, ab) / denom);
}

// --------------------------------------------------------------- Mat3x2 ----
struct Mat3x2 {
    float m[6] = {1, 0, 0, 1, 0, 0}; // {a,b,c,d,e,f}

    constexpr Mat3x2() = default;
    constexpr static Mat3x2 FromRows(float a, float b, float c, float d, float e, float f) {
        Mat3x2 r;
        r.m[0] = a; r.m[1] = b; r.m[2] = c;
        r.m[3] = d; r.m[4] = e; r.m[5] = f;
        return r;
    }

    constexpr static Mat3x2 Identity() { return {}; }

    /// TRS：先缩放、再旋转、后平移（2D 仿射，列向量约定 p' = T·R·S·p）
    static Mat3x2 FromTRS(Vec2 pos, float rotRad, Vec2 scale) {
        float c = std::cos(rotRad), s = std::sin(rotRad);
        // R·S = [c·sx, -s·sy; s·sx, c·sy]
        return FromRows(c * scale.x, s * scale.x, -s * scale.y, c * scale.y, pos.x, pos.y);
    }

    /// 矩阵复合 this ∘ rhs：先施加 rhs 再施加 this
    constexpr Mat3x2 operator*(const Mat3x2& rhs) const {
        const auto& l = m;
        const auto& r = rhs.m;
        return FromRows(
            l[0] * r[0] + l[2] * r[1],
            l[1] * r[0] + l[3] * r[1],
            l[0] * r[2] + l[2] * r[3],
            l[1] * r[2] + l[3] * r[3],
            l[0] * r[4] + l[2] * r[5] + l[4],
            l[1] * r[4] + l[3] * r[5] + l[5]);
    }

    constexpr Vec2 Apply(Vec2 p) const {
        return {m[0] * p.x + m[2] * p.y + m[4], m[1] * p.x + m[3] * p.y + m[5]};
    }

    /// 世界→NDC 正交投影。约定：世界 Y 向下（屏幕语义）→ Vulkan NDC Y 向下
    ///（世界下方 = NDC +1 = 屏幕下方）。修订（2026-09-19，anim-smoke 截图实锤）：
    /// 原实现按 GL 语义做 Y 翻转（世界下方→NDC -1=屏幕顶），在 Vulkan/MoltenVK 上
    /// 整体镜像（文字倒印/布局上下颠倒）；对称内容的 bench 从未暴露。
    static Mat3x2 Ortho(Vec2 center, float halfW, float halfH) {
        float sx = 1.0f / halfW, sy = 1.0f / halfH;
        return FromRows(sx, 0.0f, 0.0f, sy, -center.x * sx, -center.y * sy);
    }
};

// ---------------------------------------------------------------- Rect ----
struct Rect {
    Vec2 min, max;

    constexpr Rect() = default;
    constexpr Rect(Vec2 min_, Vec2 max_) : min(min_), max(max_) {}
    constexpr static Rect FromCenterHalf(Vec2 c, float hw, float hh) {
        return {{c.x - hw, c.y - hh}, {c.x + hw, c.y + hh}};
    }
    constexpr static Rect FromMinSize(Vec2 min_, Vec2 size) {
        return {min_, {min_.x + size.x, min_.y + size.y}};
    }

    constexpr Vec2 Size() const { return max - min; }
    constexpr Vec2 Center() const { return {(min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f}; }
    constexpr bool Contains(Vec2 p) const {
        return p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y;
    }
    constexpr bool Overlaps(const Rect& r) const {
        return min.x <= r.max.x && max.x >= r.min.x && min.y <= r.max.y && max.y >= r.min.y;
    }
    constexpr Rect Expanded(float m) const { return {{min.x - m, min.y - m}, {max.x + m, max.y + m}}; }
    constexpr Rect ClampedTo(const Rect& r) const {
        return {{Clamp(min.x, r.min.x, r.max.x), Clamp(min.y, r.min.y, r.max.y)},
                {Clamp(max.x, r.min.x, r.max.x), Clamp(max.y, r.min.y, r.max.y)}};
    }
};

// --------------------------------------------------------------- Color ----
// rgba8 打包：r | g<<8 | b<<16 | a<<24（小端即内存序 R,G,B,A，
// 与 GPU unpackUnorm4x8 → (r,g,b,a)/255 一致；spike-02 已验证）
constexpr uint32_t PackRGBA(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16) | ((uint32_t)a << 24);
}
struct Color {
    float r = 1, g = 1, b = 1, a = 1;
    constexpr uint32_t ToRGBA8() const {
        auto q = [](float v) { return (uint8_t)(SaturateClamp(v) * 255.0f + 0.5f); };
        return PackRGBA(q(r), q(g), q(b), q(a));
    }
    constexpr static Color FromRGBA8(uint32_t c) {
        return {(float)(c & 0xFF) / 255.0f, (float)((c >> 8) & 0xFF) / 255.0f,
                (float)((c >> 16) & 0xFF) / 255.0f, (float)((c >> 24) & 0xFF) / 255.0f};
    }
    constexpr Color WithAlpha(float a_) const { return {r, g, b, a_}; }
};

// ---------------------------------------------------------------- utils ---
constexpr float Lerp(float a, float b, float t) { return a + (b - a) * t; }
constexpr Vec2 Lerp(Vec2 a, Vec2 b, float t) { return a + (b - a) * t; }

/// 帧率无关指数阻尼系数：t = 1 - exp(-rate·dt)（Camera2D 平滑跟随 / Prowl2D smooth-damp 同族）。
/// inline 非 constexpr（批⑦ CI 实锤）：std::exp 的 constexpr 是 libc++ 扩展（C++23
/// 未进标准面），MSVC/libstdc++ 均不标——常量求值无消费方，按 Length() 同款口径
inline float Damp(float rate, float dt) { return 1.0f - std::exp(-rate * dt); }

/// 吸附到网格（像素完美相机用；pixel = 网格尺寸，如 1/zoom）。同上：std::round
/// 非 constexpr 可移植面 → inline（Vec2 重载经 float 版间接调用，一并降级）
inline float SnapTo(float v, float grid) { return grid * std::round(v / grid); }
inline Vec2 SnapTo(Vec2 v, float grid) { return {SnapTo(v.x, grid), SnapTo(v.y, grid)}; }

// ------------------------------------------------------- 三角函数查找表 --
// 4096 项 LUT + 线性插值：热路径（每实例仿射 15 万次/帧级）替代 libm sin/cos，
// 误差 < 1e-3（像素风视觉无感）；需要精确值时用 std::sin（相机/物理等低频路径）
namespace detail {
struct SinTable {
    float v[4097]; // 多一项便于插值
    SinTable() {
        for (int i = 0; i < 4097; ++i) v[i] = std::sin(i * kTau / 4096.0f);
    }
};
inline const SinTable& GetSinTable() {
    static const SinTable t;
    return t;
}
} // namespace detail

inline float FastSin(float rad) {
    const float* t = detail::GetSinTable().v;
    float x = rad * (4096.0f / kTau);
    int i = (int)x;
    float f = x - (float)i;
    i &= 4095; // 2 的幂掩码对负角补码回绕正确
    return t[i] + (t[i + 1] - t[i]) * f;
}
inline float FastCos(float rad) { return FastSin(rad + 1.5707963267948966f); }
/// 同角 sin/cos 一次查表（共享索引/插值系数，比两次 FastSin 省 ~40%）
inline void FastSinCos(float rad, float& s, float& c) {
    const float* t = detail::GetSinTable().v;
    float x = rad * (4096.0f / kTau);
    int i = (int)x;
    float f = x - (float)i;
    i &= 4095;
    int ci = (i + 1024) & 4095; // cos 相位 +π/2（1024/4096 圈）
    s = t[i] + (t[i + 1] - t[i]) * f;
    c = t[ci] + (t[ci + 1] - t[ci]) * f;
}

} // namespace lemon::math

namespace lemon {
using math::Color;
using math::Mat3x2;
using math::Rect;
using math::Vec2;
}
