// Lemon 编辑器 — 文件监视器实现（快照轮询；跳过点开头目录/文件）
#include "Assets/FileWatcher.h"

#include <chrono>
#include <filesystem>
#include <utility>

#include "Core/Log.h"

namespace lemon::editor {
namespace fs = std::filesystem;

void FileWatcher::Start(std::string rootDir) {
    if (running_.load()) return;
    root_ = std::move(rootDir);
    primed_ = false; // 重启（切项目）重立基线
    last_.clear();
    stop_ = false;
    running_ = true;
    thread_ = std::thread([this] { PollLoop(); });
}

void FileWatcher::Stop() {
    if (!running_.exchange(false)) return;
    stop_ = true;
    if (thread_.joinable()) thread_.join();
}

void FileWatcher::PollLoop() {
    while (!stop_.load()) {
        std::map<std::string, std::pair<uintmax_t, int64_t>> snap;
        std::error_code ec;
        for (auto it = fs::recursive_directory_iterator(
                 root_, fs::directory_options::skip_permission_denied, ec);
             it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (ec) break;
            const std::string name = it->path().filename().string();
            if (name.empty() || name[0] == '.') {
                if (it->is_directory(ec)) it.disable_recursion_pending();
                continue;
            }
            if (!it->is_regular_file(ec)) continue;
            auto lm = it->last_write_time(ec);
            if (ec) continue;
            snap[it->path().string()] = {it->file_size(ec), (int64_t)lm.time_since_epoch().count()};
        }

        // 首拍 = 基线不算变更（primed_ 语义；M4.6 修复：原先 "!snap.empty()" 条件写反，
        // 有文件的目录首拍必置脏 → 每次打开项目误触发一次热重载/重扫）
        if (!primed_) {
            primed_ = true;
            last_ = std::move(snap);
        } else if (snap != last_) {
            dirty_ = true;
            last_ = std::move(snap);
        }

        // 500ms 节拍（分段睡 = Stop 即时响应）
        for (int i = 0; i < 50 && !stop_.load(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    LEMON_LOG("file-watcher: 线程退出（%s）", root_.c_str());
}

} // namespace lemon::editor
