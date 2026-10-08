// Lemon 引擎 — 脚本宿主桥实现（批量帧缓冲构造在管线线程；执行在托管域线程）
#include "Scripting/ScriptHost.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

#include "Components/CoreComponents.h"
#include "Components/RenderComponents.h"
#include "Audio/AudioEngine.h" // NativeAudioMasterVolGet 直读引擎态（M6c 批②）
#include "Core/Log.h"
#include "Core/FileOps.h" // AcpToUtf8：win 侧 ACP 路径过 C++/C# UTF-8 边界前归一
#include "ECS/ComponentRegistry.h"
#include "ECS/SceneMembership.h" // 运行时建实体打标（批⑥b"零未打标"不变量）
#include "Systems/Systems.h" // SeparationSystem 完整定义（调参下放通道）

namespace lemon::scripting {

namespace {

// native api 当前上下文（仅域线程 tick 期间有效；TickBatch 入口设置、出口清空）
ecs::World* g_world = nullptr;
ecs::Scene* g_scene = nullptr;

/// g_world/g_scene 的 RAII 窗口：托管回调（Update/Awake/OnDestroy/事件订阅方）内
/// native 调用（Ui.Set/Time.Scale/Spawn/Save）依赖此窗口，漏设 = 静默空转。
/// 五处统一走本守卫：TickBatch、DispatchEvents、AttachBehaviour、结构命令
/// Destroy/AttachScript（M11：原仅前两处设窗口，Awake/OnDestroy 内 native 调用空转）
struct NativeApiWindow {
    NativeApiWindow(ecs::World* w, ecs::Scene* s) : prevW(g_world), prevS(g_scene) {
        g_world = w;
        g_scene = s;
    }
    ~NativeApiWindow() {
        g_world = prevW;
        g_scene = prevS;
    }
    ecs::World* prevW;
    ecs::Scene* prevS;
};

// ---- D5 批量帧护栏（M7a 批③；评审 §D5）——两层设计 ----
// 全部块组件指针在 lemon_scripts_tick 之前收集（GatherEntity → ptrBuf_），生命周期
// 跨整个托管 tick（Start/Update → Batch → LateUpdate）。窗口内就地建实体 /
// get-or-create 的调用可能让 EnTT packed 池越容量重分配 → 已收集指针悬垂（潜伏
// AV/静默损坏）。Update 内逐帧 Spawn 是既有正当用法（模板/svr-test 大量依赖），
// 故第一层 = **gather 前对被查询池 reserve(count+1024 结构余量)**：合法负载零
// 重分配零触发（零行为漂移；script-chain 冒烟实证——无预留版曾三触发）。第二层
// = 病态兜底：基址比对真实搬移才置位全部 BatchSystemFrame.stale，C# 块循环头查
// stale → 本帧剩余块跳过（fail-stop：超余量爆量 = 响亮截断而非悬垂读）。基址仅
// 比对不 deref；SceneOps/ApplyStructural 在 Essential 帧首应用 = 窗口外，不经此护栏。
constexpr uint32_t kMaxPoolWatch = 64; // 去重后被查询组件数上限（现 31 组件，余量足）
struct PoolWatch {
    uint16_t compId = 0;
    const void* base = nullptr;
};
PoolWatch g_poolWatch[kMaxPoolWatch];
uint32_t g_poolWatchCount = 0;
std::vector<BatchSystemFrame>* g_batchFrames = nullptr; // 消费中的帧数组（置位目标）
bool g_batchStaleWarned = false;                        // 红字去重（每 tick 一次）
uint64_t g_batchStaleMarks = 0;                         // 累计置位次数（测试探针）

void MarkBatchStaleIfPoolsMoved() {
    if (!g_batchFrames || g_poolWatchCount == 0) return;
    auto& reg = ecs::ComponentRegistry::Instance();
    for (uint32_t i = 0; i < g_poolWatchCount; i++) {
        const auto fn = reg.At(g_poolWatch[i].compId).poolDataFn;
        if (fn && fn(*g_scene) != g_poolWatch[i].base) {
            for (BatchSystemFrame& fr : *g_batchFrames) fr.stale = 1;
            if (!g_batchStaleWarned) {
                g_batchStaleWarned = true;
                ++g_batchStaleMarks;
                LEMON_ERROR("批量帧指针失效防护（D5）：单 tick 窗口内结构操作超出"
                            "结构余量（1024）触发池重分配，已收集组件指针悬垂——本帧"
                            "剩余批量块已跳过。请分帧生成或挪出批量遍历（SceneOps）");
            }
            return;
        }
    }
}

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
    const bool emplaced = p == nullptr;
    if (!p) p = m.emplaceFn(*g_scene, ent);
    std::memcpy(p, src, m.sizeOf);
    if (emplaced) MarkBatchStaleIfPoolsMoved(); // D5：emplace 可能触池增长（比对定真伪）
    return (int)m.sizeOf;
}

// ---- M4.4 SDK 增量（M4.md §4-8）----
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
    // 就地建实体：当帧 C# 批量块已构造完毕 → 新实体对后续系统下帧可见，比
    // SceneOps 命令缓冲（帧首生效）晚一拍的可见性是有意为之（省占位句柄两段式），
    // 是 04 §「结构变更全走命令缓冲」成文语义的登记偏差（#56—— Instantiate.Spawn
    // 同径）。Meta.guid=0 = 运行时生成实体。
    if (!g_scene) return 0;
    ecs::Entity e = g_scene->Create();
    auto& tf = g_scene->Emplace<ecs::Transform2D>(e);
    tf.pos = {x, y};
    if (spriteId != 0) {
        auto& sr = g_scene->Emplace<ecs::SpriteRenderer>(e); // 默认启用
        sr.spriteId = spriteId;
    }
    auto& m = g_scene->Emplace<ecs::Meta>(e);
    std::snprintf(m.tag, sizeof(m.tag), "spawned");
    // 批⑥b：运行时建实体打标 active（Instantiate 落点，ADR-017 D1）——漏标 =
    // 换场组清场收不走（旧场实体泄漏进新场，档1 RunSweeper 手工清的同类病）
    if (g_world)
        g_scene->Emplace<ecs::SceneMembership>(e).scene = g_world->ActiveSceneHandle();
    MarkBatchStaleIfPoolsMoved(); // D5：四 emplace 可能触池增长（比对定真伪）
    return e.id;
}

