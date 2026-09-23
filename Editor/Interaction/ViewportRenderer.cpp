// Lemon 编辑器 — 视口渲染器实现（M4.md §2.2/§3.1；内核 #2/#4/#11/#13）
#include "Interaction/ViewportRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>

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
using ecs::kSrEnabled;     // RenderComponents.h 单一来源
using ecs::kSrFlipMask;
static constexpr float kOverlayLayer = 63.0f; // overlay 排序层（最顶）

// ------------------------------------------------------- 图标形状页 ----
// M4.7b 决议 D1：自绘 16 枚。4× 超采样软件光栅化（128² 覆盖缓冲 → box 降采样 32²），
// 白色形状（RGB 全 255，A=覆盖）→ ImGui::Image tint 染主题色；DPI 2x 下线性采样仍清晰。
namespace {
class IconCanvas {
public:
    static constexpr uint32_t kSS = 4;                       // 超采样倍数
    static constexpr uint32_t kN = ProceduralAtlas::kIconPx * kSS;
    float cov_[kN * kN] = {0};                               // 32px 坐标系下的覆盖

    void Add(float x, float y) {                             // 单样本累加
        if (x < 0 || y < 0 || x >= 32.0f || y >= 32.0f) return;
        uint32_t ix = (uint32_t)(x * kSS), iy = (uint32_t)(y * kSS);
        if (cov_[(size_t)iy * kN + ix] < 1.0f) cov_[(size_t)iy * kN + ix] += 1.0f;
    }
    void FillTri(Vec2 a, Vec2 b, Vec2 c) {                   // 重心覆盖（逐样本判定）
        float minX = std::max(0.0f, std::min({a.x, b.x, c.x}));
        float maxX = std::min(32.0f, std::max({a.x, b.x, c.x}));
        float minY = std::max(0.0f, std::min({a.y, b.y, c.y}));
        float maxY = std::min(32.0f, std::max({a.y, b.y, c.y}));
        for (float y = minY; y < maxY; y += 0.25f)
            for (float x = minX; x < maxX; x += 0.25f) {
                const float px = x + 0.125f, py = y + 0.125f;
                const bool s0 = (b.x - a.x) * (py - a.y) - (b.y - a.y) * (px - a.x) >= 0;
                const bool s1 = (c.x - b.x) * (py - b.y) - (c.y - b.y) * (px - b.x) >= 0;
                const bool s2 = (a.x - c.x) * (py - c.y) - (a.y - c.y) * (px - c.x) >= 0;
                if (s0 == s1 && s1 == s2) Add(px, py);
            }
    }
    void FillRect(float x0, float y0, float x1, float y1) {
        for (float y = std::max(0.0f, y0); y < std::min(32.0f, y1); y += 0.25f)
            for (float x = std::max(0.0f, x0); x < std::min(32.0f, x1); x += 0.25f)
                Add(x + 0.125f, y + 0.125f);
    }
    void StrokeRect(float x0, float y0, float x1, float y1, float w) {
        FillRect(x0, y0, x1, y0 + w);
        FillRect(x0, y1 - w, x1, y1);
        FillRect(x0, y0, x0 + w, y1);
        FillRect(x1 - w, y0, x1, y1);
    }
    void StrokeLine(Vec2 a, Vec2 b, float w) {               // 点到线段距离 ≤ w/2
        const float minX = std::max(0.0f, std::min(a.x, b.x) - w);
        const float maxX = std::min(32.0f, std::max(a.x, b.x) + w);
        const float minY = std::max(0.0f, std::min(a.y, b.y) - w);
        const float maxY = std::min(32.0f, std::max(a.y, b.y) + w);
        const Vec2 d{b.x - a.x, b.y - a.y};
        const float len2 = d.x * d.x + d.y * d.y;
        for (float y = minY; y < maxY; y += 0.25f)
            for (float x = minX; x < maxX; x += 0.25f) {
                const float px = x + 0.125f, py = y + 0.125f;
                float t = len2 > 0 ? ((px - a.x) * d.x + (py - a.y) * d.y) / len2 : 0.0f;
                t = std::clamp(t, 0.0f, 1.0f);
                const float cx = a.x + d.x * t, cy = a.y + d.y * t;
                const float dx = px - cx, dy = py - cy;
                if (dx * dx + dy * dy <= w * w * 0.25f) Add(px, py);
            }
    }
    void StrokeArc(Vec2 c, float r, float a0, float a1, float w) {
        for (float y = std::max(0.0f, c.y - r - w); y < std::min(32.0f, c.y + r + w); y += 0.25f)
            for (float x = std::max(0.0f, c.x - r - w); x < std::min(32.0f, c.x + r + w);
                 x += 0.25f) {
                const float px = x + 0.125f, py = y + 0.125f;
                const float dx = px - c.x, dy = py - c.y;
                const float dist = std::sqrt(dx * dx + dy * dy);
                if (std::fabs(dist - r) > w * 0.5f) continue;
                float ang = std::atan2(dy, dx);
                if (ang < 0) ang += math::kTau;
                const float A0 = std::fmin(a0, a1), A1 = std::fmax(a0, a1);
                if (ang >= A0 && ang <= A1) Add(px, py);
            }
    }
    void FillCircle(Vec2 c, float r) { StrokeArc(c, r, 0.0f, math::kTau, r * 2.0f); }
};
} // namespace

