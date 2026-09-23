// Lemon 引擎 — 粒子系统（02 §6，Luma ParticleData 布局思想 + 路径 A 统一实例化）
// * CPU AoS 池，死亡 swap-and-pop O(1)；全局预算（质量分级：High 100k / Med 50k / Low 20k）
// * 模拟按渲染帧 dt（表现层语义，非确定可接受；不参与 03 §12 确定性回放）
// * 提取直接产出 SpritePacket 视图（与精灵共用 SpriteBatcher 合批；渲染零额外格式转换）
// * M1 单线程模拟；JobSystem 并行化（逐发射器 grain 256）在 M2 接入
#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "Core/Math.h"
#include "Renderer/Renderable.h"

namespace lemon::renderer {

struct ParticleData {
    Vec2 pos, vel;
    float age = 0, lifetime = 1;
    uint32_t color0 = 0xFFFFFFFFu, color1 = 0xFFFFFFFFu; // 起止色 rgba8
    float size0 = 8, size1 = 0;
    float rot = 0, rotSpeed = 0;
    float drag = 0.0f;
    uint32_t spriteId = 0;
    uint8_t blend = (uint8_t)BlendKind::Additive;
    uint8_t filter = (uint8_t)FilterKind::Linear;
    uint8_t sortingLayer = 250; // 粒子默认压精灵（02 §4 Game Pass 顺序）
    uint8_t _pad = 0;
};

/// 发射器配置（Unity Shuriken 风格子集；曲线资产化进 M5 编辑器）
struct EmitterConfig {
    Vec2 pos;
    float rate = 100.0f;                 // 每秒发射数
    float lifetimeMin = 0.5f, lifetimeMax = 1.5f;
    float speedMin = 50.0f, speedMax = 220.0f;
    float angleMin = 0.0f, angleMax = 6.2831853f; // 发射锥（弧度）
    float sizeMin = 6.0f, sizeMax = 14.0f;
    uint32_t color0 = 0xFFFFFFFFu, color1 = 0x80FFFFFFu;
    Vec2 gravity{0, 0};
    float drag = 0.6f;
    float rotSpeedMax = 2.0f;
    uint32_t spriteId = 0;
    uint8_t blend = (uint8_t)BlendKind::Additive;
    uint8_t filter = (uint8_t)FilterKind::Linear;
    uint8_t sortingLayer = 250;
};

class ParticleSystem {
public:
    static constexpr uint32_t kMaxParticleKeys = 32; // 计数桶分组容量（8 精灵×2 层=16 已可达，留一倍余量）
    /// 全局预算（存活上限）；池容量不缩，只钳发射
    void SetBudget(uint32_t maxParticles) { budget_ = maxParticles; }
    uint32_t Budget() const { return budget_; }
    uint32_t AliveCount() const { return alive_; }

    /// 按速率注入；accum 为该发射器的小数累积（调用方持有，每发射器一份）
    void Emit(const EmitterConfig& e, float dt, uint64_t streamSeed, float& accum);

    /// 闭式积分：重力/阻力/旋转；死亡 swap-and-pop
    void Simulate(Vec2 gravity, float dt);

    /// 剔除 + 组包（与精灵同构的 SpritePacket；内部复用缓冲）
    std::span<const SpritePacket> Extract(const AtlasRegistry& atlas, Vec2 viewCenter,
                                          float viewHalfW, float viewHalfH);

    struct Stats {
        uint32_t emittedThisTick = 0;
        uint32_t diedThisTick = 0;
        uint32_t droppedFull = 0;       // 池满丢弃
        uint32_t droppedParticles = 0;  // 键表满（kMaxParticleKeys）：超限键的粒子不渲染
    };
    const Stats& LastStats() const { return stats_; }

private:
    ParticleData* Alloc();
    std::vector<ParticleData> pool_;
    uint32_t alive_ = 0;
    uint32_t budget_ = 100000;
    std::vector<SpritePacket> packets_;
    std::vector<SpritePacket> staging_; // 分桶搬运缓冲
    std::vector<uint8_t> slotOf_;       // 单遍生成时的槽索引缓存
    Stats stats_;
};

} // namespace lemon::renderer
