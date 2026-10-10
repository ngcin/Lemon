// Lemon 引擎 — 组件目录 · 光照组首件（M8 批①；Unity Light2D 同构粒度：
// 光贴图合成方案的数据面，ADR-019 M2。渲染消费归批② LightCompositor——本批
// 仅落位组件/序列化/镜像，**渲染侧零消费**）。
// 运行时状态严禁回写本组件（快照确定性铁律，AudioSource 同款）——光照纯
// 表现层：昼夜循环 = 脚本逐帧写 Global 灯字段（op2 整写，入回放）；渲染侧
// 只读扫描。Global 灯至多一个生效（重复取首个 + WARN——批② 渲染侧语义）。
#pragma once

#include <cstdint>
#include <type_traits>

namespace lemon::ecs {

// type 枚举值（编辑器名表 kLightTypeNames 同序；C# 侧裸值镜像）
inline constexpr uint32_t kLightTypeGlobal = 0;     // 环境光（颜色×强度 = lightRT 底色）
inline constexpr uint32_t kLightTypePoint = 1;      // 径向衰减点光
inline constexpr uint32_t kLightTypeSpot = 2;       // 锥光（方向 = Transform2D 旋转）
inline constexpr uint32_t kLightTypeSpriteMask = 3; // 蒙版光（贴图造型，采样图集页）

// blend 枚举值（lightRT 上的混合模式；kLightBlendNames 同序）
inline constexpr uint32_t kLightBlendAdditive = 0;
inline constexpr uint32_t kLightBlendScreen = 1;
inline constexpr uint32_t kLightBlendSubtract = 2;

inline constexpr uint16_t kLightEnabled = 1 << 0; // bit0：启用（默认置位，Unity 同构）

struct Light2D {
    uint32_t type = kLightTypePoint; // 默认点光（Unity URP 2D 同构；Global 至多一个）
    uint32_t blend = kLightBlendAdditive;
    uint32_t color = 0xFFFFFFFFu;    // 白。镜像坑：C# default = 0（透明黑）——
                                     // SetComponent 整写须显式置色（Flags 同款）
    float intensity = 1.0f;
    float range = 256.0f;            // 点/锥光半径（px）
    float angle = 1.57079632f;       // 锥光张角 90°（弧度存储，Inspector 按角度编辑）
    float innerRatio = 0.5f;         // 锥光内角占比 [0,1]（软边）
    uint32_t maskSprite = 0;         // spriteId（SpriteMask 用；AtlasRegistry 查 UV）
    uint16_t flags = kLightEnabled;  // 镜像坑：C# default = 0 = 禁用——整写须显式
                                     // 置位（SpriteRenderer.Flags 同款）
    uint16_t pad_ = 0;
};

// ---- 布局冻结（M3 桥侧 blittable 前提：C# 镜像 struct 逐字节同构，改动=破回放）----
// 4B 槽 ×8（u32×4 + f32×4 之后再 u32）+ u16×2 = 36B 自然对齐 sizeof=36/alignof=4。
static_assert(std::is_trivially_copyable_v<Light2D> && sizeof(Light2D) == 36 &&
                  alignof(Light2D) == 4,
              "Light2D 布局冻结");

struct BlobShadow2D {
    float radius = 32.0f;   // 椭圆暗斑半径（px）。镜像坑：C# default = 0——整写须显式
    float aspect = 1.0f;    // 椭圆纵横比（1 = 正圆）
    float offsetY = 0.0f;   // 沿 Y 偏移（角色脚下贴地用）
    float opacity = 0.5f;   // 暗斑强度 [0,1]（lightRT Subtract）
};

static_assert(std::is_trivially_copyable_v<BlobShadow2D> && sizeof(BlobShadow2D) == 16 &&
                  alignof(BlobShadow2D) == 4,
              "BlobShadow2D 布局冻结");

} // namespace lemon::ecs
