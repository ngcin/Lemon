// Lemon 引擎 — 换场编排（ADR-017 换场帧协议 v1 Single 同步路径；M7c 批⑥b）
// ⑥a 数据面（SceneMembership/场景档案/BuildInto）之上的执行面：单帧先清后装
// （批文件设计定案①：F1 形态②——半死窗口零容忍，装载帧 = 新场 Update 帧，
// 对齐协议"下一帧 Essential 段执行换场"）。消费入口三处：SceneSwitchSystem
// （管线 Essential #18 自动执行）/ 宿主与单测直调 Execute / 批⑧ async 状态机
// 的激活段（同协议，预备段分帧 + 激活帧原子 = D3 统一管线）。
// 红线：OnDestroy 通知只经 backend->NotifyPendingDestroys（先于 commit——
// 直调 Scene::CommitDestroys 跳过通知 = 静默漏 OnDestroy，F1 本体）。
// M7c 批⑧：AsyncSceneLoader（分帧状态机）入此——Parse→Build(暂存 registry)
// →Assets 占位→Gate→激活帧原子集成。单槽统一（D3=A）：任一新请求（同步或
// 异步）WARN 后取代在途者；被取代 async op 的 completed 不推（取消语义）。
#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "ECS/SystemPipeline.h"
#include "Serialization/SceneArchive.h" // StagedSceneBuild（异步机预备段载体；批⑧）

namespace lemon::ecs {

class World;
class Scene;

/// 换场请求（宿主侧完成 D4 寻址：路径 > 唯一 stem > 响亮失败——引擎层不碰 IO）
struct SceneSwitchRequest {
    std::string name;     // 档案名（.scene "name" 段）
    std::string path;     // 项目相对路径（档案寻址键）
    std::string jsonText; // 场景 JSON（ValidateParse 内含迁移链）
    uint8_t mode = 0;     // LoadSceneMode（C# 镜像：0=Single；1=Additive 预留——
                          // vtable 侧红字拒，此处不达非 0 值）
};

/// 宿主回调面（引擎件清扫编排直兑；宿主件与资产解析经钩子注入——
/// SetScriptBackend 同款注入纪律，World 不拥有 UI/资产源）
struct SceneSwitchHooks {
    /// 随行清扫（销毁提交后、装载前）：UI origin=Scene 文档卸载（R10）等宿主件。
    std::function<void()> sweep;
    /// 装载后置 pass（BuildInto + 打标后；F3）：SpriteRef 归一 / ScriptBox 解析
    /// （Awake/OnEnable 在此同步触发）/ UIDocument 声明装载——镜像宿主装配序。
    std::function<void(Scene&)> afterBuild;
};

enum class SceneSwitchStatus : uint8_t {
    NoPending = 0, // 无请求（系统 Tick 常规路径，零成本）
    Success,
    ParseFailed, // ValidateParse 预检失败——世界逐位不动（原子性）
};

/// 执行结果（smoke/单测断言面；批⑦ sceneLoaded/sceneUnloaded 事件族的数据源）
struct SceneSwitchReport {
    SceneSwitchStatus status = SceneSwitchStatus::NoPending;
    uint32_t destroyedOldGroup = 0; // 清场入队总数（批⑦ D2 起为"除 DDOL 系外全清"
                                    // 的口径——名义旧组，含自愈收编的未指派实体）
    uint32_t ddolSurvivors = 0;     // 换场后 DDOL 系幸存者总数（含后挂子实体；多跳
                                    // 累积，语义见 CollectDontDestroyOnLoadLineage）
    uint32_t stampedNew = 0;        // 新组打标收编数（零孤组不变量下 = 新建实体数）
    uint32_t oldHandle = 0;
    uint32_t newHandle = 0;         // ValidateParse 失败 = 0（未建档）；BuildInto
                                    // 极端失败已建档非 0（isLoaded=false，设计定案③ 敞口）
};

/// 异步装载机查询快照（NativeSceneAsyncQuery 载荷；批⑧ §4 契约的机械定义）
struct AsyncSceneQuery {
    float progress = 0.0f; // 0..1；门关封顶 0.9（Unity 同款）；终态 1.0（失败终态
                           // 保持中止时刻值——世界不动，进度无 1.0 语义可给）
    uint8_t isDone = 0;    // 1 = 终态（激活完成或装载期失败）
};

/// 异步装载分帧状态机（M7c 批⑧；ADR-017 D3"一条管线两个门面"的 async 面）。
/// 状态：Parse（原子，StagedSceneBuild::Parse）→ BuildCreate/BuildDecode（按
/// 预算分帧建进**暂存 Scene**——主世界逐位不动、旧场照常 tick、哈希流不变）→
/// Assets（占位秒过，未来 M9 tilemap 接入位）→ Gate（allowSceneActivation 门，
/// 关 = 停 0.9 每帧重查）→ 激活（**单个 Essential 窗口原子执行**：清场原码 +
/// 集成 + 打标 + afterBuild + 三事件 + AsyncCompleted 收口）。工作小一帧走完 =
/// 与同步路径同帧效（Tick 内预算未尽且门开 → 当帧直达激活）。
class AsyncSceneLoader {
public:
    /// 预算（毫秒/帧，墙钟；默认 4ms——ADR D3 开工实测定量级）。只约束 Build
    /// 分帧段；Parse 段原子不可分帧、激活段为原子窗口（预算豁免——集成 = create
    /// + memcpy，无 JSON 解码，Unity 激活帧同款有界尖峰）。
    void SetBudgetMs(float ms) { budgetMs_ = ms > 0.0f ? ms : 0.0f; }
    float BudgetMs() const { return budgetMs_; }

