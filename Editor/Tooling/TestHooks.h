// Lemon 编辑器 — 测试矩形登记（--smoke-ui 真人会话注入回归的定位通道）
// 面板在 OnGui 里把可点控件屏幕矩形登记进来（每帧覆盖）；驱动读最新值注入鼠标。
// 零行为零断言：生产路径只是多一次 map 赋值；键用 "面板.控件" 命名。
#pragma once

#include <string>
#include <unordered_map>

#include "imgui.h"

namespace lemon::editor::testhooks {

/// key → 控件矩形（屏幕点 min/max）。每帧由 UI 侧覆盖登记。
inline std::unordered_map<std::string, ImVec4>& Rects() {
    static std::unordered_map<std::string, ImVec4> m;
    return m;
}

inline void Stash(const char* key, ImVec2 mn, ImVec2 mx) {
    Rects()[key] = ImVec4{mn.x, mn.y, mx.x, mx.y};
}

/// 驱动侧取矩形；false = 本帧未登记（控件没画/没滚动到）
inline bool Find(const char* key, ImVec2& mn, ImVec2& mx) {
    const auto it = Rects().find(key);
    if (it == Rects().end()) return false;
    mn = ImVec2{it->second.x, it->second.y};
    mx = ImVec2{it->second.z, it->second.w};
    return true;
}

/// 每帧 UI 构建前清空（防隐藏控件的陈旧矩形误导注入）
inline void ClearAll() { Rects().clear(); }

} // namespace lemon::editor::testhooks
