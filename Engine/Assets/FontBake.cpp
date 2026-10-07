// Lemon 引擎 — TTF→位图图集离线烘焙器实现（M7c 批① S1；见 FontBake.h 契约）
#include "Assets/FontBake.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>

#include <ft2build.h>
#include FT_FREETYPE_H

#include "Core/FileOps.h"
#include "Core/Log.h"

namespace lemon::assets {
namespace fs = std::filesystem;

// ---- UTF-8 解码（烘焙字符集 + BitmapFont 运行时寻址共用）----
// 容错语义（16 字节截断尾落在多字节序列中间的残缺防御）：非法续字节/截断
// 序列 = 丢弃该字符继续（不产 U+FFFD——战斗词表场景静默跳过比豆腐块干净）
bool DecodeUtf8(const std::string& s, std::vector<uint32_t>& out) {
    out.clear();
    out.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        const uint8_t c = (uint8_t)s[i];
        uint32_t cp = 0;
        size_t n = 0;
        if (c < 0x80) { cp = c; n = 1; }
        else if ((c & 0xE0) == 0xC0 && c >= 0xC2) { cp = c & 0x1F; n = 2; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; n = 3; }
        else if ((c & 0xF8) == 0xF0 && c <= 0xF4) { cp = c & 0x07; n = 4; }
        else { ++i; continue; } // 孤立续字节/超五字节：弃
        if (i + n > s.size()) break; // 截断尾：弃（FxText 16B 截断防御）
        bool ok = true;
        for (size_t k = 1; k < n; ++k) {
            const uint8_t cc = (uint8_t)s[i + k];
            if ((cc & 0xC0) != 0x80) { ok = false; break; }
            cp = (cp << 6) | (cc & 0x3F);
        }
        if (!ok) { ++i; continue; }
        if (n == 3 && cp < 0x800) ok = false;            // 过长编码
        if (n == 4 && cp < 0x10000) ok = false;
        if (cp > 0x10FFFF) ok = false;
        if (ok) out.push_back(cp);
        i += n;
    }
    return !out.empty();
}

// ---- 参数哈希（FNV-1a 64：golden 判定 = 同参数两次烘焙字节一致的锚点之一）----
uint64_t FontBakeParams::Hash() const {
    const auto& cs = charset;
    // kHashSalt：烤制器算法版本盐——算法/序列化修正后 +1，使磁盘上旧 .baked
    // 的 paramsHash 与新算值必然失配 → BakeFontStale 判陈旧全量自动重烘
    //（TTF mtime 与用户参数都感知不到烤制代码自身的变更）。初版 = 2
    //（1 = 2026-10-07 字形像素落位修正前的产物，全部作废）。
    static constexpr uint64_t kHashSalt = 2;
    uint64_t h = 0xcbf29ce484222325ull;
    auto mix = [&h](const void* p, size_t n) {
        const uint8_t* b = (const uint8_t*)p;
        for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 0x100000001b3ull; }
    };
    mix(&kHashSalt, sizeof(kHashSalt));
    mix(&fontSizePx, sizeof(fontSizePx));
    mix(&outlinePx, sizeof(outlinePx));
    mix(&outlineColor, sizeof(outlineColor));
    mix(cs.data(), cs.size());
    return h;
}

std::string FontBakeParams::EffectiveCharset() const {
    if (!charset.empty()) return charset;
    std::string s;
    s.reserve(95);
    for (uint32_t c = 32; c <= 126; ++c) s.push_back((char)c);
    return s;
}

