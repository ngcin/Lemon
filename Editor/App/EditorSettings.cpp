// Lemon 编辑器 — 编辑器设置持久化实现（接口见 EditorSettings.h）。
#include "App/EditorSettings.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>

#include <nlohmann/json.hpp>

namespace lemon::editor {

namespace {
std::string SettingsPath() {
    const char* home = std::getenv("HOME");
    return home ? std::string(home) + "/.lemon/editor-settings.json" : std::string();
}
} // namespace

EditorSettings LoadEditorSettings() {
    EditorSettings s;
    const std::string p = SettingsPath();
    if (p.empty()) return s;
    std::ifstream f(p, std::ios::binary);
    if (!f) return s;
    const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    const nlohmann::json doc = nlohmann::json::parse(text, nullptr, false);
    if (doc.is_discarded()) return s;
    if (doc.contains("language") && doc.at("language").is_string())
        s.language = doc.at("language").get<std::string>();
    return s;
}

void SaveEditorSettings(const EditorSettings& s) {
    const std::string p = SettingsPath();
    if (p.empty()) return;
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(p).parent_path(), ec);
    nlohmann::json doc;
    doc["version"] = 1;
    doc["language"] = s.language;
    std::ofstream of(p, std::ios::binary | std::ios::trunc);
    of << doc.dump(2);
}

} // namespace lemon::editor