rhi::Texture ProceduralAtlas::BuildIconPage(rhi::Device& device) {
    // 覆盖缓冲 16×64KB → 堆分配（栈上放不下）
    auto cv = std::make_unique<IconCanvas[]>(kIconCount);
    // 坐标系 32px，白形状（下面每枚一段；传输/工具/实体/资产四组）
    { // Play ▶
        IconCanvas& c = cv[(int)IconKind::Play];
        c.FillTri({10, 6}, {10, 26}, {26, 16});
    }
    { // Pause ‖
        IconCanvas& c = cv[(int)IconKind::Pause];
        c.FillRect(9, 7, 14.5f, 25);
        c.FillRect(17.5f, 7, 23, 25);
    }
    { // Step |▶
        IconCanvas& c = cv[(int)IconKind::Step];
        c.FillRect(7, 7, 11, 25);
        c.FillTri({14, 7}, {14, 25}, {25, 16});
    }
    { // Stop ■
        IconCanvas& c = cv[(int)IconKind::Stop];
        c.FillRect(8, 8, 24, 24);
    }
    { // Move ✥（十字 + 四端箭头）
        IconCanvas& c = cv[(int)IconKind::Move];
        c.StrokeLine({16, 5}, {16, 27}, 2.4f);
        c.StrokeLine({5, 16}, {27, 16}, 2.4f);
        c.FillTri({12.5f, 6}, {19.5f, 6}, {16, 2.5f});
        c.FillTri({12.5f, 26}, {19.5f, 26}, {16, 29.5f});
        c.FillTri({6, 12.5f}, {6, 19.5f}, {2.5f, 16});
        c.FillTri({26, 12.5f}, {26, 19.5f}, {29.5f, 16});
    }
    { // Rotate ↻（弧 + 箭头）
        IconCanvas& c = cv[(int)IconKind::Rotate];
        c.StrokeArc({16, 17}, 9, math::kTau * 0.08f, math::kTau * 0.80f, 2.4f);
        c.FillTri({21, 4}, {27.5f, 9}, {20.5f, 11.5f});
    }
    { // Scale ⤡（对角线 + 端箭头 + 角标）
        IconCanvas& c = cv[(int)IconKind::Scale];
        c.StrokeLine({7, 25}, {24, 8}, 2.4f);
        c.FillTri({18, 5.5f}, {26.5f, 5.5f}, {26.5f, 14});
        c.StrokeLine({5, 19}, {5, 27}, 2.2f);
        c.StrokeLine({5, 27}, {13, 27}, 2.2f);
    }
    { // Grid #（井字网格）
        IconCanvas& c = cv[(int)IconKind::Grid];
        c.StrokeLine({11, 6}, {11, 26}, 2.0f);
        c.StrokeLine({21, 6}, {21, 26}, 2.0f);
        c.StrokeLine({6, 11}, {26, 11}, 2.0f);
        c.StrokeLine({6, 21}, {26, 21}, 2.0f);
    }
    { // Entity ◇（空实体）
        IconCanvas& c = cv[(int)IconKind::Entity];
        c.StrokeLine({16, 5}, {27, 16}, 2.2f);
        c.StrokeLine({27, 16}, {16, 27}, 2.2f);
        c.StrokeLine({16, 27}, {5, 16}, 2.2f);
        c.StrokeLine({5, 16}, {16, 5}, 2.2f);
    }
    { // Sprite ■（精灵实体 = 实心四边形）
        IconCanvas& c = cv[(int)IconKind::Sprite];
        c.FillRect(9, 9, 23, 23);
    }
    { // Camera ▣▶（机身 + 镜头楔）
        IconCanvas& c = cv[(int)IconKind::Camera];
        c.StrokeRect(5, 10, 21, 25, 2.2f);
        c.FillTri({21, 14.5f}, {27, 11.5f}, {27, 23.5f});
    }
    { // Script ≣（带脚本实体 = 文档 + 行）
        IconCanvas& c = cv[(int)IconKind::Script];
        c.StrokeRect(7, 4, 25, 28, 2.0f);
        c.StrokeLine({11, 11}, {21, 11}, 2.2f);
        c.StrokeLine({11, 16.5f}, {21, 16.5f}, 2.2f);
        c.StrokeLine({11, 22}, {17, 22}, 2.2f);
    }
    { // AssetSprite ⛰（图片：框 + 山 + 日）
        IconCanvas& c = cv[(int)IconKind::AssetSprite];
        c.StrokeRect(5, 6, 27, 26, 2.2f);
        c.FillTri({8, 23}, {14.5f, 13}, {21, 23});
        c.FillCircle({21.5f, 12}, 2.4f);
    }
    { // AssetPrefab ❐（叠层方块 = 组合体）
        IconCanvas& c = cv[(int)IconKind::AssetPrefab];
        c.StrokeRect(5, 12, 19, 27, 2.2f);
        c.StrokeRect(12, 5, 26, 20, 2.2f);
    }
    { // AssetScript ‹›（代码文档 = 折角 + 书名号）
        IconCanvas& c = cv[(int)IconKind::AssetScript];
        c.StrokeRect(7, 4, 25, 28, 2.0f);
        c.FillTri({19, 4}, {25, 4}, {25, 10}); // 折角
        c.StrokeLine({13, 13}, {10.5f, 16.5f}, 2.0f);
        c.StrokeLine({10.5f, 16.5f}, {13, 20}, 2.0f);
        c.StrokeLine({18, 13}, {20.5f, 16.5f}, 2.0f);
        c.StrokeLine({20.5f, 16.5f}, {18, 20}, 2.0f);
    }
    { // AssetGeneric ▭（通用资产 = 空文档）
        IconCanvas& c = cv[(int)IconKind::AssetGeneric];
        c.StrokeRect(7, 4, 25, 28, 2.0f);
        c.StrokeLine({11, 10}, {21, 10}, 2.0f);
    }
    { // Cursor ↖（Select 工具 = 经典箭头光标 + 尾翼）
        IconCanvas& c = cv[(int)IconKind::Cursor];
        c.FillTri({9, 4}, {9, 25}, {15.5f, 18.5f});
        c.FillTri({13, 18}, {22.5f, 22.5f}, {17.5f, 24.5f});
        c.FillTri({13, 18}, {17.5f, 24.5f}, {12.5f, 21.5f});
    }
    { // Magnet U（拖拽吸附 = 马蹄磁铁，开口向上）
        IconCanvas& c = cv[(int)IconKind::Magnet];
        c.StrokeArc({16, 13}, 8, 0.0f, 3.14159265f, 5.0f); // 底部半圆
        c.StrokeLine({8, 13}, {8, 5}, 5.0f);
        c.StrokeLine({24, 13}, {24, 5}, 5.0f);
        c.FillRect(5.5f, 3, 10.5f, 8);  // 极靴（加深两臂端头）
        c.FillRect(21.5f, 3, 26.5f, 8);
    }

    // 覆盖缓冲 → 白 RGBA（4×4 box 降采样）
    constexpr uint32_t n = kIconCount, px = ProceduralAtlas::kIconPx;
    std::vector<uint8_t> out((size_t)n * px * px * 4, 0);
    for (uint32_t i = 0; i < n; ++i)
        for (uint32_t y = 0; y < px; ++y)
            for (uint32_t x = 0; x < px; ++x) {
                float acc = 0;
                for (uint32_t sy = 0; sy < IconCanvas::kSS; ++sy)
                    for (uint32_t sx = 0; sx < IconCanvas::kSS; ++sx)
                        acc += cv[i].cov_[((size_t)y * IconCanvas::kSS + sy) * IconCanvas::kN +
                                          x * IconCanvas::kSS + sx];
                const uint8_t a = (uint8_t)std::lround(std::min(acc, 16.0f) / 16.0f * 255.0f);
                uint8_t* q = &out[(((size_t)y * n + i) * px + x) * 4];
                q[0] = q[1] = q[2] = 255;
                q[3] = a;
            }
    rhi::Texture tex = device.CreateTexture(
        {.width = n * px, .height = px, .debugName = "editorIcons"});
    device.UploadTexture(tex, out.data(), out.size());
    return tex;
}

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

    iconPage_ = BuildIconPage(device); // 形状页 → 图集槽 2（M4.7b）
    device.BindTextureToSlot(iconPage_, 2);
    atlas_.RegisterAtlas(2, iconPage_, kIconCount * kIconPx, kIconPx);

    linear_ = device.CreateSampler({});
    point_ = device.CreateSampler({.min = rhi::FilterMode::Point, .mag = rhi::FilterMode::Point});
    device.BindSamplerToSlot(linear_, 0);
    device.BindSamplerToSlot(point_, 1);
}

