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
    const int fd = _open(path.c_str(), _O_RDONLY | _O_BINARY);
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
