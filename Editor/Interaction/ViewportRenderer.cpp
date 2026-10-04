// Lemon 编辑器 — 视口渲染器实现（M4.md §2.2/§3.1；内核 #2/#4/#11/#13）
#include "Interaction/ViewportRenderer.h"

#include <algorithm>
#include <chrono>
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
#include "Renderer/GameFx.h" // renderer::AppendGameFx（Play 表现层段；M7a 批④ 下沉件）

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
    { // AssetRml ▭⌒（UI 文档 = 窗口页：框 + 标题栏 + 圆点钮；批③b）
        IconCanvas& c = cv[(int)IconKind::AssetRml];
        c.StrokeRect(5, 6, 27, 26, 2.2f);
        c.StrokeLine({5, 12.5f}, {27, 12.5f}, 2.2f); // 标题栏分隔
        c.FillCircle({9.5f, 9.2f}, 1.7f);            // 窗口钮
    }
    { // AssetRcss ▭≡（样式表 = 文档 + 阶梯行 + 尾点；批③b）
        IconCanvas& c = cv[(int)IconKind::AssetRcss];
        c.StrokeRect(7, 4, 25, 28, 2.0f);
        c.StrokeLine({11, 12}, {21, 12}, 2.0f);
        c.StrokeLine({11, 17.5f}, {17, 17.5f}, 2.0f);
        c.StrokeLine({11, 23}, {13.5f, 23}, 2.0f);
        c.FillCircle({19, 23}, 1.6f); // 行尾选点
    }
    { // AssetAudio ◩∿（音频 = 扬声器锥 + 两道声波；M6c 批①）
        IconCanvas& c = cv[(int)IconKind::AssetAudio];
        c.FillTri({8, 13}, {8, 21}, {13.5f, 17.0f}); // 箱体
        c.StrokeRect(5, 13, 8, 21, 2.0f);
        c.StrokeLine({17, 12}, {21.5f, 8.5f}, 2.0f);  // 声波一
        c.StrokeLine({21.5f, 8.5f}, {25, 12}, 2.0f);
        c.StrokeLine({19, 19}, {23.5f, 15.5f}, 2.0f); // 声波二
        c.StrokeLine({23.5f, 15.5f}, {27, 19}, 2.0f);
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
    { // Add ＋（列表行"新建"）
        IconCanvas& c = cv[(int)IconKind::Add];
        c.FillRect(6, 14, 26, 18);
        c.FillRect(14, 6, 18, 26);
    }
    { // Duplicate ⧉（前实后虚两个错位矩形）
        IconCanvas& c = cv[(int)IconKind::Duplicate];
        c.StrokeRect(4, 10, 20, 26, 2.6f); // 后块（描边）
        c.FillRect(11, 3, 27, 19);         // 前块（实心，盖住后块一角）
    }
    { // Delete ×（两道粗对角线）
        IconCanvas& c = cv[(int)IconKind::Delete];
        c.StrokeLine({8, 8}, {24, 24}, 4.5f);
        c.StrokeLine({24, 8}, {8, 24}, 4.5f);
    }
    { // Rename ✎（铅笔 = 斜杆 + 笔尖三角 + 笔尾横杠）
        IconCanvas& c = cv[(int)IconKind::Rename];
        c.StrokeLine({7, 25}, {22, 10}, 4.2f);        // 笔杆
        c.FillTri({4, 28}, {10, 28}, {7, 21});        // 笔尖（斜切三角）
        c.StrokeLine({19, 5}, {26, 12}, 4.2f);        // 笔尾橡皮头
    }
    { // Search 🔍（圆 + 斜柄）
        IconCanvas& c = cv[(int)IconKind::Search];
        c.StrokeArc({13, 13}, 7.5f, 0.0f, math::kTau, 3.4f);
        c.StrokeLine({18.5f, 18.5f}, {27, 27}, 4.2f);
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
void ProceduralAtlas::ReleaseGpu(rhi::Device& device) {
    // #95：换项目复位路径的旧代显式释放——Reset()+Build() 覆盖句柄 ≠ 释放资源，
    // 每次项目切换净漏 3 张纹理（调色板/图标/字体页；字体页句柄只在 registry，
    // 须在 Reset 前取回）。设备丢失路径不走这里（句柄已死，直接 Reset 重建）。
    // 采样器不释放（RHI 无 DestroySampler API，对象轻量）——Build 会重建覆盖。
    device.WaitIdle(); // 在途帧可能采样旧页
    uint32_t w = 0, h = 0;
    if (rhi::Texture f = atlas_.AtlasTexture(font_.AtlasSlot(), w, h); f.IsValid())
        device.DestroyTexture(f);
    if (page_.IsValid()) device.DestroyTexture(page_);
    if (iconPage_.IsValid()) device.DestroyTexture(iconPage_);
    page_ = iconPage_ = {};
}

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
    // 合批器不在此重 Init（评审 D3，2026-09-30）：其自登记回调（注册序在本回调之前）
    // 已在同一次设备丢失序列里完成几何/管线/实例环重建；此处再 Init 只会重复登记
    // 回调 token 并覆盖刚重建的活资源（每次设备丢失净漏 2 buffer + 2 shader + 4 pipeline）
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
    if (rt.tex.IsValid() && rt.w == wantW && rt.h == wantH) {
        rt.pendFrames = 0; // 尺寸匹配：撤销待稳计数
        return rt.imguiTexId;
    }
    // 尺寸稳定节流（review 2026-10-02 #98）：活体尚在且尺寸漂移（拖 dock 分隔条
    // 逐帧变）→ 连续 kRtStableFrames 帧同尺寸才重建；漂移期旧 RT 继续服役
    //（Image 全幅拉伸采样，拖拽中轻微模糊可接受）。首建/设备重建后无活体 = 立即建
    if (rt.tex.IsValid()) {
        constexpr uint32_t kRtStableFrames = 3;
        if (rt.pendW != wantW || rt.pendH != wantH) {
            rt.pendW = wantW;
            rt.pendH = wantH;
            rt.pendFrames = 0;
        }
        if (++rt.pendFrames < kRtStableFrames) return rt.imguiTexId;
    }
    rt.pendFrames = 0;
    if (rt.tex.IsValid() || rt.imguiTexId) {
        device_->WaitIdle(); // 停稳重建低频；在途帧可能引用旧视图/描述符集（验证层实抓）
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
// M7a 批③：提取本体 = 引擎 renderer::SceneExtractor（搬家非复制；编辑器/lemon-game
// 共用，防第三套实现）。编辑器保持每渲染帧直调时序（Render 内、插值 alpha 契约）
// ——不装 RenderExtractSystem（管线驱动形态 = 批④ lemon-game 装配）。
void ViewportRenderer::ExtractScene(EditorContext& ctx) {
    extractor_.Extract(ctx.ActiveScene(), assets_.Registry(), rm_);
}

// ---------------------------------------------------------------- 渲染 ----
void ViewportRenderer::Render(rhi::CommandList& cl, EditorContext& ctx, float simAlpha) {
    ExtractScene(ctx);
    RenderViewport(cl, 0, sceneBatcher_, sceneCam_, /*withOverlay=*/true, ctx, simAlpha);
    RenderViewport(cl, 1, gameBatcher_, gameCam_, /*withOverlay=*/false, ctx, simAlpha);
    overlay_.clear();
}

void ViewportRenderer::RenderViewport(rhi::CommandList& cl, uint32_t idx, SpriteBatcher& batcher,
                                      const Camera2D& cam, bool withOverlay,
                                      EditorContext& ctx, float simAlpha) {
    RT& rt = rts_[idx];
    if (!rt.tex.IsValid()) return;
    const float aspect = (float)rt.w / (float)rt.h;
    const Rect view = cam.ViewRect(aspect);
    rm_.SetViewport(cam.center, (view.max.x - view.min.x) * 0.5f,
                    (view.max.y - view.min.y) * 0.5f, 200.0f); // SetViewport 取半宽/半高
    auto packets = rm_.Extract(assets_.Registry(), simAlpha);
    if (idx == 0) lastSceneVisible_ = rm_.LastStats().visible;

    // 复用缓冲（2026-09-26 渲染提取批：提取段零分配）——两视口串行调用、
    // Bake 消费完即弃，单缓冲安全
    auto& textPackets = textBuf_;
    textPackets.clear();
    auto& fxBarPackets = fxBarBuf_;
    fxBarPackets.clear();
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

    // M6a 批①：世界空间表现通道（GameView 专属；06 §8 恒定原则——恒走 sprite
    // 管线）。本体 = 引擎 renderer::AppendGameFx（M7a 批④ 下沉，lemon-game 共用，
    // 防第三套实现）；此处保留渲染帧时钟（lastFxTime_）与出 Play 复位。
    if (!withOverlay && ctx.Playing()) {
        const auto now = std::chrono::steady_clock::now();
        const float fxDt = lastFxTime_.time_since_epoch().count() == 0
                               ? 0.0f
                               : std::clamp(
                                     std::chrono::duration<float>(now - lastFxTime_).count(),
                                     0.0f, 0.1f);
        lastFxTime_ = now;
        renderer::AppendGameFx(ctx.ActiveWorld().Fx(), ctx.ActiveScene(), view,
                               assets_.WhiteSprite(), assets_.Font(), fxDt, fxBarPackets,
                               textPackets);
    } else if (!withOverlay) {
        lastFxTime_ = {}; // 出 Play 复位（重进 Play 首帧 dt=0）
    }

    // 包合成 → Bake 尾段（2026-09-26 渲染提取批）：精灵主段直传 Extract span，
    // fxBar/overlay 走 tailPackets——段拼接序 = 原"合成一个 vector"的序（绘制输出
    // 同构），免每帧堆分配 + 50k 级包拷贝（双视口 ×56B/包）
    batcher.Bake(assets_.Registry(), packets, {},
                 textPackets, withOverlay ? std::span<const SpritePacket>(overlay_)
                                          : std::span<const SpritePacket>(fxBarPackets));

    const float clear[4] = {0.09f, 0.10f, 0.13f, 1.0f};
    cl.BeginOffscreenPass(rt.tex, clear); // RT 显式声明（#11）；EndPass 转 SHADER_READ
    cl.SetViewportScissor(rt.w, rt.h);
    batcher.Record(cl, cam.ViewProj(aspect));
    // 批③a（ADR-014）：游戏 UI（RmlUi）叠画于 sprite 之上——同一动态渲染块内、
    // EndPass 之前（离屏块 loadOp 恒 CLEAR，块后另开块会清内容；此为唯一合法插入点）
    if (idx == 1 && gameUi_ && ctx.Playing()) gameUi_(cl, rt.w, rt.h);
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
