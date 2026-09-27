// Lemon 编辑器 — 通用图片缩略图缓存（M6a 批② T3-UX4：FilePicker 缩略图视图）
// 任意绝对路径图片 → 解码 + 缩放（≤160px 边）→ RHI 纹理 → ImGui 描述符集。
// 与 AssetGpuCache 的差异：不经 bindless 槽 / 不进 Atlas（ImGui 采样直连描述符
// 集），因此不占 kMaxTextureSlots 预算；LRU 上限防长会话膨胀（逐出时 WaitIdle，
// 同热重导入口径）。项目内已导入精灵直接复用 AssetGpuCache 的 thumb（零解码）。
// 解码走主线程逐帧预算（Tick 每帧 ≤2 张）——首帧占位灰框，后续帧回填。
// ImGui-free 头（AssetGpuCache 同款豁免：句柄一律 void*/uint32_t）。
#pragma once

#include <string>

namespace lemon {
namespace rhi {
class Device;
}
namespace editor {

class ImGuiBackend;
class AssetDatabase;
class AssetGpuCache;

namespace thumbcache {

/// 生命周期随 gpuAssets（设备就绪处 Init；换项目/设备回收处 Clear）
void Init(rhi::Device* device, ImGuiBackend* ui, const AssetDatabase* db,
          const AssetGpuCache* gpu);
/// 释放全部缩略图纹理（换项目 / 关闭）。Init 前调用为空操作。
void Clear();
/// 每帧预算入口：处理 ≤2 张排队解码（FilePicker::Draw 每帧调一次）
void Tick();
/// 取缩略图（键 = 图片绝对路径）。返回 ImTextureID（失败/未就绪 = nullptr，
/// 调用方画占位框）；w/h 出参 = 原图尺寸（未知时保持原值）。
void* Get(const std::string& absPath, int* w = nullptr, int* h = nullptr);

} // namespace thumbcache
} // namespace editor
} // namespace lemon
