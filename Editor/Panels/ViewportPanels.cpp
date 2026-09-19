// Lemon 编辑器 — SceneView / GameView 面板（M4-Editor-Plan §2.2；内核 #11/#13 消费端）
// SceneView：编辑相机（pan/zoom/F 自愈）+ 拾取（单选/Ctrl 多选）+ Gizmo 三态
// （移动十字/旋转圈/四角缩放）+ 网格吸附 + overlay 注入（网格/选框/Gizmo 手柄）。
// GameView：游戏相机离屏（编辑态也实时显示）；输入门控 M4.3。
// Gizmo 拖拽 = Transform 直写 + dirty；Undo 属性轨 M4.3 接入（拖拽合并）。
#include <cmath>
#include <cstring>

#include "App/EditorApp.h"
#include "App/ImGuiBackend.h"
#include "Core/Math.h"
#include "ECS/Hierarchy.h"
#include "EditorContext.h"
#include "Interaction/ViewportRenderer.h"
#include "Panels/BuiltInPanels.h"
#include "imgui.h"

namespace lemon::editor {

using ecs::Entity;

namespace {

constexpr float kRefHalfHeight = 360.0f; // 编辑相机 zoom=1 基准半高
constexpr float kGridSpacing = 32.0f;    // 网格间距（世界 px）
constexpr float kSnapPos = 8.0f;         // 平移吸附档
constexpr float kSnapRotDeg = 15.0f;     // 旋转吸附档
// overlay 颜色统一经 PackRGBA（与 sprite 着色同通道，避免字节序歧义）
const uint32_t kSelColor = math::PackRGBA(120, 225, 240, 220);   // 选框青
const uint32_t kPrimaryColor = math::PackRGBA(90, 215, 245, 255); // 主选亮青
const uint32_t kGridColor = math::PackRGBA(120, 140, 150, 70);   // 网格
const uint32_t kAxisColor = math::PackRGBA(150, 175, 185, 110);  // 主轴稍亮
const uint32_t kHandleColor = math::PackRGBA(250, 220, 90, 255); // Gizmo 手柄黄

float SnapTo(float v, float step) { return std::round(v / step) * step; }

} // namespace

// -------------------------------------------------------------- SceneView --
void SceneViewPanel::OnGui(EditorApp& app) {
    if (!ImGui::Begin("Scene", nullptr, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }
    EditorContext& ctx = app.Ctx();
    ViewportRenderer& vr = app.Viewport();

    ImGui::BeginChild("sv", ImVec2(0, 0), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float dpi = app.Ui().DisplayScale();
    if (avail.x < 16 || avail.y < 16) {
        ImGui::EndChild();
        ImGui::End();
        return;
    }
    const uint32_t rtW = (uint32_t)(avail.x * dpi), rtH = (uint32_t)(avail.y * dpi);
    void* tex = vr.EnsureRenderTarget(0, rtW, rtH, "sceneRT");
    Camera2D& cam = vr.SceneCam();
    cam.halfHeight = kRefHalfHeight / cam.zoom; // zoom 语义半高重导（resize 自愈同路径）

    const ImVec2 imagePos = ImGui::GetCursorScreenPos();
    const ImGuiIO& io = ImGui::GetIO();
    const ImVec2 mouseRel(io.MousePos.x - imagePos.x, io.MousePos.y - imagePos.y);
    const bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    const Vec2 mouseWorld = vr.ScreenToWorld(cam, Vec2{mouseRel.x, mouseRel.y}, rtW, rtH);

    // ---- 相机：滚轮缩放（以鼠标为中心）----
    if (hovered && io.MouseWheel != 0.0f && !io.WantCaptureKeyboard) {
        const Vec2 before = vr.ScreenToWorld(cam, Vec2{mouseRel.x, mouseRel.y}, rtW, rtH);
        cam.zoom = math::Clamp(cam.zoom * std::exp(-io.MouseWheel * 0.12f), 0.05f, 64.0f);
        cam.halfHeight = kRefHalfHeight / cam.zoom;
        const Vec2 after = vr.ScreenToWorld(cam, Vec2{mouseRel.x, mouseRel.y}, rtW, rtH);
        cam.center += before - after; // 鼠标下世界点不动（自愈式缩放）
    }
    // ---- 相机：中键 / 空格+左键 平移 ----
    const bool panning = ImGui::IsWindowHovered() &&
                         (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) ||
                          (ImGui::IsKeyDown(ImGuiKey_Space) && ImGui::IsMouseDragging(ImGuiMouseButton_Left)));
    if (panning) {
        ImVec2 d = ImGui::GetIO().MouseDelta;
        const float aspect = (float)rtW / (float)rtH;
        const float wppX = cam.HalfWidth(aspect) * 2.0f / (float)rtW;
        const float wppY = cam.halfHeight * 2.0f / (float)rtH;
        cam.center.x -= d.x * wppX;
        cam.center.y -= d.y * wppY;
    }

    // ---- Gizmo / 拾取状态机 ----
    const bool canInteract = hovered && !io.WantTextInput && !io.WantCaptureKeyboard;
    if (drag_ == DragMode::None && canInteract && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const Entity picked = vr.Pick(ctx, mouseWorld);
        const bool onSelected = !picked.IsNull() && ctx.IsSelected(picked);
        if (onSelected) BeginGizmoDrag(app, picked, mouseWorld);
        else clickPending_ = true; // 抬起时无拖拽 → 落选
    }
    if (drag_ != DragMode::None) {
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) EndGizmoDrag(app);
        else UpdateGizmoDrag(app, mouseWorld, rtW, rtH);
    } else if (clickPending_ && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        clickPending_ = false;
        if (canInteract) {
            Entity picked = vr.Pick(ctx, mouseWorld);
            if (!picked.IsNull()) ctx.Select(picked, io.KeyCtrl);
            else if (!io.KeyCtrl) ctx.ClearSelection();
        }
    }
    // F 框选聚焦（视口悬停时；§2.3 键位）
    if (hovered && !io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F, false))
        FocusSelection(app, rtW, rtH);

