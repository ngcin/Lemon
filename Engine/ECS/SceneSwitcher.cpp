// Lemon 引擎 — 换场编排实现（ADR-017 换场帧协议；M7c 批⑥b，批⑧ 扩异步面）
#include "ECS/SceneSwitcher.h"

#include <cstring>
#include <unordered_map>

#include "Core/Log.h"
#include "Audio/AudioEngine.h"
#include "ECS/ComponentRegistry.h"
#include "ECS/Scene.h"
#include "ECS/SceneMembership.h"
#include "ECS/World.h"
#include "Scripting/ScriptBox.h"
#include "Serialization/SceneArchive.h"

namespace lemon::ecs {

// ---------------------------------------------------------- 异步装载机 ----
// 批⑧ D1=A：Parse→Build 建进暂存 registry（主世界逐位不动——加载帧哈希流不变 =
// "回放走同步路径+记录激活帧"契约的机械基础）；激活 = 单 Essential 窗口原子执行
// （清场原码 + 集成 + 事件）。progress 权重（批文件 §4）：Parse 0.09 / 建槽
// 0.045 / 解码 0.675 / Assets 0.09 → 预备毕 0.9；门关停 0.9；激活 1.0。

uint32_t AsyncSceneLoader::Request(SceneSwitchRequest&& r) {
    if (phase_ != Phase::Idle) {
        LEMON_WARN("scene async: op %u（'%s'）被 '%s' 取代（单槽 last-wins——被取代"
                   " op 已取消，completed 不推）",
                   opId_, req_.name.c_str(), r.name.c_str());
    }
    req_ = std::move(r);
    staged_.reset();
    staging_.reset();
    progress_ = 0.0f;
    allowActivation_ = true;
    opId_ = nextOpId_++;
    phase_ = Phase::Parse;
    return opId_;
}

void AsyncSceneLoader::Cancel(const char* reason) {
    if (phase_ == Phase::Idle) return;
    LEMON_WARN("scene async: op %u（'%s'）被取消——%s（completed 不推）", opId_,
               req_.name.c_str(), reason);
    phase_ = Phase::Idle;
    opId_ = 0;
    staged_.reset();
    staging_.reset();
    req_ = SceneSwitchRequest{};
}

bool AsyncSceneLoader::SetActivation(uint32_t opId, bool allow) {
    if (phase_ == Phase::Idle || opId != opId_) return false; // 终态不可改门/未知 op
    allowActivation_ = allow;
    return true;
}

bool AsyncSceneLoader::Query(uint32_t opId, AsyncSceneQuery& out) const {
    if (phase_ != Phase::Idle && opId == opId_) {
        out.progress = progress_;
        out.isDone = 0;
        return true;
    }
    // 终态缓存（Unity op 完成后仍可查）——不要求 Idle：新 op 在途时旧完成 op
    // 仍可查（review F4：原 phase_==Idle 条件让单槽缓存被在途 op 遮蔽）
    if (opId != 0 && opId == doneOpId_) {
        out = done_;
        return true;
    }
    return false;
}

/// 预备段推进（预算内多段连走——小档一帧直抵 Gate = 与同步路径同帧效的机器面）。
/// 返回 true = 激活就绪（Gate 开；激活本体归 SceneSwitcher::TickAsync——共核
/// RunSwitch + 终态先于 completed 推送〔isDone 可查契约〕）。
bool AsyncSceneLoader::Tick(World& world, Scene& scene) {
    (void)scene; // 预备段零主世界接触（staging 独立 registry）
    if (phase_ == Phase::Idle) return false;
    const auto t0 = std::chrono::steady_clock::now();
    const auto spentMs = [&] {
        return std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() -
                                                        t0)
            .count();
    };
    const auto overBudget = [&] { return budgetMs_ > 0.0f && spentMs() >= budgetMs_; };

