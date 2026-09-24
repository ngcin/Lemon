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

#include <string>

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

} // namespace lemon::ecs