// ---- .baked 布局 ----
// 头 = sizeof(FileHeader) = 40B（36B 字段 + 8 对齐尾填充；magic[4] version u32
//      paramsHash u64 pageW/H u32 ascender/descender i32 glyphCount u32）
// 表：FontGlyph × glyphCount（20B 行，codepoint 升序——std::map 序）
// 尾：RGBA 页位图 pageW*pageH*4
// 读写两侧均按 sizeof(FileHeader) 定位——直接改字段须同步检查结构填充。
namespace {

bool ReadAll(const char* path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    const std::streamoff n = f.tellg();
    if (n < 0) return false;
    f.seekg(0, std::ios::beg);
    out.resize((size_t)n);
    if (n > 0) f.read((char*)out.data(), n);
    return f.good() || f.eof();
}

struct FileHeader {
    char magic[4];
    uint32_t version;
    uint64_t paramsHash;
    uint32_t pageW, pageH;
    int32_t ascender, descender;
    uint32_t glyphCount;
};
constexpr uint32_t kVersion = 1;
constexpr uint32_t kPageW = 512;      // 页宽定死（行装箱宽）；高按需倍增
constexpr uint32_t kPageHMax = 4096;  // 上限（超限截断字符集——战斗词表场景不会触）
constexpr uint32_t kSpacing = 1;      // 字形间 1px 防渗色（内置 5×7 页同款）

/// A8 位图 N 轮 8 邻域膨胀（描边 = 字形 alpha>0 视为实心源）
std::vector<uint8_t> Dilate(const std::vector<uint8_t>& src, uint32_t w, uint32_t h,
                            uint32_t rounds) {
    std::vector<uint8_t> cur = src, next;
    for (uint32_t r = 0; r < rounds; ++r) {
        next.assign(src.size(), 0);
        for (uint32_t y = 0; y < h; ++y)
            for (uint32_t x = 0; x < w; ++x) {
                uint8_t m = 0;
                for (int dy = -1; dy <= 1 && !m; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int xx = (int)x + dx, yy = (int)y + dy;
                        if (xx < 0 || yy < 0 || xx >= (int)w || yy >= (int)h) continue;
                        if (cur[(size_t)yy * w + xx]) { m = 1; break; }
                    }
                next[(size_t)y * w + x] = m;
            }
        cur.swap(next);
    }
    return cur;
}
} // namespace

