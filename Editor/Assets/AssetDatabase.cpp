// Lemon 编辑器 — 资产数据库实现（GUID/.meta/manifest/体检；06 §2）
// nlohmann/json 只进本 .cpp。FNV-1a 内容哈希（非加密——只做变更检测）。
#include "Assets/AssetDatabase.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

#include "Core/FileOps.h"
#include "Core/Guid.h"
#include "Core/Log.h"
#include "Assets/Csv.h"

namespace lemon::editor {
namespace fs = std::filesystem;
using Json = nlohmann::json;

const char* AssetTypeName(AssetType t) {
    switch (t) {
        case AssetType::Sprite: return "sprite";
        case AssetType::Prefab: return "prefab";
        case AssetType::Script: return "script";
        case AssetType::Clip: return "clip"; // M5 批③：06 §2.2 clip2d（.anim JSON）
        case AssetType::Table: return "table"; // M6a 批②：.tab 配置表（ADR-012）
        case AssetType::AnimSet: return "animset"; // M6a 批② T3c：.override 动画集容器
        case AssetType::Controller: return "controller"; // T3d：.controller 状态机
        case AssetType::Rml: return "rml";   // M6b 批③b：UI 文档（ADR-014 一屏一文档）
        case AssetType::Rcss: return "rcss"; // M6b 批③b：UI 样式表（<link> 引用）
        default: return "generic";
    }
}

bool WriteFileAtomic(const std::string& path, const void* data, size_t n) {
    const std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f.write((const char*)data, (std::streamsize)n);
        f.flush();
        if (!f.good()) { // 写失败在 flush 捕获（析构阶段的错误不再静默）
            f.close();
            std::error_code rm;
            fs::remove(tmp, rm);
            return false;
        }
    } // 析构 close
    // RenameReplace：覆盖语义钉在助手内（Windows 阻断项⑤，07 §3.6），不再依赖各
    // STL 对 fs::rename 覆盖目标的实现定义行为
    if (!RenameReplace(tmp, path)) {
        std::error_code rm;
        fs::remove(tmp, rm);
        return false;
    }
    return true;
}

std::string AssetEntry::FileName() const {
    size_t s = relPath.find_last_of('/');
    return s == std::string::npos ? relPath : relPath.substr(s + 1);
}

std::string AssetEntry::Dir() const {
    size_t s = relPath.find_last_of('/');
    return s == std::string::npos ? std::string() : relPath.substr(0, s);
}

// ---------------------------------------------------------------- 查询 ----
const AssetEntry* AssetDatabase::FindByGuid(uint64_t guid) const {
    for (const auto& e : entries_)
        if (e.guid == guid) return &e;
    return nullptr;
}

const AssetEntry* AssetDatabase::FindByPath(const std::string& relPath) const {
    for (const auto& e : entries_)
        if (e.relPath == relPath) return &e;
    return nullptr;
}

const AssetEntry* AssetDatabase::FindBySpriteId(uint32_t spriteId) const {
    if (spriteId == 0) return nullptr;
    for (const auto& e : entries_)
        if (e.spriteId == spriteId) return &e;
    return nullptr;
}

bool AssetDatabase::SpriteIdRegistered(uint32_t spriteId) const {
    if (spriteId == 0) return false;
    for (const auto& e : entries_) {
        if (e.spriteId == spriteId) return true;
        if (e.sliceCount > 0 && spriteId >= e.sliceBase &&
            spriteId < e.sliceBase + e.sliceCount) // 切片连号区间（模板场景引 cell 号）
            return true;
    }
    return false;
}

const AssetEntry* AssetDatabase::FindClipByLowId(uint32_t lowId) const {
    if (lowId == 0) return nullptr;
    for (const auto& e : entries_) {
        if (e.missing || e.type != AssetType::Clip) continue;
        if ((uint32_t)e.guid == lowId) return &e;
    }
    return nullptr;
}

std::vector<const AssetEntry*> AssetDatabase::EntriesInDir(const std::string& dirPrefix) const {
    std::vector<const AssetEntry*> out;
    const std::string prefix = dirPrefix.empty() ? "" : dirPrefix + "/";
    for (const auto& e : entries_) {
        if (e.missing) continue;
        if (e.relPath.size() <= prefix.size()) continue;
        if (e.relPath.compare(0, prefix.size(), prefix) != 0) continue;
        if (e.relPath.find('/', prefix.size()) != std::string::npos) continue; // 子目录
        out.push_back(&e);
    }
    return out;
}

std::vector<std::string> AssetDatabase::Directories() const {
    std::vector<std::string> dirs{""};
    for (const auto& e : entries_) {
        if (e.missing) continue;
        std::string d = e.Dir();
        while (!d.empty()) {
            if (std::find(dirs.begin(), dirs.end(), d) == dirs.end()) dirs.push_back(d);
            size_t s = d.find_last_of('/');
            d = s == std::string::npos ? "" : d.substr(0, s);
        }
    }
    std::sort(dirs.begin() + 1, dirs.end());
    return dirs;
}

uint32_t AssetDatabase::SpriteAssetCount() const {
    uint32_t n = 0;
    for (const auto& e : entries_)
        if (!e.missing && e.type == AssetType::Sprite) ++n;
    return n;
}

std::string AssetDatabase::AssetsRoot() const { return root_ + "/Assets"; }

std::string AssetDatabase::GuidToHex(uint64_t guid) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)guid);
    return buf;
}

uint64_t AssetDatabase::HexToGuid(const char* hex) {
    if (!hex) return 0;
    uint64_t v = 0;
    for (const char* p = hex; *p && p - hex < 16; ++p) {
        v <<= 4;
        char c = *p;
        if (c >= '0' && c <= '9') v |= (uint64_t)(c - '0');
        else if (c >= 'a' && c <= 'f') v |= (uint64_t)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= (uint64_t)(c - 'A' + 10);
        else return 0; // 非 hex 字符
    }
    return v;
}

