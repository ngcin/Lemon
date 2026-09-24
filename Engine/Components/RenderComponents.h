// Lemon 引擎 — 组件目录 · Render 组（03 文档 §3.2；渲染提取消费，M4/M5 激活）
#pragma once

#include <cstdint>
#include <type_traits>

namespace lemon::ecs {

/// SpriteRenderer.flags 位（单一来源；C# 镜像 Components.cs 同步注释）
inline constexpr uint8_t kSrFlipX = 0x1;
inline constexpr uint8_t kSrFlipY = 0x2;
inline constexpr uint8_t kSrEnabled = 0x4;
inline constexpr uint8_t kSrFlipMask = kSrFlipX | kSrFlipY;

struct SpriteRenderer {
    uint32_t spriteId = 0;     // AtlasRegistry 静态表 id（进程内派生缓存；真源 = spriteGuid）
    uint32_t colorRGBA = 0xFFFFFFFFu;
    int16_t sortOrder = 0;
    uint8_t sortingLayer = 0;
    uint8_t flags = kSrEnabled; // 新增即启用（Unity 语义；C# default(T) 零值 = 禁用）
    // M6a 批⓪：引用 GUID 化（尾加）。改名/移位/manifest 重建后 spriteId 漂移，
    // 装载期由 EditorContext::ResolveSpriteRefs 按 guid 归一。0 = 存量档未回填
    //（spriteId 仍是唯一真源；下次保存升级）。
    uint64_t spriteGuid = 0;
};

struct Animator2D {
    uint32_t clipId = 0;
    float time = 0.0f;
    float speed = 1.0f;
    uint8_t loop = 1;
    uint8_t playOnStart = 1;
    uint16_t curFrame = 0;
};

struct ParticleEmitterRef {
    uint32_t emitterId = 0;
    uint8_t playing = 1;
    uint8_t _pad[3] = {};
};

/// 运行时排序覆盖（血条永远压怪物；提取阶段生效）
struct SortingOverride {
    int16_t order = 0;
};

// ---- 布局冻结（M3 桥侧 blittable 前提：C# 镜像 struct 与此逐字节对齐，改动=破回放）----
static_assert(std::is_trivially_copyable_v<SpriteRenderer> && sizeof(SpriteRenderer) == 24, "SpriteRenderer 布局冻结");
static_assert(std::is_trivially_copyable_v<Animator2D> && sizeof(Animator2D) == 16, "Animator2D 布局冻结");
static_assert(std::is_trivially_copyable_v<ParticleEmitterRef> && sizeof(ParticleEmitterRef) == 8, "ParticleEmitterRef 布局冻结");
static_assert(std::is_trivially_copyable_v<SortingOverride> && sizeof(SortingOverride) == 2, "SortingOverride 布局冻结");

} // namespace lemon::ecs
