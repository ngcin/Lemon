// Lemon 编辑器 — 资产 GPU 缓存实现（stb_image 解码只进本 .cpp）
#include "Assets/AssetGpuCache.h"

#include <algorithm>
#include <cstdio>

#include "stb_image.h"

#include "App/ImGuiBackend.h"
#include "Assets/AssetDatabase.h"
#include "Core/Log.h"

namespace lemon::editor {

void AssetGpuCache::Init(rhi::Device& device, ImGuiBackend* ui, renderer::AtlasRegistry* atlas,
                         const AssetDatabase& db, uint32_t firstSlot) {
    device_ = &device;
    ui_ = ui;
    atlas_ = atlas;
    db_ = &db;
    firstSlot_ = firstSlot;
}

AssetGpuCache::Page* AssetGpuCache::Find(uint64_t guid) {
    for (auto& p : pages_)
        if (p.guid == guid) return &p;
    return nullptr;
}
const AssetGpuCache::Page* AssetGpuCache::Find(uint64_t guid) const {
    for (const auto& p : pages_)
        if (p.guid == guid) return &p;
    return nullptr;
}

void AssetGpuCache::ImportSprite(const AssetEntry& e) {
    if (!device_ || !atlas_ || !db_ || e.type != AssetType::Sprite) return;

    int w = 0, h = 0, comp = 0;
    uint8_t* px = stbi_load(db_->AbsolutePath(e).c_str(), &w, &h, &comp, 4);
    if (!px || w <= 0 || h <= 0) {
        LEMON_ERROR("PNG 解码失败：%s", e.relPath.c_str());
        stbi_image_free(px);
        return;
    }

    if (Page* p = Find(e.guid)) { // 热重导入
        if ((uint32_t)w == p->w && (uint32_t)h == p->h) {
            device_->UploadTexture(p->tex, px, (uint64_t)w * h * 4); // 同尺寸：原位重传
        } else {
            device_->WaitIdle(); // 尺寸变化低频；在途帧可能采样旧视图
            device_->DestroyTexture(p->tex);
            p->tex = device_->CreateTexture(
                {.width = (uint32_t)w, .height = (uint32_t)h, .debugName = "importSprite"});
            device_->UploadTexture(p->tex, px, (uint64_t)w * h * 4);
            device_->BindTextureToSlot(p->tex, p->slot);
            atlas_->UpdateAtlasPage(p->slot, p->tex, (uint32_t)w, (uint32_t)h);
            if (p->thumb) {
                ui_->UnregisterViewportTexture(p->thumb);
                p->thumb = ui_->RegisterViewportTexture(p->tex.id);
            }
            p->w = (uint32_t)w;
            p->h = (uint32_t)h;
            LEMON_LOG("资产热重导入（尺寸 %u×%u → 同槽 %u）：%s", p->w, p->h, p->slot,
                      e.relPath.c_str());
        }
        stbi_image_free(px);
        return;
    }

    // 新页：bindless 槽容量校验（kMaxTextureSlots 是硬上限）
    if (firstSlot_ + pages_.size() >= rhi::kMaxTextureSlots) {
        LEMON_ERROR("bindless 纹理槽满（%u）：'%s' 未导入——M4 最小集上限，图集打包 M6 解",
                    rhi::kMaxTextureSlots, e.relPath.c_str());
        stbi_image_free(px);
        return;
    }

    Page p{};
    p.guid = e.guid;
    p.slot = firstSlot_ + (uint32_t)pages_.size();
    p.spriteId = e.spriteId;
    p.w = (uint32_t)w;
    p.h = (uint32_t)h;
    p.tex = device_->CreateTexture(
        {.width = p.w, .height = p.h, .debugName = "importSprite"});
    device_->UploadTexture(p.tex, px, (uint64_t)w * h * 4);
    device_->BindTextureToSlot(p.tex, p.slot);
    atlas_->RegisterAtlas(p.slot, p.tex, p.w, p.h);
    // 按 DB 记账号显式登记（全幅单 sprite，M4 最小集）——注册表与 manifest 同一本账，
    // 扫描序无关。2026-09-21 根治：曾按登记序自增 + "自愈"接受漂移号（且并未重指
    // 场景引用）——文件系统扫描序一变，两本账交叉 = 指定 A 显示 B；号超出注册表
    // 容量 = 全部不显示（etest 实抓）。
    if (!atlas_->AddSpriteAt(e.spriteId, p.slot, 0, 0, p.w, p.h)) {
        LEMON_ERROR("spriteId %u 登记失败（0/已占用）：'%s'——manifest 记账冲突，"
                    "资产未导入（引用红显；删 .lemon/manifest.json 可重排）",
                    e.spriteId, e.relPath.c_str());
        device_->WaitIdle(); // 刚上传的纹理无登记号：回滚整页
        device_->DestroyTexture(p.tex);
        stbi_image_free(px);
        return;
    }
    stbi_image_free(px);

    // 保持 spriteId 升序（RebuildAll 的槽位复原依赖此序）
    pages_.push_back(p);
    std::sort(pages_.begin(), pages_.end(), [](const Page& a, const Page& b) {
        return a.spriteId < b.spriteId;
    });
    if (ui_) Find(e.guid)->thumb = ui_->RegisterViewportTexture(Find(e.guid)->tex.id);
    LEMON_LOG("资产导入：'%s' %ux%u → 槽 %u spriteId %u", e.relPath.c_str(), p.w, p.h, p.slot,
              p.spriteId);
}

void AssetGpuCache::Evict(uint64_t guid) {
    // M4 最小集：纹理与登记号保留（"幽灵页"）——销毁纹理会使仍指向该 spriteId 的
    // 场景实体采样悬空描述符；DB 已标墓碑（Inspector 槽红显 + 体检红字），浏览器
    // 隐藏，重启后不再导入。图集打包/槽位回收 M6。
    if (Find(guid)) LEMON_WARN("资产 GPU 页转幽灵（保留到重启；场景引用红显）：%016llx",
                               (unsigned long long)guid);
}

void AssetGpuCache::RebuildAll(rhi::Device& device) {
    device_ = &device;
    pages_.clear(); // 纹理已随设备丢失销毁；登记号已由 AtlasRegistry.Reset 清空
    if (!db_) return;
    for (const auto& e : db_->Entries()) {
        if (e.missing || e.type != AssetType::Sprite) continue;
        // 按 DB 记账号升序重导入（与程序化页拼接后追加序 = 原分配序 → 编号复原）
        ImportSprite(e);
    }
}

void* AssetGpuCache::Thumbnail(uint64_t guid) const {
    const Page* p = Find(guid);
    return p ? p->thumb : nullptr;
}

bool AssetGpuCache::PageInfo(uint64_t guid, uint32_t& w, uint32_t& h) const {
    const Page* p = Find(guid);
    if (!p) return false;
    w = p->w;
    h = p->h;
    return true;
}

} // namespace lemon::editor