uint64_t NativeInstantiatePrefab(const char* guidHex, float x, float y) {
    const uint64_t e = g_editorAssets.instantiatePrefab
                           ? g_editorAssets.instantiatePrefab(guidHex, x, y)
                           : 0;
    // D5：钩子内 LoadEntityTree 任意 emplace（树深度/组件面不可静态知）——返回后
    // 统一比对（比对定真伪，无需枚举树内组件）
    if (e != 0) MarkBatchStaleIfPoolsMoved();
    // 批⑥b 登记项：钩子注册者（当前无宿主注册，恒 0 失败路径）负责子树打标
    // active——World::SpawnPrefab 的 StampTreeMembership 同款责任边界（注册时接）
    return e;
}

// ---- M5 批①（timeScale：Time.Scale ↔ World；域线程 tick 窗口约定同上）----
float NativeGetTimeScale() { return g_world ? g_world->TimeScale() : 1.0f; }
void NativeSetTimeScale(float s) {
    if (g_world) g_world->SetTimeScale(s);
}

// M5 批①（RT UI：Lemon.Ui.Set → World.RtUi；呈现层专用不入 StateHash）
void NativeRtUiSet(const char* key, const char* text, float frac) {
    if (g_world) g_world->RtUi().Set(key, text, frac);
}

// ---- M5 批④（存档 + HUD 完整版；g_world 窗口约定同上）----
ScriptIoHooks g_scriptIo{nullptr};
bool g_saveIoWarned = false; // 未注入钩子的 Flush 红字去重

int32_t NativeSaveSet(const char* key, const void* bytes, uint32_t len) {
    return g_world && g_world->Saves().Set(key, bytes, len) ? (int32_t)len : -1;
}
int32_t NativeSaveGetLen(const char* key) {
    return g_world ? g_world->Saves().GetLen(key) : -1;
}
int32_t NativeSaveGet(const char* key, void* out, uint32_t cap) {
    return g_world ? g_world->Saves().Get(key, out, cap) : -1;
}
// M6a 批② T5：分档三指针（ch 越界红字 + 落 slot——ClampSaveChannel 钳位）
static uint8_t ClampSaveCh(const char* who, uint8_t ch) {
    if (ch >= ecs::kSaveChannelCount) {
        LEMON_WARN("%s：存档通道号 %u 越界（合法 0..%u），已回落 slot_0", who, ch,
                   (unsigned)ecs::kSaveChannelCount - 1);
        return ecs::kSaveSlot;
    }
    return ch;
}
int32_t NativeSaveSetEx(const char* key, const void* bytes, uint32_t len, uint8_t ch) {
    if (!g_world) return -1;
    ch = ClampSaveCh("Save.Set", ch);
    return g_world->Saves(ch).Set(key, bytes, len) ? (int32_t)len : -1;
}
int32_t NativeSaveGetLenEx(const char* key, uint8_t ch) {
    if (!g_world) return -1;
    return g_world->Saves(ClampSaveCh("Save.Get", ch)).GetLen(key);
}
int32_t NativeSaveGetEx(const char* key, void* out, uint32_t cap, uint8_t ch) {
    if (!g_world) return -1;
    return g_world->Saves(ClampSaveCh("Save.Get", ch)).Get(key, out, cap);
}
void NativeSaveFlush() {
    if (!g_world) return;
    if (g_scriptIo.saveFlush) {
        g_scriptIo.saveFlush(*g_world);
    } else if (!g_saveIoWarned) {
        g_saveIoWarned = true;
        LEMON_WARN("Save.Flush：宿主未注入存档 IO 钩子（纯运行时 M8 前编辑器外为 no-op）");
    }
}

// ---- M6c 批②（音频命令通道：Lemon.Audio → g_world->Audio() 当帧 staging，
// Save 同款域 tick 窗口约定；guid→clipId 桥内经 World::ResolveAudioClip 解析，
// 0 = 未注册即失败。引擎提交归 AudioSystem #20；AudioHooks 退役）----
uint32_t NativeAudioPlay(uint64_t guid, int32_t group, float volume, float pan, int32_t loop) {
    if (!g_world) return 0;
    const uint32_t clip = g_world->ResolveAudioClip(guid);
    if (clip == 0) return 0;
    return g_world->Audio().StagePlay(clip, group, volume, pan, loop != 0);
}
uint32_t NativeAudioPlayAt(uint64_t guid, float x, float y, float volume, int32_t group,
                           float refDist, float maxDist, int32_t loop) {
    if (!g_world) return 0;
    const uint32_t clip = g_world->ResolveAudioClip(guid);
    if (clip == 0) return 0;
    return g_world->Audio().StagePlayAt(clip, x, y, volume, group, refDist, maxDist,
                                        loop != 0);
}
int32_t NativeAudioStop(uint32_t voiceId) {
    return g_world && voiceId != 0 && g_world->Audio().StageStop(voiceId) ? 1 : 0;
}
int32_t NativeAudioBgm(uint64_t guid, float volume, float fadeSec) {
    if (!g_world) return 0;
    const uint32_t clip = g_world->ResolveAudioClip(guid);
    if (clip == 0) return 0;
    return g_world->Audio().StageBgm(clip, volume, fadeSec);
}
void NativeAudioBgmStop(float fadeSec) {
    if (g_world) g_world->Audio().StageBgmStop(fadeSec);
}
void NativeAudioSetGroupVolume(int32_t group, float volume) {
    if (g_world) g_world->Audio().StageGroupVolume(group, volume);
}
void NativeAudioStopAll() {
    if (g_world) g_world->Audio().StageStopAll();
}
void NativeAudioMasterVol(float volume) {
    if (g_world) g_world->Audio().StageMasterVolume(volume);
}
float NativeAudioMasterVolGet() {
    // 引擎态直读（命令表不镜像音量——设置类低频，get 走源不走去重）
    return g_world && g_world->AudioSink() ? g_world->AudioSink()->MasterVolume() : 1.0f;
}
void NativeAudioSetPaused(int32_t on) {
    if (g_world) g_world->Audio().StageSetPaused(on != 0);
}
uint8_t NativeAudioPausedGet() {
    // 引擎态直读（review 2026-10-02 #71，MasterVolGet 同款：set 走 staging、
    // get 读引擎现值——同帧写读 = 旧值，跨帧往返为准）
    return g_world && g_world->AudioSink() && g_world->AudioSink()->IsPaused() ? 1 : 0;
}

