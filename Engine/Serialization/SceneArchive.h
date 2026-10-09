// Lemon 引擎 — 场景存取（.scene，06 文档 §3：JSON + schema 版本化 + 迁移链；
// 2026-09-19 扩展名由 .lscene 更名，格式 schema 不变）
// v1（M2）：实体 + 全组件目录（注册表元数据驱动）；viewport/tilemap/RLE 段
// M6 扩展；Prefab 覆盖段 M4 编辑器侧。
// v2（M6a 批⓪）：实体脚本单数 "script" → 复数 "scripts":[{guid,class},…]
// （每实体多脚本）。迁移链 v1→v2 升级旧档；.prefab 的 LoadEntityTree 不走迁移，
// ReadEntity 双读（复数优先/单数兼容）隐式升级。
// 格式：{"schemaVersion":2, "name":"...", "entities":[{"components":{…},
//   "scripts":[{"guid":…,"class":"…"}]}]}
//   * 组件按注册表 name 匹配；未知组件名 = 新版档旧引擎 → 跳过并告警（前向兼容）；
//   * 实体引用（EntityRef 字段）序列化为 "eN" 索引，读档时 remap；
//   * 旧 schemaVersion 逐级迁移到当前版本后解析（03 §13 迁移链）。
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "ECS/Entity.h"
#include "ECS/Scene.h"

namespace lemon::ecs {

class SceneArchive {
public:
    static constexpr uint32_t kSchemaVersion = 2;

    /// 导出为 JSON 文本（开发态 .scene；发布态 .baked 由 packager 转换，M7）
    /// 实体含 ScriptBox 时附加 "scripts":[{guid,class}] 数组（M6a 批⓪ v2，原单数
    /// script；typeId 注册序不持久，装载侧按 className 找宿主映射）。
    static std::string Save(Scene& scene);

    /// 从 JSON 文本载入（清空目标 Scene 后重建）；失败返回 false（已告警）
    static bool Load(Scene& scene, const std::string& jsonText);

    /// 构建段装载（ADR-017 D3 Build 阶段；M7c 批⑥）：**不清空**——实体追加进目标
    /// Scene（换场编排负责旧组清场；DDOL 实体在场即共存 = 单 registry membership
    /// 语义，见 SceneMembership.h）。解析/校验/迁移失败返回 false 且场景不动；成功
    /// 后调用方 StampSceneMembership 打标。Load = 本函数 + 清空前奏（行为同旧）。
    static bool BuildInto(Scene& scene, const std::string& jsonText);

    /// 只读预检（M7c 批⑥b 换场原子性）：ParseSceneDoc 同链（解析 + 迁移到当前
    /// 版本 + 段校验），零构建零副作用。换场编排清场前调用——失败 = 响亮取消，
    /// 世界逐位不动。成功不保证 BuildInto 必成（实体段构建异常的极端路径仍可能
    /// 失败，编排侧红字交底）。
    static bool ValidateParse(const std::string& jsonText);

    /// 读档名（M7c 批⑦）：同 ParseSceneDoc 链（解析 + 迁移）零构建；out = doc
    /// "name" 段（缺省清空）。宿主 resolveScene 钩子填 SceneSwitchRequest.name 用。
    /// 解析失败返回 false（告警已在内）。
    static bool SceneDocName(const std::string& jsonText, std::string& out);

    /// 版本迁移链：把 fromVersion 的文档升到 kSchemaVersion（逐级）。
    /// 每级迁移是一个纯 json→json 变换；无法处理返回 false。
    static bool Migrate(std::string& jsonText, uint32_t fromVersion);

    // ---- 实体子树 IO（M4.4 Prefab 最小集，06 §4：结构同场景实体段）----
    /// 导出 root 子树（父子序 = Hierarchy 链序；跨子树 EntityRef 写 null；
    /// Meta.guid 原样保留——实例化侧负责换新）。
    static std::string SaveEntityTree(Scene& scene, Entity root);

    /// 从 JSON 实例化子树（新实体集，EntityRef 子树内重映射、跨树引用置空，
    /// root 挂为独立根）。Meta.guid 全部换新（GenerateGuid）。返回新 root。
    static Entity LoadEntityTree(Scene& scene, const std::string& jsonText);
};

/// 分帧预备构建（ADR-017 D3 分帧状态机 Parse/Build 段；M7c 批⑧ D1=A）：
/// LoadSceneAsync 的预备段载体——Parse 段产物（Json DOM + 档名）+ Build 段分帧
/// 游标与槽账。**全 pimpl 在 SceneArchive.cpp**（nlohmann 红线：json 类型不出
/// 本头文件）。消费方 = SceneSwitcher 异步装载机：Parse 一次 → CreateSlots/
/// DecodeEntities 按预算分帧建进**暂存 Scene**（主世界逐位不动——加载帧哈希流
/// 不变 = 回放激活帧契约基础）；激活帧按 Ledger() 在主 registry 复刻 BuildInto
/// 的槽位分配序列（含坏条目建-毁路径），句柄与同步路径逐位一致。
class StagedSceneBuild {
public:
    /// Parse 段：解析 + 迁移 + 校验（ParseSceneDoc 同链——与 BuildInto/
    /// ValidateParse 零分叉）。失败返回 nullptr（告警已在内）。**原子不可分帧**
    ///（单次解析调用；大档超预算属已知口径，Build 段才是预算约束主体）。
    static std::unique_ptr<StagedSceneBuild> Parse(const std::string& jsonText);
    ~StagedSceneBuild();

    uint32_t EntityCount() const;      // 档内实体条目数（含坏条目——槽账口径）
    uint32_t CreatedCount() const;     // 相一游标（已建槽数——进度权重面）
    uint32_t DecodedCount() const;     // 相二游标（已解码条数——进度权重面）
    /// 档是否含 "name" 字符串段（ApplySceneName 同构判据——集成段仅在真有时
    /// 覆写主场景名，无名档保持现名；review F2）
    bool HasName() const;
    const std::string& Name() const;   // 档 "name" 段（缺省空串；ApplySceneName 同源）

    /// Build 段相一：doc 序建槽（暂存 Scene::Create，maxN 上限/帧）。返回本相
    /// 是否全部建毕。分帧期间台账 = 已建槽前缀。
    bool CreateSlots(Scene& staging, uint32_t maxN);
    /// Build 段相二：逐实体解码（ReadEntity 同码；坏条目 = 暂存槽即时回收——
    /// 台账保留原句柄，有效性以 staging.Alive() 为判据）。返回是否全部解码完毕。
    bool DecodeEntities(Scene& staging, uint32_t maxN);
    /// 槽账（doc 序句柄全量——含坏条目槽；**未建槽位 = Null**，坏条目 = 已回收
    /// 句柄，有效性以 staging.Alive() 判据）。激活帧集成的复刻依据 + 档内
    /// EntityRef 重映射源（坏槽引用与同步路径同样解析到"已建后毁"句柄）。
    const std::vector<Entity>& Ledger() const;

    /// 释放 Json DOM——**分帧版**（万实体档 DOM 递归析构数十 ms，整释会破帧
    /// 预算）：每次从 entities 数组尾部 resize 掉 maxElems 棵子树（O(1)/元素），
    /// 清空后连同 doc 对象整体释放。返回是否全部释放完毕（此后仅剩槽账/档名）。
    bool ReleaseDocChunk(uint32_t maxElems);

    StagedSceneBuild(const StagedSceneBuild&) = delete;
    StagedSceneBuild& operator=(const StagedSceneBuild&) = delete;

private:
    StagedSceneBuild() = default;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace lemon::ecs