// ---------------------------------------------------------------- 类型 ----
AssetType AssetDatabase::TypeOf(const std::string& relPath) {
    fs::path p(relPath);
    std::string ext = p.extension().string();
    for (char& c : ext) c = (char)std::tolower((unsigned char)c);
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp") return AssetType::Sprite;
    if (ext == ".prefab") return AssetType::Prefab;
    if (ext == ".cs") return AssetType::Script;
    if (ext == ".anim") return AssetType::Clip; // M5 批③帧动画资产（06 §2.2）
    if (ext == ".tab") return AssetType::Table; // M6a 批②配置表资产（ADR-012）
    if (ext == ".override") return AssetType::AnimSet; // M6a 批② T3c 动画集容器
    if (ext == ".controller") return AssetType::Controller; // T3d 动画状态机
    if (ext == ".rml") return AssetType::Rml;     // M6b 批③b UI 文档（ADR-014）
    if (ext == ".rcss") return AssetType::Rcss;   // M6b 批③b UI 样式表
    if (ext == ".wav" || ext == ".ogg" || ext == ".mp3" || ext == ".flac")
        return AssetType::Audio; // M6c 竖切批：音频源（ADR-015 D1 四格式）
    return AssetType::Generic;
}

uint64_t AssetDatabase::HashFile(const std::string& absPath) {
    std::ifstream f(absPath, std::ios::binary);
    if (!f) return 0;
    uint64_t h = 1469598103934665603ull; // FNV-1a 64 offset basis
    char buf[16384];
    while (f.read(buf, sizeof(buf)) || f.gcount() > 0) {
        std::streamsize n = f.gcount();
        for (std::streamsize i = 0; i < n; ++i) {
            h ^= (uint64_t)(uint8_t)buf[i];
            h *= 1099511628211ull;
        }
        if (!f) break;
    }
    return h;
}

// ---------------------------------------------------------------- meta ----
// importer 段（06 §2.1）网格切片声明（M5 批③）：
//   "importer": { "slice": "grid", "cell": [16, 32], "frames": [9, 1] }
// frames 由作者显式声明（yami .anim hframes/vframes 同款）——DB 零解码即可记账
// （Rescan 分配连号块）；像素整除/越界校验归 AssetGpuCache（解码侧）。
// 音频 importer 段（M6c 批①，ADR-015）："loop":[起,止(秒)]（0/0 = 全曲）+
// "preload":bool。与网格切片同款口径：每次重扫重读，热改 meta 即生效（下次烤制消费）。
static void ParseAudioImporter(const Json& doc, AssetEntry& e) {
    e.audioLoopStart = e.audioLoopEnd = 0.0f;
    e.audioPreload = false;
    auto it = doc.find("importer");
    if (it == doc.end() || !it->is_object()) return;
    const Json& imp = *it;
    if (auto loop = imp.find("loop"); loop != imp.end() && loop->is_array() && loop->size() == 2) {
        const Json& a = (*loop)[0];
        const Json& b = (*loop)[1];
        if (a.is_number() && b.is_number()) {
            const double x = a.get<double>(), y = b.get<double>();
            if (x >= 0 && y >= x) {
                e.audioLoopStart = (float)x;
                e.audioLoopEnd = (float)y;
            }
        }
    }
    if (auto pre = imp.find("preload"); pre != imp.end() && pre->is_boolean())
        e.audioPreload = pre->get<bool>();
}

static void ParseGridImporter(const Json& doc, AssetEntry& e) {
    e.cellW = e.cellH = e.gridCols = e.gridRows = 0;
    auto it = doc.find("importer");
    if (it == doc.end() || !it->is_object()) return;
    const Json& imp = *it;
    const auto slice = imp.find("slice");
    if (slice == imp.end() || !slice->is_string() || *slice != "grid") return;
    struct V2 {
        uint16_t x, y;
    };
    auto readPair = [&imp](const char* key, V2& out) {
        auto p = imp.find(key);
        if (p == imp.end() || !p->is_array() || p->size() != 2) return false;
        const Json& a = (*p)[0];
        const Json& b = (*p)[1];
        if (!a.is_number() || !b.is_number()) return false;
        const double x = a.get<double>(), y = b.get<double>();
        if (x < 1 || x > 65535 || y < 1 || y > 65535) return false;
        out = {(uint16_t)x, (uint16_t)y};
        return true;
    };
    V2 cell{0, 0}, frames{0, 0};
    if (!readPair("cell", cell) || !readPair("frames", frames)) {
        LEMON_WARN("importer 网格段不完整（需 slice/cell/frames），按全幅处理：%s",
                   e.relPath.c_str());
        return;
    }
    e.cellW = cell.x;
    e.cellH = cell.y;
    e.gridCols = frames.x;
    e.gridRows = frames.y;
    if ((uint32_t)e.gridCols * e.gridRows > 4096) { // 防荒谬声明（16k px 页上限量级）
        LEMON_WARN("importer 网格超限（%u×%u > 4096 切片），按全幅处理：%s",
                   (unsigned)e.gridCols, (unsigned)e.gridRows, e.relPath.c_str());
        e.cellW = e.cellH = e.gridCols = e.gridRows = 0;
    }
}

// 运行时按"资产 GUID 低 32 位"索引的类型域（03 §69 组件 schema 恒 uint32：
// prefabId/clipId/controllerId/表 id；EditorContext BuildPlay*Cache 映射约定）。
// 域内两资产低 32 位同值 = 后登记者静默丢映射（2026-10-01 svr-test Player/Mob
// 手工"前缀+小序号"guid 家族实证，DevLog 同日条目）。
static bool IsLow32Keyed(AssetType t) {
    switch (t) {
    case AssetType::Prefab:
    case AssetType::Clip:
    case AssetType::AnimSet:
    case AssetType::Controller:
    case AssetType::Table: return true;
    default: return false;
    }
}

