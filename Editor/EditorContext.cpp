// Lemon 编辑器 — EditorContext 实现（场景 IO / 实体操作 / 选择集）
#include "EditorContext.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <unordered_set>

#include <nlohmann/json.hpp>

#include "Assets/Csv.h" // ParseTableJson（BuildPlayTableCache；M6a 批② T2）
#include "Assets/ClipEdit.h" // ParseAnimSetJson（BuildPlayClipCache 集登记；T3c）
#include "Assets/ControllerEdit.h" // ParseControllerJson（BuildPlayControllerCache；T3d）
#include "ECS/ControllerTable.h" // ControllerDef（编译目标形态；T3d）
#include "Components/CoreComponents.h"
#include "Components/RenderComponents.h"
#include "Core/Guid.h"
#include "Core/Log.h"
#include "ECS/ComponentRegistry.h"
#include "ECS/Hierarchy.h"
#include "Serialization/SceneArchive.h"
#include "Scripting/ScriptBox.h"
#include "Scripting/ScriptHost.h"
#include "Systems/Systems.h"

namespace lemon::editor {
using ecs::RegisterAllComponents;
using ecs::SceneArchive;

namespace {
/// 路径规范化（2026-09-24 用户报告：最近场景同名重复——相对/绝对、冗余 ./ 段、
/// 符号链接等不同拼写在字符串精确去重下各成条目）。weakly_canonical 折叠上述
/// 形态；指向不存在文件的路径（最近档悬空）按最长存在前缀 + 词法归一，不失败。
/// 失败/空串兜底原串（调用方语义不变）。场景路径与最近记录统一走此口。
std::string CanonicalPath(const std::string& path) {
    if (path.empty()) return path;
    std::error_code ec;
    auto c = std::filesystem::weakly_canonical(path, ec);
    return ec ? path : c.string();
}
} // namespace

EditorContext::EditorContext() {
    RegisterAllComponents(); // 编辑器宿主进程内的注册表初始化（幂等）
    world_ = std::make_unique<ecs::World>(ecs::WorldDesc{.seed = 20260919ull});
    // 编辑世界只装 Essential（销毁提交）；Play 世界 M4.3 独立构建全量管线
    world_->Pipeline().AddSystem(std::make_unique<ecs::DestroyCommitSystem>());
    world_->Pipeline().ResolveOrder();
    scene_ = &world_->CreateScene("edit");
    world_->SetActiveScene(scene_);
}

EditorContext::~EditorContext() = default;

void EditorContext::NewScene() {
    // 清空重建经 SceneArchive 空档（Load 语义 = 清空目标 Scene 后重建；World 不动）
    static const char* kEmpty = R"({"schemaVersion":2,"name":"untitled","entities":[]})";
    SceneArchive::Load(*scene_, kEmpty);
    scenePath_.clear();
    selection_.clear();
    dirty = false;
    // 场景已换：结构轨快照都是旧场景的 JSON，Ctrl+Z 会把旧场景整体灌进新场景
    // （与 EnterPlay 的清栈同款；2026-09-24 审查发现 OpenScene/NewScene 漏配）
    undo_.Clear();
}

bool EditorContext::OpenScene(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        LEMON_WARN("打开场景失败：文件不存在 %s", path.c_str());
        return false;
    }
    std::string text((size_t)f.tellg(), '\0');
    f.seekg(0);
    f.read(text.data(), (std::streamsize)text.size());
    if (!SceneArchive::Load(*scene_, text)) {
        LEMON_WARN("打开场景失败：解析失败 %s", path.c_str());
        return false;
    }
    scenePath_ = CanonicalPath(path);
    selection_.clear();
    // 场景已换：旧场景的结构轨快照不得残留（Ctrl+Z 会把旧场景灌进当前场景）
    undo_.Clear();
    BackfillGuids();
    // M6a 批⓪ T2：guid → spriteId 归一（改名/移位/重开 id 重排不断链）+ 存量回填
    const SpriteRefStats st = ResolveSpriteRefs();
    // 悬空引用聚合告警（2026-09-22 测试报告观察 6）：guid 悬空（资产被删/manifest
    // 丢失）∪ 存量数字号未登记 = 渲染静默缺失（"sprite 不显示"排查半天的第一案
    // 发现场）。装载期一次性列出计数，不逐实体刷屏。合法域 = 程序化页（< 基号）
    // ∪ DB 记账号（含切片区间与墓碑——源文件缺失走 Inspector ⚠，不在此重复报）。
    if (!assets_.ProjectRoot().empty()) {
        uint32_t dangling = 0;
        scene_->Each([&](ecs::Entity e) {
            if (const ecs::SpriteRenderer* sr = scene_->TryGet<ecs::SpriteRenderer>(e)) {
                if (sr->spriteGuid != 0) return; // guid 路径已由 st.danglingGuid 计
                const uint32_t id = sr->spriteId;
                if (id != 0 && id >= assets_.SpriteIdBase() &&
                    !assets_.SpriteIdRegistered(id)) // M5 批④：含切片区间（模板场景
                    ++dangling;                      // 引用 cell 号是常态，勿误报）
            }
        });
        if (st.danglingGuid || dangling)
            LEMON_WARN("场景装载：%u 个 spriteGuid 失效 + %u 个存量 spriteId 未登记"
                       "（渲染将缺失；Inspector sprite 槽重指可修）",
                       st.danglingGuid, dangling);
    }
    // 回填过 guid = 档内容已升级，保持 dirty 提示保存（下次装载不再回填）
    dirty = st.backfilled > 0;
    RecordRecentScene(scenePath_);
    LEMON_LOG("场景已打开：%s（%u 实体）", scenePath_.c_str(), scene_->AliveCount());
    return true;
}

// ------------------------------------------------ 最近场景（M4.8-b）----
void EditorContext::LoadRecentScenes() {
    recentScenes_.clear();
    const std::string& root = assets_.ProjectRoot();
    if (root.empty()) return;
    std::ifstream f(root + "/.lemon/recent-scenes.json", std::ios::binary);
    if (!f) return;
    try {
        nlohmann::json j = nlohmann::json::parse(f);
        for (const auto& e : j.at("scenes")) {
            if (!e.is_string()) continue;
            const std::string raw = e.get<std::string>();
            if (raw.empty()) continue; // 脏档清洗：自别名 UAF 曾写入空串
            const std::string p = CanonicalPath(raw); // 存量异形拼写折叠（相对/./.. 段）
            if (std::find(recentScenes_.begin(), recentScenes_.end(), p) != recentScenes_.end())
                continue; // 脏档清洗：重复条目保序留首见
            recentScenes_.push_back(p);
        }
    } catch (const std::exception&) {
        recentScenes_.clear(); // 坏档丢弃（下次保存重建）
    }
    if (recentScenes_.size() > 5) recentScenes_.resize(5);
}

void EditorContext::RecordRecentScene(const std::string& path) {
    if (path.empty() || assets_.ProjectRoot().empty()) return;
    // path 可能别名 recentScenes_ 自身元素（File→最近场景菜单直传引用）；
    // 下方 erase/insert 会毁掉该元素，必须先拷贝再动容器（否则 UAF：2026-09-22 崩溃案）
    const std::string p = path;
    recentScenes_.erase(std::remove_if(recentScenes_.begin(), recentScenes_.end(),
                                       [&](const std::string& e) { return e == p; }),
                        recentScenes_.end());
    recentScenes_.insert(recentScenes_.begin(), p); // 置顶
    if (recentScenes_.size() > 5) recentScenes_.resize(5);
    nlohmann::json j;
    j["scenes"] = recentScenes_;
    std::error_code ec;
    std::filesystem::create_directories(assets_.ProjectRoot() + "/.lemon", ec);
    std::ofstream f(assets_.ProjectRoot() + "/.lemon/recent-scenes.json",
                    std::ios::binary | std::ios::trunc);
    if (f) f << j.dump(2) << '\n';
}

