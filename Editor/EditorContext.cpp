// Lemon 编辑器 — EditorContext 实现（场景 IO / 实体操作 / 选择集）
#include "EditorContext.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>

#include <nlohmann/json.hpp>

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
    static const char* kEmpty = R"({"schemaVersion":1,"name":"untitled","entities":[]})";
    SceneArchive::Load(*scene_, kEmpty);
    scenePath_.clear();
    selection_.clear();
    dirty = false;
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
    scenePath_ = path;
    selection_.clear();
    BackfillGuids();
    // 悬空 spriteId 聚合告警（2026-09-22 测试报告观察 6）：AtlasRegistry 无此 id =
    // 渲染静默缺失（"sprite 不显示"排查半天的第一案发现场）。装载期一次性列出
    // 计数，不逐实体刷屏。合法域 = 程序化页（< 基号）∪ DB 记账号（含墓碑——
    // 源文件缺失走 Inspector ⚠，不在此重复报）。0 = 未设置，跳过。
    if (!assets_.ProjectRoot().empty()) {
        uint32_t dangling = 0;
        scene_->Each([&](ecs::Entity e) {
            if (const ecs::SpriteRenderer* sr = scene_->TryGet<ecs::SpriteRenderer>(e)) {
                const uint32_t id = sr->spriteId;
                if (id != 0 && id >= assets_.SpriteIdBase() &&
                    !assets_.SpriteIdRegistered(id)) // M5 批④：含切片区间（模板场景
                    ++dangling;                      // 引用 cell 号是常态，勿误报）
            }
        });
        if (dangling)
            LEMON_WARN("场景装载：%u 个 SpriteRenderer.spriteId 未在资产库（渲染将缺失；"
                       "Inspector sprite 槽重指可修）", dangling);
    }
    dirty = false;
    RecordRecentScene(path);
    LEMON_LOG("场景已打开：%s（%u 实体）", path.c_str(), scene_->AliveCount());
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
            const std::string p = e.get<std::string>();
            if (p.empty()) continue; // 脏档清洗：自别名 UAF 曾写入空串
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
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) {
        LEMON_WARN("保存场景失败：无法写入 %s", path.c_str());
        return false;
    }
    f.write(json.data(), (std::streamsize)json.size());
    if (!f.good()) {
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
    ecs::Entity e = scene_->Create();
    scene_->Emplace<ecs::Transform2D>(e);
    ecs::Meta& m = scene_->Emplace<ecs::Meta>(e);
    m.guid = GenerateGuid();
    std::snprintf(m.tag, sizeof(m.tag), "%s", tag);
    dirty = true;
    return e;
}

ecs::Entity EditorContext::CreateSpriteEntity(const char* tag, uint32_t spriteId) {
    ecs::Entity e = CreateEntity(tag);
    ecs::SpriteRenderer& sr = scene_->Emplace<ecs::SpriteRenderer>(e); // 默认启用
    sr.spriteId = spriteId;
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
    scene_->Get<ecs::Transform2D>(e).pos = pos;
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
    scripting::ScriptBox& sb = scene_->Has<scripting::ScriptBox>(e)
                                   ? scene_->Get<scripting::ScriptBox>(e)
                                   : scene_->Emplace<scripting::ScriptBox>(e);
    sb.scriptGuid = assetGuid;
    std::memset(sb.className, 0, sizeof(sb.className));
    std::snprintf(sb.className, sizeof(sb.className), "%s", className ? className : "");
    sb.typeId = ResolveScriptTypeId(sb.className);
    sb.flags &= ~1u;
    dirty = true;
}

uint32_t EditorContext::SpriteIdOfGuidHex(const char* hex) const {
    const AssetEntry* e = assets_.FindByGuid(AssetDatabase::HexToGuid(hex));
    return e && !e->missing ? e->spriteId : 0;
}

void EditorContext::ResolvePlayScripts() {
    if (!scripts_ || !playScene_) return;
    playScene_->Each([this](ecs::Entity e) {
        scripting::ScriptBox* sb = playScene_->TryGet<scripting::ScriptBox>(e);
        if (!sb || sb->typeId >= 0) return;
        int id = ResolveScriptTypeId(sb->className);
        if (id < 0) {
            LEMON_WARN("Play 装配：脚本类型未注册（跳过）'%s'", sb->className);
            return;
        }
        scripts_->AttachBehaviour(*playWorld_, *playScene_, e, id);
    });
}

