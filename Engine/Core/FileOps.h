// Lemon 引擎 — 文件系统小件：跨平台原子替换改名
// 背景（07 §3.6 Windows 阻断项⑤）：std::filesystem::rename 的"覆盖既有目标"语义
// 标准留为实现定义——MSVC STL 实测按 MoveFileExW(REPLACE_EXISTING) 替换，但把
// 覆盖语义钉在本助手而非各 STL 实现的善意上。WriteFileAtomic（manifest / 场景 /
// 管线缓存）统一走这里。
#pragma once

#include <cstddef>
#include <string>

namespace lemon {

/// 原子替换式改名。POSIX = rename(2)（同文件系统覆盖目标为原子替换）；
/// Windows = MoveFileExW(MOVEFILE_REPLACE_EXISTING)（同卷原子；不带 COPY_ALLOWED，
/// 跨卷失败优于非原子拷贝——调用方 tmp 与目标同目录，天然同卷）。
/// 路径按调用方编码经 win 侧 ACP 转换（见 RenameReplace 实现注记）。返回 false =
/// 失败（不打日志，由调用方决定语义）。
bool RenameReplace(const std::string& from, const std::string& to);

/// ACP 窄路径 → UTF-8（W6 2026-10-06 真机实抓新增）：win 侧 argv/fs 派生窄串
/// 按 ACP 走（与 fs::path(std::string) 的窄串解释同源），而 C++/C# 边界契约是
/// UTF-8——过边界前就地归一。POSIX 恒等（argv 本就是 UTF-8）。
std::string AcpToUtf8(const std::string& s);

/// UTF-8 → ACP 窄路径（逆函数，同 W6）：SDL drop/剪贴板等外来路径按 UTF-8 契约
/// 进来，win 侧 fs::path(窄串) 按 ACP 解——过 fs 前就地归一（不可逆字符会变 '?'，
/// 调用方限路径场景）。POSIX 恒等。
std::string Utf8ToAcp(const std::string& s);

/// 原子整文件落盘（共享工具，2026-09-24 审查 F-04/P-13；M7a 批③ 自编辑器
/// AssetDatabase 下沉——运行时 SaveStore 与编辑器写侧共用）：同目录 .tmp 全量
/// 写入 + flush 显式校验 + rename 替换——磁盘满/进程中断只丢 .tmp，不把原文件
/// 截成半档。场景/Prefab/存档/manifest 四条保存链统一走此口。
/// durable（M7a 批① M21）= rename 前对 .tmp fsync：掉电后名字交换至多回到旧档，
/// 不会出现长度 0 的新档；高频写（.meta/场景）不必开，manifest/存档等"重建代价
/// 高"的落盘点开。
bool WriteFileAtomic(const std::string& path, const void* data, size_t n,
                     bool durable = false);
inline bool WriteFileAtomic(const std::string& path, const std::string& s,
                            bool durable = false) {
    return WriteFileAtomic(path, s.data(), s.size(), durable);
}

} // namespace lemon
