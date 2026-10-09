// Lemon 引擎 — 游戏事件包（03 文档 §11：帧内延迟派发，C++ 内部与 C# 桥同一队列）
// 系统只入队（RingQueue<EventPacket>，World 持有），帧末 ScriptEventDispatch 批量
// 派发后清空；语义：同帧内顺序敏感（按入队序派发）。
#pragma once

#include <cstdint>

#include "ECS/Entity.h"

namespace lemon::ecs {

enum class GameEvent : uint16_t {
    Spawn = 0,
    Hit,
    Death,
    TriggerEnter,
    TriggerExit,
    WaveStart,
    LevelUp,
    Pickup,
    TimerFire,
    Custom, // 用户自定义区起点（Custom + 用户资产注册 id）
    // T3d 批③（表尾追加在 Custom 之后——Custom 与用户区起点值零位移；事件为瞬态
    // 帧内数据不入档不入哈希，两侧同编译即安全）：
    AnimFrame,    // 帧事件（user = 事件 id；userArg = clipId；payload[0] = 帧号）
    AnimFinished, // 非 loop 段播完（userArg = clipId；补 SDK IsPlaying 判不了播完的缺口）
    // A 档补间（2026-09-28，表尾追加同上约定）：Once 完成恰一次（Yoyo 无完成）。
    // src = 补间实体，userArg = tween 句柄（Lemon.Tween 返回值；Events.Subscribe
    // (GameEvent.TweenFinished) 消费，与 AnimFinished 同款）。
    TweenFinished,
};

/// 换场事件族（M7c 批⑦ D3 / ADR-017 协议⑤）：不进 EventPacket 队列——帧末 #16
/// 派发晚于当帧 Update，满足不了"sceneLoaded 先于新场脚本同帧 Start/Update"的
/// 时序契约；走 IScriptBackend::SceneEventNotify 换场 Essential 窗口内同步直推。
enum class SceneEventKind : uint8_t {
    Unloaded = 0,    // sceneUnloaded：载荷 = old（随行清扫收口后）
    Loaded,          // sceneLoaded：载荷 = new（Awake/OnEnable 后、Start 前；mode 透传）
    ActiveChanged,   // activeSceneChanged：载荷 = old → new（收口）
    AsyncCompleted,  // AsyncSceneLoad.completed（M7c 批⑧）：载荷复用 = oldHandle 侧带
                     // **opId**、newHandle = 新场景句柄（失败终态 = 0——世界不动）。
                     // 推送点 = 激活收口三事件之后（订阅方可查新场）；不走 EventPacket
                     //（同族时序理由——域线程换场窗口内同步直推）
};

struct EventPacket {
    GameEvent type = GameEvent::Spawn;
    uint16_t user = 0;          // Custom 事件的资产注册 id / 命中部位等
    Entity src, dst;            // 8B：事件主体 / 承受者
    float payload[4] = {};      // 16B：位置/伤害量/方向等（按事件类型约定）
    uint64_t userArg = 0;       // 8B：透传句柄（资产 GUID / 指针由语义层解释）
};
static_assert(sizeof(EventPacket) == 48, "EventPacket 布局固定（桥侧 blittable）");

} // namespace lemon::ecs
