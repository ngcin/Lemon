// Lemon 引擎 — 脚本宿主桥后端（04 §2.2 / ADR-010 D1 域线程模型）
// 职责：CoreCLR 引导 + 用户程序集装载 + 批量帧缓冲构造（管线线程）→ lemon_batch_tick
// （域线程执行）。头文件零 hostfxr 类型（CoreCLRHost 自有句柄）。
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ECS/Scene.h"
#include "ECS/World.h"
#include "Scripting/CoreCLRHost.h"
#include "Scripting/ScriptBox.h"
#include "Ui/UiBridge.h"

namespace lemon::scripting {

// 与 Lemon.Entry/Batch.cs 逐字节一致（改动 = 破回放，两侧同步）
struct BatchBlock {
    ecs::Entity* entities; // length 个
    void** comps;          // [slot * stride + i] = 实例指针
    int32_t length;
    int32_t stride;
};
struct BatchSystemFrame {
    int32_t systemIndex;
    BatchBlock* blocks;
    int32_t blockCount;
    uint64_t rngSeed;      // 世界种子（C# 侧 Lemon.Scripting.Rng 派生子流）
    float dt;
    int32_t disabled;      // C# 回写：异常禁用后 C++ 侧跳过后续帧构造
};
static_assert(sizeof(BatchBlock) == 24);
static_assert(sizeof(BatchSystemFrame) == 40); // disabled 落在原尾垫（C# 同规则）

/// native 函数表（低频语法糖通道；与 Lemon.SDK/NativeApi.cs 逐字节一致）
struct NativeApiVtable {
    int (*isAlive)(uint64_t);
    int (*hasComponent)(uint64_t, uint8_t);
    int (*readComponent)(uint64_t, uint8_t, void*, uint32_t);
    int (*writeComponent)(uint64_t, uint8_t, const void*, uint32_t);
    // ---- M4.4 SDK 最小增量（M4.md §4-8；表尾追加 = 旧宿主零扰动）----
    void (*getInput)(uint64_t* buttons, float* ax, float* ay);      // 当前 InputState 快照
    uint32_t (*spriteOfGuid)(const char*);                          // 资产 GUID → spriteId（0=无）
    uint64_t (*spawnSprite)(uint32_t, float, float);                // Instantiate.Spawn → 实体句柄
    uint64_t (*instantiatePrefab)(const char*, float, float);       // Prefab 实例化 → 句柄（0=失败）
    // ---- M5 批①（timeScale ↔ Time.Scale；表尾追加同上约定）----
    float (*getTimescale)();                                        // World::TimeScale
    void (*setTimescale)(float);                                    // World::SetTimeScale（clamp [0,8]）
    void (*rtUiSet)(const char* key, const char* text, float frac); // Lemon.Ui.Set → World::RtUi
    // ---- M5 批④（存档通道 + HUD 完整版；表尾追加同上约定）----
    int32_t (*saveSet)(const char* key, const void* bytes, uint32_t len); // Lemon.Save.Set → World::Saves
    int32_t (*saveGetLen)(const char* key);                              // -1 = 无此键
    int32_t (*saveGet)(const char* key, void* out, uint32_t cap);        // 返回拷贝数（-2 = cap 不足）
    void (*saveFlush)();                                                 // ScriptIoHooks 落盘（C# Save.Flush）
    void (*rtUiClear)(const char* key);                                  // Lemon.Ui.Clear → RtUi 删单行
    void (*rtUiSetEx)(const char* key, const char* text, float frac,
                      uint32_t color);                                   // Lemon.Ui.Set 着色版（0 = 默认）
    void (*uiCards)(int32_t show, const char* title, const char* a,
                    const char* b, const char* c);                       // 三选一卡片显隐/内容
    int32_t (*uiCardPick)();                                             // 消费式：返回已选索引后置 -1
    // ---- M6a 批①（世界空间表现通道：飘字/血条 → World.Fx；表尾追加同上约定）----
    void (*fxPopup)(const char* text, float x, float y, uint32_t color);
    void (*fxBar)(uint64_t entity, float frac, uint32_t color, float width);
    // ---- 2026-09-26 调参下放批（分离力参数场景侧化：Lemon.Physics.Separation →
    // World::Separation()；表尾追加同上约定。改运行时参数不入 StateHash，基准场
    // 零调用 = 零漂移）----
    void (*setSeparation)(float radius, float strength, int32_t maxNeighbors,
                          int32_t densityCap); // 各参 <0 = 保持现值
    // ---- M6a 批②（配置表读取：Lemon.Table → World.Tables；表尾追加同上约定。
    // guidHex 16 位 hex → 低 32 位查表；空宿主/无表 = -1 降级（SDK 侧自查）。
    // 表通道不入 StateHash，基准场零调用 = 零漂移）----
    int32_t (*tableRows)(const char* guidHex);                       // 行数（含列头行）；-1 = 无表
    int32_t (*tableCols)(const char* guidHex);                       // 列数；-1 = 无表
    int32_t (*tableCell)(const char* guidHex, int32_t row, int32_t col,
                         char* out, uint32_t cap); // 拷贝数（不含 NUL）；-1 越界/无表；-2 cap 不足
    // ---- M6a 批② T3c（动画集按名解析：Lemon.Anim 字符串重载 → 实体当前段所在
    // .override 集内查段名；表尾追加同上约定。空宿主/无段/不属集/集内无名 = -1。
    // 按名通道不入 StateHash，基准场零调用 = 零漂移。T3d 批①起作用域优先实体
    // AnimGraph.setGuid 显式绑定，回退 T3c 当前段口径）----
    int64_t (*clipByName)(uint64_t entity, const char* name);        // clipId；-1 = 解析失败
    // ---- M6a 批② T3d（动画参数槽解析：Lemon.Anim SetParam/GetParam/Trigger →
    // 实体所绑 .controller 参数表定序槽位；表尾追加同上约定。空宿主/未绑
    // controller/表未登记/参数表无名 = -1。低频桥，基准场零调用 = 零漂移）----
    int32_t (*animParamSlot)(uint64_t entity, const char* name);     // 槽位 0..7；-1 = 解析失败
    // ---- A 档运行时补间（2026-09-28：Lemon.Tween → World.Tweens；表尾追加同上
    // 约定。tweenTo 失败（空宿主/实体亡/组件缺/字段名未命中/类型不可插值）= 0
    // 句柄 + warn-once；基准场零调用 = 零漂移。ease/mode 取值 = TweenEase/
    // TweenMode 枚举（引擎侧防御钳）----
    uint64_t (*tweenTo)(uint64_t entity, uint8_t compId, const char* field,
                        const float* to4, float duration, uint8_t ease,
                        uint8_t mode); // 补间句柄（0 = 失败；Alive/TweenFinished userArg 用）
    int32_t (*tweenKill)(uint64_t entity, uint8_t compId,
                         const char* field); // 删同实体同字段（field 空 = 该组件全部）；返回移除数
    int32_t (*tweenKillEntity)(uint64_t entity); // 删该实体全部；返回移除数
    int32_t (*tweenAlive)(uint64_t handle);      // 0/1（低频轮询）
    // ---- M6a 批② T4（XP 曲线参数：Lemon.Balance.XpCurveK → World；表尾追加
    // 同上约定。ADR-012 D3：默认 1.25 不变 = 基准场零漂移；空宿主读默认写丢弃）----
    float (*getXpCurveK)();                      // World::XpCurveK
    void (*setXpCurveK)(float);                  // World::SetXpCurveK（不 clamp）
    // ---- M6a 批② T5（存档分档：Lemon.Save × Chan → World::Saves(ch)；表尾追加
    // 同上约定。ch 越界红字一次 + 落 slot；空宿主与既有四指针同款降级。旧宿主无
    // Ex = SDK 判空回落单档语义（chan 忽略）。saveFlush 不加 Ex——语义 = 全档落盘 ----
    int32_t (*saveSetEx)(const char* key, const void* bytes, uint32_t len,
                         uint8_t ch); // Lemon.Save.Set(…, Chan) → World::Saves(ch)
    int32_t (*saveGetLenEx)(const char* key, uint8_t ch); // -1 = 无此键
    int32_t (*saveGetEx)(const char* key, void* out, uint32_t cap,
                         uint8_t ch); // 返回拷贝数（-2 = cap 不足）
};

/// 编辑器资产钩子（M4.4：编辑器宿主装配期经 SetEditorAssetHooks 注入；
/// 纯运行时（打包游戏）不装 = 相关 native 入口返回 0，脚本侧无编辑器依赖）
struct EditorAssetHooks {
    uint32_t (*spriteOfGuid)(const char* guidHex);
    uint64_t (*instantiatePrefab)(const char* guidHex, float x, float y);
};
/// 进程级单份（宿主装配期一次；thread-safe 之前 = 引导期约定）
void SetEditorAssetHooks(const EditorAssetHooks& hooks);

/// 存档 IO 钩子（M5 批④ D1：编辑器注入项目路径实现；纯运行时 M8 自带
/// %APPDATA% 版）。未注入 = C# Save.Flush 红字一次后 no-op（内存态照常）。
struct ScriptIoHooks {
    void (*saveFlush)(ecs::World& world); // 全量落盘（幂等）
};
void SetScriptIoHooks(const ScriptIoHooks& hooks);

/// UI 桥钩子（M6a 批③c，ADR-014 D2 M2/M3）：ops 应用方与事件抽干方。编辑器装配期
/// 经 SetUiHooks 注入（applyOps → UiSubsystem::ApplyOps / drainEvents → DrainEvents）；
/// 纯运行时/测试宿主不装 = ops 丢弃 warn-once（同 EditorAssetHooks 降级语义）。
/// UI 状态不入 StateHash、UI 交互不入输入快照——基准场零调用零漂移、金回放零重录。
struct UiHooks {
    void (*applyOps)(const ui::UiOpC* ops, uint32_t count, const char* arena,
                     uint32_t arenaBytes);
    uint32_t (*drainEvents)(ui::UiEventC* dst, uint32_t cap);
};
void SetUiHooks(const UiHooks& hooks);

/// 结构命令（与 Lemon.SDK/SceneOps.cs SceneOp 一致，16B）
struct SceneOpC {
    uint8_t type;
    uint8_t compId;
    uint16_t reserved;
    uint64_t entity;
};
static_assert(sizeof(SceneOpC) == 16);

class ScriptHost final : public ecs::IScriptBackend {
public:
    /// CoreCLR 引导（进程一次）。路径为空时由测试/宿主传入 LEMON_SCRIPT_DIR 下产物。
    bool Initialize(const char* dotnetRoot, const char* runtimeConfigPath,
                    const char* entryAssemblyPath);

