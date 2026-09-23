// Lemon 编辑器 — 资产 GPU 缓存（M4-Editor-Plan §5 M4.4 导入器；06 §2.2 sprite 族）
// PNG/JPG 解码（stb_image）→ 每文件独立纹理页（bindless 槽 2..；一页一 sprite
// 全幅 uv——切片/图集打包 M5+/M6）→ AtlasRegistry 登记（spriteId 由 AssetDatabase
// 持久分配；导入顺序 = spriteId 升序 → 与追加式登记天然对齐）。
// 缩略图：纹理经 ImGuiBackend 注册为 ImTextureID（AssetBrowser/Inspector 槽预览）。
// 热重导入：同尺寸 = 原纹理重上传（staging 同步）；尺寸变化 = 销毁重建同槽位
// （低频路径 WaitIdle）+ AtlasRegistry::UpdateAtlasPage。设备丢失 = RebuildAll。
// 纪律：本 TU 不含 imgui.h（隔离断言面）；ImGui 经 ImGuiBackend.h（零 ImGui 头）。
#pragma once

#include <cstdint>
#include <vector>

#include "Renderer/Atlas.h"
#include "Renderer/RHI.h"

namespace lemon::editor {

class AssetDatabase;
class ImGuiBackend;
struct AssetEntry;

class AssetGpuCache {
public:
    /// firstSlot：导入页起始 bindless 槽（0=程序化调色板 1=字体页，导入从 2 起）。
    /// db = 资产库（路径解析/GUID 查找；引用须长于本缓存）
    void Init(rhi::Device& device, ImGuiBackend* ui, renderer::AtlasRegistry* atlas,
              const AssetDatabase& db, uint32_t firstSlot);

    /// 导入/热重导入一个 sprite 资产（Rescan 的 ChangeSet 消费端）。失败红字。
    void ImportSprite(const AssetEntry& e);
    /// 文件删除（墓碑）：纹理/缩略图释放（spriteId 号保留在 DB；场景引用悬空
    /// 由 Inspector 槽红显——不再渲染占位）。
    void Evict(uint64_t guid);
    /// 换项目复位（M5 批④后修②）：旧项目导入页整体释放（纹理/缩略图注销）。
    /// 必须与 AtlasRegistry::Reset 成对——槽位序（firstSlot_+pages_.size()）与
    /// 新注册表的图集序要在空表上重新对齐，重导入由 OpenProjectPipeline 接续。
    void ClearPages();
    /// 设备丢失后全量重建（AtlasRegistry.Reset 已清号；按 spriteId 升序重导入
    /// 与程序化页拼接 → 编号复原）
    void RebuildAll(rhi::Device& device);

    /// 缩略图 ImTextureID（非 sprite / 未导入 = nullptr）
    void* Thumbnail(uint64_t guid) const;
    /// 导入页信息（冒烟断言：尺寸热替换可见）
    bool PageInfo(uint64_t guid, uint32_t& w, uint32_t& h) const;
    uint32_t PageCount() const { return (uint32_t)pages_.size(); }

private:
    struct Page {
        uint64_t guid;
        uint32_t slot;
        uint32_t spriteId;
        rhi::Texture tex{};
        void* thumb = nullptr; // ImTextureID
        uint32_t w = 0, h = 0;
        // 切片配置快照（M5 批③）：热重导入比对——像素尺寸或网格任一变化 = 重切
        uint16_t cellW = 0, cellH = 0, gridCols = 0, gridRows = 0;
    };
    Page* Find(uint64_t guid);
    const Page* Find(uint64_t guid) const;
    /// 网格切片登记（D3）：像素校验（网格越界 = 红字 false，宁缺勿错不 assert）；
    /// overwrite=false 走 AddSpriteAt（新页，占号冲突红字）；true 走 SetSpriteAt
    /// （热重导重切——块号已由 manifest 记账，覆盖写）。
    bool RegisterSlices(const AssetEntry& e, uint32_t slot, uint32_t w, uint32_t h,
                        bool overwrite);

    rhi::Device* device_ = nullptr;
    ImGuiBackend* ui_ = nullptr;
    renderer::AtlasRegistry* atlas_ = nullptr;
    const AssetDatabase* db_ = nullptr;
    uint32_t firstSlot_ = 2;
    std::vector<Page> pages_; // spriteId 升序
};

} // namespace lemon::editor
