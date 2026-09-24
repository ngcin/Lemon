// Lemon 引擎 — 环形队列（03 文档 §11 事件系统的容器：系统只入队、帧末批量消费后清空）
// 容量策略：2 的幂起步、满时翻倍（稳定后不再分配）；设硬上限防失控
// （上限内永不丢事件——丢弃会静默破坏玩法语义；达到上限 push 失败并计数，
// 由 F3 面板/日志暴露）。
#pragma once

#include <cstdint>
#include <vector>

#include "Core/Log.h"

namespace lemon {

template <typename T>
class RingQueue {
public:
    static constexpr uint32_t kDefaultCapacity = 4096; // 2 的幂
    static constexpr uint32_t kMaxCapacity = 1u << 20; // 1M 条（EventPacket 32B → 32MB 上限）

    explicit RingQueue(uint32_t initialCapacity = kDefaultCapacity)
        : buf_(FloorPow2(initialCapacity)), mask_((uint32_t)buf_.size() - 1) {}

    /// 满则扩容（翻倍，保序搬运）；达硬上限返回 false 并计数（调用方告警）
    bool Push(const T& item) {
        if (size_ == buf_.size()) {
            if (buf_.size() >= kMaxCapacity) {
                ++dropped_;
                return false;
            }
            Grow();
        }
        buf_[(head_ + size_) & mask_] = item;
        ++size_;
        if (size_ > peak_) peak_ = size_;
        return true;
    }

    T& Front() {
        LEMON_ASSERT(size_ > 0, "ring queue: front on empty");
        return buf_[head_];
    }
    const T& Front() const {
        LEMON_ASSERT(size_ > 0, "ring queue: front on empty");
        return buf_[head_];
    }

    void Pop() {
        LEMON_ASSERT(size_ > 0, "ring queue: pop on empty");
        head_ = (head_ + 1) & mask_;
        --size_;
    }

    /// 下标访问（0 = Front），供帧末批量派发遍历（消费不清底层存储，清空用 Clear）
    T& At(uint32_t i) { return buf_[(head_ + i) & mask_]; }

    /// 取走全部现有元素（保序移入 out；队列清空、容量保留）。派发侧专用：
    /// 取出后 out 与队列底层彻底分离——派发回调内再 Push 落回队列（下帧派发），
    /// 即使触发 Grow 扩容也不影响 out 的引用（2026-09-24 审查 P5：原"At(i) 引用 +
    /// 末尾 Clear()"在回调再入队时吞事件、回调 Push 扩容时引用悬空）
    void TakeAll(std::vector<T>& out) {
        out.clear();
        out.reserve(size_);
        for (uint32_t i = 0; i < size_; ++i) out.push_back(std::move(buf_[(head_ + i) & mask_]));
        head_ = 0;
        size_ = 0;
    }

    /// 首连续段（桥侧两段零拷贝派发用）：[ptr, ptr+n)，n ≤ Size()；回绕余量走 TailSpan
    void HeadSpan(const T*& ptr, uint32_t& n) const {
        uint32_t first = (uint32_t)buf_.size() - head_;
        if (first > size_) first = size_;
        ptr = buf_.data() + head_;
        n = first;
    }
    /// 次连续段（HeadSpan 之后的回绕部分）；无回绕时 n = 0
    void TailSpan(const T*& ptr, uint32_t& n) const {
        uint32_t first = (uint32_t)buf_.size() - head_;
        n = size_ > first ? size_ - first : 0;
        ptr = buf_.data();
    }

    uint32_t Size() const { return size_; }
    bool Empty() const { return size_ == 0; }
    void Clear() { head_ = 0; size_ = 0; }

    uint32_t PeakSize() const { return peak_; }
    uint32_t Dropped() const { return dropped_; }
    void ResetPeak() { peak_ = size_; }

private:
    static uint32_t FloorPow2(uint32_t v) {
        uint32_t p = kDefaultCapacity;
        while (p < v && p < kMaxCapacity) p <<= 1;
        return p;
    }
    void Grow() {
        std::vector<T> next(buf_.size() * 2);
        for (uint32_t i = 0; i < size_; ++i) next[i] = buf_[(head_ + i) & mask_];
        buf_ = std::move(next);
        mask_ = (uint32_t)buf_.size() - 1;
        head_ = 0;
        ++growCount_;
    }

    std::vector<T> buf_;
    uint32_t head_ = 0;
    uint32_t size_ = 0;
    uint32_t mask_ = 0;
    uint32_t peak_ = 0;
    uint32_t dropped_ = 0;
    uint32_t growCount_ = 0;
};

} // namespace lemon
