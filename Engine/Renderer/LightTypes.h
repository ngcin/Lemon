// Lemon 引擎 — 光贴图数据布局与光照纯函数（M8 批②；ADR-019 M1/M2）
// 三段式合成的渲染侧数据面：
//   * LightItem/BlobItem = RenderableManager 提取产物（插值后世界变换 + 组件快照
//     平铺——Renderer 不含 ECS 组件头，#L34 纪律；字段与 ecs::Light2D 一一对应）
//   * LightInstance = light.vert 的 per-instance 环形 SSBO 布局（4×vec4 = 64B）
//   * 衰减/角向纯函数 = 02 §8 定形（2026-10-11 拍板：smoothstep 端点零导数），
//     CPU 侧单测消费；shader 内同式实现（GLSL 侧无共享头，改公式两处同步）
#pragma once

#include <cmath>
#include <cstdint>

#include "Renderer/Atlas.h"

namespace lemon::renderer {

// ---- 光贴图 RT 的 bindless 槽位（M8 批③ 起两壳共用；建 RT 时绑一次，M4.7-P0）----
// 顶部保留段由低到高：glow 程序化页（Atlas.h kGlowTexSlot=249）→ sceneRT → lightRT。
// #L20 记账：顶部段 3 槽（249/250/251），远离资产页（2 起，分配上限 kAtlasSlotCeiling）
inline constexpr uint32_t kSceneRtTexSlot = 250;
inline constexpr uint32_t kLightRtTexSlot = 251;

// ---- CPU 提取产物（RenderableManager → LightCompositor）----
struct LightItem {
    float posX, posY;         // 插值后世界位置
    float rot;                // 插值后旋转（Spot 方向；引擎约定 0 = +X）
    uint32_t type;            // ecs::kLightType*（0=Global 1=Point 2=Spot 3=SpriteMask）
    uint32_t blend;           // ecs::kLightBlend*（0=Additive 1=Screen 2=Subtract）
    uint32_t color;           // ColorHex（r|g<<8|b<<16|a<<24）
    float intensity;
    float range;              // 点/锥光半径（px）
    float angle;              // 锥光张角（弧度）
    float innerRatio;         // 锥光内角占比 [0,1]
    uint32_t maskSprite;      // spriteId（SpriteMask 用；Bake 经 ResolveLightMask 消费）
    bool enabled;             // kLightEnabled（禁用灯不入提取——提取侧已滤，防御位）
};

struct BlobItem {
    float posX, posY;         // 插值后世界位置（offsetY 渲染期加）
    float radius;
    float aspect;             // 椭圆纵横比（1 = 正圆）
    float offsetY;
    float opacity;            // [0,1]（lightRT Subtract 强度）
};

// ---- GPU 实例（与 light.vert 逐字段对应；4×vec4 = 64B，std430 无空洞）----
struct alignas(16) LightInstance {
    // A：仿射前两行（旋转 + 四边形世界边长 = 2×range）
    float a, b;
    float c, d;
    // B：平移 + 颜色 + type<<28|blend<<24|蒙版页槽 打包
    float e, f;
    uint32_t colorBits;
    uint32_t typeBlend;
    // C：光照参数
    float intensity;
    float range;
    float angle;
    float innerRatio;
    // D：蒙版 UV（SpriteMask 用，批③ 起由 Bake 填；Point/Spot 弃——布局一次定死免改环）
    float maskU0, maskV0, maskU1, maskV1;
};
static_assert(sizeof(LightInstance) == 64, "光照实例必须 64B（4×vec4）");

// 批③ 起打包含蒙版图集页槽：type<<28 | blend<<24 | atlasIdx（各 4/4/24 bit——
// type 0..3、blend 0..2、bindless 槽 <256，位宽三段都富余；light.frag 按实例
// nonuniform 取页消费低 24 位）
inline constexpr uint32_t PackLightTypeBlend(uint32_t type, uint32_t blend,
                                             uint32_t atlasIdx) {
    return ((type & 0xFu) << 28) | ((blend & 0xFu) << 24) | (atlasIdx & 0x00FFFFFFu);
}

// light.vert push 常量（与 sprite 的 48B 块同 <128B 上限，布局独立——管线共享
// 布局的 push range 是全 128B，各着色器块自管）
struct LightPushConstants {
    float vpR0[4];     // 正交仿射行0/行1（sprite 同款）
    float vpR1[4];     // 平移 + 预留
    uint32_t baseInstance;
    uint32_t ringIndex; // 实例环 SSBO 数组槽（LightBatcher 固定槽）
    uint32_t _pad[2];
};
static_assert(sizeof(LightPushConstants) == 48);

// composite push 常量：全屏四边形由角点直推 NDC（vp 恒等），只需双纹理索引
struct CompositePushConstants {
    uint32_t sceneTexIndex; // bindless 槽（sceneRT）
    uint32_t lightTexIndex; // bindless 槽（lightRT）
    uint32_t samplerIndex;  // bindless 采样器槽
    uint32_t _pad;
};
static_assert(sizeof(CompositePushConstants) == 16);

// ---- 光照纯函数（02 §8 定形；shader 内同式实现，改动两处同步）----
// 径向衰减：t = clamp(1 − d/range)，att = t²(3−2t)（smoothstep 端点零导数——
// range 边界无亮环/硬切；dn = d/range 归一化距离）
inline float LightRadialAttenuation(float dn) {
    const float t = 1.0f - dn;
    if (t <= 0.0f) return 0.0f;
    const float tc = t > 1.0f ? 1.0f : t;
    return tc * tc * (3.0f - 2.0f * tc);
}

// Spot 角向软边：cosAng 与外/内半角余弦的 smoothstep（outerCos < innerCos）。
// 外半角之外归零，innerRatio 内全亮，其间软过渡
inline float LightSpotAngular(float cosAng, float outerCos, float innerCos) {
    if (cosAng <= outerCos) return 0.0f;
    if (cosAng >= innerCos) return 1.0f;
    const float t = (cosAng - outerCos) / (innerCos - outerCos);
    return t * t * (3.0f - 2.0f * t);
}

// ---- 批③ 纯函数（单测面；shader/CPU 双实现同式纪律不适用于此二者——纯 CPU）----

/// SpriteMask 蒙版解析：maskSprite → 图集 UV 矩形 + 页槽（Bake 填 D 行/typeBlend 用）。
/// 无效/悬空 spriteId → false（调用方跳过该灯 + 一次性 WARN——SceneExtractor 头注
/// "悬空不阻断提取，渲染期回退"的落点）。atlas 允许为 nullptr（无图集壳 = 恒 false）
inline bool ResolveLightMask(const AtlasRegistry* atlas, uint32_t spriteId, float uv[4],
                             uint32_t& atlasIdx) {
    if (atlas == nullptr || !atlas->IsValidSprite(spriteId)) return false;
    const SpriteInfo& si = atlas->GetSprite(spriteId);
    uv[0] = si.u0;
    uv[1] = si.v0;
    uv[2] = si.u1;
    uv[3] = si.v1;
    atlasIdx = si.atlasIndex;
    return true;
}

/// blob → 光照实例映射（M8 批③，ADR-019 M2：lightRT 上 Subtract 椭圆暗斑）。
/// 走 Point 径向路径零 shader 改动：color 白、intensity=opacity、range=radius；
/// 椭圆由仿射承担——edgeX=2·radius·aspect / edgeY=2·radius（SceneExtractor 提取的
/// posY 在此加 offsetY；与编辑器 gizmo ViewportPanels 同约定，+Y 上）
inline void FillBlobInstance(LightInstance& inst, const BlobItem& b) {
    const float edgeX = 2.0f * b.radius * b.aspect;
    const float edgeY = 2.0f * b.radius;
    inst.a = edgeX; // 无旋转（blob 无 rot 字段）
    inst.b = 0.0f;
    inst.c = 0.0f;
    inst.d = edgeY;
    inst.e = b.posX;
    inst.f = b.posY + b.offsetY;
    inst.colorBits = 0xFFFFFFFFu; // 白 × intensity=opacity → dst − 白·opacity·att
    inst.typeBlend = PackLightTypeBlend(1u /*Point 径向路径*/, 2u /*Subtract*/, 0u);
    inst.intensity = b.opacity;
    inst.range = b.radius; // 径向路径死字段（dn 已按四边形 extents 归一）——勿以调 blob 尺寸
    inst.angle = 0.0f;
    inst.innerRatio = 0.0f;
    inst.maskU0 = inst.maskV0 = inst.maskU1 = inst.maskV1 = 0.0f;
}

} // namespace lemon::renderer
