// Lemon 引擎 — Play 世界三缓存构建（M7a 批④；M5 批③ / M6a 批② T2·T3c·T3d 随迁）
// 自 EditorContext::{BuildPlayClipCache,BuildPlayControllerCache,BuildPlayTableCache}
// 下沉（搬家非复制，日志字符串逐字节保留）：进 Play/运行时启动期把 .anim/.override/
// .controller/.tab 资产解析进 World 三通道（Clips/Controllers/Tables），键 = 资产
// GUID 低 32 位（clipId/prefabId 同款映射约定，03 §69 组件 schema 恒 u32）。
// 装载时刻快照语义（Play 中改档不生效）；坏档红字跳过不炸 Play。编辑器
// （AssetDatabase 源）与独立运行时（AssetIndex 源）共用——杜绝双实现漂移。
#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "Assets/AssetIndex.h" // IndexedEntry（AssetEntry 的运行时子集视图）
#include "Assets/AssetTypes.h"

namespace lemon::ecs {
class World;
}

namespace lemon::assets {

/// 三缓存构建的资产只读源（SpriteRefSource 同款纪律：编辑器 AssetDatabase /
/// 运行时 AssetIndex 双实现）。Each 只迭代健康资产（missing 归适配器过滤）。
class PlayCacheSource {
public:
    virtual ~PlayCacheSource() = default;
    /// 按 type 迭代（Entries() relPath 升序）；回调 (guid, relPath, absPath)
    virtual void Each(AssetType type,
                      const std::function<void(uint64_t guid, const std::string& relPath,
                                               const std::string& absPath)>& fn) const = 0;
    /// guid → sprite 条目（clip 帧解析判源；查无/非 sprite/missing = nullptr。
    /// 返回指针有效期 = 至下次调用（编辑器适配器为 scratch 单槽））
    virtual const IndexedEntry* FindSprite(uint64_t guid) const = 0;
    /// guid → 健康 clip 资产在场（animset 段悬空检查）
    virtual bool HasClip(uint64_t guid) const = 0;
};

/// clip 表 + 动画集（.anim → ClipTable 帧映射 + .override 集按名索引）。
/// EditorContext::BuildPlayClipCache 同源。
void BuildClipCache(ecs::World& world, const PlayCacheSource& src);

/// 状态机表（.controller → ControllerTable，字符串形态编译为下标形态）。
/// EditorContext::BuildPlayControllerCache 同源。
void BuildControllerCache(ecs::World& world, const PlayCacheSource& src);

/// 配置表（.tab → TableStore 全字符串格）。EditorContext::BuildPlayTableCache 同源。
void BuildTableCache(ecs::World& world, const PlayCacheSource& src);

} // namespace lemon::assets
