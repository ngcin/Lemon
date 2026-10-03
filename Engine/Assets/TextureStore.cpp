// Lemon 引擎 — TextureStore 实现（M7a 批②；导入骨架自 AssetGpuCache::ImportSprite
// 运行时子集化——首次导入路径逐行同源，热重导/Evict/缩略图半边不随迁）
#include "Assets/TextureStore.h"

#include <algorithm>

#include "stb_image.h"

#include "Core/Log.h"

namespace lemon::assets {

void TextureStore::Init(rhi::Device& device, renderer::AtlasRegistry* atlas,
                        const AssetIndex& index, uint32_t firstSlot) {
    device_ = &device;
    atlas_ = atlas;
    index_ = &index;
    firstSlot_ = firstSlot;
}

TextureStore::Page* TextureStore::Find(uint64_t guid) {
    for (auto& p : pages_)
        if (p.guid == guid) return &p;
    return nullptr;
}
const TextureStore::Page* TextureStore::Find(uint64_t guid) const {
    for (const auto& p : pages_)
        if (p.guid == guid) return &p;
    return nullptr;
}

void TextureStore::RegisterSlices(const IndexedEntry& e, uint32_t slot, uint32_t w, uint32_t h) {
    // 像素整除/越界校验在解码侧（AssetIndex 只信 meta 声明）；行优先 cell →
    // sliceBase+cell 连号。宁缺勿错：网格与像素不符 = 红字不切（全幅仍可用）
    if (e.gridCols == 0 || e.sliceBase == 0) return;
    const uint32_t gw = (uint32_t)e.gridCols * e.cellW, gh = (uint32_t)e.gridRows * e.cellH;
    if (gw > w || gh > h) {
        LEMON_ERROR("切片网格 %u×%u 格（%u×%u px）超出图面 %u×%u——按全幅处理：%s",
                    (unsigned)e.gridCols, (unsigned)e.gridRows, gw, gh, w, h,
                    e.relPath.c_str());
        return;
    }
    for (uint32_t r = 0; r < e.gridRows; ++r)
        for (uint32_t c = 0; c < e.gridCols; ++c) {
            const uint32_t id = e.SliceSpriteId(r * e.gridCols + c);
            atlas_->SetSpriteAt(id, slot, c * e.cellW, r * e.cellH, e.cellW, e.cellH);
        }
    LEMON_LOG("切片登记：'%s' %u×%u 格 %u px → %u..%u（槽 %u）", e.relPath.c_str(),
              (unsigned)e.gridCols, (unsigned)e.gridRows, (unsigned)e.cellW, e.sliceBase,
              e.sliceBase + e.sliceCount - 1, slot);
}

bool TextureStore::LoadSprite(const IndexedEntry& e) {
    if (!device_ || !atlas_ || !index_ || e.type != AssetType::Sprite) return false;

    int w = 0, h = 0, comp = 0;
    uint8_t* px = stbi_load(index_->AbsolutePath(e).c_str(), &w, &h, &comp, 4);
    if (!px || w <= 0 || h <= 0) {
        LEMON_ERROR("图像解码失败：%s", e.relPath.c_str());
        stbi_image_free(px);
        return false;
    }

    // bindless 槽容量校验（kMaxTextureSlots 是硬上限）
    if (firstSlot_ + pages_.size() >= rhi::kMaxTextureSlots) {
        LEMON_ERROR("bindless 纹理槽满（%u）：'%s' 未导入——图集 .baked 归批⑥",
                    rhi::kMaxTextureSlots, e.relPath.c_str());
        stbi_image_free(px);
        return false;
    }

    Page p{};
    p.guid = e.guid;
    p.slot = firstSlot_ + (uint32_t)pages_.size();
    p.spriteId = e.spriteId;
    p.w = (uint32_t)w;
    p.h = (uint32_t)h;
    p.tex = device_->CreateTexture(
        {.width = p.w, .height = p.h, .debugName = "loadSprite"});
    device_->UploadTexture(p.tex, px, (uint64_t)w * h * 4);
    device_->BindTextureToSlot(p.tex, p.slot);
    atlas_->RegisterAtlas(p.slot, p.tex, p.w, p.h);
    // 按 index 记账号显式登记（全幅单 sprite；注册表与 manifest 同一本账，
    // 扫描序无关——AssetGpuCache 2026-09-21 根治注记同款纪律）
    if (!atlas_->AddSpriteAt(e.spriteId, p.slot, 0, 0, p.w, p.h)) {
        LEMON_ERROR("spriteId %u 登记失败（0/已占用）：'%s'——记账冲突，资产未导入",
                    e.spriteId, e.relPath.c_str());
        device_->WaitIdle(); // 刚上传的纹理无登记号：回滚整页
        device_->DestroyTexture(p.tex);
        // 同步注销图集页：pages_ 未收编 → 下一导入复用同槽，残留注册条目会让
        // RegisterAtlas 撞 "atlas slot reused" 断言（AssetGpuCache 同款坑）
        atlas_->UnregisterAtlas(p.slot);
        stbi_image_free(px);
        return false;
    }
    RegisterSlices(e, p.slot, p.w, p.h);
    stbi_image_free(px);

    pages_.push_back(p); // LoadAll 调用方保证 spriteId 升序喂入
    LEMON_LOG("纹理装载：'%s' %ux%u → 槽 %u spriteId %u", e.relPath.c_str(), p.w, p.h,
              p.slot, p.spriteId);
    return true;
}

uint32_t TextureStore::LoadAll() {
    if (!index_) return 0;
    // 按 DB 记账号升序导入：bindless 槽位分配确定性，与文件系统扫描序无关
    std::vector<const IndexedEntry*> imps;
    for (const IndexedEntry& e : index_->Entries())
        if (e.type == AssetType::Sprite) imps.push_back(&e);
    std::sort(imps.begin(), imps.end(), [](const IndexedEntry* a, const IndexedEntry* b) {
        return a->spriteId < b->spriteId;
    });
    uint32_t ok = 0;
    for (const IndexedEntry* e : imps)
        if (LoadSprite(*e)) ++ok;
    return ok;
}

bool TextureStore::PageInfo(uint64_t guid, uint32_t& w, uint32_t& h) const {
    const Page* p = Find(guid);
    if (!p) return false;
    w = p->w;
    h = p->h;
    return true;
}

} // namespace lemon::assets
