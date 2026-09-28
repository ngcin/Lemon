// Lemon 引擎 — 运行时属性补间表实现（A 档 tween；头说明与批文件
// 2026-09-28-b2-tween-a-runtime.md 为语义权威）
#include "ECS/TweenTable.h"

#include <cmath>
#include <cstring>

#include "Core/Math.h"
#include "ECS/ComponentRegistry.h"
#include "ECS/Events.h"
#include "ECS/Scene.h"
#include "ECS/World.h"

namespace lemon::ecs {

namespace {

constexpr float kPi = 3.14159265358979323846f;

// 缓动（标准公式；t 入参已钳 [0,1]，输出可过冲——OutBack/Elastic 是手感来源）
constexpr float kBackC1 = 1.70158f;
constexpr float kBackC3 = kBackC1 + 1.0f;

float EaseOutBounce(float t) {
    const float n1 = 7.5625f, d1 = 2.75f;
    if (t < 1.0f / d1) return n1 * t * t;
    if (t < 2.0f / d1) {
        const float u = t - 1.5f / d1;
        return n1 * u * u + 0.75f;
    }
    if (t < 2.5f / d1) {
        const float u = t - 2.25f / d1;
        return n1 * u * u + 0.9375f;
    }
    const float u = t - 2.625f / d1;
    return n1 * u * u + 0.984375f;
}

/// 按 kind 把插值结果写进组件字段（getFn 可写取址直写——低频指令、量 ≤ 百级，
/// 不走整组件镜像往返）
void ApplyValue(const TweenEntry& en, void* comp, float k) {
    uint8_t* base = static_cast<uint8_t*>(comp) + en.offset;
    switch (en.kind) {
    case TweenValKind::Float:
        *reinterpret_cast<float*>(base) = math::Lerp(en.from[0], en.to[0], k);
        break;
    case TweenValKind::Vec2: {
        Vec2* v = reinterpret_cast<Vec2*>(base);
        v->x = math::Lerp(en.from[0], en.to[0], k);
        v->y = math::Lerp(en.from[1], en.to[1], k);
        break;
    }
    case TweenValKind::ColorRGBA: {
        // 四字节颜色通道逐个插值（通道序原样保持——RGBA/ABGR 布局对插值透明）
        uint32_t packed = 0;
        for (int i = 0; i < 4; ++i) {
            uint32_t ch = (uint32_t)(math::Lerp(en.from[i], en.to[i], k) + 0.5f);
            if (ch > 255u) ch = 255u;
            packed |= ch << (8 * i);
        }
        *reinterpret_cast<uint32_t*>(base) = packed;
        break;
    }
    }
}

} // namespace

float TweenEaseAt(TweenEase e, float t) {
    switch (e) {
    case TweenEase::Linear: return t;
    case TweenEase::InQuad: return t * t;
    case TweenEase::OutQuad: return t * (2.0f - t);
    case TweenEase::InOutQuad: return t < 0.5f ? 2.0f * t * t : -1.0f + (4.0f - 2.0f * t) * t;
    case TweenEase::OutCubic: {
        const float u = t - 1.0f;
        return 1.0f + u * u * u;
    }
    case TweenEase::InOutCubic: return t < 0.5f ? 4.0f * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) / 2.0f;
    case TweenEase::OutBack: {
        const float u = t - 1.0f;
        return 1.0f + kBackC3 * u * u * u + kBackC1 * u * u;
    }
    case TweenEase::OutElastic:
        if (t <= 0.0f) return 0.0f;
        if (t >= 1.0f) return 1.0f;
        return std::pow(2.0f, -10.0f * t) * std::sin((t * 10.0f - 0.75f) * (2.0f * kPi / 3.0f)) + 1.0f;
    case TweenEase::OutBounce: return EaseOutBounce(t);
    }
    return t;
}