void NativeRtUiClear(const char* key) {
    if (g_world) g_world->RtUi().Clear(key);
}
void NativeRtUiSetEx(const char* key, const char* text, float frac, uint32_t color) {
    if (g_world) g_world->RtUi().Set(key, text, frac, color);
}
void NativeUiCards(int32_t show, const char* title, const char* a, const char* b,
                   const char* c) {
    if (!g_world) return;
    if (show) g_world->Cards().Show(title, a, b, c);
    else g_world->Cards().Hide();
}
int32_t NativeUiCardPick() { return g_world ? g_world->Cards().ConsumePick() : -1; }

// M6a 批①（世界空间表现：Lemon.Fx → World.Fx；呈现层专用不入 StateHash）
void NativeFxPopup(const char* text, float x, float y, uint32_t color) {
    if (g_world) g_world->Fx().PopupText(text, x, y, color);
}
void NativeFxBar(uint64_t entity, float frac, uint32_t color, float width) {
    if (g_world) g_world->Fx().Bar(entity, frac, color, width);
}

// M7c 批①（Fx 表现升级）：全参数形态——飘字动效（scale/life/driftX/curve）与
// 贴图血条（bg/fg guid + 延迟条 + 高度）。guid→spriteId 复用 spriteOfGuid 钩子
// 通道（编辑器 DB / 运行时 AssetIndex 两态同一解析；未装/坏 guid = 0 → 白精灵
// 现状路径，游戏侧零防御）
void NativeFxPopupEx(const char* text, float x, float y, uint32_t color, float scale,
                     float life, float driftX, uint8_t curve) {
    if (g_world)
        g_world->Fx().PopupTextEx(text, x, y, color, scale, life, driftX,
                                  (ecs::FxCurve)curve);
}
void NativeFxBarEx(uint64_t entity, float frac, uint32_t color, float width,
                   const char* bgGuidHex, const char* fgGuidHex, uint32_t lagColor,
                   float height, float anchorDy) {
    if (!g_world || entity == 0) return;
    g_world->Fx().BarEx(entity, frac, color, width,
                        bgGuidHex && *bgGuidHex ? NativeSpriteOfGuid(bgGuidHex) : 0,
                        fgGuidHex && *fgGuidHex ? NativeSpriteOfGuid(fgGuidHex) : 0,
                        lagColor, height, anchorDy);
}

// 2026-09-26 调参下放批：分离力参数场景侧覆盖（引擎默认不动；呈现/调参通道
// 不入 StateHash，基准场脚本零调用 = 回放零漂移）
void NativeSetSeparation(float radius, float strength, int32_t maxNeighbors,
                        int32_t densityCap) {
    if (!g_world) return;
    if (auto* sep = g_world->Separation(); sep) {
        if (radius >= 0.0f) sep->radius = radius;
        if (strength >= 0.0f) sep->strength = strength;
        if (maxNeighbors >= 0) sep->maxNeighbors = (uint32_t)maxNeighbors;
        if (densityCap >= 0) sep->densityCap = (uint32_t)densityCap;
    }
}

// M6a 批② T2：配置表读取（Lemon.Table → World.Tables；ADR-012 D1）。guidHex
// 取低 32 位查表（clipId/prefabId 同款映射约定——AssetDatabase::HexToGuid 在
// 编辑器层，桥内自持 mini 解析）。空宿主/无表 = -1；负 row/col 经 uint32 化
// 落到越界分支（Cell 双保险）。
uint32_t TableIdOfHex(const char* guidHex) {
    uint64_t v = 0;
    if (!guidHex) return 0;
    for (const char* p = guidHex; *p && p - guidHex < 16; ++p) {
        v <<= 4;
        const char c = *p;
        if (c >= '0' && c <= '9') v |= (uint64_t)(c - '0');
        else if (c >= 'a' && c <= 'f') v |= (uint64_t)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= (uint64_t)(c - 'A' + 10);
        else return 0; // 非 hex = 无表
    }
    return (uint32_t)v; // 低 32 位
}
int32_t NativeTableRows(const char* guidHex) {
    if (!g_world) return -1;
    const auto* t = g_world->Tables().Find(TableIdOfHex(guidHex));
    return t ? (int32_t)t->size() : -1;
}
int32_t NativeTableCols(const char* guidHex) {
    if (!g_world) return -1;
    const auto* t = g_world->Tables().Find(TableIdOfHex(guidHex));
    return t && !t->empty() ? (int32_t)(*t)[0].size() : -1;
}
int32_t NativeTableCell(const char* guidHex, int32_t row, int32_t col, char* out,
                        uint32_t cap) {
    if (!g_world) return -1;
    const std::string* c =
        g_world->Tables().Cell(TableIdOfHex(guidHex), (uint32_t)row, (uint32_t)col);
    if (!c) return -1;
    if (c->size() + 1 > cap) return -2; // cap 需含 NUL（SaveGet 同款二段语义）
    std::memcpy(out, c->data(), c->size());
    out[c->size()] = '\0';
    return (int32_t)c->size();
}

