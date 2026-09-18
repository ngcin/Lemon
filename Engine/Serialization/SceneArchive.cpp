// Lemon 引擎 — 场景存取实现（nlohmann/json 只进本 .cpp，头文件零泄漏）
#include "Serialization/SceneArchive.h"

#include <algorithm>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "Core/Log.h"
#include "Core/Math.h"
#include "ECS/ComponentRegistry.h"

namespace lemon::ecs {
namespace {

using Json = nlohmann::json;

// ---------------------------------------------------------- 字段 codec ----
// 读：json 值 → 按 FieldMeta.offset 写入组件内存（类型表驱动，无逐组件手写）
bool ReadField(const Json& src, const FieldMeta& f, char* comp, const Entity* remap,
               size_t remapCount) {
    switch (f.type) {
    case FieldType::Float:   *(float*)(comp + f.offset) = src.get<float>(); return true;
    case FieldType::Double:  *(double*)(comp + f.offset) = src.get<double>(); return true;
    case FieldType::Int32:   *(int32_t*)(comp + f.offset) = src.get<int32_t>(); return true;
    case FieldType::UInt32:  *(uint32_t*)(comp + f.offset) = src.get<uint32_t>(); return true;
    case FieldType::UInt64:  *(uint64_t*)(comp + f.offset) = src.get<uint64_t>(); return true;
    case FieldType::Int16:   *(int16_t*)(comp + f.offset) = src.get<int16_t>(); return true;
    case FieldType::UInt16:  *(uint16_t*)(comp + f.offset) = src.get<uint16_t>(); return true;
    case FieldType::Int8:    *(int8_t*)(comp + f.offset) = src.get<int8_t>(); return true;
    case FieldType::UInt8:   *(uint8_t*)(comp + f.offset) = src.get<uint8_t>(); return true;
    case FieldType::Bool:    *(uint8_t*)(comp + f.offset) = src.get<bool>() ? 1 : 0; return true;
    case FieldType::TeamRef: *(uint32_t*)(comp + f.offset) = src.get<uint32_t>(); return true;
    case FieldType::Vec2: {
        Vec2& v = *(Vec2*)(comp + f.offset);
        v.x = src.at(0).get<float>();
        v.y = src.at(1).get<float>();
        return true;
    }
    case FieldType::EntityRef: {
        // "eN" 索引 → 句柄；null 保持空
        if (src.is_null()) { *(Entity*)(comp + f.offset) = Entity::Null(); return true; }
        std::string ref = src.get<std::string>();
        if (ref.size() < 2 || ref[0] != 'e') return false;
        size_t idx = (size_t)std::strtoull(ref.c_str() + 1, nullptr, 10);
        if (idx >= remapCount) return false;
        *(Entity*)(comp + f.offset) = remap[idx];
        return true;
    }
    case FieldType::Blob24: {
        char* dst = comp + f.offset;
        std::string s = src.get<std::string>();
        size_t n = s.size() < 23 ? s.size() : 23; // 末字节保 \0
        std::memcpy(dst, s.data(), n);
        dst[n] = '\0';
        return true;
    }
    }
    return false;
}

// 写：组件内存 → json 值（EntityRef 解析失败/句柄不在场景 → null）
Json WriteField(const FieldMeta& f, const char* comp,
                const std::unordered_map<Entity, uint32_t>& entityIds) {
    switch (f.type) {
    case FieldType::Float:   return *(const float*)(comp + f.offset);
    case FieldType::Double:  return *(const double*)(comp + f.offset);
    case FieldType::Int32:   return *(const int32_t*)(comp + f.offset);
    case FieldType::UInt32:  return *(const uint32_t*)(comp + f.offset);
    case FieldType::UInt64:  return *(const uint64_t*)(comp + f.offset);
    case FieldType::Int16:   return *(const int16_t*)(comp + f.offset);
    case FieldType::UInt16:  return *(const uint16_t*)(comp + f.offset);
    case FieldType::Int8:    return *(const int8_t*)(comp + f.offset);
    case FieldType::UInt8:   return *(const uint8_t*)(comp + f.offset);
    case FieldType::Bool:    return *(const uint8_t*)(comp + f.offset) != 0;
    case FieldType::TeamRef: return *(const uint32_t*)(comp + f.offset);
    case FieldType::Vec2: {
        const Vec2& v = *(const Vec2*)(comp + f.offset);
        return Json::array({v.x, v.y});
    }
    case FieldType::EntityRef: {
        Entity e = *(const Entity*)(comp + f.offset);
        auto it = entityIds.find(e);
        if (e.IsNull() || it == entityIds.end()) return nullptr;
        return "e" + std::to_string(it->second);
    }
    case FieldType::Blob24:
        return std::string(comp + f.offset); // 定长 \0 结尾
    }
    return nullptr;
}

} // namespace

std::string SceneArchive::Save(Scene& scene) {
    auto& reg = ComponentRegistry::Instance();

    Json doc;
    doc["schemaVersion"] = kSchemaVersion;
    doc["name"] = scene.Name();

    // 输出序 = 句柄升序（EnTT 遍历序与创建/回收历史有关，不排序则同一场景
    // 两次 Save 文本漂移，破坏 diff 与 roundtrip 不动点）
    std::vector<Entity> ordered;
    scene.Each([&](Entity e) { ordered.push_back(e); });
    std::sort(ordered.begin(), ordered.end(),
              [](Entity a, Entity b) { return a.id < b.id; });

    // 两遍：先编号再写字段（EntityRef 可引用本场景任意实体）
    std::unordered_map<Entity, uint32_t> entityIds;
    for (Entity e : ordered) entityIds.emplace(e, (uint32_t)entityIds.size());

    Json entities = Json::array();
    for (Entity e : ordered) {
        Json comps = Json::object();
        for (uint16_t id = 0; id < reg.Count(); ++id) {
            const ComponentMeta& m = reg.At(id);
            const char* comp = (const char*)m.readFn(scene, e);
            if (!comp) continue;
            Json obj = Json::object();
            for (uint16_t f = 0; f < m.fieldCount; ++f) {
                if (m.fields[f].flags & kFieldRuntime) continue; // 运行时态不入档
                obj[m.fields[f].name] = WriteField(m.fields[f], comp, entityIds);
            }
            comps[m.name] = std::move(obj);
        }
        entities.push_back(Json{{"components", std::move(comps)}});
    }
    doc["entities"] = std::move(entities);
    return doc.dump();
}

bool SceneArchive::Load(Scene& scene, const std::string& jsonText) {
    Json doc = Json::parse(jsonText, nullptr, false);
    if (doc.is_discarded()) {
        LEMON_WARN("scene parse failed (invalid json)");
        return false;
    }
    uint32_t ver = doc.value("schemaVersion", 0u);
    if (ver == 0) {
        LEMON_WARN("scene missing schemaVersion");
        return false;
    }
    std::string text = jsonText;
    while (ver < kSchemaVersion) {
        if (!Migrate(text, ver)) {
            LEMON_WARN("scene migration failed at v%u", ver);
            return false;
        }
        doc = Json::parse(text, nullptr, false);
        ver = doc.value("schemaVersion", 0u);
    }
    if (ver > kSchemaVersion) {
        LEMON_WARN("scene schema %u newer than engine %u", ver, kSchemaVersion);
        return false;
    }
    if (!doc.contains("entities")) {
        LEMON_WARN("scene missing entities");
        return false;
    }

    // 清空目标场景（两阶段销毁直接提交）
    scene.Each([&](Entity e) { scene.Destroy(e); });
    scene.CommitDestroys();

    auto& reg = ComponentRegistry::Instance();
    const Json& entities = doc.at("entities");

    // 两遍：先建全部实体（EntityRef 目标可能在本实体之后）
    std::vector<Entity> remap;
    remap.reserve(entities.size());
    for (size_t i = 0; i < entities.size(); ++i) remap.push_back(scene.Create());

    size_t i = 0;
    for (const Json& ent : entities) {
        Entity e = remap[i++];
        if (!ent.contains("components")) continue;
        const Json& comps = ent.at("components");
        for (auto it = comps.begin(); it != comps.end(); ++it) {
            const ComponentMeta* m = reg.Find(it.key().c_str());
            if (!m) {
                LEMON_WARN("unknown component '%s' skipped (newer scene?)",
                           it.key().c_str());
                continue;
            }
            char* comp = (char*)m->emplaceFn(scene, e);
            for (uint16_t f = 0; f < m->fieldCount; ++f)
                if (it.value().contains(m->fields[f].name))
                    ReadField(it.value().at(m->fields[f].name), m->fields[f], comp,
                              remap.data(), remap.size());
        }
    }
    return true;
}

bool SceneArchive::Migrate(std::string& jsonText, uint32_t fromVersion) {
    // 迁移链骨架：每级一个 case，纯 json→json 变换后回写 schemaVersion。
    // v1 是首版无历史；后续版本在此追加（老档自动升级，风险台账 #7 的 CI 保障）。
    Json doc = Json::parse(jsonText, nullptr, false);
    if (doc.is_discarded()) return false;
    (void)doc;
    LEMON_WARN("no migration registered from schema v%u", fromVersion);
    return false;
}

} // namespace lemon::ecs
