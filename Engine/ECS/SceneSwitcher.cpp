// Lemon 引擎 — 换场编排实现（ADR-017 换场帧协议；M7c 批⑥b）
#include "ECS/SceneSwitcher.h"

#include "Core/Log.h"
#include "Audio/AudioEngine.h"
#include "ECS/Scene.h"
#include "ECS/SceneMembership.h"
#include "ECS/World.h"
#include "Serialization/SceneArchive.h"

namespace lemon::ecs {

void SceneSwitcher::Request(SceneSwitchRequest&& r) {
    if (hasPending_)
        LEMON_WARN("scene switch: pending '%s' overwritten by '%s'（单槽 last-wins）",
                   pending_.name.c_str(), r.name.c_str());
    pending_ = std::move(r);
    hasPending_ = true;
}

SceneSwitchReport SceneSwitcher::Execute(World& world, Scene& scene) {
    SceneSwitchReport rep;
    if (!hasPending_) return rep;
    hasPending_ = false; // 入口取走——OnDestroy/Awake 回调内再 Request 进下一帧
    SceneSwitchRequest req = std::move(pending_);

    // 0. 原子性预检：坏档不清场（世界逐位不动）
    if (!SceneArchive::ValidateParse(req.jsonText)) {
        LEMON_ERROR("scene switch: '%s' 解析失败——换场取消，世界保持原状",
                    req.path.c_str());
        rep.status = SceneSwitchStatus::ParseFailed;
        return rep;
    }
    const uint32_t oldHandle = world.ActiveSceneHandle();
    rep.oldHandle = oldHandle;

    // 1-3. 清场（协议 ①③；批⑦ D2 用户拍板）：除 DDOL 系外全清 = Single「清世界
    // 装新场」语义本体（跨组 + 未指派自愈；自愈不遮羞——未打标泄漏红字一次）。
    // 入队 → OnDestroy 补发（恰好一次 flag 去重，池序 = 确定序——批文件设计定案②）
    // → 提交。notify 必须先于 commit（F1 本体）。
    const SceneClearReport clear = QueueDestroyAllExceptDdolLineage(scene);
    if (clear.unassignedCollected > 0)
        LEMON_WARN("scene switch: 自愈收编未打标实体 %u 个（零未打标不变量被破坏"
                   "——请上报路径）",
                   clear.unassignedCollected);
    rep.destroyedOldGroup = clear.queued;
    if (auto* backend = world.ScriptBackend())
        backend->NotifyPendingDestroys(world, scene);
    scene.CommitDestroys();

    // 4. 随行清扫：引擎件直兑（D6 Audio.Paused 强制清 = 引擎承诺引擎兑付，
    // 不经钩子——防"清场语义靠宿主自觉"回潮）；宿主件（UI origin=Scene 卸载）经钩子
    world.Fx().Clear();
    if (auto* audio = world.AudioSink()) audio->SetPaused(false);
    if (hooks_.sweep) hooks_.sweep();
    // 协议 ⑤（批⑦ D3）：sceneUnloaded = ①③ 收口后（旧档案此时尚未翻 isLoaded，
    // C# 侧事件载荷查询到的 = 装载态快照）
    if (oldHandle != 0 && world.ScriptBackend())
        world.ScriptBackend()->SceneEventNotify(world, scene, SceneEventKind::Unloaded,
                                                oldHandle, 0, req.mode);

    // 5-7. 装载（协议 ④）：建档 → BuildInto 追加 → 打标（仅收编未指派）→ 宿主
    // 后置 pass（F3：SpriteRef 归一/脚本解析（Awake/OnEnable）/UI 声明装载）→
    // 档案态收口（协议 ⑤ 的数据面；事件推送归批⑦）
    const uint32_t newHandle = world.CreateSceneRecord(req.name.c_str(), req.path.c_str());
    rep.newHandle = newHandle;
    if (!SceneArchive::BuildInto(scene, req.jsonText)) {
        // 预检过的极端失败（实体段构建异常）：世界已清场——响亮交底，档案标未装载。
        // v1 不做事务回滚（批文件设计定案③已知敞口，与 Unity 装载失败炸场同级）
        LEMON_ERROR("scene switch: '%s' BuildInto 失败（预检已过）——世界已清场，"
                    "新场未装载",
                    req.path.c_str());
        rep.status = SceneSwitchStatus::ParseFailed;
        return rep;
    }
    rep.stampedNew = StampSceneMembership(scene, newHandle);
    // 档案面收口先于 afterBuild：装载语义原子完成（新场 Awake/OnEnable 内
    // GetActiveScene 已是新场——Unity sceneLoaded 时序同构；批⑦ 事件族在
    // afterBuild 段尾推）
    if (World::SceneRecord* prev = world.FindSceneRecord(oldHandle))
        prev->isLoaded = false;
    if (World::SceneRecord* rec = world.FindSceneRecord(newHandle)) rec->isLoaded = true;
    world.SetActiveSceneHandle(newHandle);
    if (hooks_.afterBuild) hooks_.afterBuild(scene);
    // 协议 ⑤（批⑦ D3）：sceneLoaded = Awake/OnEnable（afterBuild）后、本帧 Start
    // 之前（#15 批量 tick 在 Update 段，晚于此处）→ activeSceneChanged 收口。
    if (auto* backend = world.ScriptBackend()) {
        backend->SceneEventNotify(world, scene, SceneEventKind::Loaded, oldHandle,
                                  newHandle, req.mode);
        if (oldHandle != 0)
            backend->SceneEventNotify(world, scene, SceneEventKind::ActiveChanged,
                                      oldHandle, newHandle, req.mode);
    }
    rep.ddolSurvivors = (uint32_t)CollectDontDestroyOnLoadLineage(scene).size();
    rep.status = SceneSwitchStatus::Success;
    // 批⑦ 手测可观测性补：成功换场此前静默（只能靠 afterBuild 侧日志间接推断）——
    // 宿主侧直接可见一条（失败路径各分支已各自红字）
    const World::SceneRecord* oldRec = world.FindSceneRecord(oldHandle);
    LEMON_LOG("scene switch: '%s' → '%s'（清 %u / DDOL 幸存 %u / 新组 %u）",
              oldRec ? oldRec->name.c_str() : "?", req.name.c_str(),
              rep.destroyedOldGroup, rep.ddolSurvivors, rep.stampedNew);
    return rep;
}

void SceneSwitchSystem::Tick(World& world, Scene& scene, float dt) {
    (void)dt;
    world.Switcher().Execute(world, scene);
}

} // namespace lemon::ecs
