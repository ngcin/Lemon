// Lemon 编辑器 — 共享状态模型（M4.md §3.3 的 M4.1 形态）
// 唯一跨面板共享状态：面板间不互相 include，一律经 EditorApp 拿到这里。
// M4.3 增 playWorld/editSnapshot（Play 沙盒）；M4.4 增 assets（AssetDatabase）。
#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Assets/AssetDatabase.h"
#include "ECS/Entity.h"
#include "ECS/Scene.h"
#include "ECS/World.h"
#include "Tooling/UndoStack.h"

namespace lemon::scripting {
class ScriptHost;
}

namespace lemon::editor {

/// sprite 引用解析统计（M6a 批⓪ T2；OpenScene 聚合告警用）
struct SpriteRefStats {
    uint32_t danglingGuid = 0; // guid≠0 查无/missing（spriteId 保留旧值渲染）
    uint32_t backfilled = 0;   // 存量 guid=0 → 补写（保存后升级 guid 主键）
};

class EditorContext {
public:
    EditorContext();
    ~EditorContext();

    ecs::World& World() { return *world_; }
    /// 编辑数据（.scene 内容）；IO 一律走它
    ecs::Scene& EditScene() { return *scene_; }

    // ---- 资产库（M4.4；DB 归此处，GPU 缓存/导入器在 EditorApp——设备态不入 ctx）----
    AssetDatabase& Assets() { return assets_; }

    // ---- C# 脚本装配通路（M4.4 内核 #7；host 由 EditorApp 装配后注入）----
    void SetScriptHost(scripting::ScriptHost* host) { scripts_ = host; }
    scripting::ScriptHost* Scripts() const { return scripts_; }
    /// 已注册脚本类型名（无 host/未加载 = 空）
    const std::vector<std::string>& ScriptTypeNames() const;
    int ResolveScriptTypeId(const char* className) const;
    /// 挂脚本（M6a 批⓪ 多槽：追加槽；同类型唯一入口闸——已有同类拒绝 + 告警；
    /// className 持久键，typeId 即时解析（无 host = -1））
    void AttachScript(ecs::Entity e, uint64_t assetGuid, const char* className);
    /// 换槽类型（Inspector 逐槽 combo）：与其他槽撞类型 = 拒绝返回 false
    bool SetSlotScript(ecs::Entity e, uint32_t slotIdx, uint64_t assetGuid,
                       const char* className);
    /// 移除槽（保序；count 归零 = 移除整个 ScriptBox 组件）
    void RemoveScriptSlot(ecs::Entity e, uint32_t slotIdx);
    /// 热重载换装后重装配（M4.5）：Edit 世界刷新 ScriptBox.typeId；Play 世界原位
    /// 换实例（AttachBehaviour → 新域 Awake/OnEnable + StateBag OnHotReloadIn）。
    /// 返回 Play 世界重装配实例数（未在 Play = 0）。
    int RefreshScriptsAfterReload();

    // ---- 场景 IO（M4.md §3.8）----
    /// 新建空场景（untitled；dirty=false）。清空当前 Scene 重建（World 不重建）。
    void NewScene();
    /// 打开 .scene；失败（文件缺失/解析失败）返回 false 且场景不动
    bool OpenScene(const std::string& path);
    /// 保存；path 为空 = 写 scenePath（无路径返回 false）。成功后 dirty=false。
    bool SaveScene(std::string path = "");
    const std::string& ScenePath() const { return scenePath_; }
    std::string SceneName() const; // 文件名或 "untitled"

    // ---- 最近场景（M4.8-b：File 菜单一键切回；.lemon/recent-scenes.json，≤5 去重）----
    const std::vector<std::string>& RecentScenes() const { return recentScenes_; }
    void LoadRecentScenes();                        // 开项目时调（OpenProjectPipeline）
    void RecordRecentScene(const std::string& path); // OpenScene 成功路径集中记

