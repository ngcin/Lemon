// Lemon 出包 — PE import 表解析实现（M7a 批⑦ B1）。格式参照 Microsoft PE
// Specification：DOS 头 e_lfanew → PE 签名 → COFF(20B) → OptionalHeader
// （PE32+ magic 0x20b / PE32 0x10b，数据目录偏移随位宽分叉）→ 节表 RVA→文件
// 偏移换算 → import descriptor 数组（20B/项，Name RVA → ASCII DLL 名）。
// 防御边界：全部读前置边界检查；节数 ≤ 96 / descriptor 链 ≤ 512 / 名长 ≤ 256 /
// 文件 ≤ 512MiB——越界即 err（真实 exe 远不及这些量级，超限 = 坏档而非裁剪）。
#include "PeImports.h"

#include <cstdint>
#include <cstdio>
#include <fstream>

namespace lemon::pkg {
namespace {

constexpr uint32_t kMaxPeBytes = 512u * 1024 * 1024;

bool ReadAll(const std::filesystem::path& p, std::vector<uint8_t>& out, std::string& err) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(p, ec)) {
        err = "不是常规文件：" + p.string();
        return false;
    }
    std::ifstream f(p, std::ios::binary);
    if (!f) {
        err = "打开失败：" + p.string();
        return false;
    }
    f.seekg(0, std::ios::end);
    const auto n = (uint64_t)f.tellg();
    if (n > kMaxPeBytes) {
        err = "文件超 sane 上限（" + std::to_string(n) + " 字节）";
        return false;
    }
    f.seekg(0, std::ios::beg);
    out.resize((size_t)n);
    if (n > 0) f.read((char*)out.data(), (std::streamsize)n);
    if (!f && !f.eof()) {
        err = "读取失败：" + p.string();
        return false;
    }
    return true;
}

uint16_t Rd16(const std::vector<uint8_t>& b, size_t off) {
    return (uint16_t)((uint16_t)b[off] | ((uint16_t)b[off + 1] << 8));
}
uint32_t Rd32(const std::vector<uint8_t>& b, size_t off) {
    return (uint32_t)b[off] | ((uint32_t)b[off + 1] << 8) | ((uint32_t)b[off + 2] << 16) |
           ((uint32_t)b[off + 3] << 24);
}

/// 越界安全读（返回 false = 坏档，err 已置）
bool RangeOk(const std::vector<uint8_t>& b, size_t off, size_t n, std::string& err,
             const char* what) {
    if (off > b.size() || n > b.size() - off) {
        err = std::string("结构越界（") + what + " @ " + std::to_string(off) + " + " +
              std::to_string(n) + " > " + std::to_string(b.size()) + "）";
        return false;
    }
    return true;
}

} // namespace

