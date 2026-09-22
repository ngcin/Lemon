// Lemon 引擎 — 系统管线（03 文档 §4 / 01 §3.2）
// 三阶段：Essential（场景生命周期/销毁提交）→ FixedTick（60Hz 模拟步）→
// Extract（渲染提取，M4 接入）。阶段内顺序 = 声明式 After 依赖的拓扑序，
// 同秩按注册序（确定性：同一注册集恒得同一执行序）。
// 每系统独立计时（steady_clock）进 profile——F3 面板/性能回退定位的数据源。
#pragma once

#include <cstdint>
#include <memory>
#include <vector>

namespace lemon::ecs {

class World;
class Scene;

enum class SystemStage : uint8_t { Essential = 0, FixedTick, Extract };

struct SystemProfile {
    const char* name = "";
    uint64_t runs = 0;      // 累计执行次数
    double totalMs = 0.0;   // 累计耗时（ms）
    float lastMs = 0.0f;
    float maxMs = 0.0f;
};

class ISystem {
public:
    virtual ~ISystem() = default;

    virtual const char* Name() const = 0;
    virtual SystemStage Stage() const { return SystemStage::FixedTick; }
    /// 声明"必须晚于某系统"（按名字）；单前驱足够表达 03 §4 线性序
    virtual const char* After() const { return nullptr; }
    virtual void Tick(World& world, Scene& scene, float dt) = 0;
};

class SystemPipeline {
public:
    /// 注册系统（注册序 = 系统 RNG 子流 id，见 World::SystemRng）
    ISystem& AddSystem(std::unique_ptr<ISystem> sys);

    /// 拓扑解析（AddSystem 后调用一次；重复注册需先 Reset）。依赖缺失/成环 → 断言失败
    void ResolveOrder();

    void RunStage(World& world, Scene& scene, SystemStage stage, float dt);

    const std::vector<std::unique_ptr<ISystem>>& Systems() const { return systems_; }
    const std::vector<SystemProfile>& Profiles() const { return profiles_; }
    const SystemProfile* FindProfile(const char* name) const;
    void ResetProfiles(); // F3 峰值清零（面板刷新周期用：max 基线移到 last）
    void ZeroProfiles();  // 计数全清零（测量窗口起点；性能批② sim 分解用）

private:
    std::vector<std::unique_ptr<ISystem>> systems_; // 执行序（ResolveOrder 后）
    std::vector<SystemProfile> profiles_;
};

} // namespace lemon::ecs
