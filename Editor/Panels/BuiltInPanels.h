// Lemon 编辑器 — 内建面板声明（实现分布见同名 .cpp；CreateAllPanels 统一注册）
#pragma once

#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ECS/Hierarchy.h" // WorldTransform2D（Select resize 的父链世界变换缓存）

#include "Assets/AssetDatabase.h"
#include "Components/CoreComponents.h"
#include "ECS/ComponentRegistry.h"
#include "ECS/Entity.h"
#include "Panels/Panel.h"
#include "Renderer/Camera2D.h"

namespace lemon::editor {

using renderer::Camera2D;

class ViewportRenderer;

class HierarchyPanel final : public IEditorPanel {
public:
    const char* Name() const override { return "Hierarchy"; }
    void OnGui(EditorApp& app) override;

private:
    void DrawNode(EditorApp& app, ecs::Entity e, bool hasHierarchy);
    void StartRename(ecs::Scene& s, ecs::Entity e);  // M4.6 §4-7：F2/右键/叶子双击进入
    void CommitRename(EditorApp& app, ecs::Entity e, bool apply);
    static bool PassFilter(ecs::Scene& s, ecs::Entity e, const char* filter);
    static bool SubtreeMatches(ecs::Scene& s, ecs::Entity e, const char* filter);

    std::string filter_;
    std::unordered_set<uint64_t> openedOnce_;
    ecs::Entity renaming_{};    // 重命名中的实体（Null = 无）
    bool renameFocus_ = false; // 重命名输入框首帧聚焦
    std::string renameBuf_;
};

class InspectorPanel final : public IEditorPanel {
public:
    const char* Name() const override { return "Inspector"; }
    void OnGui(EditorApp& app) override;

private:
    void DrawComponent(EditorApp& app, const ecs::ComponentMeta& meta, ecs::Entity e);
    void DrawFields(EditorApp& app, const ecs::ComponentMeta& meta, void* comp, ecs::Entity e);
    void DrawArraySeg(EditorApp& app, const ecs::ComponentMeta& meta, const void* comp);

    std::unordered_set<uint64_t> openedHeaders_;
    // 属性轨空闲缓存（M4.7d 修：按 组件键→快照 多槽——原单槽被"排最后的组件"
    // 的空闲刷新覆盖，编辑首个组件时 key 永不匹配 → 控件编辑不进 Undo）
    std::unordered_map<uint64_t, std::vector<uint8_t>> idleSnaps_;
};

/// SceneView（M4.2）：编辑相机 + 拾取 + Gizmo 三态 + 网格吸附（§2.2）
/// M4.7c：一段式拖拽（4px 阈值，D5）/Move 轴约束/Esc 取消/hover 轮廓
class SceneViewPanel final : public IEditorPanel {
public:
    const char* Name() const override { return "Scene"; }
    void OnGui(EditorApp& app) override;

    // --smoke-drag 注入定位（M4.7c 交互回归）：上帧视口矩形/RT 尺寸 + 诊断计数
    float LastVpX() const { return vpX_; }
    float LastVpY() const { return vpY_; }
    float LastVpW() const { return vpW_; }
    float LastVpH() const { return vpH_; }
    uint32_t LastRtW() const { return rtW_; }
    uint32_t LastRtH() const { return rtH_; }
    // 拖拽链路诊断（失效时打印分段定位：按下→arm→阈值→Update→位移）
    int dbgPress_ = 0;        // IsMouseClicked 且 drag_==None 到达次数
    bool dbgHover_ = false;   // 最近一次按下时 hovered
    bool dbgCanInteract_ = false;
    int dbgPickedNull_ = -1;  // 最近一次按下 Pick 结果（0=命中 1=空）
    int dbgArmed_ = 0;        // BeginGizmoDrag 次数
    bool dbgActiveEver_ = false; // 4px 阈值达成过
    int dbgUpdates_ = 0;      // UpdateGizmoDrag 次数
    bool dbgTrace_ = false;   // 按住期间逐帧打印（smoke-drag 诊断开关）

private:
    enum class DragMode { None, Move, Rotate, Scale, Resize };
    enum class AxisHint : uint8_t { None = 0, X = 1, Y = 2, Free = 3 };
    void BeginGizmoDrag(EditorApp& app, ecs::Entity primary, Vec2 world, AxisHint axis,
                        DragMode forced = DragMode::None);
    void UpdateGizmoDrag(EditorApp& app, Vec2 world);
    void EndGizmoDrag(EditorApp& app, bool dragged);
    void CancelGizmoDrag(EditorApp& app);
    void FocusSelection(EditorApp& app, uint32_t rtW, uint32_t rtH);
    /// Move 手柄命中（屏幕常量尺寸）：X/Y 轴段或中心块；None = 未命中
    AxisHint HitTestMoveHandles(Vec2 c, Vec2 world, const Camera2D& cam) const;
    /// Select 模式 8 向手柄命中：4 角 + 4 边中点（屏幕 8px 半径）；命中写入符号
    /// （角 = 双非零 / 边 = 单非零），返回 false = 未命中
    bool HitTestSelectHandles(Vec2 c, Vec2 size, float rot, Vec2 world, const Camera2D& cam,
                              int8_t& kx, int8_t& ky) const;
    void DrawGrid(ViewportRenderer& vr, const Camera2D& cam, uint32_t rtW, uint32_t rtH);
    void DrawGizmoHandles(EditorApp& app, ViewportRenderer& vr, ecs::Entity e,
                          const Camera2D& cam, Vec2 c, Vec2 s, float rot);