    /// 用户程序集装载（可回收 ALC + GameMain.Configure）+ 拉取批量系统注册表。
    bool LoadUserAssembly(const char* path);

    // ---- M4.5 热重载（ADR-010 A 线整域重建；M4.md §3.7）----
    struct HotReloadInfo {
        bool ok = false;       // 新域可用
        int reloadCount = 0;   // 累计换装次数
        int leakCount = 0;     // 累计旧域未回收次数（已知 runtime 限制，红字告警口径）
        bool lastCollected = false; // 本次旧域确认回收（runtime 修复 → B 线信号）
    };
    /// 整域换装：StateBag 捕获 → 旧域尽力卸载 → 新 ALC 装载。成功后类型名表/
    /// 批量注册表惰性重拉；场景侧 ScriptBox 重装配由编辑器驱动（Edit 刷新 typeId、
    /// Play 经 AttachBehaviour 原位换实例 + 状态恢复）。
    HotReloadInfo HotReloadAssembly(const char* path);
    int HotReloadCount() const { return hrCount_; }
    int HotReloadLeakCount() const { return hrLeaks_; }

    bool IsUserLoaded() const { return userLoaded_; }
    uint32_t BatchSystemCount() const {
        return batchPulled_ ? (uint32_t)batch_.size()
                            : (uint32_t)(batchCountFn_ ? batchCountFn_() : 0);
    }

