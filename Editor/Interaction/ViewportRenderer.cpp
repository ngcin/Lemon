// Lemon 编辑器 — 视口渲染器实现（M4-Editor-Plan §2.2/§3.1；内核 #2/#4/#11/#13）
#include "Interaction/ViewportRenderer.h"

#include <cmath>
#include <cstring>

#include "App/ImGuiBackend.h"
#include "Components/CoreComponents.h"
#include "Components/RenderComponents.h"
#include "Core/Log.h"
#include "Core/Math.h"
#include "ECS/Hierarchy.h"
#include "EditorContext.h"

namespace lemon::editor {

using ecs::Entity;
using ecs::Scene;
static constexpr uint32_t kSrEnabled = 0x4; // SpriteRenderer.flags bit2
static constexpr uint32_t kSrFlipMask = 0x3;
static constexpr float kOverlayLayer = 63.0f; // overlay 排序层（最顶）

// ------------------------------------------------------------- 调色板页 ----
void ProceduralAtlas::Build(rhi::Device& device) {
    constexpr uint32_t kPageW = kPaletteSprites * kCellPx; // 512×64
    constexpr uint32_t kPageH = kCellPx;
    std::vector<uint8_t> px((size_t)kPageW * kPageH * 4, 0);
    // 0 白（overlay 染色底纹）/ 1 红 / 2 绿 / 3 蓝 / 4 柠檬黄 / 5 青 / 6 品红 / 7 棋盘
    static const uint8_t kPal[8][3] = {{255, 255, 255}, {235, 80, 80},  {90, 200, 90},
                                       {90, 120, 235},  {250, 220, 60}, {80, 220, 220},
                                       {220, 90, 220},  {200, 200, 200}};
    for (uint32_t s = 0; s < kPaletteSprites; ++s)
        for (uint32_t y = 0; y < kPageH; ++y)
            for (uint32_t x = 0; x < kCellPx; ++x) {
                uint8_t* p = &px[((size_t)y * kPageW + (size_t)s * kCellPx + x) * 4];
                if (s == 7) { // 棋盘 8px（点采样像素对齐验证）
                    bool cell = ((x / 8) + (y / 8)) % 2 == 0;
                    p[0] = p[1] = p[2] = cell ? 210 : 60;
                    p[3] = 255;
                    continue;
                }
                p[0] = kPal[s][0];
                p[1] = kPal[s][1];
                p[2] = kPal[s][2];
                p[3] = 255;
            }
    page_ = device.CreateTexture({.width = kPageW, .height = kPageH, .debugName = "editorPalette"});
    device.UploadTexture(page_, px.data(), px.size());
    device.BindTextureToSlot(page_, 0);
    atlas_.RegisterAtlas(0, page_, kPageW, kPageH);
    whiteId_ = atlas_.AddSprite(0, 0, 0, kCellPx, kCellPx); // 槽 0 白（overlay 底纹）
    for (uint32_t s = 1; s < kPaletteSprites; ++s)           // 彩色块依次登记
        atlas_.AddSprite(0, s * kCellPx, 0, kCellPx, kCellPx);

    font_.Init(device, atlas_, 1); // 字体页 → 图集槽 1（程序化 ASCII，anim-smoke 同款）

    linear_ = device.CreateSampler({});
    point_ = device.CreateSampler({.min = rhi::FilterMode::Point, .mag = rhi::FilterMode::Point});
    device.BindSamplerToSlot(linear_, 0);
    device.BindSamplerToSlot(point_, 1);
}

// ------------------------------------------------------------- 生命周期 ----
void ViewportRenderer::Init(rhi::Device& device, ImGuiBackend& ui) {
    device_ = &device;
    ui_ = &ui;
    assets_.Build(device);
    paletteIconTex_ = ui.RegisterViewportTexture(assets_.Page().id); // 图标源（M4.4）
    const rhi::Format rtFormat = rhi::Format::RGBA8Unorm; // 与 EnsureRenderTarget 一致
    sceneBatcher_.Init(device, 0, 1, rtFormat);
    gameBatcher_.Init(device, 0, 1, rtFormat);
    sceneCam_.center = {640, 360};
    sceneCam_.halfHeight = 360.0f;
    gameCam_.center = {640, 360};
    gameCam_.halfHeight = 360.0f;
    device.AddRecreateCallback("editor-viewport", [this](rhi::Device& d) {
        for (RT& rt : rts_) { // RT 已随资源表销毁；句柄失效，尺寸保留待下帧重建
            rt.tex = {};
            rt.imguiTexId = nullptr;
        }
        OnDeviceRecreated(d);
    });
}

void ViewportRenderer::OnDeviceRecreated(rhi::Device& device) {
    // 旧页登记清空（纹理已随设备丢失销毁；不 Reset 则 RegisterAtlas 槽位重用断言）
    assets_.Registry().Reset();
    assets_.Build(device); // 程序化页/字体页按原序重建 → spriteId 1..N 复原；
    // 导入页由 EditorApp 的 asset-gpu 回调按 DB 记账号升序重导入接续编号
    if (ui_) paletteIconTex_ = ui_->RegisterViewportTexture(assets_.Page().id);
    const rhi::Format rtFormat = rhi::Format::RGBA8Unorm;
    sceneBatcher_.Init(device, 0, 1, rtFormat); // 管线经磁盘缓存重建；实例环形缓冲重建
    gameBatcher_.Init(device, 0, 1, rtFormat);
}

void* ViewportRenderer::EnsureRenderTarget(uint32_t idx, uint32_t wantW, uint32_t wantH,
                                           const char* debugName) {
    RT& rt = rts_[idx];
    if (wantW < 8 || wantH < 8) return rt.imguiTexId; // 面板过小（折叠中）
    if (rt.tex.IsValid() && rt.w == wantW && rt.h == wantH) return rt.imguiTexId;
    if (rt.tex.IsValid() || rt.imguiTexId) {
        device_->WaitIdle(); // 尺寸变化低频；在途帧可能引用旧视图/描述符集（验证层实抓）
        if (rt.tex.IsValid()) device_->DestroyTexture(rt.tex);
        if (rt.imguiTexId) {
            ui_->UnregisterViewportTexture(rt.imguiTexId);
            rt.imguiTexId = nullptr;
        }
    }
    rt.w = wantW;
    rt.h = wantH;
    rt.tex = device_->CreateTexture({.width = wantW, .height = wantH,
                                     .renderTarget = true, .debugName = debugName});
    rt.imguiTexId = ui_->RegisterViewportTexture(rt.tex.id);
    return rt.imguiTexId;
}

// ---------------------------------------------------------------- 提取 ----
void ViewportRenderer::ExtractScene(EditorContext& ctx) {
    Scene& s = ctx.ActiveScene();

    // 内核 #4：场景对象变化（新建/打开/Play 切换 → Scene 指针不同）→ 映射全失效
    const uint64_t stamp = (uint64_t)(uintptr_t)&s;
    if (stamp != lastSceneStamp_) {
        for (auto& [id, rid] : entityToRenderable_) rm_.Destroy(rid);
        entityToRenderable_.clear();
        lastSceneStamp_ = stamp;
    }

    rm_.BeginSimTick();
    std::vector<uint64_t> seen;
    seen.reserve(entityToRenderable_.size() + 16);
    const uint32_t spriteIdCap = assets_.Registry().SpriteCount();
    for (auto [ent, tf, sr] : s.View<ecs::Transform2D, ecs::SpriteRenderer>().each()) {
        (void)tf;
        Entity e = Scene::FromEntt(ent);
        if (!(sr.flags & kSrEnabled)) continue;
        // 悬空引用（资产已删/未导入）：不建 renderable（Inspector 槽红显 + 体检红字；
        // GetSprite 越界断言的编辑器侧防线）
        if (sr.spriteId == 0 || sr.spriteId > spriteIdCap) continue;
        seen.push_back(e.id);

        // 世界变换（内核 #1 消费端；链异常回退本地，保持可渲染）
        ecs::WorldTransform2D wt{};
        if (!ecs::ComputeWorldTransform(s, e, wt)) {
            wt.pos = tf.pos;
            wt.rot = tf.rot;
            wt.scale = tf.scale;
        }
        uint32_t rid;
        auto it = entityToRenderable_.find(e.id);
        if (it == entityToRenderable_.end()) {
            rid = rm_.Create({.spriteId = sr.spriteId,
                              .colorBits = sr.colorRGBA,
                              .sortingLayer = sr.sortingLayer,
                              .order = sr.sortOrder,
                              .blend = (uint8_t)renderer::BlendKind::Alpha,
                              .filter = (uint8_t)renderer::FilterKind::Linear,
                              .flags = (uint8_t)(sr.flags & kSrFlipMask)});
            entityToRenderable_[e.id] = rid;
        } else {
            rid = it->second;
        }
        rm_.SetSprite(rid, sr.spriteId);
        rm_.SetColor(rid, sr.colorRGBA);
        rm_.SetSort(rid, sr.sortingLayer, sr.sortOrder);
        rm_.SetTransform(rid, wt.pos, wt.rot, wt.scale);
    }
    // 内核 #2：销毁/禁用差集 → renderable 释放
    for (auto it = entityToRenderable_.begin(); it != entityToRenderable_.end();) {
        if (std::find(seen.begin(), seen.end(), it->first) == seen.end()) {
            rm_.Destroy(it->second);
            it = entityToRenderable_.erase(it);
        } else {
            ++it;
        }
    }
}

// ---------------------------------------------------------------- 渲染 ----
void ViewportRenderer::Render(rhi::CommandList& cl, EditorContext& ctx) {
    ExtractScene(ctx);
    RenderViewport(cl, 0, sceneBatcher_, sceneCam_, /*withOverlay=*/true, ctx);
    RenderViewport(cl, 1, gameBatcher_, gameCam_, /*withOverlay=*/false, ctx);
    overlay_.clear();
}

void ViewportRenderer::RenderViewport(rhi::CommandList& cl, uint32_t idx, SpriteBatcher& batcher,
                                      const Camera2D& cam, bool withOverlay,
                                      EditorContext& ctx) {
    RT& rt = rts_[idx];
    if (!rt.tex.IsValid()) return;
    const float aspect = (float)rt.w / (float)rt.h;
    const Rect view = cam.ViewRect(aspect);
    rm_.SetViewport(cam.center, view.max.x - view.min.x, view.max.y - view.min.y, 200.0f);
    auto packets = rm_.Extract(assets_.Registry(), 1.0f);
    if (idx == 0) lastSceneVisible_ = rm_.LastStats().visible;

    std::vector<SpritePacket> textPackets;
    textPackets.reserve(64);
    if (withOverlay) {
        // 实体名标签（屏幕恒定字号：字级 × 1/zoom 反缩放；锚点 = 实体底边中点下方）
        const float labelScale = 0.9f / cam.zoom;
        const uint32_t ink = math::PackRGBA(240, 255, 200, 230);
        auto& reg = assets_.Registry();
        for (auto [ent, tf, sr] : ctx.ActiveScene().View<ecs::Transform2D, ecs::SpriteRenderer>().each()) {
            (void)tf;
            Entity e = Scene::FromEntt(ent);
            ecs::WorldTransform2D wt{};
            ecs::ComputeWorldTransform(ctx.ActiveScene(), e, wt);
            char label[40];
            if (const ecs::Meta* m = ctx.ActiveScene().TryGet<ecs::Meta>(e); m && m->tag[0])
                std::snprintf(label, sizeof(label), "%s", m->tag);
            else
                std::snprintf(label, sizeof(label), "#%u", (uint32_t)(e.id & 0xFFFFFFFF));
            const uint32_t spriteH = sr.spriteId != 0 && sr.spriteId <= reg.SpriteCount() ? reg.GetSprite(sr.spriteId).heightPx
                                                                     : ProceduralAtlas::kCellPx;
            Vec2 anchor{wt.pos.x - assets_.Font().TextWidth(label, labelScale) * 0.5f,
                        wt.pos.y + wt.scale.y * spriteH * 0.5f + 4.0f / cam.zoom};
            assets_.Font().DrawText(textPackets, label, anchor, labelScale, ink);
        }
    }

    // 包合成：游戏包 + overlay（SceneView 专属，层 63 顶置）
    std::vector<SpritePacket> all(packets.begin(), packets.end());
    if (withOverlay) all.insert(all.end(), overlay_.begin(), overlay_.end());
    batcher.Bake(assets_.Registry(), all, {}, textPackets);

    const float clear[4] = {0.09f, 0.10f, 0.13f, 1.0f};
    cl.BeginOffscreenPass(rt.tex, clear); // RT 显式声明（#11）；EndPass 转 SHADER_READ
    cl.SetViewportScissor(rt.w, rt.h);
    batcher.Record(cl, cam.ViewProj(aspect));
    cl.EndPass();
}

// ------------------------------------------------------------- overlay ----
void ViewportRenderer::PushOverlayQuad(Vec2 pos, Vec2 size, float rot, uint32_t rgba,
                                       int16_t order) {
    SpritePacket p{};
    p.spriteId = assets_.WhiteSprite();
    p.colorBits = rgba;
    p.posX = pos.x;
    p.posY = pos.y;
    p.rot = rot;
    p.scaleX = size.x / (float)ProceduralAtlas::kCellPx;
    p.scaleY = size.y / (float)ProceduralAtlas::kCellPx;
    p.key = renderer::MakeBatchKey(0, renderer::BlendKind::Alpha, renderer::FilterKind::Linear,
                                   (uint32_t)kOverlayLayer);
    p.sortKey = ((uint64_t)(uint32_t)kOverlayLayer << 56) | ((uint64_t)(uint16_t)order << 40);
    overlay_.push_back(p);
}

void ViewportRenderer::PushOverlayLine(Vec2 a, Vec2 b, uint32_t rgba, float lineW, int16_t order) {
    Vec2 d = b - a;
    float len = Length(d);
    if (len < 1e-3f) return;
    PushOverlayQuad((a + b) * 0.5f, Vec2{len, lineW}, std::atan2(d.y, d.x), rgba, order);
}

void ViewportRenderer::PushOverlayRect(Vec2 center, Vec2 size, float rot, uint32_t rgba,
                                       float lineW, int16_t order) {
    const float cs = std::cos(rot), sn = std::sin(rot);
    auto corner = [&](float sx, float sy) {
        return Vec2{center.x + cs * sx * size.x * 0.5f - sn * sy * size.y * 0.5f,
                    center.y + sn * sx * size.x * 0.5f + cs * sy * size.y * 0.5f};
    };
    Vec2 c0 = corner(-1, -1), c1 = corner(1, -1), c2 = corner(1, 1), c3 = corner(-1, 1);
    PushOverlayLine(c0, c1, rgba, lineW, order);
    PushOverlayLine(c1, c2, rgba, lineW, order);
    PushOverlayLine(c2, c3, rgba, lineW, order);
    PushOverlayLine(c3, c0, rgba, lineW, order);
}

// --------------------------------------------------------------- 拾取 ----
bool ViewportRenderer::WorldBoundsOf(EditorContext& ctx, Entity e, Vec2& center, Vec2& size,
                                     float& rot) const {
    Scene& s = ctx.ActiveScene();
    if (!s.Alive(e)) return false;
    const ecs::Transform2D* t = s.TryGet<ecs::Transform2D>(e);
    if (!t) return false;
    ecs::WorldTransform2D wt{};
    if (!ecs::ComputeWorldTransform(s, e, wt)) {
        wt.pos = t->pos;
        wt.rot = t->rot;
        wt.scale = t->scale;
    }
    center = wt.pos;
    rot = wt.rot;
    if (const ecs::SpriteRenderer* sr = s.TryGet<ecs::SpriteRenderer>(e);
        sr && sr->spriteId < assets_.Registry().SpriteCount()) {
        const auto& info = assets_.Registry().GetSprite(sr->spriteId);
        size = Vec2{wt.scale.x * info.widthPx, wt.scale.y * info.heightPx};
    } else {
        size = Vec2{24.0f * wt.scale.x, 24.0f * wt.scale.y};
    }
    return true;
}

ecs::Entity ViewportRenderer::Pick(EditorContext& ctx, Vec2 worldPos) const {
    Scene& s = ctx.ActiveScene();
    Entity best = Entity::Null();
    float bestD = 1e30f;
    s.Each([&](Entity e) {
        Vec2 c, sz;
        float rot = 0;
        if (!WorldBoundsOf(ctx, e, c, sz, rot)) return;
        Vec2 d{worldPos.x - c.x, worldPos.y - c.y};
        const float cs = std::cos(-rot), sn = std::sin(-rot);
        Vec2 l{cs * d.x - sn * d.y, sn * d.x + cs * d.y};
        if (std::fabs(l.x) > sz.x * 0.5f || std::fabs(l.y) > sz.y * 0.5f) return;
        float dist = Length(d);
        if (dist < bestD) {
            bestD = dist;
            best = e;
        }
    });
    return best;
}

Vec2 ViewportRenderer::WorldToScreen(const Camera2D& cam, Vec2 world, uint32_t rtW,
                                     uint32_t rtH) const {
    const float aspect = (float)rtW / (float)rtH;
    Vec2 ndc = cam.ViewProj(aspect).Apply(world); // [-1,1]，Y 向下
    return Vec2{(ndc.x * 0.5f + 0.5f) * (float)rtW, (ndc.y * 0.5f + 0.5f) * (float)rtH};
}

Vec2 ViewportRenderer::ScreenToWorld(const Camera2D& cam, Vec2 screen, uint32_t rtW,
                                     uint32_t rtH) const {
    const float aspect = (float)rtW / (float)rtH;
    // ViewProj = Ortho(center, hw, hh)：world = (screen01*2-1)*(hw,hh) + center
    const float hw = cam.HalfWidth(aspect), hh = cam.halfHeight;
    return Vec2{(screen.x / (float)rtW * 2.0f - 1.0f) * hw + cam.center.x,
                (screen.y / (float)rtH * 2.0f - 1.0f) * hh + cam.center.y};
}

} // namespace lemon::editor
