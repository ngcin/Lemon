#include "Core/Log.h"

#include <cstdarg>
#include <cstring>
#include <mutex>

namespace lemon {

namespace {
std::mutex gLogMutex;                 // LogMsg 可能来自 JobSystem worker 线程
LogSink gSink = nullptr;
void* gSinkUserData = nullptr;
uint64_t gCounts[3] = {0, 0, 0};      // 下标 = (int)LogLevel
} // namespace

void SetLogSink(LogSink sink, void* userData) {
    std::lock_guard<std::mutex> lock(gLogMutex);
    gSink = sink;
    gSinkUserData = userData;
}

uint64_t LogCountOf(LogLevel level) {
    std::lock_guard<std::mutex> lock(gLogMutex);
    return gCounts[(int)level];
}

void LogMsg(LogLevel level, const char* fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    // fprintf 在锁内（评审 M26）：JobSystem worker 与主线程并发打印时行交错撕裂
    std::lock_guard<std::mutex> lock(gLogMutex);
    std::fprintf(stderr, "[lemon][%s] %s\n", ToString(level), buf);
    if (level == LogLevel::Error) std::fflush(stderr);
    ++gCounts[(int)level];
    if (gSink) gSink(level, buf, gSinkUserData);
}

} // namespace lemon
