// Lemon 引擎 — 路径 A 实例/push-constant 内存布局（与 sprite.vert 逐字段对应）
// 实例 = 3×vec4 = 48B（02 §3.3 设计值；以 vec4 数组避免 std430 结构体空洞）
#pragma once

#include <cmath>
#include <cstdint>

namespace lemon::renderer {

struct alignas(16) SpriteInstance {
    // A：仿射前两行（旋转+缩放）
    float a, b;   // 行0：x 基
    float c, d;   // 行1：y 基
    // B：平移 + uv 左上
    float e, f;   // 平移（世界像素）
    float u0, v0;
    // C：uv 右下 + 颜色/标志
    float u1, v1;
    uint32_t colorBits; // r | g<<8 | b<<16 | a<<24（unpackUnorm4x8）
    uint32_t flags;     // bit0 flipX, bit1 flipY
};
static_assert(sizeof(SpriteInstance) == 48, "路径 A 实例必须 48B（02 §3.3）");

inline void FillInstanceAffine(SpriteInstance& out, float posX, float posY, float rotRad,
                               float scaleX, float scaleY) {
    float sinR, cosR;
    math::FastSinCos(rotRad, sinR, cosR); // LUT 共享索引（15 万次/帧级热路径）
    out.a = cosR * scaleX;
    out.b = sinR * scaleX;
    out.c = -sinR * scaleY;
    out.d = cosR * scaleY;
    out.e = posX;
    out.f = posY;
}

struct SpritePushConstants {
    float vpR0[4];     // 正交仿射行0(a,b) 行1(c,d)
    float vpR1[4];     // 平移(e,f) + 预留
    uint32_t baseInstance;
    uint32_t atlasIndex;
    uint32_t samplerIndex;
    uint32_t ringIndex; // 实例环 SSBO 数组槽（多视口合批器各占一槽，与 sprite.vert 同名）
};
static_assert(sizeof(SpritePushConstants) == 48);

// 实例 flags 位
constexpr uint32_t kInstFlipX = 1u << 0;
constexpr uint32_t kInstFlipY = 1u << 1;
// Packet 侧位（M7c 批① S2）：bit2 = 产包自带 UV 覆盖（Bake 消费，不进实例——
// sprite.vert 的 flags 位面不变）
constexpr uint32_t kPktUvOverride = 1u << 2;

} // namespace lemon::renderer