bool BakeFontFile(const char* srcTtf, const char* dstBaked, const FontBakeParams& p) {
    // 字符集解码 + 去重升序（map 兼做排序与重复吸收——装箱序稳定 = golden 前提）
    std::vector<uint32_t> cps;
    if (!DecodeUtf8(p.EffectiveCharset(), cps)) {
        LEMON_ERROR("font: 字符集解码为空（UTF-8 非法或空串）：%s", srcTtf);
        return false;
    }
    std::map<uint32_t, bool> uniq;
    for (uint32_t cp : cps) uniq[cp] = true;

    FT_Library ft = nullptr;
    if (FT_Init_FreeType(&ft)) {
        LEMON_ERROR("font: FreeType 初始化失败");
        return false;
    }
    struct FtGuard {
        FT_Library f;
        ~FtGuard() { if (f) FT_Done_FreeType(f); }
    } guard{ft};

    FT_Face face = nullptr;
    if (FT_New_Face(ft, srcTtf, 0, &face) || !face) {
        LEMON_ERROR("font: 字体文件不可开（路径错/非 TTF-OTF）：%s", srcTtf);
        return false;
    }
    struct FaceGuard {
        FT_Face f;
        ~FaceGuard() { if (f) FT_Done_Face(f); }
    } faceGuard{face};
    FT_Set_Pixel_Sizes(face, 0, p.fontSizePx);

    // 光栅化（含描边扩边）→ shelf 装箱
    struct Placed {
        FontGlyph g;
        std::vector<uint8_t> rgba; // 扩边后位图（含描边色）——直写页
    };
    std::vector<Placed> placed;
    placed.reserve(uniq.size());
    uint32_t shelfY = kSpacing, shelfH = 0, penX = kSpacing; // 装箱游标
    uint32_t dropped = 0;
    const int ascender = (int)(face->size->metrics.ascender >> 6);
    const int descender = (int)(face->size->metrics.descender >> 6);

    for (auto& [cp, _] : uniq) {
        const FT_UInt gi = FT_Get_Char_Index(face, cp);
        if (gi == 0 && cp != '?') { // .notdef： '?'/不存在字形都跳（回退内置页补位）
            ++dropped;
            continue;
        }
        if (FT_Load_Glyph(face, gi, FT_LOAD_RENDER)) { ++dropped; continue; }
        const FT_GlyphSlot& gl = face->glyph;
        const FT_Bitmap& bm = gl->bitmap;
        const uint32_t bw = bm.width, bh = bm.rows;
        const uint32_t pad = p.outlinePx;
        const uint32_t cw = bw + pad * 2, ch = bh + pad * 2;
        if (!cw || !ch) { // 空格类（零尺寸位图）：只记 advance（+2*pad 与实字形节奏一致）
            Placed pl;
            pl.g.codepoint = cp;
            pl.g.advance = (uint16_t)std::max(0, (int)(gl->advance.x >> 6) + 2 * (int)pad);
            placed.push_back(std::move(pl));
            continue;
        }
        if (penX + kSpacing + cw > kPageW) { // 换行
            shelfY += shelfH + kSpacing;
            shelfH = 0;
            penX = kSpacing;
        }
        if (shelfY + kSpacing + ch > kPageHMax) { ++dropped; continue; } // 页满截断
        Placed pl;
        pl.g.codepoint = cp;
        pl.g.pageX = (uint16_t)penX;
        pl.g.pageY = (uint16_t)shelfY;
        pl.g.width = (uint16_t)cw;
        pl.g.height = (uint16_t)ch;
        pl.g.bearingX = (int16_t)(gl->bitmap_left - (int)pad);
        pl.g.bearingY = (int16_t)(gl->bitmap_top + (int)pad);
        // 步进加 2*pad：位图含描边扩边后比原字形宽，advance 不补则相邻字形 quad
        // 重叠 1-2px（描边互相压边——2026-10-07 走查"字体重叠"根因之一）
        pl.g.advance = (uint16_t)std::max(0, (int)(gl->advance.x >> 6) + 2 * (int)pad);
        // 页位图合成：pad 圈 = 描边色（膨胀实心），内圈 = 白字
        pl.rgba.assign((size_t)cw * ch * 4, 0);
        std::vector<uint8_t> core((size_t)cw * ch, 0);
        const int pitch = bm.pitch < 0 ? -bm.pitch : bm.pitch; // flow-down 面板防御
        for (uint32_t y = 0; y < bh; ++y)
            for (uint32_t x = 0; x < bw; ++x) {
                const uint8_t a = bm.buffer[(size_t)y * pitch + x];
                if (!a) continue;
                const size_t di = (size_t)(y + pad) * cw + (x + pad);
                core[di] = 1;
                uint8_t* px = &pl.rgba[di * 4];
                px[0] = px[1] = px[2] = 255;
                px[3] = a;
            }
        if (pad) {
            const std::vector<uint8_t> ring = Dilate(core, cw, ch, pad);
            const float or_ = (p.outlineColor >> 0) & 0xFF, og = (p.outlineColor >> 8) & 0xFF,
                        ob = (p.outlineColor >> 16) & 0xFF,
                        oa = (p.outlineColor >> 24) & 0xFF;
            for (size_t i = 0; i < ring.size(); ++i) {
                if (!ring[i] || core[i]) continue; // 只填外圈（内圈白字已写）
                uint8_t* px = &pl.rgba[i * 4];
                px[0] = (uint8_t)or_; px[1] = (uint8_t)og; px[2] = (uint8_t)ob;
                px[3] = (uint8_t)oa;
            }
        }
        placed.push_back(std::move(pl));
        penX += cw + kSpacing;
        shelfH = std::max(shelfH, ch);
    }
    if (placed.empty()) {
        LEMON_ERROR("font: 零可烘字形（字号 %u）：%s", p.fontSizePx, srcTtf);
        return false;
    }

    // 页高 = 覆盖末行即止的 2 幂（512 起；纹理对齐惯例，空区零成本）
    uint32_t pageH = 512;
    while (pageH < shelfY + shelfH + kSpacing && pageH < kPageHMax) pageH *= 2;
    if (dropped)
        LEMON_WARN("font: %u 字形未入页（字体缺字形/页满截断——缺字回退内置页）："
                   "%s",
                   dropped, srcTtf);

    // 序列化（小端定长——golden：同 TTF 同参数两次烘焙字节一致）
    FileHeader h{};
    std::memcpy(h.magic, kFontBakeMagic, 4);
    h.version = kVersion;
    h.paramsHash = p.Hash();
    h.pageW = kPageW;
    h.pageH = pageH;
    h.ascender = ascender;
    h.descender = descender;
    h.glyphCount = (uint32_t)placed.size();
    std::vector<uint8_t> buf(sizeof(h) + (size_t)h.glyphCount * sizeof(FontGlyph) +
                             (size_t)kPageW * pageH * 4, 0);
    std::memcpy(buf.data(), &h, sizeof(h));
    // 像素区基址 = 表后固定偏移（tableEnd）——不能复用随表推进的游标做基址，
    // 否则第 k 个字形位图整体左移 (N-1-k)*5px、后写覆盖先写（2026-10-07 真人
    // 走查「暴击」显示成邻槽字形「格挡」的根因；golden 字节一致测不出自错位）
    const size_t pixBase = sizeof(h) + (size_t)h.glyphCount * sizeof(FontGlyph);
    size_t off = sizeof(h);
    for (const Placed& pl : placed) {
        std::memcpy(buf.data() + off, &pl.g, sizeof(FontGlyph));
        off += sizeof(FontGlyph);
        if (!pl.rgba.empty())
            for (uint32_t y = 0; y < pl.g.height; ++y)
                std::memcpy(&buf[pixBase + ((size_t)(pl.g.pageY + y) * kPageW + pl.g.pageX) * 4],
                            &pl.rgba[(size_t)y * pl.g.width * 4], (size_t)pl.g.width * 4);
    }
    return WriteFileAtomic(dstBaked, buf.data(), buf.size());
}