    // ---- overlay 注入（Render 前；世界空间）----
    const float lineW = 1.2f / cam.zoom;
    if (app.GridSnap()) DrawGrid(vr, cam, rtW, rtH);
    for (Entity e : ctx.Selection())
        if (e != ctx.Primary()) {
            Vec2 c, s;
            float rot = 0;
            if (vr.WorldBoundsOf(ctx, e, c, s, rot))
                vr.PushOverlayRect(c, s, rot, kSelColor, lineW);
        }
    { // 主选：选框 + Gizmo 手柄
        Entity p = ctx.Primary();
        Vec2 c, s;
        float rot = 0;
        if (!p.IsNull() && vr.WorldBoundsOf(ctx, p, c, s, rot)) {
            vr.PushOverlayRect(c, s, rot, kPrimaryColor, lineW * 1.5f);
            DrawGizmoHandles(app, vr, p, cam, c, s, rot);
        }
    }

    // 视口本体（UV：引擎 Y 向下与 ImGui 一致；0 号 RT）
    ImGui::Image(tex, avail, ImVec2(0, 0), ImVec2(1, 1));
    if (ImGui::BeginDragDropTarget()) { // 实体拖入视口 = 摘根（资产拖入 M4.4）
        if (const ImGuiPayload* pay = ImGui::AcceptDragDropPayload("LemonEntity")) {
            Entity dropped{};
            std::memcpy(&dropped, pay->Data, sizeof(dropped));
            if (SceneSetParent(ctx.ActiveScene(), dropped, Entity::Null())) ctx.dirty = true;
        }
        ImGui::EndDragDropTarget();
    }
    // 视口角标
    ImGui::SetCursorPos(ImVec2(6, 6));
    ImGui::TextDisabled("zoom %.2fx  center (%.0f, %.0f)  visible %u", cam.zoom, cam.center.x,
                        cam.center.y, vr.LastSceneVisible());

