// Lemon 引擎 — 系统管线实现
#include "ECS/SystemPipeline.h"

#include <algorithm>
#include <chrono>
#include <cstring>

#include "Core/Log.h"
#include "ECS/Scene.h"
#include "ECS/World.h"

namespace lemon::ecs {

ISystem& SystemPipeline::AddSystem(std::unique_ptr<ISystem> sys) {
    LEMON_ASSERT(sys != nullptr, "null system");
    // [ISSUE-3] 重名防线查 systems_（原查 profiles_，ResolveOrder 前恒空 → 空转）
    for (const auto& existing : systems_)
        LEMON_ASSERT(std::strcmp(existing->Name(), sys->Name()) != 0,
                     "duplicate system: %s", sys->Name());
    systems_.push_back(std::move(sys));
    return *systems_.back();
}

void SystemPipeline::ResolveOrder() {
    // Kahn 拓扑：依赖边 = After(name)；每轮取"无未满足依赖"的最早注册者
    // （同秩按注册序 → 唯一确定性输出）
    const uint32_t n = (uint32_t)systems_.size();
    std::vector<uint32_t> place(n, UINT32_MAX); // 注册序 → 执行位
    std::vector<bool> placed(n, false);
    std::vector<uint32_t> order;
    order.reserve(n);

    for (uint32_t round = 0; round < n; ++round) {
        bool progressed = false;
        for (uint32_t i = 0; i < n; ++i) {
            if (placed[i]) continue;
            const char* after = systems_[i]->After();
            int32_t depIdx = -1;
            if (after) {
                for (uint32_t j = 0; j < n; ++j)
                    if (std::strcmp(systems_[j]->Name(), after) == 0) {
                        depIdx = (int32_t)j;
                        break;
                    }
                LEMON_ASSERT(depIdx >= 0, "After('%s') names unknown system", after);
                if (!placed[depIdx]) continue; // 依赖未就绪，本轮跳过
            }
            place[i] = (uint32_t)order.size();
            order.push_back(i);
            placed[i] = true;
            progressed = true;
            break; // 重头扫：保证"最早注册的可放置者"语义
        }
        LEMON_ASSERT(progressed, "system order cycle or duplicate dependency");
    }

    // 按执行序重排（稳定 move）
    std::vector<std::unique_ptr<ISystem>> reordered;
    reordered.reserve(n);
    for (uint32_t idx : order) reordered.push_back(std::move(systems_[idx]));
    systems_ = std::move(reordered);

    profiles_.clear();
    for (const auto& s : systems_) profiles_.push_back({s->Name(), 0, 0.0, 0.0f, 0.0f});
}

void SystemPipeline::RunStage(World& world, Scene& scene, SystemStage stage, float dt) {
    // [ISSUE-3] 未 ResolveOrder（或 ResolveOrder 后再 AddSystem）时 profiles_ 与
    // systems_ 长度不等 → 下文 profiles_[i] 越界 UB（Release 无容器断言）
    LEMON_ASSERT(profiles_.size() == systems_.size(),
                 "RunStage before ResolveOrder (or AddSystem after it)");
    using Clock = std::chrono::steady_clock;
    for (uint32_t i = 0; i < systems_.size(); ++i) {
        if (systems_[i]->Stage() != stage) continue;
        auto t0 = Clock::now();
        systems_[i]->Tick(world, scene, dt);
        float ms = std::chrono::duration<float, std::milli>(Clock::now() - t0).count();
        SystemProfile& p = profiles_[i];
        p.runs += 1;
        p.totalMs += ms;
        p.lastMs = ms;
        if (ms > p.maxMs) p.maxMs = ms;
    }
}

const SystemProfile* SystemPipeline::FindProfile(const char* name) const {
    for (const SystemProfile& p : profiles_)
        if (std::strcmp(p.name, name) == 0) return &p;
    return nullptr;
}

void SystemPipeline::ResetProfiles() {
    for (SystemProfile& p : profiles_) p.maxMs = p.lastMs;
}

void SystemPipeline::ZeroProfiles() {
    for (SystemProfile& p : profiles_) {
        p.runs = 0;
        p.totalMs = 0.0;
        p.lastMs = 0.0f;
        p.maxMs = 0.0f;
    }
}

} // namespace lemon::ecs
