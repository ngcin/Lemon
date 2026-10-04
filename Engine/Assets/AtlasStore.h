// Lemon 引擎 — LAT1 图集装载器（M7a 批⑥ reader 半边；ADR-016 M5 / AtlasBake.h
// 容器契约）。消费形态 = 包专用：GameEntry 探测 `<root>/.lemon/baked/atlas/
// atlas.baked` 在场即走本路径（sprite 源不入包，装载失败 = 包完整性事故响亮退出，
// 不回退 PNG 解码路径）；dev 形态（编辑器 Play/lemon-game 裸项目）维持
// TextureStore 一文件一页——编辑器零改动，金回放零影响（D4 默认 packager 专用）。
// 登记核 RegisterAtlasSprites 独立于 RHI（单测面）：LAT1 guid ↔ 索引 Sprite 双向
// 对账 + manifest 记账号整图登记 + .meta 网格切片子矩形派生。
#pragma once

#include <cstdint>
#include <string>

#include "Assets/AtlasBake.h"
#include "Renderer/Atlas.h"
#include "Renderer/RHI.h"

namespace lemon::assets {

/// 纯登记核：LAT1 条目按 index 记账号登记进注册表（slot = firstSlot + 页号；
/// 页须已 RegisterAtlas——尺寸/在场性由页登记面供给，单测以哑纹理注册）。
/// 双向对账：LAT1 guid ⊆ 索引 Sprite 且数量相等（缺 guid/类型不符/多出条目 =
/// 包与账失配，红字 false——fail-stop，不静默缺精灵）。成功 outRegistered =
/// 整图登记数（切片子矩形随 RegisterGridSlices 登记但不计数——整图数即
/// 「索引精灵全覆盖」的判据面）。
bool RegisterAtlasSprites(renderer::AtlasRegistry& atlas, const AssetIndex& index,
                          const BakedAtlasBuild& build, uint32_t firstSlot,
                          uint32_t& outRegistered);

class AtlasStore {
public:
    /// firstSlot = 导入页起始 bindless 槽（与 TextureStore 同约定：程序化页之后）。
    /// index 引用须长于本缓存。
    void Init(rhi::Device& device, renderer::AtlasRegistry* atlas, const AssetIndex& index,
              uint32_t firstSlot);

    /// 读 LAT1 → 页上传（CreateTexture/UploadTexture/BindTextureToSlot）→ 页登记 →
    /// RegisterAtlasSprites。任一步失败红字 false（调用方响亮退出）。
    bool Load(const std::string& path);

    /// 设备丢失重建（TextureStore::RebuildAll 同款时序契约：调用方已
    /// AtlasRegistry::Reset() 并重建程序化页，登记号已清空）。Load 失败 = 红字
    /// 不中断重建（与 TextureStore::LoadAll 同语义——重建期单点失败不弃整局；
    /// 包损坏场景在首次 Load 已被 GameEntry 响亮拦下，此处失败面 = 磁盘态突变）
    void RebuildAll(rhi::Device& device) {
        Init(device, atlas_, *index_, firstSlot_);
        Load(path_);
    }

    uint32_t PageCount() const { return pageCount_; }

private:
    rhi::Device* device_ = nullptr;
    renderer::AtlasRegistry* atlas_ = nullptr;
    const AssetIndex* index_ = nullptr;
    uint32_t firstSlot_ = 2;
    std::string path_;
    uint32_t pageCount_ = 0;
};

} // namespace lemon::assets
