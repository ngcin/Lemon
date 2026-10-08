// Lemon 引擎 — ProjectFile 实现（M7a 批②；见 ProjectFile.h 契约注记）
#include "Assets/ProjectFile.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <vector>

#include <nlohmann/json.hpp>

#include "Assets/AssetTypes.h"
#include "Core/Log.h"

namespace lemon::assets {
namespace fs = std::filesystem;

ProjectFile ParseProjectFile(std::string_view text) {
    ProjectFile pf;
    const nlohmann::json j = nlohmann::json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return pf;
    if (!j.contains("name") || !j.at("name").is_string()) return pf;
    pf.name = j.at("name").get<std::string>();
    if (j.contains("guid")) {
        const nlohmann::json& g = j.at("guid");
        if (g.is_string()) pf.guid = HexToGuid(g.get<std::string>().c_str());
        else if (g.is_number_unsigned()) pf.guid = g.get<uint64_t>();
    }
    if (j.contains("engineVersion") && j.at("engineVersion").is_string())
        pf.engineVersion = j.at("engineVersion").get<std::string>();
    if (j.contains("entryScene") && j.at("entryScene").is_string())
        pf.entryScene = j.at("entryScene").get<std::string>();
    if (j.contains("fxFont")) { // M7c 批①：Fx 飘字字体资产（16 位 hex）
        const nlohmann::json& f = j.at("fxFont");
        if (f.is_string()) pf.fxFont = HexToGuid(f.get<std::string>().c_str());
        else if (f.is_number_unsigned()) pf.fxFont = f.get<uint64_t>();
    }
    pf.ok = true;
    return pf;
}

ProjectFile LoadProjectFile(const std::string& projectRoot) {
    std::ifstream f(fs::path(projectRoot) / "project.lemon", std::ios::binary);
    if (!f) {
        LEMON_ERROR("project.lemon 缺失/不可读：%s", projectRoot.c_str());
        return {};
    }
    const std::string text((std::istreambuf_iterator<char>(f)),
                           std::istreambuf_iterator<char>());
    return ParseProjectFile(text);
}

namespace {

/// 项目内 .scene 全量收集（Scenes/ 惯例落位 + Assets/ 内嵌合法；ResolveEntryScene
/// 唯一性回退与 ResolveScene stem 寻址共用）
void CollectSceneFiles(const std::string& projectRoot, std::vector<std::string>& out) {
    std::error_code ec;
    for (const char* tree : {"Scenes", "Assets"}) {
        const fs::path top = fs::path(projectRoot) / tree;
        if (!fs::is_directory(top, ec)) continue;
        for (auto it = fs::recursive_directory_iterator(
                 top, fs::directory_options::skip_permission_denied, ec);
             it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (ec) break;
            const fs::directory_entry& de = *it;
            if (de.is_directory(ec)) {
                const std::string n = de.path().filename().string();
                if (n.empty() || n[0] == '.') it.disable_recursion_pending();
                continue;
            }
            std::string ext = de.path().extension().string();
            for (char& c : ext) c = (char)std::tolower((unsigned char)c);
            if (ext != ".scene") continue;
            std::error_code ec2;
            const std::string rel = fs::relative(de.path(), projectRoot, ec2).generic_string();
            if (!ec2) out.push_back(rel);
        }
    }
    std::sort(out.begin(), out.end());
}

} // namespace

std::string ResolveEntryScene(const std::string& projectRoot, const ProjectFile& pf) {
    std::error_code ec;
    if (!pf.entryScene.empty()) {
        if (fs::is_regular_file(fs::path(projectRoot) / pf.entryScene, ec))
            return pf.entryScene;
        // 声明悬空不静默回退（声明 = 作者意图，错路径要响亮可见）；
        // 继续唯一 .scene 探测并在悬空时双报由调用方红字，此处返回空
        return std::string();
    }
    // 未声明 → 唯一 .scene 回退
    std::vector<std::string> scenes;
    CollectSceneFiles(projectRoot, scenes);
    if (scenes.size() == 1) return scenes[0];
    return std::string(); // 零/多场景且未声明 = 调用方红字（无法猜入口）
}

std::string ResolveScene(const std::string& projectRoot, const ProjectFile& pf,
                         const std::string& nameOrPath) {
    if (nameOrPath.empty()) return ResolveEntryScene(projectRoot, pf);
    const fs::path p(nameOrPath);
    if (p.is_absolute()) return std::string(); // 项目相对寻址红线（越根 = 拒绝）
    std::error_code ec;
    // ① 路径精确命中（"Scenes/Forest.scene" / "Assets/UI/Main.scene"）
    if (fs::is_regular_file(fs::path(projectRoot) / p, ec)) return p.generic_string();
    // ② 唯一 stem 命中（"Forest"——全库唯一者；零/多 = 响亮失败）
    std::vector<std::string> scenes;
    CollectSceneFiles(projectRoot, scenes);
    std::vector<std::string> hits;
    for (const std::string& rel : scenes)
        if (fs::path(rel).stem() == p) hits.push_back(rel);
    return hits.size() == 1 ? hits[0] : std::string();
}

} // namespace lemon::assets
