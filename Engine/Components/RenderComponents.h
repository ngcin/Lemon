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
    // M6a 批② T3b-2：LoopMode（0=Once 钳末帧 / 1=Loop 回绕 / 2=PingPong 往返）。
    // 旧值 0/1 语义逐位不变（布局冻结/C# bool 写路径兼容）；PingPong 帧映射 =
    // 纯函数（period=2(n-1)，无逐帧累加状态机）。档面 clip.loopMode = 创建默认，
    // 本字段 = 运行时权威（Inspector 下拉热调参）。
    uint8_t loop = 1;
    uint8_t playOnStart = 1;
    uint16_t curFrame = 0;
    // M6a 批①：换段队列（FIELD_RT——入状态哈希不入档）。nextClipId=0 = 无队列；
    // fadeRemain<0 = Queue（当前段收尾/回绕点切）、>0 = CrossFade 倒计时。
    // SDK Lemon.Anim 纯字段写；AnimatorSystem 消费（无 clip 表 = 整体旁路）。
    uint32_t nextClipId = 0;
    float fadeRemain = 0.0f;
    uint16_t nextLoop = 1;
    // T3d 批③：段末边沿（FIELD_RT）。借原 _pad[2] 首字节——28B 布局逐位不动；
    // 非 loop 段收尾钳 total 时置 1（0→1 边沿发 AnimFinished 事件），切段归 0。
    // M6a 批①/③前基线场景零 Animator2D 实例 → 入哈希零重录（同款口径）。
    uint8_t ended = 0;
    uint8_t _pad = 0;
};

// T3d 批①（ADR-013 D1/D2）：实体 ↔ controller/集 显式绑定——按名解析作用域与
// AnimGraphSystem 图评估都经此组件（消灭"当前 clip 恰属哪个集"的隐式反推）。
// 入档入哈希（配置引用面，spriteGuid/prefabId 同款）；0 = 未绑（旁路图/回退隐式集）。
struct AnimGraph {
    uint64_t controllerGuid = 0; // .controller 资产 GUID（0 = 无图，纯绑定用）
    uint64_t setGuid = 0;        // .override 集资产 GUID（按名解析作用域）
    // T3d 批②：图初始化边沿（FIELD_RT，入哈希不入档）——首 tick 由此触发：
    // AnimParams 槽从 controller 默认值播种 + 当前段不在集内时 Play(entry)。
    uint8_t inited = 0;
    uint8_t _pad[7] = {};
};

// T3d 批②（ADR-013 D1）：参数黑板（float/bool/trigger 统一 f32 槽；槽位 = 实体
// 所绑 controller 参数表定序）。全 FIELD_RT（入哈希不入档）——参数是运行时态，
// 每局由 controller 默认值重新播种（AnimGraph.inited 边沿驱动）。trigger 语义 =
// 非 0 待消费，AnimGraphSystem 评估命中即清 0（消费即清发生在图评估内）。
struct AnimParams {
    float v[8] = {};
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
static_assert(std::is_trivially_copyable_v<Animator2D> && sizeof(Animator2D) == 28, "Animator2D 布局冻结");
static_assert(std::is_trivially_copyable_v<AnimGraph> && sizeof(AnimGraph) == 24, "AnimGraph 布局冻结");
static_assert(std::is_trivially_copyable_v<AnimParams> && sizeof(AnimParams) == 32, "AnimParams 布局冻结");
static_assert(std::is_trivially_copyable_v<ParticleEmitterRef> && sizeof(ParticleEmitterRef) == 8, "ParticleEmitterRef 布局冻结");
static_assert(std::is_trivially_copyable_v<SortingOverride> && sizeof(SortingOverride) == 2, "SortingOverride 布局冻结");

} // namespace lemon::ecs
