// Lemon 引擎 — LAT1 图集容器实现（M7a 批⑥；字节表见 AtlasBake.h 契约注记与
// ADR-016 M5 追记）。nlohmann/stb 不进本文件契约面：stb_image 只在 BakeProjectAtlas
// 解码路径（实现 TU 内含，include 域 PRIVATE 于 lemon-engine）。
#include "Assets/AtlasBake.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <set>
#include <unordered_set>

#include "stb_image.h"

#include "Core/FileOps.h"
#include "Core/Log.h"

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace lemon::assets {
namespace fs = std::filesystem;

namespace {

// LAT1 头（32B；字段自然对齐，小端宿主直写——LBA1 同款口径，ADR-016 M5 追记表）
struct LatHeader {
    char magic[4];        // 'L','A','T','1'
    uint16_t version;     // 1
    uint16_t headerSize;  // 32（自校验）
    uint16_t pageCount;   // ≥1
    uint16_t flags;       // 0 预留（采样/旋转策略归消费侧渲染配置）
    uint32_t pageWidth;   // 虚拟装箱页宽（=4096 参考值）
    uint32_t pageHeight;  // 同上
    uint32_t entryCount;  // ≥1
    uint32_t reserved;    // 0
    uint32_t payloadBytes;// Σ页 w×h×4（读取侧 64 位域对账）
};
static_assert(sizeof(LatHeader) == 32, "LAT1 header must be 32 bytes (ADR-016 M5)");

constexpr size_t kLatEntryBytes = 18; // guid u64 + page/x/y/w/h u16×5（紧排）
constexpr size_t kLatPageBytes = 8;   // w u32 + h u32

void PutLE16(uint8_t* p, uint16_t v) {
    p[0] = uint8_t(v);
    p[1] = uint8_t(v >> 8);
}
void PutLE32(uint8_t* p, uint32_t v) {
    p[0] = uint8_t(v);
    p[1] = uint8_t(v >> 8);
    p[2] = uint8_t(v >> 16);
    p[3] = uint8_t(v >> 24);
}
void PutLE64(uint8_t* p, uint64_t v) {
    PutLE32(p, uint32_t(v));
    PutLE32(p + 4, uint32_t(v >> 32));
}
uint16_t GetLE16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
uint32_t GetLE32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) |
           (uint32_t(p[3]) << 24);
}
uint64_t GetLE64(const uint8_t* p) { return GetLE32(p) | (uint64_t(GetLE32(p + 4)) << 32); }

uint32_t Align4(uint32_t v) { return (v + 3u) & ~3u; }

// tmp 唯一化（BakeAudioFile 同款动机）：并发烤同目标时各写各的 tmp，原子换名后
// 写者胜——确定性 tmp 名会让两把 FILE* 交错写坏产物。
uint32_t BakeTmpSeq() {
    static std::atomic<uint32_t> seq{1};
    return seq.fetch_add(1, std::memory_order_relaxed);
}
int BakePid() {
#if defined(_WIN32)
    return _getpid();
#else
    return static_cast<int>(::getpid());
#endif
}

} // namespace

