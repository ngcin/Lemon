// Lemon 引擎 — 场景存取实现（nlohmann/json 只进本 .cpp，头文件零泄漏）
#include "Serialization/SceneArchive.h"

#include <algorithm>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "Core/Guid.h"
#include "Core/Log.h"
#include "Core/Math.h"
#include "Components/CoreComponents.h"
#include "ECS/ComponentRegistry.h"
#include "ECS/Hierarchy.h"
#include "Scripting/ScriptBox.h"

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
        // 全消费检查（review 2026-10-09 M21）：strtoull 前缀解析不要求到串尾，
        // "e5x"/"e 5" 会静默指到实体 5（坏档用户可手编，编号越界有防、格式垃圾
        // 无防）——要求 endptr 落在串尾且首字符即数字（拒前导空白/负号/空串）
        char* end = nullptr;
        const char* digits = ref.c_str() + 1;
        if (*digits < '0' || *digits > '9') return false; // 拒前导空白/符号（strtoull 会跳会收）
        const unsigned long long idx = std::strtoull(digits, &end, 10);
        if (*end != '\0') return false;
        if (idx >= remapCount) return false;
        *(Entity*)(comp + f.offset) = remap[(size_t)idx];
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

Json WriteField(const FieldMeta& f, const char* comp,
                const std::unordered_map<Entity, uint32_t>& entityIds);

// 数组段：读（元素对象数组或 uint32 数值数组；越界 count 截断到容量）
void ReadArraySeg(const Json& src, const ArraySegMeta& seg, char* comp) {
    if (!src.is_array()) return;
    size_t n = src.size();
    if (n > seg.maxCount) n = seg.maxCount;
    char* base = comp + seg.offset;
    for (size_t i = 0; i < n; ++i) {
        char* elem = base + i * seg.elemSize;
        const Json& ev = src[i];
        if (seg.elemFields) {
            for (uint16_t f = 0; f < seg.elemFieldCount; ++f)
                if (ev.contains(seg.elemFields[f].name))
                    ReadField(ev.at(seg.elemFields[f].name), seg.elemFields[f], elem,
                              nullptr, 0);
        } else if (ev.is_number_unsigned() || ev.is_number_integer()) {
            *(uint32_t*)elem = ev.get<uint32_t>();
        }
    }
    if (seg.countOffset != 0xFFFF) *(uint8_t*)(comp + seg.countOffset) = (uint8_t)n;
}

