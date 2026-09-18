// Lemon 引擎 — 实体句柄（03 文档 §2）
// 业务代码（Samples/Editor/Templates/C#）只经此句柄与 World/Scene 交互，
// 不接触 EnTT 类型。编码：低 32 位 = entt::entity（自带 version 回收位）+1 偏移，
// 0 恒为 null（entt 0 是合法实体，必须偏移避开）；高 32 位保留（未来跨 Scene 索引）。
#pragma once

#include <cstdint>
#include <functional>

namespace lemon::ecs {

struct Entity {
    uint64_t id = 0;

    constexpr bool IsNull() const { return id == 0; }
    constexpr bool operator==(const Entity& r) const { return id == r.id; }
    constexpr bool operator!=(const Entity& r) const { return id != r.id; }

    static constexpr Entity Null() { return {}; }
};

} // namespace lemon::ecs

template <>
struct std::hash<lemon::ecs::Entity> {
    size_t operator()(const lemon::ecs::Entity& e) const noexcept {
        return std::hash<uint64_t>{}(e.id);
    }
};
