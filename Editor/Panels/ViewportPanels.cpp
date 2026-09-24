// Lemon 编辑器 — SceneView / GameView 面板（M4.md §2.2；内核 #11/#13 消费端）
// SceneView：编辑相机（pan/zoom/F 自愈）+ 拾取（单选/Ctrl 多选）+ Gizmo 三态
// （移动十字/旋转圈/四角缩放）+ 网格吸附 + overlay 注入（网格/选框/Gizmo 手柄）。
// GameView：游戏相机离屏（编辑态也实时显示）；输入门控 M4.3。
// Gizmo 拖拽 = Transform 直写 + dirty；Undo 属性轨 M4.3 接入（拖拽合并）。
#include <algorithm>
#include <cmath>
#include <cstring>

#include "App/EditorApp.h"
#include "App/ImGuiBackend.h"
#include "Core/Math.h"
#include "Components/RenderComponents.h"
#include "ECS/Hierarchy.h"
#include "EditorContext.h"
#include "Interaction/ViewportRenderer.h"
#include "Panels/BuiltInPanels.h"
#include "imgui.h"
#include "imgui_internal.h" // GImGui（smoke-drag 诊断：ActiveId 归属）

namespace lemon::editor {

using ecs::Entity;

namespace {

constexpr float kRefHalfHeight = 360.0f; // 编辑相机 zoom=1 基准半高
constexpr float kGridSpacing = 32.0f;    // 网格间距（世界 px）
constexpr float kSnapPos = 8.0f;         // 平移吸附档
constexpr float kSnapRotDeg = 15.0f;     // 旋转吸附档
// overlay 颜色统一经 PackRGBA（与 sprite 着色同通道，避免字节序歧义）；
// 值定义于 Interaction/ViewportRenderer.h overlay::（EditorApp 冒烟像素断言共用）
const uint32_t kSelColor = overlay::kSelectColor;      // 选框青
const uint32_t kPrimaryColor = overlay::kPrimaryColor; // 主选亮青
const uint32_t kGridColor = overlay::kGridColor;       // 网格
const uint32_t kAxisColor = overlay::kAxisColor;       // 主轴稍亮
const uint32_t kHandleColor = overlay::kHandleColor;   // Gizmo 手柄黄
const uint32_t kHoverColor = math::PackRGBA(235, 235, 235, 90); // hover 轮廓（白淡）
// Move 轴向色（Unity 语义：X 红 / Y 绿；hover 提亮）
inline uint32_t AxisColor(bool xAxis, bool hovered) {
    return xAxis ? math::PackRGBA(235, 90, 80, hovered ? 255 : 220)
                 : math::PackRGBA(100, 210, 110, hovered ? 255 : 220);
}

float SnapTo(float v, float step) { return std::round(v / step) * step; }

// 轴对齐选框：四条边吸附到 RT 像素中心、线宽恒 1 物理像素——无 AA、颜色恒定
// （亚像素位置的 1.8px 线会被 AA 摊薄到任何像素都取不到纯色 → 冒烟 sel 断言抖动；
// 旋转框不吸附，仍走 AA 路径）。网格 v4 同一套吸附逻辑。
void PushOverlayRectSnapped(ViewportRenderer& vr, const Camera2D& cam, uint32_t rtW,
                            uint32_t rtH, Vec2 c, Vec2 s, uint32_t rgba, int16_t order) {
    const float aspect = (float)rtW / (float)rtH;
    const Rect v = cam.ViewRect(aspect);
    const float wppx = (v.max.x - v.min.x) / (float)rtW;
    const float wppy = (v.max.y - v.min.y) / (float)rtH;
    auto snapEdge = [&](float edge, float origin, float wpp) {
        return origin + (std::floor((edge - origin) / wpp) + 0.5f) * wpp;
    };
    const float x0 = snapEdge(c.x - s.x * 0.5f, v.min.x, wppx);
    const float x1 = snapEdge(c.x + s.x * 0.5f, v.min.x, wppx);
    const float y0 = snapEdge(c.y - s.y * 0.5f, v.min.y, wppy);
    const float y1 = snapEdge(c.y + s.y * 0.5f, v.min.y, wppy);
    vr.PushOverlayLine(Vec2{x0, y0}, Vec2{x1, y0}, rgba, wppy, order);
    vr.PushOverlayLine(Vec2{x1, y0}, Vec2{x1, y1}, rgba, wppx, order);
    vr.PushOverlayLine(Vec2{x1, y1}, Vec2{x0, y1}, rgba, wppy, order);
    vr.PushOverlayLine(Vec2{x0, y1}, Vec2{x0, y0}, rgba, wppx, order);
}

} // namespace

// -------------------------------------------------------------- SceneView --
void SceneViewPanel::OnGui(EditorApp& app) {
    if (!ImGui::Begin("Scene", nullptr,
                      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
                          ImGuiWindowFlags_NoScrollWithMouse)) {
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
    // SDL3 后端透传窗口“点”坐标，而 ScreenToWorld/锚点按 RT“像素”归一——
    // DPI≠1（Retina）时不换算会让拾取/拖拽/鼠标锚点整体偏移（点→像素 = rtW/avail）
    const float ptToPx = (float)rtW / std::max(1.0f, avail.x);
    const Vec2 mousePx{mouseRel.x * ptToPx, mouseRel.y * ptToPx};
    const bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    const Vec2 mouseWorld = vr.ScreenToWorld(cam, mousePx, rtW, rtH);
    lastMouseRel_ = Vec2{mouseRel.x, mouseRel.y};
    vpX_ = imagePos.x; vpY_ = imagePos.y; vpW_ = avail.x; vpH_ = avail.y;
    rtW_ = rtW; rtH_ = rtH;

    // ---- 相机：滚轮缩放（前推 = 放大）----
    // （WantCaptureKeyboard 不可作门：ImGui 1.92 语义 = 有窗口持有焦点即真，编辑器内
    // 恒真——曾经因此滚轮缩放/点击拾取全灭，M4.7 手测抓到；输入态由 WantTextInput 表达）
    // 锚点：有主选中 = 对象中心（缩放前后屏幕位置不动，Godot/Unity 手感）；
    // 无选中 = 鼠标点。ImGui MouseWheel 正值 = 前推。
    if (hovered && io.MouseWheel != 0.0f && !io.WantTextInput) {
        Vec2 anchorPt{mousePx.x, mousePx.y}; // 屏幕锚点（RT px），默认 = 鼠标
        if (!ctx.Primary().IsNull()) {
            Vec2 c, s;
            float r = 0;
            if (vr.WorldBoundsOf(ctx, ctx.Primary(), c, s, r)) {
                const Vec2 a = vr.WorldToScreen(cam, c, rtW, rtH);
                // 仅当对象在视野附近（±25% 余量）才锚其中心。视野外对象若仍锚定，
                // 每格滚轮把相机向它拖 (1-1/k)·距离——父链变换爆炸的实体（Inspector
                // 显示的是局部值）一格就能甩出十万量级（手测第四轮 center=(1.2e4,1.4e5)）
                const float mx = 0.25f * (float)rtW, my = 0.25f * (float)rtH;
                if (a.x >= -mx && a.x <= (float)rtW + mx && a.y >= -my &&
                    a.y <= (float)rtH + my)
                    anchorPt = a; // 选中对象中心 → 屏幕点（Godot/Unity 手感）
            }
        }
        const Vec2 before = vr.ScreenToWorld(cam, anchorPt, rtW, rtH);
        cam.zoom = math::Clamp(cam.zoom * std::exp(io.MouseWheel * 0.12f), 0.05f, 64.0f);
        cam.halfHeight = kRefHalfHeight / cam.zoom;
        const Vec2 after = vr.ScreenToWorld(cam, anchorPt, rtW, rtH);
        cam.center += before - after; // 锚点下世界点不动
    }
    // ---- 相机：平移（主手势 = 按住右键拖动；辅 = 中键 / 空格+左键 / Alt+左键）----
    // M4.7 手测修复：①门控原用裸 IsWindowHovered()——与上面缩放同款问题（有
    // ActiveId 持活时恒假），改用同源 hovered；②macOS 触控板/妙控鼠标没有中键，
    // 右键拖拽为无修饰主手势（Scene 视口无右键菜单，无冲突）。场景无固定大小：
    // 起拖后不再要求悬停，拖出视口边缘仍持续平移到松键（对象拖出屏后拖视口找回）。
    const bool panMod = ImGui::IsKeyDown(ImGuiKey_Space) || io.KeyAlt;
    if (!panning_ && hovered && !io.WantTextInput &&
        (ImGui::IsMouseClicked(ImGuiMouseButton_Right) ||
         ImGui::IsMouseClicked(ImGuiMouseButton_Middle) ||
         (panMod && ImGui::IsMouseClicked(ImGuiMouseButton_Left))))
        panning_ = true;
    if (panning_) {
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Right) &&
            !ImGui::IsMouseDown(ImGuiMouseButton_Middle) &&
            !(panMod && ImGui::IsMouseDown(ImGuiMouseButton_Left))) {
            panning_ = false; // 全部平移键已松开
        } else {
            ImVec2 d = ImGui::GetIO().MouseDelta;
            const float wppX = cam.HalfWidth((float)rtW / (float)rtH) * 2.0f / (float)rtW;
            const float wppY = cam.halfHeight * 2.0f / (float)rtH;
            cam.center.x -= d.x * ptToPx * wppX; // MouseDelta=点 → 换 RT 像素再乘世界/像素
            cam.center.y -= d.y * ptToPx * wppY;
        }
    }
    if (panning_ || (panMod && hovered))
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); // 平移反馈/可发现性

    // ---- Gizmo / 拾取状态机（M4.7c：一段式 + 4px 阈值 + 轴约束 + Esc 取消）----
    // 同上：不再看 WantCaptureKeyboard（1.92 = 窗口焦点恒真；真在输入由 WantTextInput 拦）。
    // 平移修饰（空格/Alt）按住时左键归平移——不拾取不清选（Photoshop 式手型语义）
    const bool canInteract = hovered && !io.WantTextInput && !panMod;
    if (dbgTrace_) // 逐帧视口矩形（smoke-drag 稳定性排查；仅注入帧区间开启）
        std::printf("[vp] f-rel=(%.0f,%.0f) vp=(%.0f,%.0f %.0fx%.0f) rt=%ux%u\n",
                    mouseRel.x, mouseRel.y, vpX_, vpY_, vpW_, vpH_, rtW_, rtH_);
    hoverAxis_ = AxisHint::None; // 每帧重拾（拖拽中锁定 dragAxis_）
    hoverKX_ = hoverKY_ = 0;
    if (drag_ == DragMode::None && canInteract && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const Entity picked = vr.Pick(ctx, mouseWorld);
        AxisHint axis = AxisHint::None;
        DragMode forced = DragMode::None; // Select 模式的手柄命中直接定模式
        if (!ctx.Primary().IsNull() && app.Tool() == EditTool::Move) {
            Vec2 c, s;
            float rot = 0;
            if (vr.WorldBoundsOf(ctx, ctx.Primary(), c, s, rot))
                axis = HitTestMoveHandles(c, mouseWorld, cam); // 手柄命中 > 实体本体
            if (axis != AxisHint::None) forced = DragMode::Move;
        } else if (!ctx.Primary().IsNull() && app.Tool() == EditTool::Select &&
                   ctx.Selection().size() == 1) {
            // 8 向手柄仅在单选时激活（多选 = 移动/旋转合体框语义，不提供合拉）
            Vec2 c, s;
            float rot = 0;
            if (vr.WorldBoundsOf(ctx, ctx.Primary(), c, s, rot) &&
                HitTestSelectHandles(c, s, rot, mouseWorld, cam, resizeKX_, resizeKY_))
                forced = DragMode::Resize; // 8 向手柄：对侧锚定调整大小
        }
        if (!picked.IsNull() || forced != DragMode::None) {
            const Entity target = forced != DragMode::None ? ctx.Primary() : picked;
            dbgPress_++; // smoke-drag 诊断（拖拽失效分段定位）
            dbgHover_ = hovered;
            dbgCanInteract_ = canInteract;
            dbgPickedNull_ = 0;
            ctx.Select(target, io.KeyCtrl);
            if (ctx.IsSelected(target)) { // Ctrl 点已选项 = 取消选中 → 不 arm
                DragMode dm = forced;
                if (dm == DragMode::None) // 本体拖拽：Select/Move = 移动，其余按工具
                    dm = app.Tool() == EditTool::Rotate ? DragMode::Rotate
                         : app.Tool() == EditTool::Scale ? DragMode::Scale
                                                         : DragMode::Move;
                BeginGizmoDrag(app, target, mouseWorld,
                               axis != AxisHint::None ? axis : AxisHint::Free, dm);
            } else
                clickPending_ = false;
        } else {
            clickPending_ = true; // 空白按下：抬起时清选（Ctrl 除外）
        }
    }
    if (drag_ != DragMode::None) {
        const Vec2 d{mouseRel.x - dragScreenStart_.x, mouseRel.y - dragScreenStart_.y};
        if (!dragActive_ && Length(d) >= 4.0f) {
            dragActive_ = true; // D5 阈值：点 vs 拖
            dbgActiveEver_ = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            CancelGizmoDrag(app); // 恢复快照，不入 Undo
        } else if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            EndGizmoDrag(app, dragActive_); // 真拖过才入 Undo；纯点击 = 选择已生效
        } else if (dragActive_) {
            UpdateGizmoDrag(app, mouseWorld);
        }
    } else if (clickPending_ && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        clickPending_ = false;
        if (canInteract && !io.KeyCtrl) ctx.ClearSelection();
    }
    // F 聚焦：不要求悬停 Scene 窗口（层级面板选中后直接按 F 即可——hover 门曾致
    // 相机甩飞后“选中还在却永远找不回”；文本输入中不抢键）
    if (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F, false))
        FocusSelection(app, rtW, rtH);

    // ---- overlay 注入（Render 前；世界空间）----
    const float lineW = 1.2f / cam.zoom;
    if (app.GridVisible()) DrawGrid(vr, cam, rtW, rtH); // v4：像素吸附 + 主轴（纯视觉）
    for (Entity e : ctx.Selection())
        if (e != ctx.Primary()) {
            Vec2 c, s;
            float rot = 0;
            if (vr.WorldBoundsOf(ctx, e, c, s, rot)) {
                if (std::fabs(std::cos(rot)) > 0.999f)
                    PushOverlayRectSnapped(vr, cam, rtW, rtH, c, s, kSelColor, 1000);
                else
                    vr.PushOverlayRect(c, s, rot, kSelColor, lineW);
            }
        }
    { // 主选：选框 + Gizmo 手柄 + hover 轮廓（非拖拽时空白悬停实体亮边）
        Entity p = ctx.Primary();
        Vec2 c, s;
        float rot = 0;
        if (!p.IsNull() && vr.WorldBoundsOf(ctx, p, c, s, rot)) {
            if (std::fabs(std::cos(rot)) > 0.999f)
                PushOverlayRectSnapped(vr, cam, rtW, rtH, c, s, kPrimaryColor, 1000);
            else
                vr.PushOverlayRect(c, s, rot, kPrimaryColor, lineW * 1.5f);
            DrawGizmoHandles(app, vr, p, cam, c, s, rot); // Play 中也可编辑（落 Play 世界；Undo 侧已守卫）
        }
    }
    if (drag_ == DragMode::None && canInteract && !ctx.Primary().IsNull()) {
        // 手柄 hover 高亮 + 光标（Move 轴：EW/NS；Select 8 向：NS/EW/NWSE/NESW；旋转：手型）
        Vec2 c, s;
        float rot = 0;
        if (vr.WorldBoundsOf(ctx, ctx.Primary(), c, s, rot)) {
            if (app.Tool() == EditTool::Move) {
                hoverAxis_ = HitTestMoveHandles(c, mouseWorld, cam);
                if (hoverAxis_ == AxisHint::X) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
                else if (hoverAxis_ == AxisHint::Y) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
                else if (hoverAxis_ == AxisHint::Free) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            } else if (app.Tool() == EditTool::Select && ctx.Selection().size() == 1 &&
                       HitTestSelectHandles(c, s, rot, mouseWorld, cam, hoverKX_, hoverKY_)) {
                if (hoverKX_ == 0) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
                else if (hoverKY_ == 0) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
                else if (hoverKX_ == hoverKY_)
                    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNWSE);
                else ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNESW);
            }
        }
    }
    if (drag_ == DragMode::None && canInteract) { // hover 轮廓（发现性根基；框选 M5 铺底）
        const Entity hoverEnt = vr.Pick(ctx, mouseWorld);
        if (!hoverEnt.IsNull() && !ctx.IsSelected(hoverEnt)) {
            Vec2 c, s;
            float rot = 0;
            if (vr.WorldBoundsOf(ctx, hoverEnt, c, s, rot)) {
                if (std::fabs(std::cos(rot)) > 0.999f)
                    PushOverlayRectSnapped(vr, cam, rtW, rtH, c, s, kHoverColor, 1000);
                else
                    vr.PushOverlayRect(c, s, rot, kHoverColor, lineW);
            }
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
        // 资产拖入（M4.4）：sprite = 光标处建实体；prefab = 光标处实例化
        if (const ImGuiPayload* pay = ImGui::AcceptDragDropPayload("LemonAsset")) {
            AssetDragPayload d{};
            std::memcpy(&d, pay->Data, sizeof(d));
            const std::string before = ctx.SnapshotSceneJson();
            Entity ne = Entity::Null();
            if (d.kind == 0)
                ne = ctx.CreateSpriteEntityFromAsset("Sprite", d.guid, mouseWorld);
            else if (d.kind == 1)
                ne = ctx.InstantiatePrefabAsset(d.guid, mouseWorld);
            if (!ne.IsNull()) {
                ctx.Select(ne, false);
                if (!ctx.Playing()) ctx.PushStructuralUndo("资产拖入视口", before);
            }
        }
        ImGui::EndDragDropTarget();
    }
    // 视口角标
    ImGui::SetCursorPos(ImVec2(6, 6));
    ImGui::TextDisabled("zoom %.2fx  center (%.0f, %.0f)  visible %u", cam.zoom, cam.center.x,
                        cam.center.y, vr.LastSceneVisible());
    if (vr.LastSceneVisible() == 0 && !ctx.Primary().IsNull())
        ImGui::TextDisabled("选中对象在视野外 —— 按 F 聚焦");

    ImGui::EndChild();
    ImGui::End();
}

