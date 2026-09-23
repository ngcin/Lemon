// Lemon 引擎 — 组件目录登记表（03 文档 §3 全量；单一可审计列表）
// 登记顺序即 id 顺序（只增不改序——序列化按 name 兼容，id 仅运行时索引）。
// M4.1 起：每组件可附 FieldEditorMeta 平行表（编辑器控件元数据，内核 #6；
// nullptr = 全默认。布局探针协议不受影响——探针只镜像运行时位）。
#include <cstddef>
#include <iterator>
#include <new>

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

// 编辑器元数据行（FieldEditorMeta 平行表，与字段表等长）
#define ED {}
#define ED_DEG(tip) { FieldHint::Degree, 0, 0, nullptr, 0, tip }
#define ED_COLOR { FieldHint::ColorHex }
#define ED_BOOL8 { FieldHint::Bool8 }
#define ED_RANGE(lo, hi) { FieldHint::Range, lo, hi }
#define ED_HIDE { FieldHint::Hide }
#define ED_TIP(tip) { FieldHint::None, 0, 0, nullptr, 0, tip }
#define ED_RESET { FieldHint::Reset }
#define ED_ASSET(tip) { FieldHint::AssetRef, 0, 0, nullptr, 0, tip }
#define ED_CLIPREF(tip) { FieldHint::ClipRef, 0, 0, nullptr, 0, tip } // M5 批③

// ---- Core（id 0..4）----
constexpr FieldMeta kTransform2D[] = {
    FIELD(Transform2D, pos, Vec2), FIELD(Transform2D, rot, Float),
    FIELD(Transform2D, scale, Vec2)};
constexpr FieldEditorMeta kEdTransform2D[] = {
    ED_RESET, ED_DEG("旋转角（弧度存储，按角度编辑）"), ED};
constexpr FieldMeta kVelocity[] = {FIELD(Velocity, v, Vec2)};
constexpr FieldMeta kHierarchy[] = {
    FIELD(Hierarchy, parent, EntityRef), FIELD(Hierarchy, firstChild, EntityRef),
    FIELD(Hierarchy, next, EntityRef), FIELD(Hierarchy, prev, EntityRef)};
constexpr FieldEditorMeta kEdHierarchy[] = {
    ED_TIP("父实体（ECS/Hierarchy.h 维护，勿手改）"), ED_HIDE, ED_HIDE, ED_HIDE};
constexpr FieldMeta kMeta[] = {
    FIELD(Meta, prefabId, UInt64), FIELD(Meta, team, TeamRef),
    FIELD(Meta, layer, UInt16), FIELD(Meta, tag, Blob24),
    FIELD(Meta, guid, UInt64)};
constexpr FieldEditorMeta kEdMeta[] = {
    ED_TIP("Prefab 回链（0 = 非实例）"), ED, ED_TIP("碰撞/过滤层位索引 [0,16)"),
    ED_TIP("实体短名（Hierarchy 显示）"), ED_HIDE};
constexpr FieldMeta kDestroyQueueTag[] = {};

// ---- Render（id 5..8）----
constexpr FieldMeta kSpriteRenderer[] = {
    FIELD(SpriteRenderer, spriteId, UInt32), FIELD(SpriteRenderer, colorRGBA, UInt32),
    FIELD(SpriteRenderer, sortOrder, Int16), FIELD(SpriteRenderer, sortingLayer, UInt8),
    FIELD(SpriteRenderer, flags, UInt8)};
constexpr FieldEditorMeta kEdSpriteRenderer[] = {
    ED_ASSET("精灵资产槽（AssetBrowser 拖入 / 下拉选择；M4.4 接通）"), ED_COLOR, ED, ED,
    ED_TIP("bit2 enabled / bit0 flipX / bit1 flipY")};
constexpr FieldMeta kAnimator2D[] = {
    FIELD(Animator2D, clipId, UInt32), FIELD(Animator2D, time, Float),
    FIELD(Animator2D, speed, Float), FIELD(Animator2D, loop, UInt8),
    FIELD(Animator2D, playOnStart, UInt8), FIELD(Animator2D, curFrame, UInt16)};
