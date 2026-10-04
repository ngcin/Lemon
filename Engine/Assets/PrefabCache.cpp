// Lemon 引擎 — Play 世界 Prefab 工厂缓存实现（M7a 批③ 自 EditorContext 搬家，
// 逐行同源）
#include "Assets/PrefabCache.h"

#include <fstream>
#include <iterator>

#include "Core/Log.h"
#include "ECS/Scene.h"
#include "ECS/Hierarchy.h"
#include "Components/CoreComponents.h"
#include "Scripting/ScriptBox.h"
#include "Scripting/ScriptHost.h"
#include "Serialization/SceneArchive.h"

namespace lemon::assets {

namespace {
// className → 注册序（ScriptHost::BehaviourTypeNames 线性查——与编辑器
// EditorContext::ResolveScriptTypeId 同一实现形态，随批迁入）
int ResolveTypeId(scripting::ScriptHost& host, const char* className) {
    if (!className || !className[0]) return -1;
    const auto& names = host.BehaviourTypeNames();
    for (size_t i = 0; i < names.size(); ++i)
        if (names[i] == className) return (int)i;
    return -1;
}
} // namespace

void PrefabCache::Build(const PrefabSource& src) {
    Clear();
    src.EachPrefab([&](uint64_t guid, const std::string& absPath) {
        std::ifstream f(absPath, std::ios::binary);
        if (!f) return true; // 读不开 = 跳过（健康源仍可能被外部并发删——响亮度让位）
        Entry c;
        c.guid = guid;
        c.json.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        const uint32_t id = (uint32_t)guid; // 低 32 位（映射约定）
        if (!byId_.emplace(id, std::move(c)).second)
            LEMON_WARN("Play prefab 映射碰撞：guid %016llx 与另一 prefab 低 32 位同值"
                       "（id %08x 取先登记者）",
                       (unsigned long long)guid, id);
        return true;
    });
}

ecs::Entity PrefabCache::Spawn(ecs::Scene& s, uint32_t prefabId, Vec2 pos,
                               uint32_t team) const {
    if (prefabId == 0) return ecs::Entity::Null();
    auto it = byId_.find(prefabId);
    if (it == byId_.end()) {
        // 去重告警（prefabId 错绑的 Spawner 每帧触发——不刷屏）
        if (warned_.insert(prefabId).second)
            LEMON_WARN("Play 刷怪失败：prefabId %08x 无对应 prefab 资产（应填资产 GUID "
                       "低 32 位）",
                       prefabId);
        return ecs::Entity::Null();
    }
    ecs::Entity root = InstantiateJson(s, it->second.json, it->second.guid, pos);
    // 队伍覆盖：spawnTeam/弹队语义优先于 prefab 源值（bench 工厂同款）
    if (ecs::Meta* m = root.IsNull() ? nullptr : s.TryGet<ecs::Meta>(root)) m->team = team;
    return root;
}

ecs::Entity PrefabCache::InstantiateJson(ecs::Scene& s, const std::string& json,
                                         uint64_t prefabGuid, Vec2 pos) {
    // 无日志/无 IO/无 dirty——高频 spawn 工厂与交互路径共用（M5 清障②）
    ecs::Entity root = ecs::SceneArchive::LoadEntityTree(s, json);
    if (root.IsNull()) return root;
    if (s.Has<ecs::Transform2D>(root)) s.Get<ecs::Transform2D>(root).pos = pos;
    if (ecs::Meta* m = s.TryGet<ecs::Meta>(root)) m->prefabId = prefabGuid;
    return root;
}

void PrefabCache::ResolveTreeScripts(ecs::World& w, ecs::Scene& s, ecs::Entity root,
                                     scripting::ScriptHost& host) {
    std::function<void(ecs::Entity)> resolveTree = [&](ecs::Entity e) {
        if (scripting::ScriptBox* sb = s.TryGet<scripting::ScriptBox>(e))
            for (uint32_t i = 0; i < sb->count; ++i) {
                scripting::ScriptSlot& sl = sb->slots[i];
                if (sl.typeId >= 0) continue;
                int id = ResolveTypeId(host, sl.className);
                if (id < 0) {
                    LEMON_WARN("Prefab spawn：脚本类型未注册（跳过）'%s'", sl.className);
                    continue;
                }
                host.ResolveSlotBehaviour(w, s, e, i, id);
            }
        if (const ecs::Hierarchy* h = s.TryGet<ecs::Hierarchy>(e))
            for (ecs::Entity c = h->firstChild; !c.IsNull();) {
                const ecs::Entity next = s.Get<ecs::Hierarchy>(c).next;
                resolveTree(c);
                c = next;
            }
    };
    resolveTree(root);
}

} // namespace lemon::assets