uint64_t AssetDatabase::GenerateUniqueGuid(AssetType type) const {
    for (;;) {
        const uint64_t g = GenerateGuid();
        if (g == 0) continue; // 0 = "无 guid"哨兵，不发
        const uint32_t low = (uint32_t)g;
        bool clash = false;
        for (const AssetEntry& e : entries_) {
            if (e.guid == g || (e.type == type && (uint32_t)e.guid == low)) {
                clash = true;
                break;
            }
        }
        if (!clash) return g;
    }
}

void AssetDatabase::SyncMeta(AssetEntry& e) const {
    const std::string abs = AbsolutePath(e);
    const std::string metaPath = abs + ".meta";

    // 读：guid 优先取档（.meta 随文件走 = 外部移动也能找回）
    bool metaExists = false;
    if (std::ifstream mf(metaPath, std::ios::binary); mf) {
        metaExists = true;
        std::string text((std::istreambuf_iterator<char>(mf)), std::istreambuf_iterator<char>());
        Json doc = Json::parse(text, nullptr, false);
        if (!doc.is_discarded() && doc.contains("guid")) {
            const Json& g = doc.at("guid");
            uint64_t got = 0;
            if (g.is_string()) got = HexToGuid(g.get<std::string>().c_str());
            else if (g.is_number_unsigned()) got = g.get<uint64_t>();
            if (got != 0 && e.guid == 0) e.guid = got;
        }
        if (!doc.is_discarded()) {
            ParseGridImporter(doc, e); // 每次重扫重读（热改 meta 即生效）
            if (e.type == AssetType::Audio) ParseAudioImporter(doc, e);
        }
    }
    if (e.guid == 0) e.guid = GenerateUniqueGuid(e.type);
    if (metaExists) return; // 已在档：不重写（500ms 轮询重扫不做写放大）

    Json doc;
    doc["guid"] = GuidToHex(e.guid);
    doc["type"] = AssetTypeName(e.type);
    doc["hash"] = e.hash;
    doc["importedAt"] = (uint64_t)std::time(nullptr);
    if (e.type == AssetType::Audio) { // 批①：音频默认 importer 段（全曲循环 + 非预载）
        Json imp;
        imp["loop"] = {0.0, 0.0};
        imp["preload"] = false;
        doc["importer"] = imp;
    }
    std::ofstream of(metaPath, std::ios::binary | std::ios::trunc);
    of << doc.dump(2);
}

// ------------------------------------------------------------ Open/Scan ----
namespace {
// 扫描排除（06 §1 布局）：点开头（.lemon/.git 等）与 obj/bin 任何层级；顶层
// Game/Scenes/Data/Builds 整树排除（脚本工程/场景/数据/出包目录不是资产源）。
bool SkipDirAny(const std::string& name) {
    return name.empty() || name[0] == '.' || name == "obj" || name == "bin";
}
bool SkipDirTop(const std::string& name) {
    return SkipDirAny(name) || name == "Game" || name == "Scenes" ||
           name == "Data" || name == "Builds";
}
} // namespace

bool AssetDatabase::OpenProject(const std::string& projectRoot, uint32_t spriteIdBase) {
    // 相对路径入（向导手敲 ./x 等）→ 入库即绝对化：LoadFromAssemblyPath 只收绝对路径，
    // 且 meta/manifest/autosave 全链混用相对路径会随 cwd 漂移（M4.6 实测闪退根因之一）
    std::error_code eca;
    root_ = fs::absolute(projectRoot, eca).generic_string();
    spriteIdBase_ = spriteIdBase;
    nextSpriteId_ = spriteIdBase;
    entries_.clear();
    refCorpusTried_ = false;
    refFiles_.clear(); // 切项目 = 语料全失效（旧项目文件不得复用，#24 缓存随项目走）
    opened_ = true;

    std::error_code ec;
    fs::create_directories(AssetsRoot(), ec);
    fs::create_directories(root_ + "/Prefabs", ec); // 06 §1 根级 Prefabs/（M4.5 向导统一落位）
    if (!fs::is_directory(AssetsRoot())) {
        LEMON_ERROR("项目目录不可用：%s", root_.c_str());
        return false;
    }

    // manifest 先载（spriteId 记账 + guid/path 对齐）；损坏 = 弃档重建（红字）。
    // 只在启动这一次携带：后续 Rescan 的稳定性由 entries_ 自身维持。
    // 类型安全读取（2026-09-24 审查 P-13）：语法合法但字段类型不符（"guid": "abc"）
    // 会抛 json::type_error 且调用链无兜底 = std::terminate——逐字段验型，坏条目
    // 跳过（该资产按新号重排，红字可见）而非整进程崩溃。
    const std::string manifestPath = root_ + "/.lemon/manifest.json";
    if (std::ifstream mf(manifestPath, std::ios::binary); mf) {
        std::string text((std::istreambuf_iterator<char>(mf)), std::istreambuf_iterator<char>());
        Json doc = Json::parse(text, nullptr, false);
        if (!doc.is_discarded() && doc.contains("assets") && doc.at("assets").is_array()) {
            uint32_t badEntries = 0;
            uint64_t v = 0;
            // 读无符号整数字段；键缺失 = 0 通过，类型不符 = false（条目跳过）
            auto readU64 = [&v](const Json& a, const char* key) {
                if (!a.contains(key)) {
                    v = 0;
                    return true;
                }
                const Json& jv = a.at(key);
                if (!jv.is_number_unsigned()) return false;
                v = jv.get<uint64_t>();
                return true;
            };
            for (const Json& a : doc.at("assets")) {
                if (!a.is_object() || !a.contains("path") || !a.at("path").is_string()) {
                    ++badEntries;
                    continue;
                }
                CarryInfo info;
                const Json emptySlice = Json::object();
                const Json& sl = a.contains("slice") && a.at("slice").is_object()
                                     ? a.at("slice")
                                     : emptySlice;
                bool ok = readU64(a, "guid");
                info.guid = v;
                ok = ok && readU64(a, "spriteId");
                info.spriteId = (uint32_t)v;
                v = 0; // 守卫读取（键可缺）前清零：短路跳过 readU64 时不得残留上次的值
                ok = ok && (!sl.contains("base") || readU64(sl, "base"));
                info.sliceBase = (uint32_t)v;
                v = 0;
                ok = ok && (!sl.contains("count") || readU64(sl, "count"));
                info.sliceCount = (uint32_t)v;
                if (!ok) {
                    ++badEntries;
                    continue;
                }
                manifestCarry_[a.at("path").get<std::string>()] = info;
            }
            if (badEntries)
                LEMON_ERROR("manifest.json 有 %u 条字段类型异常（跳过，相关资产号将重排）",
                            badEntries);
            if (doc.contains("nextSpriteId") && doc.at("nextSpriteId").is_number_unsigned()) {
                uint32_t n = doc.at("nextSpriteId").get<uint32_t>();
                if (n > nextSpriteId_) nextSpriteId_ = n;
            } else if (doc.contains("nextSpriteId")) {
                LEMON_ERROR("manifest.json nextSpriteId 类型异常（按新号继续）");
            }
        } else if (!text.empty()) {
            LEMON_ERROR("manifest.json 损坏——资产记账重建（spriteId 将重排，已存场景引用可能失效）");
        } else {
            LEMON_ERROR("manifest.json 为空文件——资产记账重建（spriteId 将重排；"
                        "原子写后不应出现，请检查磁盘/外部改动）");
        }
    }

    Rescan();
    manifestCarry_.clear();
    SaveManifest();
    LEMON_LOG("项目资产：%u 条（sprite %u，next id %u，体检红字 %u 条）",
              (uint32_t)entries_.size(), SpriteAssetCount(), nextSpriteId_, healthIssues_);
    return true;
}