    ImGui::EndChild();
    ImGui::End();
}

void SceneViewPanel::BeginGizmoDrag(EditorApp& app, Entity /*primary*/, Vec2 world) {
    EditorContext& ctx = app.Ctx();
    dragStart_ = world;
    dragLast_ = world;
    dragTfs_.clear();
    for (Entity e : ctx.Selection()) {
        if (ctx.ActiveScene().Has<ecs::Transform2D>(e))
            dragTfs_.push_back({e, ctx.ActiveScene().Get<ecs::Transform2D>(e)});
    }
    pivot_ = ctx.Primary().IsNull() ? world : [&] {
        Vec2 c, s;
        float r = 0;
        return app.Viewport().WorldBoundsOf(ctx, ctx.Primary(), c, s, r) ? c : world;
    }();
    drag_ = app.Tool() == EditTool::Move ? DragMode::Move
          : app.Tool() == EditTool::Rotate ? DragMode::Rotate
                                           : DragMode::Scale;
    startAngle_ = std::atan2(world.y - pivot_.y, world.x - pivot_.x);
    startDist_ = Length(Vec2{world.x - pivot_.x, world.y - pivot_.y});
}

void SceneViewPanel::UpdateGizmoDrag(EditorApp& app, Vec2 world, uint32_t rtW, uint32_t rtH) {
    (void)rtW;
    (void)rtH;
    EditorContext& ctx = app.Ctx();
    const bool snap = app.GridSnap();
    if (drag_ == DragMode::Move) {
        Vec2 delta{world.x - dragStart_.x, world.y - dragStart_.y};
        for (auto& [e, start] : dragTfs_) {
            if (!ctx.ActiveScene().Alive(e)) continue;
            ecs::Transform2D& tf = ctx.ActiveScene().Get<ecs::Transform2D>(e);
            tf.pos = start.pos + delta;
            if (snap) {
                tf.pos.x = SnapTo(tf.pos.x, kSnapPos);
                tf.pos.y = SnapTo(tf.pos.y, kSnapPos);
            }
        }
    } else if (drag_ == DragMode::Rotate) {
        float ang = std::atan2(world.y - pivot_.y, world.x - pivot_.x) - startAngle_;
        if (snap) ang = math::kTau * SnapTo(ang / math::kTau * 360.0f, kSnapRotDeg) / 360.0f;
        for (auto& [e, start] : dragTfs_) {
            if (!ctx.ActiveScene().Alive(e)) continue;
            ecs::Transform2D& tf = ctx.ActiveScene().Get<ecs::Transform2D>(e);
            Vec2 d = start.pos - pivot_;
            float c = std::cos(ang), s = std::sin(ang);
            tf.pos = pivot_ + Vec2{c * d.x - s * d.y, s * d.x + c * d.y}; // 绕质心公转
            tf.rot = start.rot + ang;
        }
    } else if (drag_ == DragMode::Scale) {
        float dist = Length(Vec2{world.x - pivot_.x, world.y - pivot_.y});
        float f = startDist_ > 1.0f ? dist / startDist_ : 1.0f;
        if (snap) f = std::max(0.25f, SnapTo(f, 0.25f)); // 0.25 步进档
        for (auto& [e, start] : dragTfs_) {
            if (!ctx.ActiveScene().Alive(e)) continue;
            ecs::Transform2D& tf = ctx.ActiveScene().Get<ecs::Transform2D>(e);
            Vec2 d = start.pos - pivot_;
            tf.pos = pivot_ + d * f;
            tf.scale = Vec2{start.scale.x * f, start.scale.y * f};
        }
    }
    ctx.dirty = true;
}