// 数组段：写（元素字段表 → 对象数组；标量段 → 数值数组）
Json WriteArraySeg(const ArraySegMeta& seg, const char* comp) {
    uint32_t n = seg.maxCount;
    if (seg.countOffset != 0xFFFF) n = *(const uint8_t*)(comp + seg.countOffset);
    if (n > seg.maxCount) n = seg.maxCount;
    const char* base = comp + seg.offset;
    Json arr = Json::array();
    for (uint32_t i = 0; i < n; ++i) {
        const char* elem = base + i * seg.elemSize;
        if (seg.elemFields) {
            Json obj = Json::object();
            for (uint16_t f = 0; f < seg.elemFieldCount; ++f)
                obj[seg.elemFields[f].name] =
                    WriteField(seg.elemFields[f], elem, {});
            arr.push_back(std::move(obj));
        } else {
            arr.push_back(*(const uint32_t*)elem);
        }
    }
    return arr;
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

// [ISSUE-1] 版本字段类型校验：value() 对字符串/浮点等类型错会抛 type_error
// 抛穿加载器（违反"坏档不得抛穿"不变量）——显式判型，非法一律按无效版本拒绝
uint32_t ReadSchemaVersion(const Json& d) {
    auto it = d.find("schemaVersion");
    if (it == d.end() || !it->is_number_unsigned()) return 0;
    uint64_t v = it->get<uint64_t>();
    return v > 0xFFFFFFFFull ? 0xFFFFFFFFu : (uint32_t)v; // 超范围 → 按"未来版本"拒绝
}

// ---- 实体读写共享体（Save/Load 与实体树 IO 复用；M4.4 Prefab 最小集）----

/// 单实体 → JSON（components + 可选 script 段）。entityIds = 本档案实体编号表
/// （跨表 EntityRef 写 null——子树导出时树外引用自然悬空）。
Json WriteEntity(Scene& scene, Entity e,
                 const std::unordered_map<Entity, uint32_t>& entityIds) {
    auto& reg = ComponentRegistry::Instance();
    Json comps = Json::object();
    for (uint16_t id = 0; id < reg.Count(); ++id) {
        const ComponentMeta& m = reg.At(id);
        // 运行时销毁队列标记永不入档（review 2026-10-02 #10）：空字段组件命中才
        // 比名，热路径零 strcmp
        if (m.fieldCount == 0 && std::strcmp(m.name, "DestroyQueueTag") == 0) continue;
        const char* comp = (const char*)m.readFn(scene, e);
        if (!comp) continue;
        Json obj = Json::object();
        for (uint16_t f = 0; f < m.fieldCount; ++f) {
            if (m.fields[f].flags & kFieldRuntime) continue; // 运行时态不入档
            obj[m.fields[f].name] = WriteField(m.fields[f], comp, entityIds);
        }
        if (m.arraySeg) // 定长数组段（active/items/relicIds 等）
            obj[m.arraySeg->field] = WriteArraySeg(*m.arraySeg, comp);
        comps[m.name] = std::move(obj);
    }
    Json ent{{"components", std::move(comps)}};
    if (const scripting::ScriptBox* sb = scene.TryGet<scripting::ScriptBox>(e)) {
        // M6a 批⓪ schema v2：scripts[] 多脚本（typeId 注册序不持久——代码增删即
        // 漂移，className 是持久键；guid 供资产侧追踪/热重载目标）
        Json arr = Json::array();
        for (uint32_t i = 0; i < sb->count; ++i)
            arr.push_back(Json{{"guid", sb->slots[i].scriptGuid},
                               {"class", std::string(sb->slots[i].className)}});
        ent["scripts"] = std::move(arr);
    }
    return ent;
}

/// JSON → 单实体组件集（remap = 档内编号 → 实体）。仅对全新实体调用：对已持有
/// 同组件的实体 emplace 是 entt 断言路径——现行调用点 Load/LoadEntityTree 均传
/// 新建实体（review 2026-10-02 #49：原注释宣称"清掉已有可重建组件后重建"，实现
/// 从未做清理，契约按实现收敛）。返回 false = 条目非 object/缺 components
///（Save 恒写 components 键，缺失即坏档——Load 侧回收预建实体并红字）。
/// 用户可编辑文本档：类型错/结构坏不得抛穿（json 异常就地降级）。
bool ReadEntity(Scene& scene, const Json& ent, Entity e, const Entity* remap,
                size_t remapCount) {
    auto& reg = ComponentRegistry::Instance();
    if (!ent.is_object() || !ent.contains("components")) return false;
    const Json& comps = ent.at("components");
    if (!comps.is_object()) return false;
    for (auto it = comps.begin(); it != comps.end(); ++it) {
        const ComponentMeta* m = reg.Find(it.key().c_str());
        if (!m) {
            LEMON_WARN("unknown component '%s' skipped (newer scene?)", it.key().c_str());
            continue;
        }
        // 运行时销毁队列标记拒读（review 2026-10-02 #10）：旧档若含（保存窗口期
        // 写出），emplace 出无队列项的空标记 = 读档即永生僵尸（CommitDestroys 只
        // 消费当帧队列，永不回收）
        if (std::strcmp(m->name, "DestroyQueueTag") == 0) continue;
        if (!it.value().is_object()) { // 坏档可见（review 2026-10-02 #48）：原静默跳过
            LEMON_WARN("component '%s' value not an object — skipped", it.key().c_str());
            continue;
        }
        char* comp = (char*)m->emplaceFn(scene, e);
        const Json& obj = it.value();
        for (uint16_t f = 0; f < m->fieldCount; ++f) {
            if (!obj.contains(m->fields[f].name)) continue;
            try {
                // 返回 false = EntityRef 格式非法/编号越界（review 2026-10-02 #48：
                // 原静默吞掉——引用保持默认 null，与类型错同款红字可见）
                if (!ReadField(obj.at(m->fields[f].name), m->fields[f], comp, remap,
                               remapCount))
                    LEMON_WARN("field '%s.%s' invalid (ref format/index?) — kept default",
                               m->name, m->fields[f].name);
            } catch (const Json::exception& ex) {
                LEMON_WARN("field '%s.%s' type mismatch skipped: %s", m->name,
                           m->fields[f].name, ex.what());
            }
        }
        if (m->arraySeg && obj.contains(m->arraySeg->field)) {
            try {
                ReadArraySeg(obj.at(m->arraySeg->field), *m->arraySeg, comp);
            } catch (const Json::exception& ex) {
                LEMON_WARN("array seg '%s.%s' skipped: %s", m->name, m->arraySeg->field,
                           ex.what());
            }
        }
        // [ISSUE-2] count 是普通序列化字段：档里只写 count（无数组键）时不经
        // ReadArraySeg 截断 → 消费方（StatSystem 等）按 count 遍历即越界读写
        if (m->arraySeg && m->arraySeg->countOffset != 0xFFFF) {
            uint8_t& cnt = *(uint8_t*)(comp + m->arraySeg->countOffset);
            if (cnt > m->arraySeg->maxCount) {
                LEMON_WARN("array count %u > capacity %u in '%s' clamped", cnt,
                           m->arraySeg->maxCount, m->name);
                cnt = (uint8_t)m->arraySeg->maxCount;
            }
        }
    }
    // M6a 批⓪ schema v2：scripts[]（多脚本）。双读：旧单数 "script"（v1 .scene 经
    // 迁移链已升 v2；.prefab 的 LoadEntityTree 不走迁移——此处兼容旧资产隐式升级）。
    // 同名重复项保序留首见 + 告警（同类型唯一不变量的加载侧清洗，Plans/M6a 批⓪
    // 决策 4）；全部无效时不留空 ScriptBox 组件。
    const Json* scriptSlots = nullptr;
    Json legacyWrapped;
    if (ent.contains("scripts") && ent.at("scripts").is_array()) {
        scriptSlots = &ent.at("scripts");
    } else if (ent.contains("script") && ent.at("script").is_object()) {
        legacyWrapped = Json::array({ent.at("script")});
        scriptSlots = &legacyWrapped;
    }
    if (scriptSlots) {
        scripting::ScriptBox& sb = scene.Emplace<scripting::ScriptBox>(e);
        for (const Json& sj : *scriptSlots) {
            if (!sj.is_object()) continue;
            uint64_t guid = 0;
            std::string cls;
            try {
                if (sj.contains("guid")) guid = sj.at("guid").get<uint64_t>();
                if (sj.contains("class")) cls = sj.at("class").get<std::string>();
            } catch (const Json::exception& ex) {
                LEMON_WARN("script member malformed skipped: %s", ex.what());
                continue;
            }
            if (cls.empty()) continue;
            if (scripting::FindSlot(sb, cls.c_str()) >= 0) {
                LEMON_WARN("scripts[] 同名重复项清洗（保序留首见）'%s'", cls.c_str());
                continue;
            }
            if (!scripting::AppendSlot(sb, guid, cls.c_str()))
                LEMON_WARN("scripts[] 槽数超限（%u），多余项忽略",
                           scripting::kMaxScriptsPerEntity);
        }
        if (sb.count == 0) scene.Remove<scripting::ScriptBox>(e);
    }
    return true;
}

/// root 子树收集（父先于子；Hierarchy 链序；深度上限防脏档环）
void CollectSubtree(Scene& scene, Entity e, std::vector<Entity>& out, int depth = 0) {
    if (depth > (int)kMaxHierarchyDepth + 1) return;
    out.push_back(e);
    const Hierarchy* h = scene.TryGet<Hierarchy>(e);
    if (!h) return;
    for (Entity c = h->firstChild; !c.IsNull() && scene.Alive(c);) {
        const Hierarchy* ch = scene.TryGet<Hierarchy>(c);
        Entity next = ch ? ch->next : Entity::Null();
        CollectSubtree(scene, c, out, depth + 1);
        c = next;
    }
}

} // namespace