    // ---- 自动备份与崩溃恢复（§3.8 M4.5：5 分钟快照 + 启动 mtime 比对提示）----
    /// 每帧驱动：interval 秒且 dirty 且非 Play → 写 .lemon/autosave/<名>.scene（单份滚动）
    void TickAutosave(double nowSec, double intervalSec = 300.0);
    /// 立即快照（测试/手动触发共用；无项目根/空场景返回 false）
    bool AutoSaveNow();
    /// 恢复检测：autosave 新于磁盘 .scene（或 .scene 缺失而 autosave 在）→ 返回其路径
    std::string DetectAutosaveRecovery() const;
    /// 恢复 = 载入 autosave 内容但 scenePath 指向原 .scene、dirty 置位（用户决定落盘）
    bool OpenSceneRecovery(const std::string& autosavePath);
    /// 忽略并删除 = 丢弃该备份（只删 autosave 目录内的路径——防误删任意文件）。
    /// 普通忽略只关本会话弹窗不动文件：untitled 无盘档永新，下次启动仍会提示
    bool DiscardAutosave(const std::string& autosavePath);

    // ---- 实体操作（编辑器创建的实体恒带 guid + Meta + Transform2D）----
    ecs::Entity CreateEntity(const char* tag);
    ecs::Entity CreateSpriteEntity(const char* tag, uint32_t spriteId = 4); // 默认柠檬黄
    /// guid 版（生成器/模板链）：guid → 查表 spriteId 双写（查无 = id 0 + 告警，
    /// 实体仍建——比 FromAsset 宽容：模板链断资不炸，Inspector ⚠ 可见）
    ecs::Entity CreateSpriteEntityByGuid(const char* tag, uint64_t spriteGuid);
    /// 资产落地版：sprite 资产 GUID → spriteId（找不到/悬空 = Null + 红字）
    ecs::Entity CreateSpriteEntityFromAsset(const char* tag, uint64_t assetGuid, Vec2 pos);
    /// 深拷贝组件（注册表驱动 POD 复制）；副本 = 新根 + 新 guid；
    /// EntityRef 字段指向自身集合内的 → 置空（跨副本引用不猜）。子树不复制（M4.2 伴生）。
    ecs::Entity DuplicateEntity(ecs::Entity e);
    /// 删除实体树（含后代），清选择集中成员，dirty 置位
    void DestroyEntityTree(ecs::Entity e);

    // ---- Prefab 最小集（M4.md §3.9；.prefab = 实体子树 JSON，06 §4）----
    /// 选中实体导出为 Assets/Prefabs/<tag>.prefab + 挂 prefabId 回链；返回资产 GUID（0=败）
    uint64_t MakePrefabFrom(ecs::Entity e);
    /// .prefab 实例化（新 guid 集合 + prefabId 回链；pos 覆盖 root 本地位置）
    ecs::Entity InstantiatePrefabAsset(uint64_t prefabGuid, Vec2 pos);
    /// 实例化核心（json 已在手；无 IO/日志/dirty——高频 spawn 工厂复用，M5 清障②）
    ecs::Entity InstantiatePrefabJson(ecs::Scene& s, const std::string& json,
                                      uint64_t prefabGuid, Vec2 pos);
    /// 实例改动写回源资产
    bool ApplyPrefabInstance(ecs::Entity e);
    /// 回到源资产态（整体：destroy + 重建于原父之下）
    bool RevertPrefabInstance(ecs::Entity e);
    /// 断链成普通实体（仅 root prefabId 清零）
    void BreakPrefabInstance(ecs::Entity e);

    // ---- 选择集（末位 = 主选中；M4.md §3.3 双记 guid 在 Undo 接入时补）----
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

    // ---- Play 沙盒（M4.md §3.4；进出 checklist 全项）----
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