std::vector<std::string> PeImportDlls(const std::filesystem::path& peFile, std::string& err) {
    err.clear();
    std::vector<uint8_t> b;
    if (!ReadAll(peFile, b, err)) return {};

    // DOS 头：'MZ' + e_lfanew（0x3C）
    if (!RangeOk(b, 0, 64, err, "DOS header")) return {};
    if (b[0] != 'M' || b[1] != 'Z') {
        err = "非 PE（无 MZ）：" + peFile.string();
        return {};
    }
    const uint32_t peOff = Rd32(b, 0x3C);
    if (!RangeOk(b, peOff, 4 + 20, err, "PE/COFF header")) return {}; // 签名 + COFF 最小面
    if (b[peOff] != 'P' || b[peOff + 1] != 'E' || b[peOff + 2] != 0 || b[peOff + 3] != 0) {
        err = "非 PE（签名缺失）@" + std::to_string(peOff);
        return {};
    }
    const uint16_t numSections = Rd16(b, peOff + 6);
    if (numSections > 96) {
        err = "节数超 sane 上限：" + std::to_string(numSections);
        return {};
    }
    const size_t optOff = peOff + 24;
    const uint16_t sizeOpt = Rd16(b, peOff + 20);
    if (!RangeOk(b, optOff, sizeOpt, err, "OptionalHeader")) return {};
    const uint16_t magic = Rd16(b, optOff);
    const bool pe32Plus = magic == 0x20b;
    if (!pe32Plus && magic != 0x10b) {
        err = "未知 OptionalHeader magic：0x" + std::to_string(magic);
        return {};
    }
    // OptionalHeader 尺寸下限（review 实锤③）：后续要读 NumberOfRvaAndSizes
    // （PE32+ @opt+108 / PE32 @opt+92）与数据目录条目 1（至 opt+128 / opt+112）；
    // 上面的 RangeOk 只保证 [optOff, optOff+sizeOpt) 在文件内——sizeOpt 声称过小
    // 时这些读会越过已验证窗口（合成坏档可触发 OOB）。真实 PE 的 SizeOfOptionalHeader
    // 恒 ≥ 224/240，取不到下限 = 坏档。
    const uint16_t kMinOpt = pe32Plus ? 128 : 112;
    if (sizeOpt < kMinOpt) {
        err = "SizeOfOptionalHeader 过小（" + std::to_string(sizeOpt) + " < " +
              std::to_string(kMinOpt) + "）";
        return {};
    }
    // 数据目录：PE32+ 在 opt+112 / PE32 在 opt+96；条目 1 = import 表（RVA+Size）
    const size_t dataDirOff = optOff + (pe32Plus ? 112 : 96);
    const size_t numRva = Rd32(b, optOff + (pe32Plus ? 108 : 92));
    if (numRva < 2) return {}; // 无 import 项的合法形态
    const uint32_t importRva = Rd32(b, dataDirOff + 8); // 目录 0 起算，条目 1 偏 8（尺寸下限已覆盖）
    if (importRva == 0) return {}; // 无 import 项

    // 节表：VA/SizeOfRawData/PointerToRawData → RVA 换算表
    const size_t secOff = optOff + sizeOpt;
    if (!RangeOk(b, secOff, (size_t)numSections * 40, err, "section table")) return {};
    struct Sec {
        uint32_t va, sizeRaw, ptrRaw;
    };
    std::vector<Sec> secs;
    secs.reserve(numSections);
    for (uint16_t i = 0; i < numSections; ++i) {
        const size_t s = secOff + (size_t)i * 40;
        secs.push_back({Rd32(b, s + 12), Rd32(b, s + 16), Rd32(b, s + 20)});
    }
    auto RvaToOff = [&](uint32_t rva, size_t& off) {
        for (const Sec& s : secs)
            if (s.sizeRaw > 0 && rva >= s.va && rva - s.va < s.sizeRaw) {
                off = (size_t)s.ptrRaw + (rva - s.va);
                return true;
            }
        return false;
    };

    size_t impOff;
    if (!RvaToOff(importRva, impOff)) {
        err = "import RVA 不落任何节：0x" + std::to_string(importRva);
        return {};
    }
    std::vector<std::string> dlls;
    for (uint32_t i = 0; i < 512; ++i) { // descriptor 链 sane 上限（终项全零）
        const size_t d = impOff + (size_t)i * 20;
        if (!RangeOk(b, d, 20, err, "import descriptor")) return {};
        if (Rd32(b, d) == 0 && Rd32(b, d + 12) == 0) break; // 终项（Name RVA=0 判据充分）
        const uint32_t nameRva = Rd32(b, d + 12);
        size_t nameOff;
        if (!RvaToOff(nameRva, nameOff)) {
            err = "import 名 RVA 不落任何节：0x" + std::to_string(nameRva);
            return {};
        }
        if (!RangeOk(b, nameOff, 1, err, "import name")) return {};
        std::string name;
        for (size_t j = nameOff; j < b.size() && name.size() < 256 && b[j] != 0; ++j)
            name.push_back((char)b[j]);
        if (name.empty() || name.size() >= 256) {
            err = "import 名畸形（空/超长）@" + std::to_string(nameOff);
            return {};
        }
        dlls.push_back(name);
    }
    return dlls;
}

} // namespace lemon::pkg
