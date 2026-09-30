#include "Core/FileOps.h"

#include <filesystem>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace lemon {

bool RenameReplace(const std::string& from, const std::string& to) {
#if defined(_WIN32)
    // UTF-8 → UTF-16（CP_UTF8）；宽字符绕开 ANSI codepage 的中文路径问题
    auto widen = [](const char* s) -> std::wstring {
        const int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
        std::wstring w((size_t)(n > 0 ? n : 1), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s, -1, w.data(), n);
        return w;
    };
    const std::wstring wFrom = widen(from.c_str());
    const std::wstring wTo = widen(to.c_str());
    if (!MoveFileExW(wFrom.c_str(), wTo.c_str(), MOVEFILE_REPLACE_EXISTING)) return false;
    return true;
#else
    std::error_code ec;
    std::filesystem::rename(from, to, ec);
    return !ec;
#endif
}

} // namespace lemon