void SceneViewPanel::BeginGizmoDrag(EditorApp& app, Entity /*primary*/, Vec2 world,
                                    AxisHint axis, DragMode forced) {
    EditorContext& ctx = app.Ctx();
    dbgArmed_++; // smoke-drag 诊断
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
    drag_ = forced != DragMode::None
              ? forced
              : app.Tool() == EditTool::Move ? DragMode::Move
            : app.Tool() == EditTool::Rotate ? DragMode::Rotate
            : app.Tool() == EditTool::Scale  ? DragMode::Scale
                                             : DragMode::Move; // Select 本体 = 移动
    dragAxis_ = drag_ == DragMode::Move ? axis : AxisHint::Free;
    dragActive_ = false;           // 4px 阈值内 = 点击语义（D5）
    dragScreenStart_ = lastMouseRel_;
    startAngle_ = std::atan2(world.y - pivot_.y, world.x - pivot_.x);
    startDist_ = Length(Vec2{world.x - pivot_.x, world.y - pivot_.y});
    if (drag_ == DragMode::Resize) { // 对侧手柄锚定数据（Godot 式 8 向调整）
        if (Entity p = ctx.Primary(); !p.IsNull()) {
            Vec2 c, s;
            float r = 0;
            if (app.Viewport().WorldBoundsOf(ctx, p, c, s, r)) {
                selHalf0_ = Vec2{s.x * 0.5f, s.y * 0.5f};
                selRot_ = r;
                // 对侧手柄世界点（锚定不动）：c + R(rot)·((−kx,−ky)∘半尺寸)
                {
                    const float acs = std::cos(r), asn = std::sin(r);
                    selAnchor_ =
                        c + Vec2{acs * (-resizeKX_) * selHalf0_.x - asn * (-resizeKY_) * selHalf0_.y,
                                 asn * (-resizeKX_) * selHalf0_.x + acs * (-resizeKY_) * selHalf0_.y};
                }
                // 世界缩放 = 本地缩放 ⊙ 父链缩放（Hierarchy TRS 复合语义）
                const ecs::Transform2D* tf = ctx.ActiveScene().TryGet<ecs::Transform2D>(p);
                Vec2 ws = tf ? Vec2{std::max(1e-3f, std::fabs(tf->scale.x)),
                                    std::max(1e-3f, std::fabs(tf->scale.y))}
                             : Vec2{1, 1};
                selHasPw_ = false;
                selPw_ = ecs::WorldTransform2D{};
                if (const ecs::Hierarchy* h = ctx.ActiveScene().TryGet<ecs::Hierarchy>(p);
                    h && !h->parent.IsNull() && ctx.ActiveScene().Alive(h->parent)) {
                    if (ecs::ComputeWorldTransform(ctx.ActiveScene(), h->parent, selPw_)) {
                        selHasPw_ = true;
                        ws.x *= std::max(1e-3f, std::fabs(selPw_.scale.x));
                        ws.y *= std::max(1e-3f, std::fabs(selPw_.scale.y));
                    }
                }
                selWorldScale0_ = ws;
                // 鼠标起点相对锚点的本地投影（比例跟随基准；arm 时 = 2×半宽）
                Vec2 d{world.x - selAnchor_.x, world.y - selAnchor_.y};
                const float cs = std::cos(r), sn = std::sin(r);
                selD0_ = Vec2{cs * d.x + sn * d.y, -sn * d.x + cs * d.y};
            }
        }
    }
}