std::string SceneArchive::Save(Scene& scene) {
    Json doc;
    doc["schemaVersion"] = kSchemaVersion;
    doc["name"] = scene.Name();

    // 输出序 = 句柄升序（EnTT 遍历序与创建/回收历史有关，不排序则同一场景
    // 两次 Save 文本漂移，破坏 diff 与 roundtrip 不动点）
    std::vector<Entity> ordered;
    // 待销毁实体不入档（review 2026-10-02 #10）：窗口期保存含『死实体』，读档
    // 复活且永不入队（CommitDestroys 只消费当帧队列）= 每帧 tick 的永生僵尸。
    // 引擎侧防线——此前只靠调用方"保存前先 Commit"约定，编辑器侧已实录过漏配
    // 事故（smoke-ui 真人链路）
    scene.Each([&](Entity e) {
        if (scene.Has<DestroyQueueTag>(e)) return;
        ordered.push_back(e);
    });
    std::sort(ordered.begin(), ordered.end(),
              [](Entity a, Entity b) { return a.id < b.id; });

    // 两遍：先编号再写字段（EntityRef 可引用本场景任意实体）
    std::unordered_map<Entity, uint32_t> entityIds;
    for (Entity e : ordered) entityIds.emplace(e, (uint32_t)entityIds.size());

    Json entities = Json::array();
    for (Entity e : ordered) entities.push_back(WriteEntity(scene, e, entityIds));
    doc["entities"] = std::move(entities);
    return doc.dump();
}

