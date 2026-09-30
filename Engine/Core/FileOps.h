// Lemon 引擎 — 文件系统小件：跨平台原子替换改名
// 背景（07 §3.6 Windows 阻断项⑤）：std::filesystem::rename 的"覆盖既有目标"语义
// 标准留为实现定义——MSVC STL 实测按 MoveFileExW(REPLACE_EXISTING) 替换，但把
// 覆盖语义钉在本助手而非各 STL 实现的善意上。WriteFileAtomic（manifest / 场景 /
// 管线缓存）统一走这里。
#pragma once

#include <string>

namespace lemon {

/// 原子替换式改名。POSIX = rename(2)（同文件系统覆盖目标为原子替换）；
/// Windows = MoveFileExW(MOVEFILE_REPLACE_EXISTING)（同卷原子；不带 COPY_ALLOWED，
/// 跨卷失败优于非原子拷贝——调用方 tmp 与目标同目录，天然同卷）。
/// 路径按 UTF-8 解释（Windows 侧内部转 UTF-16）。返回 false = 失败（不打日志，
/// 由调用方决定语义）。
bool RenameReplace(const std::string& from, const std::string& to);

} // namespace lemon
