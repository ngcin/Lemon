// Lemon 引擎 — 帧动画 clip 表（M5 批③；03 §5 帧映射 / 06 §2.2 clip2d 数据通道）
// clipId → {fps, loop, frames[]}：纯 spriteId 数组，零 GPU 依赖——World 持有，
// 编辑器 EnterPlay 一次性建表（进 Play 时刻资产快照，Play 中改 .clip 不生效），
// 引擎测试/玩法层可直接填。帧号 = 纯函数（time*fps 截断），无逐帧累加状态机
// ——确定性回放不引入次序敏感状态。
// clipId 语义 = clip 资产 GUID 低 32 位（prefabId 同款映射约定；M7 dense id 表
// 同语义替换）。表只 Find 不遍历：unordered_map 迭代序不进任何确定路径。
#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace lemon::ecs {

struct ClipDef {
    float fps = 8.0f;        // 帧率（帧时长 = 1/fps，全片统一；per-frame 时长归 M6）
    bool loop = true;        // 档面默认；实体上 Animator2D.loop 为权威（热调参）
    std::vector<uint32_t> frames; // 每帧 spriteId（编辑器解析期由 (sheet guid, cell) 解析）
};

class ClipTable {
public:
    /// 登记一个 clip（clipId=0 或空帧表 = 拒绝）。重复 id = 覆盖（后者胜，编辑器
    /// 建表对低 32 位碰撞另有去重告警）。
    bool Add(uint32_t clipId, std::vector<uint32_t> frames, float fps, bool loop);
    /// clipId=0 / 未登记 → nullptr（调用方回退 M2 无 clip 路径，逐位不变）
    const ClipDef* Find(uint32_t clipId) const;
    void Clear() { clips_.clear(); }
    uint32_t Count() const { return (uint32_t)clips_.size(); }

private:
    std::unordered_map<uint32_t, ClipDef> clips_;
};

} // namespace lemon::ecs