    /// 运行时挂新脚本（op4/C# AddComponent 路径）：ScriptBox 追加槽 + 托管实例/
    /// Awake。同类型唯一由 Behaviours.Attach 断言兜底（真双挂 = 红字跳过）。
    /// Awake/OnEnable 在此同步执行，native 窗口（g_world/g_scene）由内部就位（M11）。
    void AttachBehaviour(ecs::World& world, ecs::Scene& scene, ecs::Entity e, int typeId);

    /// 运行时卸单槽脚本（op5/C# RemoveComponent 脚本分路，M6a 批⓪ T3）：
    /// 托管侧单实例 OnDestroy + 实例级订阅退订 → ScriptBox 槽移除（保序；
    /// 空盒随卸）。未挂/已卸 = 幂等 no-op。旧 Entry 无 lemon_scripts_detach
    /// 导出 = 只卸槽不通知托管（挂空安全）。
    void DetachBehaviour(ecs::World& world, ecs::Scene& scene, ecs::Entity e, int typeId);

    /// 既有槽解析（编辑器 EnterPlay/热重载路径）：场景档槽 typeId=-1 待解析时按名
    /// 映射并原位落 typeId（不追加槽），再挂托管实例（M6a 批⓪ 与 AttachBehaviour
    /// 拆分——原 get-or-create 覆写语义在多槽下会错删兄弟槽）。
    void ResolveSlotBehaviour(ecs::World& world, ecs::Scene& scene, ecs::Entity e,
                              uint32_t slotIdx, int typeId);