// M6a 批② T3c：动画集按名解析（Lemon.Anim Play/Queue/CrossFade 字符串重载）。
// T3d 批①起作用域优先级：①实体 AnimGraph.setGuid 显式绑定集（绑定为作用域，
// 不依赖"当前段恰属该集"的隐式反推——首段无需 Inspector 手工指 clip 即可按名）；
// ②回退 T3c 口径 = 当前段（Animator2D.clipId）所属集（旧场景零改动兼容）。
// 都失败 / 集内无名 = -1（SDK 侧红字 + no-op）。
int64_t NativeClipByName(uint64_t entity, const char* name) {
    if (!g_world || !g_scene || !name || !*name) return -1;
    if (const auto* graph = g_scene->TryGet<ecs::AnimGraph>(ecs::Entity{entity})) {
        if (graph->setGuid != 0) {
            const uint32_t id =
                g_world->Clips().FindByName((uint32_t)graph->setGuid, name);
            return id != 0 ? (int64_t)id : -1;
        }
    }
    const auto* an = g_scene->TryGet<ecs::Animator2D>(ecs::Entity{entity});
    if (!an || an->clipId == 0) return -1;
    const uint32_t setId = g_world->Clips().SetOfClip(an->clipId);
    if (setId == 0) return -1;
    const uint32_t id = g_world->Clips().FindByName(setId, name);
    return id != 0 ? (int64_t)id : -1;
}

// T3d 批②：参数名 → 槽位（实体所绑 controller 参数表定序；0..7）。低频桥
// （Lemon.Anim SetParam/GetParam/Trigger 首次解析后脚本侧可自缓存）。实体无
// AnimGraph / 未绑 controller / controller 未登记 / 参数表无名 = -1。
int32_t NativeAnimParamSlot(uint64_t entity, const char* name) {
    if (!g_world || !g_scene || !name || !*name) return -1;
    const auto* graph = g_scene->TryGet<ecs::AnimGraph>(ecs::Entity{entity});
    if (!graph || graph->controllerGuid == 0) return -1;
    const auto* ctrl = g_world->Controllers().Find((uint32_t)graph->controllerGuid);
    if (!ctrl) return -1;
    const int32_t slot = ctrl->ParamIndex(name);
    return slot >= 0 && slot < 8 ? slot : -1;
}

// A 档补间（2026-09-28）：Lemon.Tween → World.Tweens。建链失败 = 作者错误
// （字段名拼错/类型不可插值），区别于实体亡的静默丢弃——warn-once 按首个
// 失败现场报（复刻换段队列 warn-once 口径）。
uint64_t NativeTweenTo(uint64_t entity, uint8_t compId, const char* field,
                       const float* to4, float duration, uint8_t ease, uint8_t mode) {
    if (!g_world || !g_scene || !to4) return 0;
    const uint64_t h = g_world->Tweens().Create(
        *g_scene, ecs::Entity{entity}, compId, field, to4, duration,
        (ecs::TweenEase)ease, (ecs::TweenMode)mode);
    if (h == 0) {
        static bool warned = false;
        if (!warned) {
            warned = true;
            LEMON_WARN("Tween.To：实体 %llu 组件 %u 字段 '%s' 不可建（未命中/类型不可"
                       "插值——白名单 Float/Vec2/UInt32 颜色）→ no-op",
                       (unsigned long long)entity, compId, field ? field : "(null)");
        }
    }
    return h;
}

int32_t NativeTweenKill(uint64_t entity, uint8_t compId, const char* field) {
    if (!g_world) return 0;
    return g_world->Tweens().KillField(ecs::Entity{entity}, compId, field);
}

int32_t NativeTweenKillEntity(uint64_t entity) {
    if (!g_world) return 0;
    return g_world->Tweens().KillEntity(ecs::Entity{entity});
}

int32_t NativeTweenAlive(uint64_t handle) {
    if (!g_world) return 0;
    return g_world->Tweens().Alive(handle) ? 1 : 0;
}

// M6a 批② T4：XP 曲线参数（Lemon.Balance.XpCurveK → World；ADR-012 D3）。
// 空宿主读默认 1.25（TimeScale 读 1 同款降级）；写不 clamp（消费侧 max 兜底）
float NativeGetXpCurveK() { return g_world ? g_world->XpCurveK() : 1.25f; }
void NativeSetXpCurveK(float k) {
    if (g_world) g_world->SetXpCurveK(k);
}

const NativeApiVtable kNativeApi{NativeIsAlive,
                                 NativeHas,
                                 NativeRead,
                                 NativeWrite,
                                 NativeGetInput,
                                 NativeSpriteOfGuid,
                                 NativeSpawnSprite,
                                 NativeInstantiatePrefab,
                                 NativeGetTimeScale,
                                 NativeSetTimeScale,
                                 NativeRtUiSet,
                                 NativeSaveSet,
                                 NativeSaveGetLen,
                                 NativeSaveGet,
                                 NativeSaveFlush,
                                 NativeRtUiClear,
                                 NativeRtUiSetEx,
                                 NativeUiCards,
                                 NativeUiCardPick,
                                 NativeFxPopup,
                                 NativeFxBar,
                                 NativeSetSeparation,
                                 NativeTableRows,
                                 NativeTableCols,
                                 NativeTableCell,
                                 NativeClipByName,
                                 NativeAnimParamSlot,
                                 NativeTweenTo,
                                 NativeTweenKill,
                                 NativeTweenKillEntity,
                                 NativeTweenAlive,
                                 NativeGetXpCurveK,
                                 NativeSetXpCurveK,
                                 NativeSaveSetEx,
                                 NativeSaveGetLenEx,
                                 NativeSaveGetEx,
                                 NativeAudioPlay,
                                 NativeAudioPlayAt,
                                 NativeAudioStop,
                                 NativeAudioBgm,
                                 NativeAudioBgmStop,
                                 NativeAudioSetGroupVolume,
                                 NativeAudioStopAll,
                                 NativeAudioMasterVol,
                                 NativeAudioMasterVolGet,
                                 NativeAudioSetPaused,
                                 NativeAudioPausedGet,
                                 NativeFxPopupEx,
                                 NativeFxBarEx};
} // namespace

void SetScriptIoHooks(const ScriptIoHooks& hooks) { g_scriptIo = hooks; }

uint64_t BatchStaleMarkCount() { return g_batchStaleMarks; }
void ResetBatchStaleMarkCount() { g_batchStaleMarks = 0; }