    while (phase_ != Phase::Idle) {
        switch (phase_) {
        case Phase::Parse: {
            staged_ = StagedSceneBuild::Parse(req_.jsonText); // 原子段（头注口径）
            if (!staged_) {
                // 失败契约（批文件 §5）：世界逐位不动（预检在暂存）+ 红字（Parse
                // 内已响）+ 终态 + kind3 失败推送（newHandle=0）。opId/mode 先捕——
                // FinishActivation 会复位 opId_/清 req_
                const uint32_t failedOp = opId_;
                const uint8_t mode = req_.mode;
                LEMON_ERROR("scene async: op %u（'%s'）Parse 段失败——世界保持原状",
                            failedOp, req_.path.c_str());
                FinishActivation(false, failedOp);
                if (auto* backend = world.ScriptBackend())
                    backend->SceneEventNotify(world, scene, SceneEventKind::AsyncCompleted,
                                              failedOp, 0, mode);
                return false;
            }
            progress_ = 0.09f;
            phase_ = Phase::BuildCreate;
            break;
        }
        case Phase::BuildCreate: {
            if (!staging_) staging_ = std::make_unique<Scene>("__async_staging__");
            const uint32_t total = staged_->EntityCount();
            // 预算粒度：每 64 槽查一次表（create 便宜，时钟读反而是开销大头）
            bool done = false;
            do {
                done = staged_->CreateSlots(*staging_, 64);
            } while (!done && !overBudget());
            progress_ =
                0.09f + 0.045f * (total ? (float)staged_->CreatedCount() / (float)total
                                         : 1.0f);
            if (done) phase_ = Phase::BuildDecode;
            break;
        }
        case Phase::BuildDecode: {
            const uint32_t total = staged_->EntityCount();
            bool done = false;
            do {
                done = staged_->DecodeEntities(*staging_, 16); // 解码重——粒度细
            } while (!done && !overBudget());
            progress_ =
                0.135f + 0.675f * (total ? (float)staged_->DecodedCount() / (float)total
                                          : 1.0f);
            if (done) phase_ = Phase::Assets;
            break;
        }
        case Phase::Assets: {
            // 占位段（ADR D3：贴图 eager 预载现状下 no-op；M9 tilemap/按需装载
            // 接入位）。v1 的真实工作 = **DOM 分帧回收**（Decode 毕 doc 已死——
            // 万实体档整体析构数十 ms 会破帧预算，按 512 元素/块摊入预算）
            bool released = false;
            do {
                released = staged_->ReleaseDocChunk(512);
            } while (!released && !overBudget());
            if (released) {
                phase_ = Phase::Gate;
                progress_ = 0.9f;
            }
            break;
        }
        case Phase::Gate:
            progress_ = 0.9f;
            if (!allowActivation_) return false; // 门关：停 0.9，每帧重查
            return true;                         // 激活就绪（TickAsync 执行）
        default:
            return false;
        }
        if (overBudget()) return false; // 本帧预算尽——下帧续
    }
    return false;
}

void AsyncSceneLoader::FinishActivation(bool success, uint32_t finishedOp) {
    // 重入护栏：RunSwitch 的 afterBuild（Awake）内新请求会取代本 op——被取代即
    // 不再触碰（新请求已自带全套状态；本 op 的终态缓存随取代放弃）
    if (phase_ == Phase::Idle || opId_ != finishedOp) return;
    doneOpId_ = opId_;
    done_.isDone = 1;
    done_.progress = success ? 1.0f : progress_;
    phase_ = Phase::Idle;
    opId_ = 0;
    staged_.reset();
    staging_.reset();
    req_ = SceneSwitchRequest{};
}

// -------------------------------------------------------------- 集成段 ----
// 批⑧ D1=A 核心：按台账在主 registry 复刻 BuildInto 槽位分配序列——doc 序建槽
// 全量（含坏条目槽）→ 好条目组件 memcpy → EntityRef 全槽重映射（坏槽引用与同步
// 路径同样解析到"已建后毁"句柄）→ 坏槽销毁提交。同步/异步激活句柄逐位一致的
// 结构保证：同清场 + 同建槽序 + 同回收集合。

namespace {

/// 单组件 EntityRef 字段重映射（顶层字段 + 数组段元素字段——Hierarchy 链在
/// 注册表内被泛型覆盖，零逐组件手写）
void RemapEntityRefs(const ComponentMeta& m, char* comp,
                     const std::unordered_map<Entity, Entity>& remap) {
    auto remapOne = [&](Entity& e) {
        const auto it = remap.find(e);
        if (it != remap.end()) e = it->second; // 未命中（null 引用）保持
    };
    for (uint16_t f = 0; f < m.fieldCount; ++f) {
        const FieldMeta& fm = m.fields[f];
        if (fm.type == FieldType::EntityRef)
            remapOne(*(Entity*)(comp + fm.offset));
    }
    if (m.arraySeg && m.arraySeg->elemFields) {
        const uint32_t n = m.arraySeg->countOffset != 0xFFFF
                               ? *(const uint8_t*)(comp + m.arraySeg->countOffset)
                               : m.arraySeg->maxCount;
        char* base = comp + m.arraySeg->offset;
        for (uint32_t i = 0; i < n && i < m.arraySeg->maxCount; ++i) {
            char* elem = base + i * m.arraySeg->elemSize;
            for (uint16_t f = 0; f < m.arraySeg->elemFieldCount; ++f) {
                const FieldMeta& fm = m.arraySeg->elemFields[f];
                if (fm.type == FieldType::EntityRef) remapOne(*(Entity*)(elem + fm.offset));
            }
        }
    }
}

} // namespace

