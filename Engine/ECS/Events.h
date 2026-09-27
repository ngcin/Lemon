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
