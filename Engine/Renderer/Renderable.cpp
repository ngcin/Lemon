#include "Renderer/Renderable.h"

#include <algorithm>

#include "Core/Log.h"
#include "Renderer/Atlas.h"

namespace lemon::renderer {

uint32_t RenderableManager::Create(const RenderableDesc& desc) {
    LEMON_ASSERT(desc.spriteId != 0, "renderable needs spriteId");
    uint32_t id;
    if (!free_.empty()) {
        id = free_.back();
        free_.pop_back();
    } else {
        entries_.emplace_back();
        id = (uint32_t)entries_.size();
    }
    Entry& e = entries_[id - 1];
    e = Entry{};
    e.desc = desc;
    e.alive = 1;
    e.seq = nextSeq_++;
    e.curScale = {1, 1};
    e.prevScale = {1, 1};
    ++alive_;
    return id;
}

void RenderableManager::Destroy(uint32_t id) {
    if (id == kInvalid || id > entries_.size()) return;
    Entry& e = entries_[id - 1];
    LEMON_ASSERT(e.alive, "double destroy");
    e.alive = 0;
    free_.push_back(id);
    --alive_;
    ++simVersion_; // 提取缓存失效
}

void RenderableManager::SetSprite(uint32_t id, uint32_t spriteId) {
    auto& e = entries_[id - 1];
    LEMON_ASSERT(e.alive, "dead renderable");
    e.desc.spriteId = spriteId;
}

void RenderableManager::SetColor(uint32_t id, uint32_t colorBits) {
    auto& e = entries_[id - 1];
    LEMON_ASSERT(e.alive, "dead renderable");
    e.desc.colorBits = colorBits;
}

void RenderableManager::SetSort(uint32_t id, uint8_t layer, int16_t order) {
    auto& e = entries_[id - 1];
    LEMON_ASSERT(e.alive, "dead renderable");
    e.desc.sortingLayer = layer;
    e.desc.order = order;
}

void RenderableManager::BeginSimTick() {
    for (auto& e : entries_)
        if (e.alive) {
            e.prevPos = e.curPos;
            e.prevRot = e.curRot;
            e.prevScale = e.curScale;
        }
    ++simVersion_;
}

void RenderableManager::SetTransform(uint32_t id, Vec2 pos, float rotRad, Vec2 scale) {
    Entry& e = entries_[id - 1];
    LEMON_ASSERT(e.alive, "dead renderable");
    e.curPos = pos;
    e.curRot = rotRad;
    e.curScale = scale;
}

void RenderableManager::SetViewport(Vec2 center, float halfW, float halfH, float margin) {
    viewport_ = Rect::FromCenterHalf(center, halfW, halfH).Expanded(margin);
    hasViewport_ = true;
    ++viewportVersion_;
}

void RenderableManager::ClearViewport() {
    hasViewport_ = false;
    ++viewportVersion_;
}

std::span<const SpritePacket> RenderableManager::Extract(const AtlasRegistry& atlas, float alpha) {
    // 缓存命中：模拟未动 + 视口未动 + 同 alpha（暂停/静止场景零成本，Luma 同款）
    if (builtSimVersion_ == simVersion_ && builtViewportVersion_ == viewportVersion_ &&
        builtAlpha_ == alpha && !packets_.empty()) {
        return packets_;
    }

    packets_.clear();
    stats_.culled = 0;
    for (auto& e : entries_) {
        if (!e.alive) continue;
        const SpriteInfo& spr = atlas.GetSprite(e.desc.spriteId);

        Vec2 pos = math::Lerp(e.prevPos, e.curPos, alpha);
        float rot = math::Lerp(e.prevRot, e.curRot, alpha);
        Vec2 scale = math::Lerp(e.prevScale, e.curScale, alpha);

        if (hasViewport_) {
            // 剔除：实例是缩放后的四边形，外接圆半径 = 0.5*len(scale*spriteSize)
            float radX = 0.5f * scale.x * spr.widthPx;
            float radY = 0.5f * scale.y * spr.heightPx;
            float r = std::max(radX, radY);
            if (pos.x + r < viewport_.min.x || pos.x - r > viewport_.max.x ||
                pos.y + r < viewport_.min.y || pos.y - r > viewport_.max.y) {
                ++stats_.culled;
                continue;
            }
        }

        SpriteBatchKey key = MakeBatchKey(spr.atlasIndex, (BlendKind)e.desc.blend,
                                          (FilterKind)e.desc.filter, e.desc.sortingLayer);
        // 排序全序：layer(8b) | keyHash 摘要(16b) | order(16b) | seq(24b)
        uint64_t kh = (key.hash >> 45) & 0xFFFFull; // 摘要取高位降低相邻碰撞
        uint64_t uOrder = (uint64_t)(uint16_t)e.desc.order;
        SpritePacket p;
        p.sortKey = ((uint64_t)e.desc.sortingLayer << 56) | (kh << 40) | (uOrder << 24) |
                    ((uint64_t)e.seq & 0xFFFFFFull);
        p.key = key;
        p.spriteId = e.desc.spriteId;
        p.colorBits = e.desc.colorBits;
        p.flags = e.desc.flags;
        p.posX = pos.x;
        p.posY = pos.y;
        p.rot = rot;
        p.scaleX = scale.x * spr.widthPx;
        p.scaleY = scale.y * spr.heightPx;
        packets_.push_back(p);
    }

    // 分组必须由完整 64 位批键哈希保证（摘要碰撞会让不同键交错 → 批数爆炸）；
    // 同键内按 sortKey（order → seq）稳定有序
    std::sort(packets_.begin(), packets_.end(), [](const SpritePacket& a, const SpritePacket& b) {
        if (a.key.layer != b.key.layer) return a.key.layer < b.key.layer;
        if (a.key.hash != b.key.hash) return a.key.hash < b.key.hash;
        return a.sortKey < b.sortKey;
    });

    stats_.visible = (uint32_t)packets_.size();
    builtSimVersion_ = simVersion_;
    builtViewportVersion_ = viewportVersion_;
    builtAlpha_ = alpha;
    return packets_;
}

} // namespace lemon::renderer
