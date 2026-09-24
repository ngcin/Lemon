// Lemon 引擎 — 世界空间表现通道实现（M6a 批①；头文件注释为语义权威）
#include "ECS/FxChannel.h"

#include <cstring>

namespace lemon::ecs {

void FxChannel::PopupText(const char* text, float x, float y, uint32_t color) {
    FxText& t = texts_[textHead_];
    if (textCount_ < kMaxTexts) ++textCount_; // 覆写最老槽时计数不变
    if (text) {
        std::memcpy(t.text, text, sizeof(t.text) - 1); // 截断留 NUL 位
        t.text[sizeof(t.text) - 1] = '\0';
    } else {
        t.text[0] = '\0';
    }
    t.x = x;
    t.y = y;
    t.color = color;
    t.age = 0.0f;
    textHead_ = (textHead_ + 1) % kMaxTexts;
}

void FxChannel::Bar(uint64_t entity, float frac, uint32_t color, float width) {
    if (entity == 0) return;
    for (FxBar& b : bars_) {
        if (b.entity == entity) { // 命中刷新（受击续命 / 常显条每帧刷新）
            b.frac = frac < 0.0f ? 0.0f : (frac > 1.0f ? 1.0f : frac);
            b.color = color;
            b.width = width;
            b.age = 0.0f;
            return;
        }
    }
    FxBar* slot = nullptr;
    for (FxBar& b : bars_) {
        if (b.entity == 0) {
            slot = &b;
            break;
        }
    }
    if (!slot) { // 池满：淘汰最旧（age 最大——最近最少刷新者）
        FxBar* oldest = &bars_[0];
        for (FxBar& b : bars_)
            if (b.age > oldest->age) oldest = &b;
        slot = oldest;
    } else {
        ++barCount_;
    }
    slot->entity = entity;
    slot->frac = frac < 0.0f ? 0.0f : (frac > 1.0f ? 1.0f : frac);
    slot->color = color;
    slot->width = width;
    slot->age = 0.0f;
}

void FxChannel::Simulate(float dt) {
    // 飘字：全场同龄速递增 → 过期序 = 插入序，从队首批量回收
    for (uint32_t i = 0; i < textCount_; ++i)
        texts_[(textHead_ + kMaxTexts - textCount_ + i) % kMaxTexts].age += dt;
    while (textCount_ > 0 &&
           texts_[(textHead_ + kMaxTexts - textCount_) % kMaxTexts].age >= kTextLife)
        --textCount_;
    // 血条：逐槽递增，到期腾槽（保序 = 键控数组稳定）
    for (FxBar& b : bars_) {
        if (b.entity == 0) continue;
        b.age += dt;
        if (b.age >= kBarSticky) {
            b = FxBar{};
            --barCount_;
        }
    }
}

uint32_t FxChannel::ExtractBarQuads(FxQuad* out, uint32_t maxOut,
                                    const std::function<bool(uint64_t, Vec2&)>& resolve,
                                    Rect view, float margin) const {
    uint32_t n = 0;
    for (const FxBar& b : bars_) {
        if (b.entity == 0 || n + 2 > maxOut) continue;
        Vec2 pos{};
        if (!resolve(b.entity, pos)) continue; // 悬空实体：跳过（等 sticky 过期）
        if (pos.x < view.min.x - margin || pos.x > view.max.x + margin ||
            pos.y < view.min.y - margin || pos.y > view.max.y + margin)
            continue;
        const float cy = pos.y + kBarHeight * 0.5f + 2.0f; // 条中心：实体位上方 2px
        out[n++] = {Vec2{pos.x, cy}, Vec2{b.width, kBarHeight}, kBarBgColor};
        out[n++] = {Vec2{pos.x - b.width * 0.5f * (1.0f - b.frac), cy},
                    Vec2{b.width * b.frac, kBarHeight}, b.color}; // 左锚比例宽
    }
    return n;
}

} // namespace lemon::ecs
