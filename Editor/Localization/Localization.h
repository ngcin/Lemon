// Lemon 编辑器 — UI 文案本地化（i18n，M7c 批③）。
// 翻译源 = Editor/Strings/<lang>/*.json（扁平 "namespace.key": "文本"；模块
// 一个文件，中英同 key 对齐；en 为基准——缺 key 回退 en，再缺回退 key 本身，
// 保证 UI 永不因漏译崩坏，且漏译在 Console 可见）。
// ImGui 即时模式每帧重取文案 → SetLanguage 下一帧全 UI 生效，无需重启。
#pragma once

#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace lemon::editor::loc {

// 扫描 dir 下每个子目录（目录名 = 语言码，如 zh-CN/、en/），合并其中 *.json。
// 文件名仅作模块拆分，不进 key。启动调一次；表不 erase → tr 指针全程稳定。
void LoadStrings(const std::string& dir);

// 语言码（"zh-CN"/"en"）；SetLanguage 即时切换（未知码 = 保持原语言并 WARN）
void SetLanguage(const std::string& lang);
const std::string& CurrentLanguage();

// 已装载语言码（字典序稳定，菜单用）
std::vector<std::string> AvailableLanguages();
// 菜单展示名："zh-CN"→"中文"、"en"→"English"（其余回退语言码本身）
const char* LanguageDisplayName(const std::string& lang);

// 查表：当前语言 → en → key 本身。缺 key 首查 WARN 一条（进程内每 key 一次）
const char* tr(const char* key);

// 带参占位（JSON 内写 {0} {1} {2}…；实参一律先格式化成字符串再传）
std::string trFmt(const char* key, std::initializer_list<std::string_view> args);

} // namespace lemon::editor::loc
