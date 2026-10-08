// Lemon 引擎 — 换场编排（ADR-017 换场帧协议 v1 Single 同步路径；M7c 批⑥b）
// ⑥a 数据面（SceneMembership/场景档案/BuildInto）之上的执行面：单帧先清后装
// （批文件设计定案①：F1 形态②——半死窗口零容忍，装载帧 = 新场 Update 帧，
// 对齐协议"下一帧 Essential 段执行换场"）。消费入口三处：SceneSwitchSystem
// （管线 Essential #18 自动执行）/ 宿主与单测直调 Execute / 批⑧ async 状态机
// 的 ActivationGate 段（同协议加分帧门，D3 统一管线）。
// 红线：OnDestroy 通知只经 backend->NotifyPendingDestroys（先于 commit——
// 直调 Scene::CommitDestroys 跳过通知 = 静默漏 OnDestroy，F1 本体）。
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <utility>

#include "ECS/SystemPipeline.h"

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

class SceneSwitcher {
public:
    void SetHooks(SceneSwitchHooks h) { hooks_ = std::move(h); }

    /// 请求换场（单槽 last-wins：pending 未消费时再请求 = WARN 覆盖——Unity 连调
    /// LoadScene 最后者赢的同构简化；同帧多跳排队 v1 不做）
    void Request(SceneSwitchRequest&& r);

    bool HasPending() const { return hasPending_; }
    const SceneSwitchRequest& Pending() const { return pending_; }

    /// 编排主体（批文件"编排序列"六步；系统 Tick 与单测共用）。无 pending =
    /// NoPending 短路。Execute 内脚本回调（OnDestroy/Awake）再 Request = 进
    /// 单槽等下一帧（请求在入口即取走，重入安全）。
    SceneSwitchReport Execute(World& world, Scene& scene);

private:
    SceneSwitchRequest pending_;
    bool hasPending_ = false;
    SceneSwitchHooks hooks_;
};

/// 管线执行壳（Essential 段，After "DestroyCommitSystem"）——InstallDefaultSystems
/// 尾插（不消费 RNG 不占子流，AnimGraph/Tween 同款纪律）；编辑器 edit 世界不装
/// 默认管线 = 天然不进编辑态。批⑦ 起 C# LoadScene op 在此落地为 Request。
class SceneSwitchSystem final : public ISystem {
public:
    const char* Name() const override { return "SceneSwitch"; }
    SystemStage Stage() const override { return SystemStage::Essential; }
    // 依赖名 = DestroyCommitSystem::Name() 的实际串（"DestroyCommit"）
    const char* After() const override { return "DestroyCommit"; }
    void Tick(World& world, Scene& scene, float dt) override;
};

} // namespace lemon::ecs