std::vector<Entity> IntegrateStaged(Scene& live, Scene& staging,
                                    const std::vector<Entity>& ledger) {
    auto& reg = ComponentRegistry::Instance();
    // 池预留（激活尖峰压低：2 万级实体逐 emplace 的池倍增重分配是大头；
    // reserveFn/countFn = 既有元数据钩子，缺省槽跳过——entt reserve 恒不缩容）
    for (uint16_t id = 0; id < reg.Count(); ++id) {
        const ComponentMeta& m = reg.At(id);
        if (!m.reserveFn) continue;
        const uint32_t base = m.countFn ? m.countFn(live) : 0;
        m.reserveFn(live, base + (uint32_t)ledger.size());
    }
    // 相一：doc 序建槽全量（复刻 BuildEntities 两遍结构的第一遍）
    std::vector<Entity> liveLedger;
    liveLedger.reserve(ledger.size());
    for (size_t i = 0; i < ledger.size(); ++i) liveLedger.push_back(live.Create());

    // 全槽重映射表（坏槽入表——档内引用它们的 EntityRef 与同步路径同值）
    std::unordered_map<Entity, Entity> remap;
    remap.reserve(ledger.size() * 2);
    for (size_t i = 0; i < ledger.size(); ++i) remap.emplace(ledger[i], liveLedger[i]);

    // 相二：好条目组件拷贝（注册表元数据驱动 memcpy；ScriptBox 不入注册表特例）
    bool dropped = false;
    for (size_t i = 0; i < ledger.size(); ++i) {
        if (!staging.Alive(ledger[i])) { // 坏条目（DecodeEntities 已回收暂存槽）
            live.Destroy(liveLedger[i]);
            dropped = true;
            continue;
        }
        for (uint16_t id = 0; id < reg.Count(); ++id) {
            const ComponentMeta& m = reg.At(id);
            // 运行时销毁队列标记不入拷贝（读档拒入同口径；staging 侧本就不会有）
            if (m.fieldCount == 0 && std::strcmp(m.name, "DestroyQueueTag") == 0) continue;
            const void* src = m.readFn(staging, ledger[i]);
            if (!src) continue;
            void* dst = m.emplaceFn(live, liveLedger[i]);
            std::memcpy(dst, src, m.sizeOf);
        }
        if (const scripting::ScriptBox* sb = staging.TryGet<scripting::ScriptBox>(ledger[i]))
            live.Emplace<scripting::ScriptBox>(liveLedger[i]) = *sb;
    }

    // EntityRef 重映射（拷贝后一遍——避免边拷边查的序依赖）
    for (size_t i = 0; i < ledger.size(); ++i) {
        if (!staging.Alive(ledger[i])) continue;
        for (uint16_t id = 0; id < reg.Count(); ++id) {
            const ComponentMeta& m = reg.At(id);
            if (m.fieldCount == 0 && std::strcmp(m.name, "DestroyQueueTag") == 0) continue;
            if (char* comp = (char*)m.readFn(live, liveLedger[i]))
                RemapEntityRefs(m, comp, remap);
        }
    }

    if (dropped) live.CommitDestroys(); // 镜像 BuildEntities 坏条目回收路径
    return liveLedger;
}

// ------------------------------------------------------------ 换场编排 ----

void SceneSwitcher::Request(SceneSwitchRequest&& r) {
    if (async_.HasInFlight()) async_.Cancel("同步 LoadScene 请求（单槽统一：显式同步赢）");
    if (hasPending_)
        LEMON_WARN("scene switch: pending '%s' overwritten by '%s'（单槽 last-wins）",
                   pending_.name.c_str(), r.name.c_str());
    pending_ = std::move(r);
    hasPending_ = true;
}

uint32_t SceneSwitcher::RequestAsync(SceneSwitchRequest&& r) {
    if (hasPending_) {
        LEMON_WARN("scene async: 未消费同步 pending '%s' 被取代（单槽 last-wins）",
                   pending_.name.c_str());
        hasPending_ = false;
        pending_ = SceneSwitchRequest{};
    }
    return async_.Request(std::move(r)); // 内含在途 async 取代 WARN
}

