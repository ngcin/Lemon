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
/// 路径按 UTF-8 解释（Windows 侧内部转 UTF-16）。返回 false = 失败（不打日志，
/// 由调用方决定语义）。
bool RenameReplace(const std::string& from, const std::string& to);

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
