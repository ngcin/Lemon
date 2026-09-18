#include "Core/Log.h"

#include <cstdarg>

namespace lemon {

void LogMsg(LogLevel level, const char* fmt, ...) {
    std::fprintf(stderr, "[lemon][%s] ", ToString(level));
    va_list args;
    va_start(args, fmt);
    std::vfprintf(stderr, fmt, args);
    va_end(args);
    std::fprintf(stderr, "\n");
    if (level == LogLevel::Error) std::fflush(stderr);
}

} // namespace lemon
