// Lemon 引擎 — .controller 动画状态机资产解析/序列化（M6a 批② T3d；ADR-013
// D1/D4；06 §2.2；M7a 批② 自 Editor/Assets/ControllerEdit 下沉引擎——运行时
// BuildPlayControllerCache 与编辑器共用）。纯文本逻辑，零 ImGui/GPU 依赖（可单测）。
// schema（字符串形态——运行时编译为下标形态入 ControllerTable）：
//   { "schemaVersion": 1, "name": "BasicCharacter",
//     "params": [ {"name":"speed","kind":"float","def":0.0},
//                 {"name":"attack","kind":"trigger"} ],   // ≤8；kind 缺省 float
//     "entry": "Idle",                                    // 缺省 = states[0]
//     "states": ["Idle","Run","Attack"],                  // 状态名 = 集内段名
//     "transitions": [
//       {"from":"Idle","to":"Run","when":[{"param":"speed",">":0.1}]},
//       {"from":"Attack","to":"Idle","on":"exitTime"} ] } // 段末过渡；可并 "when"
// 边界纪律（ADR-013 D4）：无 any-state/blend/layer；同一 from 按文件序评估首条
// 命中即切；空 conds 且非 exitTime = 无条件即切 → 解析期拒绝（作者错误拦在写盘前）。
// 序列化手写（非 nlohmann dump）：字段序/缩进与 ClipToJson 同型——保存后 diff 最小。
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace lemon::assets {

/// 参数（kind 值域 = AnimParamKind：0 float / 1 bool / 2 trigger）
struct ControllerParamEdit {
    std::string name;
    int kind = 0;
    float def = 0.0f;
    friend bool operator==(const ControllerParamEdit&, const ControllerParamEdit&) = default;
};

/// 条件（op 值域 = AnimCondOp：0 "<" 1 "<=" 2 ">" 3 ">=" 4 "==" 5 trigger）
struct ControllerCondEdit {
    std::string param;
    int op = 2;
    float value = 0.0f;
    friend bool operator==(const ControllerCondEdit&, const ControllerCondEdit&) = default;
};

/// 出边（from/to = 状态名；exitTime = 段末过渡，可与 when 并存 = 段末且条件成立）
struct ControllerTransitionEdit {
    std::string from, to;
    bool exitTime = false;
    std::vector<ControllerCondEdit> conds;
    friend bool operator==(const ControllerTransitionEdit&,
                           const ControllerTransitionEdit&) = default;
};

/// .controller 解析产物（ClipData 同款 ok/error 约定；坏档 ok=false 不炸调用方）
struct ControllerData {
    bool ok = false;
    std::string error;
    std::string name; // 缺省 ""（新建落盘前补文件名）
    std::vector<ControllerParamEdit> params;
    std::vector<std::string> states;
    std::string entry; // 缺省 ""（= states[0]）
    std::vector<ControllerTransitionEdit> transitions;
};

/// .controller JSON 文本 → ControllerData。states[] 必需非空；params/entry/
/// transitions 可缺省。状态重名/参数 >8 或重名/from-to- entry 引用未列状态/
/// op-on-kind 字符串未识别/空 conds 非 exitTime → ok=false + error（首错即返）。
ControllerData ParseControllerJson(std::string_view text);

/// ControllerData → .controller JSON 文本（手写定版格式，见文件头）。ok=false → 空串。
std::string ControllerToJson(const ControllerData& c);

} // namespace lemon::assets
