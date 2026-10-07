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

/// 读 .meta 的音频 importer 段（AssetDatabase::ParseAudioImporter 同款宽容度：
/// "loop":[起,止(秒)] + "preload":bool；坏段 = 全曲循环 + 非预载）。M7a 批④：
/// AudioMount 装载消费（loop 冻结在 .baked 头、preload 决定流式分流）。
void ReadAudioImporter(const fs::path& assetPath, IndexedEntry& e) {
    e.audioLoopStart = e.audioLoopEnd = 0.0f;
    e.audioPreload = false;
    std::ifstream mf(assetPath.string() + ".meta", std::ios::binary);
    if (!mf) return;
    std::string text((std::istreambuf_iterator<char>(mf)), std::istreambuf_iterator<char>());
    const Json doc = Json::parse(text, nullptr, false);
    if (doc.is_discarded()) return;
    auto it = doc.find("importer");
    if (it == doc.end() || !it->is_object()) return;
    const Json& imp = *it;
    if (auto loop = imp.find("loop"); loop != imp.end() && loop->is_array() && loop->size() == 2) {
        const Json& a = (*loop)[0];
        const Json& b = (*loop)[1];
        if (a.is_number() && b.is_number()) {
            e.audioLoopStart = (float)a.get<double>();
            e.audioLoopEnd = (float)b.get<double>();
        }
    }
    if (auto pre = imp.find("preload"); pre != imp.end() && pre->is_boolean())
        e.audioPreload = pre->get<bool>();
}

