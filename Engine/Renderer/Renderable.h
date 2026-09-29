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
    /// 模拟侧整包推送（desc 四字段 + 变换一次寻址写全）——大场提取热路径用
    /// （逐 setter 独立寻址 ×N 实体是 ExtractScene 的纯耗成分）
    void SetAll(uint32_t id, uint32_t spriteId, uint32_t colorBits, uint8_t layer,
                int16_t order, Vec2 pos, float rotRad, Vec2 scale);
    /// prev←cur 基线对齐（2026-09-29 复审 2b）：新建/换代实体首帧免从 Create 缺省
    /// (0,0) 原点插值拉丝——ExtractScene 建槽后紧随 SetAll 调用。编辑器专用语义，
    /// 引擎自驱路径（模拟侧逐 tick SetTransform）本就逐次落 prev，无需此口。
    void SnapPrev(uint32_t id);

    // --- 模拟侧 ---
    void BeginSimTick(); // prev ← cur 双缓冲翻转（未写实体的插值结果保持不变）
    void SetTransform(uint32_t id, Vec2 pos, float rotRad, Vec2 scale);

    // --- 渲染侧 ---
    void SetViewport(Vec2 center, float halfW, float halfH, float margin);
    void ClearViewport(); // 编辑器 SceneView：全量渲染（Luma 注释过的坑）

    /// 剔除 + 插值 + 排序；返回本帧可见包（视图在下次 Extract 前有效）。
    /// 帧内计算（有效性/插值/键归类）按 sim 版本 + alpha 缓存跨视口共享——
    /// 同帧第二视口（编辑器双视口）免全量重算，只做各自剔除+分桶（2026-09-26
    /// 渲染提取批；搬运序与字段值逐位不变，绘制输出同构）。
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

    // 帧内共享预计算（Extract 首次调用时构建；同 (simVersion, alpha) 的后续视口复用）
    struct Prepared {
        SpritePacket p; // 剔除无关字段全量填好（剔除后原样搬运）
        float radX = 0, radY = 0; // 剔除半宽/半高（含 sprite 尺寸；视口判定用）
        uint8_t slot = 0;         // 键槽索引（分桶用）
    };
    struct KeySlot {
        SpriteBatchKey key;
        uint32_t atlasIndex;
        uint32_t count, cursor;
    };
    std::vector<Prepared> prepared_;
    KeySlot slots_[kMaxSpriteKeys];
    uint32_t slotCount_ = 0;
    bool anyOrder_ = false;
    uint64_t preparedSimVersion_ = 0;
    float preparedAlpha_ = -1.0f;

    uint64_t builtSimVersion_ = 0;
    uint64_t builtViewportVersion_ = 0;
    float builtAlpha_ = -1.0f;
    Stats stats_;
    bool warnedSanitize_ = false; // desc 钳制告警只响一次
};

} // namespace lemon::renderer
