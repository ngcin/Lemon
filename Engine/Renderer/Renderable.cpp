#include "Renderer/Renderable.h"

#include <algorithm>

#include "Core/Log.h"
#include "Renderer/Atlas.h"

namespace lemon::renderer {

uint32_t RenderableManager::Create(const RenderableDesc& desc) {
    LEMON_ASSERT(desc.spriteId != 0, "renderable needs spriteId");
    // 批键位宽防御（blend:4 bit 可存 0–15，但 SpriteBatcher 只有 4 条管线/2 个采样器槽）：
    // 序列化数据驱动的越界值在此钳回合法域，否则 Record 端 pipelines_[blend] 越界读
    RenderableDesc sane = desc;
    if (sane.blend > (uint8_t)BlendKind::Multiply || sane.filter > (uint8_t)FilterKind::Point) {
        if (!warnedSanitize_) {
            warnedSanitize_ = true;
            LEMON_WARN("renderable desc blend/filter out of range (blend=%u filter=%u) — clamped",
                       sane.blend, sane.filter);
        }
        sane.blend &= 3u;
        sane.filter &= 1u;
    }
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
    e.desc = sane;
    e.alive = 1;
    e.seq = nextSeq_++;
    e.curScale = {1, 1};
    e.prevScale = {1, 1};
    ++alive_;
    ++simVersion_; // 提取缓存失效（暂停态无 BeginSimTick，新实体也须立即可见；与 Destroy 对齐）
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
    stats_.droppedSprites = 0;
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
        // spriteId 0/越界/空洞退役号 = 槽位无 sprite（Unity 语义 Sprite=None：合法存在，
        // 不渲染）——IsValidSprite 三态全检（与粒子路径同语义；仅查 SpriteCount 会放过
        // 落在界内的空洞号，atlasIndex=哨兵 → bindless 采样不存在槽）
        if (!atlas.IsValidSprite(e.desc.spriteId)) continue;
        const SpriteInfo& spr = atlas.GetSprite(e.desc.spriteId);
        Vec2 pos = math::Lerp(e.prevPos, e.curPos, alpha);
        float rot = math::Lerp(e.prevRot, e.curRot, alpha);
        Vec2 scale = math::Lerp(e.prevScale, e.curScale, alpha);

        if (hasViewport_) {
            float radX = 0.5f * scale.x * spr.widthPx;
            float radY = 0.5f * scale.y * spr.heightPx;
            // 轴对齐时半宽/半高即紧界；带旋转的角点最远到半对角线——max 界对旋转
            // 矩形欠估，屏幕边缘旋转中的精灵会整帧消失。半对角线只是保守不多剔。
            float r = rot != 0.0f ? std::sqrt(radX * radX + radY * radY)
                                  : std::max(radX, radY);
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
            // 键表满：软丢弃（断言在 Release 不设防，超限写栈数组 = 越界）。可恢复动作 =
            // 降图集/混合/层组合数；丢弃数记 stats_.droppedSprites
            if (slotCount >= kMaxSpriteKeys) {
                ++stats_.droppedSprites;
                continue;
            }
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

    // 桶序即绘制序（Bake 按连续同键段录制，下游无重排）：偏移须按（layer, hash）
    // 分配——首遇序会把先创建的高层整桶画到低层下面，违背排序契约
    uint8_t order[kMaxSpriteKeys];
    for (uint32_t si = 0; si < slotCount; ++si) order[si] = (uint8_t)si;
    std::sort(order, order + slotCount, [&](uint8_t a, uint8_t b) {
        const KeySlot& sa = slots[a];
        const KeySlot& sb = slots[b];
        return sa.key.layer != sb.key.layer ? sa.key.layer < sb.key.layer
                                            : sa.key.hash < sb.key.hash;
    });

    // 纯搬运分桶（不重算剔除/插值；slots 原地不动，slotOf_ 存的槽下标仍有效）
    uint32_t visible = (uint32_t)packets_.size();
    uint32_t offset = 0;
    for (uint32_t k = 0; k < slotCount; ++k) {
        slots[order[k]].cursor = offset;
        offset += slots[order[k]].count;
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
