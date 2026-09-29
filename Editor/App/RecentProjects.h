// Lemon 编辑器 — 最近项目持久化（M4.6 §4-4；$HOME/.lemon/recent.json，用户级
// 跨项目共享）。批② 2026-09-29 自 EditorApp.cpp 匿名命名空间外迁：
// BuildMenuBar（Chrome）/ OpenProjectPipeline（Scripts）/ Run 三 TU 共用，
// 内部链接保不住。解析失败 = 静默清空重来（recent 是便利件不是账本，
// 任何损坏不得阻断启动）。
#pragma once

#include <string>
#include <vector>

namespace lemon::editor {

std::string RecentProjectsPath();
std::vector<std::string> LoadRecentProjects();
void SaveRecentProjects(const std::vector<std::string>& v);
/// 去重置顶 + 截断 5 条 + 落盘
void PushRecentProject(const std::string& root, std::vector<std::string>& cur);

} // namespace lemon::editor