bool PackAtlasPages(const std::vector<BakedAtlasImage>& images, BakedAtlasBuild& out,
                    std::string* errMsg) {
    out = BakedAtlasBuild{};
    const auto Fail = [errMsg](const char* m) {
        if (errMsg) *errMsg = m;
        return false;
    };
    if (images.empty()) return Fail("空输入");

    // 确定性全序：h desc → w desc → guid asc（guid 唯一由调用方保证——packager
    // 校验 guid 冲突在先；同输入同放置序 = 同容器字节）
    std::vector<uint32_t> order(images.size());
    for (uint32_t i = 0; i < order.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) {
        const BakedAtlasImage& A = images[a];
        const BakedAtlasImage& B = images[b];
        if (A.h != B.h) return A.h > B.h;
        if (A.w != B.w) return A.w > B.w;
        return A.guid < B.guid;
    });

    struct Placed {
        uint32_t img, page, x, y;
    };
    // 页状态：closed = oversized 专属页（不再收件）；shelf 行式游标
    struct PageCtx {
        uint32_t shelfY = 0, shelfH = 0, cursorX = 0;
        bool closed = false;
    };
    std::vector<PageCtx> pages;
    std::vector<Placed> placed;
    placed.reserve(images.size());

    for (uint32_t idx : order) {
        const BakedAtlasImage& img = images[idx];
        if (img.w == 0 || img.h == 0) return Fail("零尺寸精灵");
        if (img.w > kAtlasMaxImageDim || img.h > kAtlasMaxImageDim)
            return Fail("精灵超 GPU 尺寸域 16384");
        if (img.rgba.size() != size_t(img.w) * img.h * 4) return Fail("像素载荷尺寸不符");

        const uint32_t g = kAtlasGutter;
        if (img.w + 2 * g > kAtlasVirtualPage || img.h + 2 * g > kAtlasVirtualPage) {
            // 专属页：虚拟页放不下 → 精灵独占一页（(0,0) 起，无 gutter——页独占
            // 无邻居，边缘 clamp-to-edge 兜底）
            pages.push_back({0, 0, 0, /*closed=*/true});
            placed.push_back({idx, uint32_t(pages.size() - 1), 0, 0});
            continue;
        }
        bool done = false;
        if (!pages.empty() && !pages.back().closed) {
            PageCtx& p = pages.back();
            if (p.cursorX + img.w + g <= kAtlasVirtualPage && img.h <= p.shelfH) {
                placed.push_back({idx, uint32_t(pages.size() - 1), p.cursorX, p.shelfY});
                p.cursorX += img.w + g;
                done = true;
            } else {
                const uint32_t nextY = p.shelfY + p.shelfH + g;
                if (nextY + img.h + g <= kAtlasVirtualPage) {
                    p.shelfY = nextY;
                    p.shelfH = img.h;
                    p.cursorX = g;
                    placed.push_back({idx, uint32_t(pages.size() - 1), p.cursorX, p.shelfY});
                    p.cursorX += img.w + g;
                    done = true;
                }
            }
        }
        if (!done) { // 新页（首个精灵即开首条 shelf）
            PageCtx p;
            p.shelfY = g;
            p.shelfH = img.h;
            p.cursorX = g;
            placed.push_back({idx, uint32_t(pages.size()), g, g});
            p.cursorX += img.w + g;
            pages.push_back(p);
        }
    }

    // 页裁剪：用到 extent 取 4px 对齐（extent ≤ 4096 → 对齐值 ≤ 4096 恒成立；
    // 专属页 extent = 精灵尺寸）。左/上 gutter 保留、右/下裁至贴边——外缘采样
    // 由 clamp-to-edge 边缘延展兜底
    out.pages.resize(pages.size());
    for (const Placed& pl : placed) {
        const BakedAtlasImage& img = images[pl.img];
        BakedAtlasPage& pg = out.pages[pl.page];
        pg.w = std::max(pg.w, Align4(pl.x + img.w));
        pg.h = std::max(pg.h, Align4(pl.y + img.h));
    }

    uint64_t payload = 0;
    for (const BakedAtlasPage& pg : out.pages) payload += uint64_t(pg.w) * pg.h * 4;
    if (payload > UINT32_MAX)
        return Fail("页载荷超 u32 域（>4GiB）——压缩/分卷归 M7b，v1 拒烤");

    // 页合成 + 条目输出（放置序）
    out.pagePixels.resize(pages.size());
    for (size_t i = 0; i < out.pages.size(); ++i)
        out.pagePixels[i].assign(size_t(out.pages[i].w) * out.pages[i].h * 4, 0);
    out.entries.reserve(placed.size());
    for (const Placed& pl : placed) {
        const BakedAtlasImage& img = images[pl.img];
        const BakedAtlasPage& pg = out.pages[pl.page];
        for (uint32_t row = 0; row < img.h; ++row)
            std::memcpy(&out.pagePixels[pl.page][(size_t(pl.y + row) * pg.w + pl.x) * 4],
                        &img.rgba[size_t(row) * img.w * 4], size_t(img.w) * 4);
        BakedAtlasEntry e;
        e.guid = img.guid;
        e.page = uint16_t(pl.page);
        e.x = uint16_t(pl.x);
        e.y = uint16_t(pl.y);
        e.w = uint16_t(img.w);
        e.h = uint16_t(img.h);
        out.entries.push_back(e);
    }
    return true;
}

