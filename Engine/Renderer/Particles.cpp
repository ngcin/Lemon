#include "Renderer/Particles.h"

#include <algorithm>

#include "Core/Log.h"
#include "Renderer/Atlas.h"

namespace lemon::renderer {

namespace {
inline uint32_t Xorshift(uint64_t& s) {
    s ^= s << 13;
    s ^= s >> 7;
    s ^= s << 17;
    return (uint32_t)(s >> 32);
}
inline float Frand(uint64_t& s) { return (float)(Xorshift(s) >> 8) / 16777216.0f; }

/// rgba8 打包色整数插值（ti = t*256 定点）：R/B 与 G/A 两组并算
inline uint32_t LerpColorPack(uint32_t a, uint32_t b, uint32_t ti) {
    uint32_t a0 = a & 0x00FF00FFu, b0 = b & 0x00FF00FFu;           // R、B
    uint32_t a1 = (a >> 8) & 0x00FF00FFu, b1 = (b >> 8) & 0x00FF00FFu; // G、A
    uint32_t r0 = a0 + (((b0 - a0) * ti) >> 8);
    uint32_t r1 = a1 + (((b1 - a1) * ti) >> 8);
    return (r0 & 0x00FF00FFu) | ((r1 & 0x00FF00FFu) << 8);
}
} // namespace

ParticleData* ParticleSystem::Alloc() {
    if (alive_ >= budget_) return nullptr;
    if (alive_ >= pool_.size()) pool_.emplace_back();
    return &pool_[alive_++];
}

void ParticleSystem::Emit(const EmitterConfig& e, float dt, uint64_t streamSeed, float& accum) {
    uint64_t rng = streamSeed * 6364136223846793005ull + 1442695040888963407ull;
    Xorshift(rng);
    // 批键位宽防御（与 RenderableManager::Create 同款）：blend/filter 为数据驱动
    // uint8_t，越界值经 SpritePacket 直达 SpriteBatcher 固定数组（4 管线/2 采样器）
    uint8_t blend = e.blend, filter = e.filter;
    if (blend > (uint8_t)BlendKind::Multiply || filter > (uint8_t)FilterKind::Point) {
        if (!warnedSanitize_) {
            warnedSanitize_ = true;
            LEMON_WARN("emitter blend/filter out of range (blend=%u filter=%u) — clamped",
                       blend, filter);
        }
        blend &= 3u;
        filter &= 1u;
    }
    accum += e.rate * dt;
    uint32_t n = (uint32_t)accum;
    accum -= (float)n;
    for (uint32_t i = 0; i < n; ++i) {
        ParticleData* p = Alloc();
        if (!p) {
            stats_.droppedFull += n - i;
            return;
        }
        float angle = e.angleMin + Frand(rng) * (e.angleMax - e.angleMin);
        float speed = e.speedMin + Frand(rng) * (e.speedMax - e.speedMin);
        p->pos = e.pos;
        p->vel = {std::cos(angle) * speed, std::sin(angle) * speed};
        p->age = 0;
        p->lifetime = e.lifetimeMin + Frand(rng) * (e.lifetimeMax - e.lifetimeMin);
        p->color0 = e.color0;
        p->color1 = e.color1;
        p->size0 = e.sizeMin + Frand(rng) * (e.sizeMax - e.sizeMin);
        p->size1 = p->size0 * (0.1f + 0.4f * Frand(rng)); // 收缩趋势
        p->rot = Frand(rng) * math::kTau;
        p->rotSpeed = (Frand(rng) * 2.0f - 1.0f) * e.rotSpeedMax;
        p->drag = e.drag;
        p->spriteId = e.spriteId;
        p->blend = blend;
        p->filter = filter;
        p->sortingLayer = e.sortingLayer;
        ++stats_.emittedThisTick;
    }
}

void ParticleSystem::Simulate(Vec2 gravity, float dt) {
    uint32_t i = 0;
    while (i < alive_) {
        ParticleData& p = pool_[i];
        p.age += dt;
        if (p.age >= p.lifetime) {
            pool_[i] = pool_[alive_ - 1]; // swap-and-pop O(1) 回收
            --alive_;
            ++stats_.diedThisTick;
            continue;
        }
        p.vel += gravity * dt;
        p.vel = p.vel * std::max(0.0f, 1.0f - p.drag * dt);
        p.pos += p.vel * dt;
        p.rot += p.rotSpeed * dt;
        ++i;
    }
}

std::span<const SpritePacket> ParticleSystem::Extract(const AtlasRegistry& atlas,
                                                      Vec2 viewCenter, float viewHalfW,
                                                      float viewHalfH) {
    // 计数桶分组（单遍生成 + 槽缓存 + 纯搬运分桶，与 RenderableManager 同方案）：
    // 桶间按（layer, hash）排、桶内池序即稳定序（层内 order 恒 0）→ O(n) 免逐包排序
    struct KeySlot {
        SpriteBatchKey key;
        uint32_t spriteId;
        uint32_t count, cursor;
    };
    KeySlot slots[kMaxParticleKeys];
    uint32_t slotCount = 0;

    const Rect view = Rect::FromCenterHalf(viewCenter, viewHalfW, viewHalfH).Expanded(64.0f);
    packets_.clear();
    slotOf_.clear();
    stats_.droppedParticles = 0;

    for (uint32_t i = 0; i < alive_; ++i) {
        const ParticleData& p = pool_[i];
        if (p.pos.x < view.min.x || p.pos.x > view.max.x || p.pos.y < view.min.y ||
            p.pos.y > view.max.y)
            continue;
        // spriteId 无效（未设默认 0/越界/空洞退役号）= 不渲染，与 RenderableManager
        // 同语义（Unity Sprite=None）；EmitterConfig 默认 0，无此过滤 GetSprite(0) 必断言
        if (!atlas.IsValidSprite(p.spriteId)) continue;
        uint32_t si = 0;
        while (si < slotCount &&
               !(slots[si].spriteId == p.spriteId && slots[si].key.blend == p.blend &&
                 slots[si].key.filter == p.filter && slots[si].key.layer == p.sortingLayer))
            ++si;
        if (si == slotCount) {
            // 键表满：软丢弃（断言在 Release 不设防，超限写栈数组 = 越界）；
            // 丢弃数记 stats_.droppedParticles
            if (slotCount >= kMaxParticleKeys) {
                ++stats_.droppedParticles;
                continue;
            }
            const SpriteInfo& spr = atlas.GetSprite(p.spriteId);
            slots[si].key = MakeBatchKey(spr.atlasIndex, (BlendKind)p.blend,
                                         (FilterKind)p.filter, p.sortingLayer);
            slots[si].spriteId = p.spriteId;
            slots[si].count = 0;
            ++slotCount;
        }
        ++slots[si].count;

        const float t = p.age / p.lifetime;
        SpritePacket out;
        out.sortKey = ((uint64_t)p.sortingLayer << 56) |
                      ((slots[si].key.hash >> 45) & 0xFFFFull) << 40 | (uint64_t)i;
        out.key = slots[si].key;
        out.spriteId = p.spriteId;
        out.colorBits = LerpColorPack(p.color0, p.color1, (uint32_t)(t * 256.0f));
        out.flags = 0;
        out.posX = p.pos.x;
        out.posY = p.pos.y;
        out.rot = p.rot;
        // SpritePacket.scale 语义 = 渲染像素宽高（与 RenderableManager::Extract 一致）
        out.scaleX = math::Lerp(p.size0, p.size1, t);
        out.scaleY = out.scaleX;
        packets_.push_back(out);
        slotOf_.push_back((uint8_t)si);
    }

    // 桶序即绘制序（Bake 按连续同键段录制，下游无重排）：偏移按（layer, hash）
    // 分配——首遇序会把后遇的低层整桶画到高层下面
    uint8_t order[kMaxParticleKeys];
    for (uint32_t si = 0; si < slotCount; ++si) order[si] = (uint8_t)si;
    std::sort(order, order + slotCount, [&](uint8_t a, uint8_t b) {
        const KeySlot& sa = slots[a];
        const KeySlot& sb = slots[b];
        return sa.key.layer != sb.key.layer ? sa.key.layer < sb.key.layer
                                            : sa.key.hash < sb.key.hash;
    });

    // 纯搬运分桶（slots 原地不动，slotOf_ 存的槽下标仍有效）
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
    return packets_;
}

} // namespace lemon::renderer
