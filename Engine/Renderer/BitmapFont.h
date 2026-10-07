// Lemon 引擎 — 位图文本 v1（02 §7：HUD 数字/计数高频路径绝不动态栅格化）
// 内置 5×7 像素字模烘焙独立纹理页（bindless 独立槽 = 独立图集 → 与精灵天然分批）；
// 低频富文本走 ImGui（M4 编辑器）。
// M7c 批① S1：TTF→图集离线烘焙落地（FontBake/ADR-015 烤制先例）——外部烘焙页
// 与内置页并存（外部优先、缺字形回退内置 5×7 + 红字一次，不静默豆腐块）；
// 运行时零 FreeType 零栅格化（红线不破）。
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "Core/Math.h"
#include "Assets/FontBake.h" // LBF1 装载（外部烘焙页 + 测试口）
#include "Renderer/Renderable.h"
#include "Renderer/RHI.h"

namespace lemon::renderer {

class AtlasRegistry;

class BitmapFont {
public:
    static constexpr uint32_t kFirstChar = 32;   // ' '
    static constexpr uint32_t kCharCount = 95;   // ASCII 32..126
    static constexpr uint32_t kGlyphW = 5, kGlyphH = 7;
    static constexpr uint32_t kCellW = 6, kCellH = 8; // 1px 间隔防渗色
    static constexpr uint8_t kDefaultLayer = 254;     // UI 层（压精灵与粒子）

    /// 生成内置字体页并注册为独立图集（返回 false = 槽位已占用）
    bool Init(rhi::Device& device, AtlasRegistry& atlas, uint32_t bindlessSlot);

    /// 装载外部烘焙页（LBF1：TTF 离线烘焙产物）。bindlessSlot 必须空闲。
    /// 字形产包走 UV 覆盖通道（kPktUvOverride）——不 AddSprite 登记 sprite 表，
    /// 与资产号段零交互：装载时点自由（项目打开后任意时刻可换页）。
    /// 返回 false = 产物不可读/槽位占用（调用方降级内置页继续跑）。
    bool LoadBaked(rhi::Device& device, AtlasRegistry& atlas, uint32_t bindlessSlot,
                   const char* bakedPath);
    bool HasBaked() const { return bakedSlot_ != 0; }
    uint32_t BakedSlot() const { return bakedSlot_ != 0 ? bakedSlot_ : 0; }
    /// 设备丢失重建（AtlasRegistry::Reset 清页后调用；path 留档重装载。
    /// 无外部页 = no-op 返回 true）
    bool RebuildBaked(rhi::Device& device, AtlasRegistry& atlas) {
        if (bakedPath_.empty()) return true;
        const uint32_t slot = bakedSlot_ != 0 ? bakedSlot_ : kDefaultBakedSlot;
        bakedSlot_ = 0;
        bakedGlyphs_.clear();
        return LoadBaked(device, atlas, slot, bakedPath_.c_str());
    }
    /// 外部页装载槽约定（两渲染壳同值）：尾部保留槽，资产段 2..254 永不冲突
    static constexpr uint32_t kDefaultBakedSlot = 255;
    /// 外部页态清空（换项目：页纹理释放 + 留档同清，防旧项目字体复活）
    void ClearBaked() {
        bakedSlot_ = 0;
        bakedPageW_ = bakedPageH_ = 0;
        bakedAscender_ = bakedDescender_ = 0;
        bakedPath_.clear();
        bakedGlyphs_.clear();
    }
    /// 外部页行高（px；无外部页 = 内置 kCellH）——飘字锚点/行距用
    float LineHeight(float scale = 1.0f) const;

    /// 逐字符生成路径 A 实例包（ASCII 快路径——内置页语义不变）
    void DrawText(std::vector<SpritePacket>& out, const char* text, Vec2 pos, float charScale,
                  uint32_t colorBits, uint8_t layer = kDefaultLayer) const;

    /// 主入口（M7c 批①）：UTF-8 文本 = 外部烘焙页优先，缺字形回退内置页
    /// （warn 一次）。pos = 行框左上角（世界 Y 向下，baseline = pos.y +
    /// ascender×scale）；scale = 世界像素缩放。池化通道
    /// （FxChannel/GameFx）与未来 HUD 数字页共用此口。
    void DrawTextEx(std::vector<SpritePacket>& out, const char* utf8, Vec2 pos, float scale,
                    uint32_t colorBits, uint8_t layer) const;

    float TextWidth(const char* text, float charScale) const;
    /// UTF-8 版测宽（外部页 advance 累计；缺字按内置 cell 计）
    float TextWidthEx(const char* utf8, float scale) const;
    uint32_t AtlasSlot() const { return atlasSlot_; }

    /// 测试口（M7c 批①）：仅装载度量与页几何（不建纹理、不登记图集）——
    /// DrawTextEx 的 UV 寻址与缺字回退分支 headless 直测；生产路径走 LoadBaked
    bool LoadBakedMetricsForTest(const char* bakedPath) {
        assets::BakedFontInfo info;
        std::vector<assets::FontGlyph> glyphs;
        std::vector<uint8_t> rgba;
        if (!assets::LoadBakedFont(bakedPath, info, glyphs, rgba)) return false;
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
        bakedSlot_ = kDefaultBakedSlot;
        bakedPageW_ = info.pageW;
        bakedPageH_ = info.pageH;
        bakedAscender_ = info.ascender;
        bakedDescender_ = info.descender;
        bakedPath_ = bakedPath;
        return true;
    }

private:
    /// 外部页字形行（几何 + 度量；装载期从 LBF1 度量表建立；产包按页几何
    /// 归一化 UV 走覆盖通道——sprite 表零登记）
    struct GlyphMeta {
        uint16_t advance = 0;
        int16_t bearingX = 0, bearingY = 0;
        uint16_t w = 0, h = 0;
        uint16_t pageX = 0, pageY = 0;
    };

    uint32_t atlasSlot_ = 0;
    uint32_t glyphSprites_[kCharCount] = {}; // spriteId 查表
    uint32_t bakedSlot_ = 0;                 // 外部烘焙页槽（0 = 无）
    uint32_t bakedPageW_ = 0, bakedPageH_ = 0;
    int32_t bakedAscender_ = 0, bakedDescender_ = 0;
    std::string bakedPath_;                  // 留档（设备丢失重建 / 换页判定）
    mutable bool fallbackWarned_ = false;    // 缺字形回退红字只响一次
    std::unordered_map<uint32_t, GlyphMeta> bakedGlyphs_;
};

} // namespace lemon::renderer