SceneSwitchReport SceneSwitcher::Execute(World& world, Scene& scene) {
    if (!hasPending_) {
        SceneSwitchReport rep;
        return rep;
    }
    hasPending_ = false; // 入口取走——OnDestroy/Awake 回调内再 Request 进下一帧
    return RunSwitch(world, scene, std::move(pending_), nullptr, nullptr, 0);
}

/// 换场协议共核（批⑧ 分节：同步 Execute 与异步激活段共用；staged 非空 = 异步
/// 激活——预检段跳过（Parse 段已检）、装载位换集成、尾推 AsyncCompleted 归
/// TickAsync〔终态先于推送的 isDone 契约〕）。同步路径行为逐字节不变——孪生
/// 测 + 金回放三抽验机械复核。
SceneSwitchReport SceneSwitcher::RunSwitch(World& world, Scene& scene,
                                           SceneSwitchRequest&& req,
                                           StagedSceneBuild* staged, Scene* staging,
                                           uint32_t asyncOpId) {
    SceneSwitchReport rep;

    // 0. 原子性预检：坏档不清场（世界逐位不动）。同步路径本体；async 在 Parse
    // 段已检（失败已于暂存态取消，不达此处）
    if (!staged && !SceneArchive::ValidateParse(req.jsonText)) {
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

    // 5-7. 装载（协议 ④）：建档 → BuildInto 追加（同步）| 集成（async——复刻
    // 槽位序列 + 档名恢复）→ 打标（仅收编未指派）→ 宿主后置 pass（F3：SpriteRef
    // 归一/脚本解析（Awake/OnEnable）/UI 声明装载）→ 档案态收口（协议 ⑤ 数据面）
    const uint32_t newHandle = world.CreateSceneRecord(req.name.c_str(), req.path.c_str());
    rep.newHandle = newHandle;
    bool built = false;
    if (staged) {
        IntegrateStaged(scene, *staging, staged->Ledger());
        // ApplySceneName 同构（review F2：档含 "name" 才覆写——无名档保持现名，
        // 与同步路径 BuildInto 逐字节同语义）
        if (staged->HasName()) scene.SetName(staged->Name().c_str());
        built = true; // 集成无失败路径：预备段已过全部 JSON 校验，拷贝不可失败
    } else {
        built = SceneArchive::BuildInto(scene, req.jsonText);
    }
    if (!built) {
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
    if (asyncOpId)
        LEMON_LOG("scene async switch: op %u '%s' → '%s'（清 %u / DDOL 幸存 %u / 新组 %u）",
                  asyncOpId, oldRec ? oldRec->name.c_str() : "?", req.name.c_str(),
                  rep.destroyedOldGroup, rep.ddolSurvivors, rep.stampedNew);
    else
        LEMON_LOG("scene switch: '%s' → '%s'（清 %u / DDOL 幸存 %u / 新组 %u）",
                  oldRec ? oldRec->name.c_str() : "?", req.name.c_str(),
                  rep.destroyedOldGroup, rep.ddolSurvivors, rep.stampedNew);
    return rep;
}

/// 异步机驱动壳（Execute 之后同 Essential 窗口）：预备段推进 → 激活（共核）
/// → 终态先于 kind3 推送（completed 订阅方 Query 即得 isDone/1.0）。
void SceneSwitcher::TickAsync(World& world, Scene& scene) {
    if (!async_.Tick(world, scene)) return;
    const uint32_t opId = async_.opId_;
    const uint8_t mode = async_.req_.mode;
    SceneSwitchRequest req = std::move(async_.req_);
    std::unique_ptr<StagedSceneBuild> staged = std::move(async_.staged_);
    std::unique_ptr<Scene> staging = std::move(async_.staging_);
    const SceneSwitchReport rep =
        RunSwitch(world, scene, std::move(req), staged.get(), staging.get(), opId);
    async_.FinishActivation(rep.status == SceneSwitchStatus::Success, opId);
    if (auto* backend = world.ScriptBackend()) {
        backend->SceneEventNotify(world, scene, SceneEventKind::AsyncCompleted, opId,
                                  rep.status == SceneSwitchStatus::Success ? rep.newHandle
                                                                          : 0,
                                  mode);
    }
}

void SceneSwitchSystem::Tick(World& world, Scene& scene, float dt) {
    (void)dt;
    SceneSwitcher& sw = world.Switcher();
    sw.Execute(world, scene);
    sw.TickAsync(world, scene); // 同步 pending 当帧已消费 = async 已被取消（no-op）
}

} // namespace lemon::ecs
