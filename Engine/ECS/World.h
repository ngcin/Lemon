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
#include "ECS/ClipTable.h"
#include "ECS/Events.h"
#include "ECS/FxChannel.h"
#include "ECS/Input.h"
#include "ECS/SaveChannel.h"
#include "ECS/Scene.h"
#include "ECS/SystemPipeline.h"
#include "ECS/TeamTable.h"

namespace lemon::ecs {

// ---- Game RT UI 最小通道（M5 批① D6；M8 打包 HUD 复用同通道）------------------
// World 级定长槽（8 × key/text/frac）：C# Lemon.Ui.Set 写入，编辑器 GameView /
// 打包 HUD 读出。命中 key 即覆写、空槽即占、满槽忽略；text 截断 47 字符。
// 呈现层专用——不入 StateHash（ComputeStateHash 只哈希 Scene），EnterPlay 新建
// World 自清零。
struct RtUiSlot {
    char key[16] = {};
    char text[48] = {};
    float frac = -1.0f; // <0 纯文本；[0,1] 附进度条
    uint32_t color = 0; // 0 = 默认色；非 0 = ABGR（文本与进度条着色，M5 批④）
};

class RtUiChannel {
public:
    bool Set(const char* key, const char* text, float frac,
             uint32_t color = 0); // 命中覆写/空槽即占；text 截断 47 字符
    bool Clear(const char* key);  // 删单行（M5 批④：结算后清 HUD；尾槽前移）
    void Clear() { count_ = 0; }
    uint32_t Count() const { return count_; }
    const RtUiSlot& At(uint32_t i) const { return slots_[i]; }

private:
    RtUiSlot slots_[8]{};
    uint32_t count_ = 0;
};

// ---- HUD 三选一卡片（M5 批④ D3；升级选择/死亡重开的交互回读通道）----------------
// C# Ui.ShowCards 写入、GameView 呈现按钮并回写 pick、C# Ui.CardPick 消费式读。
// 选择属用户 IO 不入输入快照（金档场景通道空转 = 回放零漂移；依赖卡片选择的
// 场景不进金回放口径——09 §7 分类）。不入 StateHash。
struct RtUiCards {
    bool active = false;
    char title[48] = {};
    char labels[3][48] = {};
    int32_t pick = -1; // 已选索引（呈现层写；ConsumePick 读后置 -1）

    void Show(const char* title, const char* a, const char* b, const char* c);
    void Hide() { active = false; pick = -1; }
    int32_t ConsumePick() {
        const int32_t p = pick;
        pick = -1;
        return p;
    }
};

/// 脚本桥后端（M3：Scripting/ScriptHost 实现；由宿主构造并注入 World，World 不拥有）
struct IScriptBackend {
    virtual ~IScriptBackend() = default;
    virtual void TickBatch(World& world, Scene& scene, float dt) = 0;       // #15 调用
    virtual void DispatchEvents(World& world, Scene& scene) = 0;            // #16 调用
    virtual void ApplyStructural(World& world, Scene& scene) = 0;           // DestroyCommit 前调用
    /// 销毁提交前补通知（2026-09-24 审查 F-08.2）：C++ 系统路径入队的销毁在此
    /// 触发脚本 OnDestroy——此前只有脚本命令路径通知，托管实例/订阅残留到换域。
    virtual void NotifyPendingDestroys(World& world, Scene& scene) = 0;     // CommitDestroys 前调用
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

    // ---- 帧动画 clip 表（M5 批③；AnimatorSystem #13 帧映射消费）----
    // 空表 = 全体 Animator2D 走 M2 无 clip 旧路径（逐位不变）——既有场景/金档零漂移。
    ClipTable& Clips() { return clips_; }
    const ClipTable& Clips() const { return clips_; }

    // ---- Game RT UI 通道（上方 RtUiChannel 说明）----
    RtUiChannel& RtUi() { return rtUi_; }
    const RtUiChannel& RtUi() const { return rtUi_; }

    // ---- 世界空间表现通道（M6a 批①；FxChannel 头说明——飘字/血条恒走 sprite 管线，
    // 呈现层专用不入 StateHash，EnterPlay 新建 World 自清零）----
    FxChannel& Fx() { return fx_; }
    const FxChannel& Fx() const { return fx_; }

    // ---- HUD 三选一卡片（上方 RtUiCards 说明）----
    RtUiCards& Cards() { return cards_; }
    const RtUiCards& Cards() const { return cards_; }

    // ---- 游戏存档通道（M5 批④ D1；SaveChannel 头说明）----
    // C# Lemon.Save 写读；IO 归宿主（编辑器钩子/打包运行时 M8）。不入 StateHash。
    SaveChannel& Saves() { return saves_; }
    const SaveChannel& Saves() const { return saves_; }

    // ---- 输入（InputSnapshot 系统消费的快照通道；录制/回放共用）----
    const InputState& Input() const { return input_; }
    void ApplyInput(const InputState& in) { input_ = in; }

    // ---- 世界边界（Movement 钳制 / 投射物越界回收；无边界则不钳）----
    void SetBounds(Rect bounds) { bounds_ = bounds; hasBounds_ = true; }
    bool HasBounds() const { return hasBounds_; }
    Rect Bounds() const { return bounds_; }

    // ---- 时间缩放（01 §2：作用于模拟步进，不影响渲染插值；M5 批①）----
    // =0 冻结暂停（tick 照推、RNG 不消耗——回放帧对齐保持）；clamp [0,8]。
    // C# 侧（Time.Scale）由事件驱动 = 回放内确定性；编辑器侧改动属调试操作不入输入快照。
    void SetTimeScale(float s) { timeScale_ = s < 0.0f ? 0.0f : (s > 8.0f ? 8.0f : s); }
    float TimeScale() const { return timeScale_; }

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
    RtUiChannel rtUi_;
    RtUiCards cards_;
    ClipTable clips_;
    FxChannel fx_;
    SaveChannel saves_;
    uint64_t tick_ = 0;
    float timeScale_ = 1.0f;
};

} // namespace lemon::ecs
