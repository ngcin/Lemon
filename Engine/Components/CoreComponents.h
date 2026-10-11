// Lemon 引擎 — 组件目录 · Core 组（03 文档 §3.1）
// 全部纯数据（POD，trivially copyable——状态哈希/序列化的前提）；行为在系统。
#pragma once

#include <cstring>
#include <string>
#include <type_traits>

#include "Core/Math.h"
#include "ECS/Entity.h"

namespace lemon::ecs {

struct Transform2D {
    Vec2 pos{};
    float rot = 0.0f;      // 弧度（Math.h 约定；C# 门面提供 deg 视图）
    Vec2 scale{1.0f, 1.0f};
};

/// 系统管线中间量（03 §4 隐含、§3 目录未列）：AI/分离/击退写，Movement 积分读
struct Velocity {
    Vec2 v{};
};

/// 父子层级（03 §2：渲染 transform 提取时合成世界矩阵，深度 ≤ 8 层防环）
struct Hierarchy {
    Entity parent{};
    Entity firstChild{};
    Entity next{};
    Entity prev{};
};

struct Meta {
    uint64_t prefabId = 0;   // 0 = 非实例；回链供编辑器 Apply/Revert
    uint32_t team = 0;       // → TeamTable（块 5）；位索引语义，合法域 [0,32)
    uint16_t layer = 0;      // 碰撞/过滤层位索引（[0,16)，查询侧 1<<layer）
    char tag[24] = {};       // 短标签（查找/调试；长名走资产 GUID）
    uint64_t guid = 0;       // 持久实体 GUID（M4.1 内核 #5：编辑器选中找回/Undo 往返/
                             // Prefab 回链共用；0 = 运行时生成实体（脚本 Spawn 等），编辑器
                             // 创建的实体恒非 0。Core/Guid.h 生成）
};

/// tag[24] 定长拷贝（**UTF-8 边界对齐截断**，tag 写入三链路共用：SceneArchive
/// 读档 / EditorContext 建实体 / Hierarchy 改名）。裸按字节截断会把多字节中文
/// 切成半字符驻留内存——JSON 序列化时 nlohmann 抛 type_error.316 崩编辑器
///（2026-10-11 light.scene 超 24B 中文 tag 实抓）。超限时回退到尾字符起始字节。
inline void CopyTag24(char (&dst)[24], const char* src) {
    size_t n = 0;
    while (n < 23 && src[n] != '\0') ++n; // min(strlen, 23)——末字节保 \0
    while (n > 0 && (unsigned char)src[n - 1] >= 0x80) {
        size_t lead = n - 1;
        while (lead > 0 && ((unsigned char)src[lead] & 0xC0) == 0x80) --lead; // 续字节回溯
        const unsigned char b = (unsigned char)src[lead];
        const size_t need = (b & 0xE0) == 0xC0   ? 2
                            : (b & 0xF0) == 0xE0 ? 3
                            : (b & 0xF8) == 0xF0 ? 4
                                                 : 1;
        if (n - lead >= need) break; // 尾字符完整（≥0x80 但成序列）
        n = lead;                    // 半字符：截到该字符之前
    }
    std::memcpy(dst, src, n);
    dst[n] = '\0';
}
inline void CopyTag24(char (&dst)[24], const std::string& s) { CopyTag24(dst, s.c_str()); }

/// 帧末销毁标记（Destroy 入队时打标，提取/查询按需过滤）
struct DestroyQueueTag {};

// ---- 布局冻结（M3 桥侧 blittable 前提：C# 镜像 struct 与此逐字节对齐，改动=破回放）----
static_assert(std::is_trivially_copyable_v<Transform2D> && sizeof(Transform2D) == 20, "Transform2D 布局冻结");
static_assert(std::is_trivially_copyable_v<Velocity> && sizeof(Velocity) == 8, "Velocity 布局冻结");
static_assert(std::is_trivially_copyable_v<Hierarchy> && sizeof(Hierarchy) == 32, "Hierarchy 布局冻结");
static_assert(std::is_trivially_copyable_v<Meta> && sizeof(Meta) == 48, "Meta 布局冻结（M4.1 增 guid 40→48，C# 镜像同步；破 M3 前回放档）");
static_assert(std::is_trivially_copyable_v<DestroyQueueTag> && sizeof(DestroyQueueTag) == 1, "DestroyQueueTag 布局冻结");

} // namespace lemon::ecs
