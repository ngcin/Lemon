// Lemon 引擎 — 组件注册表（03 文档 §3：反射元数据，Inspector 与序列化共用）
// 设计：
//   * 每个登记组件在 ComponentCatalog.cpp 显式登记（可审计的单一列表，不玩
//     静态 registrar——规避跨编译单元初始化顺序坑）；
//   * 序列化按 name 匹配（.scene 可读可 diff），运行时 Find 建 name→id 索引；
//   * 字段元数据含偏移与类型，codec 按类型遍历读写 POD；范围/控件元数据
//     （编辑器 Inspector 细化）在 M4 扩展，接口预留 flags 位。
#pragma once

#include <cstdint>
#include <cstring> // std::strcmp（MSVC 无 __builtin_*；Windows 阻断项③，07 §3.6）
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

// ---- 编辑器元数据（M4.1 内核 #6；独立平行表，不占 FieldMeta.flags——布局探针
// C# 侧只镜像运行时位，编辑器提示留在编辑域。Inspector 按此渲染控件）----
enum class FieldHint : uint32_t {
    None     = 0,
    Degree   = 1u << 0,  // 弧度存储，Inspector 以角度显示/编辑
    ColorHex = 1u << 1,  // UInt32 颜色（ColorEdit 控件）
    Bool8    = 1u << 2,  // UInt8 布尔语义（Checkbox 控件）
    Enum     = 1u << 3,  // 整数枚举（enumNames 名表，值 = 下标）
    AssetRef = 1u << 4,  // 资产引用（GUID 槽控件；M4.4 接通）
    Hide     = 1u << 5,  // 不进 Inspector（池内冗余字段）
    Range    = 1u << 6,  // 数值夹取 [rangeMin, rangeMax]（Drag 控件）
    Reset    = 1u << 7,  // 字段级重置按钮（Inspector 值列尾；恢复默认构造值）
    ClipRef  = 1u << 8,  // clip 资产槽（uint32 = .anim 资产 GUID 低 32 位；M5 批③）
    AnimSetRef    = 1u << 9,  // 动画集槽（uint64 = .override/.override 资产 GUID；T3d 批①）
    ControllerRef = 1u << 10, // 状态机槽（uint64 = .controller 资产 GUID；T3d 批①）
    RmlRef        = 1u << 11, // .rml 文档槽（uint64 = 资产 GUID 全量；M6b 批③d 前置）
};
inline constexpr FieldHint operator|(FieldHint a, FieldHint b) {
    return (FieldHint)((uint32_t)a | (uint32_t)b);
}
inline constexpr bool HasHint(FieldHint h, FieldHint bit) {
    return ((uint32_t)h & (uint32_t)bit) != 0;
}

struct FieldEditorMeta {
    FieldHint hints = FieldHint::None;
    float rangeMin = 0.0f, rangeMax = 0.0f;
    const char* const* enumNames = nullptr; // Enum：名表（enumCount 项，枚举值 = 下标）
    uint32_t enumCount = 0;
    const char* tooltip = nullptr;          // 悬浮提示（nullptr = 无）
};

/// 定长数组段元数据（StatusEffects.active / Inventory.items / Equipment.relicIds
/// 这类"POD 元素定长数组 + count"字段；M2 特判路径，M5 数组元数据并入字段表后移除）
struct ArraySegMeta {
    const char* comp;      // 所属组件名
    const char* field;     // 序列化键名
    uint16_t offset;       // 数组首元素偏移
    uint16_t elemSize;     // sizeof(元素)
    uint16_t countOffset;  // uint8 count 字段偏移；0xFFFF = 定长（无 count，用 maxCount）
    uint16_t maxCount;     // 容量
    // 元素内字段表（元素内偏移）；nullptr = uint32 标量数组（json 数值数组）
    const FieldMeta* elemFields;
    uint16_t elemFieldCount;
};

struct ComponentMeta {
    const char* name;      // "Transform2D"（序列化键，改名 = 格式变更）
    uint16_t id;           // 登记序（运行时索引）
    uint16_t fieldCount;
    uint32_t sizeOf;
    const FieldMeta* fields;
    const FieldEditorMeta* editorMeta; // 编辑器元数据平行表（与 fields 等长；nullptr = 全默认）
    const ArraySegMeta* arraySeg; // 数组段（无则 nullptr）
    // 运行时构造钩子（SceneArchive 按元数据读写组件，不逐组件手写 codec）
    bool (*hasFn)(class Scene&, Entity);
    void* (*emplaceFn)(class Scene&, Entity);      // 不存在时构造，返回组件指针
    const void* (*readFn)(class Scene&, Entity);   // 只读取址，不存在返回 nullptr
    // 可写取址（存在时返回指针，否则 nullptr；Inspector 编辑/M4.2 Undo 记录用）
    void* (*getFn)(class Scene&, Entity) = nullptr;
    // 全池遍历（StateHash/提取层；C 回调+ctx 避免模板穿透元数据层）
    void (*forEachFn)(class Scene&, void (*)(Entity, const void*, void*), void* ctx);
    // 移除组件（M3 脚本结构命令缓冲用；nullptr = 不支持）
    void (*removeFn)(class Scene&, Entity) = nullptr;
    // 池大小（M3 桥并行切段构造用；nullptr = 不支持）
    uint32_t (*countFn)(class Scene&) = nullptr;
    // 按池序分段遍历 [begin,end)（M3 桥并行构造；nullptr = 不支持）
    void (*forEachRangeFn)(class Scene&, uint32_t, uint32_t,
                           void (*)(Entity, const void*, void*), void*) = nullptr;
    // 默认构造到调用方缓冲（placement-new；Inspector 字段级重置读默认值；
    // nullptr = 不支持。构造口径与 emplaceFn 一致）
    void (*constructFn)(void* mem) = nullptr;
};

class ComponentRegistry {
public:
    /// 登记入口（ComponentCatalog.cpp 在首次 Instance() 后顺序调用）
    uint16_t Register(const ComponentMeta& meta);

    const ComponentMeta* Find(const char* name) const;
    const ComponentMeta& At(uint16_t id) const;
    /// 按组件名查数组段（M2 特判路径；无返回 nullptr）
    static const ArraySegMeta* FindArraySeg(const char* compName);
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
        if (std::strcmp(m.name, name) == 0) return &m;
    return nullptr;
}

inline const ComponentMeta& ComponentRegistry::At(uint16_t id) const {
    LEMON_ASSERT(id < metas_.size(), "component id out of range");
    return metas_[id];
}

inline const ArraySegMeta* ComponentRegistry::FindArraySeg(const char* compName) {
    const ComponentMeta* m = Instance().Find(compName);
    return m ? m->arraySeg : nullptr;
}

} // namespace lemon::ecs
