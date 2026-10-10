// Lemon 引擎 — 运行时只读资产索引（M7a 批②；ADR-016 M2 / 06 §2）
// 编辑器 AssetDatabase（写侧 DB：发号/写 .meta/manifest 落盘）的只读镜像——
// 两侧共同事实源 = .meta（guid 随文件走）与 .lemon/manifest.json（spriteId 记账），
// 故无漂移；双扫描器合并归 M8（ADR-016 登记项）。
//   * 快路径：manifest 在场且合法 → 直读 {guid,type,spriteId,slice}，零目录扫描
//    （收 manifest 全账——含编辑器侧索引的根级 Generic 散件，如 README.md；
//     与回退的集合差无消费者：hooks/四缓存/TextureStore 只认强类型资产）；
//   * 回退：manifest 缺失/损坏 → 扫 Assets/** + 根级 Prefabs/**（06 §1 布局，
//     与 AssetDatabase.Rescan 同款排除规则）+ .meta 读 guid + **确定性 spriteId
//     派生**（路径排序单调发号 + 切片连号块）——「git clean -xfd 后可启动」的
//     机器保证（M7a 出口判据）。
//   * guid 是真源、spriteId 是进程内派生号：runtime 与编辑器 id 数值不同无害，
//     装载后 ResolveSpriteRefs 按 guid 归一（M6a 批⓪ 口径）。
// 零写侧：不发号、不写 .meta/manifest（打包/编辑态操作归编辑器与 packager）。
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "Assets/AssetTypes.h"
#include "Assets/FontBake.h" // M7c 批①：FontBakeParams（IndexedEntry::FontBake 打包）

namespace lemon::assets {

/// 只读条目（AssetEntry 的运行时子集：无 hash/missing/audio importer 等编辑器字段）
struct IndexedEntry {
    uint64_t guid = 0;
    std::string relPath;   // 相对项目根（'/' 分隔，含扩展名）
    AssetType type = AssetType::Generic;
    uint32_t spriteId = 0; // Sprite：AtlasRegistry 稳定 id（0 = 非 sprite）
    // 网格切片（.meta importer 段声明；cell 序号 → sliceBase+cell 连号，行优先）
    uint16_t cellW = 0, cellH = 0;
    uint16_t gridCols = 0, gridRows = 0;
    uint32_t sliceBase = 0;
    uint32_t sliceCount = 0;
    // 音频 importer 段（M7a 批④；AudioMount 装载消费——loop 冻结在 .baked 头，
    // preload 决定流式分流。manifest 不载此字段：.meta 一次小 IO 读入，与 Sprite
    // 的网格声明同款口径）
    float audioLoopStart = 0.0f, audioLoopEnd = 0.0f; // 秒；0/0 = 全曲循环
    bool audioPreload = false;                         // 显式整载（默认 >1MiB 流式）
    // 听感覆写三件（M7c 批②，preload 同款注册期消费；哨兵 = 继承全局）
    float audioRetriggerCd = -1.0f; // <0 = 继承；0 = 该 clip 关节流
    int32_t audioVoiceCap = 0;      // 0 = 继承；1 = 此声永不叠发
    float audioPitchJitter = -1.0f; // <0 = 继承；0 = 该 clip 关微扰
    // 字体 importer 段（M7c 批①；packager 烤制消费——paramsHash 冻结在 .baked 头）
    uint16_t fontPx = 24;
    uint8_t fontOutlinePx = 0;
    uint32_t fontOutlineColor = 0xFF202020u;
    std::string fontCharset; // 空 = ASCII 95 默认

    /// 字体烘焙参数（FontBake/packager 消费）
    FontBakeParams FontBake() const {
        FontBakeParams p;
        p.fontSizePx = fontPx;
        p.charset = fontCharset;
        p.outlinePx = fontOutlinePx;
        p.outlineColor = fontOutlineColor;
        return p;
    }
    bool Sliced() const { return sliceBase != 0 && sliceCount != 0; }
    uint32_t SliceSpriteId(uint32_t cell) const {
        return cell < sliceCount ? sliceBase + cell : 0;
    }
    std::string FileName() const;
    std::string Dir() const;
};

class AssetIndex {
public:
    /// 打开项目（root 含 Assets/）。spriteIdBase = 程序化图集之后首个可用号
    ///（调用方装配后 SpriteCount()+1，与 AssetDatabase::OpenProject 同口径）。
    /// 返回 false = root 无 Assets 目录（不可用）。
    bool Open(const std::string& projectRoot, uint32_t spriteIdBase);

    const IndexedEntry* FindByGuid(uint64_t guid) const; // O(1)（批⑪ #M16 哈希表）
    const IndexedEntry* FindByPath(const std::string& relPath) const;
    /// 全幅号 ∪ 切片区间（资产反查；SpriteRefSource 回填面**不可**用此口——
    /// cell 号命中会把切片引用升级成整图 guid，本体号专用查询见下）
    const IndexedEntry* FindBySpriteId(uint32_t spriteId) const;
    /// 仅本体号（SpriteRefs 回填契约：cell 号/程序化页号查无 = 不回填）。
    /// O(1)（批⑪ #M16——ResolveSpriteRefs 每实体调用，原线性全扫）
    const IndexedEntry* FindByWholeSpriteId(uint32_t spriteId) const;
    /// clip/prefab/controller/table/animset 按 GUID 低 32 位反查（运行时映射约定，
    /// 03 §69 组件 schema 恒 uint32；碰撞 = 路径序先登记者）
    const IndexedEntry* FindByLowId(AssetType type, uint32_t lowId) const;
    bool SpriteIdRegistered(uint32_t spriteId) const;

    const std::vector<IndexedEntry>& Entries() const { return entries_; }
    const std::string& ProjectRoot() const { return root_; }
    std::string AssetsRoot() const { return root_ + "/Assets"; }
    std::string AbsolutePath(const IndexedEntry& e) const { return root_ + "/" + e.relPath; }
    uint32_t SpriteIdBase() const { return spriteIdBase_; }
    /// 上次 Open 是否走 manifest 快路径（冒烟/单测探针位）
    bool FromManifest() const { return fromManifest_; }

    /// 打包账导出（M7a 批⑤ packager 消费）：schema 与 LoadFromManifest 单源对齐
    ///（assets[{path,guid,type,spriteId,slice}] + nextSpriteId 号域上界）。写入
    /// `<root>/.lemon/manifest.pkg.json` 后，包形态运行时 Open 走 pkg 快路径。
    bool ExportManifest(const std::string& path) const;

private:
    bool LoadFromManifest(const std::string& manifestPath);
    void ScanFallback();

    std::string root_;
    std::vector<IndexedEntry> entries_; // relPath 升序
    std::unordered_map<std::string, uint32_t> byPath_; // relPath → entries_ 下标
    // 批⑪ #M16：装载期等值索引——FindByGuid/FindByWholeSpriteId 被每实体
    //（ResolveSpriteRefs——装载与每次换场）、每图集条目（AtlasStore）调用，
    // 数千资产 × 万级实体 = 千万次 u64 比较级装载卡顿。先登记者保留（emplace）
    // = 原线性版路径序语义。FindBySpriteId（切片区间版）无运行时热消费方不动。
    std::unordered_map<uint64_t, uint32_t> byGuid_;
    std::unordered_map<uint32_t, uint32_t> byWholeId_;
    uint32_t spriteIdBase_ = 0;
    bool fromManifest_ = false;
    bool opened_ = false;
};

} // namespace lemon::assets
