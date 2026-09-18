// Lemon 引擎 — 通用对象池（03 文档 §10：引擎内建纪律，帧末统一归还）
// 语义：
//   * Acquire：优先复用已释放槽位（placement new 重新构造），否则新分配；
//   * Release：立即归还；DeferredRelease：入延迟队列，FlushReleases()（DestroyCommit
//     阶段调用）才真正归还——系统遍历中释放的对象在当帧保持可用；
//   * 槽位永不缩容（容量单调增长），稳态零分配；统计供 F3 面板。
// 注意：ECS 实体本身由 EnTT registry 回收（天然池化）；本模板用于非 ECS 的
// 重负载对象（粒子层/飘字/事件扩展载荷等，01 文档 §6）。
#pragma once

#include <cstdint>
#include <vector>

#include "Core/Log.h"

namespace lemon {

template <typename T>
class Pool {
public:
    /// 返回槽位下标（0..Capacity-1，稳定，可作句柄存储）
    template <typename... Args>
    uint32_t Acquire(Args&&... args) {
        uint32_t idx;
        if (!free_.empty()) {
            idx = free_.back();
            free_.pop_back();
            ++reuseHits_;
        } else {
            idx = (uint32_t)items_.size();
            items_.emplace_back();
        }
        T* slot = &items_[idx];
        slot->~T();
        new (slot) T(std::forward<Args>(args)...); // 复用槽位重新构造
        ++liveCount_;
        return idx;
    }

    void Release(uint32_t idx) {
        LEMON_ASSERT(idx < items_.size(), "pool: release out of range");
        free_.push_back(idx);
        --liveCount_;
    }

    void DeferredRelease(uint32_t idx) { deferred_.push_back(idx); }

    /// DestroyCommit 阶段统一执行（顺序与 DeferredRelease 调用序一致）
    void FlushReleases() {
        for (uint32_t idx : deferred_) Release(idx);
        deferred_.clear();
    }

    T& operator[](uint32_t idx) { return items_[idx]; }
    const T& operator[](uint32_t idx) const { return items_[idx]; }

    uint32_t LiveCount() const { return (uint32_t)liveCount_; }
    uint32_t Capacity() const { return (uint32_t)items_.size(); }
    uint32_t ReuseHits() const { return reuseHits_; }
    uint32_t DeferredCount() const { return (uint32_t)deferred_.size(); }

private:
    std::vector<T> items_;
    std::vector<uint32_t> free_;
    std::vector<uint32_t> deferred_;
    uint32_t liveCount_ = 0;
    uint32_t reuseHits_ = 0;
};

} // namespace lemon