bool EditorContext::SaveScene(std::string path) {
    if (path.empty()) {
        if (scenePath_.empty()) return false; // 无路径 = 需另存为
        path = scenePath_;
    }
    PruneSelection(); // 序列化前清死引用选中项
    std::string json = SceneArchive::Save(*scene_);
    if (!WriteFileAtomic(path, json.data(), json.size())) {
        LEMON_WARN("保存场景失败：写入中断 %s", path.c_str());
        return false;
    }
    scenePath_ = path;
    dirty = false;
    // 正常落盘后清掉同场景 autosave（避免下次启动误报"有较新快照"）
    std::error_code ec;
    std::filesystem::remove(AutosavePathFor(SceneName()), ec);
    LEMON_LOG("场景已保存：%s（%zu 字节）", path.c_str(), json.size());
    return true;
}

// ------------------------------------------------ 自动备份/崩溃恢复（§3.8）----
std::string EditorContext::AutosavePathFor(const std::string& sceneName) const {
    return assets_.ProjectRoot() + "/.lemon/autosave/" + sceneName;
}

void EditorContext::TickAutosave(double nowSec, double intervalSec) {
    if (nowSec - lastAutosaveSec_ < intervalSec) return;
    lastAutosaveSec_ = nowSec;
    if (!dirty || Playing()) return;       // §3.8：dirty 且非 Play 才写
    if (assets_.ProjectRoot().empty()) return; // 无项目根 = 无 .lemon/（临时场景）
    if (scene_->AliveCount() == 0) return; // 空场景无快照价值
    AutoSaveNow();
}