void ProceduralAtlas::IconUV(IconKind k, float& u0, float& v0, float& u1, float& v1) const {
    const float n = (float)kIconCount;
    const uint32_t i = (uint32_t)k;
    u0 = (float)i / n;
    u1 = (float)(i + 1) / n;
    v0 = 0.0f;
    v1 = 1.0f;
}

// ------------------------------------------------------------- 生命周期 ----
void ViewportRenderer::Init(rhi::Device& device, ImGuiBackend& ui) {
    device_ = &device;
    ui_ = &ui;
    assets_.Build(device);
    paletteIconTex_ = ui.RegisterViewportTexture(assets_.Page().id); // 图标源（M4.4）
    iconTex_ = ui.RegisterViewportTexture(assets_.IconPage().id);    // 形状页（M4.7b）
    const rhi::Format rtFormat = rhi::Format::RGBA8Unorm; // 与 EnsureRenderTarget 一致
    sceneBatcher_.Init(device, 0, 1, rtFormat, /*ringSlot=*/0); // 实例环各占一槽
    gameBatcher_.Init(device, 0, 1, rtFormat, /*ringSlot=*/1);  // （M4.7-P0 竞态修复）
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
    RebindProceduralIcons();
    const rhi::Format rtFormat = rhi::Format::RGBA8Unorm;
    sceneBatcher_.Init(device, 0, 1, rtFormat, /*ringSlot=*/0); // 管线经磁盘缓存重建；实例环形缓冲重建
    gameBatcher_.Init(device, 0, 1, rtFormat, /*ringSlot=*/1);
}

