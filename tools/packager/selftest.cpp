// lemon-packager-selftest — PE import 解析器单测（M7a 批⑦ B2）
// 手工合成最小 PE（DOS→PE→COFF→PE32+ Optional→单节表→import descriptor）断言
// 解析路径；坏档三例断言防御边界。平台无关（mac/win 双跑——CI 面也吃）。
// 不引框架：断言失败 = abort（engine_tests LEMON_ASSERT 同款口径）。
#include "PeImports.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

int g_fail = 0;
void Check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++g_fail;
    }
}

void Put16(std::vector<uint8_t>& b, size_t off, uint16_t v) {
    b[off] = (uint8_t)(v & 0xFF);
    b[off + 1] = (uint8_t)(v >> 8);
}
void Put32(std::vector<uint8_t>& b, size_t off, uint32_t v) {
    for (int i = 0; i < 4; ++i) b[off + (size_t)i] = (uint8_t)(v >> (8 * i));
}

// 最小合法 PE32+：单节（VA 0x1000，raw @0x200 尺寸 0x400）+ import 目录
// （RVA 0x1000）+ 两个 descriptor（名 RVA 0x1100/0x1150）→ 两个依赖名。
std::vector<uint8_t> MakeSamplePe() {
    std::vector<uint8_t> b(0x600, 0);
    b[0] = 'M';
    b[1] = 'Z';
    Put32(b, 0x3C, 0x40); // e_lfanew
    const size_t pe = 0x40;
    b[pe] = 'P';
    b[pe + 1] = 'E';
    b[pe + 2] = 0;
    b[pe + 3] = 0;
    Put16(b, pe + 4, 0x8664);   // Machine = AMD64
    Put16(b, pe + 6, 1);        // NumberOfSections
    Put16(b, pe + 20, 240);     // SizeOfOptionalHeader（PE32+ 标准）
    Put16(b, pe + 22, 0x0022);  // Characteristics：EXECUTABLE_IMAGE | 32BITMACHINE
    const size_t opt = pe + 24;
    Put16(b, opt, 0x20b); // PE32+
    Put32(b, opt + 108, 16);   // NumberOfRvaAndSizes
    Put32(b, opt + 112 + 8, 0x1000); // DataDirectory[1].RVA = import 目录
    Put32(b, opt + 112 + 12, 40);    // DataDirectory[1].Size
    const size_t sec = opt + 240;
    Put32(b, sec + 12, 0x1000); // VirtualAddress
    Put32(b, sec + 16, 0x400);  // SizeOfRawData
    Put32(b, sec + 20, 0x200);  // PointerToRawData
    // import descriptors @0x200（=RVA 0x1000）：两真项 + 终项
    Put32(b, 0x200 + 12, 0x1100); // 名 RVA → "VULKAN-1.dll"
    Put32(b, 0x214 + 12, 0x1150); // 名 RVA → "USER32.dll"
    // 名串 @0x300 / @0x350（=RVA 0x1100/0x1150，均在节内）
    const char* n1 = "VULKAN-1.dll";
    const char* n2 = "USER32.dll";
    for (size_t i = 0; n1[i]; ++i) b[0x300 + i] = (uint8_t)n1[i];
    for (size_t i = 0; n2[i]; ++i) b[0x350 + i] = (uint8_t)n2[i];
    return b;
}

std::filesystem::path WriteTemp(const std::vector<uint8_t>& b, const char* name) {
    const auto p = std::filesystem::temp_directory_path() / name;
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f.write((const char*)b.data(), (std::streamsize)b.size());
    return p;
}

} // namespace