/// 读 .meta 的字体 importer 段（M7c 批①；packager 烤制消费——AssetIndex 打开期
/// 与音频同款一次小 IO）：`charset/size/outline`；坏段 = 默认（ASCII 95 无描边）
void ReadFontImporter(const fs::path& assetPath, IndexedEntry& e) {
    e.fontCharset.clear();
    e.fontPx = 24;
    e.fontOutlinePx = 0;
    e.fontOutlineColor = 0xFF202020u;
    std::ifstream mf(assetPath.string() + ".meta", std::ios::binary);
    if (!mf) return;
    std::string text((std::istreambuf_iterator<char>(mf)), std::istreambuf_iterator<char>());
    const Json doc = Json::parse(text, nullptr, false);
    if (doc.is_discarded()) return;
    auto it = doc.find("importer");
    if (it == doc.end() || !it->is_object()) return;
    const Json& imp = *it;
    if (auto cs = imp.find("charset"); cs != imp.end() && cs->is_string())
        e.fontCharset = cs->get<std::string>();
    if (auto sz = imp.find("size"); sz != imp.end() && sz->is_number_unsigned()) {
        const uint64_t v = sz->get<uint64_t>();
        if (v >= 8 && v <= 128) e.fontPx = (uint16_t)v;
    }
    if (auto ol = imp.find("outline"); ol != imp.end() && ol->is_array() && ol->size() == 2) {
        const Json& px = (*ol)[0];
        const Json& col = (*ol)[1];
        if (px.is_number_unsigned()) {
            const uint64_t v = px.get<uint64_t>();
            if (v <= 8) e.fontOutlinePx = (uint8_t)v;
        }
        if (col.is_string())
            e.fontOutlineColor = (uint32_t)std::stoull(col.get<std::string>(), nullptr, 16);
    }
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

    // 快路径（M7a 批⑤ 起两级）：packager 打包账 manifest.pkg.json（包形态，只读
    // 消费）→ 编辑器账 manifest.json（主档坏 → .bak 兜底，编辑器 M21 写 .bak 的
    // 恢复语义只读侧同享）→ 主/备全坏/缺失 → 回退扫描（红字）。
    if (LoadFromManifest(root_ + "/.lemon/manifest.pkg.json")) {
        fromManifest_ = true;
    } else {
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
    } // manifest.json 链结束（pkg 账在场时整链跳过）

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
        // 类型串自愈（M7a 批④）：manifest 记 "generic" 但扩展名有强类型 → 按扩展名
        // 重派。既有项目音频记账全为 "generic"（AssetTypeName 漏 Audio 分支时期的
        // 产物，批④已修写入面）；编辑器类型本就恒 = TypeOf(relPath)，重派不发明事实
        if (e.type == AssetType::Generic) e.type = TypeOf(e.relPath);
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
        // 在 .meta importer 段——TextureStore 登记消费）；Audio 条目补 importer
        // 段（loop/preload——AudioMount 消费）。均一次小 IO，与像素装载同量级
        if (e.type == AssetType::Sprite)
            ReadGridImporter(fs::path(root_) / e.relPath, e);
        if (e.type == AssetType::Audio)
            ReadAudioImporter(fs::path(root_) / e.relPath, e);
        if (e.type == AssetType::Font) // M7c 批①：packager 烤制消费
            ReadFontImporter(fs::path(root_) / e.relPath, e);
        out.push_back(std::move(e));
    }
    if (badEntries)
        LEMON_ERROR("AssetIndex：manifest 有 %u 条字段类型异常（跳过）", badEntries);
    entries_ = std::move(out);

    // Sprite 补号（丢记账者）+ 切片块号域校验：块不落 [base, 上限) = 丢块转全幅。
    // 号域上限 = manifest nextSpriteId（在场时），缺省 = 已见最大号 + 1——
    // 防御坏账把切片块指到程序化页/他页。**号域绝对 sane 上限**（review 2026-10-05，
    // LBA1 sane-上限防线同语义）：AtlasRegistry::sprites_ 按号 resize，巨号直接
    // 巨分配崩装载——idCeiling 一律钳 kMaxSpriteIdSanity，超限号全成坏账清零。
    constexpr uint32_t kMaxSpriteIdSanity = 1u << 22; // 419 万（~168MB vector 上界）
    uint32_t idCeiling = spriteIdBase_;
    if (doc.contains("nextSpriteId") && doc.at("nextSpriteId").is_number_unsigned()) {
        const uint64_t declared = doc.at("nextSpriteId").get<uint64_t>();
        idCeiling = uint32_t(std::min<uint64_t>(
            kMaxSpriteIdSanity, std::max<uint64_t>(spriteIdBase_, declared)));
    } else {
        uint64_t seen = spriteIdBase_;
        for (const IndexedEntry& e : entries_) { // 64 位域累算（防 u32 +1/求和回绕）
            seen = std::max<uint64_t>(seen, uint64_t(e.spriteId) + 1);
            if (e.sliceCount)
                seen = std::max<uint64_t>(seen, uint64_t(e.sliceBase) + e.sliceCount);
        }
        idCeiling = uint32_t(std::min<uint64_t>(seen, kMaxSpriteIdSanity));
    }
    // 块账 sane 域收口（review 2026-10-05：u32 加法回绕防线——sliceBase+sliceCount
    // 精确回绕可绕过原防御，SetSpriteAt 以巨值 resize 直接 bad_alloc 崩装载，LBA1
    // review 2026-10-01 同款教训在号域重演；收口先于随迁/保号消费，count 同时钳
    // 编辑器网格上限 4096 = ReadGridImporter 同域）。真坏账清零转全幅（宁缺勿错）。
    for (IndexedEntry& e : entries_) {
        if (e.type != AssetType::Sprite || e.sliceCount == 0) continue;
        if (e.sliceCount > 4096 || uint64_t(e.sliceBase) + e.sliceCount > idCeiling)
            e.sliceBase = e.sliceCount = 0;
    }
    uint32_t nextId = idCeiling;
    std::unordered_map<uint32_t, uint8_t> taken; // 已占号集（撞号检测；无 sprite 的
                                                 // 纯数据项目号域不消费 = 合法）
    for (IndexedEntry& e : entries_) {
        if (e.type != AssetType::Sprite) continue;
        // 坏账两态清零重派：低域号（撞程序化页）/越号域上界（巨号巨 resize 面）
        if (e.spriteId != 0 &&
            (e.spriteId < spriteIdBase_ || e.spriteId > idCeiling))
            e.spriteId = 0;
        if (e.spriteId != 0) {
            if (!taken.emplace(e.spriteId, 0).second) {
                LEMON_ERROR("AssetIndex：manifest spriteId %u 重复记账（%s 丢号重派）",
                            e.spriteId, e.relPath.c_str());
                e.spriteId = 0;
            }
        }
    }
    // 切片块号域（验收热修 2026-10-05）：本体重派时低域块**随本体连号重发**（结构
    // = 本体后紧跟连号块，ScanFallback 发号序同款）而非清零——几何真源在 .meta、
    // 号只是进程内派生号，整体平移无害。低域块成因：packager 与运行时 spriteIdBase
    // 不同（无编辑器账项目 fallback 自 base=2 记账，对 lemon-game 程序化页后大基线）。
    std::vector<uint8_t> rebased(entries_.size(), 0);
    for (size_t i = 0; i < entries_.size(); ++i) {
        IndexedEntry& e = entries_[i];
        if (e.type != AssetType::Sprite || e.spriteId != 0) continue;
        while (taken.count(nextId)) ++nextId;
        e.spriteId = nextId++;
        taken.emplace(e.spriteId, 0);
        rebased[i] = 1;
        if (e.sliceCount > 0 && e.sliceBase < spriteIdBase_) {
            while (taken.count(nextId)) ++nextId;
            e.sliceBase = nextId;
            nextId += e.sliceCount; // ≤4096（上方收口钳过）
            for (uint32_t id = e.sliceBase; id < e.sliceBase + e.sliceCount; ++id)
                taken.emplace(id, 0);
        }
    }
    // 保号条目的低域怪块收尾（本体在域内而块指向程序化页 = 账不自洽）→ 清零转全幅
    //（上界越界已在 sane 域收口清过）
    for (size_t i = 0; i < entries_.size(); ++i) {
        IndexedEntry& e = entries_[i];
        if (e.type != AssetType::Sprite || e.sliceCount == 0 || rebased[i]) continue;
        if (e.sliceBase < spriteIdBase_) e.sliceBase = e.sliceCount = 0;
    }
    return true;
}

