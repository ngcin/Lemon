// Lemon 引擎 — 工作窃取作业系统（M2 系统管线内部并行的执行地基）
// 结构移植自 Luma `Event/JobSystem`（MIT，版权见下），按 01 文档 §3.1 修正：
//   * Schedule 改值语义（模板 + std::packaged_task 移动入队），消除调用方
//     生命周期陷阱（Luma 原版 IJob* 裸指针要求调用方保活）；
//   * 去惰性单例：由 World 显式构造持有（依赖治理：不引入 DI/服务注册器）；
//   * 增加 ParallelFor 与单线程诊断档（threadCount=1 时任务就地执行，
//     用于确定性回放 "--threads 1" 隔离并行因素）。
// 纪律：任务内禁止再调 ParallelFor（本实现无嵌套作业图，嵌套等待会死锁）。
//
// Copyright (c) Luma authors — JobSystem 队列/窃取结构（MIT）。
// Lemon 侧改动如上；完整许可声明见 THIRD_PARTY.md。
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <future>
#include <mutex>
#include <thread>
#include <vector>

#include "Core/FunctionRef.h"

namespace lemon {

class JobSystem {
public:
    /// threadCount：0 = 自动（hw_concurrency-1，至少 1）；1 = 单线程诊断档（不建线程，
    /// Schedule 就地执行）；>1 = 工作窃取池
    explicit JobSystem(int threadCount = 0);
    ~JobSystem();

    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    using JobHandle = std::future<void>;

    /// 提交任务（值语义，f 的拷贝/移动由包装持有）。单线程档：就地执行后返回已完成的 future。
    template <typename F>
    JobHandle Schedule(F&& f) {
        if (singleThreaded_) {
            f();
            return {};
        }
        std::packaged_task<void()> task(std::forward<F>(f));
        JobHandle handle = task.get_future();
        Enqueue(std::move(task));
        return handle;
    }

    /// 等待单个任务完成
    static void Complete(JobHandle& handle) {
        if (handle.valid()) handle.wait();
    }

    /// 把 [0, count) 切成不小于 grain 的块并发执行 fn(begin, end)。
    /// 块间必须独立（无顺序依赖、无共享写）；调用线程在等待期间参与执行。
    /// count==0 或 grain==0 直接返回。
    void ParallelFor(uint32_t count, uint32_t grain,
                     FunctionRef<void(uint32_t, uint32_t)> fn);

    int ThreadCount() const { return threadCount_; }

private:
    void Enqueue(std::packaged_task<void()> task);
    void WorkerLoop(int threadIndex);
    std::packaged_task<void()> TryPopLocal();
    std::packaged_task<void()> TrySteal();

    bool singleThreaded_ = false;
    int threadCount_ = 0;
    std::vector<std::thread> threads_;
    std::vector<std::deque<std::packaged_task<void()>>> queues_;
    std::vector<std::mutex> queueMutexes_;
    std::mutex wakeMutex_;
    std::condition_variable wakeCv_;
    std::atomic<bool> stop_{false};
    std::atomic<int> queuedTasks_{0}; // 尚未被取走的任务数（等待谓词）
    std::atomic<unsigned> nextQueue_{0};
};

} // namespace lemon
