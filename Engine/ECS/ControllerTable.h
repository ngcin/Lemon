// Lemon 引擎 — 动画状态机 controller 表（M6a 批② T3d；ADR-013 D1 决策层 / 03 §8.2）
// controllerId → {params[], states[], entry, transitions[]}：纯数据 + 评估纯函数，
// 零 GPU 依赖——World 持有，编辑器 EnterPlay 一次性建表（进 Play 时刻资产快照，
// Play 中改 .controller 不生效，BuildPlayClipCache 同语义）。
// 分层（ADR-013 D1）：本表只答"当前状态下哪条出边该切到哪"——切换执行 = 写
// Animator2D 换段队列（指令层既有机制），本表不持有任何逐帧累加状态：当前状态由
// 换段消费后的 clipId 经集绑定反推（ClipTable::NameOfClip），trigger 清零发生在
// AnimGraphSystem 评估内——回放确定性不引入次序敏感隐藏状态。
// controllerId 语义 = .controller 资产 GUID 低 32 位（clipId/prefabId 同款映射约定）。
// 表只 Find 不遍历：unordered_map 迭代序不进任何确定路径。
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace lemon::ecs {

/// 参数种类（AnimParams.v[] 槽的值域约定；trigger = 非 0 待消费，消费即清 0）
enum class AnimParamKind : uint8_t { Float = 0, Bool = 1, Trigger = 2 };

/// 过渡条件算子（value 语义随参数种类：float 比较 / bool 等值(0,1) / trigger 非 0）
enum class AnimCondOp : uint8_t { Lt = 0, Le, Gt, Ge, Eq, Trigger };

struct AnimParamDef {
    std::string name;             // 参数名（controller 内唯一 = 槽位解析键）
    AnimParamKind kind = AnimParamKind::Float;
    float def = 0.0f;             // 档面默认（EnterPlay 初始化 AnimParams 槽）
};

struct AnimCondDef {
    uint16_t param = 0;           // ControllerDef::params 下标
    AnimCondOp op = AnimCondOp::Gt;
    float value = 0.0f;
};

/// 出边：from/to = states 下标；exitTime = 段末过渡（非 loop 段收尾即切，语义 =
/// Queue 的图化形态，吸收 T3c 遗留"段末自动过渡表"）；conds 全 and 首条出边胜
struct AnimTransitionDef {
    uint16_t from = 0, to = 0;
    bool exitTime = false;
    std::vector<AnimCondDef> conds;
};

class ControllerDef {
public:
    std::vector<AnimParamDef> params;    // ≤8（编辑器拦；评估槽 = 下标）
    std::vector<std::string> states;     // 状态名 = 集内段名（绑定键，ADR-013 D2/D4）
    int32_t entry = 0;                   // 入口状态下标（缺省 0）
    std::vector<AnimTransitionDef> transitions; // 同 from 按文件序评估，首条命中即切

    int32_t StateIndex(std::string_view name) const; // -1 = 无此状态
    int32_t ParamIndex(std::string_view name) const; // -1 = 无此参数
};

/// 条件评估（纯函数，engine-tests 直测）：trigger 算子命中即视为"已消费"——
/// 调用方（AnimGraphSystem）对被评估出边里的 trigger 槽执行清 0。
bool AnimCondHolds(const AnimCondDef& c, float paramValue);

/// 出边整体评估（conds 全 and；空 conds = 无条件——exitTime 出边恒由段末驱动，
/// 条件出边空 conds = 每 tick 即切，编辑器侧应拦，运行时宽容）
bool AnimCondsHold(const ControllerDef& def, const AnimTransitionDef& t,
                   const float* params);

class ControllerTable {
public:
    /// 登记一个 controller（id=0 / 空 states = 拒绝；重复 id = 覆盖后者胜）
    bool Add(uint32_t controllerId, ControllerDef&& def);
    /// id=0 / 未登记 → nullptr（调用方旁路图评估，实体走纯指令层）
    const ControllerDef* Find(uint32_t controllerId) const;
    void Clear() { controllers_.clear(); }
    uint32_t Count() const { return (uint32_t)controllers_.size(); }

private:
    std::unordered_map<uint32_t, ControllerDef> controllers_;
};

} // namespace lemon::ecs