int main() {
    using lemon::pkg::PeImportDlls;
    std::vector<std::filesystem::path> temps;
    const auto Keep = [&](std::vector<uint8_t> b, const char* name) {
        const auto p = WriteTemp(b, name);
        temps.push_back(p);
        return p;
    };
    const std::filesystem::path good = Keep(MakeSamplePe(), "lemon-pe-selftest-good.exe");

    { // ① 合成 PE → 依赖两名
        std::string err;
        const auto dlls = PeImportDlls(good, err);
        Check(err.empty(), "sample: err empty");
        Check(dlls.size() == 2, "sample: 2 dlls");
        Check(dlls.size() == 2 && dlls[0] == "VULKAN-1.dll", "sample: dll[0] name");
        Check(dlls.size() == 2 && dlls[1] == "USER32.dll", "sample: dll[1] name");
    }
    { // ② 截断 → err 非空（e_lfanew 指向被截掉的区域）
        std::vector<uint8_t> trunc = MakeSamplePe();
        trunc.resize(100);
        std::string err;
        const auto dlls = PeImportDlls(Keep(std::move(trunc), "lemon-pe-selftest-trunc.exe"), err);
        Check(!err.empty(), "truncated: err set");
        Check(dlls.empty(), "truncated: no dlls");
    }
    { // ③ 坏 e_lfanew（越界）→ err 非空
        std::vector<uint8_t> bad = MakeSamplePe();
        Put32(bad, 0x3C, 0x7FFFFFF0);
        std::string err;
        PeImportDlls(Keep(std::move(bad), "lemon-pe-selftest-badlfa.exe"), err);
        Check(!err.empty(), "bad e_lfanew: err set");
    }
    { // ④ 无 import 目录（DataDirectory[1].RVA=0）→ 空表 + err 空
        std::vector<uint8_t> none = MakeSamplePe();
        Put32(none, 0x40 + 24 + 112 + 8, 0);
        std::string err;
        const auto dlls = PeImportDlls(Keep(std::move(none), "lemon-pe-selftest-noimp.exe"), err);
        Check(err.empty(), "no-import: err empty");
        Check(dlls.empty(), "no-import: empty list");
    }
    { // ⑤ 非 PE（无 MZ）→ err 非空
        std::vector<uint8_t> notpe = {0xDE, 0xAD, 0xBE, 0xEF, 0, 0, 0, 0};
        std::string err;
        PeImportDlls(Keep(std::move(notpe), "lemon-pe-selftest-notpe.exe"), err);
        Check(!err.empty(), "not-pe: err set");
    }
    { // ⑥ SizeOfOptionalHeader 声称过小（review 实锤③：越已验证窗口的 OOB 读）
        // 阴性钉法：节表随**声称值** 64 摆对位置（opt+64）、numRva/importRVA 的
        // 字节也在文件内——无 kMinOpt 检查时会"成功"解析出依赖（err 空），
        // 有检查则红字。首版夹具节表仍按 240 摆 → 撤检查也走"RVA 落节外"错误
        // 路径通过 = 没钉住修复（阴性验证实抓，见批⑦ review DevLog）。
        std::vector<uint8_t> tiny = MakeSamplePe();
        Put16(tiny, 0x40 + 20, 64);                    // 声称 OptionalHeader 64B
        std::copy(tiny.begin() + 0x118, tiny.begin() + 0x118 + 40,
                  tiny.begin() + 0x40 + 24 + 64);      // 节表搬到 opt+64
        std::string err;
        PeImportDlls(Keep(std::move(tiny), "lemon-pe-selftest-tinyopt.exe"), err);
        Check(!err.empty(), "tiny optional header: err set");
    }
    { // ⑦ import RVA 不落任何节（脏 RVA）→ err 非空
        std::vector<uint8_t> wild = MakeSamplePe();
        Put32(wild, 0x40 + 24 + 112 + 8, 0x9000); // 节 VA 0x1000/raw 0x400 之外
        std::string err;
        PeImportDlls(Keep(std::move(wild), "lemon-pe-selftest-wildrva.exe"), err);
        Check(!err.empty(), "wild import RVA: err set");
    }
    std::error_code ec;
    for (const auto& p : temps) std::filesystem::remove(p, ec);
    std::printf("RESULT pe-selftest: %s（fail=%d）\n", g_fail == 0 ? "OK" : "FAIL", g_fail);
    return g_fail == 0 ? 0 : 1;
}
