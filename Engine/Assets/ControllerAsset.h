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
//       {"from":"Attack","to":"Idle","on":"exitTime"},    // 段末过渡；可并 "when"
//       {"from":"*","to":"Hit","when":[{"trigger":"hit"}]} ] } // any-state（批⑪）
// 边界纪律（ADR-013 D4 修订 2026-10-16）：any-state 开禁（from="*"——任意当前状态
// 出发，受击/死亡等全局中断族；求值序 = any 边先于状态边、组内文件序、首条命中即切，
// 编译期前置实现）；无 blend/layer/sub-FSM（红线维持）。自环 from==to 拒（图切换
// 无重启语义——重启走脚本 Play）；空 conds 且非 exitTime = 无条件即切 → 解析期拒绝。
// 序列化手写（非 nlohmann dump）：字段序/缩进与 ClipToJson 同型——保存后 diff 最小。
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace lemon::assets {

/// any-state 出边源哨兵（M8 批⑪，ADR-013 D4 修订）：from == "*" = 从任意当前
/// 状态出发（受击/死亡等全局中断族）。状态名域保留字——ParseControllerJson 拒
/// 状态名 "*"；to 不接受哨兵（to 必须列于 states）。
inline constexpr const char* kControllerAnyFrom = "*";

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

/// 画布节点坐标（.controller 顶层可选 editor.nodes："状态名": [x, y]）。整数，
/// 8px 网格由编辑器拖动侧量化（schema 不强制对齐——旧档手写非网格值照常读）。
/// M8 批⑩a：布局随档持久化（ADR-013 D6；旧档缺省 = 确定性自动布局）。
struct ControllerNodePos {
    std::string state;
    int x = 0, y = 0;
    friend bool operator==(const ControllerNodePos&, const ControllerNodePos&) = default;
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
    std::vector<ControllerNodePos> layout; // 画布布局（可选块；恒按 states 序归一）
};

/// .controller JSON 文本 → ControllerData。states[] 必需非空；params/entry/
/// transitions/editor 可缺省。状态重名或为 "*"（any 哨兵）/参数 >8 或重名/
/// from-to-entry 引用未列状态（from 可为 "*"）/自环 from==to/op-on-kind 字符串
/// 未识别/空 conds 非 exitTime → ok=false + error（首错即返）。
/// editor.nodes 为编辑面附加块：容错读取（未列状态键/坏坐标 = 忽略单项，不拦
/// 解析），读取按 states 序归一（序列化同序——roundtrip 等值的前提）。
ControllerData ParseControllerJson(std::string_view text);

/// ControllerData → .controller JSON 文本（手写定版格式，见文件头）。ok=false → 空串。
std::string ControllerToJson(const ControllerData& c);

/// 结构 lint（M8 批⑨）：解析管"引用悬空"，这里管"逻辑死状态"——entry 出发
/// 沿 transitions 可达性 BFS，不可达状态逐个点名。返回告警文案（空 = 干净）；
/// 与解析错误不同，告警不拦解析（ok=true 的档也可能有死状态——作者面提示）。
/// 编辑器工作台每帧展示、EditorTests 单测同源。
std::vector<std::string> ControllerStructuralWarnings(const ControllerData& c);

/// 可达性位图（states 序；1 = 从 entry（缺省 states[0]）沿 transitions 可达）。
/// 坏档/entry 悬空 = 全 0。结构 lint 与画布灰化同源（M8 批⑩a——杜绝两套 BFS）。
std::vector<uint8_t> ControllerReachability(const ControllerData& c);

/// 确定性自动布局（M8 批⑩a；entry BFS 分层，列距 192 / 行距 96——均 8px 网格
/// 对齐）。无布局档/新增状态/首次打开用；同输入同输出（无随机，测试依赖）。
/// 不可达状态归尾列（BFS 展开，states 序起根）。返回恒按 states 序。
std::vector<ControllerNodePos> ControllerAutoLayoutNodes(const ControllerData& c);

/// 画布坐标网格量化（8px 最近格；编辑器拖动/落位用——schema 不强制，编辑器保证）
int ControllerSnapGrid(int v);

} // namespace lemon::assets