// 批③c：UI 桥钩子（进程级单份，装配期一次；同 g_editorAssets 形态）
static UiHooks g_uiHooks;
void SetUiHooks(const UiHooks& hooks) { g_uiHooks = hooks; }

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
    // M4.5 换装族导出（旧宿主程序集无这些导出 = 空指针，热重载 API 返回失败态）
    dmReload_ = (int (*)(const char*, int*, int*))host_.GetExport(kType, "lemon_dm_reload");
    hrReloadsFn_ = (int (*)())host_.GetExport(kType, "lemon_hr_reloads");
    hrLeaksFn_ = (int (*)())host_.GetExport(kType, "lemon_hr_leaks");
    batchCountFn_ = (int (*)())host_.GetExport(kType, "lemon_batch_count");
    batchQueryFn_ = (int (*)(int, uint8_t*, int))host_.GetExport(kType, "lemon_batch_query");
    eventsDispatchFn_ =
        (void (*)(const ecs::EventPacket*, int))host_.GetExport(kType, "lemon_events_dispatch");
    eventsPullFn_ = (int (*)(ecs::EventPacket*, int))host_.GetExport(kType, "lemon_events_pull");
    scriptsTickFn_ =
        (void (*)(BatchSystemFrame*, int, float))host_.GetExport(kType, "lemon_scripts_tick");
    timeResetFn_ = (void (*)())host_.GetExport(kType, "lemon_time_reset"); // M5 清障①（可缺席）
    playResetFn_ = (void (*)())host_.GetExport(kType, "lemon_play_reset"); // M5 批④后修（可缺席）
    scriptsAttachFn_ = (void (*)(int, uint64_t))host_.GetExport(kType, "lemon_scripts_attach");
    scriptsDestroyFn_ = (void (*)(uint64_t))host_.GetExport(kType, "lemon_scripts_destroy");
    scriptsDetachFn_ =
        (void (*)(int, uint64_t))host_.GetExport(kType, "lemon_scripts_detach"); // M6a 批⓪ T3（可缺席）
    opsPullFn_ = (int (*)(SceneOpC*, int))host_.GetExport(kType, "lemon_ops_pull");
    // 批③c（ADR-014 M2/M3）：UI ops 拉取 + UiEvent 派发（旧 Entry 缺 = null 挂空）
    uiOpsPullFn_ =
        (int (*)(ui::UiOpC*, int, char*, int, int*))host_.GetExport(kType, "lemon_ui_ops_pull");
    uiEventsDispatchFn_ = (void (*)(const ui::UiEventC*, int))host_.GetExport(
        kType, "lemon_ui_events_dispatch");
    // native 表注册（2026-09-29 复审 4b）：优先尺寸握手版——宿主表字节数随表传入，
    // SDK 侧 min 拷贝 + 尾零，"新 SDK 配旧宿主"不再越界读宿主 const 表尾部（原整拷
    // 下 !=null 守卫反会去调 .rodata 相邻字节拼出的垃圾指针）。旧 Entry 无
    // register2 = 回落单参版（SDK 侧已冻结为 36 槽表宽拷贝，对本仓同期旧宿主安全）。
    if (auto reg2 = (void (*)(const NativeApiVtable*, uint32_t))host_.GetExport(
            kType, "lemon_api_register2"))
        reg2(&kNativeApi, (uint32_t)sizeof(NativeApiVtable));
    else if (auto reg = (void (*)(const NativeApiVtable*))host_.GetExport(
            kType, "lemon_api_register"))
        reg(&kNativeApi);
    return dmLoad_ && batchCountFn_ && batchQueryFn_ &&
           eventsDispatchFn_ && eventsPullFn_ && scriptsTickFn_ && scriptsAttachFn_ &&
           scriptsDestroyFn_ && opsPullFn_;
}

bool ScriptHost::LoadUserAssembly(const char* path) {
    // C++/C# 边界契约 UTF-8（Exports.cs 按 UTF8.GetString 解码）；win 侧 argv/fs
    // 派生路径是 ACP——不归一即 GBK→'???' FileNotFound（W6 2026-10-06 真机实抓）
    if (!dmLoad_ || dmLoad_(AcpToUtf8(path).c_str()) != 1) return false;
    userLoaded_ = true;
    batchPulled_ = false;     // 惰性：注册表此时可能尚未登记（World 未构造），首帧再拉
    behaviourNames_.clear();  // 换装程序集 → 类型名表重拉（M4.5 热重载同路径）
    return true;
}

ScriptHost::HotReloadInfo ScriptHost::HotReloadAssembly(const char* path) {
    HotReloadInfo info;
    if (!dmReload_) return info; // 旧 Entry 程序集（无 M4.5 导出）
    // 绝对化兜底（LoadFromAssemblyPath 只收绝对路径；相对路径 = ArgumentException
    // 未捕获 → coreclr abort。InitScriptHostFrom 侧早有同款，M4.6 实测闪退后补齐此口）
    std::error_code eca;
    const std::string abs = std::filesystem::absolute(path, eca).generic_string();
    int leaks = 0, collected = 0;
    info.ok = dmReload_(AcpToUtf8(abs).c_str(), &leaks, &collected) == 1; // 同 LoadUserAssembly：边界前归一 UTF-8
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
        } else {
            // 无效查询 → 占位不跳过：fr.systemIndex 是 C# 注册序（Batch.Tick 按
            // Get(idx) 解析），跳过任一前置系统 = 其后全部错位——错系统的 ForEach
            // 拿对方查询的指针数组按自己类型解释 = 野读写。占位保 1:1 对齐，
            // TickBatch 跳过零参占位（该系统永不执行，告警响亮）。
            LEMON_WARN("batch system %d invalid query (compCount=%u, regCount=%u) — "
                       "held as placeholder, never ticks",
                       i, bs.compCount, regCount);
            bs.compCount = 0;
        }
        batch_.push_back(bs);
    }
}