// ------------------------------------------------------------ 孤儿清扫 ----
AssetDatabase::OrphanSweepResult AssetDatabase::SweepOrphanMetas() {
    OrphanSweepResult r = SweepOrphanMetasInternal();
    for (const std::string& p : r.cleaned)
        LEMON_LOG("清理孤儿 .meta（源已删且零引用）：%s", p.c_str());
    for (const std::string& p : r.keptReferenced)
        LEMON_ERROR("孤儿 .meta 保留（guid 仍被引用——恢复源文件即可复链，"
                    "确弃请先清引用）：%s", p.c_str());
    return r;
}

AssetDatabase::OrphanSweepResult AssetDatabase::SweepOrphanMetasInternal() {
    if (!opened_) return {}; // 无项目态（菜单恒可用——空报告，与 Rescan/SaveManifest 守卫同款）
    OrphanSweepResult r;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(
             root_, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        const fs::directory_entry& de = *it;
        if (de.is_directory(ec)) {
            const std::string name = de.path().filename().string();
            std::string relDir = fs::relative(de.path(), root_, ec).generic_string();
            const bool top = !ec && relDir.find('/') == std::string::npos;
            const bool skip = ec || (top ? SkipDirTop(name) : SkipDirAny(name));
            if (skip) it.disable_recursion_pending();
            continue;
        }
        const std::string name = de.path().filename().string();
        if (name.size() <= 5 || name.compare(name.size() - 5, 5, ".meta") != 0) continue;
        const fs::path base = de.path().parent_path() / name.substr(0, name.size() - 5);
        std::error_code ec2;
        if (fs::exists(base, ec2)) continue; // 正常伴生
        // 源缺失 → 孤儿。guid 解不出（坏档/无 guid）= 无从被引用，判垃圾一并清
        uint64_t guid = 0;
        if (std::ifstream mf(de.path(), std::ios::binary); mf) {
            std::string text((std::istreambuf_iterator<char>(mf)),
                             std::istreambuf_iterator<char>());
            const Json doc = Json::parse(text, nullptr, false);
            if (!doc.is_discarded() && doc.contains("guid")) {
                const Json& g = doc.at("guid");
                if (g.is_string()) guid = HexToGuid(g.get<std::string>().c_str());
                else if (g.is_number_unsigned()) guid = g.get<uint64_t>();
            }
        }
        const std::string rel = fs::relative(de.path(), root_, ec2).generic_string();
        if (guid != 0 && GuidReferenced(guid)) {
            r.keptReferenced.push_back(rel);
        } else if (fs::remove(de.path(), ec2)) {
            r.cleaned.push_back(rel);
        } else {
            LEMON_WARN("孤儿 .meta 清理失败（权限/IO？）：%s", rel.c_str());
        }
    }
    return r;
}