    DragMode drag_ = DragMode::None;
    bool dragActive_ = false;      // 位移超 4px 阈值后才真正改 Transform（D5 防误触）
    AxisHint dragAxis_ = AxisHint::None; // Move 轴约束（X/Y/自由）
    AxisHint hoverAxis_ = AxisHint::None; // 手柄 hover 高亮（每帧拾取）
    int8_t resizeKX_ = 0, resizeKY_ = 0;  // Resize 拖拽的手柄方向（−1/0/1）
    int8_t hoverKX_ = 0, hoverKY_ = 0;    // Select 手柄 hover（画高亮 + 光标形状）
    Vec2 selHalf0_{};              // Resize 起点：半尺寸（世界 px）
    Vec2 selWorldScale0_{};        // 起点世界缩放（= 本地缩放 ⊙ 父链缩放）
    Vec2 selD0_{};                 // arm 时鼠标相对锚点的本地投影（比例跟随基准）
    Vec2 selAnchor_{};             // 对侧手柄世界点（锚定不动）
    float selRot_ = 0.0f;          // 起点世界旋转（本地轴投影用）
    ecs::WorldTransform2D selPw_{}; // 父链世界变换（根 = 恒等；世界意图 → 本地）
    bool selHasPw_ = false;
    Vec2 dragScreenStart_{};       // 屏幕像素（阈值判据用）
    Vec2 lastMouseRel_{};          // 最近帧鼠标（视口内像素；BeginGizmoDrag 取起点）
    bool clickPending_ = false;
    Vec2 dragStart_{}, dragLast_{}, pivot_{};
    float startAngle_ = 0.0f, startDist_ = 0.0f;
    std::vector<std::pair<ecs::Entity, ecs::Transform2D>> dragTfs_; // 拖拽起点快照
    float vpX_ = 0, vpY_ = 0, vpW_ = 0, vpH_ = 0; // 视口矩形（smoke-drag 注入）
    uint32_t rtW_ = 0, rtH_ = 0;
};

/// GameView（M4.2）：编辑相机离屏；聚焦输入门控 M4.3（§3.6）
/// M4.7c：Aspect 下拉（Free/16:9/4:3/1:1 letterbox）
class GameViewPanel final : public IEditorPanel {
public:
    const char* Name() const override { return "Game"; }
    void OnGui(EditorApp& app) override;

private:
    int aspectIdx_ = 1; // 0=Free 1=16:9 2=4:3 3=1:1
};

/// AssetBrowser（M4.4）：Assets/ 目录树 + 缩略图网格 + 拖拽（进 SceneView/Inspector
/// sprite 槽）+ 右键导入/重命名/删除（guid 稳定 → 引用不断，06 §2）
struct AssetDragPayload {         // "LemonAsset" 拖拽载荷（面板间约定）
    uint64_t guid;
    uint32_t spriteId;
    uint8_t kind;                 // 0=sprite 1=prefab 2=script
};

class AssetBrowserPanel final : public IEditorPanel {
public:
    const char* Name() const override { return "Assets"; }
    void OnGui(EditorApp& app) override;

    /// 当前浏览目录（"" = Assets/ 根；M4.6 §5-3 拖拽导入落点）
    const std::string& CurrentDir() const { return currentDir_; }

private:
    void DrawItem(EditorApp& app, const AssetEntry& e);

    std::string currentDir_ = ""; // "" = Assets/ 根
    std::string filter_;
    uint64_t renamingGuid_ = 0;   // 0 = 无重命名进行中
    std::string renameBuf_;
};

class ProfilerPanel final : public IEditorPanel {
public:
    const char* Name() const override { return "Profiler"; }
    void OnGui(EditorApp& app) override;

private:
    std::vector<float> frameMs_;
    bool showGpu_ = true;
    uint64_t gcPrev_ = 0; // GcAllocated 差分基线（M4.5 GC 红字口径）
};

} // namespace lemon::editor
