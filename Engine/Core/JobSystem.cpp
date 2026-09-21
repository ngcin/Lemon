// Lemon 引擎 — 工作窃取作业系统实现（结构移植自 Luma Event/JobSystem.cpp，MIT；
// 修正与差异见 JobSystem.h 文件头）。窃取策略：本地队列 LIFO（缓存局部性），
// 偷取者从队头取（FIFO，降低与宿主的竞争端）。
#include "Core/JobSystem.h"

#include <algorithm>

namespace lemon {

namespace {
// 工作线程自身的队列索引（-1 = 非工作线程）
thread_local int t_threadIndex = -1;

// 线程指纹（窃取探测起点用）：std::thread::id 的哈希，廉价打散
unsigned threadIndexHash() {
    return (unsigned)std::hash<std::thread::id>{}(std::this_thread::get_id());
}
} // namespace

JobSystem::JobSystem(int threadCount) {
    if (threadCount <= 0)
        threadCount = std::max(1u, std::thread::hardware_concurrency() - 1);
    threadCount_ = threadCount;
    if (threadCount <= 1) {
        singleThreaded_ = true;
        return;
    }

    // vector<mutex> 不可移动插入（resize 不行），构造临时再移动赋值（指针转移）
    queues_ = std::vector<std::deque<std::packaged_task<void()>>>(threadCount);
    queueMutexes_ = std::vector<std::mutex>(threadCount);
    for (int i = 0; i < threadCount; ++i)
        threads_.emplace_back(&JobSystem::WorkerLoop, this, i);
}

JobSystem::~JobSystem() {
    { // stop_ 写入须与 WorkerLoop 谓词检查互斥：不持锁的 notify 会落进
      // 「谓词已查 false → 入队等待」窗口被蒸发 → 工作线程永眠、join 挂死
      // （2026-09-21 ctest 实抓：lemon-tests 偶发不退出，采样停在 cond_wait）
      std::lock_guard<std::mutex> lock(wakeMutex_);
      stop_.store(true, std::memory_order_release);
    }
    wakeCv_.notify_all();
    for (std::thread& t : threads_)
        if (t.joinable()) t.join();
}

void JobSystem::Enqueue(std::packaged_task<void()> task) {
    // 工作线程内提交 → 入本线程队列（子任务贴近消费者）；否则 round-robin
    unsigned q = t_threadIndex >= 0
                     ? (unsigned)t_threadIndex
                     : nextQueue_.fetch_add(1, std::memory_order_relaxed) % (unsigned)threadCount_;
    {
        std::lock_guard<std::mutex> lock(queueMutexes_[q]);
        queues_[q].push_back(std::move(task));
    }
    { // 同 ~JobSystem：计数更新入临界区，notify 不丢（谓词见 JobSystem.h）
        std::lock_guard<std::mutex> lock(wakeMutex_);
        queuedTasks_.fetch_add(1, std::memory_order_release);
    }
    wakeCv_.notify_one(); // 只唤醒一个，避免惊群
}

void JobSystem::WorkerLoop(int threadIndex) {
    t_threadIndex = threadIndex;
    while (!stop_.load(std::memory_order_acquire)) {
        std::packaged_task<void()> task = TryPopLocal();
        if (!task.valid()) task = TrySteal();
        if (task.valid()) {
            task();
        } else {
            // 谓词用"未取走任务数"而非"活跃任务数"：他人执行中而队列空时应当休眠
            // （Luma 旧版此处忙等烧 CPU，其注释留下的教训一并继承）
            std::unique_lock<std::mutex> lock(wakeMutex_);
            wakeCv_.wait(lock, [this] {
                return stop_.load(std::memory_order_acquire) ||
                       queuedTasks_.load(std::memory_order_acquire) > 0;
            });
        }
    }
}

std::packaged_task<void()> JobSystem::TryPopLocal() {
    std::unique_lock<std::mutex> lock(queueMutexes_[t_threadIndex], std::try_to_lock);
    if (lock.owns_lock() && !queues_[t_threadIndex].empty()) {
        auto task = std::move(queues_[t_threadIndex].back());
        queues_[t_threadIndex].pop_back();
        queuedTasks_.fetch_sub(1, std::memory_order_acq_rel);
        return task;
    }
    return {};
}

std::packaged_task<void()> JobSystem::TrySteal() {
    // 从错开起点环形探测受害者，避免所有空闲线程都扑向同一队列
    // （Weyl 序列：线程本地自增 × 大奇数，分布足够散且免随机库依赖）
    const unsigned n = (unsigned)threadCount_;
    thread_local unsigned t_probe = (unsigned)threadIndexHash();
    t_probe += 0x9E3779B9u;
    unsigned start = t_probe % n;
    for (unsigned i = 0; i < n; ++i) {
        unsigned victim = (start + i) % n;
        if ((int)victim == t_threadIndex) continue;
        std::unique_lock<std::mutex> lock(queueMutexes_[victim], std::try_to_lock);
        if (lock.owns_lock() && !queues_[victim].empty()) {
            auto task = std::move(queues_[victim].front());
            queues_[victim].pop_front();
            queuedTasks_.fetch_sub(1, std::memory_order_acq_rel);
            return task;
        }
    }
    return {};
}

void JobSystem::ParallelFor(uint32_t count, uint32_t grain,
                            FunctionRef<void(uint32_t, uint32_t)> fn) {
    if (count == 0 || grain == 0) return;
    if (grain > count) grain = count;

    uint32_t blockCount = (count + grain - 1) / grain;
    if (blockCount <= 1 || singleThreaded_) {
        fn(0, count); // 单块或诊断档：直接同步执行（确定性回放的单线程基线）
        return;
    }

    // 切块入队；留下最后一块给调用线程自己干（减少一次派发与唤醒）
    std::vector<JobHandle> handles;
    handles.reserve(blockCount - 1);
    for (uint32_t b = 0; b < blockCount - 1; ++b) {
        uint32_t begin = b * grain;
        uint32_t end = (b == blockCount - 1) ? count : std::min(begin + grain, count);
        handles.emplace_back(Schedule([&fn, begin, end] { fn(begin, end); }));
    }
    uint32_t lastBegin = (blockCount - 1) * grain;
    fn(lastBegin, count);
    for (JobHandle& h : handles) Complete(h);
}

} // namespace lemon
