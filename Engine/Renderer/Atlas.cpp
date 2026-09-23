#include "Renderer/Atlas.h"

#include <cmath>
#include <cstring>

#include "Core/Log.h"

namespace lemon::renderer {

namespace {
/// 空洞哨兵：atlasIndex 槽位恒非法（bindless 越界）——IsValidSprite 据此过滤退役号
constexpr uint32_t kHoleAtlasIdx = 0xFFFFFFFFu;
const SpriteInfo kHoleSprite = MakeSpriteInfo(kHoleAtlasIdx, 1, 1, 0, 0, 1, 1);
} // namespace

SpriteInfo MakeSpriteInfo(uint32_t atlasIndex, uint32_t atlasW, uint32_t atlasH, uint32_t px,
                          uint32_t py, uint32_t w, uint32_t h) {
    SpriteInfo s;
    s.atlasIndex = atlasIndex;
    s.u0 = (float)px / (float)atlasW;
    s.v0 = (float)py / (float)atlasH;
    s.u1 = (float)(px + w) / (float)atlasW;
    s.v1 = (float)(py + h) / (float)atlasH;
    s.widthPx = (uint16_t)w;
    s.heightPx = (uint16_t)h;
    return s;
}

void AtlasRegistry::Reset() {
    atlases_.clear();
    sprites_.clear();
}

void AtlasRegistry::RegisterAtlas(uint32_t atlasIndex, rhi::Texture tex, uint32_t width,
                                  uint32_t height) {
    for (auto& a : atlases_)
        LEMON_ASSERT(a.atlasIndex != atlasIndex, "atlas slot reused");
    atlases_.push_back({atlasIndex, tex, width, height});
}

void AtlasRegistry::UpdateAtlasPage(uint32_t atlasIndex, rhi::Texture tex, uint32_t width,
                                    uint32_t height) {
    for (auto& a : atlases_) {
        if (a.atlasIndex != atlasIndex) continue;
        a.tex = tex;
        a.width = width;
        a.height = height;
        for (auto& s : sprites_) {
            if (s.atlasIndex != atlasIndex) continue;
            if (s.u0 == 0.0f && s.v0 == 0.0f && s.u1 == 1.0f && s.v1 == 1.0f) {
                s.widthPx = (uint16_t)width; // 全幅 sprite：uv 不变，像素尺寸刷新
                s.heightPx = (uint16_t)height;
            }
            // 切片 sprite（uv 子矩形）：本调用不重切——调用方（AssetGpuCache 热重导，
            // M5 批③起）随后按新网格 SetSpriteAt 覆盖重登记
        }
        return;
    }
    LEMON_ASSERT(false, "unknown atlasIndex");
}

uint32_t AtlasRegistry::AddSprite(uint32_t atlasIndex, uint32_t px, uint32_t py, uint32_t w,
                                  uint32_t h) {
    for (auto& a : atlases_) {
        if (a.atlasIndex != atlasIndex) continue;
        LEMON_ASSERT(px + w <= a.width && py + h <= a.height, "sprite rect out of atlas");
        sprites_.push_back(MakeSpriteInfo(atlasIndex, a.width, a.height, px, py, w, h));
        return (uint32_t)sprites_.size();
    }
    LEMON_ASSERT(false, "unknown atlasIndex");
    return 0;
}

bool AtlasRegistry::AddSpriteAt(uint32_t spriteId, uint32_t atlasIndex, uint32_t px,
                                uint32_t py, uint32_t w, uint32_t h) {
    if (spriteId == 0) return false;
    if (spriteId <= sprites_.size()) {
        if (sprites_[spriteId - 1].atlasIndex != kHoleAtlasIdx) return false; // 已占用
    } else {
        // 中间空洞 = 退役号（资产删除只增不减）：哨兵占位，IsValidSprite 过滤
        sprites_.resize(spriteId, kHoleSprite);
    }
    for (auto& a : atlases_) {
        if (a.atlasIndex != atlasIndex) continue;
        LEMON_ASSERT(px + w <= a.width && py + h <= a.height, "sprite rect out of atlas");
        sprites_[spriteId - 1] = MakeSpriteInfo(atlasIndex, a.width, a.height, px, py, w, h);
        return true;
    }
    LEMON_ASSERT(false, "unknown atlasIndex");
    return false;
}

void AtlasRegistry::SetSpriteAt(uint32_t spriteId, uint32_t atlasIndex, uint32_t px,
                                uint32_t py, uint32_t w, uint32_t h) {
    LEMON_ASSERT(spriteId != 0, "spriteId 0 reserved");
    if (spriteId > sprites_.size()) sprites_.resize(spriteId, kHoleSprite);
    for (auto& a : atlases_) {
        if (a.atlasIndex != atlasIndex) continue;
        LEMON_ASSERT(px + w <= a.width && py + h <= a.height, "sprite rect out of atlas");
        sprites_[spriteId - 1] = MakeSpriteInfo(atlasIndex, a.width, a.height, px, py, w, h);
        return;
    }
    LEMON_ASSERT(false, "unknown atlasIndex");
}

bool AtlasRegistry::IsValidSprite(uint32_t spriteId) const {
    return spriteId != 0 && spriteId <= sprites_.size() &&
           sprites_[spriteId - 1].atlasIndex != kHoleAtlasIdx;
}

const SpriteInfo& AtlasRegistry::GetSprite(uint32_t spriteId) const {
    LEMON_ASSERT(spriteId != 0 && spriteId <= sprites_.size(), "bad spriteId");
    return sprites_[spriteId - 1];
}

// ------------------------------------------------------------ 默认图集 ----
namespace {
constexpr uint32_t kDefaultAtlasSize = 512;

void PutLemon(std::vector<uint8_t>& px, int ox, int oy, int size) {
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            float dx = ((float)x + 0.5f) / size - 0.5f;
            float dy = ((float)y + 0.5f) / size - 0.5f;
            float d = std::sqrt(dx * dx + dy * dy);
            uint8_t* p = &px[(((size_t)oy + y) * kDefaultAtlasSize + ((size_t)ox + x)) * 4];
            if (d < 0.46f) {
                float lit = std::max(0.0f, 1.0f - d / 0.46f);
                p[0] = (uint8_t)(250 - 40 * (1.0f - lit));
                p[1] = (uint8_t)(225 - 80 * (1.0f - lit));
                p[2] = (uint8_t)(30 + 60 * (1.0f - lit));
                p[3] = 255;
                if (d > 0.44f) p[3] = (uint8_t)(255 * (0.46f - d) / 0.02f);
            } else {
                p[0] = p[1] = p[2] = 0;
                p[3] = 0;
            }
        }
}

void PutGlow(std::vector<uint8_t>& px, int ox, int oy, int size) {
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            float dx = ((float)x + 0.5f) / size - 0.5f;
            float dy = ((float)y + 0.5f) / size - 0.5f;
            float d = std::sqrt(dx * dx + dy * dy) * 2.0f;
            uint8_t* p = &px[(((size_t)oy + y) * kDefaultAtlasSize + ((size_t)ox + x)) * 4];
            float a = std::max(0.0f, 1.0f - d);
            a = a * a * a; // 三次方衰减：中心亮、边缘柔
            p[0] = p[1] = p[2] = 255;
            p[3] = (uint8_t)(a * 255.0f);
        }
}

