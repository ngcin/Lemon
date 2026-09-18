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
    slotOf_.clear();

    // 键桶化（与 ParticleSystem 同方案）：单遍生成 + 槽缓存 + 纯搬运分桶（O(n)，
    // 一遍计算 + 一遍 56B 搬运）；order 全零时桶内池序即稳定序，免排序
    struct KeySlot {
        SpriteBatchKey key;
        uint32_t atlasIndex;
        uint32_t count, cursor;
    };
    KeySlot slots[kMaxSpriteKeys];
    uint32_t slotCount = 0;
    bool anyOrder = false;

    for (auto& e : entries_) {
        if (!e.alive) continue;
        const SpriteInfo& spr = atlas.GetSprite(e.desc.spriteId);
        Vec2 pos = math::Lerp(e.prevPos, e.curPos, alpha);
        float rot = math::Lerp(e.prevRot, e.curRot, alpha);
        Vec2 scale = math::Lerp(e.prevScale, e.curScale, alpha);

        if (hasViewport_) {
            float radX = 0.5f * scale.x * spr.widthPx;
            float radY = 0.5f * scale.y * spr.heightPx;
            float r = std::max(radX, radY);
            if (pos.x + r < viewport_.min.x || pos.x - r > viewport_.max.x ||
                pos.y + r < viewport_.min.y || pos.y - r > viewport_.max.y) {
                ++stats_.culled;
                continue;
            }
        }

        uint32_t si = 0;
        while (si < slotCount &&
               !(slots[si].key.blend == e.desc.blend && slots[si].key.filter == e.desc.filter &&
                 slots[si].key.layer == e.desc.sortingLayer &&
                 slots[si].atlasIndex == spr.atlasIndex))
            ++si;
        if (si == slotCount) {
            LEMON_ASSERT(slotCount < kMaxSpriteKeys, "sprite batch keys exceed table");
            slots[si].key = MakeBatchKey(spr.atlasIndex, (BlendKind)e.desc.blend,
                                         (FilterKind)e.desc.filter, e.desc.sortingLayer);
            slots[si].atlasIndex = spr.atlasIndex;
            slots[si].count = 0;
            ++slotCount;
        }
        ++slots[si].count;
        if (e.desc.order != 0) anyOrder = true;

        SpritePacket p;
        p.sortKey = ((uint64_t)e.desc.sortingLayer << 56) |
                    ((slots[si].key.hash >> 45) & 0xFFFFull) << 40 |
                    ((uint64_t)(uint16_t)e.desc.order << 24) | ((uint64_t)e.seq & 0xFFFFFFull);
        p.key = slots[si].key;
        p.spriteId = e.desc.spriteId;
        p.colorBits = e.desc.colorBits;
        p.flags = e.desc.flags;
        p.posX = pos.x;
        p.posY = pos.y;
        p.rot = rot;
        p.scaleX = scale.x * spr.widthPx;
        p.scaleY = scale.y * spr.heightPx;
        packets_.push_back(p);
        slotOf_.push_back((uint8_t)si);
    }

    // 纯搬运分桶（不重算剔除/插值）
    uint32_t visible = (uint32_t)packets_.size();
    uint32_t offset = 0;
    for (uint32_t si = 0; si < slotCount; ++si) {
        slots[si].cursor = offset;
        offset += slots[si].count;
    }
    staging_.resize(visible);
    for (uint32_t i = 0; i < visible; ++i)
        staging_[slots[slotOf_[i]].cursor++] = packets_[i];
    packets_.swap(staging_);

    // order 语义存在时桶内补排；全零快路径跳过
    if (anyOrder) {
        for (uint32_t si = 0; si < slotCount; ++si) {
            uint32_t s0 = slots[si].cursor - slots[si].count;
            std::sort(packets_.begin() + s0, packets_.begin() + slots[si].cursor,
                      [](const SpritePacket& a, const SpritePacket& b) { return a.sortKey < b.sortKey; });
        }
    }

    stats_.visible = (uint32_t)packets_.size();
    builtSimVersion_ = simVersion_;
    builtViewportVersion_ = viewportVersion_;
    builtAlpha_ = alpha;
    return packets_;
}

} // namespace lemon::renderer
