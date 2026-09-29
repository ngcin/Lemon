// Lemon 引擎 — 图集运行时（02 §3.2）
// AtlasRegistry：纹理页(bindless 槽) + spriteId → UV 映射。
// 编辑器/打包期的 MaxRects 图集打包在 M6 资产管线落地；M1 提供运行时注册与程序化默认图集。
// 单图集放不下的超大 sprite 自动降级独立纹理页 —— 批键不同自然分批，无需特殊路径。
#pragma once

#include <cstdint>
#include <vector>

#include "Renderer/RHI.h"

namespace lemon::renderer {

/// 空洞哨兵：AddSpriteAt/SetSpriteAt 中间空洞占位用的 atlasIndex（渲染侧据此过滤）
inline constexpr uint32_t kHoleAtlasIdx = 0xFFFFFFFFu;

struct SpriteInfo {
    uint32_t atlasIndex = 0;   // bindless 纹理槽位
    float u0 = 0, v0 = 0, u1 = 1, v1 = 1; // 归一化 UV（左上/右下）
    uint16_t widthPx = 0, heightPx = 0;
    uint8_t pivotX = 50, pivotY = 50;      // 归一化轴心（0-100，Unity 心智）
};

/// 纯函数：像素矩形 → 归一化 UV（单测覆盖，编辑器打包器复用）
SpriteInfo MakeSpriteInfo(uint32_t atlasIndex, uint32_t atlasW, uint32_t atlasH, uint32_t px,
                          uint32_t py, uint32_t w, uint32_t h);

class AtlasRegistry {
public:
    /// 注册纹理页（返回 atlasIndex = bindless 槽位由调用方指定）
    void RegisterAtlas(uint32_t atlasIndex, rhi::Texture tex, uint32_t width, uint32_t height);
    /// 注销纹理页（导入回滚路径）：页上仍有登记 sprite 时断言拒绝——须先回滚登记
    void UnregisterAtlas(uint32_t atlasIndex);
    /// 热重导入：同槽位换纹理/改尺寸（M4.4 编辑器导入器）。全幅 sprite（uv 0..1，
    /// M4 最小集一页一 sprite）像素尺寸随之刷新；切片页 M5 图集打包器接管时重切。
    void UpdateAtlasPage(uint32_t atlasIndex, rhi::Texture tex, uint32_t width, uint32_t height);
    /// 在页内登记一块子纹理，返回全局 spriteId
    uint32_t AddSprite(uint32_t atlasIndex, uint32_t px, uint32_t py, uint32_t w, uint32_t h);
    /// 按调用方持久号显式登记（编辑器导入页：号 = manifest 记账，与扫描序无关；
    /// 中间空洞 = 退役号——资产删除只增不减）。id 0 / 已占用返回 false，调用方红字
    /// ——根治"注册表自增号与 DB 记账两本账漂移"（2026-09-21，指定 A 显示 B 的元凶）
    bool AddSpriteAt(uint32_t spriteId, uint32_t atlasIndex, uint32_t px, uint32_t py,
                     uint32_t w, uint32_t h);
    /// 覆盖式登记（M5 批③切片热重导）：同号重写几何，不查占用——号已由 manifest
    /// 记账（块归属确定），热改 grid/整页重切用；未知 atlasIndex 仍 assert。
    void SetSpriteAt(uint32_t spriteId, uint32_t atlasIndex, uint32_t px, uint32_t py,
                     uint32_t w, uint32_t h);
    /// 该号是否有效登记（0 / 越界 / 空洞 = false）。空洞页采样越界，渲染侧须先过滤。
    /// 头文件内联：提取热路径每精灵一调（bench-mow 10 万/帧），跨 TU 调用实测 -4% fps
    bool IsValidSprite(uint32_t spriteId) const {
        return spriteId != 0 && spriteId <= sprites_.size() &&
               sprites_[spriteId - 1].atlasIndex != kHoleAtlasIdx;
    }
    const SpriteInfo& GetSprite(uint32_t spriteId) const;
    uint32_t SpriteCount() const { return (uint32_t)sprites_.size(); }
    uint32_t AtlasCount() const { return (uint32_t)atlases_.size(); }
    /// 图集页查询（M6b 批③b UI 贴图桥：页纹理 + 尺寸；未知 index = 无效纹理）。
    /// 外部消费者只借用（RmlUi 后端按 view 包装，不持有 image）
    rhi::Texture AtlasTexture(uint32_t atlasIndex, uint32_t& w, uint32_t& h) const;
    /// 设备丢失重建前清空（纹理句柄已失效；spriteId 由调用方按相同顺序重建恢复稳定）
    void Reset();

    /// 程序化默认图集（引擎内置测试/占位素材：柠檬点/光晕/实心点/方块/环）
    /// 生成 512×512 纹理注册到 bindless 槽 defaultSlot，返回登记的首个 spriteId 集合（见实现）
    struct DefaultSprites {
        uint32_t lemon64, glow128, dotWhite16, dotRed16, squareWhite16, ring32;
    };
    static DefaultSprites CreateDefaultAtlas(rhi::Device& device, AtlasRegistry& registry,
                                             uint32_t defaultSlot);

private:
    struct AtlasPage {
        uint32_t atlasIndex;
        rhi::Texture tex;
        uint32_t width, height;
    };
    std::vector<AtlasPage> atlases_;
    std::vector<SpriteInfo> sprites_; // spriteId = 下标+1
};

} // namespace lemon::renderer
