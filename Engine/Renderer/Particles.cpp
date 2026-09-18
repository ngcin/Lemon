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
        p->blend = e.blend;
        p->filter = e.filter;
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
    // 计数桶分组：粒子层内 order 恒 0，桶序即稳定序 → O(n) 免排序（对比 std::sort 实测
    // 8 万粒子 5ms → 桶化后大幅下降；键数粒子场景 ≤ 个位数）
    struct KeySlot {
        SpriteBatchKey key;
        uint32_t spriteId;
        uint32_t count;
        uint32_t cursor;
    };
    KeySlot slots[kMaxParticleKeys];
    uint32_t slotCount = 0;

    const Rect view = Rect::FromCenterHalf(viewCenter, viewHalfW, viewHalfH).Expanded(64.0f);
    uint32_t visible = 0;
    // pass1：剔除 + 键计数（spriteId 线性缓存，粒子种类少）
    for (uint32_t i = 0; i < alive_; ++i) {
        const ParticleData& p = pool_[i];
        if (p.pos.x < view.min.x || p.pos.x > view.max.x || p.pos.y < view.min.y ||
            p.pos.y > view.max.y)
            continue;
        ++visible;
        uint32_t si = 0;
        while (si < slotCount &&
               !(slots[si].spriteId == p.spriteId && slots[si].key.blend == p.blend &&
                 slots[si].key.filter == p.filter && slots[si].key.layer == p.sortingLayer))
            ++si;
        if (si == slotCount) {
            LEMON_ASSERT(slotCount < kMaxParticleKeys, "particle batch keys exceed table");
            const SpriteInfo& spr = atlas.GetSprite(p.spriteId);
            slots[si].key = MakeBatchKey(spr.atlasIndex, (BlendKind)p.blend,
                                         (FilterKind)p.filter, p.sortingLayer);
            slots[si].spriteId = p.spriteId;
            slots[si].count = 0;
            ++slotCount;
        }
        ++slots[si].count;
    }

    // 前缀和定位每桶区间
    packets_.resize(visible);
    uint32_t offset = 0;
    for (uint32_t si = 0; si < slotCount; ++si) {
        slots[si].cursor = offset;
        offset += slots[si].count;
    }

    // pass2：组包写入各桶（桶内按池顺序，天然稳定）
    for (uint32_t i = 0; i < alive_; ++i) {
        const ParticleData& p = pool_[i];
        if (p.pos.x < view.min.x || p.pos.x > view.max.x || p.pos.y < view.min.y ||
            p.pos.y > view.max.y)
            continue;
        uint32_t si = 0;
        while (si + 1 < slotCount &&
               !(slots[si].spriteId == p.spriteId && slots[si].key.blend == p.blend &&
                 slots[si].key.filter == p.filter && slots[si].key.layer == p.sortingLayer))
            ++si;

        const float t = p.age / p.lifetime;
        const uint32_t ti = (uint32_t)(t * 256.0f);
        SpritePacket& out = packets_[slots[si].cursor++];
        out.sortKey = ((uint64_t)p.sortingLayer << 56) | ((slots[si].key.hash >> 45) & 0xFFFFull) << 40 |
                      (uint64_t)i;
        out.key = slots[si].key;
        out.spriteId = p.spriteId;
        out.colorBits = LerpColorPack(p.color0, p.color1, ti);
        out.flags = 0;
        out.posX = p.pos.x;
        out.posY = p.pos.y;
        out.rot = p.rot;
        // SpritePacket.scale 语义 = 渲染像素宽高（与 RenderableManager::Extract 一致）
        out.scaleX = math::Lerp(p.size0, p.size1, t);
        out.scaleY = out.scaleX;
    }
    return packets_;
}

} // namespace lemon::renderer
