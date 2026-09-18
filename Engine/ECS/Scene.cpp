// Lemon 引擎 — Scene 非模板部分实现
#include "ECS/Scene.h"

#include "Components/CoreComponents.h"
#include "Physics2D/SpatialHash.h"

namespace lemon::ecs {

Scene::Scene(const char* name)
    : name_(name),
      spatial_(std::make_unique<physics2d::SpatialHash>()) {}

Scene::~Scene() = default;

physics2d::SpatialHash& Scene::Spatial() { return *spatial_; }
const physics2d::SpatialHash& Scene::Spatial() const { return *spatial_; }

Entity Scene::Create() {
    ++createdTotal_;
    return FromEntt(registry_.create());
}

void Scene::Destroy(Entity e) {
    if (e.IsNull()) return;
    entt::entity ent = ToEntt(e);
    {
        // 线程安全：ProjectileLifetimeSystem 在 ParallelFor worker 中调用本函数
        //（裸 vector 并发 push_back 是数据竞争——审计修复）。锁内同时打销毁标记，
        // 提取/查询层按 DestroyQueueTag 过滤当帧待删实体；Commit 时随 destroy 移除。
        std::lock_guard<std::mutex> lock(destroyMutex_);
        if (registry_.valid(ent) && !registry_.all_of<DestroyQueueTag>(ent))
            registry_.emplace<DestroyQueueTag>(ent);
        destroyQueue_.push_back(ent);
    }
}

bool Scene::Alive(Entity e) const {
    return !e.IsNull() && registry_.valid(ToEntt(e));
}

uint32_t Scene::AliveCount() const {
    // entt 3.15 实体池删除策略 = swap_only：销毁实体以 tombstone 留在 packed 数组
    // 等回收，storage size() 含回收位（审计修复：原实现销毁后虚高）。
    // 精确计数 = 累计创建 - 累计提交销毁（O(1)）。
    return (uint32_t)(createdTotal_ - destroyedTotal_);
}

void Scene::CommitDestroys() {
    std::vector<entt::entity> pending;
    {
        std::lock_guard<std::mutex> lock(destroyMutex_);
        if (destroyQueue_.empty()) return;
        pending.swap(destroyQueue_);
    }
    for (entt::entity e : pending) {
        if (registry_.valid(e)) { // version 失配（重复入队/已提交）静默跳过
            registry_.destroy(e); // 含 DestroyQueueTag，一并移除
            ++destroyedTotal_;
        }
    }
}

} // namespace lemon::ecs
