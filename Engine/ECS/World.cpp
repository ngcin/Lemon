// Lemon 引擎 — World 实现（16 系统安装移入 Systems/，见 Systems.cpp）
#include "ECS/World.h"

#include "ECS/ComponentRegistry.h"

namespace lemon::ecs {

World::World(const WorldDesc& desc)
    : desc_(desc),
      jobs_(std::make_unique<JobSystem>(desc.threadCount)),
      systemRngs_(kMaxSystemRngs),
      events_(desc.initialEventCapacity),
      teams_(TeamTable::Default()) { // 默认敌我表（03 §9）；资产侧加载后覆写
    // 组件目录自动登记（幂等）。StateHash/SceneArchive/桥都遍历注册表，
    // 宿主漏调即静默空转——M2 的 bench-sim 回放因此恒真过验（ISSUE-9），
    // 自此登记随首个 World 构造发生，不再依赖调用方纪律。
    RegisterAllComponents();
}

World::~World() = default;

Rng& World::SystemRng(uint32_t systemId) {
    auto& slot = systemRngs_[systemId & (systemRngs_.size() - 1)];
    if (!slot) slot = std::make_unique<Rng>(desc_.seed, kRngStreamBase + systemId);
    return *slot;
}

Scene& World::CreateScene(const char* name) {
    scenes_.push_back(std::make_unique<Scene>(name));
    return *scenes_.back();
}

void World::Step(float fixedDt) {
    if (!active_) return; // 无活动场景 = 空步（不崩；tick 不推进）
    pipeline_.RunStage(*this, *active_, SystemStage::Essential, fixedDt);
    pipeline_.RunStage(*this, *active_, SystemStage::FixedTick, fixedDt);
    ++tick_;
}

} // namespace lemon::ecs
