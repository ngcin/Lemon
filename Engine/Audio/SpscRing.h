// Lemon 引擎 — SPSC 无锁字节环（M6c 批①b，ADR-015 M2）
// 恰一生产者（流式填充线程 / 离线泵）× 恰一消费者（设备回调 / MixOffline，任一
// 时刻只有其一在跑——设备模式游标归音频线程的既有契约已保证）。索引单调递增
// （免回卷比较 ABA），head 只由生产者写、tail 只由消费者写，release/acquire 成对
// 发布；容量向上取 2^n，掩码取段代替取模。字节语义——流式侧保证读写字节数对
// 帧整数倍（PumpFeed 按 free/帧字节 收敛 chunk）。
#pragma once

#include <atomic>
#include <cstring>
#include <vector>

namespace lemon::audio {

class SpscRing {
public:
    explicit SpscRing(size_t capacityBytes) {
        size_t cap = 1;
        while (cap < capacityBytes) cap <<= 1;
        buf_.resize(cap);
        mask_ = cap - 1;
    }

    /// 生产者：写入至多 bytes，返回实写（环满 = 0）
    size_t Write(const void* src, size_t bytes) {
        const size_t t = tail_.load(std::memory_order_acquire);
        const size_t h = head_.load(std::memory_order_relaxed); // 自家独写
        const size_t freeBytes = buf_.size() - (h - t);
        const size_t n = bytes < freeBytes ? bytes : freeBytes;
        if (n == 0) return 0;
        const size_t pos = h & mask_;
        const size_t first = buf_.size() - pos < n ? buf_.size() - pos : n;
        std::memcpy(buf_.data() + pos, src, first);
        if (n > first)
            std::memcpy(buf_.data(), static_cast<const uint8_t*>(src) + first, n - first);
        head_.store(h + n, std::memory_order_release);
        return n;
    }

    /// 消费者：读取至多 bytes，返回实读（环空 = 0）
    size_t Read(void* dst, size_t bytes) {
        const size_t h = head_.load(std::memory_order_acquire);
        const size_t t = tail_.load(std::memory_order_relaxed); // 自家独写
        const size_t avail = h - t;
        const size_t n = bytes < avail ? bytes : avail;
        if (n == 0) return 0;
        const size_t pos = t & mask_;
        const size_t first = buf_.size() - pos < n ? buf_.size() - pos : n;
        std::memcpy(dst, buf_.data() + pos, first);
        if (n > first)
            std::memcpy(static_cast<uint8_t*>(dst) + first, buf_.data(), n - first);
        tail_.store(t + n, std::memory_order_release);
        return n;
    }

    size_t Capacity() const { return buf_.size(); }
    /// 生产者视角可用空间（消费者不会使其变小；消费者侧请用 Size()）
    size_t Free() const {
        return buf_.size() - (head_.load(std::memory_order_relaxed) -
                              tail_.load(std::memory_order_acquire));
    }
    /// 消费者视角可读字节
    size_t Size() const {
        return head_.load(std::memory_order_acquire) -
               tail_.load(std::memory_order_relaxed);
    }

private:
    std::vector<uint8_t> buf_;
    size_t mask_ = 0;
    alignas(64) std::atomic<size_t> head_{0}; // 生产者游标（单调字节序号）
    alignas(64) std::atomic<size_t> tail_{0}; // 消费者游标
};

} // namespace lemon::audio
