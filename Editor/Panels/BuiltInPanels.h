// Lemon 编辑器 — 内建面板声明（实现分布见同名 .cpp；CreateAllPanels 统一注册）
#pragma once

#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

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
    static bool PassFilter(ecs::Scene& s, ecs::Entity e, const char* filter);
    static bool SubtreeMatches(ecs::Scene& s, ecs::Entity e, const char* filter);

    std::string filter_;
    std::unordered_set<uint64_t> openedOnce_;
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
    // 属性轨空闲缓存（key = guid ⊕ compId<<48；空闲帧刷新，交互结束帧作 before）
    uint64_t idleKey_ = 0;
    std::vector<uint8_t> idleSnap_;
};

/// SceneView（M4.2）：编辑相机 + 拾取 + Gizmo 三态 + 网格吸附（§2.2）
class SceneViewPanel final : public IEditorPanel {
public:
    const char* Name() const override { return "Scene"; }
    void OnGui(EditorApp& app) override;

private:
    enum class DragMode { None, Move, Rotate, Scale };
    void BeginGizmoDrag(EditorApp& app, ecs::Entity primary, Vec2 world);
    void UpdateGizmoDrag(EditorApp& app, Vec2 world, uint32_t rtW, uint32_t rtH);
    void EndGizmoDrag(EditorApp& app);
    void FocusSelection(EditorApp& app, uint32_t rtW, uint32_t rtH);
    void DrawGrid(ViewportRenderer& vr, const Camera2D& cam, uint32_t rtW, uint32_t rtH);
    void DrawGizmoHandles(EditorApp& app, ViewportRenderer& vr, ecs::Entity e,
                          const Camera2D& cam, Vec2 c, Vec2 s, float rot);

    DragMode drag_ = DragMode::None;
    bool clickPending_ = false;
    Vec2 dragStart_{}, dragLast_{}, pivot_{};
    float startAngle_ = 0.0f, startDist_ = 0.0f;
    std::vector<std::pair<ecs::Entity, ecs::Transform2D>> dragTfs_; // 拖拽起点快照
};

/// GameView（M4.2）：游戏相机离屏；聚焦输入门控 M4.3（§3.6）
class GameViewPanel final : public IEditorPanel {
public:
    const char* Name() const override { return "Game"; }
    void OnGui(EditorApp& app) override;
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
};

} // namespace lemon::editor
