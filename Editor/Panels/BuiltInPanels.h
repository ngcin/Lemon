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
#include "Tooling/FilePicker.h" // AnimationPanel 面板私有文件选择器（v3.1 选图）
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

/// Animation（M6a 批② T3/T3b/T3c + 工作台 v3 交互重设计）：.anim 帧动画编辑
/// ——05 §7 内容编辑器三件套之一。Godot SpriteFrames 式主从工作台（用户实测
/// 三轮反馈收敛）：左列段清单（inline 新建/改名、搜索、复制段——建段只输入
/// 名字，选帧回到右区做）+ 右区三层（帧操作/播放工具条 · 大预览自适应 ·
/// 胶片带+属性行）。帧操作键盘化（←/→ 移选 · Ctrl+←/→ 换序 · Del 删 ·
/// Ctrl+D 复制 · Space 播放，面板持焦点时经 CapturesGlobalKeys 仲裁吃键）；
/// 拖 Assets 精灵入胶片带格=换图、入带尾/预览=加帧；从精灵表对话框 =
/// 全选/点选序/缩放（Godot Select Frames 式）。数据模型（.anim/.override schema）
/// 与三通道向导/集弹窗边沿触发机制不变；EnterPlay 快照语义 = 保存后下次
/// Enter Play 生效；Play 中只读。双击 AssetBrowser 的 .anim/.override 进入。
class AnimationPanel final : public IEditorPanel {
public:
    const char* Name() const override { return "Animation"; }
    /// 按需窗口（Unity 同款：双击资产/Window 菜单打开，不占默认布局位）
    bool OpenByDefault() const override { return false; }
    void OnGui(EditorApp& app) override;
    /// 窗口（含子窗）持焦点时捕获 Delete/Ctrl+D（帧操作）——EditorApp 据此
    /// 跳过实体级同名键，防双触发（见 Panel.h / BuildShortcuts）
    bool CapturesGlobalKeys() const override { return keysFocused_; }
    /// AssetBrowser 双击 clip → EditorApp::OpenAnimationEditor 的落点
    void SetTarget(uint64_t guid) { targetGuid_ = guid; }
    /// 文件夹右键"从此文件夹创建动画"入口：开面板 + 向导文件夹页预选（T3b-5）。
    /// destDir = 落点目录（AssetBrowser 当前浏览目录——决策 4：跟随当前目录）
    void StartCreateFromFolder(const std::string& relDir, const std::string& destDir);
    /// 空白区右键"新建动画剪辑…"入口（v3.1）：向导空白页；面板内不再放"新建"
    /// 按钮——创建动作归 AssetBrowser（用户实测反馈）
    void StartCreateBlank(const std::string& destDir);
    // ---- T3c 动画集工作台 ----
    /// 打开 .override 集（浏览器双击 .override / OpenAnimationEditor 集归并解析落点）
    void OpenSet(uint64_t setGuid) { setGuid_ = setGuid; }
    /// 集模式下选中段（双击 .anim → 归属集解析后调；clipGuid 0 = 清选）
    void SelectSegment(uint64_t clipGuid) { segGuid_ = clipGuid; targetGuid_ = clipGuid; }
    /// 文件夹右键"新建动画集…"入口：开新建集弹窗（srcDir 非空 = 并建首段勾选）
    void StartCreateSet(const std::string& relDir, const std::string& destDir);
    /// 冒烟回归钩子：当前集模式目标（0 = 传统 clip 模式）——2026-09-27 修复
    /// "双击 .override 不开集"后补的程序化断言面（OpenAnimationEditor 直接分支）
    uint64_t SetGuidForTest() const { return setGuid_; }
    /// 工具条「从图片文件…」同款入口（T3-UX4 抽取：菜单与冒烟注入共用）
    void StartImageFilePick(EditorApp& app);
    /// 冒烟：多选计数（FilePicker.multiSel_ 大小）
    size_t PickerSelCountForTest() const { return picker_.MultiSelCountForTest(); }
    /// 冒烟：直走真实选择语义（Shift 范围/Ctrl 加选——修饰键不便注入）
    void PickerClickForTest(size_t idx, bool ctrl, bool shift) {
        picker_.ApplyMultiClickForTest(idx, ctrl, shift);
    }

private:
    void LoadFrom(const AssetDatabase& db, const AssetEntry& e); // 缓存键失效 → 重读
    void DrawCellImage(EditorApp& app, const AssetEntry* sheet, uint32_t cell,
                       float edge); // 切片号/整图 → 页缩略图直染（未切片 = 全幅 UV）
    bool TrySave(EditorApp& app, const AssetEntry& e); // 校验 + 原子写 + 主动 Rescan
    /// 统一保存（v3）：段 TrySave 成功 → 集模式连存集（一个按钮，替代双保存位）
    bool SaveAll(EditorApp& app);
    /// 新建落盘共用：Assets 下 dir/<name>.anim 写盘 + Rescan + SetTarget（T3b-5/6）
    bool TryCreateClip(EditorApp& app, const ClipData& c, const std::string& dir,
                       std::string& err);
    // ---- 帧操作原语（工具条/键盘/右键菜单三入口共用）----
    /// idx 后插入帧（idx=-1 = 末尾追加）并选中新帧
    void InsertFrameAfter(int idx, const ClipFrame& f);
    void DuplicateSelectedFrames(); // 复制选中帧插其后（多选 = 各自插后）
    void DeleteSelectedFrames();    // 删 selSet_（无多选 = selFrame_ 单帧）
    // ---- 右区三层 ----
    void DrawFrameToolbar(EditorApp& app, bool ro); // 帧操作+播放传输+fps/循环+保存
    void DrawPreview(EditorApp& app, bool ro, int shown); // 大预览（fit ≤512）+拖入加帧+信息角标
    void DrawFilmstrip(EditorApp& app, bool ro, int playFrame); // 胶片带（v3）
    void DrawSelectedFrameRow(EditorApp& app); // 属性行（多选批量/单帧 sheet+cell）
    void HandleKeys(bool ro); // 键盘帧操作（焦点/WantTextInput/弹窗守卫）
    void DrawWizard(EditorApp& app);         // 新建动画剪辑向导（三通道；入口 = AssetBrowser）
    // ---- 从精灵表添加帧 v3.1（用户实测反馈重做）：文件选择 → 选帧对话框 ----
    /// 开文件选择器选精灵图（createMode=true = 向导页产出 wizFrames_）
    void StartSheetPick(EditorApp& app, bool createMode);
    /// 文件选择器结果路由（v3.1 三流：精灵表单图 / 多图整图入帧 / 向导精灵表）。
    /// 项目内文件 → FindByPath（未登记则 Rescan 收编）；项目外 → ImportFile 落
    /// 到集目录/浏览器目录。内含 Rescan——调用后 OnGui 只用 guid 重查。
    void HandlePickerResult(EditorApp& app);
    /// 选帧对话框 v3.1（Godot Select Frames 式）：左图区（网格实时重绘 +
    /// InvisibleButton 覆盖捕获拖动——修复"拖框选变成拖窗口"）+ 右参数栏（按
    /// 块数/按像素，改动即清选区）+ 底行动态按钮。**添加时**才把网格写 .meta
    /// （SetGridSlice + Rescan + guid 重查）——分割全程所见即所得。
    void DrawSheetPicker(EditorApp& app);
    /// 从 .anim 复制帧：列表弹窗（v3.1 加帧通道之一）
    void DrawClipPickModal(EditorApp& app);
    /// 读 clip 资产帧表追加到当前编辑态（弹窗与拖 .anim 资产两入口共用）
    void AppendClipFrames(EditorApp& app, uint64_t clipGuid);
    // ---- T3c 动画集工作台 ----
    void LoadSetFrom(const AssetDatabase& db, const AssetEntry& e); // 集档缓存键失效 → 重读
    /// 集校验（段名非空唯一 / 段引用可解析）+ 原子写 + Rescan（调用后段/集指针失效）
    bool TrySaveSet(EditorApp& app, const AssetEntry& setEntry);
    /// .override 新建落盘（TryCreateClip 同款：撞路拒 + 墓碑复活 + Rescan + OpenSet）
    bool TryCreateSet(EditorApp& app, const std::string& dir, const std::string& name,
                      std::vector<AnimSetSeg> segs, std::string& err);
    /// 创建流程入口统一清编辑态（2026-09-27 热修②：面板常驻 PanelRegistry，
    /// 右键"新建…"弹窗背后不得照渲染上次打开的集/剪辑——历史数据误读源）
    void ResetEditingState();
    /// 左列 v3.2（Godot Animations 列）：集名行 + 图标工具条（新动画/改名/复制/
    /// 移除）+ 搜索 + 动画清单（inline 改名 + 右键菜单）+ 底部 inline 新建输入。
    /// width/height <0 = 该向填满（宽窗 = 竖列全高 230px；窄窗 = 顶部横条 38% 高）；
    /// sameLineAfter = 画完回右侧同行（宽窗并排；窄窗堆叠传 false）
    void DrawLeftColumn(EditorApp& app, float width, float height, bool sameLineAfter);
    /// 右区 v3.2：ro 横幅 → 工具条 → 预览条 → 帧网格 → 属性行（时钟推进在内）。
    /// 宿主为 ##right child——窄窗内容超宽出滚动条（v3.1 前直接裁切 = "右侧
    /// 空白不可编辑"的窄窗根因）
    void DrawRightArea(EditorApp& app, bool ro);
    /// inline 新建落点：空 .anim 落盘 + 入集 + 选中（输入框保持开 = 连续建段）
    void QuickCreateSegment(EditorApp& app);
    /// inline 改名提交：校验 → db.Rename 段文件 → 集段名同步 → TrySaveSet
    /// （失败保持输入开，setErr_ 提示）
    void CommitSegRename(EditorApp& app, int idx);
    /// 复制段：读源 .anim → 撞名后缀 -2.. 落盘 + 入集 + 选中
    void DuplicateSegment(EditorApp& app, size_t idx);
    void DrawSetModals(EditorApp& app);      // 新建集弹窗（边沿触发）

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
    int previewFrame_ = 0; // 暂停位/手动步进（v3：暂停态跟随选中帧）
    // 胶片带（v3）：当前选中帧 + 多选集 + Shift 范围锚 + 缩放
    int selFrame_ = -1;
    std::vector<int> selSet_;
    int selAnchor_ = -1;              // Shift 范围选锚（上次单选位）
    float stripEdge_ = 64.0f;         // 帧缩略图边长（Ctrl+滚轮 / ± 按钮，40..128）
    bool keysFocused_ = false;        // 本面板窗口（含子窗）持焦点（键捕获仲裁）
    // 新建向导（T3b-5/6）：三通道 + 落点（默认 = AssetBrowser 当前目录，可改）。
    // wizPending_ = 边沿触发（只在入口点击帧调一次 OpenPopup——每帧重调破坏
    // 弹窗栈序，T3b 修正批实测）
    bool wizOpen_ = false, wizPending_ = false;
    int wizTab_ = 0;              // 0 文件夹 / 1 精灵表 / 2 空白
    std::string wizDir_;          // 文件夹页源目录（"Assets/..."）
    std::string wizPath_;         // 落点目录（相对项目根；"Assets" = 根）
    std::string wizName_ = "new-clip";
    int wizFps_ = 8, wizLoop_ = 1;
    std::string wizErr_;
    std::vector<ClipFrame> wizFrames_; // 精灵表页已选帧（内嵌体产出）
    bool wizPicked_ = false;      // 精灵表页有产出（显示"已选 N 帧"摘要）
    // 从精灵表加帧（T3b-4 → v3.1 两段式：文件选择器 → 选帧对话框）；边沿触发
    bool pickOpen_ = false, pickPending_ = false; // 选帧对话框（第二段模态）
    uint64_t pickGuid_ = 0;       // 目标精灵资产
    int pickCols_ = 4, pickRows_ = 4, pickCellW_ = 32, pickCellH_ = 32;
    int pickInput_ = 0;          // 分割输入法：0 = 按块数 / 1 = 按像素 cell 尺寸
    bool pickHasRect_ = false, pickDrag_ = false; // 拖框选状态
    int pickR0_ = 0, pickC0_ = 0, pickR1_ = 0, pickC1_ = 0; // 框选格区间（含端点）
    float pickZoom_ = 1.0f;      // 表预览缩放（0.25..8 × fit 宽；Ctrl+滚轮/按钮）
    int pickOrderMode_ = 0;      // 0 = 拖框选（行优先）/ 1 = 点选（按点击序 = Godot As Selected）
    std::vector<uint32_t> pickSelCells_; // 点选序收集的 cell（有序去重）
    bool pickCreate_ = false;    // 选帧对话框产出流向：true = 向导 wizFrames_
    FilePicker picker_;          // 面板私有文件选择器（v3.1：选图入口）
    int pickFlow_ = 0;           // 0 无 / 1 精灵表单图（编辑态）/ 2 多图整图入帧 / 3 精灵表（向导）
    bool clipPickOpen_ = false, clipPickPending_ = false; // 从 .anim 复制列表弹窗
    // ---- T3c 动画集工作台（目标 = .override；段编辑复用上方 clip 机制——集模式下
    // targetGuid_ 指向当前段，setGuid_ 指向集容器）----
    uint64_t setGuid_ = 0;                    // 当前集（0 = 裸 clip 传统模式）
    uint64_t setLoadedGuid_ = 0, setLoadedHash_ = 0; // 集档缓存键
    AnimSetData setEdit_;                     // 集编辑态副本（ok=false = 坏档红字）
    uint64_t segGuid_ = 0;                    // 集模式当前选中段（0 = 未选）
    // 左列 v3：inline 新建（Enter 建段后输入保持开 = 连续建段）/ inline 改名 / 搜索
    bool segNewActive_ = false;
    std::string segNewName_;
    bool segNewFocus_ = false;      // 建段输入一次性抢焦点（Enter 提交后重新抢）
    int segEditIdx_ = -1;         // inline 改名目标段下标（-1 = 无）
    std::string segEditBuf_;
    bool segEditFocus_ = false;    // 改名输入一次性抢焦点
    std::string segFilter_;       // 段搜索子串（大小写不敏感；建段时自动清）
    std::string setMsg_;          // 集保存/校验结果行（左列底部）
    bool setOk_ = false;
    bool setCreateOpen_ = false, setCreatePending_ = false; // 新建集弹窗（边沿触发）
    std::string setCreateName_ = "player";
    std::string setCreateDir_;                // 落点目录（相对项目根）
    std::string setCreateSrc_;                // 源目录（非空 = 显示"并建首段"勾选）
    bool setCreateWithSeg_ = false;
    std::string setErr_;
};

} // namespace lemon::editor
