// Lemon 编辑器 — 本地化实现（接口见 Localization.h）。
#include "Localization/Localization.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <unordered_map>
#include <unordered_set>

#include <nlohmann/json.hpp>

#include "Core/Log.h"

namespace lemon::editor::loc {

namespace {

// lang → (key → 文案)。node-based 容器 + 只增不删 → 文案指针稳定可作 const char*
std::unordered_map<std::string, std::unordered_map<std::string, std::string>> g_tables;
std::string g_lang = "zh-CN";
std::unordered_set<std::string> g_warned; // 缺 key 首查告警去重

void LoadLanguageDir(const std::filesystem::path& dir, const std::string& lang) {
    auto& table = g_tables[lang];
    std::error_code ec;
    for (const auto& it : std::filesystem::directory_iterator(dir, ec)) {
        if (!it.is_regular_file(ec) || it.path().extension() != ".json") continue;
        std::ifstream f(it.path(), std::ios::binary);
        if (!f) continue;
        const std::string text((std::istreambuf_iterator<char>(f)),
                                std::istreambuf_iterator<char>());
        // 单文件损坏 = 跳过该模块继续（回退链兜底，不阻断启动——recent.json 同款口径）
        const nlohmann::json doc = nlohmann::json::parse(text, nullptr, false);
        if (doc.is_discarded() || !doc.is_object()) {
            LEMON_WARN("i18n：%s 解析失败，已跳过", it.path().string().c_str());
            continue;
        }
        for (auto kv = doc.begin(); kv != doc.end(); ++kv)
            if (kv.value().is_string()) table[kv.key()] = kv.value().get<std::string>();
    }
}

} // namespace

void LoadStrings(const std::string& dir) {
    std::error_code ec;
    for (const auto& it : std::filesystem::directory_iterator(dir, ec)) {
        if (!it.is_directory(ec)) continue;
        const std::string lang = it.path().filename().string();
        if (lang.empty() || lang[0] == '.') continue;
        LoadLanguageDir(it.path(), lang);
        LEMON_LOG("i18n：装载 %s（%zu 条）", lang.c_str(), g_tables[lang].size());
    }
}

void SetLanguage(const std::string& lang) {
    if (!g_tables.contains(lang)) {
        LEMON_WARN("i18n：未知语言码 %s（已装载 %zu 种），保持 %s", lang.c_str(),
                   g_tables.size(), g_lang.c_str());
        return;
    }
    g_lang = lang;
}

const std::string& CurrentLanguage() { return g_lang; }

std::vector<std::string> AvailableLanguages() {
    std::vector<std::string> out;
    out.reserve(g_tables.size());
    for (const auto& [lang, _] : g_tables) out.push_back(lang);
    std::sort(out.begin(), out.end());
    return out;
}

const char* LanguageDisplayName(const std::string& lang) {
    static const std::unordered_map<std::string, std::string> kNames = {
        {"zh-CN", "中文"}, {"en", "English"}};
    if (auto it = kNames.find(lang); it != kNames.end()) return it->second.c_str();
    return lang.c_str();
}

const char* tr(const char* key) {
    if (auto cur = g_tables.find(g_lang); cur != g_tables.end())
        if (auto it = cur->second.find(key); it != cur->second.end()) return it->second.c_str();
    if (g_lang != "en")
        if (auto en = g_tables.find("en"); en != g_tables.end())
            if (auto it = en->second.find(key); it != en->second.end()) return it->second.c_str();
    if (g_warned.insert(key).second) // 漏译自查面：Console WARN 一条 + 界面显示 key 原文
        LEMON_WARN("i18n：缺 key %s（%s 与 en 均无，显示 key 原文）", key, g_lang.c_str());
    return key;
}

std::string trFmt(const char* key, std::initializer_list<std::string_view> args) {
    std::string out = tr(key);
    char ph[4] = {'{', '0', '}', '\0'};
    for (size_t i = 0; i < args.size(); ++i) {
        ph[1] = static_cast<char>('0' + i);
        size_t pos = 0;
        while ((pos = out.find(ph, pos)) != std::string::npos) {
            out.replace(pos, 3, args.begin()[i]);
            pos += args.begin()[i].size();
        }
    }
    return out;
}

} // namespace lemon::editor::loc