void ScriptHost::TickBatch(ecs::World& world, ecs::Scene& scene, float dt) {
    if (!userLoaded_ || !scriptsTickFn_) return;
    if (!batchPulled_) PullBatchRegistry(); // 惰性拉取（此时 World 已构造 = 注册表就绪）
    // 注：批量帧为空也继续——档① behaviours 可能存在（scriptsNeedTick_ 早退从未
    // 接线已删，见 ScriptHost.h；空场景成本登记 M7a）

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
        if (bs.disabled || bs.compCount == 0) continue; // 禁用 / 拉取期占位
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

    // D5 布防（第一层 = 容量预留，第二层 = 基址比对 fail-stop；评审 §D5）：
    // 对本帧全部被查询池 reserve(count + 结构余量)——托管 tick 窗口内的合法
    // spawn/get-or-create 在余量内零重分配（收集指针免疫，entBuf_ 同款"构造期
    // 零扩容"技术下沉组件池；ScriptHost 自己的三个 buffer 上方 reserve 同理）。
    // 预留**先于** gather（reserve 本身可能重分配）与基址记录（记录的是预留后
    // 的稳定基址）。超余量爆量（单 tick 千级以上）仍可能重分配 → 第二层基址
    // 比对置 stale + 剩余块跳过（病态用例响亮截断，正常负载零触发）。
    {
        auto& reg2 = ecs::ComponentRegistry::Instance();
        bool seen[kMaxPoolWatch] = {}; // compId < kMaxPoolWatch（31 组件，余量足）
        g_poolWatchCount = 0;
        g_batchStaleWarned = false;
        constexpr uint32_t kBatchStructHeadroom = 1024; // 单 tick 窗口内结构操作余量
        for (const BatchSys& bs : batch_) {
            if (bs.disabled || bs.compCount == 0) continue;
            for (uint32_t c = 0; c < bs.compCount; c++) {
                const uint16_t id = bs.comps[c];
                if (id < kMaxPoolWatch) {
                    if (seen[id]) continue;
                    seen[id] = true;
                }
                if (g_poolWatchCount >= kMaxPoolWatch) break;
                const ecs::ComponentMeta& m = reg2.At(id);
                if (!m.poolDataFn) continue;
                if (m.reserveFn) {
                    const uint32_t n = m.countFn ? m.countFn(scene) : 0;
                    m.reserveFn(scene, n + kBatchStructHeadroom);
                }
                g_poolWatch[g_poolWatchCount++] = {id, m.poolDataFn(scene)};
            }
        }
    }

    for (uint32_t s = 0; s < batch_.size(); s++) {
        const BatchSys& bs = batch_[s];
        if (bs.disabled || bs.compCount == 0)
            continue; // C# 侧异常禁用：不构造死块；占位（无效查询）：永不执行
        const auto& driver = reg.At(bs.comps[0]);
        if (!driver.countFn) {
            // 预留钩子缺失（M13）：不预留就 gather = 块缓冲增长期 realloc，先前系统
            // fr.blocks 悬垂 → C# 线性步进野读。宁可响亮地整系统跳过，不静默降级
            if (!warnedNoCountFn_) {
                warnedNoCountFn_ = true;
                LEMON_WARN("batch system %u driver lacks countFn — system skipped", s);
            }
            continue;
        }
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
    // 无条件调（批量帧空时档① behaviours 仍需 tick）
    {
        // D5 第二层挂闸：g_batchFrames 指向本帧帧数组——窗口内结构操作触发池重分配
        // 时 MarkBatchStaleIfPoolsMoved 就地置位各帧 stale（C# 块循环头响应）。池
        // 基址已在 gather 前的布防块（reserve + 记录）就位
        g_batchFrames = &frameBuf_;
        NativeApiWindow win(&world, &scene);
        scriptsTickFn_(frameBuf_.data(), (int)frameBuf_.size(), dt);
        // 回读禁用位（域线程已同步返回，栅栏保证可见）
        for (auto& fr : frameBuf_)
            if (fr.disabled) batch_[fr.systemIndex].disabled = true;
        g_batchFrames = nullptr; // D5 收防：窗口关闭（后续 native 调用不再比对）
        g_poolWatchCount = 0;
    }

    // ---- 批③c（ADR-014 M2）：UI ops 拉取——脚本当帧 UI.* + UI.Apply() 的 ready
    // 队列。表现层通道当帧可见：EditorApp 侧钩子直转 UiSubsystem::ApplyOps（先于
    // 主循环 gameUi_->Update()）。无导出（旧 Entry）= 挂空；无钩子（纯运行时/测试
    // 宿主）= 丢弃 + warn-once（同 EditorAssetHooks 降级语义）。
    if (uiOpsPullFn_ && g_uiHooks.applyOps) {
        if (uiOpBuf_.empty()) {
            uiOpBuf_.resize(ui::kUiOpsPerFrame);
            uiArenaBuf_.resize(ui::kUiArenaBytesPerFrame);
        }
        int arenaBytes = 0;
        const int n = uiOpsPullFn_(uiOpBuf_.data(), (int)uiOpBuf_.size(),
                                   uiArenaBuf_.data(), (int)uiArenaBuf_.size(), &arenaBytes);
        if (n > 0) {
            g_uiHooks.applyOps(uiOpBuf_.data(), (uint32_t)n, uiArenaBuf_.data(),
                               (uint32_t)(arenaBytes < 0 ? 0 : arenaBytes));
        } else if (n < 0 && !warnedUiOpsDropped_) {
            warnedUiOpsDropped_ = true;
            LEMON_WARN("ui-ops：单帧 ops/arena 超容量被截断（%d op / %dB arena 上限）",
                       (int)ui::kUiOpsPerFrame, (int)ui::kUiArenaBytesPerFrame);
        }
    } else if (uiOpsPullFn_ && !g_uiHooks.applyOps) {
        // 无钩子宿主也拉空（清 ready 队列防跨宿主滞留）+ warn-once
        int arenaBytes = 0;
        const int n = uiOpsPullFn_(uiOpBuf_.empty() ? nullptr : uiOpBuf_.data(),
                                   0, nullptr, 0, &arenaBytes);
        if (n != 0 && !warnedUiOpsDropped_) {
            warnedUiOpsDropped_ = true;
            LEMON_WARN("ui-ops：宿主未装 UiHooks——脚本 UI ops 丢弃（纯运行时预期内；"
                       "编辑器装配遗漏则查 SetUiHooks）");
        }
    }
}

void ScriptHost::AttachBehaviour(ecs::World& world, ecs::Scene& scene, ecs::Entity e,
                                 int typeId) {
    // M6a 批⓪：追加新槽（运行时挂载路径——op4/C# AddComponent；className 空 =
    // Play 期内槽，ExitPlay 随快照丢弃不入档）。同类型唯一双层分权：C++ 槽层
    // **幂等**——同 typeId 槽已存在 = 不追加、重挂实例（盖 ResetPlayDomain 后的
    // 合法重挂：槽在、C# 实例已清）；真双挂（C# 实例仍在）由 Behaviours.Attach
    // 断言拒绝（桥层可见实例态）。槽满拒绝。
    ScriptBox* sb = scene.TryGet<ScriptBox>(e);
    if (!sb) sb = &scene.Emplace<ScriptBox>(e);
    bool slotExists = false;
    for (uint32_t i = 0; i < sb->count; ++i)
        if (sb->slots[i].typeId == typeId) slotExists = true;
    if (!slotExists) {
        if (!AppendSlot(*sb, 0, nullptr)) {
            LEMON_WARN("脚本槽满（%u）：实体 %llu 挂载 '%d' 拒绝", kMaxScriptsPerEntity,
                       (unsigned long long)e.id, typeId);
            return;
        }
        sb->slots[sb->count - 1].typeId = typeId;
    }
    // Awake/OnEnable 在 PostBatch 内同步执行——native 窗口必须就位（M11）
    if (scriptsAttachFn_) {
        NativeApiWindow win(&world, &scene);
        scriptsAttachFn_(typeId, e.id);
    }
}

void ScriptHost::ResolveSlotBehaviour(ecs::World& world, ecs::Scene& scene, ecs::Entity e,
                                      uint32_t slotIdx, int typeId) {
    ScriptBox* sb = scene.TryGet<ScriptBox>(e);
    if (!sb || slotIdx >= sb->count) return;
    ScriptSlot& s = sb->slots[slotIdx];
    s.typeId = typeId;
    s.flags &= ~kScriptFlagDisabled;
    if (scriptsAttachFn_) {
        NativeApiWindow win(&world, &scene);
        scriptsAttachFn_(typeId, e.id);
    }
}

void ScriptHost::DetachBehaviour(ecs::World& world, ecs::Scene& scene, ecs::Entity e,
                                 int typeId) {
    // M6a 批⓪ T3：op5/C# RemoveComponent 脚本分路。先通知托管（OnDestroy +
    // 实例级订阅退订——PostBatch 内同步执行，native 窗口就位 M11），再卸槽
    //（保序 RemoveSlot；空盒随卸与 EditorContext::RemoveScriptSlot 同口径）。
    // 幂等：槽不在（未挂/已卸/typeId 未解析 -1）= no-op。
    ScriptBox* sb = scene.TryGet<ScriptBox>(e);
    if (!sb) return;
    int idx = -1;
    for (uint32_t i = 0; i < sb->count; ++i)
        if (sb->slots[i].typeId == typeId) {
            idx = (int)i;
            break;
        }
    if (idx < 0) return;
    if (scriptsDetachFn_) { // 旧 Entry 无导出 = 只卸槽不通知托管（挂空安全）
        NativeApiWindow win(&world, &scene);
        scriptsDetachFn_(typeId, e.id);
    }
    RemoveSlot(*sb, (uint32_t)idx);
    if (sb->count == 0) scene.Remove<ScriptBox>(e);
}

const std::vector<std::string>& ScriptHost::BehaviourTypeNames() {
    if (!behaviourNames_.empty() || !userLoaded_) return behaviourNames_;
    if (!behavioursListFn_) {
        behavioursListFn_ =
            (int (*)(char*, int))host_.GetExport("Lemon.Entry.Exports, Lemon.Entry",
                                                 "lemon_behaviours_list");
        if (!behavioursListFn_) return behaviourNames_;
    }
    // M10 契约加固：返回值 = 类型数（非字节数），托管侧 '\n' 分隔 + 自写 '\0' 结尾
    //（cap 不足 = -1）。零初始化 + n<=0 早退兜住失败/短写——原未初始化缓冲在托管侧
    // 失败时走未初始化栈内存。-1 = 缓冲不足：固定 4KB 对其无解（每次重试同容量 =
    // 永远失败，类型表永远空）→ 堆缓冲倍增重试（1MB 上限 ≈ 3 万+ 类型，超限保持空表）
    std::vector<char> buf(4096);
    int n = behavioursListFn_(buf.data(), (int)buf.size());
    while (n == -1 && buf.size() < (1u << 20)) {
        buf.resize(buf.size() * 2);
        n = behavioursListFn_(buf.data(), (int)buf.size());
    }
    if (n <= 0) return behaviourNames_; // 零类型/超上限失败：保持空表（下次再试）
    for (const char* p = buf.data(); *p;) { // '\n' 分隔、'\0' 结尾
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
    return gcAllocFn_();
}

void ScriptHost::ResetScriptTime()
{
    // M5 清障①：编辑器 EnterPlay 调。启动期已解析指针，此处零 GetExport（GC 纪律同上）
    if (timeResetFn_) timeResetFn_();
}

void ScriptHost::ResetPlayDomain()
{
    // M5 批④后修：编辑器 EnterPlay 调（ResetScriptTime 之后、装配新实例之前）。
    // 启动期已解析指针（GC 纪律同上）；bench/回放路径不经过 = 金档零扰动。
    if (playResetFn_) playResetFn_();
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
                // 批⑥b：结构命令建实体打标 active（同 NativeSpawnSprite——零未打标
                // 不变量跨全部运行时建实体路径）
                scene.Emplace<ecs::SceneMembership>(e).scene = world.ActiveSceneHandle();
                resolved.emplace_back(op.entity, e);
                break;
            }
            case 1: { // Destroy（先 OnDestroy 通知，再入两阶段队列——本批随后 CommitDestroys 生效）
                ecs::Entity e = Resolve(op.entity);
                if (!e.IsNull()) {
                    // OnDestroy 在 PostBatch 内同步执行——native 窗口必须就位（M11）
                    if (scriptsDestroyFn_) {
                        NativeApiWindow win(&world, &scene);
                        // 置"已通知"位：同帧稍后的 NotifyPendingDestroys 不再对同实体
                        // 双发（F-08.2 汇合点恰好一次语义；实体级非槽级——多槽实体
                        // 按实体一次通知全量实例）
                        if (ScriptBox* sb = scene.TryGet<ScriptBox>(e))
                            sb->notified |= kScriptFlagDestroyNotified;
                        scriptsDestroyFn_(e.id);
                    }
                    scene.Destroy(e);
                }
                break;
            }
            case 2: { // AddComponent（注册表驱动；get-or-create——对已有组件再
                // emplace = entt 池损坏，与 NativeWrite/AttachBehaviour 同口径 M12）
                ecs::Entity e = Resolve(op.entity);
                if (!e.IsNull() && op.compId < reg.Count()) {
                    const auto& m = reg.At(op.compId);
                    if (!m.hasFn(scene, e)) m.emplaceFn(scene, e);
                }
                break;
            }
            case 3: { // RemoveComponent
                ecs::Entity e = Resolve(op.entity);
                if (!e.IsNull() && op.compId < reg.Count() && reg.At(op.compId).removeFn)
                    reg.At(op.compId).removeFn(scene, e);
                break;
            }
            case 4: { // AttachScript（挂 ScriptBox 新槽 + 托管实例/Awake/OnEnable）
                // AttachBehaviour = 追加路径（M6a 批⓪ 与解析路径拆分）；native 窗口
                // 内部就位（M11/M12——对快照已带 ScriptBox 的实体二次 Emplace =
                // entt 池损坏的旧坑已由 TryGet 分支消除）
                ecs::Entity e = Resolve(op.entity);
                if (!e.IsNull()) AttachBehaviour(world, scene, e, (int)op.compId);
                break;
            }
            case 5: { // DetachScript（M6a 批⓪ T3：卸单槽——OnDestroy+退订+槽移除）
                // 防御：typeId 查无槽/ScriptBox 不在 = 幂等 no-op（DetachBehaviour 内）
                ecs::Entity e = Resolve(op.entity);
                if (!e.IsNull()) DetachBehaviour(world, scene, e, (int)op.compId);
                break;
            }
            default: break;
            }
        }
        if (n < (int)opBuf_.size()) break; // 拉空
    }
}