constexpr FieldEditorMeta kEdAnimator2D[] = {
    ED_CLIPREF("clip 资产槽（.clip GUID 低 32 位；下拉/拖入。0 = 无 clip，走 M2 纯计时）"),
    ED_RANGE(0.0f, 100.0f), ED_RANGE(0.0f, 100.0f),
    ED_BOOL8, ED_BOOL8, ED_TIP("运行时帧号（clip 帧映射自动写；playOnStart=0 = 暂停开关）")};
constexpr FieldMeta kParticleEmitterRef[] = {
    FIELD(ParticleEmitterRef, emitterId, UInt32),
    FIELD(ParticleEmitterRef, playing, UInt8)};
constexpr FieldEditorMeta kEdParticleEmitterRef[] = {ED, ED_BOOL8};
constexpr FieldMeta kSortingOverride[] = {FIELD(SortingOverride, order, Int16)};

// ---- Behavior（id 9..20）----
constexpr FieldMeta kHealth[] = {FIELD(Health, max, Float), FIELD(Health, cur, Float),
                                 FIELD_RT(Health, iFrames, Float),
                                 FIELD(Health, iframeWindow, Float)};
constexpr FieldEditorMeta kEdHealth[] = {ED_RANGE(1.0f, 1e6f), ED_RANGE(0.0f, 1e6f), ED,
                                         ED_RANGE(0.0f, 2.0f)};
constexpr FieldMeta kMover[] = {FIELD(Mover, speed, Float)};
constexpr FieldEditorMeta kEdMover[] = {ED_RANGE(0.0f, 4096.0f)};
constexpr FieldMeta kPatrol[] = {FIELD(Patrol, a, Vec2), FIELD(Patrol, b, Vec2),
                                 FIELD(Patrol, pauseTime, Float),
                                 FIELD(Patrol, headingToB, UInt8)};
constexpr FieldEditorMeta kEdPatrol[] = {ED, ED, ED_RANGE(0.0f, 600.0f), ED_BOOL8};
constexpr FieldMeta kChase[] = {
    FIELD(Chase, speed, Float), FIELD(Chase, aggroRange, Float),
    FIELD(Chase, keepRange, Float), FIELD(Chase, targetTeam, TeamRef),
    FIELD_RT(Chase, target, EntityRef)};
constexpr FieldEditorMeta kEdChase[] = {ED_RANGE(0.0f, 4096.0f), ED_RANGE(0.0f, 4096.0f),
                                        ED_RANGE(0.0f, 4096.0f), ED, ED};
constexpr FieldMeta kFlee[] = {FIELD(Flee, speed, Float), FIELD(Flee, range, Float)};
constexpr FieldEditorMeta kEdFlee[] = {ED_RANGE(0.0f, 4096.0f), ED_RANGE(0.0f, 4096.0f)};
constexpr FieldMeta kShooter[] = {
    FIELD(Shooter, projectileId, UInt32), FIELD(Shooter, interval, Float),
    FIELD(Shooter, range, Float), FIELD(Shooter, targetTeam, TeamRef),
    FIELD_RT(Shooter, cooldown, Float), FIELD_RT(Shooter, target, EntityRef)};
constexpr FieldEditorMeta kEdShooter[] = {ED, ED_RANGE(0.0f, 600.0f), ED_RANGE(0.0f, 4096.0f),
                                          ED, ED, ED};
constexpr FieldMeta kProjectile[] = {
    FIELD(Projectile, speed, Float), FIELD(Projectile, lifetime, Float),
    FIELD(Projectile, damage, Float), FIELD_RT(Projectile, age, Float),
    FIELD(Projectile, pierce, UInt8), FIELD(Projectile, homing, UInt8),
    FIELD_RT(Projectile, hits, UInt16),
    FIELD(Projectile, hitRadius, Float), FIELD(Projectile, knockback, Float),
    FIELD_RT(Projectile, hitHead, UInt8),
    // hitMemory[4]：数组无逐槽字段名，手动行（kStatusInst 同法；RT 不入档仅入状态哈希）
    { "hitMemory0", FieldType::UInt32,
      (uint16_t)(offsetof(Projectile, hitMemory) + 0 * sizeof(uint32_t)), kFieldRuntime },
    { "hitMemory1", FieldType::UInt32,
      (uint16_t)(offsetof(Projectile, hitMemory) + 1 * sizeof(uint32_t)), kFieldRuntime },
    { "hitMemory2", FieldType::UInt32,
      (uint16_t)(offsetof(Projectile, hitMemory) + 2 * sizeof(uint32_t)), kFieldRuntime },
    { "hitMemory3", FieldType::UInt32,
      (uint16_t)(offsetof(Projectile, hitMemory) + 3 * sizeof(uint32_t)), kFieldRuntime }};
