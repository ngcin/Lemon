// Lemon 引擎 — Scene：一个游戏状态（03 文档 §2）
// 对 EnTT registry 的封装层：EnTT 类型只允许出现在 Engine/ECS/*.h（本层）与系统
// 实现内部；Samples/Editor/Templates 只 include 本头与 Entity.h，不接触 entt::*。
//
// 实体销毁两阶段（03 §2 纪律）：
//   Destroy(e)  仅入销毁队列（系统遍历中安全）；
//   CommitDestroys()  DestroyCommit 系统在 Essential 阶段统一提交（真正的
//   registry.destroy + 池语义由 EnTT 实体回收承担）。重复入队天然幂等：提交时
//   以 registry.valid 校验（version 失配即已提交过）。
#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <entt/entt.hpp>

#include "ECS/Entity.h"

namespace lemon::physics2d {
class SpatialHash; // 前置：Scene 持有每场景 broadphase（实现见 Physics2D/）
}

namespace lemon::ecs {

class Scene {
public:
public:
    explicit Scene(const char* name);
    ~Scene(); // unique_ptr<SpatialHash> 析构需完整类型，定义在 .cpp

    Entity Create();
    /// 两阶段：入队，帧末 CommitDestroys 提交。线程安全（ProjectileLifetime 等
    /// 并行系统从 worker 线程调用；销毁队列与打标共用一把锁）。
    void Destroy(Entity e);
    bool Alive(Entity e) const;
    uint32_t AliveCount() const;

    template <typename T, typename... Args>
    T& Emplace(Entity e, Args&&... args) {
        if constexpr (std::is_empty_v<T>) {
            // entt 对空组件（tag）emplace/get 均为 void；tag 无数据，共享空实例即可
            registry_.emplace<T>(ToEntt(e), std::forward<Args>(args)...);
            static T kEmpty{};
            return kEmpty;
        } else {
            return registry_.emplace<T>(ToEntt(e), std::forward<Args>(args)...);
        }
    }
    template <typename T>
    T& Get(Entity e) {
        if constexpr (std::is_empty_v<T>) {
            static T kEmpty{};
            return kEmpty;
        } else {
            return registry_.get<T>(ToEntt(e));
        }
    }
    template <typename T>
    T* TryGet(Entity e) {
        if constexpr (std::is_empty_v<T>) {
            static T kEmpty{};
            return registry_.all_of<T>(ToEntt(e)) ? &kEmpty : nullptr;
        } else {
            return registry_.try_get<T>(ToEntt(e));
        }
    }
    template <typename T>
    bool Has(Entity e) const {
        return registry_.all_of<T>(ToEntt(e));
    }
    template <typename T>
    void Remove(Entity e) {
        registry_.remove<T>(ToEntt(e));
    }

    /// 多组件视图（引擎系统内部使用；返回类型属 EnTT，不越过封装层泄漏）
    template <typename... Ts, typename... Exclude>
    auto View(entt::exclude_t<Exclude...> ex = entt::exclude_t<Exclude...>{}) {
        return registry_.view<Ts...>(ex);
    }

    /// 单组件池直接访问（批量写热路径，如 MovementSystem 写 Transform2D）
    template <typename T>
    auto& Pool() {
        return registry_.storage<T>();
    }

    /// 遍历全部活跃实体（SceneArchive / 提取层用；序 = 实体池内部序）。
    /// 过滤无效槽位：entt 3.15 swap_only 策略下死亡槽位以 tombstone 留在池内。
    template <typename F>
    void Each(F&& fn) {
        auto& pool = registry_.template storage<entt::entity>();
        for (entt::entity e : pool)
            if (registry_.valid(e)) fn(FromEntt(e));
    }

    void CommitDestroys();
    uint32_t PendingDestroyCount() const { return (uint32_t)destroyQueue_.size(); }

    /// 本场景的空间哈希 broadphase（03 §2：每 Scene 一份；重建由系统管线驱动）
    physics2d::SpatialHash& Spatial();
    const physics2d::SpatialHash& Spatial() const;

    const char* Name() const { return name_.c_str(); }
    void SetName(const char* name) { name_ = name; } // 装载器恢复场景名（Save 写 name，Load 读回）
    uint64_t CreatedTotal() const { return createdTotal_; }
    uint64_t DestroyedTotal() const { return destroyedTotal_; }

    /// 封装层内部转换（系统实现可用；业务代码不经由 entt::entity 操作实体）
    static entt::entity ToEntt(Entity e) { return static_cast<entt::entity>((uint32_t)e.id - 1u); }
    static Entity FromEntt(entt::entity e) { return Entity{(uint64_t)(uint32_t)e + 1u}; }

    /// index/version 位切割（提取层映射数组用：idx 稠密、回收同槽新实体 version+1
    /// ——映射槽须校验 version 防串，2026-09-26 渲染提取批）。断言钉住 entt 布局，
    /// 依赖升级漂移即编译期报。
    static uint32_t EnttIndex(Entity e) {
        return ((uint32_t)e.id - 1u) & entt::entt_traits<entt::entity>::entity_mask;
    }
    static uint32_t EnttVersion(Entity e) {
        return (uint32_t)entt::to_version(ToEntt(e));
    }

    /// 底层 registry 直访（引擎系统/查询层实现用；不得越过封装层泄漏）
    entt::registry& Registry() { return registry_; }
    const entt::registry& Registry() const { return registry_; }

private:
    entt::registry registry_;
    std::vector<entt::entity> destroyQueue_;
    mutable std::mutex destroyMutex_; // Destroy（可多线程）↔ CommitDestroys 互斥
    std::string name_;
    std::unique_ptr<class physics2d::SpatialHash> spatial_;
    uint64_t createdTotal_ = 0;
    uint64_t destroyedTotal_ = 0;
};

} // namespace lemon::ecs