bool WriteBakedAtlasFile(const std::string& path, const BakedAtlasBuild& build) {
    if (build.pages.empty() || build.entries.empty() || build.pagePixels.size() != build.pages.size()) {
        LEMON_ERROR("LAT1：空图集拒绝写盘（页/条目/像素三方不齐）：%s", path.c_str());
        return false;
    }
    if (build.pages.size() > 65535 || build.entries.size() > UINT32_MAX) {
        LEMON_ERROR("LAT1：计数超域（页 %zu 条 %zu）：%s", build.pages.size(),
                    build.entries.size(), path.c_str());
        return false;
    }
    uint64_t payload = 0;
    for (const BakedAtlasPage& pg : build.pages) {
        if (pg.w == 0 || pg.h == 0 || pg.w > kAtlasMaxImageDim || pg.h > kAtlasMaxImageDim) {
            LEMON_ERROR("LAT1：页尺寸越域（%u×%u）：%s", pg.w, pg.h, path.c_str());
            return false;
        }
        payload += uint64_t(pg.w) * pg.h * 4;
    }
    if (payload > UINT32_MAX) {
        LEMON_ERROR("LAT1：载荷超 u32 域（%llu 字节）——压缩归 M7b：%s",
                    (unsigned long long)payload, path.c_str());
        return false;
    }
    // 写侧自洽校验（review 2026-10-05，判据与 LoadBakedAtlasFile 同源——否则坏
    // build 可写出"写盘成功但永不可载"的包，LBA1 review #21「产物永久不可载」
    // 同款教训：烤制期直白拒绝优于事后拒载）：像素尺寸与页尺寸一致 + 条目页号
    // 界内 + 矩形在页内 + guid 非零唯一
    for (size_t i = 0; i < build.pages.size(); ++i)
        if (build.pagePixels[i].size() != size_t(build.pages[i].w) * build.pages[i].h * 4) {
            LEMON_ERROR("LAT1：页 %zu 像素载荷 %zu ≠ %u×%u×4（build 不自洽）：%s", i,
                        build.pagePixels[i].size(), build.pages[i].w, build.pages[i].h,
                        path.c_str());
            return false;
        }
    {
        std::unordered_set<uint64_t> seen;
        for (const BakedAtlasEntry& e : build.entries) {
            const char* why = nullptr;
            if (e.guid == 0) why = "零 guid";
            else if (!seen.insert(e.guid).second) why = "guid 重复";
            else if (e.page >= build.pages.size()) why = "页号越界";
            else if (e.w == 0 || e.h == 0 || uint32_t(e.x) + e.w > build.pages[e.page].w ||
                     uint32_t(e.y) + e.h > build.pages[e.page].h)
                why = "矩形越出页界";
            if (why) {
                LEMON_ERROR("LAT1：条目 guid %016llx 不自洽（%s）拒绝写盘：%s",
                            (unsigned long long)e.guid, why, path.c_str());
                return false;
            }
        }
    }

    LatHeader h{};
    std::memcpy(h.magic, "LAT1", 4);
    h.version = 1;
    h.headerSize = sizeof h;
    h.pageCount = uint16_t(build.pages.size());
    h.flags = 0;
    h.pageWidth = kAtlasVirtualPage;
    h.pageHeight = kAtlasVirtualPage;
    h.entryCount = uint32_t(build.entries.size());
    h.reserved = 0;
    h.payloadBytes = uint32_t(payload);

    std::error_code ec;
    const fs::path fsPath(path);
    fs::create_directories(fsPath.parent_path(), ec);
    const std::string tmp =
        path + ".tmp" + std::to_string(BakePid()) + "_" + std::to_string(BakeTmpSeq());
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) {
        LEMON_ERROR("LAT1：tmp 打开失败：%s", tmp.c_str());
        return false;
    }
    bool ok = std::fwrite(&h, sizeof h, 1, f) == 1;
    for (const BakedAtlasPage& pg : build.pages) {
        uint8_t dim[kLatPageBytes];
        PutLE32(dim, pg.w);
        PutLE32(dim + 4, pg.h);
        ok = ok && std::fwrite(dim, kLatPageBytes, 1, f) == 1;
    }
    std::vector<uint8_t> ent(kLatEntryBytes);
    for (const BakedAtlasEntry& e : build.entries) {
        PutLE64(ent.data(), e.guid);
        PutLE16(ent.data() + 8, e.page);
        PutLE16(ent.data() + 10, e.x);
        PutLE16(ent.data() + 12, e.y);
        PutLE16(ent.data() + 14, e.w);
        PutLE16(ent.data() + 16, e.h);
        ok = ok && std::fwrite(ent.data(), kLatEntryBytes, 1, f) == 1;
    }
    for (const std::vector<uint8_t>& px : build.pagePixels)
        ok = ok && !px.empty() && std::fwrite(px.data(), px.size(), 1, f) == 1;
    ok = std::fclose(f) == 0 && ok;
    if (!ok) {
        std::remove(tmp.c_str());
        LEMON_ERROR("LAT1：写盘失败：%s", path.c_str());
        return false;
    }
    if (!RenameReplace(tmp, path)) {
        std::remove(tmp.c_str());
        LEMON_ERROR("LAT1：原子换名失败：%s", path.c_str());
        return false;
    }
    return true;
}

