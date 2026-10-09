// Lemon 引擎 — Play 表现层段装配实现（M7a 批④；见 GameFx.h 契约注记）
#include "Renderer/GameFx.h"

#include <algorithm>

#include "Components/CoreComponents.h" // Transform2D
#include "Components/RenderComponents.h" // SpriteRenderer（血条贴头顶锚定）
#include "ECS/FxChannel.h"
#include "ECS/Scene.h"
#include "Renderer/Atlas.h"
#include "Renderer/BitmapFont.h"
#include "Renderer/SpriteTypes.h" // kPktUvOverride

namespace lemon::renderer {

void AppendGameFx(ecs::FxChannel& fx, ecs::Scene& scene, const Rect& view,
                  uint32_t whiteSprite, const BitmapFont& font,
                  const AtlasRegistry& atlas, float fxDt,
                  std::vector<SpritePacket>& bars, std::vector<SpritePacket>& texts) {
    fx.Simulate(fxDt);
    // 血条：实体位锚定 + 精灵头顶自动贴附（outTop = heightPx × scale.y——大精灵
    // 场景条画头顶而非身体中段；无精灵/未知 = 0 = M6a 小精灵语义），FxQuad →
    // 精灵包（贴图形态带横向 UV 裁剪——比例缩放会压扁图片，裁剪语义见 FxChannel.h）
    constexpr uint32_t kFxBarLayer = 251;
    auto resolve = [&](uint64_t id, Vec2& out, float& outTop) {
        const ecs::Entity e{id /* 低 32 位 = entt+1 */};
        const ecs::Transform2D* tf = scene.TryGet<ecs::Transform2D>(e);
        if (!tf) return false;
        out = tf->pos;
        outTop = 0.0f;
        if (const auto* sr = scene.TryGet<ecs::SpriteRenderer>(e))
            if (atlas.IsValidSprite(sr->spriteId))
                outTop = (float)atlas.GetSprite(sr->spriteId).heightPx * tf->scale.y;
        return true;
    };
    ecs::FxQuad quads[ecs::FxChannel::kMaxBars * 3];
    const uint32_t qn = fx.ExtractBarQuads(quads, (uint32_t)std::size(quads), resolve,
                                           view, 64.0f);
    bars.reserve(bars.size() + qn);
    for (uint32_t i = 0; i < qn; ++i) {
        ecs::FxQuad q = quads[i];
        // 头顶锚定 + 大精灵 = 条中心可能越出视口上沿（头顶在画外）——钉回视口
        // 内（MOBA 式贴屏上沿；正常位置不动）。只夹 y：条在实体上方，x 由锚定
        // 保证。世界 Y 向下：上沿 = view.min.y
        const float halfH = q.size.y * 0.5f;
        q.center.y = std::max(q.center.y, view.min.y + halfH + 1.0f);
        SpritePacket p{};
        p.spriteId = q.spriteId ? q.spriteId : whiteSprite;
        p.colorBits = q.color;
        p.posX = q.center.x;
        p.posY = q.center.y;
        p.rot = 0.0f;
        p.scaleX = q.size.x; // scale = 世界像素边长（overlay 同约定）
        p.scaleY = q.size.y;
        p.key = MakeBatchKey(0, BlendKind::Alpha, FilterKind::Linear, kFxBarLayer);
        if (q.spriteId) {
            // 贴图形态：批键挂图集槽 + 右缘按 uFrac 裁剪（白精灵路径键保持 0 槽
            // ——两形态同层并存，批键天然分批）
            const SpriteInfo& si = atlas.GetSprite(q.spriteId);
            p.key.textureAtlas = si.atlasIndex;
            if (q.uFrac < 1.0f) {
                p.uv0u = si.u0;
                p.uv0v = si.v0;
                p.uv1u = si.u0 + (si.u1 - si.u0) * q.uFrac;
                p.uv1v = si.v1;
                p.flags = kPktUvOverride;
            }
        }
        p.sortKey = ((uint64_t)kFxBarLayer << 56) | ((uint64_t)(i & 0xFFFF) << 40);
        bars.push_back(p);
    }
    // 飘字：字体页层 252（左上锚 + 曲线上浮/漂移/缩放；末 30% 淡出乘进 alpha）。
    // 行顶越出视口上沿时钉回（血条贴头顶 + 大字号缩放后行高可观——防"锚点在
    // 画外整行消失"；正常位置不动）。世界 Y 向下：上沿 = view.min.y，行自锚点
    // 向下延展（baseline = 锚点 + ascender），夹住行顶即整行入画
    constexpr uint32_t kFxTextLayer = 252;
    const float margin = 64.0f;
    for (uint32_t i = 0; i < fx.TextCount(); ++i) {
        const ecs::FxText& t = fx.TextAt(i);
        float dx, dy, scale, alpha;
        ecs::FxChannel::TextMotion(t, dx, dy, scale, alpha);
        if (alpha <= 0.0f || !t.text[0]) continue; // 过期隐形（Simulate 置空）
        if (t.x < view.min.x - margin || t.x > view.max.x + margin ||
            t.y < view.min.y - margin || t.y > view.max.y + margin)
            continue;
        float drawY = t.y + dy;
        if (drawY < view.min.y) drawY = view.min.y;
        const uint32_t color = (t.color & 0x00FFFFFFu) |
                               ((uint32_t)(uint8_t)(alpha * 255.0f) << 24);
        font.DrawTextEx(texts, t.text, Vec2{t.x + dx, drawY}, scale, color,
                        (uint8_t)kFxTextLayer);
    }
}

} // namespace lemon::renderer