void ScriptHost::NotifyPendingDestroys(ecs::World& world, ecs::Scene& scene) {
    // F-08.2（2026-09-24）：C++ 系统路径（战斗击杀/投射物到期/越界回收等）直接
    // scene.Destroy 入队，此前从不通知脚本域——托管实例与实例级订阅（M15 自动退订
    // 挂 OnDestroy）残留到下次换域/ClearInstances，事件回调长期持有失效句柄。
    // 在统一提交点前补发：DestroyQueueTag ∩ ScriptBox 且未通知过的实体。
    // 恰好一次由 kScriptFlagDestroyNotified 保证（脚本命令路径 ApplyStructural 已
    // 通知并置位）。池内部序遍历 = 确定序（回放两侧同源）。
    // 迭代器不变量（#52）：循环体内跑任意托管 OnDestroy——当前所有导出（spawn/
    // attach/destroy）都只投递命令不就地改池，View 迭代安全靠这一前提成立。日后
    // vtable 若加"同步 native Destroy/Attach"导出，本循环必须先收集再回调。
    if (!scriptsDestroyFn_) return;
    NativeApiWindow win(&world, &scene);
    // 空 tag 不进 each() 载荷（entt 3.15 语义）——DestroyQueueTag 只作过滤器
    for (auto&& [ent, sb] : scene.View<ecs::DestroyQueueTag, ScriptBox>().each()) {
        if (sb.notified & kScriptFlagDestroyNotified) continue;
        sb.notified |= kScriptFlagDestroyNotified;
        scriptsDestroyFn_(ecs::Scene::FromEntt(ent).id);
    }
}

