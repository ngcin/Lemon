// Lemon 引擎 — 确定性随机流（PCG32，03 文档 §12 确定性回放的地基）
// 纪律：
//   * 全引擎唯一随机源：禁止 std::rand / mt19937 / std::uniform_*_distribution
//     （实现可跨标准库漂移，破坏逐帧回放）；Float01 等用位级确定转换。
//   * 每系统独立子流（stream id = 系统序号）：系统增删不影响其他系统的随机序列，
//     回放与录制只要 seed 与子流划分一致即逐位复现。
// 参考：Melissa O'Neill, PCG family（PCG32：64 位状态/流，32 位输出，统计质量充分）。
#pragma once

#include <cstdint>

#include "Core/Math.h"

namespace lemon {

class Rng {
public:
    Rng() = default; // 未种子状态，Seed() 前调用 Next() 会被断言拦截

    /// seed：全局种子（World 级）；stream：子流 id（0=默认流，系统按注册序取号）
    constexpr Rng(uint64_t seed, uint64_t stream) { Seed(seed, stream); }

    constexpr void Seed(uint64_t seed, uint64_t stream) {
        inc_ = (stream << 1u) | 1u;   // PCG 要求增量常量为奇数
        state_ = 0u;
        Next();                        // 标准播种：先空转一步
        state_ += seed;
        Next();
    }

    /// 核心步进：LCG 状态推进 + XSH-RR 64→32 输出（PCG32 经典变体）
    uint32_t Next() {
        uint64_t old = state_;
        state_ = old * 6364136223846793005ull + inc_;
        // XSH-RR：高 32 位异或移位后按低位旋转
        uint32_t xorshifted = (uint32_t)(((old >> 18u) ^ old) >> 27u);
        uint32_t rot = (uint32_t)(old >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((-rot) & 31u));
    }

    /// [lo, hi] 闭区间均匀整数（无除法偏差：拒绝采样，被拒数与流状态相关但序列确定）
    uint32_t Range(uint32_t lo, uint32_t hi) {
        uint32_t span = hi - lo + 1;
        if (span == 0) return lo; // 全区间（溢出回绕），退化处理
        uint32_t zone = (0x100000000ull / span) * span;
        uint32_t r;
        do { r = Next(); } while (r >= zone);
        return lo + r % span;
    }

    /// [0,1) 均匀浮点：取高 24 位缩放，位级确定（24 位尾数无损进入 float）
    float Float01() { return (float)(Next() >> 8) * (1.0f / 16777216.0f); }

    /// [lo, hi) 浮点区间
    float Range(float lo, float hi) { return lo + (hi - lo) * Float01(); }

    /// [0, 2π) 角度（弧度，与 Math.h 约定一致）
    float Angle() { return Float01() * math::kTau; }

    /// 概率事件：p 为真概率（p<=0 恒假，p>=1 恒真）
    bool Chance(float p) { return p <= 0.0f ? false : Float01() < p; }

    /// 单位圆均匀方向向量（极坐标法；FastSinCos 与 std 的差异在回放两侧同源，不破坏确定性）
    math::Vec2 UnitVec2() {
        float a = Angle();
        float s, c;
        math::FastSinCos(a, s, c);
        return {c, s};
    }

private:
    uint64_t state_ = 0;
    uint64_t inc_ = 1;
};

} // namespace lemon