uint64_t TweenTable::Create(Scene& scene, Entity e, uint16_t compId, const char* field,
                            const float to[4], float duration, TweenEase ease,
                            TweenMode mode) {
    auto& reg = ComponentRegistry::Instance();
    if (compId >= reg.Count() || !field || !*field) return 0;
    const ComponentMeta& m = reg.At(compId);
    const void* cur = m.readFn ? m.readFn(scene, e) : nullptr;
    if (!cur) return 0; // 实体亡/组件缺
    const FieldMeta* f = nullptr;
    for (uint16_t i = 0; i < m.fieldCount; ++i)
        if (std::strcmp(m.fields[i].name, field) == 0) {
            f = &m.fields[i];
            break;
        }
    if (!f) return 0; // 字段名未命中
    TweenValKind kind;
    switch (f->type) {
    case FieldType::Float: kind = TweenValKind::Float; break;
    case FieldType::Vec2: kind = TweenValKind::Vec2; break;
    case FieldType::UInt32: kind = TweenValKind::ColorRGBA; break;
    default: return 0; // 白名单外（作者错误：调桥侧 warn-once）
    }
    KillField(e, compId, field); // 顶替：新指令接掌字段（存活 tween 拥有权转移）
    TweenEntry en{};
    en.handle = nextHandle_++;
    en.entity = e;
    en.compId = compId;
    en.offset = f->offset;
    en.kind = kind;
    en.ease = ease <= TweenEase::OutBounce ? ease : TweenEase::Linear; // ABI 防御钳
    en.mode = mode == TweenMode::Yoyo ? TweenMode::Yoyo : TweenMode::Once;
    en.duration = duration < 0.0f ? 0.0f : duration; // <0 视作 0（建立即完成）
    const uint8_t* base = static_cast<const uint8_t*>(cur) + f->offset;
    switch (kind) {
    case TweenValKind::Float:
        en.from[0] = *reinterpret_cast<const float*>(base);
        break;
    case TweenValKind::Vec2: {
        const Vec2* v = reinterpret_cast<const Vec2*>(base);
        en.from[0] = v->x;
        en.from[1] = v->y;
        break;
    }
    case TweenValKind::ColorRGBA: {
        const uint32_t packed = *reinterpret_cast<const uint32_t*>(base);
        for (int i = 0; i < 4; ++i) en.from[i] = (float)((packed >> (8 * i)) & 0xFFu);
        break;
    }
    }
    for (int i = 0; i < 4; ++i) en.to[i] = to[i];
    active_.push_back(en);
    return en.handle;
}

int32_t TweenTable::KillField(Entity e, uint16_t compId, const char* field) {
    // 名 → 偏移（与 Create 同一注册表口径；field 空 = 该组件全部字段）
    const bool all = (field == nullptr || *field == '\0');
    uint16_t offset = 0;
    if (!all) {
        auto& reg = ComponentRegistry::Instance();
        if (compId >= reg.Count()) return 0;
        const ComponentMeta& m = reg.At(compId);
        const FieldMeta* f = nullptr;
        for (uint16_t i = 0; i < m.fieldCount; ++i)
            if (std::strcmp(m.fields[i].name, field) == 0) {
                f = &m.fields[i];
                break;
            }
        if (!f) return 0;
        offset = f->offset;
    }
    int32_t removed = 0;
    for (size_t i = active_.size(); i-- > 0;) {
        const TweenEntry& en = active_[i];
        if (en.entity != e || en.compId != compId) continue;
        if (!all && en.offset != offset) continue;
        active_.erase(active_.begin() + (long)i);
        ++removed;
    }
    return removed;
}

int32_t TweenTable::KillEntity(Entity e) {
    int32_t removed = 0;
    for (size_t i = active_.size(); i-- > 0;) {
        if (active_[i].entity == e) {
            active_.erase(active_.begin() + (long)i);
            ++removed;
        }
    }
    return removed;
}

bool TweenTable::Alive(uint64_t handle) const {
    for (const TweenEntry& en : active_)
        if (en.handle == handle) return true;
    return false;
}

void TweenTable::Advance(World& world, Scene& scene, float dt) {
    if (active_.empty()) return;
    auto& reg = ComponentRegistry::Instance();
    // 单趟推进 + 存活拷贝到 scratch 收敛（迭代中不删不重建；事件只入队，
    // C# 回调在 ScriptEventDispatch 后段才可能 Create/Kill 触碰本表——彼时
    // Advance 已返回，无重入）
    scratch_.clear();
    for (const TweenEntry& en : active_) {
        if (!scene.Alive(en.entity)) continue; // 实体死亡 = 正常路径，静默自清
        const ComponentMeta& m = reg.At(en.compId);
        void* comp = m.getFn ? m.getFn(scene, en.entity) : nullptr;
        if (!comp) continue; // 组件被摘
        TweenEntry cur = en; // 应用与推进在副本上，末尾统一回填 scratch
        cur.elapsed += dt;
        const float raw = cur.duration > 0.0f ? cur.elapsed / cur.duration : 1.0f;
        float t;
        bool done = false;
        if (cur.mode == TweenMode::Once) {
            t = raw >= 1.0f ? 1.0f : raw;
            done = raw >= 1.0f;
        } else {
            // Yoyo 三角波：0→1→0 永续（raw 恒 ≥0；2 的模保 [0,2)）
            float phase = raw - std::floor(raw * 0.5f) * 2.0f;
            t = phase < 1.0f ? phase : 2.0f - phase;
        }
        ApplyValue(cur, comp, TweenEaseAt(cur.ease, t));
        if (done) {
            EventPacket fin{};
            fin.type = GameEvent::TweenFinished;
            fin.src = cur.entity;
            fin.userArg = cur.handle;
            world.Events().Push(fin);
            continue; // 完成项不回填
        }
        scratch_.push_back(cur);
    }
    active_.swap(scratch_);
}

} // namespace lemon::ecs
