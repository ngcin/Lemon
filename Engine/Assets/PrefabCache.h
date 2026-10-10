// Lemon 引擎 — Play 世界 Prefab 工厂缓存（M7a 批③；M5 清障② 随迁）
// 自 EditorContext 下沉（搬家非复制）：进 Play/装载期把全部 .prefab 资产读成
// 文本快照，键 = 资产 GUID 低 32 位（03 §69 组件 schema 恒 uint32 的映射约定）。
// 编辑器（AssetDatabase 源）与独立运行时（AssetIndex 源——FindByLowId 同口径）
// 共用；批④ lemon-game 装配期消费。
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "Core/Math.h" // Vec2
#include "ECS/Entity.h"
#include "Serialization/SceneArchive.h" // 批⑪ #M17：Entry 持 ParsedEntityTree（pimpl）

namespace lemon::ecs {
class Scene;
class World;
}
namespace lemon::scripting {
class ScriptHost;
}

namespace lemon::assets {

/// Prefab 资产只读源（SpriteRefSource 同款纪律：编辑器 AssetDatabase / 运行时
/// AssetIndex 双实现——实现侧过滤 type==Prefab 且健康，本层零类型判断）
class PrefabSource {
public:
    virtual ~PrefabSource() = default;
    /// 遍历健康 prefab 资产；每件回调 (guid, 绝对路径)；回调返回 false 提前止
    virtual void EachPrefab(
        const std::function<bool(uint64_t guid, const std::string& absPath)>& fn) const = 0;
};

class PrefabCache {
public:
    /// 装载期建缓存（源全量 .prefab → {低 32 位 → {guid,预解析树}}；进 Play 时刻
    /// 快照语义——Play 世界 = 快照，资产变更不追）。低 32 位碰撞 = 红字取先登记者。
    /// 批⑪ #M17：Build 期 Json::parse 一次入 Entry（原 Entry 存文本、Spawn 每发
    /// 全量重解析——高频刷怪/弹幕每轮堆分配密集解析）；坏档装载期红字跳过。
    void Build(const PrefabSource& src);
    ~PrefabCache(); // Entry 持 pimpl 不完整类型——析构出 .cpp（唯一完整类型点）
    void Clear();
    bool Empty() const { return byId_.empty(); }
    size_t Size() const { return byId_.size(); }

    /// Play 世界原生工厂 spawn（Spawner/Shooter/SetSpawnFn 桥）：未命中 Null +
    /// 去重告警（prefabId 错绑的 Spawner 每帧触发——不刷屏）；命中 = InstantiateJson
    /// + 队伍覆盖（spawnTeam/弹队语义优先于 prefab 源值，bench 工厂同款）。
    /// 注意：**不**做 scripts[] 槽解析（原 SpawnPlayPrefab 语义——原生工厂 spawn
    /// 的树不带脚本实例；脚本实例挂载走 ResolveTreeScripts，钩子/交互路径用）。
    ecs::Entity Spawn(ecs::Scene& s, uint32_t prefabId, Vec2 pos, uint32_t team) const;

    /// 实例化核心（json 已在手；无 IO/无告警——高频 spawn 工厂与交互路径共用，
    /// M5 清障② 契约）：LoadEntityTree + root pos 覆盖 + prefabId 回链。
    static ecs::Entity InstantiateJson(ecs::Scene& s, const std::string& json,
                                       uint64_t prefabGuid, Vec2 pos);

    /// 树遍历 scripts[] 槽实例挂载（批③d-2：LoadEntityTree 只落槽不建实例——
    /// Unity Instantiate 重放 behaviour 的等价路径；钩子/交互路径 spawn 后调用，
    /// 与 EnterPlay 装配同款解析。typeId 未注册 = 红字跳过不炸）
    static void ResolveTreeScripts(ecs::World& w, ecs::Scene& s, ecs::Entity root,
                                   scripting::ScriptHost& host);

private:
    struct Entry {
        uint64_t guid = 0; // 完整资产 GUID（回链用）
        /// 预解析实体树（批⑪ #M17；shared_ptr 跨 TU 持不完整 pimpl——删除器在
        /// SceneArchive.cpp 创建点捕获）
        std::shared_ptr<ecs::SceneArchive::ParsedEntityTree> parsed;
    };
    std::unordered_map<uint32_t, Entry> byId_; // 低 32 位 → 条目
    mutable std::unordered_set<uint32_t> warned_; // prefabId 错绑去重告警
};

} // namespace lemon::assets
