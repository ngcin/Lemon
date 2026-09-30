// Lemon 引擎 — 当前进程 id（跨平台；Windows 阻断项①，07 §3.6）
// 用途：冒烟/模板生成器的 tempdir 唯一后缀。SDL 3.2.14 尚无 SDL_GetPID
// （3.4 起），故自带一份；SDL 升 3.4+ 后可退役换官方。
#pragma once

#include <cstdint>

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace lemon {

inline uint64_t CurrentProcessId() {
#if defined(_WIN32)
    return (uint64_t)GetCurrentProcessId();
#else
    return (uint64_t)::getpid();
#endif
}

} // namespace lemon
