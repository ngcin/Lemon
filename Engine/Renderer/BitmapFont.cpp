#include "Renderer/BitmapFont.h"

#include <cstring>

#include "Assets/FontBake.h"
#include "Core/Log.h"
#include "Renderer/Atlas.h"
#include "Renderer/SpriteTypes.h" // kPktUvOverride

namespace lemon::renderer {

// 5×7 像素字模（公有领域经典布局；每字符 5 列，每列低 7 位 = 行，bit0 顶行）
// 索引 = ASCII - 32；顺序 0x20..0x7E
static constexpr uint32_t kGlyphCount = BitmapFont::kCharCount;
static const uint8_t kFont5x7[kGlyphCount * 5] = {
    // ' ' '!' '"' '#' '$' '%' '&' '''
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x5F, 0x00, 0x00, 0x00, 0x07, 0x00, 0x07, 0x00,
    0x14, 0x7F, 0x14, 0x7F, 0x14, 0x24, 0x2A, 0x7F, 0x2A, 0x12, 0x23, 0x13, 0x08, 0x64, 0x62,
    0x36, 0x49, 0x55, 0x22, 0x50, 0x00, 0x05, 0x03, 0x00, 0x00,
    // '(' ')' '*' '+' ',' '-' '.' '/'
    0x00, 0x1C, 0x22, 0x41, 0x00, 0x00, 0x41, 0x22, 0x1C, 0x00, 0x14, 0x08, 0x3E, 0x08, 0x14,
    0x08, 0x08, 0x3E, 0x08, 0x08, 0x00, 0x50, 0x30, 0x00, 0x00, 0x08, 0x08, 0x08, 0x08, 0x08,
    0x00, 0x60, 0x60, 0x00, 0x00, 0x20, 0x10, 0x08, 0x04, 0x02,
    // '0' '1' '2' '3' '4'
    0x3E, 0x51, 0x49, 0x45, 0x3E, 0x00, 0x42, 0x7F, 0x40, 0x00, 0x42, 0x61, 0x51, 0x49, 0x46,
    0x21, 0x41, 0x45, 0x4B, 0x31, 0x18, 0x14, 0x12, 0x7F, 0x10,
    // '5' '6' '7' '8' '9'
    0x27, 0x45, 0x45, 0x45, 0x39, 0x3C, 0x4A, 0x49, 0x49, 0x30, 0x01, 0x71, 0x09, 0x05, 0x03,
    0x36, 0x49, 0x49, 0x49, 0x36, 0x06, 0x49, 0x49, 0x29, 0x1E,
    // ':' ';' '<' '=' '>' '?'
    0x00, 0x36, 0x36, 0x00, 0x00, 0x00, 0x56, 0x36, 0x00, 0x00, 0x08, 0x14, 0x22, 0x41, 0x00,
    0x14, 0x14, 0x14, 0x14, 0x14, 0x00, 0x41, 0x22, 0x14, 0x08, 0x02, 0x01, 0x51, 0x09, 0x06,
    // '@' 'A' 'B' 'C' 'D' 'E'
    0x32, 0x49, 0x79, 0x41, 0x3E, 0x7E, 0x11, 0x11, 0x11, 0x7E, 0x7F, 0x49, 0x49, 0x49, 0x36,
    0x3E, 0x41, 0x41, 0x41, 0x22, 0x7F, 0x41, 0x41, 0x22, 0x1C, 0x7F, 0x49, 0x49, 0x49, 0x41,
    // 'F' 'G' 'H' 'I' 'J' 'K'
    0x7F, 0x09, 0x09, 0x09, 0x01, 0x3E, 0x41, 0x49, 0x49, 0x7A, 0x7F, 0x08, 0x08, 0x08, 0x7F,
    0x00, 0x41, 0x7F, 0x41, 0x00, 0x20, 0x40, 0x41, 0x3F, 0x01, 0x7F, 0x08, 0x14, 0x22, 0x41,
    // 'L' 'M' 'N' 'O' 'P' 'Q'
    0x7F, 0x40, 0x40, 0x40, 0x40, 0x7F, 0x02, 0x0C, 0x02, 0x7F, 0x7F, 0x04, 0x08, 0x10, 0x7F,
    0x3E, 0x41, 0x41, 0x41, 0x3E, 0x7F, 0x09, 0x09, 0x09, 0x06, 0x3E, 0x41, 0x51, 0x21, 0x5E,
    // 'R' 'S' 'T' 'U' 'V' 'W'
    0x7F, 0x09, 0x19, 0x29, 0x46, 0x46, 0x49, 0x49, 0x49, 0x31, 0x01, 0x01, 0x7F, 0x01, 0x01,
    0x3F, 0x40, 0x40, 0x40, 0x3F, 0x1F, 0x20, 0x40, 0x20, 0x1F, 0x3F, 0x40, 0x30, 0x40, 0x3F,
    // 'X' 'Y' 'Z' '[' '\\' ']' '^' '_'
    0x63, 0x14, 0x08, 0x14, 0x63, 0x07, 0x08, 0x70, 0x08, 0x07, 0x61, 0x51, 0x49, 0x45, 0x43,
    0x00, 0x7F, 0x41, 0x41, 0x00, 0x02, 0x04, 0x08, 0x10, 0x20, 0x00, 0x41, 0x41, 0x7F, 0x00,
    0x04, 0x02, 0x01, 0x02, 0x04, 0x40, 0x40, 0x40, 0x40, 0x40,
    // '`' 'a' 'b' 'c' 'd' 'e'
    0x00, 0x01, 0x02, 0x04, 0x00, 0x20, 0x54, 0x54, 0x54, 0x78, 0x7F, 0x48, 0x44, 0x44, 0x38,
    0x38, 0x44, 0x44, 0x44, 0x20, 0x38, 0x44, 0x44, 0x48, 0x7F, 0x38, 0x54, 0x54, 0x54, 0x18,
    // 'f' 'g' 'h' 'i' 'j' 'k'
    0x08, 0x7E, 0x09, 0x01, 0x02, 0x0C, 0x52, 0x52, 0x52, 0x3E, 0x7F, 0x08, 0x04, 0x04, 0x78,
    0x00, 0x44, 0x7D, 0x40, 0x00, 0x20, 0x40, 0x44, 0x3D, 0x00, 0x7F, 0x10, 0x28, 0x44, 0x00,
    // 'l' 'm' 'n' 'o' 'p' 'q'
    0x00, 0x41, 0x7F, 0x40, 0x00, 0x7C, 0x04, 0x18, 0x04, 0x78, 0x7C, 0x08, 0x04, 0x04, 0x78,
    0x38, 0x44, 0x44, 0x44, 0x38, 0x7C, 0x14, 0x14, 0x14, 0x08, 0x08, 0x14, 0x14, 0x18, 0x7C,
    // 'r' 's' 't' 'u' 'v' 'w'
    0x7C, 0x08, 0x04, 0x04, 0x08, 0x48, 0x54, 0x54, 0x54, 0x20, 0x04, 0x3F, 0x44, 0x40, 0x20,
    0x3C, 0x40, 0x40, 0x20, 0x7C, 0x1C, 0x20, 0x40, 0x20, 0x1C, 0x3C, 0x40, 0x30, 0x40, 0x3C,
    // 'x' 'y' 'z' '{' '|' '}' '~'
    0x44, 0x28, 0x10, 0x28, 0x44, 0x0C, 0x50, 0x50, 0x50, 0x3C, 0x44, 0x64, 0x54, 0x4C, 0x44,
    0x00, 0x08, 0x36, 0x41, 0x00, 0x00, 0x08, 0x7F, 0x08, 0x00, 0x00, 0x41, 0x36, 0x08, 0x00,
    0x00, 0x02, 0x01, 0x02, 0x04,
};

bool BitmapFont::Init(rhi::Device& device, AtlasRegistry& atlas, uint32_t bindlessSlot) {
    // 槽位冲突走头文件承诺的 return false（review 2026-10-02 #47）——原先实现
    // 恒 return true，冲突实际由 RegisterAtlas 内 LEMON_ASSERT 中止（release 亦
    // 然）。先查后建：冲突时不创建纹理不留半登记
    uint32_t probeW = 0, probeH = 0;
    if (atlas.AtlasTexture(bindlessSlot, probeW, probeH).IsValid()) {
        LEMON_WARN("BitmapFont::Init 失败：槽位 %u 已被占用", bindlessSlot);
        return false;
    }
    // 页布局：每行 16 字符（16×6=96px），95 字符 → 6 行 ×8 = 48px；页 128×64
    constexpr uint32_t kPageW = 128, kPageH = 64, kCols = 16;
    std::vector<uint8_t> px((size_t)kPageW * kPageH * 4, 0);
    for (uint32_t c = 0; c < kCharCount; ++c) {
        uint32_t cellX = (c % kCols) * kCellW;
        uint32_t cellY = (c / kCols) * kCellH;
        for (uint32_t col = 0; col < kGlyphW; ++col) {
            uint8_t bits = kFont5x7[c * 5 + col];
            for (uint32_t row = 0; row < kGlyphH; ++row) {
                if ((bits >> row) & 1) {
                    uint8_t* p =
                        &px[(((size_t)cellY + row) * kPageW + ((size_t)cellX + col)) * 4];
                    p[0] = p[1] = p[2] = p[3] = 255; // 白字，实例色调制
                }
            }
        }
    }
    rhi::Texture tex = device.CreateTexture(
        {.width = kPageW, .height = kPageH, .debugName = "fontPage"});
    device.UploadTexture(tex, px.data(), px.size());
    device.BindTextureToSlot(tex, bindlessSlot);
    atlas.RegisterAtlas(bindlessSlot, tex, kPageW, kPageH);
    atlasSlot_ = bindlessSlot;
    for (uint32_t c = 0; c < kCharCount; ++c) {
        uint32_t cx = (c % kCols) * kCellW, cy = (c / kCols) * kCellH;
        glyphSprites_[c] = atlas.AddSprite(bindlessSlot, cx, cy, kCellW, kGlyphH);
    }
    return true;
}

void BitmapFont::DrawText(std::vector<SpritePacket>& out, const char* text, Vec2 pos,
                          float charScale, uint32_t colorBits, uint8_t layer) const {
    SpriteBatchKey key = MakeBatchKey(atlasSlot_, BlendKind::Alpha, FilterKind::Point, layer);
    const uint64_t kh = (key.hash >> 45) & 0xFFFFull;
    float x = pos.x;
    for (const char* s = text; *s; ++s) {
        uint8_t c = (uint8_t)*s;
        if (c < kFirstChar || c >= kFirstChar + kCharCount) c = '?';
        uint32_t gi = c - kFirstChar;
        if (c != ' ') {
            SpritePacket p;
            p.sortKey = ((uint64_t)layer << 56) | (kh << 40) | (uint64_t)out.size();
            p.key = key;
            p.spriteId = glyphSprites_[gi];
            p.colorBits = colorBits;
            p.flags = 0;
            p.posX = x;
            p.posY = pos.y;
            p.rot = 0;
            // SpritePacket.scale = 渲染像素宽高
            p.scaleX = (float)kCellW * charScale;
            p.scaleY = (float)kGlyphH * charScale;
            out.push_back(p);
        }
        x += (float)kCellW * charScale;
    }
}

float BitmapFont::TextWidth(const char* text, float charScale) const {
    return (float)(kCellW * (uint32_t)std::strlen(text)) * charScale;
}

// ---- 外部烘焙页（M7c 批① S1；LBF1 装载 + UTF-8 寻址 + 内置页回退）----

bool BitmapFont::LoadBaked(rhi::Device& device, AtlasRegistry& atlas, uint32_t bindlessSlot,
                           const char* bakedPath) {
    uint32_t pw = 0, ph = 0;
    if (HasBaked() && bindlessSlot == bakedSlot_) {
        // 同槽换页（字体资产热改重烘 → 重装载）：字形零 sprite 登记（UV 覆盖
        // 通道），UnregisterAtlas 无悬空引用，干净换
        atlas.UnregisterAtlas(bindlessSlot);
        bakedSlot_ = 0;
    } else if (atlas.AtlasTexture(bindlessSlot, pw, ph).IsValid()) {
        LEMON_WARN("BitmapFont::LoadBaked 失败：槽位 %u 已被占用", bindlessSlot);
        return false;
    }
    assets::BakedFontInfo info;
    std::vector<assets::FontGlyph> glyphs;
    std::vector<uint8_t> rgba;
    if (!assets::LoadBakedFont(bakedPath, info, glyphs, rgba)) {
        LEMON_WARN("font: 烘焙产物不可读（重导或等后台烤制完成）：%s", bakedPath);
        return false;
    }
    rhi::Texture tex = device.CreateTexture(
        {.width = info.pageW, .height = info.pageH, .debugName = "fontBakedPage"});
    device.UploadTexture(tex, rgba.data(), rgba.size());
    device.BindTextureToSlot(tex, bindlessSlot);
    atlas.RegisterAtlas(bindlessSlot, tex, info.pageW, info.pageH);
    bakedGlyphs_.clear();
    bakedGlyphs_.reserve(glyphs.size() * 2);
    for (const assets::FontGlyph& g : glyphs) {
        GlyphMeta m;
        m.advance = g.advance;
        m.bearingX = g.bearingX;
        m.bearingY = g.bearingY;
        m.w = g.width;
        m.h = g.height;
        m.pageX = g.pageX;
        m.pageY = g.pageY;
        bakedGlyphs_[g.codepoint] = m;
    }
    bakedSlot_ = bindlessSlot;
    bakedPageW_ = info.pageW;
    bakedPageH_ = info.pageH;
    bakedAscender_ = info.ascender;
    bakedDescender_ = info.descender;
    bakedPath_ = bakedPath; // 留档（RebuildBaked 用）
    fallbackWarned_ = false; // 新页装载重置（换字体后缺字面可能变化）
    LEMON_LOG("font: 烘焙页装载（%u 字形，%ux%u，槽 %u）：%s", info.glyphCount,
              info.pageW, info.pageH, bindlessSlot, bakedPath);
    return true;
}

float BitmapFont::LineHeight(float scale) const {
    if (!HasBaked()) return (float)kCellH * scale;
    return (float)(bakedAscender_ - bakedDescender_) * scale;
}

void BitmapFont::DrawTextEx(std::vector<SpritePacket>& out, const char* utf8, Vec2 pos,
                            float scale, uint32_t colorBits, uint8_t layer) const {
    // 无外部页：整体走内置 ASCII 路径（现网行为，缺字 '?' 补位语义不变）
    if (!HasBaked()) {
        DrawText(out, utf8, pos, scale, colorBits, layer);
        return;
    }
    std::vector<uint32_t> cps;
    assets::DecodeUtf8(utf8, cps);
    const SpriteBatchKey extKey = MakeBatchKey(bakedSlot_, BlendKind::Alpha,
                                               FilterKind::Linear, layer);
    const uint64_t extKh = (extKey.hash >> 45) & 0xFFFFull;
    const float baseline = pos.y + (float)bakedAscender_ * scale;
    // UV 覆盖通道产包：spriteId 占位 = 内置页合法号（Bake 查表被覆盖位短路，
    // 批键 textureAtlas = 烘焙页槽决定采样页）
    const uint32_t placeSprite = glyphSprites_['?' - kFirstChar];
    float x = pos.x;
    for (uint32_t cp : cps) {
        if (auto it = bakedGlyphs_.find(cp); it != bakedGlyphs_.end()) {
            const GlyphMeta& m = it->second;
            if (m.w && m.h) { // 空白字形（空格类）只步进
                SpritePacket p;
                p.sortKey = ((uint64_t)layer << 56) | (extKh << 40) | (uint64_t)out.size();
                p.key = extKey;
                p.spriteId = placeSprite;
                p.colorBits = colorBits;
                p.flags = kPktUvOverride;
                p.uv0u = (float)m.pageX / (float)bakedPageW_;
                p.uv0v = (float)m.pageY / (float)bakedPageH_;
                p.uv1u = (float)(m.pageX + m.w) / (float)bakedPageW_;
                p.uv1v = (float)(m.pageY + m.h) / (float)bakedPageH_;
                // 世界 Y 向下（Camera2D 直映射，DevLog 批① 教训④）：字形顶 =
                // baseline - bearingY（bearingY = 基线上方为正）→ quad 中心 =
                // baseline - (bearingY - h/2)*scale
                p.posX = x + ((float)m.bearingX + (float)m.w * 0.5f) * scale;
                p.posY = baseline - ((float)m.bearingY - (float)m.h * 0.5f) * scale;
                p.rot = 0;
                p.scaleX = (float)m.w * scale;
                p.scaleY = (float)m.h * scale;
                out.push_back(p);
            }
            x += (float)m.advance * scale;
            continue;
        }
        // 缺字形：ASCII 可显 → 内置 5×7 回退（换页 = 换批键，自然分批）
        if (cp >= kFirstChar && cp < kFirstChar + kCharCount && cp != ' ') {
            if (!fallbackWarned_) {
                fallbackWarned_ = true;
                LEMON_WARN("font: 烘焙页缺字形 U+%04X，回退内置 5×7（词表字符集未覆盖"
                           "——改 .meta charset 重烘）",
                           cp);
            }
            const SpriteBatchKey fbKey = MakeBatchKey(atlasSlot_, BlendKind::Alpha,
                                                      FilterKind::Point, layer);
            const uint64_t fbKh = (fbKey.hash >> 45) & 0xFFFFull;
            SpritePacket p;
            p.sortKey = ((uint64_t)layer << 56) | (fbKh << 40) | (uint64_t)out.size();
            p.key = fbKey;
            p.spriteId = glyphSprites_[cp - kFirstChar];
            p.colorBits = colorBits;
            p.flags = 0;
            p.posX = x + (float)kCellW * 0.5f * scale;
            // 基线对齐：内置页 quad 底贴基线（5×7 无下伸的近似对齐）
            p.posY = baseline - (float)kGlyphH * 0.5f * scale;
            p.rot = 0;
            p.scaleX = (float)kCellW * scale;
            p.scaleY = (float)kGlyphH * scale;
            out.push_back(p);
        }
        x += (float)kCellW * scale; // 缺字步进按内置 cell（含空格）
    }
}

float BitmapFont::TextWidthEx(const char* utf8, float scale) const {
    if (!HasBaked()) return TextWidth(utf8, scale);
    std::vector<uint32_t> cps;
    assets::DecodeUtf8(utf8, cps);
    float w = 0;
    for (uint32_t cp : cps) {
        auto it = bakedGlyphs_.find(cp);
        w += it != bakedGlyphs_.end() ? (float)it->second.advance * scale
                                      : (float)kCellW * scale;
    }
    return w;
}

} // namespace lemon::renderer