bool LoadBakedAtlasFile(const std::string& path, BakedAtlasBuild& out) {
    out = BakedAtlasBuild{};
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        LEMON_ERROR("LAT1：打开失败：%s", path.c_str());
        return false;
    }
    std::vector<uint8_t> buf;
    {
        char chunk[65536];
        size_t n = 0;
        while ((n = std::fread(chunk, 1, sizeof chunk, f)) > 0) buf.insert(buf.end(), chunk, chunk + n);
        const bool readErr = std::ferror(f) != 0;
        std::fclose(f);
        if (readErr) {
            LEMON_ERROR("LAT1：读取失败：%s", path.c_str());
            return false;
        }
    }
    const auto Reject = [&](const char* why) {
        LEMON_ERROR("LAT1：拒载（%s）：%s", why, path.c_str());
        return false;
    };
    if (buf.size() < sizeof(LatHeader)) return Reject("文件短于头");
    LatHeader h;
    std::memcpy(&h, buf.data(), sizeof h);
    if (std::memcmp(h.magic, "LAT1", 4) != 0) return Reject("魔数不符");
    if (h.version != 1) return Reject("版本不认");
    if (h.headerSize != sizeof h) return Reject("头长不符");
    if (h.pageCount == 0) return Reject("零页");
    if (h.entryCount == 0) return Reject("零条目");

    const uint64_t tableBytes =
        sizeof h + uint64_t(h.pageCount) * kLatPageBytes + uint64_t(h.entryCount) * kLatEntryBytes;
    if (buf.size() < tableBytes) return Reject("表区截断");

    out.pages.resize(h.pageCount);
    uint64_t payload = 0;
    for (uint32_t i = 0; i < h.pageCount; ++i) {
        const uint8_t* p = buf.data() + sizeof h + size_t(i) * kLatPageBytes;
        BakedAtlasPage& pg = out.pages[i];
        pg.w = GetLE32(p);
        pg.h = GetLE32(p + 4);
        if (pg.w == 0 || pg.h == 0 || pg.w > kAtlasMaxImageDim || pg.h > kAtlasMaxImageDim)
            return Reject("页尺寸越域");
        payload += uint64_t(pg.w) * pg.h * 4;
    }
    // 64 位域对账（回绕防线——LBA1 review 2026-10-01 先例）+ 尾字节全等
    if (payload != h.payloadBytes) return Reject("payloadBytes 域不符");
    if (tableBytes + payload != buf.size()) return Reject("载荷尺寸不符（截断/多出）");

    out.entries.resize(h.entryCount);
    std::unordered_set<uint64_t> guids;
    const size_t entBase = sizeof h + size_t(h.pageCount) * kLatPageBytes;
    for (uint32_t i = 0; i < h.entryCount; ++i) {
        const uint8_t* p = buf.data() + entBase + size_t(i) * kLatEntryBytes;
        BakedAtlasEntry& e = out.entries[i];
        e.guid = GetLE64(p);
        e.page = GetLE16(p + 8);
        e.x = GetLE16(p + 10);
        e.y = GetLE16(p + 12);
        e.w = GetLE16(p + 14);
        e.h = GetLE16(p + 16);
        if (e.guid == 0) return Reject("零 guid 条目");
        if (!guids.insert(e.guid).second) return Reject("guid 重复");
        if (e.page >= h.pageCount) return Reject("条目页号越界");
        const BakedAtlasPage& pg = out.pages[e.page];
        if (e.w == 0 || e.h == 0 || uint32_t(e.x) + e.w > pg.w || uint32_t(e.y) + e.h > pg.h)
            return Reject("条目矩形越出页界");
    }

    // 页载荷切片（整读峰值 = 文件大小 ×2——流式装载归登记项，v1 接受）
    out.pagePixels.resize(h.pageCount);
    uint64_t off = tableBytes;
    for (uint32_t i = 0; i < h.pageCount; ++i) {
        const size_t bytes = size_t(out.pages[i].w) * out.pages[i].h * 4;
        out.pagePixels[i].assign(buf.data() + off, buf.data() + off + bytes);
        off += bytes;
    }
    return true;
}