void ScriptHost::PullPendingEvents(ecs::World& world) {
    // M3-4：#16 头部拉脚本 pending 入队（当帧派发批次）
    if (eventsPullFn_ && pullBuf_.empty()) pullBuf_.resize(256);
    while (eventsPullFn_) {
        int n = eventsPullFn_(pullBuf_.data(), (int)pullBuf_.size());
        for (int i = 0; i < n; i++)
            if (!world.Events().Push(pullBuf_[i])) break; // 满上限丢弃计数由队列管
        if (n < (int)pullBuf_.size()) break; // 拉空
    }
}

void ScriptHost::DispatchEvents(ecs::World& world, ecs::Scene& scene,
                                const ecs::EventPacket* events, uint32_t count) {
    // 入参 = #16 取出的稳定快照（与队列底层分离）：订阅方回调内 Events().Push
    // 落回队列、下帧派发，不再有旧 HeadSpan/TailSpan 直指队列缓冲、回调 Push 触发
    // Grow 即段指针悬空的窗口（2026-09-24 审查 P5；快照拷贝 48B×N 代价可忽略）
    // M5 批②：事件回调与 Update 同一 native 窗口（g_world/g_scene）——订阅方在
    // WaveStart 等回调内可调 Ui.Set/Time.Scale/Instantiate。此前窗口只盖 TickBatch，
    // #16 派发期的回调内 native 调用会静默空转（批① xp 样例恰在 Update 内调用
    // 故未暴露）。
    NativeApiWindow win(&world, &scene);
    // 批③c（M3）：UI 事件先派发——上一帧 gameUi_->Update() 产生、经 UiHooks 抽干
    // 到此（与游戏事件同一 #16 站点，帧内延迟一致；回调内可调 native，窗口已就位）
    if (uiEventsDispatchFn_ && g_uiHooks.drainEvents) {
        if (uiEventBuf_.empty()) uiEventBuf_.resize(ui::kUiEventsPerDrain);
        const uint32_t n = g_uiHooks.drainEvents(uiEventBuf_.data(),
                                                 (uint32_t)uiEventBuf_.size());
        if (n > 0) uiEventsDispatchFn_(uiEventBuf_.data(), (int)n);
    }
    if (!eventsDispatchFn_ || !events || count == 0) return;
    eventsDispatchFn_(events, (int)count);
}

} // namespace lemon::scripting
