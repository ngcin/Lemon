// Lemon 引擎 — 运行时属性补间表（A 档 tween；03 §8.3 / 批文件 2026-09-28-b2-tween-a-runtime）
// World 持有非 ECS 通道（03 §13 纪律：通道族新增机制不动组件注册表）——不入
// StateHash：补间是指令不是模拟态，效果经组件字段（本就入哈希）可验漂移；
// 基准场零调用 = 零漂移（TweenSystem 空表早退）。EnterPlay 新建 World 自清零。
// 语义（详见分册与批文件）：
//   * 存活 tween 拥有字段：同实体同字段新建 = 顶替；Kill/Once 完成后归还脚本；
//   * 推进 = elapsed 累计 + 缓动纯函数采样（Animator time 同款，回放确定）；
//   * dt 缩放先于系统（timeScale=0 冻结）；duration<=0 = 建立当 tick 即完成；
//   * 字段寻址 = FieldMeta 按名（Inspector/序列化同一元数据），建时一次解析
//     存 offset+kind，逐 tick 经 ComponentMeta::getFn 可写取址直写；
//   * 类型白名单：Float / Vec2 / UInt32（按四字节颜色通道 0..255 插值——
//     现目录唯一值得补间的 UInt32 = SpriteRenderer.colorRGBA；spriteId 字节
//     插值无意义，作者自慎）。其余类型拒建（返 0）。
// 句柄 = 单调 u64 不回收（Alive 线性查——低频轮询 + 条目量 ≤ 百级，不做代际槽）。
#pragma once

#include <cstdint>
#include <vector>

#include "ECS/Entity.h"

namespace lemon::ecs {

class Scene;
class World;

/// 缓动函数编号（纯函数；C# Tween.Ease 镜像同值——两侧同步改）
enum class TweenEase : uint8_t {
    Linear = 0,
    InQuad,
    OutQuad,
    InOutQuad,
    OutCubic,
    InOutCubic,
    OutBack,
    OutElastic,
    OutBounce,
};

/// 播放模式：Once 单程到终值即完 / Yoyo 三角波永续（Kill 停）
enum class TweenMode : uint8_t { Once = 0, Yoyo };

/// 值形（按 FieldMeta 类型解析；ColorRGBA 时 from/to 存 0..255 字节通道）
enum class TweenValKind : uint8_t { Float = 0, Vec2, ColorRGBA };

/// 缓动核心：t ∈ [0,1] → [0,1]（可过冲：OutBack/OutElastic >1）
float TweenEaseAt(TweenEase e, float t);

struct TweenEntry {
    uint64_t handle = 0; // 单调句柄（Create 返回；Alive 轮询/TweenFinished userArg
    Entity entity{};
    uint16_t compId = 0;
    uint16_t offset = 0;   // 字段偏移（建时 FieldMeta 解析，逐 tick 直写）
    TweenValKind kind = TweenValKind::Float;
    TweenEase ease = TweenEase::Linear;
    TweenMode mode = TweenMode::Once;
    float from[4] = {};    // ColorRGBA = R,G,B,A 字节通道（0..255）
    float to[4] = {};      // 同上
    float elapsed = 0.0f;  // 秒（dt 缩放后累计）
    float duration = 0.0f; // 秒（<=0 = 即完成）
};

class TweenTable {
public:
    /// 建 tween（捕获字段现值为 from；同实体同字段已存活 = 顶替）。
    /// 返回句柄；0 = 失败（组件缺/字段名未命中/类型不可插值——作者错误，
    /// 调用桥 warn-once 后降级，区别于实体亡的静默丢弃）。
    uint64_t Create(Scene& scene, Entity e, uint16_t compId, const char* field,
                    const float to[4], float duration, TweenEase ease,
                    TweenMode mode);
    /// 删同实体同字段的存活 tween（返回移除数；field 为 nullptr = 该组件全部。
    /// 纯身份匹配不触场景——Create 顶替路径复用）
    int32_t KillField(Entity e, uint16_t compId, const char* field);
    /// 删该实体全部 tween（返回移除数；死亡时 Advance 自清之外的显式通道）
    int32_t KillEntity(Entity e);
    bool Alive(uint64_t handle) const;

    void Clear() {
        active_.clear();
        nextHandle_ = 1;
    }
    uint32_t Count() const { return (uint32_t)active_.size(); }

    /// 推进 + 应用 + 完成事件入队（TweenSystem 唯一消费者；空表 = 零成本）。
    /// 实体亡/组件被摘 = 条目自清（不告警——实体死亡是正常路径）。
    void Advance(World& world, Scene& scene, float dt);

private:
    std::vector<TweenEntry> active_;
    std::vector<TweenEntry> scratch_; // Advance 收敛复用（稳态零分配）
    uint64_t nextHandle_ = 1;
};

} // namespace lemon::ecs