    bool HasInFlight() const { return phase_ != Phase::Idle; }
    uint32_t InFlightOpId() const { return opId_; }

    /// 入机（opId 单调从 1；0 保留）。在途另有 op = WARN + 取代（D3=A 单槽统一：
    /// completed 不推——取消语义）。**同步侧 pending_ 互斥由 SceneSwitcher 统一
    /// 编排（Request 路径先取消本机）**。
    uint32_t Request(SceneSwitchRequest&& r);

    /// 每帧 Essential #18 内推进（SceneSwitchSystem::Tick 在 Execute 之后调用；
    /// Execute 当帧消费了同步 pending = 本机已被取消，Tick 自然 no-op）。
    /// 返回 true = 本帧发生了激活（激活后自复位 Idle）。
    bool Tick(World& world, Scene& scene);

    /// 查询（在途或终态缓存命中；返回 false = 未知 opId——含被取代者）。
    bool Query(uint32_t opId, AsyncSceneQuery& out) const;
    /// 门（allowSceneActivation）：在途 op 受理返回 true；终态/未知 = false
    /// （终态不可改门）。Unity 同款：false 期间 progress 封顶 0.9。
    bool SetActivation(uint32_t opId, bool allow);

    /// 取消在途（单槽统一裁决的执行面；reason 入红字交底）
    void Cancel(const char* reason);

private:
    friend class SceneSwitcher;
    enum class Phase : uint8_t {
        Idle = 0,
        Parse,       // StagedSceneBuild::Parse（原子）
        BuildCreate, // 建槽分帧（暂存 registry）
        BuildDecode, // 逐实体解码分帧
        Assets,      // 占位（v1 秒过；M9 接入位）
        Gate,        // 预备毕、等 allowSceneActivation
    };
    void FinishActivation(bool success, uint32_t finishedOp); // 终态化（被新请求
                                  // 取代则跳过——重入护栏，见实现注）

    uint32_t nextOpId_ = 1;
    uint32_t opId_ = 0;         // 在途 op（0 = 无）
    uint32_t doneOpId_ = 0;     // 终态缓存（完成后查询可达：Unity op 完成后仍可查）
    AsyncSceneQuery done_;      // 终态快照（progress=1/isDone=1，或失败中止值）
    Phase phase_ = Phase::Idle;
    bool allowActivation_ = true;
    float progress_ = 0.0f;     // 0..0.9（staged 权重）；激活成功跳 1.0
    float budgetMs_ = 4.0f;
    SceneSwitchRequest req_;
    std::unique_ptr<StagedSceneBuild> staged_;
    std::unique_ptr<Scene> staging_; // 暂存 registry（Build 段载体；激活后释放）
};

class SceneSwitcher {
public:
    void SetHooks(SceneSwitchHooks h) { hooks_ = std::move(h); }

