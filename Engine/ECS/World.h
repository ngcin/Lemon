// Lemon 引擎 — World：进程级唯一，持全局服务（03 文档 §2）
// 持有：JobSystem（并行调度）、系统管线、事件队列、系统子流 RNG、Team 表、
// 输入快照、世界边界、事件汇（帧末派发）与预制体生成工厂（Spawn/Shooter 用）。
// 线程归属：Step 由调用方线程同步执行（bench-sim 主循环）；独立模拟线程与
// 双缓冲提取按 01 §2 在需要边跑边渲染时（M4+）启用，API 不绑线程。
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "Core/JobSystem.h"
#include "Core/Random.h"
#include "Core/RingQueue.h"
#include "Audio/AudioChannel.h"
#include "ECS/ClipTable.h"
#include "ECS/ControllerTable.h"
#include "ECS/Events.h"
#include "ECS/FxChannel.h"
#include "ECS/Input.h"
#include "ECS/SaveChannel.h"
#include "ECS/Scene.h"
#include "ECS/SceneSwitcher.h"
#include "ECS/SystemPipeline.h"
#include "ECS/TableStore.h"
#include "ECS/TeamTable.h"
#include "ECS/TweenTable.h"

namespace lemon::ecs {

// ---- Game RT UI 最小通道（M5 批① D6；M7a 出包 HUD 复用同通道）------------------
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
    /// #16 头部：拉脚本 pending 事件入队（并入当帧派发批次）
    virtual void PullPendingEvents(World& world) = 0;                       // #16 调用
    /// #16 主体：转发事件给 C# 订阅者。events 指向 #16 取出的稳定快照（与队列
    /// 底层分离）——订阅方回调内可安全再入队（2026-09-24 审查 P5：旧签名直读
    /// 队列段指针，回调 Push 触发扩容即悬空）
    virtual void DispatchEvents(World& world, Scene& scene, const EventPacket* events,
                                uint32_t count) = 0;                        // #16 调用
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

    // ---- 场景档案（ADR-017 D1；M7c 批⑥）----
    // 档2 场景管理的元数据面：Play 世界单 registry + SceneMembership 分组（见
    // SceneMembership.h），场景名/路径/装载态住 World。scenes_/active_（registry 面）
    // 保留——编辑器 edit/play 双 registry 与 GameEntry 单场景现状不动；LoadScene
    // 换场路径（批⑥b 编排）走档案 + membership，与 registry 面正交。句柄 0 保留
    // （kSceneHandleUnassigned），发号从 1 起单调递增（进程内；跨进程恒等性由
    // 装载次序保证——回放同序装载即同号）。
    struct SceneRecord {
        uint32_t handle = 0;
        std::string name;      // 场景名（.scene "name" 段恢复；UI 显示面）
        std::string path;      // 项目相对路径（ADR-017 D4 寻址：路径 > 唯一 stem）
        bool isLoaded = false; // 装载态（BuildInto 成功后置位，换场编排维护）
    };
    uint32_t CreateSceneRecord(const char* name, const char* path = "");
    uint32_t SceneRecordCount() const { return (uint32_t)sceneRecords_.size(); }
    /// 返回指针指向 vector 元素——**持有期间不得 CreateSceneRecord**（扩容失效）；
    /// 装载期建档、之后只读的编排形态天然安全（review 2026-10-08 F4 契约注记）
    const SceneRecord* SceneRecordAt(uint32_t index) const; // 越界 = nullptr
    SceneRecord* FindSceneRecord(uint32_t handle);
    const SceneRecord* FindSceneRecord(uint32_t handle) const;
    uint32_t ActiveSceneHandle() const { return activeSceneHandle_; }
    void SetActiveSceneHandle(uint32_t handle) { activeSceneHandle_ = handle; }

    // ---- 换场编排（ADR-017；M7c 批⑥b）----
    // 请求挂此（宿主/C# op 经 Request）；SceneSwitchSystem（Essential #18）每帧
    // 消费。hooks（sweep/afterBuild）由宿主装配期注入——World 不拥有 UI/资产源。
    SceneSwitcher& Switcher() { return switcher_; }
    const SceneSwitcher& Switcher() const { return switcher_; }

    SystemPipeline& Pipeline() { return pipeline_; }

    /// 分离力系统访问（2026-09-26 调参下放批：参数场景侧化——引擎默认不动 =
    /// 基准场零漂移，玩法经 Lemon.Physics.Separation 按场景覆盖）。未装默认
    /// 系统的自组管线 = nullptr，调用方自判
    class SeparationSystem* Separation() { return separation_; }