void SceneViewPanel::UpdateGizmoDrag(EditorApp& app, Vec2 world) {
    EditorContext& ctx = app.Ctx();
    dbgUpdates_++; // smoke-drag 诊断
    // 吸附默认关（Godot/Unity 丝滑手感）；按住 Ctrl 拖拽 = 临时取反
    const bool snap = app.SnapEnabled() != ImGui::GetIO().KeyCtrl;
    if (drag_ == DragMode::Move) {
        Vec2 delta{world.x - dragStart_.x, world.y - dragStart_.y};
        if (dragAxis_ == AxisHint::X) delta.y = 0.0f;   // 轴约束：只动 x / 只动 y
        else if (dragAxis_ == AxisHint::Y) delta.x = 0.0f;
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
    } else if (drag_ == DragMode::Resize) {
        // Godot 式 8 向：对侧手柄锚定 + 比例跟随（arm 时鼠标=手柄 → f=1 不跳变，
        // 之后手柄 1:1 跟随鼠标）。缩放硬钳 [~0.01, 200]（下限 = 2 世界 px 半宽）：
        // 极缩放下 1pt = 数十世界 px，无钳制时连拖几次即乘到天文数字（实测 pos 4.3e7）。
        constexpr float kMaxScale = 200.0f;
        Vec2 d{world.x - selAnchor_.x, world.y - selAnchor_.y};
        const float cs = std::cos(selRot_), sn = std::sin(selRot_);
        d = Vec2{cs * d.x + sn * d.y, -sn * d.x + cs * d.y}; // R(−rot)
        float hx = selHalf0_.x, hy = selHalf0_.y;
        if (resizeKX_ != 0)
            hx = selHalf0_.x * (d.x * resizeKX_) / std::max(2.0f, selD0_.x * resizeKX_);
        if (resizeKY_ != 0)
            hy = selHalf0_.y * (d.y * resizeKY_) / std::max(2.0f, selD0_.y * resizeKY_);
        const Vec2 halfPerScale{selHalf0_.x / selWorldScale0_.x, selHalf0_.y / selWorldScale0_.y};
        hx = std::clamp(hx, 2.0f, kMaxScale * halfPerScale.x);
        hy = std::clamp(hy, 2.0f, kMaxScale * halfPerScale.y);
        if (snap) { // 8px 档（与平移吸附同格）
            hx = std::clamp(SnapTo(hx, kSnapPos), 2.0f, kMaxScale * halfPerScale.x);
            hy = std::clamp(SnapTo(hy, kSnapPos), 2.0f, kMaxScale * halfPerScale.y);
        }
        const Vec2 f{hx / selHalf0_.x, hy / selHalf0_.y};
        // 世界意图：中心 = 锚 + R(rot)·((kx,ky)∘新半尺寸)；再逆父链变换落本地
        // （子实体此前直接把世界值写进本地 pos —— 父链放大器，爆炸主因之一）
        const Vec2 halfNew{selHalf0_.x * f.x, selHalf0_.y * f.y};
        const Vec2 wc{selAnchor_.x + cs * resizeKX_ * halfNew.x - sn * resizeKY_ * halfNew.y,
                      selAnchor_.y + sn * resizeKX_ * halfNew.x + cs * resizeKY_ * halfNew.y};
        for (auto& [e, start] : dragTfs_) {
            if (!ctx.ActiveScene().Alive(e)) continue;
            ecs::Transform2D& tf = ctx.ActiveScene().Get<ecs::Transform2D>(e);
            if (selHasPw_) {
                const float pcs = std::cos(selPw_.rot), psn = std::sin(selPw_.rot);
                const float psx = std::max(1e-3f, std::fabs(selPw_.scale.x));
                const float psy = std::max(1e-3f, std::fabs(selPw_.scale.y));
                const Vec2 off{wc.x - selPw_.pos.x, wc.y - selPw_.pos.y};
                // lo = R(−pw.rot)(off)；local = lo ⊘ pw.scale（TRS 复合之逆）
                const Vec2 lo{pcs * off.x + psn * off.y, -psn * off.x + pcs * off.y};
                tf.pos = Vec2{selPw_.pos.x + (pcs * (lo.x / psx) - psn * (lo.y / psy)),
                              selPw_.pos.y + (psn * (lo.x / psx) + pcs * (lo.y / psy))};
            } else {
                tf.pos = wc;
            }
            tf.scale = Vec2{start.scale.x * f.x, start.scale.y * f.y};
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

void SceneViewPanel::EndGizmoDrag(EditorApp& app, bool dragged) {
    EditorContext& ctx = app.Ctx();
    if (dragged) { // 属性轨：拖拽整段 = 一条记录/实体（before = 起点快照，§3.5 合并）
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
    } // 纯点击（阈值内）= 选择已生效，无 Undo
    drag_ = DragMode::None;
    dragActive_ = false;
    dragTfs_.clear();
    clickPending_ = false;
}

void SceneViewPanel::CancelGizmoDrag(EditorApp& app) {
    // Esc：恢复起点快照（不入 Undo——世界回到拖拽前，无净变更）
    EditorContext& ctx = app.Ctx();
    for (auto& [e, start] : dragTfs_) {
        if (!ctx.ActiveScene().Alive(e)) continue;
        ctx.ActiveScene().Get<ecs::Transform2D>(e) = start;
    }
    drag_ = DragMode::None;
    dragActive_ = false;
    dragTfs_.clear();
    clickPending_ = false;
    ctx.dirty = true;
}

void SceneViewPanel::FocusSelection(EditorApp& app, uint32_t rtW, uint32_t rtH) {
    // F：选中集包围盒 → 视野 60% 覆盖（zoom = clamp(min(rtH*0.6/h, rtW*0.6/w))）；
    // 空选中 = 全部可绘制实体（迷路/甩飞后的“回家”键）
    EditorContext& ctx = app.Ctx();
    ViewportRenderer& vr = app.Viewport();
    Rect bounds{{1e30f, 1e30f}, {-1e30f, -1e30f}};
    bool any = false;
    auto acc = [&](Entity e) {
        Vec2 c, s;
        float rot = 0;
        if (!vr.WorldBoundsOf(ctx, e, c, s, rot)) return;
        any = true;
        bounds.min.x = std::min(bounds.min.x, c.x - s.x * 0.5f);
        bounds.min.y = std::min(bounds.min.y, c.y - s.y * 0.5f);
        bounds.max.x = std::max(bounds.max.x, c.x + s.x * 0.5f);
        bounds.max.y = std::max(bounds.max.y, c.y + s.y * 0.5f);
    };
    if (ctx.Selection().empty()) {
        for (auto [ent, tf, sr] :
             ctx.ActiveScene().View<ecs::Transform2D, ecs::SpriteRenderer>().each()) {
            (void)tf;
            (void)sr;
            acc(ecs::Scene::FromEntt(ent));
        }
    } else {
        for (Entity e : ctx.Selection()) acc(e);
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
    // 网格 v4（Godot 样式，手测反馈第二轮）：网格线吸附到 RT 像素中心且宽度恒等于
    // 1 个物理像素——任何缩放下等宽等距（v3 的 1/zoom 世界宽线在极缩放下半像素
    // 覆盖，α 网格几乎隐形 → "格子忽大忽小/忽有忽无"）。原点主轴 Godot 语义
    // X 红 / Y 绿，同宽不透明。
    const float aspect = (float)rtW / (float)rtH;
    const Rect v = cam.ViewRect(aspect);
    float minor = kGridSpacing;                 // 32px 基准
    while (minor * cam.zoom < 14.0f) minor *= 2.0f;  // 缩远：翻倍间距防过密
    while (minor * cam.zoom > 72.0f && minor > 4.0f) minor *= 0.5f; // 拉近：减半加密
    const float major = minor * 4.0f;
    const float wppx = (v.max.x - v.min.x) / (float)rtW; // 每物理像素的世界宽（等比）
    const uint32_t kAxisXRGBA = math::PackRGBA(255, 72, 64, 255);   // 原点 X 轴红
    const uint32_t kAxisYRGBA = math::PackRGBA(110, 210, 100, 255); // 原点 Y 轴绿
    auto vline = [&](float wx, uint32_t col) {
        const float sx = std::floor((wx - v.min.x) / wppx) + 0.5f; // 像素中心
        const float x = v.min.x + sx * wppx;
        vr.PushOverlayLine(Vec2{x, v.min.y}, Vec2{x, v.max.y}, col, wppx);
    };
    auto hline = [&](float wy, uint32_t col) {
        const float wppy = (v.max.y - v.min.y) / (float)rtH;
        const float sy = std::floor((wy - v.min.y) / wppy) + 0.5f;
        const float y = v.min.y + sy * wppy;
        vr.PushOverlayLine(Vec2{v.min.x, y}, Vec2{v.max.x, y}, col, wppy);
    };
    const float cap = 600.0f; // 线预算上限（极端缩远的护栏；自适应密度下正常远低于此）
    auto lineColor = [&](float coord) {
        if (std::fmod(std::fabs(coord), major) < minor * 0.25f) return kAxisColor; // major
        return kGridColor;                                            // minor（主轴见下方覆盖线）
    };
    uint32_t n = 0;
    for (float x = std::floor(v.min.x / minor) * minor; x <= v.max.x && n < cap;
         x += minor, ++n)
        vline(x, lineColor(x));
    for (float y = std::floor(v.min.y / minor) * minor; y <= v.max.y && n < cap;
         y += minor, ++n)
        hline(y, lineColor(y));
    // 原点主轴：Godot 语义 X 轴（y=0 横线）红 / Y 轴（x=0 竖线）绿；吸附后偏差 ≤ 0.5 像素
    hline(0.0f, kAxisXRGBA);
    vline(0.0f, kAxisYRGBA);
}

SceneViewPanel::AxisHint SceneViewPanel::HitTestMoveHandles(Vec2 c, Vec2 world,
                                                            const Camera2D& cam) const {
    // 屏幕常量命中带：轴段宽 7px、中心块 9px（世界尺寸 = px/zoom）
    const float px = 1.0f / cam.zoom;
    const float L = 44.0f * px;
    auto distToSeg = [](Vec2 p, Vec2 a, Vec2 b) {
        Vec2 d{b.x - a.x, b.y - a.y};
        float t = std::clamp(((p.x - a.x) * d.x + (p.y - a.y) * d.y) / (d.x * d.x + d.y * d.y),
                             0.0f, 1.0f);
        return Length(Vec2{a.x + d.x * t - p.x, a.y + d.y * t - p.y});
    };
    if (Length(Vec2{world.x - c.x, world.y - c.y}) < 9.0f * px) return AxisHint::Free;
    if (distToSeg(world, c, c + Vec2{L, 0}) < 7.0f * px) return AxisHint::X;
    if (distToSeg(world, c, c + Vec2{0, L}) < 7.0f * px) return AxisHint::Y;
    return AxisHint::None;
}

bool SceneViewPanel::HitTestSelectHandles(Vec2 c, Vec2 size, float rot, Vec2 world,
                                          const Camera2D& cam, int8_t& kx, int8_t& ky) const {
    // 8 向手柄（Godot 式）：4 角 + 4 边中点，屏幕常量 8px 半径
    const float px = 1.0f / cam.zoom;
    const Vec2 h{size.x * 0.5f, size.y * 0.5f};
    const float cs = std::cos(rot), sn = std::sin(rot);
    static constexpr int8_t kDirs[8][2] = {{1, 1},   {1, -1},  {-1, 1},  {-1, -1},
                                           {1, 0},   {-1, 0},  {0, 1},   {0, -1}};
    for (const auto& d : kDirs) {
        const Vec2 hp{c.x + cs * d[0] * h.x - sn * d[1] * h.y,
                      c.y + sn * d[0] * h.x + cs * d[1] * h.y};
        if (Length(Vec2{world.x - hp.x, world.y - hp.y}) < 8.0f * px) {
            kx = d[0];
            ky = d[1];
            return true;
        }
    }
    return false;
}

void SceneViewPanel::DrawGizmoHandles(EditorApp& app, ViewportRenderer& vr, ecs::Entity e,
                                      const Camera2D& cam, Vec2 c, Vec2 s, float rot) {
    const float px = 1.0f / cam.zoom;
    if (app.Tool() == EditTool::Move) {
        // M4.7c：Unity 语义轴箭头（X 红 / Y 绿）+ 箭头头部 + 中心块（自由拖）；
        // hover/drag 命中轴提亮（dragAxis_ 拖拽中锁定高亮，hoverAxis_ 平时）
        const AxisHint active = drag_ == DragMode::None ? hoverAxis_ : dragAxis_;
        const float len = 44.0f * px;
        auto arrow = [&](bool xAxis) {
            const bool hot = active == (xAxis ? AxisHint::X : AxisHint::Y);
            const uint32_t col = AxisColor(xAxis, hot);
            const float w = (hot ? 3.2f : 2.2f) * px;
            Vec2 dir = xAxis ? Vec2{1, 0} : Vec2{0, 1};
            vr.PushOverlayLine(c, c + dir * len, col, w, 900);
            // 箭头头（等腰三角两条边）
            Vec2 tip = c + dir * (len + 8.0f * px);
            Vec2 perp{xAxis ? Vec2{0, 1} : Vec2{1, 0}};
            vr.PushOverlayLine(tip - dir * 8.0f * px - perp * 5.0f * px, tip, col, w, 901);
            vr.PushOverlayLine(tip - dir * 8.0f * px + perp * 5.0f * px, tip, col, w, 901);
        };
        arrow(true);
        arrow(false);
        vr.PushOverlayQuad(c, Vec2{(active == AxisHint::Free ? 13.0f : 10.0f) * px,
                                   (active == AxisHint::Free ? 13.0f : 10.0f) * px},
                           0, kHandleColor, 902);
    } else if (app.Tool() == EditTool::Select && app.Ctx().Selection().size() == 1) {
        // Godot 式 8 向手柄：白圈红点（4 角 + 4 边中点）；hover 放大（仅单选）
        const Vec2 h{s.x * 0.5f, s.y * 0.5f};
        const float cs = std::cos(rot), sn = std::sin(rot);
        static constexpr int8_t kDirs[8][2] = {{1, 1},   {1, -1},  {-1, 1},  {-1, -1},
                                               {1, 0},   {-1, 0},  {0, 1},   {0, -1}};
        const uint32_t kDotWhite = math::PackRGBA(245, 245, 245, 255);
        const uint32_t kDotRed = math::PackRGBA(235, 70, 60, 255);
        for (const auto& d : kDirs) {
            const bool hot = hoverKX_ == d[0] && hoverKY_ == d[1];
            const Vec2 hp{c.x + cs * d[0] * h.x - sn * d[1] * h.y,
                          c.y + sn * d[0] * h.x + cs * d[1] * h.y};
            vr.PushOverlayQuad(hp, Vec2{(hot ? 15.0f : 12.0f) * px, (hot ? 15.0f : 12.0f) * px},
                               rot, kDotWhite, 903);
            vr.PushOverlayQuad(hp, Vec2{(hot ? 10.0f : 8.0f) * px, (hot ? 10.0f : 8.0f) * px},
                               rot, kDotRed, 904);
        }
    } else if (app.Tool() == EditTool::Rotate) {
        // 圆环（32 段细线；拖拽中锁定高亮）
        const float r = std::max(s.x, s.y) * 0.5f + 18.0f * px;
        const bool hot = drag_ == DragMode::Rotate;
        const uint32_t col = hot ? math::PackRGBA(255, 240, 150, 255) : kHandleColor;
        Vec2 prev = c + Vec2{r, 0};
        for (int i = 1; i <= 32; ++i) {
            float a = (float)i / 32.0f * math::kTau;
            Vec2 cur = c + Vec2{std::cos(a) * r, std::sin(a) * r};
            vr.PushOverlayLine(prev, cur, col, hot ? 2.4f * px : 1.6f * px, 900);
            prev = cur;
        }
    } else { // Scale：四角手柄方块（角块命中走实体本体；块体拖拽中放大）
        const float cs = std::cos(rot), sn = std::sin(rot);
        const Vec2 h{s.x * 0.5f, s.y * 0.5f};
        const float hs = (drag_ == DragMode::Scale ? 15.0f : 12.0f) * px;
        const Vec2 corners[4] = {
            Vec2{c.x + cs * h.x - sn * h.y, c.y + sn * h.x + cs * h.y},
            Vec2{c.x + cs * h.x + sn * h.y, c.y + sn * h.x - cs * h.y},
            Vec2{c.x - cs * h.x + sn * h.y, c.y - sn * h.x + cs * h.y},
            Vec2{c.x - cs * h.x - sn * h.y, c.y - sn * h.x - cs * h.y}};
        for (Vec2 k : corners)
            vr.PushOverlayQuad(k, Vec2{hs, hs}, rot, kHandleColor, 901);
    }
    (void)e;
}

// -------------------------------------------------------------- GameView --
void GameViewPanel::OnGui(EditorApp& app) {
    if (!ImGui::Begin("Game", nullptr,
                      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
                          ImGuiWindowFlags_NoScrollWithMouse)) {
        ImGui::End();
        return;
    }
    ViewportRenderer& vr = app.Viewport();
    // M4.7c：Aspect 下拉（Free/16:9/4:3/1:1）——替代固定 16:9 letterbox
    static const char* kAspects[] = {"Free", "16:9", "4:3", "1:1"};
    ImGui::SetNextItemWidth(88.0f);
    ImGui::Combo("##aspect", &aspectIdx_, kAspects, 4);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", "游戏视野宽高比（letterbox）");
    ImGui::SameLine();
    ImGui::TextDisabled("Aspect");
    ImGui::BeginChild("gv", ImVec2(0, 0), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.x >= 16 && avail.y >= 16) {
        const float dpi = app.Ui().DisplayScale();
        // 先算 letterbox 显示矩形，RT 按**显示矩形**的像素尺寸建 → 1:1 呈现。
        // （2026-09-21 手测第九轮修复：曾 RT=整面板尺寸 + Image 压进 16:9 矩形，
        // 相机每帧亚像素移动 = 整幅画面逐帧重采样 = 「全屏抖动/不平顺」+ 比例畸变）
        float imgW = avail.x, imgH = avail.y;
        if (aspectIdx_ != 0) { // Free = 铺满；其余按比例 letterbox 居中
            static const float kRatio[] = {0.0f, 16.0f / 9.0f, 4.0f / 3.0f, 1.0f};
            const float gameAspect = kRatio[aspectIdx_];
            if (imgW / imgH > gameAspect) imgW = imgH * gameAspect;
            else imgH = imgW / gameAspect;
        }
        void* tex = vr.EnsureRenderTarget(1, (uint32_t)(imgW * dpi),
                                          (uint32_t)(imgH * dpi), "gameRT");
        Camera2D& cam = vr.GameCam();
        cam.halfHeight = kRefHalfHeight / cam.zoom;
        const ImVec2 off((avail.x - imgW) * 0.5f, (avail.y - imgH) * 0.5f);
        ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + off.x, ImGui::GetCursorPosY() + off.y));
        ImGui::Image(tex, ImVec2(imgW, imgH), ImVec2(0, 0), ImVec2(1, 1));
        // §3.6 门控：聚焦（且悬停）时键鼠进 Play World；失焦不进
        app.SetGameViewFocused(ImGui::IsWindowFocused() && ImGui::IsWindowHovered());
        if (app.Ctx().Playing()) {
            ImGui::SetCursorPos(ImVec2(6, 6));
            ImGui::TextDisabled("%s", ImGui::IsWindowFocused() && ImGui::IsWindowHovered()
                                         ? "输入已路由至 Play World（WASD/空格；对话框=点击或数字键）"
                                         : "点击聚焦后键鼠进游戏");
            // M5 批①：Game RT UI 通道——C# Lemon.Ui.Set 写 World.RtUi 定长槽，
            // Play 时叠画在游戏画面左上角（M8 完整 HUD 前的最小形态；frac≥0 附进度条）
            // M5 批④：color 非 0 时文本与进度条着色（ABGR；模板血条/经验/计时惯例色）
            const lemon::ecs::RtUiChannel& rt = app.Ctx().ActiveWorld().RtUi();
            for (uint32_t i = 0; i < rt.Count(); ++i) {
                const lemon::ecs::RtUiSlot& slot = rt.At(i);
                ImGui::SetCursorPos(ImVec2(off.x + 10.0f, off.y + 26.0f + i * 24.0f));
                const bool colored = slot.color != 0;
                if (colored) ImGui::PushStyleColor(ImGuiCol_Text, slot.color);
                ImGui::TextUnformatted(slot.text);
                if (colored) ImGui::PopStyleColor();
                if (slot.frac >= 0.0f) {
                    ImGui::SameLine();
                    if (colored) ImGui::PushStyleColor(ImGuiCol_PlotHistogram, slot.color);
                    ImGui::ProgressBar(slot.frac, ImVec2(120.0f, 10.0f), "");
                    if (colored) ImGui::PopStyleColor();
                }
            }
            // M5 批④：三选一卡片（升级选择；Ui.ShowCards 写入、按钮/数字键回写 pick、
            // C# Ui.CardPick 消费式读）。居中半透明面板——模拟侧惯例已 Time.Scale=0 冻结
            lemon::ecs::RtUiCards& cards = app.Ctx().ActiveWorld().Cards();
            if (cards.active) {
                const bool gvFocus = ImGui::IsWindowFocused() && ImGui::IsWindowHovered();
                const ImVec2 center(off.x + imgW * 0.5f, off.y + imgH * 0.42f);
                ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
                ImGui::SetNextWindowBgAlpha(0.88f);
                ImGui::Begin("##rtui-cards", nullptr,
                             ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoScrollbar |
                                 ImGuiWindowFlags_AlwaysAutoResize);
                ImGui::TextUnformatted(cards.title);
                ImGui::Separator();
                ImGui::Spacing();
                // 空标签槽不渲染（批④后修④：Ui.ShowDialog 单按钮对话框 = B/C 留空）
                bool anyBtn = false;
                for (int i = 0; i < 3; ++i) {
                    if (cards.labels[i][0] == '\0') continue;
                    if (anyBtn) ImGui::SameLine();
                    ImGui::PushID(i);
                    if (ImGui::Button(cards.labels[i], ImVec2(200.0f, 64.0f)))
                        cards.pick = i; // 点击回写（消费归 C# CardPick）
                    ImGui::PopID();
                    anyBtn = true;
                }
                ImGui::Spacing();
                ImGui::TextDisabled("%s", "点击或按数字键选择（对话框 = 1）");
                // 数字键选择（Game 面板或卡片窗任一聚焦即生效；模拟已冻结无输入冲突；
                // 空槽键位无效——单按钮对话框按 2/3 不写越界 pick）
                if (gvFocus || ImGui::IsWindowFocused())
                    for (int k = 0; k < 3; ++k)
                        if (cards.labels[k][0] != '\0' &&
                            ImGui::IsKeyPressed((ImGuiKey)(ImGuiKey_1 + k))) cards.pick = k;
                ImGui::End();
            }
        }
    }
    ImGui::EndChild();
    ImGui::End();
}

} // namespace lemon::editor
