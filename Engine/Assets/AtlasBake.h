// Lemon 引擎 — .baked 图集容器与烤制（M7a 批⑥；ADR-016 M5 LAT1 v1 / 06 §5）
// 家族口径（ADR-015 M2）：magic "L" + 类型字母 + 版本号；version 字段保升级通道；
// packager 单点消费（编辑器/运行时只读）；**运行时零解码**——页载荷 = RAW RGBA8
// 直传 UploadTexture（紧排行序，RHI 无 rowLength），启动判据优先于磁盘体积
//（压缩归 M7b 登记项）。
//   * 装箱：虚拟 4096 页 + shelf 行式（确定性排序 h desc → w desc → guid asc）；
//     精灵间 gutter 2px 透明（线性采样防渗色），页右/下裁剪到用到 extent（4px
//     对齐）——页边缘采样由 clamp-to-edge 兜底；超大精灵（>4096 任一边）专属页。
//   * 条目 = guid + 页号 + 像素矩形（整图本体，不含 gutter）：spriteId 记账在
//     manifest（号账）、LAT1 只存几何（几何账）——两账由 packager 同轮生成保证
//     一致；切片像素几何不入容器，装载期从 .meta 网格重派生（TextureStore 同款）。
//   * 落位 `<root>/.lemon/baked/atlas/atlas.baked`（单图集 v1；多图集组归后续）。
// 消费者：packager（BakeProjectAtlas 现烤入包）+ AtlasStore（装载登记）。
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Assets/AssetIndex.h"

namespace lemon::assets {

/// 包内相对落位（packager 写 / GameEntry 探测——两写一读共享常量防漂移）
inline constexpr const char* kBakedAtlasRelPath = ".lemon/baked/atlas/atlas.baked";

inline constexpr uint32_t kAtlasVirtualPage = 4096; ///< 虚拟装箱页宽高（参考值入头）
inline constexpr uint32_t kAtlasGutter = 2;         ///< 精灵间防渗色边距（px）
inline constexpr uint32_t kAtlasMaxImageDim = 16384; ///< GPU maxImageDimension2D 域

struct BakedAtlasEntry {
    uint64_t guid = 0;
    uint16_t page = 0;
    uint16_t x = 0, y = 0, w = 0, h = 0; // 页内像素矩形（精灵本体）
    bool operator==(const BakedAtlasEntry& o) const {
        return guid == o.guid && page == o.page && x == o.x && y == o.y && w == o.w && h == o.h;
    }
};

struct BakedAtlasPage {
    uint32_t w = 0, h = 0; // 裁剪后真实尺寸（4px 对齐；oversized = 精灵尺寸）
    bool operator==(const BakedAtlasPage& o) const { return w == o.w && h == o.h; }
};

/// 装箱输入（解码产物 / 单测合成面）
struct BakedAtlasImage {
    uint64_t guid = 0;
    uint32_t w = 0, h = 0;
    std::vector<uint8_t> rgba; // w×h×4，top-down（stb 解码行序）
};

struct BakedAtlasBuild {
    std::vector<BakedAtlasPage> pages;
    std::vector<BakedAtlasEntry> entries;      // 装箱放置序（确定性）
    std::vector<std::vector<uint8_t>> pagePixels; // 页序 RGBA（裁剪后尺寸 ×4）
};

/// shelf 装箱 + 页合成（纯函数，单测面）。确定性：同输入同输出字节。
/// 失败（errMsg 可空）：空输入 / 零尺寸 / 超 16384 / 像素尺寸不符 / 载荷超 u32 域。
bool PackAtlasPages(const std::vector<BakedAtlasImage>& images, BakedAtlasBuild& out,
                    std::string* errMsg = nullptr);

/// LAT1 写盘（32B 头 + 页尺寸表 + 条目表 + 页载荷；tmp + RenameReplace 原子写，
/// BakeAudioFile 同款）。域校验（页数 u16 / 载荷 u32）失败红字 false。
bool WriteBakedAtlasFile(const std::string& path, const BakedAtlasBuild& build);

/// LAT1 整读校验：魔数/版本/头长/计数域/页尺寸域/文件尺寸全等（截断/多出皆拒）/
/// 条目页号与矩形界内/guid 非零唯一/payloadBytes 64 位域对账（回绕防线，LBA1
/// review 先例）。失败 false（out 保证为空）。
bool LoadBakedAtlasFile(const std::string& path, BakedAtlasBuild& out);

struct AtlasBakeStats {
    uint32_t sprites = 0; // 索引内 Sprite 条目数（0 = 无图集项目，调用方跳过）
    uint32_t pages = 0;
    uint64_t bytes = 0;
};

/// 项目面烤制：index 的 Sprite 条目逐个 stb 解码 → Pack → 原子写 dst。
/// sprites==0 → false 且 stats.sprites=0（合法跳过形态，非错误）；解码/装箱/
/// 写盘失败红字 false。
bool BakeProjectAtlas(const AssetIndex& index, const std::string& dst, AtlasBakeStats& stats);

} // namespace lemon::assets
