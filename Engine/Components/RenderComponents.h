// Lemon 引擎 — 组件目录 · Render 组（03 文档 §3.2；渲染提取消费，M4/M5 激活）
#pragma once

#include <cstdint>

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

} // namespace lemon::ecs