constexpr FieldEditorMeta kEdProjectile[] = {ED_RANGE(0.0f, 8192.0f), ED_RANGE(0.0f, 600.0f),
                                             ED_RANGE(0.0f, 1e6f), ED, ED_BOOL8, ED_BOOL8, ED,
                                             ED_RANGE(0.0f, 256.0f), ED_RANGE(0.0f, 2048.0f),
                                             ED_HIDE, ED_HIDE, ED_HIDE, ED_HIDE, ED_HIDE};
constexpr FieldMeta kSpawner[] = {
    FIELD(Spawner, prefabId, UInt32), FIELD(Spawner, interval, Float),
    FIELD(Spawner, burst, UInt16), FIELD(Spawner, range, Float),
    FIELD(Spawner, maxAlive, UInt32), FIELD(Spawner, spawnTeam, TeamRef),
    FIELD_RT(Spawner, cooldown, Float)};
constexpr FieldEditorMeta kEdSpawner[] = {ED, ED_RANGE(0.0f, 600.0f), ED, ED_RANGE(0.0f, 4096.0f),
                                          ED, ED, ED};
constexpr FieldMeta kHazard[] = {FIELD(Hazard, dps, Float),
                                 FIELD(Hazard, tickInterval, Float),
                                 FIELD_RT(Hazard, tickPhase, Float),
                                 FIELD(Hazard, radius, Float)};
constexpr FieldEditorMeta kEdHazard[] = {ED_RANGE(0.0f, 1e6f), ED_RANGE(0.0f, 60.0f), ED,
                                         ED_RANGE(0.0f, 4096.0f)};
static constexpr const char* kCollectibleKindNames[] = {"Gem", "Coin", "Heart"};
constexpr FieldMeta kCollectible[] = {FIELD(Collectible, kind, UInt8),
                                      FIELD_RT(Collectible, state, UInt8),
                                      FIELD(Collectible, magnetRadius, Float),
                                      FIELD(Collectible, magnetSpeed, Float),
                                      FIELD(Collectible, value, Float),
                                      FIELD_RT(Collectible, target, EntityRef)};
constexpr FieldEditorMeta kEdCollectible[] = {
    {FieldHint::Enum, 0, 0, kCollectibleKindNames, 3, nullptr},
    ED_HIDE,
    ED_RANGE(0.0f, 4096.0f), ED_RANGE(0.0f, 2048.0f), ED_RANGE(0.0f, 1e6f),
    ED_HIDE};
constexpr FieldMeta kTrigger2D[] = {
    FIELD(Trigger2D, triggerId, UInt32), FIELD(Trigger2D, once, UInt8),
    FIELD_RT(Trigger2D, inside, UInt8), FIELD_RT(Trigger2D, fired, UInt8),
    FIELD(Trigger2D, radius, Float)};
constexpr FieldEditorMeta kEdTrigger2D[] = {ED, ED_BOOL8, ED, ED, ED_RANGE(0.0f, 4096.0f)};
constexpr FieldMeta kKnockback[] = {
    FIELD(Knockback, impulse, Vec2),
    FIELD(Knockback, decay, Float)};
constexpr FieldEditorMeta kEdKnockback[] = {ED, ED_RANGE(0.0f, 600.0f)};

// ---- Gameplay（id 21..26）----
constexpr FieldMeta kStats[] = {
    FIELD(Stats, moveSpeed, Float), FIELD(Stats, attack, Float),
    FIELD(Stats, defense, Float),   FIELD(Stats, critRate, Float),
    FIELD(Stats, critDmg, Float),   FIELD(Stats, pickupRadius, Float),
    FIELD(Stats, luck, Float)};
