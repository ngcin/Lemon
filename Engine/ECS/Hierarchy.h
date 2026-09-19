// Lemon 引擎 — Hierarchy 父子链维护与世界矩阵合成（03 §2；M4.1 内核 #1）
// 组件 Hierarchy 只存链指针（firstChild/next/prev/parent），全部维护经 SceneSetParent /
// SceneDetachChildren / SceneDestroyEntity——手改链字段 = 结构破损。
// 防环：parent 链深度上限 8（03 §2 纪律）；SetParent 目标若是自身后代 → 拒绝（false）。
// M2 复审 N6 遗留的环检测测试随本文件落地（tests/engine_tests.cpp TestHierarchy）。
#pragma once

#include "Core/Math.h"
#include "ECS/Entity.h"
#include "ECS/Scene.h"

namespace lemon::ecs {

/// 层级深度上限（成环/过深统一按此拒绝；03 §2）
inline constexpr uint32_t kMaxHierarchyDepth = 8;

struct WorldTransform2D {
    Vec2 pos{};
    float rot = 0.0f;      // 弧度
    Vec2 scale{1.0f, 1.0f};
};

/// 挂子（child → newParent；newParent 为 null = 摘根）。已完成 sibling 双链摘除与
/// 挂接；拒绝并返回 false 的情形：child 无效/已死、newParent 已死、成环（newParent
/// 在 child 的后代子树内）、深度超限。拒绝时场景结构不动。
bool SceneSetParent(Scene& s, Entity child, Entity newParent);

/// 摘根（等价 SceneSetParent(s, e, Entity::Null()) 的语义糖；实体已是根也返回 true）
bool SceneDetach(Scene& s, Entity e);

/// 销毁实体并递归销毁后代（编辑器删除/场景清理用；游戏侧通常走两阶段 Destroy）。
/// 直接提交销毁（不经 destroyQueue_）——编辑器在帧外调用，无遍历中危险。
void SceneDestroyEntityTree(Scene& s, Entity e);

/// parent 链上行合成世界变换（TRS 复合，语义同 Mat3x2::FromTRS 乘积）。
/// 失败（无 Transform2D 祖先中断/深度超限/成环）返回 false：调用方回退本地变换并告警。
bool ComputeWorldTransform(Scene& s, Entity e, WorldTransform2D& out);

/// 深度检测：e 的 parent 链长度（根 = 0）；链异常（死引用/环）返回 -1
int HierarchyDepth(Scene& s, Entity e);

} // namespace lemon::ecs
