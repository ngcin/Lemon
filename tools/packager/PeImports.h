// Lemon 出包 — PE import 表依赖清单（M7a 批⑦ B1；决策 D-7b）
// 为什么内置解析而不是 dumpbin：dumpbin 要 vcvars 环境才在 PATH——用户裸 shell
// 出包会哑火（批⑤ "依赖本机工具" 教训的 win 版）；本解析器只读文件字节、平台
// 无关（无条件编译段——mac 也编译运行，selftest 双平台跑）。
// 范围：import directory（DataDirectory[1]）的 descriptor 名表——闭包收件够用；
// delay/bound import 不参与（vulkan-1.dll 等常规件不用），扩需求时再加。
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace lemon::pkg {

/// 解析 PE 文件的直接 DLL 依赖名列表（原样大小写，调用方比对时折叠）。
/// 成功（含"无 import 项"的合法形态）= err 为空；坏档 = 返回空 + err 非空。
std::vector<std::string> PeImportDlls(const std::filesystem::path& peFile, std::string& err);

} // namespace lemon::pkg