void SceneViewPanel::EndGizmoDrag(EditorApp& app) {
    // 属性轨：拖拽整段 = 一条记录/实体（before = BeginGizmoDrag 快照，§3.5 合并语义）
    EditorContext& ctx = app.Ctx();
    auto& reg = ecs::ComponentRegistry::Instance();
    if (!ctx.Playing()) {
        const ecs::ComponentMeta* tfMeta = reg.Find("Transform2D");
        for (auto& [e, start] : dragTfs_) {
            const ecs::Meta* m = ctx.ActiveScene().TryGet<ecs::Meta>(e);
            if (!m || !m->guid || !tfMeta) continue;
            std::vector<uint8_t> before((const uint8_t*)&start,
                                        (const uint8_t*)&start + sizeof(start));
            ctx.PushPropertyUndo("Transform 拖拽", m->guid, tfMeta->id, std::move(before),
                                 ctx.SnapshotComponent(e, tfMeta->id));
        }
    }
    drag_ = DragMode::None;
    dragTfs_.clear();
    clickPending_ = false;
}

void SceneViewPanel::FocusSelection(EditorApp& app, uint32_t rtW, uint32_t rtH) {
    // F：选中集包围盒 → 视野 60% 覆盖（zoom = clamp(min(rtH*0.6/h, rtW*0.6/w))）
    EditorContext& ctx = app.Ctx();
    ViewportRenderer& vr = app.Viewport();
    if (ctx.Selection().empty()) return;
    Rect bounds{{1e30f, 1e30f}, {-1e30f, -1e30f}};
    bool any = false;
    for (Entity e : ctx.Selection()) {
        Vec2 c, s;
        float rot = 0;
        if (!vr.WorldBoundsOf(ctx, e, c, s, rot)) continue;
        any = true;
        bounds.min.x = std::min(bounds.min.x, c.x - s.x * 0.5f);
        bounds.min.y = std::min(bounds.min.y, c.y - s.y * 0.5f);
        bounds.max.x = std::max(bounds.max.x, c.x + s.x * 0.5f);
        bounds.max.y = std::max(bounds.max.y, c.y + s.y * 0.5f);
    }
    if (!any) return;
    const float w = std::max(bounds.max.x - bounds.min.x, 32.0f);
    const float h = std::max(bounds.max.y - bounds.min.y, 32.0f);
    Camera2D& cam = vr.SceneCam();
    cam.center = (bounds.min + bounds.max) * 0.5f;
    float zoom = std::min((float)rtH * 0.6f / h, (float)rtW * 0.6f / w);
    cam.zoom = math::Clamp(zoom, 0.05f, 64.0f);
    cam.halfHeight = kRefHalfHeight / cam.zoom;
}

void SceneViewPanel::DrawGrid(ViewportRenderer& vr, const Camera2D& cam, uint32_t rtW,
                              uint32_t rtH) {
    const float aspect = (float)rtW / (float)rtH;
    const Rect v = cam.ViewRect(aspect);
    const float x0 = std::floor(v.min.x / kGridSpacing) * kGridSpacing;
    const float x1 = std::ceil(v.max.x / kGridSpacing) * kGridSpacing;
    const float y0 = std::floor(v.min.y / kGridSpacing) * kGridSpacing;
    const float y1 = std::ceil(v.max.y / kGridSpacing) * kGridSpacing;
    const float lineW = 1.5f / cam.zoom;
    const float cap = 512.0f; // 行数上限（极端缩小防护）
    for (float x = x0, i = 0; x <= x1 && i < cap; x += kGridSpacing, ++i)
        vr.PushOverlayLine(Vec2{x, v.min.y}, Vec2{x, v.max.y},
                           std::fmod(x, kGridSpacing * 4) == 0.0f ? kAxisColor : kGridColor,
                           lineW);
    for (float y = y0, i = 0; y <= y1 && i < cap; y += kGridSpacing, ++i)
        vr.PushOverlayLine(Vec2{v.min.x, y}, Vec2{v.max.x, y},
                           std::fmod(y, kGridSpacing * 4) == 0.0f ? kAxisColor : kGridColor,
                           lineW);
}

