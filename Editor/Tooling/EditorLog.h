// Lemon 编辑器 — 编辑器日志环（M4.md §2.2 Console 面板数据源；内核 #14 消费端）
// 引擎 LogSink → 定长环 + 分级过滤。线程安全：sink 在引擎日志锁内被调，本环再持
// 自锁拷贝；编辑器 UI 单线程消费（Swap 前台缓冲，帧内零锁渲染）。
#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "Core/Log.h"

namespace lemon::editor {

struct EditorLogLine {
    LogLevel level;
    std::string text;
    uint64_t seq; // 全局序号（去重用）
};

class EditorLogRing {
public:
    EditorLogRing() { lines_.resize(kCapacity); }

    // ---- 引擎 sink（LogMsg 锁内回调；只做拷贝入环，须快）----
    static void SinkThunk(LogLevel level, const char* msg, void* userData) {
        ((EditorLogRing*)userData)->Push(level, msg);
    }
    void Push(LogLevel level, const char* msg) {
        std::lock_guard<std::mutex> lock(mutex_);
        // 环形下标（review 2026-10-02 #97）：原 vector + erase(begin()) 饱和后每条
        // 搬移 ~2047 行（~80KB memmove），且发生在引擎日志锁内的 sink 路径——定长
        // 环零搬移，满环顶掉最旧（槽内旧 std::string 由赋值析构）
        EditorLogLine& slot = lines_[(head_ + count_) % kCapacity];
        if (count_ < kCapacity) ++count_;
        else head_ = (head_ + 1) % kCapacity;
        slot.level = level;
        slot.text = msg;
        slot.seq = ++seq_;
    }

    /// UI 帧开头快照（全量拷贝；编辑器日志量小，M4.1 若日志洪泛再改增量）
    std::vector<EditorLogLine> Snapshot() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<EditorLogLine> out;
        out.reserve(count_);
        for (size_t i = 0; i < count_; ++i)
            out.push_back(lines_[(head_ + i) % kCapacity]); // 旧 → 新
        return out;
    }
    void Clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        head_ = count_ = 0;
    }

private:
    static constexpr size_t kCapacity = 2048;
    std::mutex mutex_;
    std::vector<EditorLogLine> lines_;
    size_t head_ = 0, count_ = 0;
    uint64_t seq_ = 0;
};

} // namespace lemon::editor
