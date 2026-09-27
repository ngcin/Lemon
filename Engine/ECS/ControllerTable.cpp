// Lemon 引擎 — ControllerTable 实现（M6a 批② T3d；头说明见 ControllerTable.h）
#include "ECS/ControllerTable.h"

namespace lemon::ecs {

int32_t ControllerDef::StateIndex(std::string_view name) const {
    for (size_t i = 0; i < states.size(); ++i)
        if (states[i] == name) return (int32_t)i;
    return -1;
}

int32_t ControllerDef::ParamIndex(std::string_view name) const {
    for (size_t i = 0; i < params.size(); ++i)
        if (params[i].name == name) return (int32_t)i;
    return -1;
}

bool AnimCondHolds(const AnimCondDef& c, float v) {
    switch (c.op) {
    case AnimCondOp::Lt: return v < c.value;
    case AnimCondOp::Le: return v <= c.value;
    case AnimCondOp::Gt: return v > c.value;
    case AnimCondOp::Ge: return v >= c.value;
    case AnimCondOp::Eq: return v == c.value; // bool（0/1）与 float 精确比较同径
    case AnimCondOp::Trigger: return v != 0.0f; // 非 0 = 待消费
    }
    return false;
}

bool AnimCondsHold(const ControllerDef& def, const AnimTransitionDef& t,
                   const float* params) {
    for (const AnimCondDef& c : t.conds) {
        if (c.param >= def.params.size()) return false; // 坏档防御：参数越界 = 不成立
        if (!AnimCondHolds(c, params[c.param])) return false;
    }
    return true;
}

bool ControllerTable::Add(uint32_t controllerId, ControllerDef&& def) {
    if (controllerId == 0 || def.states.empty()) return false;
    controllers_.insert_or_assign(controllerId, std::move(def));
    return true;
}

const ControllerDef* ControllerTable::Find(uint32_t controllerId) const {
    auto it = controllers_.find(controllerId);
    return it != controllers_.end() ? &it->second : nullptr;
}

} // namespace lemon::ecs
