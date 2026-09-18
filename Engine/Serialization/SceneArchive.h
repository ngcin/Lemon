// Lemon 引擎 — 场景存取（.lscene，06 文档 §3：JSON + schema 版本化 + 迁移链）
// v1 范围（M2）：实体 + 全组件目录（注册表元数据驱动）；viewport/tilemap/RLE 段
// M6 扩展；Prefab 覆盖段 M4 编辑器侧。
// 格式：{"schemaVersion":1, "name":"...", "entities":[{"components":{...}}]}
//   * 组件按注册表 name 匹配；未知组件名 = 新版档旧引擎 → 跳过并告警（前向兼容）；
//   * 实体引用（EntityRef 字段）序列化为 "eN" 索引，读档时 remap；
//   * 旧 schemaVersion 逐级迁移到当前版本后解析（03 §13 迁移链）。
#pragma once

#include <string>

#include "ECS/Scene.h"

namespace lemon::ecs {

class SceneArchive {
public:
    static constexpr uint32_t kSchemaVersion = 1;

    /// 导出为 JSON 文本（开发态 .lscene；发布态 .lbaked 由 packager 转换，M7）
    static std::string Save(Scene& scene);

    /// 从 JSON 文本载入（清空目标 Scene 后重建）；失败返回 false（已告警）
    static bool Load(Scene& scene, const std::string& jsonText);

    /// 版本迁移链：把 fromVersion 的文档升到 kSchemaVersion（逐级）。
    /// 每级迁移是一个纯 json→json 变换；无法处理返回 false。
    static bool Migrate(std::string& jsonText, uint32_t fromVersion);
};

} // namespace lemon::ecs
