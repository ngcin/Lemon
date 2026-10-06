#include "Core/FileOps.h"

#include <filesystem>
#include <fstream>
#include <system_error>

#include "Core/Log.h"

// FsyncFile（M7a 批① M21 durable 写；M7a 批③ 随 WriteFileAtomic 自编辑器迁入）
#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace lemon {

namespace {
// 把已写文件内容钉进磁盘（rename 前调用——内容先于名字交换落盘，掉电后至多回到
// 旧名旧档）。失败返回 true 语义由调用方定；此处 false = 调用方红字但继续
//（durable 是加固不是正确性前提）。
bool FsyncFile(const std::string& path) {
#if defined(_WIN32)
    // _commit = FlushFileBuffers 要求写句柄——_O_RDONLY 必然 ACCESS_DENIED
    //（POSIX fsync(O_RDONLY) 合法，mac 侧测不出；W6 2026-10-06 真机实抓）
    const int fd = _open(path.c_str(), _O_RDWR | _O_BINARY);
    if (fd < 0) return false;
    const bool ok = _commit(fd) == 0;
    _close(fd);
    return ok;
#else
    const int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) return false;
    const bool ok = ::fsync(fd) == 0;
    ::close(fd);
    return ok;
#endif
}
} // namespace

bool WriteFileAtomic(const std::string& path, const void* data, size_t n,
                     bool durable) {
    const std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f.write((const char*)data, (std::streamsize)n);
        f.flush();
        if (!f.good()) { // 写失败在 flush 捕获（析构阶段的错误不再静默）
            f.close();
            std::error_code rm;
            std::filesystem::remove(tmp, rm);
            return false;
        }
    } // 析构 close
    if (durable && !FsyncFile(tmp))
        LEMON_WARN("fsync 失败（%s）——继续原子换名，掉电耐久性降级", tmp.c_str());
    // RenameReplace：覆盖语义钉在助手内（Windows 阻断项⑤，07 §3.6），不再依赖各
    // STL 对 fs::rename 覆盖目标的实现定义行为
    if (!RenameReplace(tmp, path)) {
        std::error_code rm;
        std::filesystem::remove(tmp, rm);
        return false;
    }
    return true;
}

bool RenameReplace(const std::string& from, const std::string& to) {
#if defined(_WIN32)
    // ACP 窄串 → UTF-16；宽字符绕开窄 API 的路径问题。用 CP_ACP 而非 CP_UTF8：
    // 入参路径全部 argv/fs 派生——win 侧窄串按 ACP 走（与 fs::path(std::string)
    // 同源；W6 2026-10-06 真机实抓：GBK 路径按 UTF-8 宽化 = manifest 写失败）
    auto widen = [](const char* s) -> std::wstring {
        const int n = MultiByteToWideChar(CP_ACP, 0, s, -1, nullptr, 0);
        std::wstring w((size_t)(n > 0 ? n : 1), L'\0');
        MultiByteToWideChar(CP_ACP, 0, s, -1, w.data(), n);
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

std::string AcpToUtf8(const std::string& s) {
#if defined(_WIN32)
    if (s.empty()) return s;
    const int w = MultiByteToWideChar(CP_ACP, 0, s.c_str(), -1, nullptr, 0);
    if (w <= 0) return s;
    std::wstring wide((size_t)w, L'\0');
    MultiByteToWideChar(CP_ACP, 0, s.c_str(), -1, wide.data(), w);
    const int n = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0,
                                      nullptr, nullptr);
    if (n <= 0) return s;
    std::string utf8((size_t)n - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, utf8.data(), n, nullptr, nullptr);
    return utf8;
#else
    return s;
#endif
}

std::string Utf8ToAcp(const std::string& s) {
#if defined(_WIN32)
    if (s.empty()) return s;
    const int w = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (w <= 0) return s;
    std::wstring wide((size_t)w, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, wide.data(), w);
    const int n = WideCharToMultiByte(CP_ACP, 0, wide.c_str(), -1, nullptr, 0, nullptr,
                                      nullptr);
    if (n <= 0) return s;
    std::string acp((size_t)n - 1, '\0');
    WideCharToMultiByte(CP_ACP, 0, wide.c_str(), -1, acp.data(), n, nullptr, nullptr);
    return acp;
#else
    return s;
#endif
}

} // namespace lemon