    RingQueue<EventPacket>& Events() { return events_; }
    TeamTable& Teams() { return teams_; }
    const TeamTable& Teams() const { return teams_; }

    // ---- 帧动画 clip 表（M5 批③；AnimatorSystem #13 帧映射消费）----
    // 空表 = 全体 Animator2D 走 M2 无 clip 旧路径（逐位不变）——既有场景/金档零漂移。
    ClipTable& Clips() { return clips_; }
    const ClipTable& Clips() const { return clips_; }

    // ---- 动画状态机 controller 表（M6a 批② T3d；ADR-013 D1 决策层。
    // AnimGraphSystem #16 图评估消费；World 持有 + 非 ECS 不入 StateHash——
    // 基准场零 AnimGraph 实例 = 零重录，ClipTable/TableStore 同款口径）----
    ControllerTable& Controllers() { return controllers_; }
    const ControllerTable& Controllers() const { return controllers_; }

    // ---- 配置表存储（M6a 批② T2；TableStore.h 头说明——.tab 全字符串格，
    // C# Lemon.Table 读；World 持有 + 非 ECS 不入 StateHash，零重录 09 §6.8 先例二）----
    TableStore& Tables() { return tables_; }
    const TableStore& Tables() const { return tables_; }

    // ---- Game RT UI 通道（上方 RtUiChannel 说明）----
    RtUiChannel& RtUi() { return rtUi_; }
    const RtUiChannel& RtUi() const { return rtUi_; }

    // ---- 世界空间表现通道（M6a 批①；FxChannel 头说明——飘字/血条恒走 sprite 管线，
    // 呈现层专用不入 StateHash，EnterPlay 新建 World 自清零）----
    FxChannel& Fx() { return fx_; }
    const FxChannel& Fx() const { return fx_; }

    // ---- 运行时属性补间表（A 档 tween，2026-09-28；TweenTable 头说明——C#
    // Lemon.Tween 建/Kill，TweenSystem 推进；指令态不入 StateHash，效果经组件
    // 字段入哈希；基准场零调用 = 零漂移，EnterPlay 新建 World 自清零）----
    TweenTable& Tweens() { return tweens_; }
    const TweenTable& Tweens() const { return tweens_; }

    // ---- 音频命令通道（M6c 批②，ADR-015 M3；AudioChannel 头说明——C# Lemon.Audio
    // 当帧 staging，AudioSystem #20 统一提交；BGM 单槽在表内（热重载随 World 存活）。
    // 非 ECS 不入 StateHash = 零重录通道族第八员，EnterPlay 新建 World 自清零）----
    audio::AudioChannel& Audio() { return audioChannel_; }
    const audio::AudioChannel& Audio() const { return audioChannel_; }

    // ---- 音频后端注入（宿主装配期：编辑器 EnterPlay / M7a 运行时；引擎指针 null
    // = 无声宿主全记账，script-tests/无头口径）。resolver = guid→clipId（staging 期
    // 解析，0 = 未注册；ctx 归宿主——编辑器 = audioClips_ map 成员地址稳定）----
    void SetAudioBackend(audio::AudioEngine* engine,
                         uint32_t (*resolveClip)(uint64_t guid, void* ctx), void* ctx);
    audio::AudioEngine* AudioSink() const { return audioSink_; }
    uint32_t ResolveAudioClip(uint64_t guid) const; // clipId；0 = 未注册/未装后端

    // ---- 音频监听器（宿主每帧推活动相机世界位 + 视口半宽；一帧延迟口径 =
    // FeedGameUiInput 先例；AudioSystem 空间化消费）----
    void SetAudioListener(const audio::AudioListener& l) { audioListener_ = l; }
    const audio::AudioListener& AudioListener() const { return audioListener_; }

    // ---- HUD 三选一卡片（上方 RtUiCards 说明）----
    RtUiCards& Cards() { return cards_; }
    const RtUiCards& Cards() const { return cards_; }

    // ---- 游戏存档通道（M5 批④ D1；M6a 批② T5 分档三通道——SaveChannel 头说明）----
    // C# Lemon.Save 写读（Chan 参数 → Saves(ch)）；IO 归宿主（编辑器钩子/打包
    // 运行时 M7a；OS 用户目录位 M8）。不入 StateHash。默认无参 = slot（既有调用点零改兼容）。
    SaveChannel& Saves() { return saves_[kSaveSlot]; }
    const SaveChannel& Saves() const { return saves_[kSaveSlot]; }
    SaveChannel& Saves(uint8_t ch) { return saves_[ClampSaveChannel(ch)]; }
    const SaveChannel& Saves(uint8_t ch) const { return saves_[ClampSaveChannel(ch)]; }

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

