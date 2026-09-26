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
#include "Assets/ClipEdit.h" // ClipData（AnimationPanel 编辑态副本）
#include "Assets/Csv.h" // TableData（AssetBrowserPanel .tab 表格区缓存）
#include "Components/CoreComponents.h"
#include "ECS/ComponentRegistry.h"
#include "ECS/Entity.h"
#include "Panels/Panel.h"
#include "Renderer/Camera2D.h"

namespace lemon::editor {

using renderer::Camera2D;

class ViewportRenderer;

// 性能批② ui 段探针（LEMON_BENCH_UI_PROBE 开启时 HierarchyPanel 累计；
// --bench-survivor 裁决打印"占 ui 段百分比"）。零开关零成本：环境变量不存在
// 时 OnGui 内只剩一次 bool 读。
struct UiPanelProbe {
    double totalMs = 0.0;
    uint64_t frames = 0;
};
UiPanelProbe HierarchyPanelProbe();

class HierarchyPanel final : public IEditorPanel {
public:
    const char* Name() const override { return "Hierarchy"; }
    void OnGui(EditorApp& app) override;

private:
    /// 单行绘制（树节点 + 图标 + 选择/重命名/拖拽/右键菜单）；返回节点展开态。
    /// 行体与子遍历拆分 = 万级平铺列表可走 ImGuiListClipper 只画可见行（性②）
    bool DrawNodeRow(EditorApp& app, ecs::Entity e);
    void DrawNode(EditorApp& app, ecs::Entity e, bool hasHierarchy);
    void StartRename(ecs::Scene& s, ecs::Entity e);  // M4.6 §4-7：F2/右键/叶子双击进入
    void CommitRename(EditorApp& app, ecs::Entity e, bool apply);
    static bool PassFilter(ecs::Scene& s, ecs::Entity e, const char* filter);
    static bool SubtreeMatches(ecs::Scene& s, ecs::Entity e, const char* filter);

    std::string filter_;
    std::unordered_set<uint64_t> openedOnce_;
    std::vector<ecs::Entity> rootCache_; // 根收集复用容量（性②：每帧 Each 一次）
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
    void DrawArraySeg(EditorApp& app, const ecs::ComponentMeta& meta, void* comp,
                      bool& anyActive, bool& anyDeactivated);

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
    bool panning_ = false; // 视口平移中（起拖后不要求悬停——拖出边缘仍平移到松键）
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
    uint8_t kind;                 // 0=sprite 1=prefab 2=script 3=generic 4=clip（M5 批③）
                                  // 5=table（M6a 批②；暂无消费者，占位防后续重编）
};

class AssetBrowserPanel final : public IEditorPanel {
public:
    const char* Name() const override { return "Assets"; }
    void OnGui(EditorApp& app) override;

    /// 当前浏览目录（"" = Assets/ 根；M4.6 §5-3 拖拽导入落点）
    const std::string& CurrentDir() const { return currentDir_; }

private:
    void DrawItem(EditorApp& app, const AssetEntry& e);
    void DrawTableArea(EditorApp& app, const AssetEntry& e); // .tab 内嵌表格区（M6a 批② T1）
    void RenderTableGrid(EditorApp& app, const AssetEntry& e); // 内嵌区/浮动窗共用的表格渲染
    void DrawTableEditorWindow(EditorApp& app); // .tab 浮动放大编辑器（按需工具窗）

    std::string currentDir_ = ""; // "" = Assets/ 根
    std::string filter_;
    int typeFilter_ = 0;          // 类型过滤（T3b-9）：0 全部/1 图/2 动画/3 Prefab/4 表/5 脚本
    uint64_t renamingGuid_ = 0;   // 0 = 无重命名进行中
    std::string renameBuf_;