std::string SceneArchive::SaveEntityTree(Scene& scene, Entity root) {
    if (!scene.Alive(root)) return {};
    if (scene.Has<DestroyQueueTag>(root)) return {}; // 待销毁根：导出即死树，拒绝
    std::vector<Entity> subtree;
    CollectSubtree(scene, root, subtree);
    // 子树内待销毁成员剪除（Save 同款口径，review 2026-10-02 #10）
    subtree.erase(std::remove_if(subtree.begin(), subtree.end(),
                                 [&](Entity e) { return scene.Has<DestroyQueueTag>(e); }),
                  subtree.end());

    std::unordered_map<Entity, uint32_t> entityIds;
    for (Entity e : subtree) entityIds.emplace(e, (uint32_t)entityIds.size());

    Json doc;
    doc["schemaVersion"] = kSchemaVersion;
    doc["name"] = "prefab";
    Json entities = Json::array();
    for (Entity e : subtree) entities.push_back(WriteEntity(scene, e, entityIds));
    doc["entities"] = std::move(entities);
    return doc.dump();
}

// string/预解析两版共核（批⑪ #M17 拆出；逻辑逐行未动）
static ecs::Entity LoadEntityTreeCore(Scene& scene, const Json& entities) {
    if (entities.empty()) return Entity::Null();

    // 两遍：先建全部实体（EntityRef 目标可能在本实体之后）。坏档条目回收预建槽
    //（review 2026-10-02 #48，Load 同款不留空壳）；根条目坏 = 资产整体损坏，
    // 整树回收返回 Null（调用方走失败分支——半棵树无根不可用）
    std::vector<Entity> remap;
    remap.reserve(entities.size());
    for (size_t i = 0; i < entities.size(); ++i) remap.push_back(scene.Create());
    bool rootOk = true, dropped = false;
    for (size_t i = 0; i < entities.size(); ++i) {
        if (!ReadEntity(scene, entities[i], remap[i], remap.data(), remap.size())) {
            LEMON_WARN("prefab: entities[%zu] not an object/missing components — dropped",
                       i);
            scene.Destroy(remap[i]);
            remap[i] = Entity::Null();
            dropped = true;
            if (i == 0) rootOk = false;
        }
    }
    if (!rootOk) {
        for (Entity e : remap)
            if (!e.IsNull()) scene.Destroy(e);
        scene.CommitDestroys();
        return Entity::Null();
    }
    if (dropped) scene.CommitDestroys();

    // 实例化语义：guid 全部换新（Prefab 源 guid 留在资产文件里）
    for (Entity e : remap)
        if (!e.IsNull())
            if (Meta* m = scene.TryGet<Meta>(e); m) m->guid = GenerateGuid();
    return remap[0];
}

// 批⑪ #M17：实体树预解析产物（pimpl——定义不出 .cpp，nlohmann 红线）
struct SceneArchive::ParsedEntityTree {
    Json doc;
};

