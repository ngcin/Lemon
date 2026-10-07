// Lemon 引擎 — TTF→位图图集离线烘焙器（M7c 批① S1；02 §7 红线：HUD/飘字高频
// 路径绝不动态栅格化——FreeType 只活在导入期，运行时只读 .baked 位图页）。
// 形态 = 音频烘焙同款（ADR-015 先例）：编辑器后台 worker 烤制 → .lemon/baked/
// fonts/<guid>.baked；产物自包含（页位图 + 度量表），装载侧零 FreeType。
//   * 字符集 = 项目级配置（.meta importer 段），变更经 paramsHash 头对比触发重烘
//   * 风格最小版：单字号档 + 描边扩边（SDF/kerning/渐变登记不排）
//   * 装箱 = shelf 行装箱单页（页高按需倍增，上限 4096 超限截断红字）
// 纯逻辑 + FreeType 光栅化，零 Vulkan/ImGui 依赖（单测可直跑）。
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace lemon::assets {

/// 烘焙参数（.meta importer 段字段一一对应；hash 进 .baked 头做增量重烘判定）
struct FontBakeParams {
    uint32_t fontSizePx = 24;               // 光栅化字号（像素高度）
    std::string charset;                    // UTF-8 字符集原文（默认空 = ASCII 95）
    uint8_t outlinePx = 0;                  // 描边扩边像素（0 = 无）
    uint32_t outlineColor = 0xFF202020u;    // RGBA（同 SpriteRenderer.colorRGBA 序）

    /// FNV-1a 64（字号|描边|描边色|字符集原文）——同参数必同 hash（golden 判定）
    uint64_t Hash() const;
    /// 字符集默认填充：空 charset = ASCII 32..126
    std::string EffectiveCharset() const;
};

/// 字形度量（.baked 载入后的运行时查表行；20B 定长）
struct FontGlyph {
    uint32_t codepoint = 0;
    uint16_t pageX = 0, pageY = 0;      // 页内像素位（含扩边后的位图左上）
    uint16_t width = 0, height = 0;     // 位图尺寸（含描边扩边）
    int16_t bearingX = 0, bearingY = 0; // 相对基线（px；bearingY = 基线上方为正）
    uint16_t advance = 0;               // 步进（px）
    uint16_t _pad = 0;
};
static_assert(sizeof(FontGlyph) == 20, "LBF1 字形行定长 20B");

/// .baked 头（Peek 只读这 32B；画幅与行框随头走）
struct BakedFontInfo {
    uint64_t paramsHash = 0;
    uint32_t pageW = 0, pageH = 0;
    int32_t ascender = 0, descender = 0; // 行框（px；descender ≤ 0 惯例）
    uint32_t glyphCount = 0;
};

inline constexpr char kFontBakeMagic[4] = {'L', 'B', 'F', '1'}; // Lemon Baked Font v1

/// UTF-8 → codepoint 序列（烘焙字符集与 BitmapFont 运行时寻址共用；容错语义
/// 见实现注记——非法/截断序列弃字符继续，不产 U+FFFD）。返回 false = 零有效字符
bool DecodeUtf8(const std::string& s, std::vector<uint32_t>& out);

/// TTF/OTF → .baked（FreeType 光栅 + 描边膨胀 + shelf 装箱 + 度量表 + RGBA 页）
/// 失败红字返回 false（文件不可开/无字形/页超限截断仍成功——截断字符集告警）。
bool BakeFontFile(const char* srcTtf, const char* dstBaked, const FontBakeParams& p);

/// 整读 .baked（magic/版本校验；装载侧唯一入口：头 + 度量表 + RGBA 页位图）
bool LoadBakedFont(const char* bakedPath, BakedFontInfo& info,
                   std::vector<FontGlyph>& glyphs, std::vector<uint8_t>& rgba);

/// 增量重烘判定（音频 BakeStale 同款三方 mtime + paramsHash 头对比——字符集/
/// 字号/描边变更 mtime 不动，靠头 hash 抓）；无产物/坏产物 = true（重烘）
bool BakeFontStale(const char* srcTtf, const char* dstBaked, uint64_t paramsHash);

/// 产物落位（AudioMount::BakedPath 同款口径）：<root>/.lemon/baked/fonts/<hex>.baked
std::string FontBakedPath(const std::string& projectRoot, uint64_t guid);

} // namespace lemon::assets
