// Lemon 编辑器 — 编辑器级设置持久化（$HOME/.lemon/editor-settings.json；
// recent.json 同款口径：损坏 = 静默回默认，不阻断启动）。语言项由 i18n 菜单读写。
#pragma once

#include <string>

namespace lemon::editor {

struct EditorSettings {
    std::string language = "zh-CN";
};

EditorSettings LoadEditorSettings();
void SaveEditorSettings(const EditorSettings& s);

} // namespace lemon::editor
