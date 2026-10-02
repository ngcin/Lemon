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

void RenderableManager::SetAll(uint32_t id, uint32_t spriteId, uint32_t colorBits,
                               uint8_t layer, int16_t order, Vec2 pos, float rotRad,
                               Vec2 scale) {
    Entry& e = entries_[id - 1];
    LEMON_ASSERT(e.alive, "dead renderable");
    e.desc.spriteId = spriteId;
    e.desc.colorBits = colorBits;
    e.desc.sortingLayer = layer;
    e.desc.order = order;
    e.curPos = pos;
    e.curRot = rotRad;
    e.curScale = scale;
}

void RenderableManager::SnapPrev(uint32_t id) {
    Entry& e = entries_[id - 1];
    LEMON_ASSERT(e.alive, "dead renderable");
    e.prevPos = e.curPos;
    e.prevRot = e.curRot;
    e.prevScale = e.curScale;
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

    // ---- 帧内共享段（per (simVersion, alpha) 缓存，双视口复用）------------------
    // 有效性/插值/键归类/sortKey 与视口无关；第二视口免全量重算。键桶化同原方案：
    // 单遍生成 + 槽缓存；order 全零时桶内池序即稳定序，免排序。
    if (preparedSimVersion_ != simVersion_ || preparedAlpha_ != alpha || prepared_.empty()) {
        prepared_.clear();
        slotCount_ = 0;
        anyOrder_ = false;
        stats_.droppedSprites = 0;
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

            uint32_t si = 0;
            while (si < slotCount_ &&
                   !(slots_[si].key.blend == e.desc.blend && slots_[si].key.filter == e.desc.filter &&
                     slots_[si].key.layer == e.desc.sortingLayer &&
                     slots_[si].atlasIndex == spr.atlasIndex))
                ++si;
            if (si == slotCount_) {
                // 键表满：软丢弃（断言在 Release 不设防，超限写栈数组 = 越界）。可恢复动作 =
                // 降图集/混合/层组合数；丢弃数记 stats_.droppedSprites
                if (slotCount_ >= kMaxSpriteKeys) {
                    ++stats_.droppedSprites;
                    if (!dropWarned_) { // 只响一次：超限是场景级状态，逐帧刷屏无信息量
                        dropWarned_ = true;
                        LEMON_WARN("sprite key table full (%u combos) — extra sprites "
                                   "dropped from render (see stats.droppedSprites)",
                                   kMaxSpriteKeys);
                    }
                    continue;
                }
                slots_[si].key = MakeBatchKey(spr.atlasIndex, (BlendKind)e.desc.blend,
                                              (FilterKind)e.desc.filter, e.desc.sortingLayer);
                slots_[si].atlasIndex = spr.atlasIndex;
                slots_[si].count = 0;
                ++slotCount_;
            }
            if (e.desc.order != 0) anyOrder_ = true;

            Prepared pr;
            pr.p.sortKey = ((uint64_t)e.desc.sortingLayer << 56) |
                           ((slots_[si].key.hash >> 45) & 0xFFFFull) << 40 |
                           ((uint64_t)(uint16_t)e.desc.order << 24) | ((uint64_t)e.seq & 0xFFFFFFull);
            pr.p.key = slots_[si].key;
            pr.p.spriteId = e.desc.spriteId;
            pr.p.colorBits = e.desc.colorBits;
            pr.p.flags = e.desc.flags;
            pr.p.posX = pos.x;
            pr.p.posY = pos.y;
            pr.p.rot = rot;
            pr.p.scaleX = scale.x * spr.widthPx;
            pr.p.scaleY = scale.y * spr.heightPx;
            pr.radX = 0.5f * scale.x * spr.widthPx;
            pr.radY = 0.5f * scale.y * spr.heightPx;
            pr.slot = (uint8_t)si;
            prepared_.push_back(pr);
        }
        preparedSimVersion_ = simVersion_;
        preparedAlpha_ = alpha;
    }

    // ---- per-viewport 段：剔除 + 计数 + 纯搬运分桶 + 桶内补排 -------------------
    packets_.clear();
    stats_.culled = 0;
    slotOf_.clear();
    for (uint32_t si = 0; si < slotCount_; ++si) slots_[si].count = 0;
    for (const Prepared& pr : prepared_) {
        if (hasViewport_) {
            // 轴对齐时半宽/半高即紧界；带旋转的角点最远到半对角线——max 界对旋转
            // 矩形欠估，屏幕边缘旋转中的精灵会整帧消失。半对角线只是保守不多剔。
            float r = pr.p.rot != 0.0f ? std::sqrt(pr.radX * pr.radX + pr.radY * pr.radY)
                                       : std::max(pr.radX, pr.radY);
            if (pr.p.posX + r < viewport_.min.x || pr.p.posX - r > viewport_.max.x ||
                pr.p.posY + r < viewport_.min.y || pr.p.posY - r > viewport_.max.y) {
                ++stats_.culled;
                continue;
            }
        }
        ++slots_[pr.slot].count;
        packets_.push_back(pr.p);
        slotOf_.push_back(pr.slot);
    }

    // 桶序即绘制序（Bake 按连续同键段录制，下游无重排）：偏移须按（layer, hash）
    // 分配——首遇序会把先创建的高层整桶画到低层下面，违背排序契约
    uint8_t order[kMaxSpriteKeys];
    for (uint32_t si = 0; si < slotCount_; ++si) order[si] = (uint8_t)si;
    std::sort(order, order + slotCount_, [&](uint8_t a, uint8_t b) {
        const KeySlot& sa = slots_[a];
        const KeySlot& sb = slots_[b];
        return sa.key.layer != sb.key.layer ? sa.key.layer < sb.key.layer
                                            : sa.key.hash < sb.key.hash;
    });

    // 纯搬运分桶（不重算剔除/插值；slots 原地不动，slotOf_ 存的槽下标仍有效）
    uint32_t offset = 0;
    for (uint32_t k = 0; k < slotCount_; ++k) {
        slots_[order[k]].cursor = offset;
        offset += slots_[order[k]].count;
    }
    staging_.resize(packets_.size());
    for (uint32_t i = 0; i < packets_.size(); ++i)
        staging_[slots_[slotOf_[i]].cursor++] = packets_[i];
    packets_.swap(staging_);

    // order 语义存在时桶内补排；全零快路径跳过
    if (anyOrder_) {
        for (uint32_t si = 0; si < slotCount_; ++si) {
            uint32_t s0 = slots_[si].cursor - slots_[si].count;
            // stable（#45）：sortKey 末 24 位装创建序——16.7M 次创建后回绕产生
            // 同键并列，非稳定 sort 帧间可变（alpha 混合闪跳）；stable 并列按池序
            std::stable_sort(packets_.begin() + s0, packets_.begin() + slots_[si].cursor,
                             [](const SpritePacket& a, const SpritePacket& b) {
                                 return a.sortKey < b.sortKey;
                             });
        }
    }

    stats_.visible = (uint32_t)packets_.size();
    builtSimVersion_ = simVersion_;
    builtViewportVersion_ = viewportVersion_;
    builtAlpha_ = alpha;
    return packets_;
}

} // namespace lemon::renderer
