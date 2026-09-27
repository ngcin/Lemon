// Lemon 编辑器 — UI 图标辅助（M4.7b：形状页图标 → ImGui 按钮/行前缀渲染）
// 形状页见 Interaction/ViewportRenderer.cpp BuildIconPage；白形状经 tint 染主题色。
#pragma once

#include "App/EditorApp.h"
#include "Interaction/ViewportRenderer.h"
#include "Tooling/Theme.h"
#include "imgui.h"

namespace lemon::editor::ui {

/// 当前光标处绘制图标（行前缀/列表项用；不产生交互区）
inline void DrawIcon(EditorApp& app, IconKind k, const ImVec2& size, const ImVec4& tint) {
    void* tex = app.Viewport().IconTex();
    if (!tex) return;
    float u0, v0, u1, v1;
    app.Viewport().Assets().IconUV(k, u0, v0, u1, v1);
    ImGui::ImageWithBg(tex, size, ImVec2(u0, v0), ImVec2(u1, v1),
                       ImVec4(0.0f, 0.0f, 0.0f, 0.0f), tint);
}

/// 图标按钮：透明底（active = 主题蓝强调底），图标叠于按钮上；flipX = 水平
/// 镜像（上一帧/下一帧共用同形 glyph 的方向性图标）
inline bool IconButton(EditorApp& app, IconKind k, const char* id, bool active,
                       const ImVec2& iconSize = ImVec2(18, 18), bool flipX = false) {
    void* tex = app.Viewport().IconTex();
    if (!tex) return ImGui::Button(id);
    float u0, v0, u1, v1;
    app.Viewport().Assets().IconUV(k, u0, v0, u1, v1);
    if (flipX) {
        const float t = u0;
        u0 = u1;
        u1 = t;
    }
    ImGui::PushStyleColor(ImGuiCol_Button,
                          active ? theme::kAccentDim : ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                          active ? theme::kAccentDim : theme::kBgMid);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme::kBgActive);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 3));
    const bool clicked = ImGui::Button(id, ImVec2(iconSize.x + 8, iconSize.y + 6));
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
    const ImVec2 p = ImGui::GetItemRectMin(), s = ImGui::GetItemRectSize();
    ImGui::GetWindowDrawList()->AddImage(
        (ImTextureID)tex, ImVec2(p.x + (s.x - iconSize.x) * 0.5f, p.y + (s.y - iconSize.y) * 0.5f),
        ImVec2(p.x + (s.x + iconSize.x) * 0.5f, p.y + (s.y + iconSize.y) * 0.5f),
        ImVec2(u0, v0), ImVec2(u1, v1),
        ImGui::ColorConvertFloat4ToU32(active ? theme::kTextBright : theme::kText));
    return clicked;
}

} // namespace lemon::editor::ui