constexpr FieldEditorMeta kEdStats[] = {ED_RANGE(0.0f, 4096.0f), ED_RANGE(0.0f, 1e6f),
                                        ED_RANGE(0.0f, 1e6f),   ED_RANGE(0.0f, 1.0f),
                                        ED_RANGE(0.0f, 100.0f), ED_RANGE(0.0f, 4096.0f),
                                        ED_RANGE(-100.0f, 100.0f)};
// StatusEffects/Inventory 的定长数组段由 codec 特判（按 count 截断），不逐槽登记
constexpr FieldMeta kStatusEffects[] = {FIELD(StatusEffects, count, UInt8)};
constexpr FieldEditorMeta kEdStatusEffects[] = {
    ED_TIP("活跃效果数（系统维护；条目见下方数组段）")};
constexpr FieldMeta kInventory[] = {FIELD(Inventory, count, UInt8),
                                    FIELD(Inventory, gold, UInt32)};
constexpr FieldEditorMeta kEdInventory[] = {ED_TIP("物品槽数（系统维护）"), ED};
// relicIds 只由数组段 kEquipmentSeg 驱动（[ISSUE-8] 曾同时登记普通字段 →
// 同名键读档时字段循环对数组抛 type_error → 每次必发假告警）
constexpr FieldMeta kEquipment[] = {
    FIELD(Equipment, weaponId, UInt32), FIELD(Equipment, armorId, UInt32)};
constexpr FieldMeta kXpProgress[] = {
    FIELD(XpProgress, xp, Float),
    FIELD(XpProgress, xpToNext, Float),
    FIELD(XpProgress, level, UInt32)};
constexpr FieldEditorMeta kEdXpProgress[] = {ED_RANGE(0.0f, 1e9f), ED_RANGE(1.0f, 1e9f),
                                             ED_RANGE(1.0f, 1e6f)};
constexpr FieldMeta kIncrementalState[] = {
    FIELD(IncrementalState, rate, Double), FIELD(IncrementalState, multiplier, Double),
    FIELD(IncrementalState, cached, Double)};

// ---- M5 批②（id 27..；登记序=组件 id 只增不改序）----
// 波次表本体在数组段 kWaveDirectorSeg（16 波 × 76B 元素）；RT 行手工命名仿
// hitMem0..3 模式（数组无逐槽字段名）。字段顺序 = C# LayoutTables 1:1，禁重排。
constexpr FieldMeta kWaveDirector[] = {
    FIELD(WaveDirector, spawnTeam, TeamRef), FIELD(WaveDirector, capAlive, Int32),
    FIELD(WaveDirector, waveCount, UInt8),
    FIELD_RT(WaveDirector, time, Float), FIELD_RT(WaveDirector, waveIndex, UInt8),
    { "cd0", FieldType::Float, (uint16_t)(offsetof(WaveDirector, waveCooldown) + 0 * sizeof(float)), kFieldRuntime },
    { "cd1", FieldType::Float, (uint16_t)(offsetof(WaveDirector, waveCooldown) + 1 * sizeof(float)), kFieldRuntime },
    { "cd2", FieldType::Float, (uint16_t)(offsetof(WaveDirector, waveCooldown) + 2 * sizeof(float)), kFieldRuntime },
    { "cd3", FieldType::Float, (uint16_t)(offsetof(WaveDirector, waveCooldown) + 3 * sizeof(float)), kFieldRuntime },
    { "spawned0", FieldType::UInt16, (uint16_t)(offsetof(WaveDirector, waveSpawned) + 0 * sizeof(uint16_t)), kFieldRuntime },
    { "spawned1", FieldType::UInt16, (uint16_t)(offsetof(WaveDirector, waveSpawned) + 1 * sizeof(uint16_t)), kFieldRuntime },
    { "spawned2", FieldType::UInt16, (uint16_t)(offsetof(WaveDirector, waveSpawned) + 2 * sizeof(uint16_t)), kFieldRuntime },
    { "spawned3", FieldType::UInt16, (uint16_t)(offsetof(WaveDirector, waveSpawned) + 3 * sizeof(uint16_t)), kFieldRuntime }};
