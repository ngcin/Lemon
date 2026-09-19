// Lemon 引擎 — 64 位 GUID 生成（M4.1 内核 #5 配套；编辑器 Meta.guid / M4.4 资产 GUID 共用）
// 生成策略：random_device 播种 splitmix64 + 进程内单调计数混合。同进程内保证不撞
// （计数位）；跨进程/跨机器碰撞概率 2^-63 量级（资产库规模远够）。不为持久化排序
// 提供任何语义（时间有序性明确不做——防泄漏创建时间序）。
#pragma once

#include <atomic>
#include <cstdint>
#include <random>

namespace lemon {

inline uint64_t GenerateGuid() {
    static std::atomic<uint64_t> counter{0};
    static const uint64_t seed = [] {
        std::random_device rd;
        uint64_t s = ((uint64_t)rd() << 32) ^ rd();
        s += 0x9E3779B97F4A7C15ull; // splitmix64 混一步（random_device 低位质量不稳）
        s ^= s >> 30; s *= 0xBF58476D1CE4E5B9ull;
        s ^= s >> 27; s *= 0x94D049BB133111EBull;
        s ^= s >> 31;
        return s;
    }();
    uint64_t c = counter.fetch_add(1, std::memory_order_relaxed) + 1;
    uint64_t z = seed ^ (c * 0x9E3779B97F4A7C15ull);
    z ^= z >> 30; z *= 0xBF58476D1CE4E5B9ull;
    z ^= z >> 27; z *= 0x94D049BB133111EBull;
    z ^= z >> 31;
    return z;
}

} // namespace lemon
