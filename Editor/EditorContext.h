// Lemon 编辑器 — 共享状态模型（M4-Editor-Plan §3.3 的 M4.1 形态）
// 唯一跨面板共享状态：面板间不互相 include，一律经 EditorApp 拿到这里。
// M4.3 增 playWorld/editSnapshot（Play 沙盒）；M4.4 增 assets（AssetDatabase）。
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "ECS/Entity.h"
#include "ECS/Scene.h"
#include "ECS/World.h"
#include "Tooling/UndoStack.h"

namespace lemon::editor {

class EditorContext {
public:
    EditorContext();
    ~EditorContext();

    ecs::World& World() { return *world_; }
    /// 编辑数据（.scene 内容）；IO 一律走它
    ecs::Scene& EditScene() { return *scene_; }

    // ---- 场景 IO（M4-Editor-Plan §3.8）----
    /// 新建空场景（untitled；dirty=false）。清空当前 Scene 重建（World 不重建）。
    void NewScene();
    /// 打开 .scene；失败（文件缺失/解析失败）返回 false 且场景不动
    bool OpenScene(const std::string& path);
    /// 保存；path 为空 = 写 scenePath（无路径返回 false）。成功后 dirty=false。
    bool SaveScene(std::string path = "");
    const std::string& ScenePath() const { return scenePath_; }
    std::string SceneName() const; // 文件名或 "untitled"

    // ---- 实体操作（编辑器创建的实体恒带 guid + Meta + Transform2D）----
    ecs::Entity CreateEntity(const char* tag);
    ecs::Entity CreateSpriteEntity(const char* tag, uint32_t spriteId = 4); // 默认柠檬黄
    /// 深拷贝组件（注册表驱动 POD 复制）；副本 = 新根 + 新 guid；
    /// EntityRef 字段指向自身集合内的 → 置空（跨副本引用不猜）。子树不复制（M4.2 伴生）。
    ecs::Entity DuplicateEntity(ecs::Entity e);
    /// 删除实体树（含后代），清选择集中成员，dirty 置位
    void DestroyEntityTree(ecs::Entity e);

    // ---- 选择集（末位 = 主选中；M4-Editor-Plan §3.3 双记 guid 在 Undo 接入时补）----
    std::vector<ecs::Entity>& Selection() { return selection_; }
    bool IsSelected(ecs::Entity e) const;
    void Select(ecs::Entity e, bool additive);
    void ClearSelection();
    /// 主选中（空选择 = Null）
    ecs::Entity Primary() const { return selection_.empty() ? ecs::Entity::Null() : selection_.back(); }
    /// 选择集裁剪（实体死亡后调用；按 Scene 当前代校验）
    void PruneSelection();

    // ---- 帧节奏（非 Play：仅 Essential——销毁提交等；§3.3）----
    void TickEditor(float dt);
    /// Play 帧节奏：激活 World 完整 Step（固定步长；Pause 由调用方跳过本调用）
    void TickPlay(float dt);

    // ---- Play 沙盒（M4-Editor-Plan §3.4；进出 checklist 全项）----
    bool EnterPlay();          // 快照固化 → 建 playWorld/Load → 清 Undo/存选中（计时 t0）
    bool ExitPlay();           // 弃 playWorld → editScene ← Load(快照) 整体重建 → 恢复选中
    bool Playing() const { return playWorld_ != nullptr; }
    /// 激活视图（面板/提取统一读它：Play 中 = Play World，否则 = 编辑世界；§2.4）
    ecs::World& ActiveWorld() { return Playing() ? *playWorld_ : *world_; }
    ecs::Scene& ActiveScene() { return Playing() ? *playScene_ : *scene_; }
    /// 上次 Stop 校验：editScene 序列化 == 进 Play 前快照（逐字节；§3.4 验收 #5）
    bool LastExitVerified() const { return lastExitVerified_; }
    double LastEnterPlayMs() const { return lastEnterMs_; }
    double LastExitPlayMs() const { return lastExitMs_; }
    /// Play 中编辑落 Play World（决议 #5）—— dirty 不置位（Stop 即丢，不动编辑侧）

    // ---- Undo 双轨（§3.5；Play 中禁用）----
    UndoStack& Undo() { return undo_; }
    /// 组件字节快照（属性轨原料；guid 定位）
    std::vector<uint8_t> SnapshotComponent(ecs::Entity e, uint16_t compId);
    /// 按 guid 找实体（当前激活场景；找不到 = Null）
    ecs::Entity FindByGuid(uint64_t guid) const;
    /// 属性轨记录提交（before/after 组件字节；Undo/Redo 按 guid 找回）
    void PushPropertyUndo(const char* name, uint64_t guid, uint16_t compId,
                          std::vector<uint8_t> before, std::vector<uint8_t> after);
    /// 结构轨：op 前抓 before 快照 → 执行 op → 本调用抓 after 并入栈（场景 JSON 双快照）
    void PushStructuralUndo(const char* name, const std::string& beforeJson);
    /// 结构轨辅助：当前激活场景 JSON（SceneArchive::Save）
    std::string SnapshotSceneJson();

    bool dirty = false; // 场景脏标记（Ctrl+S/关闭确认/状态栏 ●）

private:
    void BackfillGuids(); // 打开旧档（无 guid 字段）时补齐

    std::unique_ptr<ecs::World> world_;
    ecs::Scene* scene_ = nullptr;
    std::string scenePath_;
    std::vector<ecs::Entity> selection_;

    // Play 沙盒态
    std::unique_ptr<ecs::World> playWorld_;
    ecs::Scene* playScene_ = nullptr;
    std::string editSnapshot_;              // 进 Play 前全量快照（§3.4-1）
    std::vector<uint64_t> savedSelectionGuids_;
    bool lastExitVerified_ = false;
    double lastEnterMs_ = 0.0, lastExitMs_ = 0.0;
    UndoStack undo_;
};

} // namespace lemon::editor