bool AssetDatabase::GuidReferenced(uint64_t guid) {
    if (!opened_ || guid == 0) return false;
    if (!refCorpusTried_) {
        refCorpusTried_ = true;
        // 引用面 = 项目数据文本（含资产扫描排除的 Game/Scenes/Data——引用常驻处）。
        // 不含 .meta（自引用假阳性）；单文件 16 MiB 上限防怪物档撑爆语料。
        // review 2026-10-02 #24：逐文件 mtime+size 增量装配——未变更文件复用缓存
        // 内容、消失文件出缓存，只重读变更/新增（此前每次 Rescan 失效全项目全量
        // 重读并拼大串，保存/删除触发的重扫在 UI 线程同步卡顿）；查找逐文件进行，
        // 免整体拼接的瞬态大分配
        static const char* kRefExt[] = {".scene", ".prefab", ".anim",   ".override",
                                        ".controller", ".tab", ".rml",  ".rcss",
                                        ".cs",     ".asset"};
        std::error_code ec;
        std::unordered_map<std::string, RefFile> next;
        for (auto it = fs::recursive_directory_iterator(
                 root_, fs::directory_options::skip_permission_denied, ec);
             it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (ec) break;
            const fs::directory_entry& de = *it;
            if (de.is_directory(ec)) {
                const std::string name = de.path().filename().string();
                const bool skip = name.empty() || name[0] == '.' || name == "obj" ||
                                  name == "bin" || name == "Builds";
                if (skip) it.disable_recursion_pending();
                continue;
            }
            std::string ext = de.path().extension().string();
            for (char& c : ext) c = (char)std::tolower((unsigned char)c);
            bool isRef = false;
            for (const char* re : kRefExt)
                if (ext == re) { isRef = true; break; }
            std::error_code ec2;
            if (!isRef || !fs::is_regular_file(de.path(), ec2)) continue;
            const uintmax_t size = fs::file_size(de.path(), ec2);
            if (ec2 || size > (uintmax_t)16 << 20) continue;
            const auto mtime = fs::last_write_time(de.path(), ec2);
            if (ec2) continue;
            const std::string rel =
                fs::relative(de.path(), root_, ec2).generic_string();
            if (ec2 || rel.empty()) continue;
            const auto old = refFiles_.find(rel);
            if (old != refFiles_.end() && old->second.size == size &&
                old->second.mtime == uint64_t(mtime.time_since_epoch().count())) {
                next.emplace(rel, std::move(old->second)); // 未变更：复用
                continue;
            }
            std::ifstream f(de.path(), std::ios::binary);
            if (!f) continue; // 读失败不入缓存 = 下轮重试（不误判"零引用"）
            RefFile rf;
            rf.mtime = uint64_t(mtime.time_since_epoch().count());
            rf.size = size;
            rf.text.assign((std::istreambuf_iterator<char>(f)),
                           std::istreambuf_iterator<char>());
            next.emplace(rel, std::move(rf));
        }
        refFiles_ = std::move(next); // 消失文件随旧 map 丢弃
    }
    if (refFiles_.empty()) return false;
    // guid 在数据文本的三种形态：引擎写出的 hex 小写串 / 十进制数（.scene 组件字段）、
    // 用户代码常量可能的大小写 hex。十进制全串（19-20 位）子串命中 ≠ 巧合数字。
    const std::string hex = GuidToHex(guid);
    std::string hexUp = hex;
    for (char& c : hexUp) c = (char)std::toupper((unsigned char)c);
    const std::string dec = std::to_string(guid);
    for (const auto& [path, rf] : refFiles_) {
        if (rf.text.find(hex) != std::string::npos) return true;
        if (rf.text.find(hexUp) != std::string::npos) return true;
        if (rf.text.find(dec) != std::string::npos) return true;
    }
    return false;
}