bool LoadBakedFont(const char* bakedPath, BakedFontInfo& info,
                   std::vector<FontGlyph>& glyphs, std::vector<uint8_t>& rgba) {
    std::vector<uint8_t> buf;
    if (!ReadAll(bakedPath, buf)) return false;
    if (buf.size() < sizeof(FileHeader)) return false;
    FileHeader h;
    std::memcpy(&h, buf.data(), sizeof(h));
    if (std::memcmp(h.magic, kFontBakeMagic, 4) != 0 || h.version != kVersion) return false;
    const size_t tableEnd = sizeof(h) + (size_t)h.glyphCount * sizeof(FontGlyph);
    const size_t pixels = (size_t)h.pageW * h.pageH * 4;
    if (buf.size() != tableEnd + pixels) return false;
    info.paramsHash = h.paramsHash;
    info.pageW = h.pageW;
    info.pageH = h.pageH;
    info.ascender = h.ascender;
    info.descender = h.descender;
    info.glyphCount = h.glyphCount;
    glyphs.resize(h.glyphCount);
    if (h.glyphCount)
        std::memcpy(glyphs.data(), buf.data() + sizeof(h), tableEnd - sizeof(h));
    rgba.assign(buf.begin() + (long)tableEnd, buf.end());
    return true;
}

bool BakeFontStale(const char* srcTtf, const char* dstBaked, uint64_t paramsHash) {
    std::error_code ec;
    if (!fs::exists(dstBaked, ec)) return true;
    BakedFontInfo info;
    std::vector<FontGlyph> glyphs;
    std::vector<uint8_t> rgba;
    if (!LoadBakedFont(dstBaked, info, glyphs, rgba)) return true; // 坏产物 = 重烘
    if (info.paramsHash != paramsHash) return true; // 字符集/字号/描边变更（mtime 不动）
    const auto dstT = fs::last_write_time(dstBaked, ec);
    if (ec) return true;
    const auto srcT = fs::last_write_time(srcTtf, ec);
    if (!ec && srcT > dstT) return true;
    const auto metaT = fs::last_write_time(std::string(srcTtf) + ".meta", ec);
    return !ec && metaT > dstT;
}

std::string FontBakedPath(const std::string& projectRoot, uint64_t guid) {
    char hex[17];
    std::snprintf(hex, sizeof(hex), "%016llx", (unsigned long long)guid);
    return projectRoot + "/.lemon/baked/fonts/" + hex + ".baked";
}

} // namespace lemon::assets
