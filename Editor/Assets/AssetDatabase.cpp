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

#include "Core/Guid.h"
#include "Core/Log.h"

namespace lemon::editor {
namespace fs = std::filesystem;
using Json = nlohmann::json;

const char* AssetTypeName(AssetType t) {
    switch (t) {
        case AssetType::Sprite: return "sprite";
        case AssetType::Prefab: return "prefab";
        case AssetType::Script: return "script";
        case AssetType::Clip: return "clip"; // M5 批③：06 §2.2 clip2d（.clip JSON）
        default: return "generic";
    }
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
    if (ext == ".clip") return AssetType::Clip; // M5 批③帧动画资产（06 §2.2）
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
        if (!doc.is_discarded()) ParseGridImporter(doc, e); // 每次重扫重读（热改 meta 即生效）
    }
    if (e.guid == 0) e.guid = GenerateGuid();
    if (metaExists) return; // 已在档：不重写（500ms 轮询重扫不做写放大）

    Json doc;
    doc["guid"] = GuidToHex(e.guid);
    doc["type"] = AssetTypeName(e.type);
    doc["hash"] = e.hash;
    doc["importedAt"] = (uint64_t)std::time(nullptr);
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
    const std::string manifestPath = root_ + "/.lemon/manifest.json";
    if (std::ifstream mf(manifestPath, std::ios::binary); mf) {
        std::string text((std::istreambuf_iterator<char>(mf)), std::istreambuf_iterator<char>());
        Json doc = Json::parse(text, nullptr, false);
        if (!doc.is_discarded() && doc.contains("assets") && doc.at("assets").is_array()) {
            for (const Json& a : doc.at("assets")) {
                if (!a.contains("path")) continue;
                CarryInfo info;
                info.guid = a.contains("guid") ? a.at("guid").get<uint64_t>() : 0;
                info.spriteId = a.contains("spriteId") ? a.at("spriteId").get<uint32_t>() : 0;
                if (a.contains("slice") && a.at("slice").is_object()) {
                    const Json& sl = a.at("slice");
                    info.sliceBase = sl.contains("base") ? sl.at("base").get<uint32_t>() : 0;
                    info.sliceCount = sl.contains("count") ? sl.at("count").get<uint32_t>() : 0;
                }
                manifestCarry_[a.at("path").get<std::string>()] = info;
            }
            if (doc.contains("nextSpriteId")) {
                uint32_t n = doc.at("nextSpriteId").get<uint32_t>();
                if (n > nextSpriteId_) nextSpriteId_ = n;
            }
        } else if (!text.empty()) {
            LEMON_ERROR("manifest.json 损坏——资产记账重建（spriteId 将重排，已存场景引用可能失效）");
        }
    }

    Rescan();
    manifestCarry_.clear();
    SaveManifest();
    LEMON_LOG("项目资产：%u 条（sprite %u，next id %u，体检红字 %u 条）",
              (uint32_t)entries_.size(), SpriteAssetCount(), nextSpriteId_, healthIssues_);
    return true;
}

void AssetDatabase::Rescan() {
    if (!opened_) return;
    lastChange_ = {};
    healthIssues_ = 0;

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
    // Game/Scenes/Data/Builds/obj/bin/点目录排除）
    std::vector<std::string> seenPaths;
    for (auto it = fs::recursive_directory_iterator(
             root_, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        const fs::directory_entry& de = *it;
        const std::string name = de.path().filename().string();
        if (de.is_directory(ec)) {
            std::string relDir = fs::relative(de.path(), root_, ec).generic_string();
            const bool top = !ec && relDir.find('/') == std::string::npos;
            const bool skip = ec || (top ? SkipDirTop(name) : SkipDirAny(name));
            if (skip) it.disable_recursion_pending();
            continue;
        }
        if (name.empty() || name[0] == '.') continue;
        if (name.size() > 5 && name.compare(name.size() - 5, 5, ".meta") == 0) continue;
        if (!de.is_regular_file(ec)) continue;

        std::string rel = fs::relative(de.path(), root_, ec).generic_string();
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

    // 消失文件 → 墓碑（号/引用保留；重启不回收）
    for (auto& [path, prev] : old) {
        if (std::find(seenPaths.begin(), seenPaths.end(), path) != seenPaths.end()) continue;
        if (!prev.missing) {
            lastChange_.removed.push_back(prev.guid);
            LEMON_WARN("资产缺失（引用悬空）：%s", path.c_str());
            ++healthIssues_;
        }
        prev.missing = true;
        entries_.push_back(prev);
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
            entries_[j].guid = GenerateGuid();
            ++healthIssues_;
        }
    }

    // 孤儿 meta（文件没了 meta 还在）体检（同排除规则）
    for (auto it = fs::recursive_directory_iterator(root_, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        const fs::directory_entry& de = *it;
        const std::string name = de.path().filename().string();
        if (de.is_directory(ec)) {
            std::string relDir = fs::relative(de.path(), root_, ec).generic_string();
            const bool top = !ec && relDir.find('/') == std::string::npos;
            const bool skip = ec || (top ? SkipDirTop(name) : SkipDirAny(name));
            if (skip) it.disable_recursion_pending();
            continue;
        }
        if (name.size() <= 5 || name.compare(name.size() - 5, 5, ".meta") != 0) continue;
        std::string base = de.path().parent_path() / fs::path(name.substr(0, name.size() - 5));
        std::error_code ec2;
        if (!fs::exists(base, ec2)) {
            LEMON_ERROR("孤儿 .meta（源文件已删）：%s", de.path().string().c_str());
            ++healthIssues_;
        }
    }
}

// ---------------------------------------------------------------- 操作 ----
bool AssetDatabase::Rename(AssetEntry& e, const std::string& newRelPath) {
    if (newRelPath.empty() || newRelPath == e.relPath) return true;
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

bool AssetDatabase::Remove(AssetEntry& e) {
    std::error_code ec;
    fs::remove(AbsolutePath(e), ec);
    fs::remove(AbsolutePath(e) + ".meta", ec);
    e.missing = true; // 墓碑：spriteId/引用保留（体检红字提示悬空）
    SaveManifest();
    LEMON_WARN("资产已删除（转墓碑，场景引用悬空将红字提示）：%s", e.relPath.c_str());
    return true;
}

const AssetEntry* AssetDatabase::ImportFile(const std::string& absSrc, const std::string& relDest) {
    if (!opened_) { // 无项目时 AssetsRoot()="/Assets"（根_)——拷贝必失败且报错误导
        LEMON_ERROR("导入失败：未打开项目（AssetDatabase 未 OpenProject）");
        return nullptr;
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
    std::ofstream of(root_ + "/.lemon/manifest.json", std::ios::binary | std::ios::trunc);
    of << doc.dump(2);
}

} // namespace lemon::editor