void AssetDatabase::Rescan() {
    if (!opened_) return;
    // review 2026-10-02 #7：Remove() 预入队的 removed 事件须穿越本次重扫——此前
    // 首行整体清空使唯一消费者（RescanAssets 先 Rescan 再读）永远读不到，GPU
    // 幽灵页回收「即时触达」承诺不成立；出表路径对 missing 态不重推，不会双发
    std::vector<uint64_t> stagedRemoved = std::move(lastChange_.removed);
    lastChange_ = {};
    lastChange_.removed = std::move(stagedRemoved);
    healthIssues_ = 0;
    refCorpusTried_ = false; // 引用语料随磁盘态失效（SweepOrphanMetas/删除引用检查
                             // 共用）；refFiles_ 保留供增量复用（#24——只重读变更文件）

    // 旧表按路径索引（保 guid/spriteId）
    std::unordered_map<std::string, AssetEntry> old;
    for (auto& e : entries_) old.emplace(e.relPath, e);
    entries_.clear();

    std::error_code ec;
    if (!fs::is_directory(root_)) {
        LEMON_ERROR("项目目录消失：%s", root_.c_str());
        return;
    }

    // 扫描项目根（M4.5 起 06 §1 布局：Assets/** + 根级 Prefabs/** 均入索引；
    // Game/Scenes/Data/Builds/obj/bin/点目录排除）。
    // [M5 批④] 两段式：先收集全部文件**按路径排序**再入账——fresh 项目的
    // spriteId/切片块分配序确定（recursive_directory_iterator 的目录序是 FS
    // 实现细节，模板分发的场景引用可复现的前提；既有项目 id 走 manifest/
    // entries 记账不受影响）。
    std::vector<fs::path> files;
    for (auto it = fs::recursive_directory_iterator(
             root_, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        const fs::directory_entry& de = *it;
        if (de.is_directory(ec)) {
            const std::string name = de.path().filename().string();
            std::string relDir = fs::relative(de.path(), root_, ec).generic_string();
            const bool top = !ec && relDir.find('/') == std::string::npos;
            const bool skip = ec || (top ? SkipDirTop(name) : SkipDirAny(name));
            if (skip) it.disable_recursion_pending();
            continue;
        }
        files.push_back(de.path());
    }
    std::sort(files.begin(), files.end());
    std::vector<std::string> seenPaths;
    for (const fs::path& file : files) {
        const std::string name = file.filename().string();
        if (name.empty() || name[0] == '.') continue;
        if (name.size() > 5 && name.compare(name.size() - 5, 5, ".meta") == 0) continue;
        if (!fs::is_regular_file(file, ec)) continue;

        std::string rel = fs::relative(file, root_, ec).generic_string();
        if (ec) continue;
        seenPaths.push_back(rel);

        AssetEntry e;
        e.relPath = rel;
        e.type = TypeOf(rel);
        auto oldIt = old.find(rel);
        if (oldIt != old.end()) e.guid = oldIt->second.guid; // 路径命中：guid 先继承

        e.hash = HashFile(AbsolutePath(e));
        // .meta（可能带外部迁入的 guid；新建时 hash 已算好）
        SyncMeta(e);
        if (oldIt != old.end()) {
            const AssetEntry& prev = oldIt->second;
            e.spriteId = prev.spriteId;
            e.sliceBase = prev.sliceBase; // 切片块跨扫描稳定（grid 参数由 meta 每次重读）
            e.sliceCount = prev.sliceCount;
            if (prev.missing) {
                lastChange_.added.push_back(e.guid); // 墓碑复活
            } else if (prev.hash != e.hash) {
                lastChange_.modified.push_back(e.guid);
            } else if (e.type == AssetType::Audio &&
                       (prev.audioLoopStart != e.audioLoopStart ||
                        prev.audioLoopEnd != e.audioLoopEnd ||
                        prev.audioPreload != e.audioPreload)) {
                // review 2026-10-02 #8：音频 .meta importer 段热改也算 modified——
                // loop 冻结在 .baked 头里，源 hash 不变时此前不触发重烤，热改
                // 永不生效（与 06 §2.1「热改 meta 即生效（下次烤制消费）」对齐）
                lastChange_.modified.push_back(e.guid);
            }
        } else {
            // 新资产：manifest 记账优先（跨会话稳定），否则新号。
            // M4.4 旧 manifest 键相对 Assets/（无前缀）——同键迁移保 spriteId 不漂。
            auto mit = manifestCarry_.find(rel);
            if (mit == manifestCarry_.end() && rel.rfind("Assets/", 0) == 0)
                mit = manifestCarry_.find(rel.substr(7));
            if (mit != manifestCarry_.end()) {
                if (mit->second.guid != 0 && !FindByGuid(mit->second.guid)) e.guid = mit->second.guid;
                if (e.type == AssetType::Sprite) {
                    uint32_t id = mit->second.spriteId;
                    if (id >= spriteIdBase_ && id < nextSpriteId_ && !FindBySpriteId(id))
                        e.spriteId = id;
                    const uint32_t b = mit->second.sliceBase, c = mit->second.sliceCount;
                    if (b >= spriteIdBase_ && c > 0 && b + c <= nextSpriteId_) {
                        e.sliceBase = b;
                        e.sliceCount = c;
                    }
                }
            }
            if (e.type == AssetType::Sprite && e.spriteId == 0) e.spriteId = nextSpriteId_++;
            lastChange_.added.push_back(e.guid);
        }
        // 路径命中但类型变成 Sprite 而旧条目无号（如 .txt → .png 改扩展名）：
        // 补新号——spriteId=0 直达导入侧 AddSpriteAt(0) 必失败并走回滚
        //（2026-09-24 审查：旧代码只在新资产分支补号，此分支漏配）
        if (e.type == AssetType::Sprite && e.spriteId == 0) e.spriteId = nextSpriteId_++;
        // 网格切片块记账（M5 批③ D3）：声明了网格但无块 → 分配连号块；
        // frames 增大 → 新块（旧块烧号，"只增不减"）；缩小 → 基不变、余号留空洞。
        if (e.type == AssetType::Sprite && e.gridCols > 0) {
            const uint32_t count = (uint32_t)e.gridCols * e.gridRows;
            if (e.sliceBase == 0 || count > e.sliceCount) {
                const bool regrow = e.sliceBase != 0;
                e.sliceBase = nextSpriteId_;
                nextSpriteId_ += count;
                if (regrow)
                    LEMON_WARN("切片 frames 增大：新块 %u..%u（旧块烧号）：%s", e.sliceBase,
                               e.sliceBase + count - 1, e.relPath.c_str());
            }
            e.sliceCount = count;
        } else if (e.sliceBase != 0 && e.gridCols == 0) {
            // meta 撤掉了网格声明：块留账（引用防悬空），按全幅导入
            e.sliceCount = 0;
        }
        entries_.push_back(std::move(e));
    }

    // 消失文件 → 条目出表（墓碑 2026-10-01 退役，06 §2.2 修订：误删恢复由 .meta
    // 随文件走 + 版本管理承担）。源+meta 双亡但仍被引用 → 红字一次（悬空可见性；
    // meta 独存的情形归下方孤儿清扫 pass 报，不双报）。
    for (auto& [path, prev] : old) {
        if (std::find(seenPaths.begin(), seenPaths.end(), path) != seenPaths.end()) continue;
        if (!prev.missing) lastChange_.removed.push_back(prev.guid);
        std::error_code ec2;
        if (prev.guid != 0 && !fs::exists(AbsolutePath(prev) + ".meta", ec2) &&
            GuidReferenced(prev.guid)) {
            LEMON_ERROR("资产已删除但仍被引用（guid %016llx）：%s——恢复源文件或清理引用",
                        (unsigned long long)prev.guid, path.c_str());
            ++healthIssues_;
        }
    }

    std::sort(entries_.begin(), entries_.end(),
              [](const AssetEntry& a, const AssetEntry& b) { return a.relPath < b.relPath; });

    // GUID 冲突体检（重复 = 后者重发号 + 红字）
    for (size_t i = 0; i < entries_.size(); ++i) {
        for (size_t j = i + 1; j < entries_.size(); ++j) {
            if (entries_[j].guid != entries_[i].guid) continue;
            LEMON_ERROR("GUID 冲突：%s 与 %s 同为 %016llx（后者重发号）",
                        entries_[i].relPath.c_str(), entries_[j].relPath.c_str(),
                        (unsigned long long)entries_[i].guid);
            entries_[j].guid = GenerateUniqueGuid(entries_[j].type);
            ++healthIssues_;
        }
    }

    // 低 32 位碰撞体检（运行时映射域，见 IsLow32Keyed）。只红字不重发：碰撞对两
    // guid 全宽互异、全宽引用（场景/脚本）完好，重发反而断引用——修复动作 = 对其中
    // 之一重新生成 GUID（分配期防线见 GenerateUniqueGuid；本体检兜手工改 .meta 与
    // 模板拼装两条不经发号函数的路径）。
    {
        std::unordered_map<uint64_t, size_t> seen; // key = type<<32 | 低 32 位
        for (size_t i = 0; i < entries_.size(); ++i) {
            const AssetEntry& e = entries_[i];
            if (e.missing || !IsLow32Keyed(e.type)) continue;
            const uint64_t key = ((uint64_t)e.type << 32) | (uint32_t)e.guid;
            const auto [it, first] = seen.emplace(key, i);
            if (!first) {
                LEMON_ERROR("GUID 低 32 位碰撞（%s）：%s 与 %s 同为 %08x"
                            "——运行时按低 32 位索引将取先登记者，请重新生成其一 GUID",
                            AssetTypeName(e.type), entries_[it->second].relPath.c_str(),
                            e.relPath.c_str(), (uint32_t)e.guid);
                ++healthIssues_;
            }
        }
    }

    // 孤儿 .meta 清扫（源缺失）：零引用 = 垃圾自动清（Unity/Cocos 同款，2026-10-01
    // 拍板）；仍被引用 = 保留 + 红字（"只恢复源文件"场景的复链钩子，盲删永久断引用
    // ——Godot 社区插件只能盲清，引擎本体有引用视图可保守）。手动入口同判定出报告。
    {
        const OrphanSweepResult sweep = SweepOrphanMetasInternal();
        for (const std::string& p : sweep.cleaned)
            LEMON_LOG("清理孤儿 .meta（源已删且零引用）：%s", p.c_str());
        for (const std::string& p : sweep.keptReferenced) {
            LEMON_ERROR("孤儿 .meta 保留（guid 仍被引用——恢复源文件即可复链，"
                        "确弃请先清引用）：%s", p.c_str());
            ++healthIssues_;
        }
    }
}

// ---------------------------------------------------------------- 操作 ----
/// 相对路径 containment（2026-09-24 审查 F-02）：拒绝绝对路径与 ".." 段——
/// 重命名/导入落点必须留在项目根内，杜绝 "../" 越出资产目录写/覆盖。
static bool IsContainedRelPath(const std::string& rel) {
    if (rel.empty()) return false;
    const fs::path p(rel);
    if (p.is_absolute()) return false;
    for (const auto& seg : p)
        if (seg == ".." || seg == ".") return false;
    return true;
}

bool AssetDatabase::Rename(AssetEntry& e, const std::string& newRelPath) {
    if (newRelPath.empty() || newRelPath == e.relPath) return true;
    if (!IsContainedRelPath(newRelPath)) {
        LEMON_WARN("重命名失败：目标路径越出项目根（拒绝）：%s", newRelPath.c_str());
        return false;
    }
    if (FindByPath(newRelPath)) {
        LEMON_WARN("重命名失败：目标已存在 %s", newRelPath.c_str());
        return false;
    }
    const std::string oldAbs = AbsolutePath(e);
    const std::string newAbs = root_ + "/" + newRelPath;
    std::error_code ec1, ec2;
    fs::create_directories(fs::path(newAbs).parent_path(), ec2);
    fs::rename(oldAbs, newAbs, ec1);           // 源文件
    fs::rename(oldAbs + ".meta", newAbs + ".meta", ec2); // meta 随行 = 引用不断的机制
    if (ec1) {
        LEMON_WARN("重命名失败（IO）：%s → %s", e.relPath.c_str(), newRelPath.c_str());
        return false;
    }
    e.relPath = newRelPath;
    e.type = TypeOf(newRelPath);
    e.hash = HashFile(AbsolutePath(e));
    std::sort(entries_.begin(), entries_.end(),
              [](const AssetEntry& a, const AssetEntry& b) { return a.relPath < b.relPath; });
    SaveManifest();
    LEMON_LOG("资产重命名：%s（guid %016llx 引用不断）", newRelPath.c_str(),
              (unsigned long long)e.guid);
    return true;
}

bool AssetDatabase::SetGridSlice(AssetEntry& e, uint32_t cellW, uint32_t cellH,
                                 uint32_t cols, uint32_t rows) {
    // M6a 批② T3b-3：切片配置进 .meta importer 段（读改写原子——guid/type/hash/
    // importedAt 保原值，只动 importer 键）。全零 = 撤销切片（整图导入）。生效 =
    // 调用方随后 Rescan()：网格重读 → 连号块分配（frames 增大烧号 / 缩小基不变
    // 余号空洞，既有语义）；内存 entry 同步刷新供 UI 即时反馈。
    if (e.type != AssetType::Sprite || e.missing) return false;
    const std::string metaPath = AbsolutePath(e) + ".meta";
    Json doc = Json::object();
    if (std::ifstream mf(metaPath, std::ios::binary); mf) {
        std::string text((std::istreambuf_iterator<char>(mf)), std::istreambuf_iterator<char>());
        const Json r = Json::parse(text, nullptr, false);
        if (!r.is_discarded()) doc = r;
    }
    doc["guid"] = GuidToHex(e.guid);
    doc["type"] = AssetTypeName(e.type);
    if (!doc.contains("hash")) doc["hash"] = e.hash;
    if (!doc.contains("importedAt")) doc["importedAt"] = (uint64_t)std::time(nullptr);
    const bool grid = cellW && cellH && cols && rows;
    if (grid) {
        Json imp = Json::object();
        imp["slice"] = "grid";
        imp["cell"] = Json::array({cellW, cellH});
        imp["frames"] = Json::array({cols, rows});
        doc["importer"] = std::move(imp);
    } else {
        doc.erase("importer");
    }
    if (!WriteFileAtomic(metaPath, doc.dump(2) + "\n")) return false;
    e.cellW = (uint16_t)cellW;
    e.cellH = (uint16_t)cellH;
    e.gridCols = (uint16_t)cols;
    e.gridRows = (uint16_t)rows;
    return true;
}

bool AssetDatabase::Remove(AssetEntry& e) {
    std::error_code ec;
    fs::remove(AbsolutePath(e), ec);
    fs::remove(AbsolutePath(e) + ".meta", ec);
    // 条目本轮隐藏（missing 位立即失效化浏览器/查询），下轮 Rescan 出表——不就地
    // erase：调用方（浏览器瓦片循环）持有 entries_ 引用，就地删除 = 迭代器失效。
    // removed 变更在此推送（Rescan 保留预入队、出表路径对 missing 态不重推——
    // review 2026-10-02 #7 勘误：此前被 Rescan 首行清空，事件永不可达）→ GPU
    // 幽灵页回收（RescanAssets Evict）即时触达。
    e.missing = true;
    lastChange_.removed.push_back(e.guid);
    SaveManifest();
    LEMON_WARN("资产已删除：%s（guid %016llx；条目下轮扫描出表，引用悬空装载期告警）",
               e.relPath.c_str(), (unsigned long long)e.guid);
    return true;
}

const AssetEntry* AssetDatabase::ImportFile(const std::string& absSrc, const std::string& relDest) {
    if (!opened_) { // 无项目时 AssetsRoot()="/Assets"（根_)——拷贝必失败且报错误导
        LEMON_ERROR("导入失败：未打开项目（AssetDatabase 未 OpenProject）");
        return nullptr;
    }
    if (!IsContainedRelPath(relDest)) { // F-02：落点越出资产目录 = 拒绝
        LEMON_WARN("导入失败：目标路径越出资产目录（拒绝）：%s", relDest.c_str());
        return nullptr;
    }
    // .csv → .tab 转换导入（M6a 批② T1 / ADR-012 D1）：解析后同名 .tab 落库，
    // csv 源不拷入（.tab 为唯一权威，避免双源漂移；批量再编辑 = Excel 改完重拖，
    // 同名覆盖再导入）。解析失败（坏编码/超限）红字拒入，不留半档。
    std::string srcExt = fs::path(absSrc).extension().string();
    for (char& c : srcExt) c = (char)std::tolower((unsigned char)c);
    if (srcExt == ".csv") {
        std::ifstream f(absSrc, std::ios::binary);
        if (!f) {
            LEMON_WARN("CSV 导入失败（读不了源文件）：%s", absSrc.c_str());
            return nullptr;
        }
        std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        const TableData t = ParseCsv(text);
        if (!t.ok) {
            LEMON_ERROR("CSV 导入失败（%s）：%s", t.error.c_str(), absSrc.c_str());
            return nullptr;
        }
        const fs::path dest(relDest); // 落点跟随 relDest 词干（调用方 = 源文件名）
        const std::string stem = dest.stem().string();
        const std::string tabRel =
            (dest.parent_path() / (stem + ".tab")).generic_string();
        const std::string tabAbs = AssetsRoot() + "/" + tabRel;
        std::error_code ecDir;
        fs::create_directories(fs::path(tabAbs).parent_path(), ecDir);
        const std::string json = TableToJson(stem, t.rows);
        if (json.empty() || !WriteFileAtomic(tabAbs, json + "\n")) {
            LEMON_ERROR("CSV 导入失败（.tab 写盘，磁盘满/权限？）：%s", tabAbs.c_str());
            return nullptr;
        }
        Rescan();
        SaveManifest();
        LEMON_LOG("CSV 已转换导入：%s（%zu 行 × %u 列，csv 源不拷入）", tabRel.c_str(),
                  t.rows.size(), t.Cols());
        return FindByPath("Assets/" + tabRel);
    }

    // relDest 语义 = Assets/ 下的相对路径（导入落点恒在资产目录）
    std::error_code ec;
    fs::create_directories(fs::path(AssetsRoot() + "/" + relDest).parent_path(), ec);
    fs::copy_file(absSrc, AssetsRoot() + "/" + relDest, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        LEMON_WARN("导入失败（IO）：%s", absSrc.c_str());
        return nullptr;
    }
    Rescan();
    SaveManifest();
    return FindByPath("Assets/" + relDest);
}

void AssetDatabase::SaveManifest() const {
    if (!opened_) return;
    std::error_code ec;
    fs::create_directories(root_ + "/.lemon", ec);
    Json doc;
    doc["version"] = 1;
    doc["nextSpriteId"] = nextSpriteId_;
    Json arr = Json::array();
    for (const auto& e : entries_) {
        if (e.missing) continue; // 墓碑不落盘（号已烧毁在 nextSpriteId 单调性里）
        Json item{{"guid", e.guid},
                  {"path", e.relPath},
                  {"type", AssetTypeName(e.type)},
                  {"spriteId", e.spriteId}};
        if (e.sliceCount > 0) // 切片连号块（M5 批③；旧档缺键 = 全幅兼容）
            item["slice"] = Json{{"base", e.sliceBase}, {"count", e.sliceCount}};
        arr.push_back(std::move(item));
    }
    doc["assets"] = std::move(arr);
    // 原子写（2026-09-24 审查 P-13）：manifest 是高频落盘点（每次 Rescan 后必写），
    // 旧实现 trunc 直写——写中崩溃/磁盘满 = 半截 JSON，下次启动走"弃档重建"
    // （spriteId 重排、已存场景引用悬空），空文件还会静默跳过损坏告警
    if (!WriteFileAtomic(root_ + "/.lemon/manifest.json", doc.dump(2) + "\n"))
        LEMON_ERROR("manifest.json 写入失败（磁盘满/权限？）——记账未落盘，"
                    "下次启动 spriteId 可能重排");
}

} // namespace lemon::editor