    // ---- .tab 表格区（M6a 批② T1 / ADR-012 D1）----
    uint64_t selectedGuid_ = 0;  // 单击选中（表格区只在选中 Table 条目时长出）
    bool tableOpen_ = true;      // CollapsingHeader 开合（空间预留用上一帧值）
    TableData table_;            // 缓存网格（键 = guid + 内容 hash，写回后失效重读）
    uint64_t tableGuid_ = 0, tableHash_ = 0;
    int editRow_ = -1, editCol_ = -1; // 双击进入编辑的格（-1 = 无）
    bool editJustStarted_ = false;    // 编辑首帧抢键盘焦点（IME 输入前提）
    std::string editBuf_;
    std::string tableError_;     // 上次提交/解析的红字提示（成功即清）
    // 浮动放大编辑器（T1 反馈批：内嵌区可操作面不足——横向滚动 + 大编辑面）。
    // 按需工具窗，不进 CreateAllPanels/DockBuilder = 不破 05 §3 面板集冻结。
    bool tableWinOpen_ = false;
    uint64_t tableWinGuid_ = 0;  // 编辑目标（guid 稳定；条目被删自动关窗）
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

/// Animation（M6a 批② T3 + T3b）：.clip 帧动画编辑——05 §7 内容编辑器三件套
/// 之一。读-改-写 + 三条生产通道（T3b 用户实测反馈）：
///   A 文件夹多单图 → 一键建动画（右键文件夹/向导；整图引用 T3b-1）
///   B 单图切片（meta importer，Unity 式 + 面板内入口 T3b-3）
///   C 网格拖框选区间 → 追加/替换帧（Godot 式交互，T3b-4）
/// 帧列表 = 横排胶片带（拖拽重排/Ctrl 多选删，T3b-7）；LoopMode 三模式
///（Once/Loop/PingPong，T3b-2）。EnterPlay 快照语义 = 保存后下次 Enter Play 生效；
/// Play 中只读。双击 AssetBrowser 的 .clip 进入。
class AnimationPanel final : public IEditorPanel {
public:
    const char* Name() const override { return "Animation"; }
    /// 按需窗口（Unity 同款：双击资产/Window 菜单打开，不占默认布局位）
    bool OpenByDefault() const override { return false; }
    void OnGui(EditorApp& app) override;
    /// AssetBrowser 双击 clip → EditorApp::OpenAnimationEditor 的落点
    void SetTarget(uint64_t guid) { targetGuid_ = guid; }
    /// 文件夹右键"从此文件夹创建动画"入口：开面板 + 向导文件夹页预选（T3b-5）。
    /// destDir = 落点目录（AssetBrowser 当前浏览目录——决策 4：跟随当前目录）
    void StartCreateFromFolder(const std::string& relDir, const std::string& destDir);

private:
    void LoadFrom(const AssetDatabase& db, const AssetEntry& e); // 缓存键失效 → 重读
    void DrawCellImage(EditorApp& app, const AssetEntry* sheet, uint32_t cell,
                       float edge); // 切片号/整图 → 页缩略图直染（未切片 = 全幅 UV）
    bool TrySave(EditorApp& app, const AssetEntry& e); // 校验 + 原子写 + 主动 Rescan
    /// 新建落盘共用：Assets 下 dir/<name>.clip 写盘 + Rescan + SetTarget（T3b-5/6）
    bool TryCreateClip(EditorApp& app, const ClipData& c, const std::string& dir,
                       std::string& err);
    void DrawFilmstrip(EditorApp& app);      // 胶片带（T3b-7）
    void DrawSelectedFrameRow(EditorApp& app); // 选中帧编辑行（sheet/cell/删）
    void DrawWizard(EditorApp& app);         // 新建三通道向导（T3b-5/6）
    void DrawSheetPicker(EditorApp& app);    // 从精灵表加帧/选定区间弹窗（T3b-4）

    uint64_t targetGuid_ = 0;               // 编辑目标（0 = 未选）
    uint64_t loadedGuid_ = 0, loadedHash_ = 0; // 缓存键（外部改动/保存回读 = 重读）
    ClipData edit_;                         // 编辑态副本（ok=false = 坏档红字只读态）
    int fpsI_ = 8;                          // DragInt 镜像（1..60；schema 仍存 float）
    bool dirty_ = false;                    // 有未保存改动（关面板不拦——资产在 git）
    std::string saveMsg_;                   // 上次保存/校验结果（一行红/绿）
    bool saveOk_ = false;                   // saveMsg_ 的着色位（√ 绿 / × 红）
    // 播放预览：编辑器时钟推进（非确定无妨——纯预览，不进模拟/回放）
    bool previewing_ = false;
    double previewT0_ = 0.0;
    int previewFrame_ = 0; // 暂停位/手动步进
    // 胶片带（T3b-7）：当前选中帧 + Ctrl 多选集（有序去重；空 = 无多选）
    int selFrame_ = -1;
    std::vector<int> selSet_;
    // 新建向导（T3b-5/6）：三通道 + 落点（默认 = AssetBrowser 当前目录，可改）
    bool wizOpen_ = false;
    int wizTab_ = 0;              // 0 文件夹 / 1 精灵表 / 2 空白
    std::string wizDir_;          // 文件夹页源目录（"Assets/..."）
    std::string wizPath_;         // 落点目录（相对项目根；"Assets" = 根）
    std::string wizName_ = "new-clip";
    int wizFps_ = 8, wizLoop_ = 1;
    std::string wizErr_;
    std::vector<ClipFrame> wizFrames_; // 精灵表页已选定区间（[选定] 暂存）
    bool wizPicked_ = false;      // 精灵表页有暂存区间（显示来源摘要）
    // 从精灵表加帧（T3b-4）：追加/替换（pickCreate_=false）与向导选定（true）共用
    bool pickOpen_ = false, pickCreate_ = false;
    uint64_t pickGuid_ = 0;       // 目标精灵资产
    int pickCols_ = 8, pickRows_ = 1, pickCellW_ = 32, pickCellH_ = 32;
    int pickInput_ = 0;          // 切片输入法：0 = 格数（尺寸按图算）/ 1 = 像素
    bool pickHasRect_ = false, pickDrag_ = false; // 拖框选状态
    int pickR0_ = 0, pickC0_ = 0, pickR1_ = 0, pickC1_ = 0; // 框选格区间（含端点）
};

} // namespace lemon::editor
