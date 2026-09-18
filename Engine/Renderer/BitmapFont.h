// Lemon 引擎 — 位图文本 v1（02 §7：HUD 数字/计数高频路径绝不动态栅格化）
// 内置 5×7 像素字模烘焙独立纹理页（bindless 独立槽 = 独立图集 → 与精灵天然分批）；
// 低频富文本走 ImGui（M4 编辑器）。TTF→图集离线生成器随资产管线（M5+）落地。
#pragma once

#include <cstdint>
#include <vector>

#include "Core/Math.h"
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

    /// 生成字体页并注册为独立图集（返回 false = 槽位已占用）
    bool Init(rhi::Device& device, AtlasRegistry& atlas, uint32_t bindlessSlot);

    /// 逐字符生成路径 A 实例包（追加进 out；层内连续，同帧多段文本同键自然合批）
    void DrawText(std::vector<SpritePacket>& out, const char* text, Vec2 pos, float charScale,
                  uint32_t colorBits, uint8_t layer = kDefaultLayer) const;

    float TextWidth(const char* text, float charScale) const;
    uint32_t AtlasSlot() const { return atlasSlot_; }

private:
    uint32_t atlasSlot_ = 0;
    uint32_t glyphSprites_[kCharCount] = {}; // spriteId 查表
};

} // namespace lemon::renderer