void PutSolidDot(std::vector<uint8_t>& px, int ox, int oy, int size, uint8_t r, uint8_t g,
                 uint8_t b) {
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            float dx = ((float)x + 0.5f) / size - 0.5f;
            float dy = ((float)y + 0.5f) / size - 0.5f;
            float d = std::sqrt(dx * dx + dy * dy);
            uint8_t* p = &px[(((size_t)oy + y) * kDefaultAtlasSize + ((size_t)ox + x)) * 4];
            float edge = std::max(0.0f, 1.0f - std::max(0.0f, d - 0.38f) / 0.12f);
            p[0] = r; p[1] = g; p[2] = b;
            p[3] = (uint8_t)(edge * 255.0f);
        }
}

void PutSquare(std::vector<uint8_t>& px, int ox, int oy, int size) {
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            uint8_t* p = &px[(((size_t)oy + y) * kDefaultAtlasSize + ((size_t)ox + x)) * 4];
            int m = std::min(std::min(x, y), std::min(size - 1 - x, size - 1 - y));
            uint8_t a = m < 1 ? 0 : 255;
            p[0] = p[1] = p[2] = 255;
            p[3] = a;
        }
}

void PutRing(std::vector<uint8_t>& px, int ox, int oy, int size) {
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            float dx = ((float)x + 0.5f) / size - 0.5f;
            float dy = ((float)y + 0.5f) / size - 0.5f;
            float d = std::sqrt(dx * dx + dy * dy);
            float ring = std::max(0.0f, 1.0f - std::fabs(d - 0.38f) / 0.08f);
            uint8_t* p = &px[(((size_t)oy + y) * kDefaultAtlasSize + ((size_t)ox + x)) * 4];
            p[0] = p[1] = p[2] = 255;
            p[3] = (uint8_t)(ring * 255.0f);
        }
}
} // namespace

AtlasRegistry::DefaultSprites AtlasRegistry::CreateDefaultAtlas(rhi::Device& device,
                                                                AtlasRegistry& registry,
                                                                uint32_t defaultSlot) {
    std::vector<uint8_t> px((size_t)kDefaultAtlasSize * kDefaultAtlasSize * 4, 0);
    // 布局：lemon64(0,0) glow128(64,0) ring32(192,0) dot16×2(224,0) square16(256,0)
    PutLemon(px, 0, 0, 64);
    PutGlow(px, 64, 0, 128);
    PutRing(px, 192, 0, 32);
    PutSolidDot(px, 224, 0, 16, 255, 255, 255);
    PutSolidDot(px, 240, 0, 16, 255, 80, 80);
    PutSquare(px, 256, 0, 16);

    rhi::Texture tex = device.CreateTexture(
        {.width = kDefaultAtlasSize, .height = kDefaultAtlasSize, .debugName = "defaultAtlas"});
    device.UploadTexture(tex, px.data(), px.size());
    device.BindTextureToSlot(tex, defaultSlot);
    registry.RegisterAtlas(defaultSlot, tex, kDefaultAtlasSize, kDefaultAtlasSize);

    DefaultSprites out;
    out.lemon64 = registry.AddSprite(defaultSlot, 0, 0, 64, 64);
    out.glow128 = registry.AddSprite(defaultSlot, 64, 0, 128, 128);
    out.ring32 = registry.AddSprite(defaultSlot, 192, 0, 32, 32);
    out.dotWhite16 = registry.AddSprite(defaultSlot, 224, 0, 16, 16);
    out.dotRed16 = registry.AddSprite(defaultSlot, 240, 0, 16, 16);
    out.squareWhite16 = registry.AddSprite(defaultSlot, 256, 0, 16, 16);
    return out;
}

} // namespace lemon::renderer
