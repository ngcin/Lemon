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
    // L27（review 2026-10-09）：本类无"未种子断言"——Next() 是 constexpr（CI win
    // 热修④的常量求值要求），恒生效断言会破坏常量求值路径。实况：默认构造后
    // 不 Seed 直接用 = 固定流（state=0/inc=1），run-to-run 仍确定，但与世界 seed
    // 无关且所有忘播种实例共享同一条流（跨系统相关随机）——请默认即
    // Rng(seed, stream) 构造或先 Seed。
    Rng() = default;

    /// seed：全局种子（World 级）；stream：子流 id（0=默认流，系统按注册序取号）
    constexpr Rng(uint64_t seed, uint64_t stream) { Seed(seed, stream); }

    constexpr void Seed(uint64_t seed, uint64_t stream) {
        inc_ = (stream << 1u) | 1u;   // PCG 要求增量常量为奇数
        state_ = 0u;
        Next();                        // 标准播种：先空转一步
        state_ += seed;
        Next();
    }

    /// 核心步进：LCG 状态推进 + XSH-RR 64→32 输出（PCG32 经典变体）。
    /// constexpr（CI win 热修④）：Seed 的空转调用要求 Next 可常量求值——MSVC 对
    /// constexpr 函数体主动诊断（C3615），clang 仅在实际常量求值时才查（mac 绿的
    /// 原因）；体为纯算术，本就够格
    constexpr uint32_t Next() {
        uint64_t old = state_;
        state_ = old * 6364136223846793005ull + inc_;
        // XSH-RR：高 32 位异或移位后按低位旋转
        uint32_t xorshifted = (uint32_t)(((old >> 18u) ^ old) >> 27u);
        uint32_t rot = (uint32_t)(old >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((-rot) & 31u));
    }

    /// [lo, hi] 闭区间均匀整数（无除法偏差：拒绝采样，被拒数与流状态相关但序列确定）。
    /// 契约 lo <= hi：单点/逆序直接返回 lo——span==1 时 zone 截 0、r>=zone 恒真，
    /// 老实现会永久死循环（2026-09-24 审查 F-10，与 Pcg32.cs 双端同语义修复）。
    /// span 为二次幂时整除 2^32、无拒绝区间：zone 计算同样截 0，直接取模（本就无偏）。
    uint32_t Range(uint32_t lo, uint32_t hi) {
        if (lo >= hi) return lo;
        uint32_t span = hi - lo + 1;
        if (span == 0) return lo; // 全区间（溢出回绕），退化处理
        if ((span & (span - 1u)) == 0) return lo + Next() % span;
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
