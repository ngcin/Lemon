// Lemon.SDK — 确定性随机（与 lemon::Rng PCG32 位级对齐；M3-1 golden 锁定，ADR-010 D3）
// 纪律：脚本侧禁 System.Random（全引擎唯一随机源 = PCG32 子流；金档回放依赖位级一致）。
// 浮点纪律（ADR-010 分层）：算术/sqrt/floor 位同放行；超越函数 .NET 自洽（不承诺与 libm 逐位一致）。
namespace Lemon;

/// <summary>PCG32（XSH-RR 64→32）：与引擎 C++ 侧同种子同子流产出完全相同的序列。</summary>
public sealed class Pcg32
{
    private ulong _state;
    private readonly ulong _inc;

    /// <summary>播种语义与 lemon::Rng::Seed 一致（先空转一步再加 seed）。</summary>
    public Pcg32(ulong seed, ulong stream)
    {
        _inc = (stream << 1) | 1u;   // PCG 增量常量必须为奇数
        _state = 0u;
        Next();
        _state += seed;
        Next();
    }

    public uint Next()
    {
        ulong old = _state;
        _state = old * 6364136223846793005ul + _inc;
        uint xorshifted = (uint)(((old >> 18) ^ old) >> 27);
        uint rot = (uint)(old >> 59);
        return (xorshifted >> (int)rot) | (xorshifted << (int)((0u - rot) & 31u));
    }

    /// <summary>[lo, hi] 闭区间整数（拒绝采样，与 C++ Range 同序列）。
    /// 契约 lo &lt;= hi：单点/逆序直接返回 lo——span==1 时 zone 截 0、r&gt;=zone 恒真，
    /// 老实现会永久死循环（2026-09-24 审查 F-10，与 lemon::Rng 双端同语义修复）。
    /// span 为二次幂时整除 2^32、无拒绝区间：zone 计算同样截 0，直接取模（本就无偏）。</summary>
    public uint Range(uint lo, uint hi)
    {
        if (lo >= hi) return lo;
        uint span = hi - lo + 1;
        if (span == 0) return lo;
        if ((span & (span - 1u)) == 0u) return lo + Next() % span;
        uint zone = (uint)((0x100000000ul / span) * span);
        uint r;
        do { r = Next(); } while (r >= zone);
        return lo + r % span;
    }

    /// <summary>[0,1) 均匀浮点：高 24 位缩放（位级确定）。</summary>
    public float Float01() => (float)(Next() >> 8) * (1.0f / 16777216.0f);

    /// <summary>[lo, hi) 浮点区间。</summary>
    public float Range(float lo, float hi) => lo + (hi - lo) * Float01();

    public bool Chance(float p) => p <= 0.0f ? false : Float01() < p;
}
