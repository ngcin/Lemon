// Lemon 引擎 — 脚本宿主桥后端（04 §2.2 / ADR-010 D1 域线程模型）
// 职责：CoreCLR 引导 + 用户程序集装载 + 批量帧缓冲构造（管线线程）→ lemon_batch_tick
// （域线程执行）。头文件零 hostfxr 类型（CoreCLRHost 自有句柄）。
#pragma once

#include <cstdint>
#include <vector>

#include "ECS/Scene.h"
#include "ECS/World.h"
#include "Scripting/CoreCLRHost.h"

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

/// 档① 脚本组件（普通 entt 组件；**不入 ComponentRegistry**——GCHandle/类型 id 属
/// 运行时桥状态，入注册表会进 StateHash/序列化，破坏回放与 .lscene 语义）。
struct ScriptBox {
    int32_t typeId = -1;     // Behaviours 注册序（C# 侧）
    uint32_t flags = 0;      // bit0 disabled（异常禁用）
};

/// native 函数表（低频语法糖通道；与 Lemon.SDK/NativeApi.cs 逐字节一致）
struct NativeApiVtable {
    int (*isAlive)(uint64_t);
    int (*hasComponent)(uint64_t, uint8_t);
    int (*readComponent)(uint64_t, uint8_t, void*, uint32_t);
    int (*writeComponent)(uint64_t, uint8_t, const void*, uint32_t);
};

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

    bool IsUserLoaded() const { return userLoaded_; }
    uint32_t BatchSystemCount() const {
        return batchPulled_ ? (uint32_t)batch_.size()
                            : (uint32_t)(batchCountFn_ ? batchCountFn_() : 0);
    }

    /// 挂载脚本组件（宿主装配期用；bench/编辑器入口）——ScriptBox + 托管实例/Awake。
    void AttachBehaviour(ecs::Scene& scene, ecs::Entity e, int typeId);

    /// 托管累计分配字节数（GC 纪律验收；两次读数差 = 期间分配）。
    uint64_t GcAllocated() const;

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
    int (*batchCountFn_)() = nullptr;
    int (*batchQueryFn_)(int, uint8_t*, int) = nullptr;
    void (*batchTickFn_)(BatchSystemFrame*, int) = nullptr;
    void (*eventsDispatchFn_)(const ecs::EventPacket*, int) = nullptr;
    int (*eventsPullFn_)(ecs::EventPacket*, int) = nullptr;
    void (*scriptsTickFn_)(BatchSystemFrame*, int, float) = nullptr;
    void (*scriptsAttachFn_)(int, uint64_t) = nullptr;
    void (*scriptsDestroyFn_)(uint64_t) = nullptr;
    int (*opsPullFn_)(SceneOpC*, int) = nullptr;
    mutable unsigned long long (*gcAllocFn_)() = nullptr; // 惰性解析一次（GetExport 每调
    // 一次会在托管侧分配——M3-7 GC 验收实测坑）

    std::vector<ecs::EventPacket> pullBuf_; // 脚本 pending 拉取缓冲（复用）
    std::vector<SceneOpC> opBuf_;           // 结构命令拉取缓冲（复用）
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
