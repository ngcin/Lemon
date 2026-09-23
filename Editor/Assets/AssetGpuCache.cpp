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

// 网格切片登记（M5 批③ D3）：像素整除/越界校验在解码侧（DB 只信 meta 声明）；
// 行优先 cell → sliceBase+cell 连号。宁缺勿错：网格与像素不符 = 红字不切。
bool AssetGpuCache::RegisterSlices(const AssetEntry& e, uint32_t slot, uint32_t w, uint32_t h,
                                   bool overwrite) {
    if (e.gridCols == 0 || e.sliceBase == 0) return false; // 未声明网格/无块
    const uint32_t gw = (uint32_t)e.gridCols * e.cellW, gh = (uint32_t)e.gridRows * e.cellH;
    if (gw > w || gh > h) {
        LEMON_ERROR("切片网格 %u×%u 格（%u×%u px）超出图面 %u×%u——按全幅处理：%s",
                    (unsigned)e.gridCols, (unsigned)e.gridRows, gw, gh, w, h, e.relPath.c_str());
        return false;
    }
    uint32_t conflicts = 0;
    for (uint32_t r = 0; r < e.gridRows; ++r)
        for (uint32_t c = 0; c < e.gridCols; ++c) {
            const uint32_t id = e.SliceSpriteId(r * e.gridCols + c);
            if (overwrite) {
                atlas_->SetSpriteAt(id, slot, c * e.cellW, r * e.cellH, e.cellW, e.cellH);
            } else if (!atlas_->AddSpriteAt(id, slot, c * e.cellW, r * e.cellH, e.cellW,
                                           e.cellH)) {
                ++conflicts; // 占号冲突：manifest 记账两本账漂移（单条红字汇总）
            }
        }
    if (conflicts)
        LEMON_ERROR("切片登记冲突 %u/%u（号被占）：'%s'——manifest 记账异常（删 "
                    ".lemon/manifest.json 可重排，已存引用将失效）",
                    conflicts, e.sliceCount, e.relPath.c_str());
    else
        LEMON_LOG("切片登记：'%s' %u×%u 格 %u px → %u..%u（槽 %u）", e.relPath.c_str(),
                  (unsigned)e.gridCols, (unsigned)e.gridRows, (unsigned)e.cellW, e.sliceBase,
                  e.sliceBase + e.sliceCount - 1, slot);
    return conflicts == 0;
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
        const bool gridChanged = p->cellW != e.cellW || p->cellH != e.cellH ||
                                 p->gridCols != e.gridCols || p->gridRows != e.gridRows;
        if ((uint32_t)w == p->w && (uint32_t)h == p->h && !gridChanged) {
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
        // 切片热重切（M5 批③）：网格/尺寸变化 = 覆盖式重登记（块号 manifest 记账
        // 稳定）；配置回看快照同步（未声明网格 = 清零，下一轮按全幅）
        if (gridChanged) RegisterSlices(e, p->slot, (uint32_t)w, (uint32_t)h, true);
        p->cellW = e.cellW;
        p->cellH = e.cellH;
        p->gridCols = e.gridCols;
        p->gridRows = e.gridRows;
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
    p.cellW = e.cellW;
    p.cellH = e.cellH;
    p.gridCols = e.gridCols;
    p.gridRows = e.gridRows;
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
    // 网格切片（M5 批③）：一页纹理 + 全幅 sprite + 连号切片块（引用兼容；冲突已
    // 红字但不回滚整页——全幅可用，切片按登记成功的子集生效）
    RegisterSlices(e, p.slot, p.w, p.h, false);
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

void AssetGpuCache::ClearPages() {
    // 低频路径（换项目）同热重导入尺寸变化口径：在途帧可能采样旧页
    if (device_ && !pages_.empty()) device_->WaitIdle();
    for (const Page& p : pages_) {
        if (p.thumb && ui_) ui_->UnregisterViewportTexture(p.thumb);
        if (p.tex.IsValid() && device_) device_->DestroyTexture(p.tex);
    }
    pages_.clear();
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
