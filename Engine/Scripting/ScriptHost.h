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

    /// 挂载脚本组件（宿主装配期用；bench/编辑器入口）——ScriptBox + 托管实例/Awake。
    void AttachBehaviour(ecs::Scene& scene, ecs::Entity e, int typeId);

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
    void DispatchEvents(ecs::World& world, ecs::Scene& scene) override;
    void ApplyStructural(ecs::World& world, ecs::Scene& scene) override; // 帧首 Essential

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
    int (*opsPullFn_)(SceneOpC*, int) = nullptr;
    int (*behavioursListFn_)(char*, int) = nullptr; // 惰性解析一次（BehaviourTypeNames 用）
    mutable unsigned long long (*gcAllocFn_)() = nullptr; // 惰性解析一次（GetExport 每调
    // 一次会在托管侧分配——M3-7 GC 验收实测坑）

    std::vector<ecs::EventPacket> pullBuf_; // 脚本 pending 拉取缓冲（复用）
    std::vector<SceneOpC> opBuf_;           // 结构命令拉取缓冲（复用）
    std::vector<std::string> behaviourNames_; // 惰性缓存（BehaviourTypeNames）
    int hrCount_ = 0;                       // 换装计数（镜像托管侧；Profiler 显示）
    int hrLeaks_ = 0;                       // 泄漏计数（红字告警口径，ADR-010 A 线）
    bool scriptsNeedTick_ = true;           // 档① 实例存在时即使无批量帧也要跑 tick
    bool batchPulled_ = false;              // 注册表惰性拉取标记（M3-7：装配可早于 World）

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