    /// 已注册脚本类型名表（M4.4 编辑器装配通路：Inspector 列表/className→typeId 解析；
    /// 未加载用户程序集返回空）。惰性拉取缓存。
    const std::vector<std::string>& BehaviourTypeNames();

    /// 托管累计分配字节数（GC 纪律验收；两次读数差 = 期间分配）。
    uint64_t GcAllocated() const;

    /// Time 归零（编辑器进 Play = 新的一局；M5 清障①）。Play↔Edit 不换脚本域，
    /// Time 属于"局"——必须显式归零。旧 Entry 程序集无该导出时 = 安全 no-op。
    void ResetScriptTime();

    /// 进 Play 域复位（M5 批④后修）：硬清 behaviour 实例/事件订阅（Unity
    /// "Enter Play = 新域"的实例面）。根因：Stop 弃 playWorld 时 C# 侧无人
    /// Detach，同实体 id 再 Attach = 双实例双 tick（VS 模板实测每局 +2 刃）。
    /// bench/回放不经此路径 = 金档零扰动；旧 Entry 无导出 = 安全 no-op。
    void ResetPlayDomain();

    /// 直接取导出（测试/扩展用；勿在热路径调用——启动期一次取全的同一纪律）。
    CoreCLRHost& RawHost() { return host_; }

    // ---- IScriptBackend（World 注入；#14/#15 调用）----
    void TickBatch(ecs::World& world, ecs::Scene& scene, float dt) override;
    void PullPendingEvents(ecs::World& world) override; // #16 头部：pending → Events()
    void DispatchEvents(ecs::World& world, ecs::Scene& scene, const ecs::EventPacket* events,
                        uint32_t count) override; // #16 主体：快照 → C# 订阅者
    void ApplyStructural(ecs::World& world, ecs::Scene& scene) override; // 帧首 Essential
    /// CommitDestroys 前遍历待销毁队列：带 ScriptBox 且未通知过的实体补发
    /// OnDestroy（F-08.2——C++ 系统销毁与脚本命令销毁统一在此汇合，恰好一次）
    void NotifyPendingDestroys(ecs::World& world, ecs::Scene& scene) override;

private:
    struct BatchSys {
        uint16_t comps[8];
        uint8_t compCount;
        bool disabled = false; // C# 回写（异常禁用）
    };
    void PullBatchRegistry(); // 惰性：首次 TickBatch 时拉（World 已构造、注册表就绪）

    CoreCLRHost host_;
    bool userLoaded_ = false;
    std::vector<BatchSys> batch_;