void SceneViewPanel::DrawGizmoHandles(EditorApp& app, ViewportRenderer& vr, ecs::Entity e,
                                      const Camera2D& cam, Vec2 c, Vec2 s, float rot) {
    const float px = 1.0f / cam.zoom;
    if (app.Tool() == EditTool::Move) {
        const float len = 44.0f * px;
        vr.PushOverlayLine(c, c + Vec2{len, 0}, 0xFF60E0A0u, 2.0f * px);  // +X 绿
        vr.PushOverlayLine(c, c + Vec2{0, len}, 0xFF70A0F0u, 2.0f * px);  // +Y 蓝
        vr.PushOverlayQuad(c, Vec2{10.0f * px, 10.0f * px}, 0, kHandleColor);
    } else if (app.Tool() == EditTool::Rotate) {
        // 圆环（32 段细线）
        const float r = std::max(s.x, s.y) * 0.5f + 18.0f * px;
        Vec2 prev = c + Vec2{r, 0};
        for (int i = 1; i <= 32; ++i) {
            float a = (float)i / 32.0f * math::kTau;
            Vec2 cur = c + Vec2{std::cos(a) * r, std::sin(a) * r};
            vr.PushOverlayLine(prev, cur, kHandleColor, 1.6f * px, 900);
            prev = cur;
        }
    } else { // Scale：四角手柄方块
        const float cs = std::cos(rot), sn = std::sin(rot);
        const Vec2 h{s.x * 0.5f, s.y * 0.5f};
        const Vec2 corners[4] = {
            Vec2{c.x + cs * h.x - sn * h.y, c.y + sn * h.x + cs * h.y},
            Vec2{c.x + cs * h.x + sn * h.y, c.y + sn * h.x - cs * h.y},
            Vec2{c.x - cs * h.x + sn * h.y, c.y - sn * h.x - cs * h.y},
            Vec2{c.x - cs * h.x - sn * h.y, c.y - sn * h.x + cs * h.y}};
        for (Vec2 k : corners)
            vr.PushOverlayQuad(k, Vec2{12.0f * px, 12.0f * px}, rot, kHandleColor, 901);
    }
    (void)e;
}

// -------------------------------------------------------------- GameView --
void GameViewPanel::OnGui(EditorApp& app) {
    if (!ImGui::Begin("Game", nullptr, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }
    ViewportRenderer& vr = app.Viewport();
    ImGui::BeginChild("gv", ImVec2(0, 0), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.x >= 16 && avail.y >= 16) {
        const float dpi = app.Ui().DisplayScale();
        void* tex = vr.EnsureRenderTarget(1, (uint32_t)(avail.x * dpi),
                                          (uint32_t)(avail.y * dpi), "gameRT");
        Camera2D& cam = vr.GameCam();
        cam.halfHeight = kRefHalfHeight / cam.zoom;
        // letterbox：保持 16:9 游戏视野（宽高比模拟 M5；此处保基准视野不变形）
        float imgW = avail.x, imgH = avail.y;
        const float gameAspect = 1280.0f / 720.0f;
        if (imgW / imgH > gameAspect) imgW = imgH * gameAspect;
        else imgH = imgW / gameAspect;
        const ImVec2 off((avail.x - imgW) * 0.5f, (avail.y - imgH) * 0.5f);
        ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + off.x, ImGui::GetCursorPosY() + off.y));
        ImGui::Image(tex, ImVec2(imgW, imgH), ImVec2(0, 0), ImVec2(1, 1));
        // §3.6 门控：聚焦（且悬停）时键鼠进 Play World；失焦不进
        app.SetGameViewFocused(ImGui::IsWindowFocused() && ImGui::IsWindowHovered());
        if (app.Ctx().Playing()) {
            ImGui::SetCursorPos(ImVec2(6, 6));
            ImGui::TextDisabled("%s", ImGui::IsWindowFocused() && ImGui::IsWindowHovered()
                                         ? "输入已路由至 Play World（WASD/空格）"
                                         : "点击聚焦后键鼠进游戏");
        }
    }
    ImGui::EndChild();
    ImGui::End();
}

} // namespace lemon::editor
