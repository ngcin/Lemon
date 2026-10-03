// Lemon 引擎 — 运行时纹理装载器（M7a 批②；ADR-016 M2/R1「运行时零 PNG 解码
// 的引擎半边」——解码能力随 StbImage TU 入引擎，装载策略 = AssetGpuCache 的
// 运行时子集：无 watcher/无缩略图/无热重导/无 Evict——运行时资产面静态
//（进 Play 后文件不变，热重载是编辑器特权），一文件一页现状形态不变（图集
// .baked LAT1 归批⑥）。
// 消费者 = 批④ GameEntry 装配序列（Init → Open(AssetIndex) → LoadAll）。
#pragma once

#include <cstdint>
#include <vector>

#include "Assets/AssetIndex.h"
#include "Renderer/Atlas.h"
#include "Renderer/RHI.h"

namespace lemon::assets {

class TextureStore {
public:
    /// firstSlot：导入页起始 bindless 槽（运行时 0=程序化调色板 1=字体页 → 从 2 起；
    /// 调用方按自有程序化页数传）。index 引用须长于本缓存。
    void Init(rhi::Device& device, renderer::AtlasRegistry* atlas, const AssetIndex& index,
              uint32_t firstSlot);

    /// 全量装载：index 的 Sprite 条目按 spriteId 升序逐个解码导入（槽位序与号序
    /// 对齐——设备丢失重建同款约定）。返回成功页数（失败红字逐条，不中断）。
    uint32_t LoadAll();

    /// 单发导入（prefab 装载期发现的延迟资产等；失败红字 false）
    bool LoadSprite(const IndexedEntry& e);

    uint32_t PageCount() const { return (uint32_t)pages_.size(); }
    /// 导入页信息（冒烟断言：尺寸/在场）
    bool PageInfo(uint64_t guid, uint32_t& w, uint32_t& h) const;

private:
    struct Page {
        uint64_t guid;
        uint32_t slot;
        uint32_t spriteId;
        rhi::Texture tex{};
        uint32_t w = 0, h = 0;
    };
    Page* Find(uint64_t guid);
    const Page* Find(uint64_t guid) const;
    /// 网格切片登记（AssetGpuCache::RegisterSlices 同款：像素校验宁缺勿错，
    /// 行优先 cell → sliceBase+cell 连号覆盖式登记）
    void RegisterSlices(const IndexedEntry& e, uint32_t slot, uint32_t w, uint32_t h);

    rhi::Device* device_ = nullptr;
    renderer::AtlasRegistry* atlas_ = nullptr;
    const AssetIndex* index_ = nullptr;
    uint32_t firstSlot_ = 2;
    std::vector<Page> pages_; // spriteId 升序
};

} // namespace lemon::assets