    int (*dmLoad_)(const char*) = nullptr;
    int (*dmUnload_)() = nullptr;
    int (*dmReload_)(const char*, int*, int*) = nullptr; // M4.5（路径, *泄漏数, *本次回收）
    int (*hrReloadsFn_)() = nullptr;
    int (*hrLeaksFn_)() = nullptr;
    int (*batchCountFn_)() = nullptr;
    int (*batchQueryFn_)(int, uint8_t*, int) = nullptr;
    void (*batchTickFn_)(BatchSystemFrame*, int) = nullptr;
    void (*eventsDispatchFn_)(const ecs::EventPacket*, int) = nullptr;
    int (*eventsPullFn_)(ecs::EventPacket*, int) = nullptr;
    void (*scriptsTickFn_)(BatchSystemFrame*, int, float) = nullptr;
    void (*timeResetFn_)() = nullptr; // M5 清障①：lemon_time_reset（旧 Entry = null）
    void (*playResetFn_)() = nullptr; // M5 批④后修：lemon_play_reset（旧 Entry = null）
    void (*scriptsAttachFn_)(int, uint64_t) = nullptr;
    void (*scriptsDestroyFn_)(uint64_t) = nullptr;
    void (*scriptsDetachFn_)(int, uint64_t) = nullptr; // M6a 批⓪ T3（旧 Entry = null 挂空安全）
    int (*opsPullFn_)(SceneOpC*, int) = nullptr;
    // ---- 批③c（旧 Entry 程序集缺两导出 = null 挂空安全，既定纪律）----
    int (*uiOpsPullFn_)(ui::UiOpC*, int, char*, int, int*) = nullptr; // n; *arenaBytes
    void (*uiEventsDispatchFn_)(const ui::UiEventC*, int) = nullptr;
    int (*behavioursListFn_)(char*, int) = nullptr; // 惰性解析一次（BehaviourTypeNames 用）
    mutable unsigned long long (*gcAllocFn_)() = nullptr; // 惰性解析一次（GetExport 每调
    // 一次会在托管侧分配——M3-7 GC 验收实测坑）

    std::vector<ecs::EventPacket> pullBuf_; // 脚本 pending 拉取缓冲（复用）
    std::vector<SceneOpC> opBuf_;           // 结构命令拉取缓冲（复用）
    // ---- 批③c：UI ops/事件桥缓冲（复用；容量 = UiBridge.h 常量）----
    std::vector<ui::UiOpC> uiOpBuf_;
    std::vector<char> uiArenaBuf_;
    std::vector<ui::UiEventC> uiEventBuf_;
    bool warnedUiOpsDropped_ = false;       // 无钩子宿主 ops 丢弃告警只响一次
    std::vector<std::string> behaviourNames_; // 惰性缓存（BehaviourTypeNames）
    int hrCount_ = 0;                       // 换装计数（镜像托管侧；Profiler 显示）
    int hrLeaks_ = 0;                       // 泄漏计数（红字告警口径，ADR-010 A 线）
    bool scriptsNeedTick_ = true;           // 档① 实例存在时即使无批量帧也要跑 tick
    bool batchPulled_ = false;              // 注册表惰性拉取标记（M3-7：装配可早于 World）
    bool warnedNoCountFn_ = false;          // 批量系统缺 countFn 告警只响一次（M13）

    // 帧缓冲（复用；块 stride 恒 64，末块 Length<64）。
    // 必须 vector + 每帧构造前 reserve 足量：C# 侧对 Blocks/Comps/Entities 做线性
    // 指针步进（fr->Blocks + b、Comps + slot*stride）——硬性要求连续；vector 容量
    // 足够时既连续又不搬移。曾用 deque 只看了"不搬移"，但 deque 分块不连续，
    // ≥170 块（24B 元素跨 4KB chunk）后 C# 步进踩出块外 = 野指针——M3-7 实测
    // 10k 弹过/15k+ 崩（AV in ForEach）的根因。
    std::vector<ecs::Entity> entBuf_;
    std::vector<void*> ptrBuf_;
    std::vector<BatchBlock> blockBuf_;
    std::vector<BatchSystemFrame> frameBuf_; // 帧只指向上面三者，本体可 vector
};

} // namespace lemon::scripting
