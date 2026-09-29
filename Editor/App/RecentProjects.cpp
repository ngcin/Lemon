// Lemon 编辑器 — 最近项目持久化实现（接口见 RecentProjects.h；批② 自
// EditorApp.cpp 匿名命名空间外迁，函数体逐行原样）。
#include "App/RecentProjects.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>

#include <nlohmann/json.hpp>

namespace lemon::editor {

// ---- 最近项目（M4.6 §4-4；$HOME/.lemon/recent.json，用户级跨项目共享）----
// 解析失败 = 静默清空重来（recent 是便利件不是账本，任何损坏不得阻断启动）。
std::string RecentProjectsPath() {
    const char* home = std::getenv("HOME");
    return home ? std::string(home) + "/.lemon/recent.json" : std::string();
}
std::vector<std::string> LoadRecentProjects() {
    std::vector<std::string> out;
    const std::string p = RecentProjectsPath();
    if (p.empty()) return out;
    std::error_code ec;
    std::ifstream f(p, std::ios::binary);
    if (!f) return out;
    std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    const nlohmann::json doc = nlohmann::json::parse(text, nullptr, false);
    if (doc.is_discarded() || !doc.contains("projects")) return out;
    for (const auto& e : doc.at("projects"))
        if (e.is_string()) out.push_back(e.get<std::string>());
    return out;
}
void SaveRecentProjects(const std::vector<std::string>& v) {
    const std::string p = RecentProjectsPath();
    if (p.empty()) return;
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(p).parent_path(), ec);
    nlohmann::json doc;
    doc["version"] = 1;
    doc["projects"] = v;
    std::ofstream of(p, std::ios::binary | std::ios::trunc);
    of << doc.dump(2);
}
void PushRecentProject(const std::string& root, std::vector<std::string>& cur) {
    cur.erase(std::remove(cur.begin(), cur.end(), root), cur.end());
    cur.insert(cur.begin(), root);
    if (cur.size() > 5) cur.resize(5);
    SaveRecentProjects(cur);
}

} // namespace lemon::editor
