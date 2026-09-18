// Lemon 引擎 — Scene 非模板部分实现
#include "ECS/Scene.h"

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
    destroyQueue_.push_back(ToEntt(e));
}

bool Scene::Alive(Entity e) const {
    return !e.IsNull() && registry_.valid(ToEntt(e));
}

uint32_t Scene::AliveCount() const {
    // entt 3.15：实体池 packed 数组大小即活跃数（回收实体即时移出 packed）
    return (uint32_t)registry_.storage<entt::entity>()->size();
}

void Scene::CommitDestroys() {
    if (destroyQueue_.empty()) return;
    for (entt::entity e : destroyQueue_) {
        if (registry_.valid(e)) { // version 失配（重复入队/已提交）静默跳过
            registry_.destroy(e);
            ++destroyedTotal_;
        }
    }
    destroyQueue_.clear();
}

} // namespace lemon::ecs
