// Lemon 引擎 — 脚本宿主桥实现（批量帧缓冲构造在管线线程；执行在托管域线程）
#include "Scripting/ScriptHost.h"

#include <cstdio>
#include <cstring>

#include "Components/CoreComponents.h"
#include "Components/RenderComponents.h"
#include "ECS/ComponentRegistry.h"

namespace lemon::scripting {

namespace {

// native api 当前上下文（仅域线程 tick 期间有效；TickBatch 入口设置、出口清空）
ecs::World* g_world = nullptr;
ecs::Scene* g_scene = nullptr;

int NativeIsAlive(uint64_t e) { return g_scene && g_scene->Alive(ecs::Entity{e}) ? 1 : 0; }
int NativeHas(uint64_t e, uint8_t id) {
    if (!g_scene || id >= ecs::ComponentRegistry::Instance().Count()) return 0;
    return ecs::ComponentRegistry::Instance().At(id).hasFn(*g_scene, ecs::Entity{e}) ? 1 : 0;
}
int NativeRead(uint64_t e, uint8_t id, void* dst, uint32_t cap) {
    if (!g_scene || id >= ecs::ComponentRegistry::Instance().Count()) return 0;
    const auto& m = ecs::ComponentRegistry::Instance().At(id);
    const void* p = m.readFn(*g_scene, ecs::Entity{e});
    if (!p) return 0;
    uint32_t n = m.sizeOf < cap ? m.sizeOf : cap;
    std::memcpy(dst, p, n);
    return (int)m.sizeOf;
}
int NativeWrite(uint64_t e, uint8_t id, const void* src, uint32_t size) {
    if (!g_scene || id >= ecs::ComponentRegistry::Instance().Count()) return 0;
    const auto& m = ecs::ComponentRegistry::Instance().At(id);
    if (size < m.sizeOf) return 0;
    // get-or-create：先 read 取已有；emplace 只对缺失组件（对已有组件二次 emplace
    // 在 entt Release 下 = 池损坏 → AV。M3-7 实测 10 万弹崩、5k 侥幸的根因）
    ecs::Entity ent{e};
    void* p = (void*)m.readFn(*g_scene, ent);
    if (!p) p = m.emplaceFn(*g_scene, ent);
    std::memcpy(p, src, m.sizeOf);
    return (int)m.sizeOf;
}

// ---- M4.4 SDK 增量（M4-Editor-Plan §4-8）----
// 编辑器资产钩子（进程级；编辑器宿主装配期注入，纯运行时为空）
EditorAssetHooks g_editorAssets{nullptr, nullptr};
} // namespace

void SetEditorAssetHooks(const EditorAssetHooks& hooks) { g_editorAssets = hooks; }

namespace {

void NativeGetInput(uint64_t* buttons, float* ax, float* ay) {
    // 域线程 tick 期间 g_world 有效（与 isAlive 等同一窗口约定）
    if (g_world) {
        const ecs::InputState& in = g_world->Input();
        if (buttons) *buttons = in.buttons;
        if (ax) *ax = in.ax;
        if (ay) *ay = in.ay;
    } else {
        if (buttons) *buttons = 0;
        if (ax) *ax = 0;
        if (ay) *ay = 0;
    }
}

uint32_t NativeSpriteOfGuid(const char* guidHex) {
    return g_editorAssets.spriteOfGuid ? g_editorAssets.spriteOfGuid(guidHex) : 0;
}

uint64_t NativeSpawnSprite(uint32_t spriteId, float x, float y) {
    // 就地建实体（当帧 C# 批量块已构造完毕，新实体下帧可见——与 SceneOps 命令缓冲
    // 的跨帧生效语义一致；省去占位句柄两段式）。Meta.guid=0 = 运行时生成实体。
    if (!g_scene) return 0;
    ecs::Entity e = g_scene->Create();
    auto& tf = g_scene->Emplace<ecs::Transform2D>(e);
    tf.pos = {x, y};
    if (spriteId != 0) {
        auto& sr = g_scene->Emplace<ecs::SpriteRenderer>(e);
        sr.spriteId = spriteId;
        sr.flags = 0x4; // enabled
    }
    auto& m = g_scene->Emplace<ecs::Meta>(e);
    std::snprintf(m.tag, sizeof(m.tag), "spawned");
    return e.id;
}

uint64_t NativeInstantiatePrefab(const char* guidHex, float x, float y) {
    return g_editorAssets.instantiatePrefab ? g_editorAssets.instantiatePrefab(guidHex, x, y) : 0;
}

const NativeApiVtable kNativeApi{NativeIsAlive,
                                 NativeHas,
                                 NativeRead,
                                 NativeWrite,
                                 NativeGetInput,
                                 NativeSpriteOfGuid,
                                 NativeSpawnSprite,
                                 NativeInstantiatePrefab};
} // namespace

namespace {

constexpr uint32_t kBlockStride = 64; // 块步长恒 64（末块 Length<64；04 §2.2 块 ≥64 摊薄）

// forEach 收集上下文（C 回调无捕获 → 打包 ctx；驱动组件 = 查询 comps[0]）
struct BuildCtx {
    std::vector<ecs::Entity>* entBuf;
    std::vector<void*>* ptrBuf;
    std::vector<BatchBlock>* blockBuf;
    ecs::Scene* scene;
    const ecs::ComponentMeta* metas[8];
    uint32_t compCount;
    ecs::Entity pendingEnts[kBlockStride];
    void* pendingPtrs[8 * kBlockStride]; // [slot*64 + i] 满步长布局
    uint32_t pendingCount;
};

void FlushBlock(BuildCtx& c) {
    const uint32_t len = c.pendingCount;
    const uint32_t compCount = c.compCount;

    BatchBlock blk{};
    blk.length = (int32_t)len;
    blk.stride = (int32_t)kBlockStride;

    size_t eOff = c.entBuf->size();
    c.entBuf->insert(c.entBuf->end(), c.pendingEnts, c.pendingEnts + len);
    blk.entities = &(*c.entBuf)[eOff];

    size_t pOff = c.ptrBuf->size();
    c.ptrBuf->insert(c.ptrBuf->end(), c.pendingPtrs,
                     c.pendingPtrs + compCount * kBlockStride);
    blk.comps = &(*c.ptrBuf)[pOff];

    c.blockBuf->push_back(blk);
    c.pendingCount = 0;
}

void GatherEntity(ecs::Entity e, const void* data, void* userdata) {
    auto* c = (BuildCtx*)userdata;
    void* ptrs[8];
    ptrs[0] = const_cast<void*>(data);
    for (uint32_t s = 1; s < c->compCount; s++) {
        ptrs[s] = const_cast<void*>(c->metas[s]->readFn(*c->scene, e));
        if (!ptrs[s]) return; // 缺任一查询组件 → 不匹配（AND 语义）
    }
    const uint32_t i = c->pendingCount;
    c->pendingEnts[i] = e;
    for (uint32_t s = 0; s < c->compCount; s++) c->pendingPtrs[s * kBlockStride + i] = ptrs[s];
    if (++c->pendingCount == kBlockStride) FlushBlock(*c);
}

} // namespace

bool ScriptHost::Initialize(const char* dotnetRoot, const char* runtimeConfigPath,
                            const char* entryAssemblyPath) {
    if (!host_.Load(dotnetRoot, runtimeConfigPath, entryAssemblyPath)) return false;

    const char* kType = "Lemon.Entry.Exports, Lemon.Entry";
    dmLoad_ = (int (*)(const char*))host_.GetExport(kType, "lemon_dm_load");
    dmUnload_ = (int (*)())host_.GetExport(kType, "lemon_dm_unload");
    // M4.5 换装族导出（旧宿主程序集无这些导出 = 空指针，热重载 API 返回失败态）
    dmReload_ = (int (*)(const char*, int*, int*))host_.GetExport(kType, "lemon_dm_reload");
    hrReloadsFn_ = (int (*)())host_.GetExport(kType, "lemon_hr_reloads");
    hrLeaksFn_ = (int (*)())host_.GetExport(kType, "lemon_hr_leaks");
    batchCountFn_ = (int (*)())host_.GetExport(kType, "lemon_batch_count");
    batchQueryFn_ = (int (*)(int, uint8_t*, int))host_.GetExport(kType, "lemon_batch_query");
    batchTickFn_ = (void (*)(BatchSystemFrame*, int))host_.GetExport(kType, "lemon_batch_tick");
    eventsDispatchFn_ =
        (void (*)(const ecs::EventPacket*, int))host_.GetExport(kType, "lemon_events_dispatch");
    eventsPullFn_ = (int (*)(ecs::EventPacket*, int))host_.GetExport(kType, "lemon_events_pull");
    scriptsTickFn_ =
        (void (*)(BatchSystemFrame*, int, float))host_.GetExport(kType, "lemon_scripts_tick");
    scriptsAttachFn_ = (void (*)(int, uint64_t))host_.GetExport(kType, "lemon_scripts_attach");
    scriptsDestroyFn_ = (void (*)(uint64_t))host_.GetExport(kType, "lemon_scripts_destroy");
    opsPullFn_ = (int (*)(SceneOpC*, int))host_.GetExport(kType, "lemon_ops_pull");
    if (auto reg = (void (*)(const NativeApiVtable*))host_.GetExport(kType, "lemon_api_register"))
        reg(&kNativeApi);
    return dmLoad_ && dmUnload_ && batchCountFn_ && batchQueryFn_ && batchTickFn_ &&
           eventsDispatchFn_ && eventsPullFn_ && scriptsTickFn_ && scriptsAttachFn_ &&
           scriptsDestroyFn_ && opsPullFn_;
}

bool ScriptHost::LoadUserAssembly(const char* path) {
    if (!dmLoad_ || dmLoad_(path) != 1) return false;
    userLoaded_ = true;
    batchPulled_ = false;     // 惰性：注册表此时可能尚未登记（World 未构造），首帧再拉
    behaviourNames_.clear();  // 换装程序集 → 类型名表重拉（M4.5 热重载同路径）
    return true;
}

ScriptHost::HotReloadInfo ScriptHost::HotReloadAssembly(const char* path) {
    HotReloadInfo info;
    if (!dmReload_) return info; // 旧 Entry 程序集（无 M4.5 导出）
    int leaks = 0, collected = 0;
    info.ok = dmReload_(path, &leaks, &collected) == 1;
    info.leakCount = leaks;
    info.lastCollected = collected != 0;
    if (hrReloadsFn_) hrCount_ = hrReloadsFn_();
    if (hrLeaksFn_) hrLeaks_ = hrLeaksFn_();
    info.reloadCount = hrCount_;
    if (info.ok) {
        userLoaded_ = true;
        batchPulled_ = false;     // 新域注册表（GameMain.Configure 已跑）
        behaviourNames_.clear();  // 类型名表重拉
    }
    return info;
}

void ScriptHost::PullBatchRegistry() {
    batchPulled_ = true;
    batch_.clear();
    int count = batchCountFn_();
    const uint16_t regCount = ecs::ComponentRegistry::Instance().Count();
    uint8_t ids[8]; // C# 侧按字节写（compCount ≤ 8），先收字节再展宽到 uint16
    for (int i = 0; i < count; i++) {
        BatchSys bs{};
        bs.compCount = (uint8_t)batchQueryFn_(i, ids, 8);
        bool valid = bs.compCount > 0 && bs.compCount <= 8;
        for (uint8_t c = 0; valid && c < bs.compCount; c++)
            valid = ids[c] < regCount; // 越界 id 防线
        if (valid) {
            for (uint8_t c = 0; c < bs.compCount; c++) bs.comps[c] = ids[c];
            batch_.push_back(bs);
        }
    }
}

void ScriptHost::TickBatch(ecs::World& world, ecs::Scene& scene, float dt) {
    if (!userLoaded_ || !scriptsTickFn_) return;
    if (!batchPulled_) PullBatchRegistry(); // 惰性拉取（此时 World 已构造 = 注册表就绪）
    if (batch_.empty() && !scriptsNeedTick_) return;

    auto& reg = ecs::ComponentRegistry::Instance();
    entBuf_.clear();
    ptrBuf_.clear();
    blockBuf_.clear();
    frameBuf_.clear();

    // 预留总量（countFn = 驱动池大小，AND 匹配数 ≤ 池大小）：vector 容量一次到位
    // ⇒ 本帧构造期零扩容——连续性与指针稳定性同时成立（C# 侧线性步进的前提）。
    // 指针数按"块数 × 每块 compCount×64 满步长"计（末块不足 64 也整块插入——按
    // n×comp 预留会短 comp×64，构造末尾 realloc = 全帧悬垂；小规模无 malloc 取整
    // 余量兜底，script-tests 实测 AV 的根因）。
    uint32_t capEnts = 0, capPtrs = 0, capBlocks = 0;
    for (const BatchSys& bs : batch_) {
        if (bs.disabled) continue;
        const auto& m = reg.At(bs.comps[0]);
        const uint32_t n = m.countFn ? m.countFn(scene) : 0;
        const uint32_t blocks = n / kBlockStride + 1; // ≥ ceil(n/64)
        capEnts += n;
        capPtrs += blocks * kBlockStride * bs.compCount;
        capBlocks += blocks;
    }
    entBuf_.reserve(entBuf_.size() + capEnts);
    ptrBuf_.reserve(ptrBuf_.size() + capPtrs);
    blockBuf_.reserve(blockBuf_.size() + capBlocks);

    for (uint32_t s = 0; s < batch_.size(); s++) {
        const BatchSys& bs = batch_[s];
        if (bs.disabled) continue; // C# 侧异常禁用：不再构造死块
        const auto& driver = reg.At(bs.comps[0]);
        const uint32_t blockBase = (uint32_t)blockBuf_.size();

        BuildCtx ctx{};
        ctx.entBuf = &entBuf_;
        ctx.ptrBuf = &ptrBuf_;
        ctx.blockBuf = &blockBuf_;
        ctx.scene = &scene;
        ctx.compCount = bs.compCount;
        for (uint32_t k = 0; k < bs.compCount; k++) ctx.metas[k] = &reg.At(bs.comps[k]);

        // 串行全池遍历（packed 序，确定性；结构变更当帧冻结——命令缓冲帧首应用）。
        // 并行切段（countFn/forEachRangeFn 钩子已备）5k 规模实测反降速（调度开销），
        // 10 万级再启用。
        driver.forEachFn(scene, GatherEntity, &ctx);
        if (ctx.pendingCount > 0) FlushBlock(ctx);

        if (blockBuf_.size() > blockBase) {
            BatchSystemFrame fr{};
            fr.systemIndex = (int32_t)s;
            fr.blocks = &blockBuf_[blockBase];
            fr.blockCount = (int32_t)(blockBuf_.size() - blockBase);
            fr.rngSeed = world.Desc().seed;
            fr.dt = dt;
            frameBuf_.push_back(fr);
        }
    }

    // 档①+档② 一帧固定序：Start/Update → 批量 → LateUpdate（域线程；ADR-010 D1）
    if (!frameBuf_.empty() || scriptsNeedTick_) {
        g_world = &world;
        g_scene = &scene;
        scriptsTickFn_(frameBuf_.data(), (int)frameBuf_.size(), dt);
        g_world = nullptr;
        g_scene = nullptr;
        // 回读禁用位（域线程已同步返回，栅栏保证可见）
        for (auto& fr : frameBuf_)
            if (fr.disabled) batch_[fr.systemIndex].disabled = true;
    }
}

void ScriptHost::AttachBehaviour(ecs::Scene& scene, ecs::Entity e, int typeId) {
    // get-or-create：场景档（.scene script 段）已带 ScriptBox（typeId=-1 待解析）时
    // 原位覆写，不二次 Emplace（entt 对已有组件再 emplace = 池损坏）
    if (ScriptBox* sb = scene.TryGet<ScriptBox>(e)) {
        sb->typeId = typeId;
        sb->flags &= ~1u;
    } else {
        scene.Emplace<ScriptBox>(e, ScriptBox{(int32_t)typeId, 0, 0, {}});
    }
    if (scriptsAttachFn_) scriptsAttachFn_(typeId, e.id);
}

const std::vector<std::string>& ScriptHost::BehaviourTypeNames() {
    if (!behaviourNames_.empty() || !userLoaded_) return behaviourNames_;
    if (!behavioursListFn_) {
        behavioursListFn_ =
            (int (*)(char*, int))host_.GetExport("Lemon.Entry.Exports, Lemon.Entry",
                                                 "lemon_behaviours_list");
        if (!behavioursListFn_) return behaviourNames_;
    }
    char buf[4096];
    int n = behavioursListFn_(buf, (int)sizeof(buf));
    (void)n;
    for (const char* p = buf; *p;) { // '\n' 分隔、'\0' 结尾
        const char* nl = std::strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : std::strlen(p);
        behaviourNames_.emplace_back(p, len);
        if (!nl) break;
        p = nl + 1;
    }
    return behaviourNames_;
}

uint64_t ScriptHost::GcAllocated() const {
    // 指针缓存：每次 GetExport 都经 load_assembly_and_get_function_pointer 在托管侧
    // 分配（~8.2KB/次，不定时）——GC 验收的采样调用必须零成本（M3-7 实测 2×8200B）
    if (!gcAllocFn_) {
        gcAllocFn_ = (unsigned long long (*)())host_.GetExport(
            "Lemon.Entry.Exports, Lemon.Entry", "lemon_gc_allocated");
        if (!gcAllocFn_) return 0;
    }
    return (uint64_t)gcAllocFn_();
}

void ScriptHost::ApplyStructural(ecs::World& world, ecs::Scene& scene) {
    (void)world; // 预留（场景级操作当前仅涉 active scene）
    if (!opsPullFn_) return;
    auto& reg = ecs::ComponentRegistry::Instance();
    constexpr uint64_t kPlaceholderBit = 0x8000'0000'0000'0000ull;

    std::vector<std::pair<uint64_t, ecs::Entity>> resolved; // 本批占位 → 真实实体
    auto Resolve = [&](uint64_t e) -> ecs::Entity {
        if (e & kPlaceholderBit) {
            for (auto& [ph, real] : resolved)
                if (ph == e) return real;
            return ecs::Entity{}; // 未定义引用（跨帧占位）：丢弃
        }
        return ecs::Entity{e};
    };

    while (true) {
        if (opBuf_.empty()) opBuf_.resize(256);
        int n = opsPullFn_(opBuf_.data(), (int)opBuf_.size());
        for (int i = 0; i < n; i++) {
            const SceneOpC& op = opBuf_[i];
            switch (op.type) {
            case 0: { // Create
                ecs::Entity e = scene.Create();
                resolved.emplace_back(op.entity, e);
                break;
            }
            case 1: { // Destroy（先 OnDestroy 通知，再入两阶段队列——本批随后 CommitDestroys 生效）
                ecs::Entity e = Resolve(op.entity);
                if (!e.IsNull()) {
                    if (scriptsDestroyFn_) scriptsDestroyFn_(e.id);
                    scene.Destroy(e);
                }
                break;
            }
            case 2: { // AddComponent（注册表驱动）
                ecs::Entity e = Resolve(op.entity);
                if (!e.IsNull() && op.compId < reg.Count()) reg.At(op.compId).emplaceFn(scene, e);
                break;
            }
            case 3: { // RemoveComponent
                ecs::Entity e = Resolve(op.entity);
                if (!e.IsNull() && op.compId < reg.Count() && reg.At(op.compId).removeFn)
                    reg.At(op.compId).removeFn(scene, e);
                break;
            }
            case 4: { // AttachScript（挂 ScriptBox + 托管实例/Awake/OnEnable）
                ecs::Entity e = Resolve(op.entity);
                if (!e.IsNull()) {
                    scene.Emplace<ScriptBox>(e, ScriptBox{(int32_t)op.compId, 0});
                    if (scriptsAttachFn_) scriptsAttachFn_((int)op.compId, e.id);
                }
                break;
            }
            default: break;
            }
        }
        if (n < (int)opBuf_.size()) break; // 拉空
    }
}

void ScriptHost::DispatchEvents(ecs::World& world, ecs::Scene&) {
    // M3-4：#15 头部先拉脚本 pending 入队（当帧派发），再两段零拷贝转发 C#
    if (eventsPullFn_ && pullBuf_.empty()) pullBuf_.resize(256);
    while (eventsPullFn_) {
        int n = eventsPullFn_(pullBuf_.data(), (int)pullBuf_.size());
        for (int i = 0; i < n; i++)
            if (!world.Events().Push(pullBuf_[i])) break; // 满上限丢弃计数由队列管
        if (n < (int)pullBuf_.size()) break; // 拉空
    }

    auto& q = world.Events();
    const ecs::EventPacket* seg = nullptr;
    uint32_t n = 0;
    q.HeadSpan(seg, n);
    if (n) eventsDispatchFn_(seg, (int)n);
    q.TailSpan(seg, n);
    if (n) eventsDispatchFn_(seg, (int)n);
}

} // namespace lemon::scripting
