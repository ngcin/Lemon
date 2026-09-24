// Lemon 编辑器 — 文件监视器（M4.md §3.1 Assets/；06 §2.2 热重载入口）
// 线程轮询快照比对（Luma FileWatcher 同款语义，轮询间隔 500ms）：任何增删改
// 置脏标志，主线程 ConsumeDirty 取走并触发 AssetDatabase::Rescan。逐事件细分
// 不做——Rescan 的 ChangeSet 已给出精确的 added/modified/removed。
// 归属：Editor/Assets（编辑器工具；引擎运行时资产热替换经同款回调接 M6）。
#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <string>
#include <thread>

namespace lemon::editor {

/// 防抖门（F-15，2026-09-24）：配 FileWatcher::ConsumeDirty 使用——窗口锚定在
/// 触发点，防抖窗内取到的脏事件转 pending（原实现直接清标志丢弃 = 窗口内的
/// 第二次保存不再触发编译），窗过后照常触发；连续变更合并为一次。
class DebounceGate {
public:
    explicit DebounceGate(double windowSec = 0.4) : window_(windowSec) {}
    /// ConsumeDirty() 为 true 时喂入（任意时刻可喂）。
    void OnDirty(double) { pending_ = true; }
    /// 每帧查询：pending 且窗口已过 → 消费并返回 true（调用方触发重编译）；
    /// 窗外首脏同帧即触发（与原"立即编译"节奏一致）。
    bool Due(double now) {
        if (!pending_ || now < until_) return false;
        pending_ = false;
        until_ = now + window_;
        return true;
    }
    bool Pending() const { return pending_; }

private:
    double window_;
    bool pending_ = false;
    double until_ = 0.0;
};

class FileWatcher {
public:
    FileWatcher() = default;
    ~FileWatcher() { Stop(); }

    void Start(std::string rootDir);
    void Stop();
    /// 主线程每帧轮询：自上次调用以来有变化 = true（一次性取走）
    bool ConsumeDirty() { return dirty_.exchange(false); }
    bool Running() const { return running_.load(); }

private:
    void PollLoop();
    std::thread thread_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> dirty_{false};
    std::atomic<bool> running_{false};
    bool primed_ = false; // 首拍基线已立（Start 前置 false；仅 PollLoop 线程读写）
    std::string root_;
    std::map<std::string, std::pair<uintmax_t, int64_t>> last_; // path → (size, mtime)
};

} // namespace lemon::editor
