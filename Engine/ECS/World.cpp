// Lemon 引擎 — World 实现（16 系统安装移入 Systems/，见 Systems.cpp）
#include "ECS/World.h"

namespace lemon::ecs {

World::World(const WorldDesc& desc)
    : desc_(desc),
      jobs_(std::make_unique<JobSystem>(desc.threadCount)),
      systemRngs_(kMaxSystemRngs),
      teams_(TeamTable::Default()), // 默认敌我表（03 §9）；资产侧加载后覆写
      events_(desc.initialEventCapacity) {}

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
    if (!active_) return;
    pipeline_.RunStage(*this, *active_, SystemStage::Essential, fixedDt);
    pipeline_.RunStage(*this, *active_, SystemStage::FixedTick, fixedDt);
    ++tick_;
}

} // namespace lemon::ecs
