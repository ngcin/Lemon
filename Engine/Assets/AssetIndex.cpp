// Lemon 引擎 — AssetIndex 实现（M7a 批②；见 AssetIndex.h 契约注记）
// nlohmann/json 只进本 .cpp。两路共守「guid 真源在 .meta、spriteId 记账在
// manifest」——快路径不发明事实，只消费编辑器/packager 写下的账。
#include "Assets/AssetIndex.h"

#include <algorithm>
#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

#include "Core/Log.h"

namespace lemon::assets {
namespace fs = std::filesystem;
using Json = nlohmann::json;

std::string IndexedEntry::FileName() const {
    size_t s = relPath.find_last_of('/');
    return s == std::string::npos ? relPath : relPath.substr(s + 1);
}

std::string IndexedEntry::Dir() const {
    size_t s = relPath.find_last_of('/');
    return s == std::string::npos ? std::string() : relPath.substr(0, s);
}

namespace {

// 扫描排除（AssetDatabase::Rescan 同款，06 §1 布局）：点开头（.lemon/.git 等）
// 与 obj/bin 任何层级。AssetIndex 只进 Assets/ 与根级 Prefabs/ 两棵树，
// Game/Scenes/Data/Builds 顶层排除由「只进两树」结构保证。
bool SkipDirAny(const std::string& name) {
    return name.empty() || name[0] == '.' || name == "obj" || name == "bin";
}

/// manifest "type" 字符串 → 枚举（AssetTypeName 的逆；未知 = Generic 兼容旧档）
AssetType TypeFromName(const std::string& s) {
    for (uint8_t t = 0; t <= (uint8_t)AssetType::Generic; ++t) {
        const AssetType at = (AssetType)t;
        if (s == AssetTypeName(at)) return at;
    }
    return AssetType::Generic;
}

/// 读 sidecar .meta 的网格切片声明（AssetDatabase::ParseGridImporter 同款宽容度；
/// 只读不改写——AssetIndex 零写侧）。坏段 = 全幅（0/0）。
void ReadGridImporter(const fs::path& assetPath, IndexedEntry& e) {
    e.cellW = e.cellH = e.gridCols = e.gridRows = 0;
    std::ifstream mf(assetPath.string() + ".meta", std::ios::binary);
    if (!mf) return;
    std::string text((std::istreambuf_iterator<char>(mf)), std::istreambuf_iterator<char>());
    const Json doc = Json::parse(text, nullptr, false);
    if (doc.is_discarded()) return;
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
    if (!readPair("cell", cell) || !readPair("frames", frames)) return;
    if ((uint32_t)frames.x * frames.y > 4096) return; // 防荒谬声明（编辑器同款上限）
    e.cellW = cell.x;
    e.cellH = cell.y;
    e.gridCols = frames.x;
    e.gridRows = frames.y;
}

/// 读 .meta 的 guid（无/坏 = 0）——回退扫描的真源
uint64_t ReadMetaGuid(const fs::path& assetPath) {
    std::ifstream mf(assetPath.string() + ".meta", std::ios::binary);
    if (!mf) return 0;
    std::string text((std::istreambuf_iterator<char>(mf)), std::istreambuf_iterator<char>());
    const Json doc = Json::parse(text, nullptr, false);
    if (doc.is_discarded() || !doc.contains("guid")) return 0;
    const Json& g = doc.at("guid");
    if (g.is_string()) return HexToGuid(g.get<std::string>().c_str());
    if (g.is_number_unsigned()) return g.get<uint64_t>();
    return 0;
}

bool ReadAll(const std::string& p, std::string& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    out.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return true;
}

} // namespace

bool AssetIndex::Open(const std::string& projectRoot, uint32_t spriteIdBase) {
    // 相对路径入 → 绝对化（AssetDatabase::OpenProject 同款：全链路径不随 cwd 漂移）
    std::error_code eca;
    root_ = fs::absolute(projectRoot, eca).generic_string();
    spriteIdBase_ = spriteIdBase;
    entries_.clear();
    byPath_.clear();
    fromManifest_ = false;
    opened_ = false;

    if (!fs::is_directory(AssetsRoot())) {
        LEMON_ERROR("AssetIndex：项目 Assets/ 目录不可用：%s", root_.c_str());
        return false;
    }

    // 快路径：manifest 在场且合法 → 直读记账。主档坏 → .bak 兜底（编辑器 M21
    // 写 .bak 的恢复语义，只读侧同享）；主/备全坏/缺失 → 回退扫描（红字）。
    const std::string manifestPath = root_ + "/.lemon/manifest.json";
    if (LoadFromManifest(manifestPath)) {
        fromManifest_ = true;
    } else {
        std::string bak;
        if (ReadAll(manifestPath + ".bak", bak) && LoadFromManifest(manifestPath + ".bak")) {
            fromManifest_ = true;
            LEMON_WARN("AssetIndex：manifest.json 损坏/缺失——已从 .bak 恢复记账"
                       "（spriteId 不重排）");
        } else {
            LEMON_WARN("AssetIndex：manifest 不可用（git clean -xfd / 首次运行？）"
                       "——回退扫描派生 spriteId（与编辑器数值可不同，装载期按 guid 归一）");
            ScanFallback();
        }
    }

    std::sort(entries_.begin(), entries_.end(),
              [](const IndexedEntry& a, const IndexedEntry& b) {
                  return a.relPath < b.relPath;
              });
    byPath_.reserve(entries_.size() * 2);
    for (uint32_t i = 0; i < (uint32_t)entries_.size(); ++i)
        byPath_.emplace(entries_[i].relPath, i);
    opened_ = true;
    LEMON_LOG("AssetIndex：%u 条（%s 路径，sprite %u 号起）", (uint32_t)entries_.size(),
              fromManifest_ ? "manifest 快" : "回退扫描", spriteIdBase_);
    return true;
}

bool AssetIndex::LoadFromManifest(const std::string& manifestPath) {
    std::string text;
    if (!ReadAll(manifestPath, text) || text.empty()) return false;
    const Json doc = Json::parse(text, nullptr, false);
    if (doc.is_discarded() || !doc.contains("assets") || !doc.at("assets").is_array())
        return false;
    std::vector<IndexedEntry> out;
    uint32_t badEntries = 0;
    for (const Json& a : doc.at("assets")) {
        if (!a.is_object() || !a.contains("path") || !a.at("path").is_string() ||
            !a.contains("guid") || !a.at("guid").is_number_unsigned()) {
            ++badEntries;
            continue;
        }
        IndexedEntry e;
        e.relPath = a.at("path").get<std::string>();
        e.guid = a.at("guid").get<uint64_t>();
        if (a.contains("type") && a.at("type").is_string())
            e.type = TypeFromName(a.at("type").get<std::string>());
        else
            e.type = TypeOf(e.relPath);
        if (a.contains("spriteId") && a.at("spriteId").is_number_unsigned())
            e.spriteId = a.at("spriteId").get<uint32_t>();
        if (a.contains("slice") && a.at("slice").is_object()) {
            const Json& sl = a.at("slice");
            if (sl.contains("base") && sl.at("base").is_number_unsigned())
                e.sliceBase = sl.at("base").get<uint32_t>();
            if (sl.contains("count") && sl.at("count").is_number_unsigned())
                e.sliceCount = sl.at("count").get<uint32_t>();
        }
        // 记账号越界运行时号域（撞程序化页/被烧毁）= 账目异常：该条目丢弃记账，
        // Sprite 由扫描语义重派（下方统一补号）；非 Sprite 无号语义，直接放行
        if (e.type == AssetType::Sprite && e.spriteId != 0 && e.spriteId < spriteIdBase_)
            e.spriteId = 0;
        // Sprite 条目补 .meta 网格声明（manifest 只记 slice 块号，切片像素几何
        // 在 .meta importer 段——TextureStore 登记消费；一次小 IO，与像素装载同量级）
        if (e.type == AssetType::Sprite)
            ReadGridImporter(fs::path(root_) / e.relPath, e);
        out.push_back(std::move(e));
    }
    if (badEntries)
        LEMON_ERROR("AssetIndex：manifest 有 %u 条字段类型异常（跳过）", badEntries);
    entries_ = std::move(out);

    // Sprite 补号（丢记账者）+ 切片块号域校验：块不落 [base, 上限) = 丢块转全幅。
    // 号域上限 = manifest nextSpriteId（在场时），缺省 = 已见最大号 + 1——
    // 防御坏账把切片块指到程序化页/他页。
    uint32_t idCeiling = spriteIdBase_;
    if (doc.contains("nextSpriteId") && doc.at("nextSpriteId").is_number_unsigned())
        idCeiling = std::max(idCeiling, doc.at("nextSpriteId").get<uint32_t>());
    else
        for (const IndexedEntry& e : entries_) {
            idCeiling = std::max(idCeiling, e.spriteId + 1);
            if (e.sliceCount) idCeiling = std::max(idCeiling, e.sliceBase + e.sliceCount);
        }
    uint32_t nextId = idCeiling;
    std::unordered_map<uint32_t, uint8_t> taken; // 已占号集（撞号检测；无 sprite 的
                                                 // 纯数据项目号域不消费 = 合法）
    for (IndexedEntry& e : entries_) {
        if (e.type != AssetType::Sprite) continue;
        if (e.sliceCount > 0 && (e.sliceBase < spriteIdBase_ ||
                                 e.sliceBase + e.sliceCount > idCeiling)) {
            e.sliceBase = e.sliceCount = 0; // 块账异常 → 转全幅（宁缺勿错）
        }
        if (e.spriteId != 0) {
            if (!taken.emplace(e.spriteId, 0).second) {
                LEMON_ERROR("AssetIndex：manifest spriteId %u 重复记账（%s 丢号重派）",
                            e.spriteId, e.relPath.c_str());
                e.spriteId = 0;
            }
        }
    }
    for (IndexedEntry& e : entries_) {
        if (e.type != AssetType::Sprite || e.spriteId != 0) continue;
        while (taken.count(nextId)) ++nextId;
        e.spriteId = nextId++;
        taken.emplace(e.spriteId, 0);
    }
    return true;
}

void AssetIndex::ScanFallback() {
    // 路径排序 → 单调发号：fresh 项目的 spriteId/切片块分配序确定（编辑器
    // Rescan 两段式同款；recursive_directory_iterator 目录序是 FS 实现细节）。
    // guid 真源 = .meta（随文件走）；无 .meta/坏 guid = 跳过 + 红字（AssetIndex
    // 只读不发号——发号是编辑器写侧职责，运行时静默发号会与编辑器双源漂移）。
    std::vector<fs::path> files;
    std::error_code ec;
    for (const char* tree : {"Assets", "Prefabs"}) {
        const fs::path top = fs::path(root_) / tree;
        if (!fs::is_directory(top, ec)) continue;
        for (auto it = fs::recursive_directory_iterator(
                 top, fs::directory_options::skip_permission_denied, ec);
             it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (ec) break;
            const fs::directory_entry& de = *it;
            if (de.is_directory(ec)) {
                if (SkipDirAny(de.path().filename().string()))
                    it.disable_recursion_pending();
                continue;
            }
            files.push_back(de.path());
        }
    }
    std::sort(files.begin(), files.end());

    uint32_t nextId = spriteIdBase_;
    uint32_t noMeta = 0;
    for (const fs::path& file : files) {
        const std::string name = file.filename().string();
        if (name.empty() || name[0] == '.') continue;
        if (name.size() > 5 && name.compare(name.size() - 5, 5, ".meta") == 0) continue;
        std::error_code ec2;
        if (!fs::is_regular_file(file, ec2)) continue;

        const uint64_t guid = ReadMetaGuid(file);
        if (guid == 0) {
            ++noMeta;
            continue; // 只读侧不认无 .meta 的散文件（编辑器打开即补齐）
        }
        IndexedEntry e;
        e.guid = guid;
        e.relPath = fs::relative(file, root_, ec2).generic_string();
        if (ec2) continue;
        e.type = TypeOf(e.relPath);
        if (e.type == AssetType::Sprite) {
            e.spriteId = nextId++;
            ReadGridImporter(file, e);
            if (e.gridCols > 0) { // 切片连号块（meta frames 声明 → 行优先 cell 序）
                e.sliceBase = nextId;
                e.sliceCount = (uint32_t)e.gridCols * e.gridRows;
                nextId += e.sliceCount;
            }
        }
        entries_.push_back(std::move(e));
    }
    if (noMeta)
        LEMON_ERROR("AssetIndex：回退扫描跳过 %u 个无 .meta 文件"
                    "（用编辑器打开项目可补齐 guid）",
                    noMeta);
}

const IndexedEntry* AssetIndex::FindByGuid(uint64_t guid) const {
    if (guid == 0) return nullptr;
    for (const IndexedEntry& e : entries_)
        if (e.guid == guid) return &e;
    return nullptr;
}

const IndexedEntry* AssetIndex::FindByPath(const std::string& relPath) const {
    auto it = byPath_.find(relPath);
    return it != byPath_.end() ? &entries_[it->second] : nullptr;
}

const IndexedEntry* AssetIndex::FindBySpriteId(uint32_t spriteId) const {
    if (spriteId == 0) return nullptr;
    for (const IndexedEntry& e : entries_) {
        if (e.spriteId == spriteId) return &e;
        if (e.sliceCount > 0 && spriteId >= e.sliceBase && spriteId < e.sliceBase + e.sliceCount)
            return &e;
    }
    return nullptr;
}

const IndexedEntry* AssetIndex::FindByWholeSpriteId(uint32_t spriteId) const {
    if (spriteId == 0) return nullptr;
    for (const IndexedEntry& e : entries_)
        if (e.type == AssetType::Sprite && e.spriteId == spriteId) return &e;
    return nullptr;
}

const IndexedEntry* AssetIndex::FindByLowId(AssetType type, uint32_t lowId) const {
    if (lowId == 0) return nullptr;
    for (const IndexedEntry& e : entries_) {
        if (e.type != type) continue;
        if ((uint32_t)e.guid == lowId) return &e;
    }
    return nullptr; // 碰撞体检是编辑器写侧职责（五域红字）；此处取路径序先登记者
}

bool AssetIndex::SpriteIdRegistered(uint32_t spriteId) const {
    return FindBySpriteId(spriteId) != nullptr;
}

} // namespace lemon::assets
