// Lemon 引擎 — 世界空间表现通道实现（M6a 批①；头文件注释为语义权威；
// M7c 批① S2/S3 扩展见各段注记）
#include "ECS/FxChannel.h"

#include <algorithm>
#include <cstring>

namespace lemon::ecs {

void FxChannel::PopupText(const char* text, float x, float y, uint32_t color) {
    PopupTextEx(text, x, y, color, 1.0f, 0.0f, 0.0f, FxCurve::Linear);
}

void FxChannel::PopupTextEx(const char* text, float x, float y, uint32_t color, float scale,
                            float life, float driftX, FxCurve curve) {
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
    t.scale = scale > 0.0f ? scale : 1.0f;
    t.life = life > 0.0f ? life : 0.0f; // 0 = 默认（TextLifeOf 归一）
    t.driftX = driftX;
    t.curve = curve;
    textHead_ = (textHead_ + 1) % kMaxTexts;
}

void FxChannel::Bar(uint64_t entity, float frac, uint32_t color, float width) {
    BarEx(entity, frac, color, width, 0, 0, 0, 0.0f);
}

void FxChannel::BarEx(uint64_t entity, float frac, uint32_t color, float width,
                      uint32_t bgSpriteId, uint32_t fgSpriteId, uint32_t lagColor,
                      float height, float anchorDy) {
    if (entity == 0) return;
    frac = frac < 0.0f ? 0.0f : (frac > 1.0f ? 1.0f : frac);
    for (FxBar& b : bars_) {
        if (b.entity == entity) { // 命中刷新（受击续命 / 常显条每帧刷新）
            b.frac = frac;
            b.color = color;
            b.width = width;
            b.age = 0.0f;
            // 贴图/延迟条皮肤变更（含开关切换）：lagFrac 对齐 frac——旧皮肤残值
            // 闪现一帧是脏表现
            if (b.bgSpriteId != bgSpriteId || b.fgSpriteId != fgSpriteId ||
                b.lagColor != lagColor)
                b.lagFrac = frac;
            b.bgSpriteId = bgSpriteId;
            b.fgSpriteId = fgSpriteId;
            b.lagColor = lagColor;
            b.height = height;
            b.anchorDy = anchorDy;
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
    slot->frac = frac;
    slot->color = color;
    slot->width = width;
    slot->age = 0.0f;
    slot->bgSpriteId = bgSpriteId;
    slot->fgSpriteId = fgSpriteId;
    slot->lagColor = lagColor;
    slot->height = height;
    slot->anchorDy = anchorDy;
    slot->lagFrac = frac; // 出生无假延迟（延迟条从首次掉血开始表演）
}

void FxChannel::Simulate(float dt) {
    // 飘字：逐条 age 递增；到期就地置空文本（立即隐形——寿命逐条化后过期序
    // 不再恒等于插入序，长寿命条目会阻塞队首计数回收，纯延迟回收无害但过期
    // 必须当场不可见）；队首 while 只做环形计数回收（保尾部连续性）
    for (uint32_t i = 0; i < textCount_; ++i) {
        FxText& t = texts_[(textHead_ + kMaxTexts - textCount_ + i) % kMaxTexts];
        t.age += dt;
        if (t.age >= TextLifeOf(t)) t.text[0] = '\0';
    }
    while (textCount_ > 0) {
        const FxText& head = texts_[(textHead_ + kMaxTexts - textCount_) % kMaxTexts];
        if (head.age < TextLifeOf(head)) break;
        --textCount_;
    }
    // 血条：逐槽递增，到期腾槽（保序 = 键控数组稳定）；延迟条线性收敛（S2）
    for (FxBar& b : bars_) {
        if (b.entity == 0) continue;
        b.age += dt;
        if (b.lagColor) {
            if (b.lagFrac > b.frac) // 掉血追赶；回升（lag<frac）贴平 = fg 全盖不可见
                b.lagFrac = std::max(b.frac, b.lagFrac - kLagCatch * dt);
            else
                b.lagFrac = b.frac;
        }
        if (b.age >= kBarSticky) {
            b = FxBar{};
            --barCount_;
        }
    }
}

void FxChannel::TextMotion(const FxText& t, float& outDx, float& outDy, float& outScale,
                           float& outAlpha) {
    const float life = TextLifeOf(t);
    const float k = std::clamp(t.age / life, 0.0f, 1.0f);
    if (t.curve == FxCurve::Pop) {
        // 更陡上浮（ease-out：前段快后段缓）+ 出生 1.4× 回落 1.0（平方缓出）。
        // 世界 Y 向下（Camera2D 直映射），上浮 = -y
        outDy = -kTextRise * (1.0f - (1.0f - k) * (1.0f - k));
        const float w = 1.0f - k / kPopScaleWindow;
        outScale = t.scale * (1.0f + (w > 0.0f ? kPopScaleBoost * w * w : 0.0f));
    } else {
        outDy = -(k < 0.7f ? k / 0.7f : 1.0f) * kTextRise; // 开局 70% 匀升（-y = 上浮）
        outScale = t.scale;
    }
    outDx = t.driftX * t.age; // 恒速漂移
    outAlpha = k < 0.7f ? 1.0f : 1.0f - (k - 0.7f) / 0.3f; // 末 30% 线性淡出（现状）
}

uint32_t FxChannel::ExtractBarQuads(FxQuad* out, uint32_t maxOut,
                                    const std::function<bool(uint64_t, Vec2&, float&)>& resolve,
                                    Rect view, float margin) const {
    uint32_t n = 0;
    for (const FxBar& b : bars_) {
        if (b.entity == 0) continue;
        const float h = b.height > 0.0f ? b.height : kBarHeight;
        const bool lag = b.lagColor != 0 && b.lagFrac > b.frac + 1e-4f;
        if (n + (lag ? 3u : 2u) > maxOut) continue;
        Vec2 pos{};
        float top = 0.0f; // 精灵头顶高度（resolve 填；未知 = 0）
        if (!resolve(b.entity, pos, top)) continue; // 悬空实体：跳过（等 sticky 过期）
        if (pos.x < view.min.x - margin || pos.x > view.max.x + margin ||
            pos.y < view.min.y - margin || pos.y > view.max.y + margin)
            continue;
        // 条中心：精灵头顶上方 2px。世界 Y 向下（Camera2D "Y 向下直映射"）：头顶
        // = 中心位 - 半高；resolve 的 top = 精灵全高 × scale.y，故先取半。
        // anchorDy = 帧透明边距下压修正（0 = 现状语义不变）
        const float cy = pos.y - top * 0.5f - h * 0.5f - 2.0f + b.anchorDy;
        // bg 整宽（贴图 = 全幅；白精灵 = 暗色染色）
        out[n++] = {Vec2{pos.x, cy}, Vec2{b.width, h},
                    b.bgSpriteId ? 0xFFFFFFFFu : kBarBgColor, b.bgSpriteId, 1.0f};
        // 延迟条（lagFrac 宽、左锚；贴图形态吃 fg 图裁剪 + lagColor 染色）
        if (lag)
            out[n++] = {Vec2{pos.x - b.width * 0.5f * (1.0f - b.lagFrac), cy},
                        Vec2{b.width * b.lagFrac, h}, b.lagColor, b.fgSpriteId, b.lagFrac};
        // fg 比例宽左锚（贴图形态横向 UV 裁剪——缩放会把图片压扁，uFrac 语义防）
        out[n++] = {Vec2{pos.x - b.width * 0.5f * (1.0f - b.frac), cy},
                    Vec2{b.width * b.frac, h}, b.color, b.fgSpriteId, b.frac};
    }
    return n;
}

} // namespace lemon::ecs