bool BakeProjectAtlas(const AssetIndex& index, const std::string& dst, AtlasBakeStats& stats) {
    stats = AtlasBakeStats{};
    std::vector<BakedAtlasImage> images;
    for (const IndexedEntry& e : index.Entries()) {
        if (e.type != AssetType::Sprite) continue;
        ++stats.sprites;
        int w = 0, h = 0, comp = 0;
        uint8_t* px = stbi_load(index.AbsolutePath(e).c_str(), &w, &h, &comp, 4);
        if (!px || w <= 0 || h <= 0) {
            LEMON_ERROR("图集烤制：解码失败：%s", e.relPath.c_str());
            stbi_image_free(px);
            return false;
        }
        BakedAtlasImage img;
        img.guid = e.guid;
        img.w = uint32_t(w);
        img.h = uint32_t(h);
        img.rgba.assign(px, px + size_t(w) * h * 4);
        stbi_image_free(px);
        images.push_back(std::move(img));
    }
    if (images.empty()) return false; // 无 sprite 项目：调用方按 stats.sprites==0 跳过

    BakedAtlasBuild build;
    std::string err;
    if (!PackAtlasPages(images, build, &err)) {
        LEMON_ERROR("图集装箱失败：%s", err.c_str());
        return false;
    }
    if (!WriteBakedAtlasFile(dst, build)) return false;
    stats.pages = uint32_t(build.pages.size());
    for (const BakedAtlasPage& pg : build.pages) stats.bytes += uint64_t(pg.w) * pg.h * 4;
    LEMON_LOG("图集烤制：精灵 %u → 页 %u（载荷 %llu 字节）→ %s", stats.sprites,
              stats.pages, (unsigned long long)stats.bytes, dst.c_str());
    return true;
}

} // namespace lemon::assets
