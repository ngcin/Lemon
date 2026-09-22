// Lemon 引擎 — World 实现（17 系统安装移入 Systems/，见 Systems.cpp）
#include "ECS/World.h"

#include <cstdio>
#include <cstring>

#include "ECS/ComponentRegistry.h"

namespace lemon::ecs {

bool RtUiChannel::Set(const char* key, const char* text, float frac) {
    if (!key || !key[0] || !text) return false;
    uint32_t found = count_;
    for (uint32_t i = 0; i < count_; ++i)
        if (std::strcmp(slots_[i].key, key) == 0) {
            found = i;
            break;
        }
    if (found == count_) {
        if (count_ >= 8) return false; // 满槽忽略（8 行 HUD 上限，文档声明）
        ++count_;
    }
    RtUiSlot& s = slots_[found];
    std::snprintf(s.key, sizeof s.key, "%s", key);
    std::snprintf(s.text, sizeof s.text, "%s", text); // 截断 47 字符
    s.frac = frac;
    return true;
}

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
    // 时间缩放（M5 批①）：全 FixedTick 系统吃缩放 dt（C# Time.DeltaTime 同值）；
    // Essential（销毁提交）不消费 dt，缩放无语义差
    const float dt = fixedDt * timeScale_;
    pipeline_.RunStage(*this, *active_, SystemStage::Essential, dt);
    pipeline_.RunStage(*this, *active_, SystemStage::FixedTick, dt);
    ++tick_;
}

} // namespace lemon::ecs