void ViewportRenderer::RebindProceduralIcons() {
    // 设备重建路径（旧纹理已死，注销旧 id 仅防句柄表残留）与换项目复位路径
    // （旧纹理活体，注销后再释放由 ProceduralAtlas 重建覆盖）共用
    if (!ui_) return;
    if (paletteIconTex_) ui_->UnregisterViewportTexture(paletteIconTex_);
    if (iconTex_) ui_->UnregisterViewportTexture(iconTex_);
    paletteIconTex_ = ui_->RegisterViewportTexture(assets_.Page().id);
    iconTex_ = ui_->RegisterViewportTexture(assets_.IconPage().id);
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
        for (auto& [id, er] : entityToRenderable_) rm_.Destroy(er.rid);
        entityToRenderable_.clear();
        lastSceneStamp_ = stamp;
    }

    rm_.BeginSimTick();
    const uint64_t epoch = ++extractEpoch_;
    for (auto [ent, tf, sr] : s.View<ecs::Transform2D, ecs::SpriteRenderer>().each()) {
        (void)tf;
        Entity e = Scene::FromEntt(ent);
        if (!(sr.flags & kSrEnabled)) continue;
        // 悬空引用（资产已删/未导入）：不建 renderable（Inspector 槽红显 + 体检红字；
        // GetSprite 越界断言的编辑器侧防线）。空洞号（退役资产）同理不渲染
        if (!assets_.Registry().IsValidSprite(sr.spriteId)) continue;

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
            entityToRenderable_[e.id] = {rid, epoch};
        } else {
            rid = it->second.rid;
            it->second.lastSeen = epoch;
        }
        rm_.SetSprite(rid, sr.spriteId);
        rm_.SetColor(rid, sr.colorRGBA);
        rm_.SetSort(rid, sr.sortingLayer, sr.sortOrder);
        rm_.SetTransform(rid, wt.pos, wt.rot, wt.scale);
    }
    // 内核 #2：销毁/禁用差集 → renderable 释放（纪元比对 O(N)）
    for (auto it = entityToRenderable_.begin(); it != entityToRenderable_.end();) {
        if (it->second.lastSeen != epoch) {
            rm_.Destroy(it->second.rid);
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
        // 实体名标签（屏幕恒定字号：字级 × 1/zoom 反缩放；锚点 = 实体底边中点下方）。
        // M5 性能批纪律：视口外不画（世界 AABB + 屏幕边距），预算封顶 kMaxLabels——
        // 万级实体全画既不可读也不可跑；谓词与 ExtractScene 对齐（禁用/悬空精灵无标签，
        // 原先禁用精灵也画标签是漏网）
        constexpr uint32_t kMaxLabels = 256;
        const float labelScale = 0.9f / cam.zoom;
        const uint32_t ink = overlay::kLabelInk; // 与冒烟像素断言共用（overlay::）
        const float margin = 128.0f / cam.zoom; // 屏幕像素 → 世界空间边距
        auto& reg = assets_.Registry();
        uint32_t labels = 0;
        for (auto [ent, tf, sr] : ctx.ActiveScene().View<ecs::Transform2D, ecs::SpriteRenderer>().each()) {
            (void)tf;
            if (labels >= kMaxLabels) break;
            if (!(sr.flags & kSrEnabled)) continue;
            if (!reg.IsValidSprite(sr.spriteId)) continue;
            Entity e = Scene::FromEntt(ent);
            ecs::WorldTransform2D wt{};
            ecs::ComputeWorldTransform(ctx.ActiveScene(), e, wt);
            if (wt.pos.x < view.min.x - margin || wt.pos.x > view.max.x + margin ||
                wt.pos.y < view.min.y - margin || wt.pos.y > view.max.y + margin)
                continue; // 视口（+边距）外
            char label[40];
            if (const ecs::Meta* m = ctx.ActiveScene().TryGet<ecs::Meta>(e); m && m->tag[0])
                std::snprintf(label, sizeof(label), "%s", m->tag);
            else
                std::snprintf(label, sizeof(label), "#%u", (uint32_t)(e.id & 0xFFFFFFFF));
            const uint32_t spriteH = reg.IsValidSprite(sr.spriteId)
                                         ? reg.GetSprite(sr.spriteId).heightPx
                                         : ProceduralAtlas::kCellPx;
            Vec2 anchor{wt.pos.x - assets_.Font().TextWidth(label, labelScale) * 0.5f,
                        wt.pos.y + wt.scale.y * spriteH * 0.5f + 4.0f / cam.zoom};
            assets_.Font().DrawText(textPackets, label, anchor, labelScale, ink);
            ++labels;
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
    // SpritePacket.scale = 世界像素边长（Extract/DrawText 同约定；shader 直接作四边形
    // 世界 extent 消费）——M4.7-P0：此处曾误除 kCellPx(64) 致 overlay 整体缩小 64 倍
    // （1.5px 线宽 → 0.023px），网格/选框/手柄全部亚像素不可见
    p.scaleX = size.x;
    p.scaleY = size.y;
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
        sr && assets_.Registry().IsValidSprite(sr->spriteId)) {
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