std::shared_ptr<SceneArchive::ParsedEntityTree>
SceneArchive::ParseEntityTree(const std::string& jsonText) {
    // 结构校验与 string 版入口一致（entities 数组在场；空数组由 core 静默 Null）
    Json doc = Json::parse(jsonText, nullptr, false);
    if (doc.is_discarded() || !doc.contains("entities") || !doc.at("entities").is_array())
        return nullptr;
    return std::shared_ptr<ParsedEntityTree>(new ParsedEntityTree{std::move(doc)});
}

ecs::Entity SceneArchive::LoadEntityTree(Scene& scene, const ParsedEntityTree& parsed) {
    return LoadEntityTreeCore(scene, parsed.doc.at("entities"));
}

ecs::Entity SceneArchive::LoadEntityTree(Scene& scene, const std::string& jsonText) {
    auto parsed = ParseEntityTree(jsonText);
    if (!parsed) {
        LEMON_WARN("prefab parse failed (invalid json)");
        return Entity::Null();
    }
    return LoadEntityTree(scene, *parsed);
}

namespace {

/// 解析+迁移+校验（Load/BuildInto 共用前半；M7c 批⑥ BuildInto 拆分）。失败已告警
/// 并返回 false（doc 不动）。
bool ParseSceneDoc(const std::string& jsonText, Json& doc) {
    doc = Json::parse(jsonText, nullptr, false);
    if (doc.is_discarded()) {
        LEMON_WARN("scene parse failed (invalid json)");
        return false;
    }
    uint32_t ver = ReadSchemaVersion(doc);
    if (ver == 0) {
        LEMON_WARN("scene missing/invalid schemaVersion");
        return false;
    }
    // L24（review 2026-10-09）：v2 现版档（绝大多数）迁移循环不进——拷贝纯浪费
    //（万实体档 MB 级深拷贝 × 5 个调用点）；确认需迁移才拷可变副本
    std::string text;
    if (ver < SceneArchive::kSchemaVersion) text = jsonText;
    while (ver < SceneArchive::kSchemaVersion) {
        if (!SceneArchive::Migrate(text, ver)) {
            LEMON_WARN("scene migration failed at v%u", ver);
            return false;
        }
        doc = Json::parse(text, nullptr, false);
        ver = ReadSchemaVersion(doc);
        if (ver == 0) {
            LEMON_WARN("scene schemaVersion lost in migration");
            return false;
        }
    }
    if (ver > SceneArchive::kSchemaVersion) {
        LEMON_WARN("scene schema %u newer than engine %u", ver, SceneArchive::kSchemaVersion);
        return false;
    }
    if (!doc.contains("entities") || !doc.at("entities").is_array()) {
        LEMON_WARN("scene missing/invalid entities array");
        return false;
    }
    return true;
}

/// 场景名恢复（Save 写 doc["name"]，Load 原从不读回 → 存档再开标题回默认名；
/// 旧档/无名档不覆盖调用方默认名）
void ApplySceneName(Scene& scene, const Json& doc) {
    if (doc.contains("name") && doc.at("name").is_string())
        scene.SetName(doc.at("name").get<std::string>().c_str());
}

/// 两遍构建（Load/BuildInto 共用主体）：先建全部实体（EntityRef 目标可能在本实体
/// 之后），再逐条 ReadEntity。坏档条目（review 2026-10-02 #48）：不留无组件空壳——
/// 回收预建槽并红字；档内引用该编号的 EntityRef 读到 null（引用目标确实不存在）。
bool BuildEntities(Scene& scene, const Json& entities) {
    std::vector<Entity> remap;
    remap.reserve(entities.size());
    for (size_t i = 0; i < entities.size(); ++i) remap.push_back(scene.Create());

    size_t i = 0;
    bool dropped = false;
    for (const Json& ent : entities) {
        const size_t idx = i++;
        if (!ReadEntity(scene, ent, remap[idx], remap.data(), remap.size())) {
            LEMON_WARN("scene: entities[%zu] not an object/missing components — dropped",
                       idx);
            scene.Destroy(remap[idx]);
            remap[idx] = Entity::Null();
            dropped = true;
        }
    }
    if (dropped) scene.CommitDestroys(); // 当帧回收，不给系统管线看见空壳实体
    // L9（review 2026-10-09）红线豁免注记：此处直调绕过 NotifyPendingDestroys 是
    // 承重的——坏槽 = ReadEntity 拒收的零组件预建空壳，无 ScriptBox 可通知
    //（无 OnDestroy 可漏，F1 语义无损）；非坏槽永不走此路径
    return true;
}

} // namespace

