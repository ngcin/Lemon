// Lemon 引擎 — SceneMembership：场景成员标签（ADR-017 D1；M7c 批⑥，批⑦ D1/D2 修订）
// 档2 场景管理的数据面："场景"从执行/数据边界降级为数据分组——运行时 Play 世界
// 单 registry，实体携带本组件指明所属场景档案（World::SceneRecord 发号）；换场 =
// 帧边界销毁非 DDOL 系实体 + BuildInto 追加新组（Unity"对象不死"语义本体：DDOL
// 实体句柄/组件/ScriptBox 全不动，C# 实例表天然续命）。
// ScriptBox 同款纪律：普通 entt 组件、不入 ComponentRegistry——不进 .scene 序列化
// （装载/实例化单点打标）与 StateHash（批⑥ 查②定案：membership 不入哈希流，金
// 回放零重录；实体在册即入哈希，漏清场/幸存者漂移由现有哈希抓）。编辑器编辑态
// 单组 = 不打标（scene==kSceneHandleUnassigned 全组语义，兼容现状）。
// 批⑦ D1 根位式修订（2026-10-08 用户拍板）：DDOL 位只标根——子树幸存由清场判据
// 的祖先链检查兑现（后挂子实体随根幸存、移出 DDOL 树的实体随新归属清场 = Unity
// 语义全对齐；位粘实体：reparent 不摘位，无反标记 API——Unity 同款）。
#pragma once

#include <cstdint>
#include <type_traits>
#include <vector>

#include "ECS/Entity.h"
#include "ECS/Scene.h"

namespace lemon::ecs {

/// 场景句柄 0 = 未指派（编辑器编辑态/装载前缺省——全组语义；World 发号从 1 起）
inline constexpr uint32_t kSceneHandleUnassigned = 0;

struct SceneMembership {
    uint32_t scene = kSceneHandleUnassigned; // 所属场景档案句柄（World 侧发号）
    uint32_t flags = 0;                      // bit0 = DontDestroyOnLoad（见下）
};

/// flags bit0：跨场景幸存（ADR-017 D5；批⑦ D1 起只落在 DDOL 根上）。清场判据
/// 只看本位 + 祖先链（IsDontDestroyOnLoadLineage）；scene 值保留来源信息（编辑器
/// 按位分组显示归批⑩）；根树整体幸存 = Unity 同款语义。
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

/// DDOL 系判定（批⑦ D1 清场判据本体）：自身或任一祖先带 DDOL 位 = 幸存系。
/// parent 链深度护栏同子树遍历（kMaxHierarchyDepth）。无 membership/链异常 = false。
bool IsDontDestroyOnLoadLineage(Scene& s, Entity e);

/// DDOL 根标记（批⑦ D1 根位式）：**只标 root 一点**（子树幸存交清场判据的祖先
/// 链检查——后挂/移出均自动正确）。未打标实体（编辑态根）先补标再置位；幂等。
/// 返回 1 = 标记成功；0 = root 无效。
uint32_t MarkDontDestroyOnLoad(Scene& s, Entity root);

/// Single 清场报告（批⑦ D2）
struct SceneClearReport {
    uint32_t queued = 0;               // 入队实体数
    uint32_t unassignedCollected = 0;  // 其中未指派（无组件/scene==0）——不变量破坏的
                                       // WARN 面（自愈不遮羞：编排侧红字一次）
};

/// Single 换场清场（批⑦ D2 用户拍板）：除 DDOL 系外**全部**入队（跨组 + 未指派
/// 自愈）——Single「清世界装新场」语义本体。**入队不提交**，提交须走管线
/// #17 DestroyCommitSystem（OnDestroy 补发在系统内 NotifyPendingDestroys——直接调
/// Scene::CommitDestroys = 静默漏通知，编排禁用，review 2026-10-08 F1）。
SceneClearReport QueueDestroyAllExceptDdolLineage(Scene& s);

/// 组清场入队（批⑥ 原语保留——测试/未来 Additive 用；Single 换场走上项）：
/// membership==sceneHandle 且非 DDOL 系的实体入队。返回入队数。
uint32_t QueueDestroySceneGroup(Scene& s, uint32_t sceneHandle);

/// 子树打标（M7c 批⑥b prefab spawn 通道）：root 连同全部后代仅收编未指派实体
/// （同 StampSceneMembership 判据，域缩到子树）——World::SpawnPrefab 出生即打
/// active 句柄，保"零未打标"不变量（否则 spawn 实体被下次装载的 Stamp 全场收编
/// 误入新场组）。返回收编数。
uint32_t StampTreeMembership(Scene& s, Entity root, uint32_t sceneHandle);

/// 统计（smoke/单测断言面；Scene 查询面无 const 重载，同 ComputeStateHash 口径收
/// 非 const）。CountSceneGroup 含同组 DDOL 实体；未打标实体计入
/// kSceneHandleUnassigned 组（编辑态全组）。CountDontDestroyOnLoad = DDOL **根**
/// 计数（批⑦ D1 起位只在根——幸存者全集用 CollectDontDestroyOnLoadLineage）。
uint32_t CountSceneGroup(Scene& s, uint32_t sceneHandle);
uint32_t CountDontDestroyOnLoad(Scene& s);

/// DDOL 系幸存者全集（smoke/单测断言面）：自身或祖先带位的全部存活实体（池序）
std::vector<Entity> CollectDontDestroyOnLoadLineage(Scene& s);

/// 组内根实体数（批⑦ C# Scene.rootCount 数据面）：membership==sceneHandle 且
/// 父缺失/父亡/父属他组。O(N) 全 registry 扫描——查询面低频，逐帧轮询勿用。
uint32_t CountSceneRoots(Scene& s, uint32_t sceneHandle);

} // namespace lemon::ecs
