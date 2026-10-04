// Lemon 引擎 — AtlasStore 实现（M7a 批⑥；见 AtlasStore.h 契约注记）
#include "Assets/AtlasStore.h"

#include <unordered_set>

#include "Assets/TextureStore.h" // RegisterGridSlices（切片登记共用件）
#include "Core/Log.h"

namespace lemon::assets {

bool RegisterAtlasSprites(renderer::AtlasRegistry& atlas, const AssetIndex& index,
                          const BakedAtlasBuild& build, uint32_t firstSlot,
                          uint32_t& outRegistered) {
    outRegistered = 0;
    if (firstSlot + build.pages.size() > rhi::kMaxTextureSlots) {
        LEMON_ERROR("图集装载：页数 %zu 超 bindless 槽容量（首槽 %u 上限 %u）",
                    build.pages.size(), firstSlot, rhi::kMaxTextureSlots);
        return false;
    }
    // 双向对账：LAT1 是包内精灵像素的唯一来源，条目数 ≠ 索引 Sprite 数 = 包与账
    // 失配（packager 同轮生成两侧，失配只可能来自手改包——红字整体拒载）
    uint32_t spriteEntries = 0;
    for (const IndexedEntry& e : index.Entries())
        if (e.type == AssetType::Sprite) ++spriteEntries;
    if (build.entries.size() != spriteEntries) {
        LEMON_ERROR("图集装载：LAT1 条目 %zu ≠ 索引精灵 %u——包数据失配（重出包）",
                    build.entries.size(), spriteEntries);
        return false;
    }
    std::unordered_set<uint64_t> seen;
    for (const BakedAtlasEntry& ent : build.entries) {
        const IndexedEntry* e = index.FindByGuid(ent.guid);
        if (!e || e->type != AssetType::Sprite) {
            LEMON_ERROR("图集装载：LAT1 guid %016llx 不在索引 Sprite 域——包数据失配",
                        (unsigned long long)ent.guid);
            return false;
        }
        if (!seen.insert(ent.guid).second) {
            LEMON_ERROR("图集装载：LAT1 guid %016llx 重复条目", (unsigned long long)ent.guid);
            return false;
        }
        const uint32_t slot = firstSlot + ent.page;
        if (!atlas.AddSpriteAt(e->spriteId, slot, ent.x, ent.y, ent.w, ent.h)) {
            LEMON_ERROR("图集装载：spriteId %u 登记失败（0/已占用）：%s——记账冲突",
                        e->spriteId, e->relPath.c_str());
            return false;
        }
        ++outRegistered;
        RegisterGridSlices(atlas, *e, slot, ent.x, ent.y, ent.w, ent.h);
    }
    return true;
}

void AtlasStore::Init(rhi::Device& device, renderer::AtlasRegistry* atlas,
                      const AssetIndex& index, uint32_t firstSlot) {
    device_ = &device;
    atlas_ = atlas;
    index_ = &index;
    firstSlot_ = firstSlot;
}

bool AtlasStore::Load(const std::string& path) {
    BakedAtlasBuild build;
    if (!LoadBakedAtlasFile(path, build)) return false; // 拒载原因已红字
    path_ = path;
    // 页上传 + 登记（槽 = firstSlot + 页序，设备丢失重建同序复号）
    for (size_t i = 0; i < build.pages.size(); ++i) {
        const BakedAtlasPage& pg = build.pages[i];
        const uint32_t slot = firstSlot_ + uint32_t(i);
        rhi::Texture tex = device_->CreateTexture(
            {.width = pg.w, .height = pg.h, .debugName = "bakedAtlas"});
        device_->UploadTexture(tex, build.pagePixels[i].data(), build.pagePixels[i].size());
        device_->BindTextureToSlot(tex, slot);
        atlas_->RegisterAtlas(slot, tex, pg.w, pg.h);
    }
    uint32_t registered = 0;
    if (!RegisterAtlasSprites(*atlas_, *index_, build, firstSlot_, registered)) return false;
    pageCount_ = uint32_t(build.pages.size());
    LEMON_LOG("图集装载：%s 页 %u 精灵 %u（槽 %u..%u）", path.c_str(), pageCount_, registered,
              firstSlot_, firstSlot_ + pageCount_ - 1);
    return true;
}

} // namespace lemon::assets