bool SceneArchive::Load(Scene& scene, const std::string& jsonText) {
    Json doc;
    if (!ParseSceneDoc(jsonText, doc)) return false;
    ApplySceneName(scene, doc);
    // 清空目标场景（两阶段销毁直接提交）
    scene.Each([&](Entity e) { scene.Destroy(e); });
    scene.CommitDestroys();
    return BuildEntities(scene, doc.at("entities"));
}

bool SceneArchive::BuildInto(Scene& scene, const std::string& jsonText) {
    // ADR-017 D3 Build 段：不清空——实体追加（换场编排负责旧组清场与 membership
    // 打标；DDOL 实体在场即共存 = 单 registry membership 语义）
    Json doc;
    if (!ParseSceneDoc(jsonText, doc)) return false;
    ApplySceneName(scene, doc);
    return BuildEntities(scene, doc.at("entities"));
}

bool SceneArchive::ValidateParse(const std::string& jsonText) {
    // 批⑥b 换场原子性预检：与 BuildInto 同一条 ParseSceneDoc 链（解析 + 迁移 +
    // 段校验），丢弃解析结果不构建。失败告警已在 ParseSceneDoc 内（口径同 Load）
    Json doc;
    return ParseSceneDoc(jsonText, doc);
}

bool SceneArchive::SceneDocName(const std::string& jsonText, std::string& out) {
    // 批⑦：宿主 resolveScene 钩子填 SceneSwitchRequest.name 用（与 BuildInto 同链
    // 解析 + 迁移；档案名 = doc "name" 段，缺省回落空串 = 调用方自定回退名）
    Json doc;
    if (!ParseSceneDoc(jsonText, doc)) return false;
    out.clear();
    if (doc.contains("name") && doc.at("name").is_string())
        out = doc.at("name").get<std::string>();
    return true;
}

bool SceneArchive::Migrate(std::string& jsonText, uint32_t fromVersion) {
    // 迁移链：每级一个 case，纯 json→json 变换后回写 schemaVersion（03 §13；
    // 老档自动升级，风险台账 #7 的 CI 保障）。
    Json doc = Json::parse(jsonText, nullptr, false);
    if (doc.is_discarded()) return false;
    switch (fromVersion) {
    case 1: {
        // v1→v2（M6a 批⓪）：实体脚本单数 "script":{guid,class} → 复数
        // "scripts":[{…}]。.prefab 的 LoadEntityTree 不经迁移（ReadEntity 双读
        // 兼容旧单数），本链只服务 .scene Load。
        if (doc.contains("entities") && doc.at("entities").is_array())
            for (auto& ent : doc.at("entities")) {
                if (!ent.is_object() || !ent.contains("script")) continue;
                if (!ent.at("script").is_object()) {
                    ent.erase("script");
                    continue;
                }
                ent["scripts"] = Json::array({ent["script"]});
                ent.erase("script");
            }
        doc["schemaVersion"] = kSchemaVersion;
        jsonText = doc.dump();
        return true;
    }
    default:
        LEMON_WARN("no migration registered from schema v%u", fromVersion);
        return false;
    }
}

// ---- StagedSceneBuild（M7c 批⑧；ADR-017 D3 Parse/Build 分帧段）----------------
// pimpl 本体：Parse 段产物（Json DOM + 档名）+ Build 段两相游标与槽账。BuildEntities
// 的"建槽全量→逐实体 ReadEntity"两遍结构拆成可分帧的两相——DecodeEntities 与
// ReadEntity 零分叉（坏条目同款回收 + 台账 Null 化，时点从"构建尾一次提交"改为
// "逐条即时回收"：暂存 registry 无管线观察者，提交时点无语义差）。
struct StagedSceneBuild::Impl {
    Json doc;                     // 迁移后的场景文档（entities 数组即 Build 源）
    std::string name;             // 档 "name" 段
    bool hasName = false;         // 档是否含 name 字符串段（ApplySceneName 同构判据）
    std::vector<Entity> ledger;   // 槽账（doc 序；Null = 坏条目/未建）
    size_t created = 0;           // 相一游标（已建槽数）
    size_t decoded = 0;           // 相二游标（已解码条数）
};

