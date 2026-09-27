// Lemon 引擎 — 帧动画 clip 表（M5 批③；03 §5 帧映射 / 06 §2.2 clip2d 数据通道）
// clipId → {fps, loop, frames[]}：纯 spriteId 数组，零 GPU 依赖——World 持有，
// 编辑器 EnterPlay 一次性建表（进 Play 时刻资产快照，Play 中改 .anim 不生效），
// 引擎测试/玩法层可直接填。帧号 = 纯函数（time*fps 截断），无逐帧累加状态机
// ——确定性回放不引入次序敏感状态。
// clipId 语义 = clip 资产 GUID 低 32 位（prefabId 同款映射约定；M7 dense id 表
// 同语义替换）。表只 Find 不遍历：unordered_map 迭代序不进任何确定路径。
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace lemon::ecs {

/// T3d 批③：帧事件（.anim events[]；id = 作者自定义 16 位标识，C# 侧按 (clipId,id)
/// 分发——命名事件列 v1.1）。帧跨越检测在 AnimatorSystem（帧号纯函数 ⇒ 事件序确定）。
struct ClipEventDef {
    uint16_t frame = 0;
    uint16_t id = 0;
    friend bool operator==(const ClipEventDef&, const ClipEventDef&) = default;
};

struct ClipDef {
    float fps = 8.0f;        // 帧率（帧时长 = 1/fps，全片统一；per-frame 时长归 M6）
    bool loop = true;        // 档面默认；实体上 Animator2D.loop 为权威（热调参）
    std::vector<uint32_t> frames; // 每帧 spriteId（编辑器解析期由 (sheet guid, cell) 解析）
    std::vector<ClipEventDef> events; // 帧事件表（按帧升序约定；空 = 无事件）
};

class ClipTable {
public:
    /// 登记一个 clip（clipId=0 或空帧表 = 拒绝）。重复 id = 覆盖（后者胜，编辑器
    /// 建表对低 32 位碰撞另有去重告警）。
    bool Add(uint32_t clipId, std::vector<uint32_t> frames, float fps, bool loop);
    /// T3d 批③：带帧事件表版本（events 按帧升序约定，评估侧线性扫过当帧命中）。
    bool Add(uint32_t clipId, std::vector<uint32_t> frames, float fps, bool loop,
             std::vector<ClipEventDef> events);
    /// clipId=0 / 未登记 → nullptr（调用方回退 M2 无 clip 路径，逐位不变）
    const ClipDef* Find(uint32_t clipId) const;
    void Clear() { clips_.clear(); setsByName_.clear(); setOfSeg_.clear(); }
    uint32_t Count() const { return (uint32_t)clips_.size(); }

    // ---- T3c 动画集按名索引（.override；World 持有非 ECS 零哈希——不入 StateHash，
    // 基准场不走按名路径 = 金回放逐位不变，09 §6.8 同款口径）----
    /// 登记集（setId = 集资产 GUID 低 32 位；segments = {段名, clipId} 有序对）。
    /// 同集重名先到先得（编辑器按 relPath 序登记并预检告警）；返回实登记数。
    size_t RegisterSet(uint32_t setId,
                       const std::vector<std::pair<std::string, uint32_t>>& segments);
    /// 集内按名解析（0 = 无此段/无此集）。Godot SpriteFrames / Unity Animator
    /// 同款集合内局部语义：跨集同名互不干扰，无全局扁平名单。
    uint32_t FindByName(uint32_t setId, const char* name) const;
    /// 段 → 所属集（0 = 不属任何集；一段入多集 = 先登记者胜）
    uint32_t SetOfClip(uint32_t clipId) const;
    /// T3d 批②：集内反查 clipId → 段名（AnimGraphSystem 当前状态反推——当前状态
    /// = 换段消费后的 clipId 经集绑定反查，无逐帧累加图状态，回放确定）。同一
    /// clip 被集内多段名引用时 RegisterSet 已按输入序去重（首个段名胜，确定性）。
    /// 返回 nullptr = 不在该集（未绑集/悬空/裸 clip）。
    const std::string* NameOfClip(uint32_t setId, uint32_t clipId) const;

private:
    std::unordered_map<uint32_t, ClipDef> clips_;
    std::unordered_map<uint32_t, std::unordered_map<std::string, uint32_t>> setsByName_;
    std::unordered_map<uint32_t, uint32_t> setOfSeg_;
};

} // namespace lemon::ecs
