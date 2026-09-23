// Lemon 引擎 — Renderable 管理器（02 §1 提取-双缓冲-插值，Luma RenderableManager 蓝本）
// 职责边界：Renderable 是渲染层最小单位（sprite/text/particle-chunk/line），不知道实体为何物。
//   * 模拟侧（60Hz 固定 tick）：BeginSimTick() 翻转双缓冲 → SetTransform/SetColor 写当前帧
//   * 渲染侧（每渲染帧）：SetViewport(世界矩形+margin) → Extract(alpha) 得到剔除+插值后的
//     有序 SpritePacket 视图（排序：SortingLayer → 批键 → order → seq，稳定）
// M2 的 ECS 提取系统把 Transform2D/SpriteRenderer 组件灌进本管理器；bench 直接驱动。
#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "Core/Math.h"

namespace lemon::renderer {

// ------------------------------------------------------------- 批键（02 §3.1）
enum class BlendKind : uint8_t { Opaque = 0, Alpha = 1, Additive = 2, Multiply = 3 };
enum class FilterKind : uint8_t { Linear = 0, Point = 1 };

struct SpriteBatchKey {
    uint64_t textureAtlas;  // bindless 图集槽
    uint32_t blend : 4;
    uint32_t filter : 2;
    uint32_t wrap : 2;      // M1 恒 Clamp；进键保语义完整
    uint32_t layer : 10;    // SortingLayer id（≤1024 层）
    uint32_t _pad : 14;
    uint64_t hash;          // 构造时预计算（boost hash_combine 0x9e3779b9）

    constexpr bool SameBatch(const SpriteBatchKey& r) const {
        return hash == r.hash && textureAtlas == r.textureAtlas && blend == r.blend &&
               filter == r.filter && wrap == r.wrap && layer == r.layer;
    }
};
static_assert(sizeof(SpriteBatchKey) == 24 && __is_trivially_copyable(SpriteBatchKey),
              "POD 批键 24B（02 §3.1）");

constexpr SpriteBatchKey MakeBatchKey(uint32_t atlasIndex, BlendKind blend, FilterKind filter,
                                      uint32_t layer) {
    SpriteBatchKey k{};
    k.textureAtlas = atlasIndex;
    k.blend = (uint32_t)blend;
    k.filter = (uint32_t)filter;
    k.wrap = 0;
    k.layer = layer;
    uint64_t h = 0x9e3779b97f4a7c15ull ^ (uint64_t)atlasIndex;
    h ^= (uint64_t)((uint32_t)blend | ((uint32_t)filter << 4) | (layer << 8)) + 0x9e3779b9 +
         (h << 6) + (h >> 2);
    k.hash = h;
    return k;
}

// ------------------------------------------------------------ 提取产物 ----
struct SpritePacket {
    uint64_t sortKey; // layer<<56 | keyHash 摘要<<40 | order<<24 | seq（稳定全序）
    SpriteBatchKey key;
    uint32_t spriteId;
    uint32_t colorBits;
    uint32_t flags;   // kInstFlipX/kInstFlipY
    float posX, posY; // 插值后世界坐标
    float rot;        // 弧度
    float scaleX, scaleY;
};

// --------------------------------------------------------- Renderable 管理 --
struct RenderableDesc {
    uint32_t spriteId = 0;
    uint32_t colorBits = 0xFFFFFFFFu; // 白
    uint8_t sortingLayer = 0;
    int16_t order = 0;
    uint8_t blend = (uint8_t)BlendKind::Alpha;
    uint8_t filter = (uint8_t)FilterKind::Linear;
    uint8_t flags = 0; // flipX/Y
};

class AtlasRegistry;

class RenderableManager {
public:
    static constexpr uint32_t kInvalid = 0;
    static constexpr uint32_t kMaxSpriteKeys = 64; // 键桶容量（精灵场景键数：图集×混合×层）

    uint32_t Create(const RenderableDesc& desc);
    void Destroy(uint32_t id);
    void SetSprite(uint32_t id, uint32_t spriteId);   // 动画换帧（AnimatorSystem 用）
    void SetColor(uint32_t id, uint32_t colorBits);
    void SetSort(uint32_t id, uint8_t layer, int16_t order);

    // --- 模拟侧 ---
    void BeginSimTick(); // prev ← cur 双缓冲翻转（未写实体的插值结果保持不变）
    void SetTransform(uint32_t id, Vec2 pos, float rotRad, Vec2 scale);

    // --- 渲染侧 ---
    void SetViewport(Vec2 center, float halfW, float halfH, float margin);
    void ClearViewport(); // 编辑器 SceneView：全量渲染（Luma 注释过的坑）

    /// 剔除 + 插值 + 排序；返回本帧可见包（视图在下次 Extract 前有效）
    std::span<const SpritePacket> Extract(const AtlasRegistry& atlas, float alpha);

    // 帧版本：sim 版本变化或视口变化时才可能复用缓存（Luma frameVersion/lastBuiltAlpha 思想）
    uint64_t SimFrameVersion() const { return simVersion_; }
    size_t AliveCount() const { return alive_; }

    struct Stats {
        uint32_t visible = 0;
        uint32_t culled = 0;
        uint32_t droppedSprites = 0; // 键表满（kMaxSpriteKeys）：超限键的精灵不渲染
    };
    const Stats& LastStats() const { return stats_; }

private:
    struct Entry {
        RenderableDesc desc;
        uint32_t alive = 0;
        Vec2 prevPos, curPos;
        float prevRot = 0, curRot = 0;
        Vec2 prevScale{1, 1}, curScale{1, 1};
        uint32_t seq = 0; // 稳定排序末位
    };
    std::vector<Entry> entries_;
    std::vector<uint32_t> free_;
    uint32_t alive_ = 0;
    uint32_t nextSeq_ = 0;
    uint64_t simVersion_ = 0;

    Rect viewport_{Vec2{0, 0}, Vec2{0, 0}};
    bool hasViewport_ = false;
    uint64_t viewportVersion_ = 0;

    std::vector<SpritePacket> packets_;  // 复用（提取段零分配）
    std::vector<SpritePacket> staging_; // 分桶搬运缓冲
    std::vector<uint8_t> slotOf_;       // 单遍生成时的槽索引缓存
    uint64_t builtSimVersion_ = 0;
    uint64_t builtViewportVersion_ = 0;
    float builtAlpha_ = -1.0f;
    Stats stats_;
    bool warnedSanitize_ = false; // desc 钳制告警只响一次
};

} // namespace lemon::renderer
