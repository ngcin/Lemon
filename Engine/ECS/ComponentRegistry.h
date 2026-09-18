// Lemon 引擎 — 组件注册表（03 文档 §3：反射元数据，Inspector 与序列化共用）
// 设计：
//   * 每个登记组件在 ComponentCatalog.cpp 显式登记（可审计的单一列表，不玩
//     静态 registrar——规避跨编译单元初始化顺序坑）；
//   * 序列化按 name 匹配（.lscene 可读可 diff），运行时 Find 建 name→id 索引；
//   * 字段元数据含偏移与类型，codec 按类型遍历读写 POD；范围/控件元数据
//     （编辑器 Inspector 细化）在 M4 扩展，接口预留 flags 位。
#pragma once

#include <cstdint>
#include <vector>

#include "Core/Log.h"

namespace lemon::ecs {

enum class FieldType : uint8_t {
    Float,
    Double,
    Int32,
    UInt32,
    UInt64,
    Int16,
    UInt16,
    Int8,
    UInt8,
    Bool,
    Vec2,       // 2×float（lemon::Vec2）
    EntityRef,  // uint64_t Entity 句柄（存档时解析为 id 引用表）
    TeamRef,    // uint32_t Team id
    Blob24,     // char[24]（tag 等定长字符串）
};

struct FieldMeta {
    const char* name;
    FieldType type;
    uint16_t offset;
    uint32_t flags;
};

/// FieldMeta.flags 位：运行时态字段（目标缓存/计时相位等），序列化跳过
static constexpr uint32_t kFieldRuntime = 1u;

struct ComponentMeta {
    const char* name;      // "Transform2D"（序列化键，改名 = 格式变更）
    uint16_t id;           // 登记序（运行时索引）
    uint16_t fieldCount;
    uint32_t sizeOf;
    const FieldMeta* fields;
    // 运行时构造钩子（SceneArchive 按元数据读写组件，不逐组件手写 codec）
    bool (*hasFn)(class Scene&, Entity);
    void* (*emplaceFn)(class Scene&, Entity);      // 不存在时构造，返回组件指针
    const void* (*readFn)(class Scene&, Entity);   // 只读取址，不存在返回 nullptr
    // 全池遍历（StateHash/提取层；C 回调+ctx 避免模板穿透元数据层）
    void (*forEachFn)(class Scene&, void (*)(Entity, const void*, void*), void* ctx);
};

class ComponentRegistry {
public:
    /// 登记入口（ComponentCatalog.cpp 在首次 Instance() 后顺序调用）
    uint16_t Register(const ComponentMeta& meta);

    const ComponentMeta* Find(const char* name) const;
    const ComponentMeta& At(uint16_t id) const;
    uint16_t Count() const { return (uint16_t)metas_.size(); }

    static ComponentRegistry& Instance(); // Meyers 单例：先于任何登记调用构造

private:
    ComponentRegistry() = default;
    std::vector<ComponentMeta> metas_;
};

/// 组件目录登记入口（定义在 Components/ComponentCatalog.cpp）
void RegisterAllComponents();

// ------------------------------------------------------------- inline 实现 --
inline ComponentRegistry& ComponentRegistry::Instance() {
    static ComponentRegistry inst;
    return inst;
}

inline uint16_t ComponentRegistry::Register(const ComponentMeta& meta) {
    LEMON_ASSERT(Find(meta.name) == nullptr, "component duplicate: %s", meta.name);
    ComponentMeta& slot = metas_.emplace_back(meta);
    slot.id = (uint16_t)(metas_.size() - 1);
    return slot.id;
}

inline const ComponentMeta* ComponentRegistry::Find(const char* name) const {
    for (const ComponentMeta& m : metas_)
        if (__builtin_strcmp(m.name, name) == 0) return &m;
    return nullptr;
}

inline const ComponentMeta& ComponentRegistry::At(uint16_t id) const {
    LEMON_ASSERT(id < metas_.size(), "component id out of range");
    return metas_[id];
}

} // namespace lemon::ecs