    /// 请求换场（单槽 last-wins：pending 未消费时再请求 = WARN 覆盖——Unity 连调
    /// LoadScene 最后者赢的同构简化；同帧多跳排队 v1 不做）。在途 async op =
    /// 先取消（批⑧ 单槽统一：同步显式请求赢，WARN 交底）。
    void Request(SceneSwitchRequest&& r);

    bool HasPending() const { return hasPending_; }
    const SceneSwitchRequest& Pending() const { return pending_; }

    /// 编排主体（批文件"编排序列"六步；系统 Tick 与单测共用）。无 pending =
    /// NoPending 短路。Execute 内脚本回调（OnDestroy/Awake）再 Request = 进
    /// 单槽等下一帧（请求在入口即取走，重入安全）。
    SceneSwitchReport Execute(World& world, Scene& scene);

    // ---- 异步面（批⑧）----
    AsyncSceneLoader& Async() { return async_; }
    const AsyncSceneLoader& Async() const { return async_; }

    /// 异步换场请求（单槽统一入口：清除未消费同步 pending + 在途 async 取代
    /// WARN，均批⑧ D3=A 语义）。返回 opId（1 起；由 SceneSwitchSystem 每帧
    /// Essential 推进——宿主/单测亦可直调 TickAsync）。
    uint32_t RequestAsync(SceneSwitchRequest&& r);
    /// 异步机推进（Execute 之后同一 Essential 窗口调用；SceneSwitchSystem 消费）：
    /// 预备段分帧 + 激活（共核 RunSwitch）+ kind3 收口推送。
    void TickAsync(World& world, Scene& scene);

private:
    /// 换场协议共核（同步 Execute 与异步激活段共用；批⑧ 共核分节——同步路径
    /// 行为逐字节不变，孪生测/金回放机械复核）。staged 非空 = 异步激活：装载位
    /// 换集成（IntegrateStaged），完成后尾推 AsyncCompleted；jsonText 路径 =
    /// BuildInto 原码。激活结果通过 asyncOpId/asyncProgress 回填异步机。
    SceneSwitchReport RunSwitch(World& world, Scene& scene, SceneSwitchRequest&& req,
                                StagedSceneBuild* staged, Scene* staging,
                                uint32_t asyncOpId);

    SceneSwitchRequest pending_;
    bool hasPending_ = false;
    SceneSwitchHooks hooks_;
    AsyncSceneLoader async_;
};

/// 集成段（批⑧ D1=A 核心）：按台账在主 registry **复刻 BuildInto 槽位分配
/// 序列**——doc 序建槽全量（含坏条目槽）→ 逐好条目组件 memcpy（注册表元数据
/// 驱动 + ScriptBox 特例）→ EntityRef 字段暂存句柄→主句柄重映射 → 坏槽销毁
/// 提交。同步/异步激活句柄逐位一致的结构保证（同清场 + 同建槽序 + 同回收路径）。
/// 返回主侧槽账（与入参台账同长同序，Null = 坏条目槽）。
std::vector<Entity> IntegrateStaged(Scene& live, Scene& staging,
                                    const std::vector<Entity>& ledger);

/// 管线执行壳（Essential 段，After "DestroyCommitSystem"）——InstallDefaultSystems
/// 尾插（不消费 RNG 不占子流，AnimGraph/Tween 同款纪律）；编辑器 edit 世界不装
/// 默认管线 = 天然不进编辑态。批⑦ 起 C# LoadScene op 在此落地为 Request；批⑧
/// 起 Execute 后再推 AsyncSceneLoader::Tick（同一 Essential 窗口）。
class SceneSwitchSystem final : public ISystem {
public:
    const char* Name() const override { return "SceneSwitch"; }
    SystemStage Stage() const override { return SystemStage::Essential; }
    // 依赖名 = DestroyCommitSystem::Name() 的实际串（"DestroyCommit"）
    const char* After() const override { return "DestroyCommit"; }
    void Tick(World& world, Scene& scene, float dt) override;
};

} // namespace lemon::ecs