constexpr FieldEditorMeta kEdWaveDirector[] = {
    ED, ED_RANGE(0, 1000000.0f),
    ED_TIP("有效波数 0..16（表格见下方数组段；期望 startTime 升序）"),
    ED_HIDE, ED_HIDE,
    ED_HIDE, ED_HIDE, ED_HIDE, ED_HIDE,
    ED_HIDE, ED_HIDE, ED_HIDE, ED_HIDE};

#undef FIELD
#undef FIELD_RT
#undef ED
#undef ED_DEG
#undef ED_COLOR
#undef ED_BOOL8
#undef ED_RANGE
#undef ED_HIDE
#undef ED_TIP
#undef ED_ASSET

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
// M5 批②：波次表段（元素 = WaveDef；entries[4] 单层数组段不支持嵌套 → 扁平
// e{0..3}{prefab,count,interval,range} 行，一处定义 16 波共用）
constexpr FieldMeta kWaveDefElem[] = {
    { "startTime", FieldType::Float, (uint16_t)offsetof(WaveDef, startTime), 0 },
    { "rampMult", FieldType::Float, (uint16_t)offsetof(WaveDef, rampMult), 0 },
    { "entryCount", FieldType::UInt8, (uint16_t)offsetof(WaveDef, entryCount), 0 },
#define WAVE_E(i, sfx, mem, t) { "e" #i #sfx, FieldType::t, \
    (uint16_t)(offsetof(WaveDef, entries) + i * sizeof(WaveEntry) + offsetof(WaveEntry, mem)), 0 }
    WAVE_E(0, prefab, prefabId, UInt32), WAVE_E(0, count, count, UInt16),
    WAVE_E(0, interval, interval, Float), WAVE_E(0, range, range, Float),
    WAVE_E(1, prefab, prefabId, UInt32), WAVE_E(1, count, count, UInt16),
    WAVE_E(1, interval, interval, Float), WAVE_E(1, range, range, Float),
    WAVE_E(2, prefab, prefabId, UInt32), WAVE_E(2, count, count, UInt16),
    WAVE_E(2, interval, interval, Float), WAVE_E(2, range, range, Float),
    WAVE_E(3, prefab, prefabId, UInt32), WAVE_E(3, count, count, UInt16),
    WAVE_E(3, interval, interval, Float), WAVE_E(3, range, range, Float)};
#undef WAVE_E
constexpr ArraySegMeta kWaveDirectorSeg = {
    "WaveDirector", "waves", SEG_OFF(WaveDirector, waves),
    (uint16_t)sizeof(WaveDef), SEG_OFF(WaveDirector, waveCount), 16,
    kWaveDefElem, (uint16_t)(sizeof(kWaveDefElem) / sizeof(FieldMeta))};
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
void* GetComponent(Scene& s, Entity e) {
    return (void*)s.TryGet<C>(e);
}
template <typename C>
void RemoveComponent(Scene& s, Entity e) {
    s.Remove<C>(e);
}
template <typename C>
void ConstructDefault(void* mem) {
    new (mem) C(); // 字段级重置的默认值来源（与 EmplaceComponent 同构造口径）
}
template <typename C>
uint32_t CountComponent(Scene& s) {
    return (uint32_t)s.View<C>().size();
}
template <typename C>
void ForEachComponentRange(Scene& s, uint32_t begin, uint32_t end,
                           void (*cb)(Entity, const void*, void*), void* ctx) {
    auto view = s.View<C>();
    if (end > view.size()) end = (uint32_t)view.size();
    auto it = view.begin();
    std::advance(it, begin);
    for (uint32_t i = begin; i < end; ++i, ++it) {
        auto ent = *it;
        if constexpr (std::is_empty_v<C>) cb(Scene::FromEntt(ent), nullptr, ctx);
        else cb(Scene::FromEntt(ent), (const void*)&view.template get<C>(ent), ctx);
    }
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
                  (uint32_t)sizeof(Name), fields, nullptr, nullptr,                   \
                  HasComponent<Name>, EmplaceComponent<Name>, ReadComponent<Name>,    \
                  GetComponent<Name>,                                                \
                  ForEachComponent<Name>, RemoveComponent<Name>,                      \
                  CountComponent<Name>, ForEachComponentRange<Name>, ConstructDefault<Name>});