bool EditorContext::AutoSaveNow() {
    if (assets_.ProjectRoot().empty()) return false;
    std::error_code ec;
    std::filesystem::path dst = AutosavePathFor(SceneName());
    std::filesystem::create_directories(dst.parent_path(), ec);
    PruneSelection();
    std::string json = SceneArchive::Save(*scene_);
    std::ofstream f(dst, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(json.data(), (std::streamsize)json.size());
    LEMON_LOG("自动备份：%s（%zu 字节）", dst.string().c_str(), json.size());
    return true;
}

std::string EditorContext::DetectAutosaveRecovery() const {
    if (assets_.ProjectRoot().empty()) return {};
    const std::string as = AutosavePathFor(SceneName());
    std::error_code ec;
    if (!std::filesystem::exists(as, ec)) return {};
    // untitled 场景：autosave 在即有可恢复内容（无对照盘档）
    if (scenePath_.empty()) return as;
    const auto asT = std::filesystem::last_write_time(as, ec);
    if (ec) return {};
    const auto scT = std::filesystem::last_write_time(scenePath_, ec);
    if (ec || asT > scT) return as; // .scene 缺失（被删）也算可恢复
    return {};
}

bool EditorContext::OpenSceneRecovery(const std::string& autosavePath) {
    std::ifstream f(autosavePath, std::ios::binary | std::ios::ate);
    if (!f) return false;
    std::string text((size_t)f.tellg(), '\0');
    f.seekg(0);
    f.read(text.data(), (std::streamsize)text.size());
    if (!SceneArchive::Load(*scene_, text)) return false;
    // scenePath_ 保持指向原 .scene（untitled 则保持空）——落盘与否由用户决定
    selection_.clear();
    BackfillGuids();
    ResolveSpriteRefs(); // M6a 批⓪ T2：autosave 可能来自旧进程（id 口径漂移）
    dirty = true;
    LEMON_LOG("已恢复自动备份（未落盘，Ctrl+S 保存 / 关闭确认丢弃）：%s", autosavePath.c_str());
    return true;
}

std::string EditorContext::SceneName() const {
    if (scenePath_.empty()) return "untitled.scene";
    size_t slash = scenePath_.find_last_of("/\\");
    return slash == std::string::npos ? scenePath_ : scenePath_.substr(slash + 1);
}

ecs::Entity EditorContext::CreateEntity(const char* tag) {
    // Play 态创建 = 落 play 世界（可见、随 Stop 丢弃，ADR-011 横幅口径）；原恒走
    // scene_（edit 世界）= Play 态创建不可见且 Stop 后残留
    ecs::Scene& s = ActiveScene();
    ecs::Entity e = s.Create();
    s.Emplace<ecs::Transform2D>(e);
    ecs::Meta& m = s.Emplace<ecs::Meta>(e);
    m.guid = GenerateGuid();
    std::snprintf(m.tag, sizeof(m.tag), "%s", tag);
    dirty = true;
    return e;
}

ecs::Entity EditorContext::CreateSpriteEntity(const char* tag, uint32_t spriteId) {
    ecs::Entity e = CreateEntity(tag);
    ecs::SpriteRenderer& sr = ActiveScene().Emplace<ecs::SpriteRenderer>(e); // 默认启用
    sr.spriteId = spriteId;
    return e;
}

ecs::Entity EditorContext::CreateSpriteEntityByGuid(const char* tag, uint64_t spriteGuid) {
    // guid/id 双写（M6a 批⓪ T2）：查无/悬空不炸——id=0 + guid 留底，Inspector ⚠ 可见
    const AssetEntry* entry = assets_.FindByGuid(spriteGuid);
    if (!entry || entry->missing || entry->type != AssetType::Sprite || entry->spriteId == 0) {
        LEMON_WARN("创建精灵：guid %016llx 未命中 sprite 资产（引用留底，重开项目后重解析）",
                   (unsigned long long)spriteGuid);
        entry = nullptr;
    }
    ecs::Entity e = CreateEntity(tag);
    ecs::SpriteRenderer& sr = ActiveScene().Emplace<ecs::SpriteRenderer>(e); // 默认启用
    sr.spriteGuid = spriteGuid;
    // 切片表默认 0 号 cell（T4 生成器惯例：首帧 = cell 0）；整图 = 本体号
    sr.spriteId = entry ? (entry->sliceCount > 0 ? entry->sliceBase : entry->spriteId) : 0;
    return e;
}

ecs::Entity EditorContext::CreateSpriteEntityFromAsset(const char* tag, uint64_t assetGuid,
                                                       Vec2 pos) {
    const AssetEntry* entry = assets_.FindByGuid(assetGuid);
    if (!entry || entry->missing || entry->type != AssetType::Sprite || entry->spriteId == 0) {
        LEMON_WARN("创建精灵失败：资产不存在或非 sprite（guid %016llx）",
                   (unsigned long long)assetGuid);
        return ecs::Entity::Null();
    }
    ecs::Entity e = CreateSpriteEntity(tag, entry->spriteId);
    ecs::Scene& s = ActiveScene();
    s.Get<ecs::Transform2D>(e).pos = pos;
    s.Get<ecs::SpriteRenderer>(e).spriteGuid = assetGuid; // M6a 批⓪ T2：双写
    return e;
}

// ------------------------------------------------------ 脚本装配（#7）----
const std::vector<std::string>& EditorContext::ScriptTypeNames() const {
    static const std::vector<std::string> kEmpty;
    return scripts_ ? scripts_->BehaviourTypeNames() : kEmpty;
}

int EditorContext::ResolveScriptTypeId(const char* className) const {
    if (!scripts_ || !className || !className[0]) return -1;
    const auto& names = scripts_->BehaviourTypeNames();
    for (size_t i = 0; i < names.size(); ++i)
        if (names[i] == className) return (int)i;
    return -1;
}

void EditorContext::AttachScript(ecs::Entity e, uint64_t assetGuid, const char* className) {
    if (e.IsNull() || !scene_->Alive(e)) return;
    const char* cls = className ? className : "";
    // M6a 批⓪ 决策 4：同类型唯一入口闸（Inspector 菜单已置灰，此处兜底 combo/
    // 程序化调用）；槽满拒绝。先查后建——不留空 count=0 组件。
    if (scripting::ScriptBox* box = scene_->TryGet<scripting::ScriptBox>(e)) {
        if (scripting::FindSlot(*box, cls) >= 0) {
            LEMON_WARN("同实体同类型脚本唯一：'%s' 已挂载，拒绝重复追加", cls);
            return;
        }
        if (!scripting::AppendSlot(*box, assetGuid, cls)) {
            LEMON_WARN("脚本槽满（%u）：拒绝追加 '%s'", scripting::kMaxScriptsPerEntity,
                       cls);
            return;
        }
    } else {
        scripting::ScriptBox& fresh = scene_->Emplace<scripting::ScriptBox>(e);
        scripting::AppendSlot(fresh, assetGuid, cls);
    }
    scripting::ScriptBox& sb = scene_->Get<scripting::ScriptBox>(e);
    sb.slots[sb.count - 1].typeId = ResolveScriptTypeId(cls);
    dirty = true;
}

bool EditorContext::SetSlotScript(ecs::Entity e, uint32_t slotIdx, uint64_t assetGuid,
                                  const char* className) {
    scripting::ScriptBox* sb =
        (e.IsNull() || !scene_->Alive(e)) ? nullptr : scene_->TryGet<scripting::ScriptBox>(e);
    if (!sb || slotIdx >= sb->count) return false;
    const char* cls = className ? className : "";
    for (uint32_t i = 0; i < sb->count; ++i) {
        if (i == slotIdx) continue;
        if (std::strcmp(sb->slots[i].className, cls) == 0) {
            LEMON_WARN("同实体同类型脚本唯一：'%s' 已在槽 %u，换类型拒绝", cls, i);
            return false;
        }
    }
    scripting::ScriptSlot& s = sb->slots[slotIdx];
    s.scriptGuid = assetGuid;
    std::memset(s.className, 0, sizeof(s.className));
    std::snprintf(s.className, sizeof(s.className), "%s", cls);
    s.typeId = ResolveScriptTypeId(cls);
    s.flags &= ~scripting::kScriptFlagDisabled;
    dirty = true;
    return true;
}

void EditorContext::RemoveScriptSlot(ecs::Entity e, uint32_t slotIdx) {
    if (e.IsNull() || !scene_->Alive(e)) return;
    scripting::ScriptBox* sb = scene_->TryGet<scripting::ScriptBox>(e);
    if (!sb || slotIdx >= sb->count) return;
    scripting::RemoveSlot(*sb, slotIdx);
    if (sb->count == 0) scene_->Remove<scripting::ScriptBox>(e);
    dirty = true;
}

uint32_t EditorContext::SpriteIdOfGuidHex(const char* hex) const {
    const AssetEntry* e = assets_.FindByGuid(AssetDatabase::HexToGuid(hex));
    return e && !e->missing ? e->spriteId : 0;
}

void EditorContext::ResolvePlayScripts() {
    if (!scripts_ || !playScene_) return;
    // M6a 批⓪：逐槽解析（typeId=-1 待解析 → className 映射 → 原位落号挂实例）
    playScene_->Each([this](ecs::Entity e) {
        scripting::ScriptBox* sb = playScene_->TryGet<scripting::ScriptBox>(e);
        if (!sb) return;
        for (uint32_t i = 0; i < sb->count; ++i) {
            scripting::ScriptSlot& s = sb->slots[i];
            if (s.typeId >= 0) continue;
            int id = ResolveScriptTypeId(s.className);
            if (id < 0) {
                LEMON_WARN("Play 装配：脚本类型未注册（跳过）'%s'", s.className);
                continue;
            }
            scripts_->ResolveSlotBehaviour(*playWorld_, *playScene_, e, i, id);
        }
    });
}

int EditorContext::RefreshScriptsAfterReload() {
    if (!scripts_) return 0;
    // Edit 世界：只刷 typeId（编辑器不 tick；EnterPlay 时本就按 className 解析）
    scene_->Each([this](ecs::Entity e) {
        if (scripting::ScriptBox* sb = scene_->TryGet<scripting::ScriptBox>(e))
            for (uint32_t i = 0; i < sb->count; ++i)
                sb->slots[i].typeId = ResolveScriptTypeId(sb->slots[i].className);
    });
    if (!playScene_) return 0;
    // Play 世界：原位换实例——ResolveSlotBehaviour 走新域 scripts_attach
    //（Behaviours.Attach → Awake/OnEnable → 同 (类名,实体) StateBag → OnHotReloadIn）
    int n = 0;
    playScene_->Each([this, &n](ecs::Entity e) {
        scripting::ScriptBox* sb = playScene_->TryGet<scripting::ScriptBox>(e);
        if (!sb) return;
        for (uint32_t i = 0; i < sb->count; ++i) {
            scripting::ScriptSlot& s = sb->slots[i];
            if (!s.className[0]) continue;
            int id = ResolveScriptTypeId(s.className);
            if (id < 0) {
                LEMON_WARN("热重载：脚本类型未注册（保持挂起）'%s'", s.className);
                continue;
            }
            scripts_->ResolveSlotBehaviour(*playWorld_, *playScene_, e, i, id);
            ++n;
        }
    });
    return n;
}

// ------------------------------------------------------ Prefab（§3.9）----
uint64_t EditorContext::MakePrefabFrom(ecs::Entity e) {
    if (e.IsNull() || !scene_->Alive(e)) return 0;
    const ecs::Meta* m = scene_->TryGet<ecs::Meta>(e);
    const std::string tag = m && m->tag[0] ? m->tag : "entity";
    std::string json = SceneArchive::SaveEntityTree(*scene_, e);
    if (json.empty()) return 0;

    // 落盘根级 Prefabs/<tag>.prefab（06 §1；M4.5 起随向导统一根级目录）重名自动 -2/-3…
    std::string rel = "Prefabs/" + tag + ".prefab";
    for (int i = 2; assets_.FindByPath(rel); ++i)
        rel = "Prefabs/" + tag + "-" + std::to_string(i) + ".prefab";
    std::string abs = assets_.ProjectRoot() + "/" + rel;
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(abs).parent_path(), ec);
    std::ofstream f(abs, std::ios::binary | std::ios::trunc);
    if (!f) {
        LEMON_WARN("Prefab 导出失败：无法写入 %s", abs.c_str());
        return 0;
    }
    f << json;
    assets_.Rescan();
    assets_.SaveManifest();
    const AssetEntry* entry = assets_.FindByPath(rel);
    if (!entry) {
        LEMON_WARN("Prefab 导出后登记失败：%s", rel.c_str());
        return 0;
    }
    scene_->Get<ecs::Meta>(e).prefabId = entry->guid; // 回链（§3.9）
    dirty = true;
    LEMON_LOG("Prefab 化：%s → %s（guid %016llx）", tag.c_str(), rel.c_str(),
              (unsigned long long)entry->guid);
    return entry->guid;
}