bool AssetIndex::ExportManifest(const std::string& path) const {
    if (!opened_) return false;
    Json doc;
    doc["assets"] = Json::array();
    uint32_t idCeiling = spriteIdBase_;
    for (const IndexedEntry& e : entries_) {
        Json a;
        a["path"] = e.relPath;
        a["guid"] = e.guid; // 十进制 u64（LoadFromManifest 同款记账口径）
        a["type"] = AssetTypeName(e.type);
        if (e.type == AssetType::Sprite && e.spriteId != 0) a["spriteId"] = e.spriteId;
        if (e.sliceCount) a["slice"] = {{"base", e.sliceBase}, {"count", e.sliceCount}};
        doc["assets"].push_back(std::move(a));
        idCeiling = std::max<uint64_t>(idCeiling, uint64_t(e.spriteId) + 1); // 64 位域
        if (e.sliceCount)
            idCeiling = std::max<uint64_t>(idCeiling, uint64_t(e.sliceBase) + e.sliceCount);
    }
    doc["nextSpriteId"] = idCeiling; // 号域上界（回读侧块账校验的 ceiling 口径）
    std::error_code ec;
    fs::create_directories(fs::path(path).parent_path(), ec);
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) {
        LEMON_ERROR("AssetIndex：打包账导出失败（不可写）：%s", path.c_str());
        return false;
    }
    f << doc.dump(2);
    return f.good();
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
        if (e.type == AssetType::Audio) ReadAudioImporter(file, e);
        if (e.type == AssetType::Font) ReadFontImporter(file, e); // M7c 批①
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
        // 64 位域区间（review 2026-10-05：u32 求和回绕防线）
        if (e.sliceCount > 0 && spriteId >= e.sliceBase &&
            uint64_t(spriteId) < uint64_t(e.sliceBase) + e.sliceCount)
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
