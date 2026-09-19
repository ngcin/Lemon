// Lemon 引擎 — World：进程级唯一，持全局服务（03 文档 §2）
// 持有：JobSystem（并行调度）、系统管线、事件队列、系统子流 RNG、Team 表、
// 输入快照、世界边界、事件汇（帧末派发）与预制体生成工厂（Spawn/Shooter 用）。
// 线程归属：Step 由调用方线程同步执行（bench-sim 主循环）；独立模拟线程与
// 双缓冲提取按 01 §2 在需要边跑边渲染时（M4+）启用，API 不绑线程。
#pragma once

#include <functional>
#include <memory>
#include <vector>

#include "Core/JobSystem.h"
#include "Core/Random.h"
#include "Core/RingQueue.h"
#include "ECS/Events.h"
#include "ECS/Input.h"
#include "ECS/Scene.h"
#include "ECS/SystemPipeline.h"
#include "ECS/TeamTable.h"

namespace lemon::ecs {

/// 脚本桥后端（M3：Scripting/ScriptHost 实现；由宿主构造并注入 World，World 不拥有）
struct IScriptBackend {
    virtual ~IScriptBackend() = default;
    virtual void TickBatch(World& world, Scene& scene, float dt) = 0;       // #14 调用
    virtual void DispatchEvents(World& world, Scene& scene) = 0;            // #15 调用
    virtual void ApplyStructural(World& world, Scene& scene) = 0;           // DestroyCommit 前调用
};

struct WorldDesc {
    uint64_t seed = 0x4C454D4F4Eull; // "LEMON"（确定性回放的根种子）
    int threadCount = 0;             // 0=自动；1=单线程诊断档（--threads 1）
    uint32_t initialEventCapacity = 4096;
};

/// 系统子流容量（2 的幂，按位掩码取槽）与子流 id 基址（与用户子流空间区分）
static constexpr uint32_t kMaxSystemRngs = 256;
static constexpr uint64_t kRngStreamBase = 0x4C320000ull;

class World {
public:
    /// 预制体实例化工厂（SpawnSystem/Shooter 调用；bench-sim/玩法层注册，
    /// 编辑器与模板接资产管线后指向 PrefabInstantiate）
    using SpawnFn = std::function<Entity(Scene&, uint32_t prefabId, Vec2 pos,
                                         uint32_t team)>;
    /// 事件汇（ScriptEventDispatch 帧末批量派发；M3 换 C# 桥，同一接口）
    using EventSink = std::function<void(World&, const EventPacket&)>;

    explicit World(const WorldDesc& desc = {});
    ~World();

    World(const World&) = delete;
    World& operator=(const World&) = delete;

    const WorldDesc& Desc() const { return desc_; }
    JobSystem& Jobs() { return *jobs_; }

    /// 系统独立子流随机源（systemId = 系统注册序，稳定；惰性创建一次）
    Rng& SystemRng(uint32_t systemId);

    Scene& CreateScene(const char* name);
    void SetActiveScene(Scene* scene) { active_ = scene; }
    Scene* ActiveScene() { return active_; }

    SystemPipeline& Pipeline() { return pipeline_; }

    RingQueue<EventPacket>& Events() { return events_; }
    TeamTable& Teams() { return teams_; }
    const TeamTable& Teams() const { return teams_; }

    // ---- 输入（InputSnapshot 系统消费的快照通道；录制/回放共用）----
    const InputState& Input() const { return input_; }
    void ApplyInput(const InputState& in) { input_ = in; }

    // ---- 世界边界（Movement 钳制 / 投射物越界回收；无边界则不钳）----
    void SetBounds(Rect bounds) { bounds_ = bounds; hasBounds_ = true; }
    bool HasBounds() const { return hasBounds_; }
    Rect Bounds() const { return bounds_; }

    // ---- 外部挂钩 ----
    void SetSpawnFn(SpawnFn fn) { spawnFn_ = std::move(fn); }
    void SetEventSink(EventSink sink) { eventSink_ = std::move(sink); }
    const SpawnFn& GetSpawnFn() const { return spawnFn_; }
    const EventSink& GetEventSink() const { return eventSink_; }

    // ---- 脚本桥后端（#14/#15 消费；宿主注入，非拥有）----
    void SetScriptBackend(IScriptBackend* backend) { scriptBackend_ = backend; }
    IScriptBackend* ScriptBackend() const { return scriptBackend_; }

    /// 安装 03 §4 的 16 系统默认管线（注册序 = 表序 = RNG 子流 id）
    void InstallDefaultSystems();

    /// 固定步长模拟步：Essential → FixedTick → tick++（Extract 阶段 M4 渲染侧驱动）
    void Step(float fixedDt);

    uint64_t TickIndex() const { return tick_; }

private:
    WorldDesc desc_;
    std::unique_ptr<JobSystem> jobs_;
    std::vector<std::unique_ptr<Rng>> systemRngs_;
    std::vector<std::unique_ptr<Scene>> scenes_;
    Scene* active_ = nullptr;
    SystemPipeline pipeline_;
    RingQueue<EventPacket> events_;
    TeamTable teams_;
    InputState input_;
    Rect bounds_{};
    bool hasBounds_ = false;
    SpawnFn spawnFn_;
    EventSink eventSink_;
    IScriptBackend* scriptBackend_ = nullptr;
    uint64_t tick_ = 0;
};

} // namespace lemon::ecs