int EditorContext::RefreshScriptsAfterReload() {
    if (!scripts_) return 0;
    // Edit 世界：只刷 typeId（编辑器不 tick；EnterPlay 时本就按 className 解析）
    scene_->Each([this](ecs::Entity e) {
        if (scripting::ScriptBox* sb = scene_->TryGet<scripting::ScriptBox>(e))
            sb->typeId = ResolveScriptTypeId(sb->className);
    });
    if (!playScene_) return 0;
    // Play 世界：原位换实例——AttachBehaviour 走新域 scripts_attach（Behaviours.Attach
    // → Awake/OnEnable → 同 (类名,实体) StateBag → OnHotReloadIn）
    int n = 0;
    playScene_->Each([this, &n](ecs::Entity e) {
        scripting::ScriptBox* sb = playScene_->TryGet<scripting::ScriptBox>(e);
        if (!sb || !sb->className[0]) return;
        int id = ResolveScriptTypeId(sb->className);
        if (id < 0) {
            LEMON_WARN("热重载：脚本类型未注册（保持挂起）'%s'", sb->className);
            return;
        }
        sb->typeId = id;
        sb->flags &= ~1u;
        scripts_->AttachBehaviour(*playWorld_, *playScene_, e, id);
        ++n;
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

// ---- M5 批③：Play 世界 clip 表（.clip JSON → ClipTable；06 §2.2 / 03 §5）----
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
            const uint32_t spriteId =
                sheet && !sheet->missing && sheet->type == AssetType::Sprite
                    ? sheet->SliceSpriteId(cell)
                    : 0;
            if (spriteId == 0) {
                LEMON_WARN("clip 帧悬空（sheet 缺失/未切片/cell 越界 %u）：%s 帧 %zu——跳过该 clip",
                           cell, e.relPath.c_str(), frames.size());
                ok = false;
                break;
            }
            frames.push_back(spriteId);
        }
        if (!ok) continue;
        const uint32_t clipId = (uint32_t)e.guid; // 低 32 位（映射约定同 prefabId）
        if (!playWorld_->Clips().Add(clipId, std::move(frames), fps, loop))
            LEMON_WARN("clip 登记失败（空帧/fps 非法）：%s", e.relPath.c_str());
        else
            LEMON_LOG("Play clip 表：'%s' → id %08x（%zu 帧 @%.1ffps）", e.relPath.c_str(),
                      clipId, playWorld_->Clips().Find(clipId)->frames.size(), fps);
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
    std::ofstream f(assets_.AbsolutePath(*entry), std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << json;
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

    const ecs::Hierarchy* h = scene_->TryGet<ecs::Hierarchy>(e);
    const ecs::Entity parent = h ? h->parent : ecs::Entity::Null();
    const uint64_t keepGuid = m ? m->guid : 0; // Revert 保持实例自身 guid（选中集/引用找回）
    SceneDestroyEntityTree(*scene_, e);
    ecs::Entity root = SceneArchive::LoadEntityTree(*scene_, json);
    if (root.IsNull()) return false;
    if (ecs::Meta* rm = scene_->TryGet<ecs::Meta>(root)) {
        rm->prefabId = m->prefabId;
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
    if (e.IsNull() || !scene_->Alive(e)) return ecs::Entity::Null();
    // C8：整子树复制——走 CopySelection/PasteClipboard 同一 SaveEntityTree/
    // LoadEntityTree 链（组件全量、子树内 EntityRef 重映射/跨树置空、guid 全换新）。
    // 此前只平移单实体组件表（"Hierarchy 不复制"），父子链 Ctrl+D 只得根。
    // 副本原位（对齐 Unity Ctrl+D；粘贴的 +24/+24 防叠偏移不在此路径）
    const std::string tree = ecs::SceneArchive::SaveEntityTree(*scene_, e);
    ecs::Entity copy = ecs::SceneArchive::LoadEntityTree(*scene_, tree);
    if (!copy.IsNull()) dirty = true;
    return copy;
}

void EditorContext::DestroyEntityTree(ecs::Entity e) {
    if (e.IsNull() || !scene_->Alive(e)) return;
    SceneDestroyEntityTree(*scene_, e);
    // 立即提交销毁（默认帧末 DestroyCommit 系统做）——结构轨 PushStructuralUndo
    // 在调用方"销毁后"快照 after，若销毁仍在队列中，快照含待删实体 → Redo 会
    // 复活被删实体（smoke-ui 真人链路抓到；右键删除/Delete 键同路径）。
    scene_->CommitDestroys();
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
    const fs::path tmp(path + ".tmp");
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) {
            LEMON_WARN("存档写入失败（无法创建 %s）", tmp.string().c_str());
            return false;
        }
        f.write((const char*)bytes.data(), (std::streamsize)bytes.size());
    }
    fs::rename(tmp, p, ec);
    if (ec) {
        LEMON_WARN("存档原子改名失败：%s", ec.message().c_str());
        return false;
    }
    return true;
}

void EditorContext::LoadSaveFile(ecs::SaveChannel& dst) {
    namespace fs = std::filesystem;
    const std::string path = SaveFilePath();
    if (path.empty()) return; // 无项目（bench/smoke tempdir 外的裸会话）= 空通道开局
    auto tryDecode = [&](const std::string& p) {
        std::error_code ec;
        if (!fs::exists(p, ec)) return false;
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
    // M5 批③：clip 表（.clip 资产 → 帧映射；AnimatorSystem #13 消费）
    BuildPlayClipCache();
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

} // namespace lemon::editor
