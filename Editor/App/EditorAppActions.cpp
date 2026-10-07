// Lemon 编辑器 — EditorApp 编辑动作（场景 IO/资产与动画入口/剪贴板/拖入导入；
// M4.2–M4.6）。批② 2026-09-29 自 EditorApp.cpp 机械拆分：成员函数跨 TU 定义，
// 类定义 App/EditorApp.h 零改动，代码逐行原样。
#include "App/EditorApp.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>

#include "Assets/AssetDatabase.h"
#include "Assets/AnimAsset.h" // M6a 批② T3：smoke-anim clip 编辑链（面板数据面同款）
#include "Components/CoreComponents.h"
#include "Core/Log.h"
#include "Core/FileOps.h" // Utf8ToAcp：SDL drop 路径（UTF-8）过 win fs（ACP）前归一
#include "EditorContext.h"
#include "Panels/BuiltInPanels.h"
#include "Ui/UiSubsystem.h" // 批③a（ADR-014）：游戏 UI 层（RmlUi）
#include "Serialization/SceneArchive.h"

namespace lemon::editor {

// ---- 场景 IO 动作 ----
void EditorApp::MenuNewScene() {
    if (ctx_.dirty && !ConfirmUnsaved(PendingSceneOp::NewScene)) return;
    ctx_.NewScene();
    LEMON_LOG("新建场景（untitled）");
}

void EditorApp::MenuOpenScene() {
    if (ctx_.dirty && !ConfirmUnsaved(PendingSceneOp::OpenScene)) return;
    pickerMode_ = PickerMode::Open;
    picker_.Open("打开场景", PickerStartDir(), "", ".scene");
}

// 场景选择器起始目录：当前场景父目录 → 项目 Scenes/ → 项目根 → CWD（无项目）。
// 新建场景无路径时此前退 CWD（= 启动目录），与打开的项目无关（2026-09-22 反馈）。
// 注意只能传存在的目录：FilePicker 对不存在的默认目录会自退 CWD。
std::string EditorApp::PickerStartDir() {
    std::error_code ec;
    const std::string cur = std::filesystem::path(ctx_.ScenePath()).parent_path().string();
    if (!cur.empty()) return cur;
    const std::string& root = ctx_.Assets().ProjectRoot();
    if (!root.empty()) {
        if (std::filesystem::is_directory(root + "/Scenes", ec)) return root + "/Scenes";
        return root; // 老项目无 Scenes/：退项目根（勿传不存在目录）
    }
    return std::filesystem::current_path(ec).string();
}

void EditorApp::MenuOpenRecentScene(std::string path) {
    if (ctx_.Playing()) {
        LEMON_WARN("Play 中不能切换场景（先 Stop）");
        return;
    }
    if (path == ctx_.ScenePath()) return; // 已是当前场景：无操作
    if (ctx_.dirty) { // 脏场景：确认后直达路径（模态期间持有）
        pendingScenePath_ = path;
        ConfirmUnsaved(PendingSceneOp::RecentScene);
        return;
    }
    ctx_.OpenScene(path);
}

void EditorApp::MenuSaveScene() {
    if (ctx_.ScenePath().empty()) {
        MenuSaveSceneAs();
        return;
    }
    ctx_.SaveScene();
}

void EditorApp::MenuSaveSceneAs() {
    pickerMode_ = PickerMode::Save;
    picker_.Open("另存场景", PickerStartDir(), ctx_.SceneName(), ".scene");
}

bool EditorApp::ConfirmUnsaved(PendingSceneOp after) {
    if (!ctx_.dirty) return true;
    quitConfirmOpen_ = true; // 复用确认模态（按上下文分流文案与去向）
    confirmContext_ = ConfirmContext::SceneOp;
    pendingSceneOp_ = after; // 保存/丢弃后续做（取消则作废）
    return false;            // 异步：模态按钮里推进（2026-09-21 补齐 M4.2 欠账）
}

// ---------------------------------------------------------------- 资产 ----
void EditorApp::MenuImportAsset() {
    // 无项目守卫：AssetsRoot() = "/Assets"（根_),拷贝必然失败且报错误导（M4.6 实测坑）
    if (ctx_.Assets().ProjectRoot().empty()) {
        LEMON_ERROR("导入失败：未打开项目。文件 → 新建项目... 或 打开项目...（也可 --project <dir> 启动）");
        return;
    }
    pickerMode_ = PickerMode::Import;
    picker_.Open("导入资产", ctx_.Assets().AssetsRoot(), "", ""); // 任意扩展名
}

void EditorApp::MenuOpenProject() {
    if (ctx_.Playing()) {
        LEMON_WARN("Play 中不能切换项目（先 Stop）");
        return;
    }
    if (ctx_.dirty && !ConfirmUnsaved(PendingSceneOp::OpenProject)) return; // 脏场景确认（同款异步环）
    // 起点目录：已开项目 → 其父目录（同级切换常见）；否则 HOME
    namespace fs = std::filesystem;
    std::error_code ec;
    std::string start;
    const std::string& cur = ctx_.Assets().ProjectRoot();
    if (!cur.empty()) start = fs::path(cur).parent_path().string();
    if (start.empty() || !fs::is_directory(start, ec)) {
        if (const char* home = std::getenv("HOME")) start = home;
        else start = fs::current_path(ec).string();
    }
    pickerMode_ = PickerMode::OpenProject;
    picker_.OpenDir("打开项目（选择项目目录）", start); // M4.6 §4-2：选目录而非 project.lemon
}

bool EditorApp::OpenProjectInSession(const std::string& root) {
    // 切项目落地（选择器/最近项目/引导卡共用）：管线 + 新会话场景。
    // Play/脏场景守卫在调用方（菜单入口已拦；此函数为最后一道防线）。
    if (ctx_.Playing()) {
        LEMON_WARN("Play 中不能切换项目（先 Stop）");
        return false;
    }
    if (!OpenProjectPipeline(root)) return false;
    if (ctx_.dirty) LEMON_WARN("切项目：场景有未保存更改，已被丢弃");
    ctx_.NewScene(); // 切项目 = 新会话场景（旧场景引用旧项目资产/脚本）
    return true;
}

void EditorApp::MenuSweepOrphanMetas() {
    // 手动清扫入口（2026-10-01 拍板）：与 Rescan 自动路径同判定（零引用删 / 被引用
    // 留），立即执行并弹报告——给"全部清除"一个人工决断口，但报告面不盲清被引用项。
    orphanSweepResult_ = ctx_.Assets().SweepOrphanMetas();
    orphanSweepReportOpen_ = true;
}

void EditorApp::RescanAssets() {
    AssetDatabase& db = ctx_.Assets();
    db.Rescan();
    // review 2026-10-02 #7：按值取走 + 立即 ConsumeChange——Remove() 预入队的
    // removed 事件此前被 Rescan 首行清空永不可达（GPU 幽灵页回收承诺落空）；
    // 现语义 = 保留至消费，取走即清零防跨重扫重复 Evict/重载
    const AssetDatabase::ChangeSet cs = db.LastChange();
    db.ConsumeChange();
    for (uint64_t g : cs.added)
        if (const AssetEntry* e = db.FindByGuid(g); e && e->type == AssetType::Sprite)
            gpuAssets_.ImportSprite(*e);
    for (uint64_t g : cs.modified)
        if (const AssetEntry* e = db.FindByGuid(g); e && e->type == AssetType::Sprite)
            gpuAssets_.ImportSprite(*e);
    // 批①：音频增量 → 后台烤制（sprite GPU 导入同款钩位；EnterPlay 只兜缺漏）
    for (uint64_t g : cs.added)
        if (const AssetEntry* e = db.FindByGuid(g); e && e->type == AssetType::Audio)
            EnqueueAudioBake(*e);
    for (uint64_t g : cs.modified)
        if (const AssetEntry* e = db.FindByGuid(g); e && e->type == AssetType::Audio)
            EnqueueAudioBake(*e);
    // M7c 批①：字体增量 → 后台烤制（音频同款钩位；装载侧 BakeStale 兜缺漏）
    bool fontChanged = false;
    for (uint64_t g : cs.added)
        if (const AssetEntry* e = db.FindByGuid(g); e && e->type == AssetType::Font) {
            EnqueueFontBake(*e);
            fontChanged = true;
        }
    for (uint64_t g : cs.modified)
        if (const AssetEntry* e = db.FindByGuid(g); e && e->type == AssetType::Font) {
            EnqueueFontBake(*e);
            fontChanged = true;
        }
    if (fontChanged) LoadFxFontPage(); // 当前 fxFont 变更 = 同步兜底烤 + 换页
    for (uint64_t g : cs.removed) gpuAssets_.Evict(g); // 幽灵页（号保留；M6 图集回收）
    // 批③b UI 文档/样式热重载（ADR-014 M2 DocumentReloaded 的编辑器侧半边；
    // C# 重灌数据事件归 ③c）。文档名 = 资产 relPath；.rcss 变更 = 逐文档
    // ReloadStyleSheet（保 DOM/状态 + 清 Factory 样式缓存——按路径缓存的解析
    // 结果不清则样式仍旧档，实测 2026-09-28）
    if (gameUi_ && !cs.Empty()) {
        bool anyRcss = false;
        auto uiHandle = [&](uint64_t g, bool removed) {
            const AssetEntry* e = db.FindByGuid(g);
            if (!e) return;
            if (e->type == AssetType::Rcss) {
                anyRcss = true;
                return;
            }
            if (e->type != AssetType::Rml) return;
            if (removed) {
                // 真人验收②观测位：逐出是否发生一目了然（docs 无此名 = no-op 也留痕）
                if (gameUi_->UnloadDocument(e->relPath.c_str()))
                    LEMON_LOG("UI 文档已卸载（资产删除）：%s", e->relPath.c_str());
            }
            else if (gameUi_->HasDocument(e->relPath.c_str()))
                gameUi_->ReloadDocument(e->relPath.c_str());
        };
        for (uint64_t g : cs.added) uiHandle(g, false);
        for (uint64_t g : cs.modified) uiHandle(g, false);
        for (uint64_t g : cs.removed) uiHandle(g, true);
        if (anyRcss) gameUi_->ReloadStyleSheets();
    }
    // 状态对账（无条件——cs 空也要跑：被裸 Rescan 吞掉事件的墓碑正是靠这拍自愈）
    ReconcileUiDocuments();
    db.SaveManifest();
    if (!cs.Empty())
        LEMON_LOG("资产重扫：+%zu ~%zu -%zu", cs.added.size(), cs.modified.size(),
                  cs.removed.size());
}

void EditorApp::OpenAnimationEditor(uint64_t guid) {
    // M6a 批② T3：首个"资产 → 专用编辑面板"通道（05 §5 先例）。按名取注册表
    // 条目（AnimationPanel 是按需窗口：open 置位 + 设目标；装载惰性在面板 OnGui）
    // T3c 集归并：双击 .anim 时若已属某 .override 集 → 开集工作台并选中该段（工作台
    // 是统一入口）；.override 直接开集。集数量小 + 双击频度低 → 现场解析可接受。
    if (PanelRegistry::Entry* en = panels_.FindEntry("Animation")) {
        auto* panel = static_cast<AnimationPanel*>(en->panel);
        en->open = true;
        AssetDatabase& db = ctx_.Assets();
        if (const AssetEntry* e = db.FindByGuid(guid);
            e && !e->missing && e->type == AssetType::Clip) {
            for (const AssetEntry& s : db.Entries()) {
                if (s.type != AssetType::AnimSet || s.missing) continue;
                std::ifstream f(db.AbsolutePath(s), std::ios::binary);
                if (!f) continue;
                std::string text(
                    (std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
                const assets::AnimSetData set = assets::ParseAnimSetJson(text);
                bool member = false;
                for (const assets::AnimSetSeg& sg : set.segments)
                    if (sg.clipGuid == guid) member = true;
                if (member) {
                    panel->OpenSet(s.guid);
                    panel->SelectSegment(guid);
                    return;
                }
            }
        }
        // .override 集容器：直接开集（2026-09-27 修复——此前误走 SetTarget 只设
        // targetGuid_，而面板集模式由 setGuid_ 驱动、剪辑模式要求 target 是 Clip：
        // 指向集资产的 targetGuid_ 两头不满足 → 空态文案；面板保留上次归并装载的
        // 集 = "显示历史内容"。曾在注释里宣称的"面板侧 OpenSet 语义"从未实现）
        if (const AssetEntry* se = db.FindByGuid(guid);
            se && !se->missing && se->type == AssetType::AnimSet) {
            panel->OpenSet(guid);
            panel->SelectSegment(0); // 装载后左列选首段（空集 = 清选）
            return;
        }
        // 裸 clip（T3-UX7 断路修复）：必须先清集态——SetTarget 只设 targetGuid_，
        // 而 OnGui 集模式优先（setGuid_ 非 0 时无视 targetGuid_）。曾因此：面板
        // 开着集时双击独立 clip"没反应"（集吃掉渲染）——真人实测报告。
        panel->OpenSet(0);
        panel->SetTarget(guid);
    }
}

void EditorApp::OpenAnimationCreateFromFolder(const std::string& relDir) {
    // M6a 批② T3b-5：文件夹右键入口——开向导文件夹页；落点跟随浏览器当前目录
    if (PanelRegistry::Entry* en = panels_.FindEntry("Animation")) {
        en->open = true;
        static_cast<AnimationPanel*>(en->panel)->StartCreateFromFolder(relDir,
                                                                       AssetBrowserDir());
    }
}

void EditorApp::OpenAnimationCreateSet(const std::string& relDir) {
    // M6a 批② T3c：空白区右键"新建动画集…"——开新建集弹窗（源目录随行，弹窗
    // 内可勾"并从源目录图片建首段"）；落点跟随浏览器当前目录。
    // （T3-UX7：文件夹右键的集入口已删——集都从 Animations 下建，用户实测定论）
    if (PanelRegistry::Entry* en = panels_.FindEntry("Animation")) {
        en->open = true;
        static_cast<AnimationPanel*>(en->panel)->StartCreateSet(relDir, AssetBrowserDir());
    }
}

void EditorApp::ClosePanel(const char* name) {
    if (PanelRegistry::Entry* en = panels_.FindEntry(name)) en->open = false;
}

const char* EditorApp::AssetBrowserDir() const {
    // ""（根）归一 "Assets"；assetPanel_ 未装配（极早期/无项目会话）兜底根
    static const char* kRoot = "Assets";
    if (!assetPanel_) return kRoot;
    const std::string& d = assetPanel_->CurrentDir();
    return (d.empty() || d == "Assets") ? kRoot : d.c_str();
}

// ------------------------------------------------ 项目/脚本管线（M4.5）----
// ---- M4.6b 日常编辑效率（§5）----
void EditorApp::CopySelection() {
    // §5-1：拷贝"选中子树的根"（祖先也在选中集内的跳过——整树由祖先携带）。
    // 树 JSON 经 SceneArchive（父子结构/组件全量；guid 由粘贴侧换新）
    entityClip_.clear();
    entityClipRootPos_.clear();
    ecs::Scene& s = ctx_.ActiveScene();
    for (ecs::Entity e : ctx_.Selection()) {
        if (e.IsNull() || !s.Alive(e)) continue;
        bool ancestorSelected = false;
        for (ecs::Entity a = e;;) {
            const ecs::Hierarchy* h = s.TryGet<ecs::Hierarchy>(a);
            if (!h || h->parent.IsNull() || !s.Alive(h->parent)) break;
            a = h->parent;
            if (ctx_.IsSelected(a)) {
                ancestorSelected = true;
                break;
            }
        }
        if (ancestorSelected) continue;
        entityClip_.push_back(ecs::SceneArchive::SaveEntityTree(s, e));
        Vec2 pos{0, 0};
        if (const ecs::Transform2D* t = s.TryGet<ecs::Transform2D>(e)) pos = t->pos;
        entityClipRootPos_.push_back(pos);
    }
    if (!entityClip_.empty())
        LEMON_LOG("已复制 %zu 个实体（子树结构随行，Ctrl+V 粘贴）", entityClip_.size());
}

void EditorApp::PasteClipboard() {
    if (entityClip_.empty() || ctx_.Playing()) return;
    const std::string before = ctx_.SnapshotSceneJson();
    ecs::Scene& s = ctx_.ActiveScene();
    bool any = false;
    for (size_t i = 0; i < entityClip_.size(); ++i) {
        ecs::Entity root = ecs::SceneArchive::LoadEntityTree(s, entityClip_[i]);
        if (root.IsNull()) continue;
        // 相对偏移：根整体 +24/+24（连续粘贴不与原件叠死；子树相对位置随序列化保留）
        if (s.Has<ecs::Transform2D>(root))
            s.Get<ecs::Transform2D>(root).pos = entityClipRootPos_[i] + Vec2{24.0f, 24.0f};
        ctx_.Select(root, any); // 粘贴根全进选择集（末位 = 主选中）
        any = true;
    }
    if (any) {
        ctx_.PushStructuralUndo("粘贴实体", before);
        LEMON_LOG("已粘贴 %zu 个实体（偏移 +24,+24）", entityClip_.size());
    }
}

void EditorApp::ImportDroppedFile(const std::string& absPath) {
    // §5-3：OS 拖入窗口的文件 → 当前资产目录（AssetBrowser 浏览目录；面板不可见 = 根）
    namespace fs = std::filesystem;
    // SDL drop 路径是 UTF-8 契约；win 侧 fs::path(窄串) 按 ACP 解——中文文件名不
    // 归一会被误判"非文件"跳过（W6 2026-10-06 真机实抓，07 §3.5 ②）
    const std::string nativePath = Utf8ToAcp(absPath);
    std::error_code ec;
    if (!fs::is_regular_file(nativePath, ec)) {
        LEMON_WARN("拖入跳过（非文件）：%s", absPath.c_str());
        return;
    }
    if (ctx_.Assets().ProjectRoot().empty()) {
        LEMON_ERROR("拖入导入失败：未打开项目。文件 → 新建项目... 或 打开项目...");
        return;
    }
    std::string subDir; // ImportFile 的 relDest 相对 Assets/（"" = 根）
    for (auto& en : panels_.Entries())
        if (auto* browser = dynamic_cast<AssetBrowserPanel*>(en.panel)) {
            const std::string& d = browser->CurrentDir(); // "" 或 "Assets[/x]"
            if (d.rfind("Assets/", 0) == 0) subDir = d.substr(7);
            break;
        }
    if (!subDir.empty() && subDir.back() != '/') subDir += '/';
    // 重名不覆盖：自动加序号（拖同名文件静默覆盖旧资产太危险）
    const std::string stem = fs::path(nativePath).stem().string();
    const std::string ext = fs::path(nativePath).extension().string();
    std::string relDest = subDir + stem + ext;
    for (int i = 2; ctx_.Assets().FindByPath("Assets/" + relDest); ++i)
        relDest = subDir + stem + " " + std::to_string(i) + ext;
    if (const AssetEntry* e = ctx_.Assets().ImportFile(nativePath, relDest)) {
        if (e->type == AssetType::Sprite) gpuAssets_.ImportSprite(*e);
        LEMON_LOG("拖入导入：%s（guid %016llx）", e->relPath.c_str(),
                  (unsigned long long)e->guid);
    }
}

} // namespace lemon::editor