    // ---- XP 曲线系数（M6a 批② T4；ADR-012 D3：原 StatSystem 硬编码 1.25 提
    // World 级单参数，TimeScale 同款）。默认值不变 = 三金档哈希流逐位不变零重录；
    // 不 clamp（消费侧 max(1,ceil) 兜底，0/负不卡死升级环）。C# Lemon.Balance.XpCurveK ----
    void SetXpCurveK(float k) { xpCurveK_ = k; }
    float XpCurveK() const { return xpCurveK_; }

    // ---- 外部挂钩 ----
    void SetSpawnFn(SpawnFn fn) { spawnFn_ = std::move(fn); }
    void SetEventSink(EventSink sink) { eventSink_ = std::move(sink); }
    const SpawnFn& GetSpawnFn() const { return spawnFn_; }
    const EventSink& GetEventSink() const { return eventSink_; }
    // ---- 出生打标通道（M7c 批⑥b；Instantiate 落点 = active 场景句柄）----
    // 系统侧统一入口：工厂出生 + **子树即时打标**——保"零未打标"不变量（裸调
    // GetSpawnFn 绕过打标 = 下次装载 Stamp 全场收编误入新场组，禁止新调用面）
    bool HasSpawnFn() const { return (bool)spawnFn_; }
    Entity SpawnPrefab(uint32_t prefabId, Vec2 pos, uint32_t team);

    // ---- 脚本桥后端（#14/#15 消费；宿主注入，非拥有）----
    void SetScriptBackend(IScriptBackend* backend) { scriptBackend_ = backend; }
    IScriptBackend* ScriptBackend() const { return scriptBackend_; }

    /// 安装 03 §4 的 16 系统默认管线（注册序 = 表序 = RNG 子流 id）
    void InstallDefaultSystems();

    /// 固定步长模拟步：Essential → FixedTick → Extract → tick++（M7a 批③ 起补跑
    /// Extract 阶段——零注册系统 = no-op；首个真实现见 renderer::RenderExtractSystem）
    void Step(float fixedDt);

    uint64_t TickIndex() const { return tick_; }

private:
    WorldDesc desc_;
    std::unique_ptr<JobSystem> jobs_;
    std::vector<std::unique_ptr<Rng>> systemRngs_;
    std::vector<std::unique_ptr<Scene>> scenes_;
    Scene* active_ = nullptr;
    std::vector<SceneRecord> sceneRecords_;        // 档2 场景档案（批⑥；与 scenes_ 正交）
    uint32_t activeSceneHandle_ = 0;               // kSceneHandleUnassigned = 无
    uint32_t nextSceneHandle_ = 1;                 // 发号器（0 保留）
    SceneSwitcher switcher_;                       // 换场编排（批⑥b；SceneSwitchSystem 消费）
    SystemPipeline pipeline_;
    class SeparationSystem* separation_ = nullptr; // InstallDefaultSystems 填（生命周期同管线）
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
    ControllerTable controllers_; // T3d：.controller 状态机（Controllers()；非 ECS 通道，零重录）
    FxChannel fx_;
    SaveChannel saves_[kSaveChannelCount]; // M6a 批② T5：slot/settings/meta 三档
                                           // （档常量与键约定见 SaveChannel.h）
    TableStore tables_; // M6a 批②：.tab 配置表（Tables()；非 ECS 通道，零重录）
    TweenTable tweens_; // A 档补间（Tweens()；指令态通道，零重录）
    audio::AudioChannel audioChannel_; // M6c 批②：音频命令表（Audio()；零重录）
    audio::AudioEngine* audioSink_ = nullptr;       // 宿主注入（非拥有）
    uint32_t (*audioResolve_)(uint64_t, void*) = nullptr; // guid→clipId（宿主注入）
    void* audioResolveCtx_ = nullptr;
    audio::AudioListener audioListener_{};
    uint64_t tick_ = 0;
    float timeScale_ = 1.0f;
    float xpCurveK_ = 1.25f; // ADR-012 D3：默认 = 原 StatSystem 硬编码值（零重录）
};

} // namespace lemon::ecs
