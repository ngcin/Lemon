// Lemon 引擎 — Play 表现层段装配实现（M7a 批④；见 GameFx.h 契约注记）
#include "Renderer/GameFx.h"

#include <algorithm>

#include "Components/CoreComponents.h" // Transform2D
#include "ECS/FxChannel.h"
#include "ECS/Scene.h"
#include "Renderer/BitmapFont.h"

namespace lemon::renderer {

void AppendGameFx(ecs::FxChannel& fx, ecs::Scene& scene, const Rect& view,
                  uint32_t whiteSprite, const BitmapFont& font, float fxDt,
                  std::vector<SpritePacket>& bars, std::vector<SpritePacket>& texts) {
    fx.Simulate(fxDt);
    // 血条：实体位锚定（悬空/出视口由通道过滤），FxEQuad → 白精灵包
    constexpr uint32_t kFxBarLayer = 251;
    auto resolve = [&](uint64_t id, Vec2& out) {
        const ecs::Entity e{id /* 低 32 位 = entt+1 */};
        const ecs::Transform2D* tf = scene.TryGet<ecs::Transform2D>(e);
        if (!tf) return false;
        out = tf->pos;
        return true;
    };
    ecs::FxQuad quads[ecs::FxChannel::kMaxBars * 2];
    const uint32_t qn = fx.ExtractBarQuads(quads, (uint32_t)std::size(quads), resolve,
                                           view, 64.0f);
    bars.reserve(bars.size() + qn);
    for (uint32_t i = 0; i < qn; ++i) {
        SpritePacket p{};
        p.spriteId = whiteSprite;
        p.colorBits = quads[i].color;
        p.posX = quads[i].center.x;
        p.posY = quads[i].center.y;
        p.rot = 0.0f;
        p.scaleX = quads[i].size.x; // scale = 世界像素边长（overlay 同约定）
        p.scaleY = quads[i].size.y;
        p.key = MakeBatchKey(0, BlendKind::Alpha, FilterKind::Linear, kFxBarLayer);
        p.sortKey = ((uint64_t)kFxBarLayer << 56) | ((uint64_t)(i & 0xFFFF) << 40);
        bars.push_back(p);
    }
    // 飘字：位图字体页层 252（左下锚 + 寿命上浮；末 30% 线性淡出乘进 alpha）
    constexpr uint32_t kFxTextLayer = 252;
    const float margin = 64.0f;
    for (uint32_t i = 0; i < fx.TextCount(); ++i) {
        const ecs::FxText& t = fx.TextAt(i);
        if (t.x < view.min.x - margin || t.x > view.max.x + margin ||
            t.y < view.min.y - margin || t.y > view.max.y + margin)
            continue;
        const float alpha = fx.TextAlpha(t);
        const uint32_t color = (t.color & 0x00FFFFFFu) |
                               ((uint32_t)(uint8_t)(alpha * 255.0f) << 24);
        font.DrawText(texts, t.text,
                      Vec2{t.x, t.y + fx.TextRise(t)}, 1.0f, color,
                      (uint8_t)kFxTextLayer);
    }
}

} // namespace lemon::renderer
