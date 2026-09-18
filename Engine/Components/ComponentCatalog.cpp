// Lemon 引擎 — 组件目录登记表（03 文档 §3 全量；单一可审计列表）
// 登记顺序即 id 顺序（只增不改序——序列化按 name 兼容，id 仅运行时索引）。
#include <cstddef>

#include "Components/BehaviorComponents.h"
#include "Components/CoreComponents.h"
#include "Components/GameplayComponents.h"
#include "Components/RenderComponents.h"
#include "ECS/ComponentRegistry.h"
#include "ECS/Scene.h"

namespace lemon::ecs {
namespace {

#define FIELD(C, f, t) { #f, FieldType::t, (uint16_t)offsetof(C, f), 0 }
#define FIELD_RT(C, f, t) { #f, FieldType::t, (uint16_t)offsetof(C, f), kFieldRuntime }

// ---- Core（id 0..4）----
constexpr FieldMeta kTransform2D[] = {
    FIELD(Transform2D, pos, Vec2), FIELD(Transform2D, rot, Float),
    FIELD(Transform2D, scale, Vec2)};
constexpr FieldMeta kVelocity[] = {FIELD(Velocity, v, Vec2)};
constexpr FieldMeta kHierarchy[] = {
    FIELD(Hierarchy, parent, EntityRef), FIELD(Hierarchy, firstChild, EntityRef),
    FIELD(Hierarchy, next, EntityRef), FIELD(Hierarchy, prev, EntityRef)};
constexpr FieldMeta kMeta[] = {
    FIELD(Meta, prefabId, UInt64), FIELD(Meta, team, TeamRef),
    FIELD(Meta, layer, UInt16), FIELD(Meta, tag, Blob24)};
constexpr FieldMeta kDestroyQueueTag[] = {};

// ---- Render（id 5..8）----
constexpr FieldMeta kSpriteRenderer[] = {
    FIELD(SpriteRenderer, spriteId, UInt32), FIELD(SpriteRenderer, colorRGBA, UInt32),
    FIELD(SpriteRenderer, sortOrder, Int16), FIELD(SpriteRenderer, sortingLayer, UInt8),
    FIELD(SpriteRenderer, flags, UInt8)};
constexpr FieldMeta kAnimator2D[] = {
    FIELD(Animator2D, clipId, UInt32), FIELD(Animator2D, time, Float),
    FIELD(Animator2D, speed, Float), FIELD(Animator2D, loop, UInt8),
    FIELD(Animator2D, playOnStart, UInt8), FIELD(Animator2D, curFrame, UInt16)};
constexpr FieldMeta kParticleEmitterRef[] = {
    FIELD(ParticleEmitterRef, emitterId, UInt32),
    FIELD(ParticleEmitterRef, playing, UInt8)};
constexpr FieldMeta kSortingOverride[] = {FIELD(SortingOverride, order, Int16)};

// ---- Behavior（id 9..20）----
constexpr FieldMeta kHealth[] = {FIELD(Health, max, Float), FIELD(Health, cur, Float),
                                 FIELD_RT(Health, iFrames, Float)};
constexpr FieldMeta kMover[] = {FIELD(Mover, speed, Float)};
constexpr FieldMeta kPatrol[] = {FIELD(Patrol, a, Vec2), FIELD(Patrol, b, Vec2),
                                 FIELD(Patrol, pauseTime, Float),
                                 FIELD(Patrol, headingToB, UInt8)};
constexpr FieldMeta kChase[] = {
    FIELD(Chase, speed, Float), FIELD(Chase, aggroRange, Float),
    FIELD(Chase, keepRange, Float), FIELD(Chase, targetTeam, TeamRef),
    FIELD_RT(Chase, target, EntityRef)};
constexpr FieldMeta kFlee[] = {FIELD(Flee, speed, Float), FIELD(Flee, range, Float)};
constexpr FieldMeta kShooter[] = {
    FIELD(Shooter, projectileId, UInt32), FIELD(Shooter, interval, Float),
    FIELD(Shooter, range, Float), FIELD(Shooter, targetTeam, TeamRef),
    FIELD_RT(Shooter, cooldown, Float), FIELD_RT(Shooter, target, EntityRef)};
constexpr FieldMeta kProjectile[] = {
    FIELD(Projectile, speed, Float), FIELD(Projectile, lifetime, Float),
    FIELD(Projectile, damage, Float), FIELD_RT(Projectile, age, Float),
    FIELD(Projectile, pierce, UInt8), FIELD(Projectile, homing, UInt8),
    FIELD_RT(Projectile, hits, UInt16)};
constexpr FieldMeta kSpawner[] = {
    FIELD(Spawner, prefabId, UInt32), FIELD(Spawner, interval, Float),
    FIELD(Spawner, burst, UInt16), FIELD(Spawner, range, Float),
    FIELD(Spawner, maxAlive, UInt32), FIELD(Spawner, spawnTeam, TeamRef),
    FIELD_RT(Spawner, cooldown, Float)};
constexpr FieldMeta kHazard[] = {FIELD(Hazard, dps, Float),
                                 FIELD(Hazard, tickInterval, Float),
                                 FIELD_RT(Hazard, tickPhase, Float)};
constexpr FieldMeta kCollectible[] = {FIELD(Collectible, kind, UInt8),
                                      FIELD(Collectible, magnetRadius, Float)};
constexpr FieldMeta kTrigger2D[] = {
    FIELD(Trigger2D, triggerId, UInt32), FIELD(Trigger2D, once, UInt8),
    FIELD_RT(Trigger2D, inside, UInt8), FIELD_RT(Trigger2D, fired, UInt8),
    FIELD(Trigger2D, radius, Float)};
constexpr FieldMeta kKnockback[] = {FIELD(Knockback, impulse, Vec2),
                                    FIELD(Knockback, decay, Float)};

// ---- Gameplay（id 21..26）----
constexpr FieldMeta kStats[] = {
    FIELD(Stats, moveSpeed, Float), FIELD(Stats, attack, Float),
    FIELD(Stats, defense, Float),   FIELD(Stats, critRate, Float),
    FIELD(Stats, critDmg, Float),   FIELD(Stats, pickupRadius, Float),
    FIELD(Stats, luck, Float)};
// StatusEffects/Inventory 的定长数组段由 codec 特判（按 count 截断），不逐槽登记
constexpr FieldMeta kStatusEffects[] = {FIELD(StatusEffects, count, UInt8)};
constexpr FieldMeta kInventory[] = {FIELD(Inventory, count, UInt8),
                                    FIELD(Inventory, gold, UInt32)};
constexpr FieldMeta kEquipment[] = {
    FIELD(Equipment, weaponId, UInt32), FIELD(Equipment, armorId, UInt32),
    FIELD(Equipment, relicIds, UInt32)};
constexpr FieldMeta kXpProgress[] = {FIELD(XpProgress, xp, Float),
                                     FIELD(XpProgress, xpToNext, Float),
                                     FIELD(XpProgress, level, UInt32)};
constexpr FieldMeta kIncrementalState[] = {
    FIELD(IncrementalState, rate, Double), FIELD(IncrementalState, multiplier, Double),
    FIELD(IncrementalState, cached, Double)};

#undef FIELD
#undef FIELD_RT

// ---- 定长数组段（元素字段表 + 段登记；序列化/状态哈希共用，见 ArraySegMeta）----
constexpr FieldMeta kStatusInst[] = {
    { "id", FieldType::UInt16, (uint16_t)offsetof(StatusInst, id), 0 },
    { "stacks", FieldType::UInt16, (uint16_t)offsetof(StatusInst, stacks), 0 },
    { "remain", FieldType::Float, (uint16_t)offsetof(StatusInst, remain), 0 },
    { "source", FieldType::UInt32, (uint16_t)offsetof(StatusInst, source), 0 }};
constexpr FieldMeta kItemStack[] = {
    { "itemId", FieldType::UInt32, (uint16_t)offsetof(ItemStack, itemId), 0 },
    { "count", FieldType::UInt16, (uint16_t)offsetof(ItemStack, count), 0 }};
#define SEG_OFF(C, f) (uint16_t)offsetof(C, f)
constexpr ArraySegMeta kStatusEffectsSeg = {
    "StatusEffects", "active", SEG_OFF(StatusEffects, active),
    (uint16_t)sizeof(StatusInst), SEG_OFF(StatusEffects, count), 4,
    kStatusInst, (uint16_t)(sizeof(kStatusInst) / sizeof(FieldMeta))};
constexpr ArraySegMeta kInventorySeg = {
    "Inventory", "items", SEG_OFF(Inventory, items),
    (uint16_t)sizeof(ItemStack), SEG_OFF(Inventory, count), 16,
    kItemStack, (uint16_t)(sizeof(kItemStack) / sizeof(FieldMeta))};
constexpr ArraySegMeta kEquipmentSeg = {
    "Equipment", "relicIds", SEG_OFF(Equipment, relicIds),
    (uint16_t)sizeof(uint32_t), 0xFFFF, 3, nullptr, 0}; // 定长标量数组
#undef SEG_OFF

// 运行时构造钩子（模板自动生成；SceneArchive 按元数据访问组件）
template <typename C>
bool HasComponent(Scene& s, Entity e) {
    return s.Has<C>(e);
}
template <typename C>
void* EmplaceComponent(Scene& s, Entity e) {
    return (void*)&s.Emplace<C>(e);
}
template <typename C>
const void* ReadComponent(Scene& s, Entity e) {
    return (const void*)s.TryGet<C>(e);
}
template <typename C>
void ForEachComponent(Scene& s, void (*cb)(Entity, const void*, void*), void* ctx) {
    auto view = s.View<C>();
    if constexpr (std::is_empty_v<C>) { // tag 组件：each() 只解构出实体
        for (auto ent : view) cb(Scene::FromEntt(ent), nullptr, ctx);
    } else {
        for (auto [ent, comp] : view.each())
            cb(Scene::FromEntt(ent), (const void*)&comp, ctx);
    }
}

#define REGISTER(Name, fields)                                                       \
    reg.Register({#Name, 0, (uint16_t)(sizeof(fields) / sizeof(FieldMeta)),           \
                  (uint32_t)sizeof(Name), fields, nullptr, HasComponent<Name>,         \
                  EmplaceComponent<Name>, ReadComponent<Name>,                        \
                  ForEachComponent<Name>});
#define REGISTER_SEG(Name, fields, seg)                                              \
    reg.Register({#Name, 0, (uint16_t)(sizeof(fields) / sizeof(FieldMeta)),           \
                  (uint32_t)sizeof(Name), fields, &seg, HasComponent<Name>,            \
                  EmplaceComponent<Name>, ReadComponent<Name>,                        \
                  ForEachComponent<Name>});

} // namespace

void RegisterAllComponents() {
    static bool registered = false;
    if (registered) return;
    registered = true;

    auto& reg = ComponentRegistry::Instance();
    REGISTER(Transform2D, kTransform2D)
    REGISTER(Velocity, kVelocity)
    REGISTER(Hierarchy, kHierarchy)
    REGISTER(Meta, kMeta)
    REGISTER(DestroyQueueTag, kDestroyQueueTag)
    REGISTER(SpriteRenderer, kSpriteRenderer)
    REGISTER(Animator2D, kAnimator2D)
    REGISTER(ParticleEmitterRef, kParticleEmitterRef)
    REGISTER(SortingOverride, kSortingOverride)
    REGISTER(Health, kHealth)
    REGISTER(Mover, kMover)
    REGISTER(Patrol, kPatrol)
    REGISTER(Chase, kChase)
    REGISTER(Flee, kFlee)
    REGISTER(Shooter, kShooter)
    REGISTER(Projectile, kProjectile)
    REGISTER(Spawner, kSpawner)
    REGISTER(Hazard, kHazard)
    REGISTER(Collectible, kCollectible)
    REGISTER(Trigger2D, kTrigger2D)
    REGISTER(Knockback, kKnockback)
    REGISTER(Stats, kStats)
    REGISTER_SEG(StatusEffects, kStatusEffects, kStatusEffectsSeg)
    REGISTER_SEG(Inventory, kInventory, kInventorySeg)
    REGISTER_SEG(Equipment, kEquipment, kEquipmentSeg)
    REGISTER(XpProgress, kXpProgress)
    REGISTER(IncrementalState, kIncrementalState)
#undef REGISTER
#undef REGISTER_SEG
}

} // namespace lemon::ecs
