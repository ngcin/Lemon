// Lemon 引擎 — 组件目录 · Render 组（03 文档 §3.2；渲染提取消费，M4/M5 激活）
#pragma once

#include <cstdint>
#include <type_traits>

namespace lemon::ecs {

struct SpriteRenderer {
    uint32_t spriteId = 0;     // AtlasRegistry 静态表 id
    uint32_t colorRGBA = 0xFFFFFFFFu;
    int16_t sortOrder = 0;
    uint8_t sortingLayer = 0;
    uint8_t flags = 0;         // bit0 flipX, bit1 flipY, bit2 enabled
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
static_assert(std::is_trivially_copyable_v<SpriteRenderer> && sizeof(SpriteRenderer) == 12, "SpriteRenderer 布局冻结");
static_assert(std::is_trivially_copyable_v<Animator2D> && sizeof(Animator2D) == 16, "Animator2D 布局冻结");
static_assert(std::is_trivially_copyable_v<ParticleEmitterRef> && sizeof(ParticleEmitterRef) == 8, "ParticleEmitterRef 布局冻结");
static_assert(std::is_trivially_copyable_v<SortingOverride> && sizeof(SortingOverride) == 2, "SortingOverride 布局冻结");

} // namespace lemon::ecs
