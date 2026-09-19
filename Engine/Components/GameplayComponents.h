// Lemon 引擎 — 组件目录 · Gameplay 组（03 文档 §3.4：RPG/数值面）
// v1 说明：文档的 SmallVec 字段（StatusEffects/Inventory）M2 落为定长数组 + count
// （容量即文档内联容量 4/16），保持全组件 trivially copyable（状态哈希前提）；
// M5 若需扩容再引 SmallVector 并同步迁移存档 schema。
#pragma once

#include <cstdint>
#include <type_traits>

namespace lemon::ecs {

struct Stats {
    float moveSpeed = 100.0f;
    float attack = 10.0f;
    float defense = 0.0f;
    float critRate = 0.05f;
    float critDmg = 1.5f;
    float pickupRadius = 32.0f;
    float luck = 0.0f;
};

struct StatusInst {
    uint16_t id = 0;       // 效果资产 id
    uint16_t stacks = 1;
    float remain = 0.0f;   // 剩余秒
    uint32_t source = 0;   // 来源实体（Entity.id 低 32 位）
};

struct StatusEffects {
    StatusInst active[4]{};
    uint8_t count = 0;
    uint8_t _pad[3] = {};
};

struct ItemStack {
    uint32_t itemId = 0;
    uint16_t count = 1;
    uint16_t _pad = 0;
};

struct Inventory {
    ItemStack items[16]{};
    uint8_t count = 0;
    uint8_t _pad[3] = {};
    uint32_t gold = 0;
};

struct Equipment {
    uint32_t weaponId = 0;
    uint32_t armorId = 0;
    uint32_t relicIds[3] = {};
};

struct XpProgress {
    float xp = 0.0f;
    float xpToNext = 100.0f;
    uint32_t level = 1;
    uint32_t _pad = 0;
};

struct IncrementalState {   // 增量挂机（M6+，占位登记）
    double rate = 0.0;
    double multiplier = 1.0;
    double cached = 0.0;
};

// ---- 布局冻结（M3 桥侧 blittable 前提：C# 镜像 struct 与此逐字节对齐，改动=破回放）----
// StatusInst/ItemStack 是数组段元素（ArraySegMeta.elemFields 依赖其布局），一并冻结。
static_assert(std::is_trivially_copyable_v<Stats> && sizeof(Stats) == 28, "Stats 布局冻结");
static_assert(std::is_trivially_copyable_v<StatusInst> && sizeof(StatusInst) == 12, "StatusInst 布局冻结");
static_assert(std::is_trivially_copyable_v<StatusEffects> && sizeof(StatusEffects) == 52, "StatusEffects 布局冻结");
static_assert(std::is_trivially_copyable_v<ItemStack> && sizeof(ItemStack) == 8, "ItemStack 布局冻结");
static_assert(std::is_trivially_copyable_v<Inventory> && sizeof(Inventory) == 136, "Inventory 布局冻结");
static_assert(std::is_trivially_copyable_v<Equipment> && sizeof(Equipment) == 20, "Equipment 布局冻结");
static_assert(std::is_trivially_copyable_v<XpProgress> && sizeof(XpProgress) == 16, "XpProgress 布局冻结");
static_assert(std::is_trivially_copyable_v<IncrementalState> && sizeof(IncrementalState) == 24, "IncrementalState 布局冻结");

} // namespace lemon::ecs
