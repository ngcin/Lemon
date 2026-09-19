// Lemon 编辑器 — 编辑器日志环（M4-Editor-Plan §2.2 Console 面板数据源；内核 #14 消费端）
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
    EditorLogRing() { lines_.reserve(kCapacity); }

    // ---- 引擎 sink（LogMsg 锁内回调；只做拷贝入环，须快）----
    static void SinkThunk(LogLevel level, const char* msg, void* userData) {
        ((EditorLogRing*)userData)->Push(level, msg);
    }
    void Push(LogLevel level, const char* msg) {
        std::lock_guard<std::mutex> lock(mutex_);
        lines_.push_back({level, msg, ++seq_});
        if (lines_.size() > kCapacity) lines_.erase(lines_.begin());
    }

    /// UI 帧开头快照（全量拷贝；编辑器日志量小，M4.1 若日志洪泛再改增量）
    std::vector<EditorLogLine> Snapshot() {
        std::lock_guard<std::mutex> lock(mutex_);
        return lines_;
    }
    void Clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        lines_.clear();
    }

private:
    static constexpr size_t kCapacity = 2048;
    std::mutex mutex_;
    std::vector<EditorLogLine> lines_;
    uint64_t seq_ = 0;
};

} // namespace lemon::editor
