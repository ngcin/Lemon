// Lemon 引擎 — SceneMembership：场景成员标签（ADR-017 D1；M7c 批⑥）
// 档2 场景管理的数据面："场景"从执行/数据边界降级为数据分组——运行时 Play 世界
// 单 registry，实体携带本组件指明所属场景档案（World::SceneRecord 发号）；换场 =
// 帧边界销毁旧组非 DDOL 实体 + BuildInto 追加新组（Unity"对象不死"语义本体：
// DDOL 实体句柄/组件/ScriptBox 全不动，C# 实例表天然续命）。
// ScriptBox 同款纪律：普通 entt 组件、不入 ComponentRegistry——不进 .scene 序列化
// （装载/实例化单点打标）与 StateHash（批⑥ 查②定案：membership 不入哈希流，金
// 回放零重录；实体在册即入哈希，漏清场/幸存者漂移由现有哈希抓）。编辑器编辑态
// 单组 = 不打标（scene==kSceneHandleUnassigned 全组语义，兼容现状）。
#pragma once

#include <cstdint>
#include <type_traits>

#include "ECS/Entity.h"
#include "ECS/Scene.h"

namespace lemon::ecs {

/// 场景句柄 0 = 未指派（编辑器编辑态/装载前缺省——全组语义；World 发号从 1 起）
inline constexpr uint32_t kSceneHandleUnassigned = 0;

struct SceneMembership {
    uint32_t scene = kSceneHandleUnassigned; // 所属场景档案句柄（World 侧发号）
    uint32_t flags = 0;                      // bit0 = DontDestroyOnLoad（见下）
};

/// flags bit0：跨场景幸存（ADR-017 D5）。清场判据只看本位（scene 值保留来源信息，
/// 编辑器按位分组显示归批⑩）；根树整体幸存 = Unity 同款语义。
inline constexpr uint32_t kSceneFlagDontDestroyOnLoad = 1u << 0;

// 卫生钉（CoreComponents 同款纪律；本组件不过 C# blittable 边界——C# Scene 结构
// 只携带句柄，此断言仅防无谓的非平凡化漂移）
static_assert(std::is_trivially_copyable_v<SceneMembership> && sizeof(SceneMembership) == 8,
              "SceneMembership 布局钉（8 字节两 u32）");

/// 打标（装载/实例化单点）：**仅收编未打标实体**（无组件或 scene==0）——增量装载
/// （BuildInto 二装）只影响本次新建实体；DDOL/既有组的 scene 值与 flags 均不动
/// （幸存者来源信息保留）。返回本次收编数。运行时 Play registry 不变量 = 零未打标
/// 实体（smoke 断言面；编辑器编辑态除外——不打标即全组）。
uint32_t StampSceneMembership(Scene& s, uint32_t sceneHandle);

/// 组清场入队：membership==sceneHandle 且无 DDOL 位的实体全部入两阶段销毁队列
/// （03 §2 脚本实体死亡豁免属 HP 战斗路径，换场清场直通不适用）。**入队不提交**，
/// 且**提交须走管线 #17 DestroyCommitSystem**（OnDestroy 通知在系统内
/// NotifyPendingDestroys 补发——直接调 Scene::CommitDestroys = 静默漏通知，编排
/// 禁用，review 2026-10-08 F1）。返回入队调用数（已在销毁队列的实体重复入队会被
/// 提交期幂等去重，但计数含重入——编排应在帧边界原子 queue+commit，不混路径）。
uint32_t QueueDestroySceneGroup(Scene& s, uint32_t sceneHandle);

/// DDOL 根树重标签（ADR-017 D5：标记根 = 整棵子树幸存）：root 连同全部后代打
/// DDOL 位，不改 scene 值。非根调用改作用于根的裁决归 C# 门面（WARN 后调本函数
/// 传根）；引擎层不判亲缘。返回重标签实体数。
uint32_t MarkDontDestroyOnLoadTree(Scene& s, Entity root);

/// 子树打标（M7c 批⑥b prefab spawn 通道）：root 连同全部后代仅收编未指派实体
/// （同 StampSceneMembership 判据，域缩到子树）——World::SpawnPrefab 出生即打
/// active 句柄，保"零未打标"不变量（否则 spawn 实体被下次装载的 Stamp 全场收编
/// 误入新场组）。返回收编数。
uint32_t StampTreeMembership(Scene& s, Entity root, uint32_t sceneHandle);

/// 统计（smoke/单测断言面；Scene 查询面无 const 重载，同 ComputeStateHash 口径收
/// 非 const）。CountSceneGroup 含同组 DDOL 实体；未打标实体计入
/// kSceneHandleUnassigned 组（编辑态全组）。
uint32_t CountSceneGroup(Scene& s, uint32_t sceneHandle);
uint32_t CountDontDestroyOnLoad(Scene& s);

} // namespace lemon::ecs