#define REGISTER_ED(Name, fields, ed)                                                \
    reg.Register({#Name, 0, (uint16_t)(sizeof(fields) / sizeof(FieldMeta)),           \
                  (uint32_t)sizeof(Name), fields, ed, nullptr,                        \
                  HasComponent<Name>, EmplaceComponent<Name>, ReadComponent<Name>,    \
                  GetComponent<Name>,                                                \
                  ForEachComponent<Name>, RemoveComponent<Name>,                      \
                  CountComponent<Name>, ForEachComponentRange<Name>, ConstructDefault<Name>});
#define REGISTER_SEG(Name, fields, seg)                                              \
    reg.Register({#Name, 0, (uint16_t)(sizeof(fields) / sizeof(FieldMeta)),           \
                  (uint32_t)sizeof(Name), fields, nullptr, &seg,                      \
                  HasComponent<Name>, EmplaceComponent<Name>, ReadComponent<Name>,    \
                  GetComponent<Name>,                                                \
                  ForEachComponent<Name>, RemoveComponent<Name>,                      \
                  CountComponent<Name>, ForEachComponentRange<Name>, ConstructDefault<Name>});
// M5 批②：编辑器元数据 + 数组段并持（WaveDirector 首例）
#define REGISTER_ED_SEG(Name, fields, ed, seg)                                       \
    reg.Register({#Name, 0, (uint16_t)(sizeof(fields) / sizeof(FieldMeta)),           \
                  (uint32_t)sizeof(Name), fields, ed, &seg,                           \
                  HasComponent<Name>, EmplaceComponent<Name>, ReadComponent<Name>,    \
                  GetComponent<Name>,                                                \
                  ForEachComponent<Name>, RemoveComponent<Name>,                      \
                  CountComponent<Name>, ForEachComponentRange<Name>, ConstructDefault<Name>});

} // namespace

void RegisterAllComponents() {
    static bool registered = false;
    if (registered) return;
    registered = true;

    auto& reg = ComponentRegistry::Instance();
    REGISTER_ED(Transform2D, kTransform2D, kEdTransform2D)
    REGISTER(Velocity, kVelocity)
    REGISTER_ED(Hierarchy, kHierarchy, kEdHierarchy)
    REGISTER_ED(Meta, kMeta, kEdMeta)
    REGISTER(DestroyQueueTag, kDestroyQueueTag)
    REGISTER_ED(SpriteRenderer, kSpriteRenderer, kEdSpriteRenderer)
    REGISTER_ED(Animator2D, kAnimator2D, kEdAnimator2D)
    REGISTER_ED(ParticleEmitterRef, kParticleEmitterRef, kEdParticleEmitterRef)
    REGISTER(SortingOverride, kSortingOverride)
    REGISTER_ED(Health, kHealth, kEdHealth)
    REGISTER_ED(Mover, kMover, kEdMover)
    REGISTER_ED(Patrol, kPatrol, kEdPatrol)
    REGISTER_ED(Chase, kChase, kEdChase)
    REGISTER_ED(Flee, kFlee, kEdFlee)
    REGISTER_ED(Shooter, kShooter, kEdShooter)
    REGISTER_ED(Projectile, kProjectile, kEdProjectile)
    REGISTER_ED(Spawner, kSpawner, kEdSpawner)
    REGISTER_ED(Hazard, kHazard, kEdHazard)
    REGISTER_ED(Collectible, kCollectible, kEdCollectible)
    REGISTER_ED(Trigger2D, kTrigger2D, kEdTrigger2D)
    REGISTER_ED(Knockback, kKnockback, kEdKnockback)
    REGISTER_ED(Stats, kStats, kEdStats)
    REGISTER_SEG(StatusEffects, kStatusEffects, kStatusEffectsSeg)
    REGISTER_SEG(Inventory, kInventory, kInventorySeg)
    REGISTER_SEG(Equipment, kEquipment, kEquipmentSeg)
    REGISTER_ED(XpProgress, kXpProgress, kEdXpProgress)
    REGISTER(IncrementalState, kIncrementalState)
    REGISTER_ED_SEG(WaveDirector, kWaveDirector, kEdWaveDirector, kWaveDirectorSeg)
}

} // namespace lemon::ecs
