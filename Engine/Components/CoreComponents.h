// Lemon 引擎 — 组件目录 · Core 组（03 文档 §3.1）
// 全部纯数据（POD，trivially copyable——状态哈希/序列化的前提）；行为在系统。
#pragma once

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
    uint32_t team = 0;       // → TeamTable（块 5）
    uint16_t layer = 0;      // 碰撞/过滤层位掩码
    char tag[24] = {};       // 短标签（查找/调试；长名走资产 GUID）
};

/// 帧末销毁标记（Destroy 入队时打标，提取/查询按需过滤）
struct DestroyQueueTag {};

} // namespace lemon::ecs
