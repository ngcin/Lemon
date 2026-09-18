// Lemon 引擎 — 基础设施：日志与断言
// 纪律：Engine 内禁用异常；错误走日志 + abort（渲染层 Vulkan 错误同样致命退出，
// 与 M0 spike 的 VK_CHECK 行为一致，便于 CI 捕获）。
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace lemon {

enum class LogLevel : uint8_t { Info, Warn, Error };

void LogMsg(LogLevel level, const char* fmt, ...) __attribute__((format(printf, 2, 3)));

inline constexpr const char* ToString(LogLevel l) {
    switch (l) {
        case LogLevel::Info: return "info";
        case LogLevel::Warn: return "warn";
        default: return "error";
    }
}

} // namespace lemon

#define LEMON_LOG(...) ::lemon::LogMsg(::lemon::LogLevel::Info, __VA_ARGS__)
#define LEMON_WARN(...) ::lemon::LogMsg(::lemon::LogLevel::Warn, __VA_ARGS__)

// 不可达/引擎内部不变量破坏：立即终止（不 recover，错误应在开发期暴露）
#define LEMON_ASSERT(cond, ...)                                              \
    do {                                                                     \
        if (!(cond)) {                                                       \
            ::lemon::LogMsg(::lemon::LogLevel::Error, "ASSERT %s:%d  %s",    \
                            __FILE__, __LINE__, #cond);                      \
            ::lemon::LogMsg(::lemon::LogLevel::Error, __VA_ARGS__);          \
            ::std::abort();                                                  \
        }                                                                    \
    } while (0)

// 外部输入校验（资产/参数非法）：与 ASSERT 同为致命，语义上区分来源
#define LEMON_CHECK(cond, ...) LEMON_ASSERT(cond, __VA_ARGS__)