std::unique_ptr<StagedSceneBuild> StagedSceneBuild::Parse(const std::string& jsonText) {
    Json doc;
    if (!ParseSceneDoc(jsonText, doc)) return nullptr; // 告警已在内
    auto out = std::unique_ptr<StagedSceneBuild>(new StagedSceneBuild());
    out->impl_ = std::make_unique<Impl>();
    out->impl_->doc = std::move(doc);
    if (out->impl_->doc.contains("name") && out->impl_->doc.at("name").is_string()) {
        out->impl_->name = out->impl_->doc.at("name").get<std::string>();
        out->impl_->hasName = true;
    }
    return out;
}

StagedSceneBuild::~StagedSceneBuild() = default;

uint32_t StagedSceneBuild::EntityCount() const {
    // L25（review 2026-10-09）：ReleaseDocChunk 后 doc 已置 null——原 at() 对 null
    // json 抛 type_error 穿透 pimpl 边界；回落台账（槽账口径，CreateSlots 已 resize
    // 对齐——SceneArchive.h 契约补注释放后仍可查询）
    if (impl_->doc.is_null()) return (uint32_t)impl_->ledger.size();
    return (uint32_t)impl_->doc.at("entities").size();
}

uint32_t StagedSceneBuild::CreatedCount() const { return (uint32_t)impl_->created; }

uint32_t StagedSceneBuild::DecodedCount() const { return (uint32_t)impl_->decoded; }

bool StagedSceneBuild::HasName() const { return impl_->hasName; }

const std::string& StagedSceneBuild::Name() const { return impl_->name; }

bool StagedSceneBuild::CreateSlots(Scene& staging, uint32_t maxN) {
    const Json& entities = impl_->doc.at("entities");
    if (impl_->ledger.empty()) impl_->ledger.resize(entities.size(), Entity::Null());
    uint32_t n = 0;
    while (impl_->created < entities.size() && n < maxN) {
        impl_->ledger[impl_->created] = staging.Create();
        ++impl_->created;
        ++n;
    }
    return impl_->created >= entities.size();
}

bool StagedSceneBuild::DecodeEntities(Scene& staging, uint32_t maxN) {
    const Json& entities = impl_->doc.at("entities");
    LEMON_ASSERT(impl_->created >= entities.size(),
                 "StagedSceneBuild: DecodeEntities before CreateSlots done");
    bool dropped = false;
    uint32_t n = 0;
    while (impl_->decoded < entities.size() && n < maxN) {
        const size_t idx = impl_->decoded++;
        if (!ReadEntity(staging, entities[idx], impl_->ledger[idx], impl_->ledger.data(),
                        impl_->ledger.size())) {
            LEMON_WARN("async scene: entities[%zu] not an object/missing components — dropped",
                       idx);
            staging.Destroy(impl_->ledger[idx]); // 台账保留原句柄（集成段全槽
                                                 // 重映射依据）；有效性 = Alive 判据
            dropped = true;
        }
        ++n;
    }
    if (dropped) staging.CommitDestroys(); // 暂存侧即时回收（无管线观察者）
    return impl_->decoded >= entities.size();
}

const std::vector<Entity>& StagedSceneBuild::Ledger() const { return impl_->ledger; }

bool StagedSceneBuild::ReleaseDocChunk(uint32_t maxElems) {
    // 万实体档 DOM 递归析构数十 ms（节点级 free）——从 entities 尾部 resize 分块
    // 释放（每元素 O(1)：destroy 尾元素 + 容量收缩），清空后整体丢 doc。
    // 槽账/档名独立于 doc，集成段不受影响。
    if (impl_->doc.is_null()) return true;
    Json& arr = impl_->doc.at("entities");
    const size_t n = arr.is_array() ? arr.size() : 0;
    if (n > maxElems) {
        arr.erase(arr.begin() + (std::ptrdiff_t)(n - maxElems), arr.end());
        return false;
    }
    impl_->doc = Json(); // 清空（含末块/非数组防御路径）——此后仅剩槽账
    return true;
}

} // namespace lemon::ecs