    // ---- 游戏存档 IO（M5 批④ D1；M6a 批② T5 分档参数化。编辑器域实现，
    // ScriptHost 钩子消费。档常量/键约定见 SaveChannel.h）----
    /// 存档路径 = 项目根/.lemon/saves/{slot_0,settings,meta}.sav（无项目/未打开 =
    /// 空串 = 全部 no-op；ch 越界钳 slot）
    std::string SaveFilePath(uint8_t ch) const;
    /// 通道 → 文件（旧档转 .bak → tmp 写 → 原子改名；空通道/无项目 = false）
    bool WriteSaveFile(uint8_t ch, const ecs::SaveChannel& chn);
    /// 文件 → 通道（EnterPlay 载入；坏档红字后试 .bak，再坏 = 空通道开局）。
    /// slot 档含旧 game.sav 惰性迁移：新档（含 .bak）不存在且旧名在 → 读旧路径
    /// （写恒写新名，免 rename 竞态；旧文件保留不删）
    void LoadSaveFile(uint8_t ch, ecs::SaveChannel& dst);

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

    // ---- C# native 钩子入参形态（EditorApp 装进 SetEditorAssetHooks；M4.4 #8）----
    /// GUID hex（C# Assets.SpriteOf 参数）→ spriteId（0 = 无/悬空）
    uint32_t SpriteIdOfGuidHex(const char* hex) const;

private:
    void BackfillGuids(); // 打开旧档（无 guid 字段）时补齐
    /// M6a 批⓪ T2：spriteGuid → spriteId 归一 + 存量回填（装载/恢复/Undo/Prefab 落地）
    SpriteRefStats ResolveSpriteRefs();
    void ResolvePlayScripts(); // EnterPlay：ScriptBox.className → typeId → AttachBehaviour
    std::string AutosavePathFor(const std::string& sceneStem) const; // .lemon/autosave/<stem>.scene
    // M5 清障②：Play 世界 Spawner/Shooter 工厂桥
    struct PlayPrefabCache {
        uint64_t guid = 0; // 完整资产 GUID（回链用）
        std::string json;  // .prefab 文本（进 Play 时刻快照）
    };
    void BuildPlayPrefabCache(); // EnterPlay：Prefab 资产 → {低 32 位 → 缓存}
    ecs::Entity SpawnPlayPrefab(ecs::Scene& s, uint32_t prefabId, Vec2 pos, uint32_t team);
    // M5 批③：EnterPlay 建 clip 表（.anim JSON → (sheet guid, cell) 解析为 spriteId
    // 入 playWorld_->Clips()；进 Play 时刻快照——Play 中改 .anim 不生效）
    void BuildPlayClipCache();
    // M6a 批② T3d：EnterPlay 建状态机表（.controller JSON → 下标形态入
    // playWorld_->Controllers()；进 Play 时刻快照——Play 中改 .controller 不生效；
    // AnimGraphSystem 图评估消费，ADR-013 D1）
    void BuildPlayControllerCache();
    // M6a 批② T2：EnterPlay 建配置表（.tab JSON → 全字符串格网格入
    // playWorld_->Tables()，键 = 资产 GUID 低 32 位；进 Play 时刻快照——
    // Play 中改 .tab 不生效；C# Lemon.Table 读，ADR-012 D1）
    void BuildPlayTableCache();

    std::unique_ptr<ecs::World> world_;
    ecs::Scene* scene_ = nullptr;
    std::string scenePath_;
    std::vector<std::string> recentScenes_; // M4.8-b：最近场景（项目内记账）
    std::vector<ecs::Entity> selection_;
    AssetDatabase assets_;
    scripting::ScriptHost* scripts_ = nullptr;
    double lastAutosaveSec_ = 0.0; // 上次快照时刻（steady 秒；编辑动作不清零节拍）

    // Play 沙盒态
    std::unique_ptr<ecs::World> playWorld_;
    ecs::Scene* playScene_ = nullptr;
    std::string editSnapshot_;              // 进 Play 前全量快照（§3.4-1）
    std::vector<uint64_t> savedSelectionGuids_;
    std::unordered_map<uint32_t, PlayPrefabCache> playPrefabCache_; // M5 清障②
    std::unordered_set<uint32_t> playSpawnWarned_; // prefabId 错绑去重告警
    bool lastExitVerified_ = false;
    double lastEnterMs_ = 0.0, lastExitMs_ = 0.0;
    UndoStack undo_;
};

} // namespace lemon::editor