ecs::Entity EditorContext::InstantiatePrefabAsset(uint64_t prefabGuid, Vec2 pos) {
    const AssetEntry* entry = assets_.FindByGuid(prefabGuid);
    if (!entry || entry->missing || entry->type != AssetType::Prefab) {
        LEMON_WARN("Prefab 实例化失败：资产不存在（guid %016llx）",
                   (unsigned long long)prefabGuid);
        return ecs::Entity::Null();
    }
    std::ifstream f(assets_.AbsolutePath(*entry), std::ios::binary);
    if (!f) return ecs::Entity::Null();
    std::string json((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    ecs::Scene& s = ActiveScene(); // Play 中脚本 Instantiate.Prefab 落 Play World
    ecs::Entity root = InstantiatePrefabJson(s, json, prefabGuid, pos);
    if (root.IsNull()) return root;
    // M6a 批⓪ T2：编辑态落地才解析（prefab 档可能带跨进程/改名后漂移 id；
    // Play 态 = 同进程 spawn，id 即真值，热路径零扫表）
    if (!Playing()) ResolveSpriteRefs();
    dirty = !Playing(); // Play 中 = 落 Play World，不动编辑侧脏标记（决议 #5）
    LEMON_LOG("Prefab 实例化：%s（%u 实体）", entry->relPath.c_str(), s.AliveCount());
    return root;
}

ecs::Entity EditorContext::InstantiatePrefabJson(ecs::Scene& s, const std::string& json,
                                                 uint64_t prefabGuid, Vec2 pos) {
    // 无日志/无 IO/无 dirty——高频 spawn 工厂与交互路径共用（M5 清障②）
    ecs::Entity root = SceneArchive::LoadEntityTree(s, json);
    if (root.IsNull()) return root;
    if (s.Has<ecs::Transform2D>(root)) s.Get<ecs::Transform2D>(root).pos = pos;
    if (ecs::Meta* m = s.TryGet<ecs::Meta>(root)) m->prefabId = prefabGuid;
    return root;
}

// ---- M5 清障②：Play 世界 SpawnFn 桥（Spawner/Shooter 的 prefabId → 资产实例化）----
// 约定：Spawner.prefabId / Shooter.projectileId 的 uint32 = prefab 资产 GUID 低 32 位
// （03 §69 组件 schema 恒 uint32；M7 烘焙引入 dense id 表时同语义替换）。进 Play 时
// 一次性建映射 + 文本缓存（Play 世界 = 进 Play 时刻快照，资产变更不追——与
// editSnapshot_ 同语义）。性能边界：每次 spawn 仍 parse JSON（小树几十 µs 级），
// 怪海爆发期可用；物化模板 + 池拷贝是 03 §10 对象池的正式工作，bench-survivor
// 实测不及格再升级。
void EditorContext::BuildPlayPrefabCache() {
    playPrefabCache_.clear();
    playSpawnWarned_.clear();
    for (const AssetEntry& e : assets_.Entries()) {
        if (e.type != AssetType::Prefab || e.missing) continue;
        std::ifstream f(assets_.AbsolutePath(e), std::ios::binary);
        if (!f) continue;
        PlayPrefabCache c;
        c.guid = e.guid;
        c.json.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        const uint32_t id = (uint32_t)e.guid; // 低 32 位（映射约定）
        if (!playPrefabCache_.emplace(id, std::move(c)).second)
            LEMON_WARN("Play prefab 映射碰撞：guid %016llx 与另一 prefab 低 32 位同值"
                       "（id %08x 取先登记者）",
                       (unsigned long long)e.guid, id);
    }
}

ecs::Entity EditorContext::SpawnPlayPrefab(ecs::Scene& s, uint32_t prefabId, Vec2 pos,
                                           uint32_t team) {
    if (prefabId == 0) return ecs::Entity::Null();
    auto it = playPrefabCache_.find(prefabId);
    if (it == playPrefabCache_.end()) {
        // 去重告警（prefabId 错绑的 Spawner 每帧触发——不刷屏）
        if (playSpawnWarned_.insert(prefabId).second)
            LEMON_WARN("Play 刷怪失败：prefabId %08x 无对应 prefab 资产（应填资产 GUID "
                       "低 32 位）",
                       prefabId);
        return ecs::Entity::Null();
    }
    ecs::Entity root = InstantiatePrefabJson(s, it->second.json, it->second.guid, pos);
    // 队伍覆盖：spawnTeam/弹队语义优先于 prefab 源值（bench 工厂同款）
    if (ecs::Meta* m = root.IsNull() ? nullptr : s.TryGet<ecs::Meta>(root)) m->team = team;
    return root;
}

// ---- M5 批③：Play 世界 clip 表（.anim JSON → ClipTable；06 §2.2 / 03 §5）----
// 格式（M5.md §16.2 D2）：
//   { "schemaVersion": 1, "fps": 8, "loop": true,
//     "frames": [ {"sheet": "<guidHex>", "cell": 0}, ... ] }
// 帧引用 = 精灵表资产 GUID + 切片序号（行优先）——不直接存 spriteId（manifest 重排
// 不断链）。进 Play 时刻快照（同 BuildPlayPrefabCache 语义）。坏 clip 红字跳过：
// 实体 Animator2D.clipId 未命中表 → M2 纯计时回退（不炸）。
void EditorContext::BuildPlayClipCache() {
    playWorld_->Clips().Clear();
    for (const AssetEntry& e : assets_.Entries()) {
        if (e.type != AssetType::Clip || e.missing) continue;
        std::ifstream f(assets_.AbsolutePath(e), std::ios::binary);
        if (!f) continue;
        std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        nlohmann::json doc = nlohmann::json::parse(text, nullptr, false);
        if (doc.is_discarded() || !doc.contains("frames") || !doc.at("frames").is_array() ||
            !doc.contains("fps")) {
            LEMON_WARN("clip 解析失败（需 frames[]/fps）：%s——跳过", e.relPath.c_str());
            continue;
        }
        const float fps = doc.at("fps").get<float>();
        const bool loop = !doc.contains("loop") || doc.at("loop").get<bool>(); // 缺省 true
        std::vector<uint32_t> frames;
        bool ok = true;
        for (const nlohmann::json& fr : doc.at("frames")) {
            if (!fr.is_object() || !fr.contains("sheet") || !fr.contains("cell")) {
                ok = false;
                break;
            }
            const uint64_t sheetGuid =
                AssetDatabase::HexToGuid(fr.at("sheet").get<std::string>().c_str());
            const AssetEntry* sheet = assets_.FindByGuid(sheetGuid);
            const uint32_t cell = fr.at("cell").get<uint32_t>();
            // M6a 批② T3b-1：整图引用——未切片 sheet 的 cell 0 = 本体号（文件夹
            // 多单图动画，一帧一图）；切片表照旧 cell 界内连号
            uint32_t spriteId = 0;
            if (sheet && !sheet->missing && sheet->type == AssetType::Sprite) {
                if (sheet->Sliced())
                    spriteId = sheet->SliceSpriteId(cell);
                else if (cell == 0)
                    spriteId = sheet->spriteId;
            }
            if (spriteId == 0) {
                LEMON_WARN("clip 帧悬空（sheet 缺失/未切片且 cell≠0/cell 越界 %u）：%s 帧 %zu——跳过该 clip",
                           cell, e.relPath.c_str(), frames.size());
                ok = false;
                break;
            }
            frames.push_back(spriteId);
        }
        if (!ok) continue;
        // T3d 批③：帧事件表（可选 events[]；宽容解析——坏事件跳过不炸 clip，
        // 与帧表同款"帧必须可解析"校验已在 ClipEdit 侧拦，此处防手写档越界）
        std::vector<ecs::ClipEventDef> events;
        if (doc.contains("events") && doc.at("events").is_array()) {
            for (const nlohmann::json& ev : doc.at("events")) {
                if (!ev.is_object() || !ev.contains("frame") || !ev.at("frame").is_number())
                    continue;
                const uint32_t frame = ev.at("frame").get<uint32_t>();
                if (frame >= frames.size()) continue;
                ecs::ClipEventDef d;
                d.frame = (uint16_t)frame;
                if (ev.contains("id") && ev.at("id").is_number())
                    d.id = (uint16_t)ev.at("id").get<uint32_t>();
                events.push_back(d);
            }
        }
        const uint32_t clipId = (uint32_t)e.guid; // 低 32 位（映射约定同 prefabId）
        if (!playWorld_->Clips().Add(clipId, std::move(frames), fps, loop, std::move(events)))
            LEMON_WARN("clip 登记失败（空帧/fps 非法）：%s", e.relPath.c_str());
        else
            LEMON_LOG("Play clip 表：'%s' → id %08x（%zu 帧 @%.1ffps）", e.relPath.c_str(),
                      clipId, playWorld_->Clips().Find(clipId)->frames.size(), fps);
    }

    // ---- T3c 动画集（.override → 集按名索引；EnterPlay 快照同语义，Play 中改不生效）。
    // Entries() = relPath 升序 → 同段入多集/同集重名一律"路径序先到先得"，可复现。
    // 悬空段（clip 缺失/未登记）跳过不炸 Play；空集合法（脚本按名 miss = 报错）。
    for (const AssetEntry& e : assets_.Entries()) {
        if (e.type != AssetType::AnimSet || e.missing) continue;
        std::ifstream f(assets_.AbsolutePath(e), std::ios::binary);
        if (!f) continue;
        std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        const editor::AnimSetData set = editor::ParseAnimSetJson(text);
        if (!set.ok) {
            LEMON_WARN("animset 解析失败：%s——%s", e.relPath.c_str(), set.error.c_str());
            continue;
        }
        const uint32_t setId = (uint32_t)e.guid; // 低 32 位（clipId/prefabId 同款映射）
        std::vector<std::pair<std::string, uint32_t>> segs;
        std::unordered_set<std::string> seenNames;
        for (const editor::AnimSetSeg& sg : set.segments) {
            if (!seenNames.insert(sg.name).second) {
                LEMON_WARN("animset 重名段（按名解析取先者）：%s「%s」", e.relPath.c_str(),
                           sg.name.c_str());
                continue;
            }
            const AssetEntry* clip = assets_.FindByGuid(sg.clipGuid);
            if (!clip || clip->missing || clip->type != AssetType::Clip) {
                LEMON_WARN("animset 段悬空（clip 缺失/非 clip）：%s「%s」", e.relPath.c_str(),
                           sg.name.c_str());
                continue;
            }
            const uint32_t cid = (uint32_t)clip->guid;
            if (!playWorld_->Clips().Find(cid)) {
                LEMON_WARN("animset 段未登记（clip 内容坏/空帧）：%s「%s」", e.relPath.c_str(),
                           sg.name.c_str());
                continue;
            }
            if (const uint32_t prevSet = playWorld_->Clips().SetOfClip(cid); prevSet != 0) {
                LEMON_WARN("animset 段已属其他集（按名归先集）：%s「%s」", e.relPath.c_str(),
                           sg.name.c_str());
                continue;
            }
            segs.emplace_back(sg.name, cid);
        }
        const size_t n = playWorld_->Clips().RegisterSet(setId, segs);
        LEMON_LOG("Play 动画集：'%s' → id %08x（%zu/%zu 段）", e.relPath.c_str(), setId, n,
                  set.segments.size());
    }
}

// ---- M6a 批② T3d：Play 世界状态机表（.controller JSON → ControllerTable；
// ADR-013 D1 决策层。进 Play 时刻快照（同 BuildPlayClipCache 语义，Play 中改
// .controller 不生效）。坏 controller 红字跳过不炸 Play——实体 AnimGraph 绑定
// 未命中表 = AnimGraphSystem 旁路（不绑图的纯集绑定不受影响）。字符串形态
//（ControllerData）编译为下标形态（ControllerDef：from/to/param 全部定序槽位，
// 运行时零字符串查找）。
void EditorContext::BuildPlayControllerCache() {
    playWorld_->Controllers().Clear();
    for (const AssetEntry& e : assets_.Entries()) {
        if (e.type != AssetType::Controller || e.missing) continue;
        std::ifstream f(assets_.AbsolutePath(e), std::ios::binary);
        if (!f) continue;
        std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        const editor::ControllerData c = editor::ParseControllerJson(text);
        if (!c.ok) {
            LEMON_WARN("controller 解析失败：%s——%s", e.relPath.c_str(), c.error.c_str());
            continue;
        }
        ecs::ControllerDef def;
        def.states = c.states;
        def.entry = c.entry.empty() ? 0 : def.StateIndex(c.entry);
        if (def.entry < 0) def.entry = 0; // 解析已拦，防御手改档
        for (const editor::ControllerParamEdit& p : c.params)
            def.params.push_back({p.name, (ecs::AnimParamKind)p.kind, p.def});
        for (const editor::ControllerTransitionEdit& t : c.transitions) {
            ecs::AnimTransitionDef td;
            td.from = (uint16_t)def.StateIndex(t.from);
            td.to = (uint16_t)def.StateIndex(t.to);
            td.exitTime = t.exitTime;
            for (const editor::ControllerCondEdit& cd : t.conds)
                td.conds.push_back({(uint16_t)def.ParamIndex(cd.param),
                                    (ecs::AnimCondOp)cd.op, cd.value});
            def.transitions.push_back(std::move(td));
        }
        const uint32_t controllerId = (uint32_t)e.guid; // 低 32 位（同款映射约定）
        if (!playWorld_->Controllers().Add(controllerId, std::move(def)))
            LEMON_WARN("controller 登记失败（空 states）：%s", e.relPath.c_str());
        else
            LEMON_LOG("Play 状态机：'%s' → id %08x（%zu 状态/%zu 过渡/%zu 参数）",
                      e.relPath.c_str(), controllerId,
                      playWorld_->Controllers().Find(controllerId)->states.size(),
                      playWorld_->Controllers().Find(controllerId)->transitions.size(),
                      playWorld_->Controllers().Find(controllerId)->params.size());
    }
}

// ---- M6a 批② T2：Play 世界配置表（.tab JSON → TableStore；ADR-012 D1）----
// 格式（Assets/Csv.h）：{ schemaVersion:1, name, rows[[]...] 全字符串格，第 0 行 =
// 列头 }。键 = 资产 GUID 低 32 位（clipId/prefabId 同款映射约定）。进 Play 时刻
// 快照（BuildPlayClipCache 同语义）；坏表红字跳过不炸 Play（行×列×格字符上限归
// ParseTableJson——超限即坏表）。Play 中改 .tab 不生效（表格区提示行已交代）。
void EditorContext::BuildPlayTableCache() {
    playWorld_->Tables().Clear();
    for (const AssetEntry& e : assets_.Entries()) {
        if (e.type != AssetType::Table || e.missing) continue;
        std::ifstream f(assets_.AbsolutePath(e), std::ios::binary);
        if (!f) continue;
        std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        TableData t = ParseTableJson(text);
        if (!t.ok) {
            LEMON_WARN("表解析失败（%s）：%s——跳过", t.error.c_str(), e.relPath.c_str());
            continue;
        }
        const uint32_t id = (uint32_t)e.guid; // 低 32 位（映射约定同 clipId）
        const size_t rowCount = t.rows.size();
        const uint32_t colCount = t.Cols();
        if (!playWorld_->Tables().Add(id, std::move(t.rows))) {
            LEMON_WARN("表登记失败（空网格）：%s", e.relPath.c_str());
            continue;
        }
        LEMON_LOG("Play 表：'%s' → id %08x（%zu 行 × %u 列）", e.relPath.c_str(), id,
                  rowCount, colCount);
    }
}

bool EditorContext::ApplyPrefabInstance(ecs::Entity e) {
    if (e.IsNull() || !scene_->Alive(e)) return false;
    const ecs::Meta* m = scene_->TryGet<ecs::Meta>(e);
    const AssetEntry* entry = m ? assets_.FindByGuid(m->prefabId) : nullptr;
    if (!entry || entry->missing) {
        LEMON_WARN("Apply 失败：prefab 资产缺失（guid %016llx）",
                   (unsigned long long)(m ? m->prefabId : 0));
        return false;
    }
    std::string json = SceneArchive::SaveEntityTree(*scene_, e);
    if (!WriteFileAtomic(assets_.AbsolutePath(*entry), json.data(), json.size()))
        return false; // F-04：原子写——Apply 中断不再截断源 Prefab 资产
    LEMON_LOG("Prefab Apply：实例写回 %s", entry->relPath.c_str());
    return true;
}

bool EditorContext::RevertPrefabInstance(ecs::Entity e) {
    if (e.IsNull() || !scene_->Alive(e)) return false;
    const ecs::Meta* m = scene_->TryGet<ecs::Meta>(e);
    const AssetEntry* entry = m ? assets_.FindByGuid(m->prefabId) : nullptr;
    if (!entry || entry->missing) return false;
    std::ifstream f(assets_.AbsolutePath(*entry), std::ios::binary);
    if (!f) return false;
    std::string json((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    // F-04：销毁旧树前先整档预验——坏 JSON/结构错直接拒绝，不再出现"旧树已毁、
    // 新树解析失败"的不可回滚中间态（LoadEntityTree 内部再解析一次，双验代价可忽略）
    {
        const nlohmann::json pre = nlohmann::json::parse(json, nullptr, false);
        if (pre.is_discarded() || !pre.contains("entities") || !pre.at("entities").is_array()) {
            LEMON_WARN("Prefab Revert 失败：源资产损坏 %s", entry->relPath.c_str());
            return false;
        }
    }

    const ecs::Hierarchy* h = scene_->TryGet<ecs::Hierarchy>(e);
    const ecs::Entity parent = h ? h->parent : ecs::Entity::Null();
    // guid/prefabId 都按值保存：下方销毁 + LoadEntityTree 都可能使 Meta 池扩容搬移，
    // 旧指针 m 在 rm->prefabId = m->prefabId 处即悬空（keepGuid 同理，2026-09-24 审查）
    const uint64_t keepGuid = m ? m->guid : 0; // Revert 保持实例自身 guid（选中集/引用找回）
    const uint64_t keepPrefabId = m ? m->prefabId : 0;
    SceneDestroyEntityTree(*scene_, e);
    ecs::Entity root = SceneArchive::LoadEntityTree(*scene_, json);
    if (root.IsNull()) return false;
    if (ecs::Meta* rm = scene_->TryGet<ecs::Meta>(root)) {
        rm->prefabId = keepPrefabId;
        if (keepGuid) rm->guid = keepGuid;
    }
    if (!parent.IsNull()) SceneSetParent(*scene_, root, parent);
    PruneSelection();
    Select(root, false);
    dirty = true;
    LEMON_LOG("Prefab Revert：实例回到源资产态 %s", entry->relPath.c_str());
    return true;
}

void EditorContext::BreakPrefabInstance(ecs::Entity e) {
    if (e.IsNull() || !scene_->Alive(e)) return;
    if (ecs::Meta* m = scene_->TryGet<ecs::Meta>(e); m && m->prefabId) {
        m->prefabId = 0;
        dirty = true;
        LEMON_LOG("Prefab Break：断链成普通实体");
    }
}

ecs::Entity EditorContext::DuplicateEntity(ecs::Entity e) {
    // Play 态复制落 play 世界（与 Create 同口径；句柄 e 属 ActiveScene，经 edit 世界
    // 的 SaveEntityTree 轻 = 越池访问/错复制）
    ecs::Scene& s = ActiveScene();
    if (e.IsNull() || !s.Alive(e)) return ecs::Entity::Null();
    // C8：整子树复制——走 CopySelection/PasteClipboard 同一 SaveEntityTree/
    // LoadEntityTree 链（组件全量、子树内 EntityRef 重映射/跨树置空、guid 全换新）。
    // 此前只平移单实体组件表（"Hierarchy 不复制"），父子链 Ctrl+D 只得根。
    // 副本原位（对齐 Unity Ctrl+D；粘贴的 +24/+24 防叠偏移不在此路径）
    const std::string tree = ecs::SceneArchive::SaveEntityTree(s, e);
    ecs::Entity copy = ecs::SceneArchive::LoadEntityTree(s, tree);
    if (!copy.IsNull()) dirty = true;
    return copy;
}

void EditorContext::DestroyEntityTree(ecs::Entity e) {
    // Play 态删除作用于 play 世界（选中句柄来自 ActiveScene；原恒走 scene_ = 拿
    // play 句柄戳 edit 世界，no-op 或 id 撞车错删）
    ecs::Scene& s = ActiveScene();
    if (e.IsNull() || !s.Alive(e)) return;
    SceneDestroyEntityTree(s, e);
    // 立即提交销毁（默认帧末 DestroyCommit 系统做）——结构轨 PushStructuralUndo
    // 在调用方"销毁后"快照 after，若销毁仍在队列中，快照含待删实体 → Redo 会
    // 复活被删实体（smoke-ui 真人链路抓到；右键删除/Delete 键同路径）。
    s.CommitDestroys();
    PruneSelection();
    dirty = true;
}

bool EditorContext::IsSelected(ecs::Entity e) const {
    for (auto& s : selection_)
        if (s == e) return true;
    return false;
}

void EditorContext::Select(ecs::Entity e, bool additive) {
    if (!additive) selection_.clear();
    // 已选则去重（Ctrl 点选已选项 = 取消，Unity 心智）
    for (auto it = selection_.begin(); it != selection_.end(); ++it) {
        if (*it == e) {
            if (additive) {
                selection_.erase(it);
                return;
            }
            selection_.clear();
            selection_.push_back(e); // 单击已选主对象 = 保持主选中
            return;
        }
    }
    selection_.push_back(e);
}

void EditorContext::ClearSelection() { selection_.clear(); }

void EditorContext::PruneSelection() {
    // 按当前可视场景校验（Play 期间 = Play 世界）：Inspector 每帧调用——若恒按编辑
    // 场景校验，Play 中的任何选中下一帧即被误清（Play 中无法选中/检视实体的根因）。
    // 编辑选区不会跨 Play 存活（EnterPlay 清空 + guid 恢复），无 id 撞车歧义。
    std::vector<ecs::Entity> keep;
    keep.reserve(selection_.size());
    for (auto e : selection_)
        if (ActiveScene().Alive(e)) keep.push_back(e);
    selection_.swap(keep);
}

// ------------------------------------------------------------- Play 沙盒 ----
// ---- 游戏存档 IO（M5 批④ D1；06 §10 防损坏三件套：版本头 + 原子改名 + .bak）----
std::string EditorContext::SaveFilePath() const {
    const std::string& root = assets_.ProjectRoot();
    return root.empty() ? std::string() : root + "/.lemon/saves/game.sav";
}

bool EditorContext::WriteSaveFile(const ecs::SaveChannel& ch) {
    namespace fs = std::filesystem;
    const std::string path = SaveFilePath();
    if (path.empty() || ch.Count() == 0) return false;
    std::error_code ec;
    const fs::path p(path);
    fs::create_directories(p.parent_path(), ec);
    // 上一代转备份（首次落盘无 .bak 属正常）
    if (fs::exists(p, ec))
        fs::copy_file(p, fs::path(path + ".bak"), fs::copy_options::overwrite_existing, ec);
    const std::vector<uint8_t> bytes = ch.Encode();
    if (!WriteFileAtomic(path, bytes.data(), bytes.size())) { // F-04：统一原子写（含 flush 检查）
        LEMON_WARN("存档写入失败（临时写入/改名）：%s", path.c_str());
        return false;
    }
    return true;
}

void EditorContext::LoadSaveFile(ecs::SaveChannel& dst) {
    namespace fs = std::filesystem;
    // 坏档防线（2026-09-24 审查 F-03）：整档上限 16 MiB，超限不 slurp——
    // 现用途 KB 级；超大文件多为损坏/误指，Decode 侧另有条目/单值上限。
    constexpr uint64_t kMaxSaveFileBytes = 16ull << 20;
    const std::string path = SaveFilePath();
    if (path.empty()) return; // 无项目（bench/smoke tempdir 外的裸会话）= 空通道开局
    auto tryDecode = [&](const std::string& p) {
        std::error_code ec;
        if (!fs::exists(p, ec)) return false;
        if (const uint64_t sz = fs::file_size(p, ec); ec || sz > kMaxSaveFileBytes) {
            LEMON_WARN("存档异常（大小超 16 MiB 上限），已跳过：%s", p.c_str());
            return false;
        }
        std::ifstream f(p, std::ios::binary);
        if (!f) return false;
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)),
                                   std::istreambuf_iterator<char>());
        if (!dst.Decode(bytes.data(), bytes.size())) {
            LEMON_WARN("存档损坏，已跳过：%s", p.c_str());
            return false;
        }
        return true;
    };
    if (!tryDecode(path)) tryDecode(path + ".bak"); // 主档坏 → 备份兜底
}

bool EditorContext::EnterPlay() {
    if (Playing()) return false;
    const auto t0 = std::chrono::steady_clock::now();
    // §3.4-1：先固化快照（未保存改动进快照但不落盘，dirty 保持）
    editSnapshot_ = SceneArchive::Save(*scene_);
    // §3.4-3：playWorld ← Load(快照)；同 seed（确定性；编辑世界种子同源）
    playWorld_ = std::make_unique<ecs::World>(world_->Desc());
    playWorld_->InstallDefaultSystems(); // 16 系统全量管线（游戏语义）
    playScene_ = &playWorld_->CreateScene("play");
    playWorld_->SetActiveScene(playScene_);
    if (!SceneArchive::Load(*playScene_, editSnapshot_)) {
        LEMON_WARN("Play 沙盒装载失败（快照解析异常）");
        playWorld_.reset();
        playScene_ = nullptr;
        return false;
    }
    // M5 清障②：Play 世界刷怪工厂（Spawner/Shooter 的 prefabId 低 32 位 → prefab
    // 资产实例化；进 Play 时刻缓存——纯运行时 World 无此桥，SpawnSystem 原告警路径保留）
    BuildPlayPrefabCache();
    playWorld_->SetSpawnFn([this](ecs::Scene& s, uint32_t prefabId, Vec2 pos, uint32_t team) {
        return SpawnPlayPrefab(s, prefabId, pos, team);
    });
    // M5 批③：clip 表（.anim 资产 → 帧映射；AnimatorSystem #13 消费）
    BuildPlayClipCache();
    // M6a 批② T3d：状态机表（.controller 资产 → 出边评估；AnimGraphSystem 消费）
    BuildPlayControllerCache();
    // M6a 批② T2：配置表（.tab 资产 → 全字符串格网格；C# Lemon.Table 读）
    BuildPlayTableCache();
    // M5 批④：存档载入（进 Play 快照语义：上一局数据进通道，C# Save.Get 即读）
    LoadSaveFile(playWorld_->Saves());
    // M4.4 装配通路（#7）：脚本后端接入 + 场景 ScriptBox 按 className 解析挂载
    if (scripts_) {
        playWorld_->SetScriptBackend(scripts_);
        scripts_->ResetScriptTime(); // M5 清障①：进 Play = 新的一局，Time 归零
        scripts_->ResetPlayDomain(); // M5 批④后修：清上局残留实例/事件订阅（防同
                                     // 实体双实例双 tick——Blade 每局递增的根因）
        ResolvePlayScripts();
    }
    // §3.4-4：清 Undo 并禁用；选中集快照后清空
    undo_.Clear();
    savedSelectionGuids_.clear();
    for (ecs::Entity e : selection_)
        if (const ecs::Meta* m = scene_->TryGet<ecs::Meta>(e); m && m->guid)
            savedSelectionGuids_.push_back(m->guid);
    selection_.clear();
    lastEnterMs_ = std::chrono::duration<double, std::milli>(
                       std::chrono::steady_clock::now() - t0)
                       .count();
    LEMON_LOG("进 Play（%.1fms）：快照 %zu 字节，%u 实体", lastEnterMs_, editSnapshot_.size(),
              playScene_->AliveCount());
    return true;
}

bool EditorContext::ExitPlay() {
    if (!Playing()) return false;
    const auto t0 = std::chrono::steady_clock::now();
    // M5 批④：存档兜底落盘（脚本显式 Flush 之外的保险——中断退 Play 不丢局）
    WriteSaveFile(playWorld_->Saves());
    // §3.4-1：弃 playWorld（两阶段销毁随 World 析构；renderable 由视口提取差集释放）
    playWorld_.reset();
    playScene_ = nullptr;
    // §3.4-2：editScene ← Load(快照) 整体重建（零状态泄漏的结构保证）
    if (!SceneArchive::Load(*scene_, editSnapshot_)) {
        LEMON_WARN("Stop 恢复失败（快照解析异常）——编辑场景可能损坏");
        lastExitVerified_ = false;
        return false;
    }
    BackfillGuids();
    // M6a 批⓪ T2：Play 期资产可能被改名/移位（watcher 不因 Play 停摆）——按
    // guid 归一到新 id。放在逐字节比对前：无资产变动 = 幂等无写、比对照常成立；
    // 真有变动 = 比对如实报"与快照不一致"（引用升级，非 Play 状态泄漏）。
    ResolveSpriteRefs();
    // §3.4-3：恢复选中集（按 guid 找回）
    selection_.clear();
    for (uint64_t g : savedSelectionGuids_)
        if (ecs::Entity e = FindByGuid(g); !e.IsNull()) selection_.push_back(e);
    // 验收 #5：Stop 后序列化 == 进 Play 前快照（逐字节）
    lastExitVerified_ = SceneArchive::Save(*scene_) == editSnapshot_;
    lastExitMs_ = std::chrono::duration<double, std::milli>(
                      std::chrono::steady_clock::now() - t0)
                      .count();
    LEMON_LOG("退 Play（%.1fms）：编辑场景重建 %s", lastExitMs_,
              lastExitVerified_ ? "逐字节一致 ✔" : "!! 与快照不一致");
    return lastExitVerified_;
}

void EditorContext::TickEditor(float dt) {
    world_->Step(dt);
    if (scene_->PendingDestroyCount() > 0) scene_->CommitDestroys();
}

void EditorContext::TickPlay(float dt) {
    playWorld_->Step(dt);
    if (playScene_->PendingDestroyCount() > 0) playScene_->CommitDestroys();
}

// ---------------------------------------------------------------- Undo ----
std::vector<uint8_t> EditorContext::SnapshotComponent(ecs::Entity e, uint16_t compId) {
    auto& reg = ecs::ComponentRegistry::Instance();
    const ecs::ComponentMeta& meta = reg.At(compId);
    void* comp = meta.getFn ? meta.getFn(ActiveScene(), e) : nullptr;
    if (!comp || meta.sizeOf == 0) return {};
    return std::vector<uint8_t>((uint8_t*)comp, (uint8_t*)comp + meta.sizeOf);
}

ecs::Entity EditorContext::FindByGuid(uint64_t guid) const {
    ecs::Entity found = ecs::Entity::Null();
    const_cast<EditorContext*>(this)->ActiveScene().Each([&](ecs::Entity e) {
        const ecs::Meta* m = const_cast<EditorContext*>(this)->ActiveScene().TryGet<ecs::Meta>(e);
        if (m && m->guid == guid) found = e;
    });
    return found;
}

void EditorContext::PushPropertyUndo(const char* name, uint64_t guid, uint16_t compId,
                                     std::vector<uint8_t> before, std::vector<uint8_t> after) {
    if (Playing()) return; // Play 中禁用（§2.4）
    undo_.Push({name, [this, guid, compId, b = std::move(before), a = std::move(after)](bool u) {
                    ecs::Entity e = FindByGuid(guid);
                    if (e.IsNull()) {
                        LEMON_WARN("Undo 目标实体不存在（guid %016llx 丢失）",
                                   (unsigned long long)guid);
                        return;
                    }
                    auto& reg = ecs::ComponentRegistry::Instance();
                    const ecs::ComponentMeta& meta = reg.At(compId);
                    void* comp = meta.getFn ? meta.getFn(ActiveScene(), e) : nullptr;
                    if (!comp) return;
                    const std::vector<uint8_t>& src = u ? b : a;
                    if (src.size() != meta.sizeOf) {
                        LEMON_WARN("Undo 组件尺寸漂移（%s）", meta.name);
                        return;
                    }
                    std::memcpy(comp, src.data(), meta.sizeOf);
                    dirty = true;
                }});
    dirty = true;
}

std::string EditorContext::SnapshotSceneJson() { return SceneArchive::Save(ActiveScene()); }

void EditorContext::PushStructuralUndo(const char* name, const std::string& beforeJson) {
    if (Playing()) return;
    const std::string afterJson = SceneArchive::Save(ActiveScene());
    undo_.Push({name, [this, b = beforeJson, a = afterJson](bool u) {
                    const std::string& json = u ? b : a;
                    if (!SceneArchive::Load(ActiveScene(), json)) {
                        LEMON_WARN("结构 Undo 恢复失败");
                        return;
                    }
                    BackfillGuids();
                    ResolveSpriteRefs(); // M6a 批⓪ T2：快照可能先于资产变动
                    PruneSelection();
                    dirty = true;
                }});
    dirty = true;
}

void EditorContext::BackfillGuids() {
    scene_->Each([this](ecs::Entity e) {
        if (!scene_->Has<ecs::Meta>(e)) return;
        ecs::Meta& m = scene_->Get<ecs::Meta>(e);
        if (m.guid == 0) m.guid = GenerateGuid();
    });
}

// ---- M6a 批⓪ T2：sprite 引用 GUID 主键化 ----
// .scene 双写 {spriteGuid（真源，.meta 随文件走）, spriteId（AtlasRegistry 进程内
// 派生号）}。装载/恢复/Undo 重建/编辑态 Prefab 落地后调本函数归一：
//   * guid≠0 命中 → 覆写 spriteId（改名/移位/manifest 重建 id 漂移后引用不断链）
//   * guid≠0 查无/missing/非 sprite → 保留 spriteId + 计入 danglingGuid（渲染仍
//     用旧号，Inspector sprite 槽 ⚠ 重指可修；不静默清零）
//   * guid=0 且 spriteId 恰为资产本体号 → 回填 guid（FindBySpriteId 只匹配本体
//     号：切片 cell 号/程序化页号查无 = 天然不回填，防 cell 号被升级成整图
//     guid 后解析覆写掉切片）。回填标 dirty——存量档下次保存即升级 guid 主键。
// Play 期 spawn 工厂（InstantiatePrefabJson 高频路径）不走此函数：同进程 id 即
// 真值，热路径保持零扫表。
SpriteRefStats EditorContext::ResolveSpriteRefs() {
    SpriteRefStats st;
    if (assets_.ProjectRoot().empty()) return st;
    scene_->Each([&](ecs::Entity e) {
        ecs::SpriteRenderer* sr = scene_->TryGet<ecs::SpriteRenderer>(e);
        if (!sr) return;
        if (sr->spriteGuid != 0) {
            const AssetEntry* en = assets_.FindByGuid(sr->spriteGuid);
            if (en && !en->missing && en->spriteId != 0) {
                // id 落在合法域内 = 已是当前进程真值，保号不覆写（guid 只锚资产，
                // cell 是层内偏移）：切片表 = cell 区间 [sliceBase, +count)，整图 =
                // 本体号。切片表的本体号与 cell 号相邻——跨进程漂移后旧 cell-0 可
                // 能恰好撞新本体号（smoke-guid BossMob 实证），无法甄别 → 切片表
                // 一律按 cell 口径归一：区间外回 cell 0（本体引用降级 cell 0，
                // 逐 cell guid 化超出本批范围）；整图区间外回本体号
                const uint32_t cur = sr->spriteId;
                const bool inRange =
                    (en->sliceCount == 0 && cur == en->spriteId) ||
                    (en->sliceCount > 0 && cur >= en->sliceBase &&
                     cur < en->sliceBase + en->sliceCount);
                if (!inRange)
                    sr->spriteId = en->sliceCount > 0 ? en->sliceBase : en->spriteId;
            } else {
                ++st.danglingGuid;
            }
        } else if (sr->spriteId >= assets_.SpriteIdBase()) {
            if (const AssetEntry* en = assets_.FindBySpriteId(sr->spriteId);
                en && en->type == AssetType::Sprite) {
                sr->spriteGuid = en->guid;
                ++st.backfilled;
            }
        }
    });
    if (st.backfilled) {
        dirty = true;
        LEMON_LOG("sprite 引用：%u 个存量 spriteId 回填 guid（保存后生效）", st.backfilled);
    }
    return st;
}

} // namespace lemon::editor
