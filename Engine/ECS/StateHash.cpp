// Lemon 引擎 — 确定性状态哈希实现
#include "ECS/StateHash.h"

#include <cstring>
#include <utility>

#include "ECS/ComponentRegistry.h"

namespace lemon::ecs {
namespace {

struct Hasher {
    uint64_t h = 1469598103934665603ull; // FNV-1a offset basis

    void Bytes(const void* p, size_t n) {
        const uint8_t* b = (const uint8_t*)p;
        for (size_t i = 0; i < n; ++i) {
            h ^= b[i];
            h *= 1099511628211ull; // FNV prime
        }
    }
    void U64(uint64_t v) { Bytes(&v, 8); }
};

// 字段有效字节数（FieldType → 宽度；数组段特判见下）
size_t FieldWidth(FieldType t) {
    switch (t) {
    case FieldType::Float:
    case FieldType::Int32:
    case FieldType::UInt32: return 4;
    case FieldType::Double:
    case FieldType::UInt64: return 8;
    case FieldType::Int16:
    case FieldType::UInt16: return 2;
    case FieldType::Int8:
    case FieldType::UInt8:
    case FieldType::Bool: return 1;
    case FieldType::Vec2: return 8;
    case FieldType::EntityRef: return 8;
    case FieldType::TeamRef: return 4;
    case FieldType::Blob24: return 24;
    }
    return 0;
}

void HashComponent(Entity e, const void* comp, void* ctx) {
    auto* pair = (std::pair<Hasher, const ComponentMeta*>*)ctx;
    Hasher& h = pair->first;
    const ComponentMeta& m = *pair->second;

    h.U64(e.id);
    for (uint16_t f = 0; f < m.fieldCount; ++f) {
        const FieldMeta& fm = m.fields[f];
        h.Bytes((const char*)comp + fm.offset, FieldWidth(fm.type));
        h.Bytes(fm.name, std::strlen(fm.name)); // 字段名入哈希（schema 漂移即分歧）
    }

    // 定长数组段：按 count（或定长容量）哈希元素原始字节（M5 数组元数据化后并表）
    if (m.arraySeg) {
        const ArraySegMeta& seg = *m.arraySeg;
        uint32_t n = seg.maxCount;
        if (seg.countOffset != 0xFFFF) n = *(const uint8_t*)((const char*)comp + seg.countOffset);
        if (n > seg.maxCount) n = seg.maxCount;
        h.Bytes((const char*)comp + seg.offset, (size_t)seg.elemSize * n);
    }
}

} // namespace

uint64_t ComputeStateHash(Scene& scene) {
    auto& reg = ComponentRegistry::Instance();
    Hasher h;
    for (uint16_t id = 0; id < reg.Count(); ++id) {
        const ComponentMeta& m = reg.At(id);
        h.Bytes(m.name, std::strlen(m.name));
        std::pair<Hasher, const ComponentMeta*> ctx{h, &m};
        m.forEachFn(scene, HashComponent, &ctx);
        h = ctx.first;
    }
    return h.h;
}

} // namespace lemon::ecs
