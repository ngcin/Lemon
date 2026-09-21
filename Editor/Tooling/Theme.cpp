// Lemon 编辑器 — 主题铺设（M4.7a；token 见 Theme.h）
#include "Tooling/Theme.h"

namespace lemon::editor::theme {

void ApplyTheme(float displayScale) {
    ImGui::StyleColorsDark(); // 基线（未显式覆盖项沿用），下方整体重铺关键面
    ImGuiStyle& s = ImGui::GetStyle();
    const float k = displayScale;
    s.WindowRounding = 4.0f * k;
    s.ChildRounding = 4.0f * k;
    s.FrameRounding = kRounding * k;
    s.GrabRounding = kRounding * k;
    s.PopupRounding = kRounding * k;
    s.TabRounding = 4.0f * k;
    s.ScrollbarRounding = 3.0f * k;
    s.WindowBorderSize = 1.0f * k;
    s.FrameBorderSize = 0.0f;
    s.WindowPadding = ImVec2(8.0f * k, 8.0f * k);
    s.FramePadding = ImVec2(8.0f * k, 4.0f * k);
    s.ItemSpacing = ImVec2(8.0f * k, 6.0f * k);
    s.ScrollbarSize = 13.0f * k;

    ImVec4* c = s.Colors;
    // 背景三层：深（窗口）→ 中（控件）→ hover/active 渐亮；弹层同深色微透
    c[ImGuiCol_WindowBg] = kBgDeep;
    c[ImGuiCol_ChildBg] = kBgDeep;
    c[ImGuiCol_PopupBg] = kPopupBg;
    c[ImGuiCol_Border] = kBorder;
    c[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_TitleBg] = ImVec4(0.086f, 0.087f, 0.094f, 1.0f);
    c[ImGuiCol_TitleBgActive] = ImVec4(0.130f, 0.131f, 0.141f, 1.0f);
    c[ImGuiCol_TitleBgCollapsed] = ImVec4(0.086f, 0.087f, 0.094f, 1.0f);
    c[ImGuiCol_MenuBarBg] = ImVec4(0.130f, 0.131f, 0.141f, 1.0f);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0.096f, 0.097f, 0.105f, 1.0f);
    c[ImGuiCol_ScrollbarGrab] = kBgMid;
    c[ImGuiCol_ScrollbarGrabHovered] = kBgHover;
    c[ImGuiCol_ScrollbarGrabActive] = kBgActive;
    // 控件
    c[ImGuiCol_FrameBg] = kBgMid;
    c[ImGuiCol_FrameBgHovered] = kBgHover;
    c[ImGuiCol_FrameBgActive] = kBgActive;
    c[ImGuiCol_Button] = kBgMid;
    c[ImGuiCol_ButtonHovered] = kBgHover;
    c[ImGuiCol_ButtonActive] = kBgActive;
    // 列表选中行/可点行（Hierarchy/菜单项）——主强调的暗化形
    c[ImGuiCol_Header] = kSelectRow;
    c[ImGuiCol_HeaderHovered] = ImVec4(kSelectRow.x * 1.15f, kSelectRow.y * 1.15f,
                                       kSelectRow.z * 1.15f, 0.75f);
    c[ImGuiCol_HeaderActive] = ImVec4(kSelectRow.x * 1.25f, kSelectRow.y * 1.25f,
                                      kSelectRow.z * 1.25f, 0.85f);
    // 勾选/滑块/分隔
    c[ImGuiCol_CheckMark] = kAccent;
    c[ImGuiCol_SliderGrab] = kAccentDim;
    c[ImGuiCol_SliderGrabActive] = kAccent;
    c[ImGuiCol_Separator] = kBorder;
    c[ImGuiCol_SeparatorHovered] = kBgActive;
    c[ImGuiCol_SeparatorActive] = kAccent;
    // Tab（面板页签）：选中页签亮一档，激活线走主强调
    c[ImGuiCol_Tab] = ImVec4(0.096f, 0.097f, 0.105f, 1.0f);
    c[ImGuiCol_TabHovered] = kBgHover;
    c[ImGuiCol_TabSelected] = ImVec4(0.173f, 0.174f, 0.186f, 1.0f);
    c[ImGuiCol_TabDimmed] = ImVec4(0.096f, 0.097f, 0.105f, 1.0f);
    c[ImGuiCol_TabDimmedSelected] = ImVec4(0.173f, 0.174f, 0.186f, 1.0f);
    // Dock（预览/目标高亮走主强调）
    c[ImGuiCol_DockingPreview] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.35f);
    c[ImGuiCol_DockingEmptyBg] = ImVec4(0.075f, 0.076f, 0.082f, 1.0f);
    c[ImGuiCol_ResizeGrip] = ImVec4(kBgMid.x, kBgMid.y, kBgMid.z, 0.40f);
    c[ImGuiCol_ResizeGripHovered] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.60f);
    c[ImGuiCol_ResizeGripActive] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.80f);
    // 文本
    c[ImGuiCol_Text] = kText;
    c[ImGuiCol_TextDisabled] = kTextDim;
    c[ImGuiCol_TextSelectedBg] = ImVec4(kSelectRow.x, kSelectRow.y, kSelectRow.z, 0.80f);
    c[ImGuiCol_DragDropTarget] = kAccent;
    c[ImGuiCol_NavCursor] = kAccent;
    // 表头（Inspector 组件标题等）
    c[ImGuiCol_TableHeaderBg] = ImVec4(0.140f, 0.141f, 0.151f, 1.0f);
    c[ImGuiCol_TableBorderStrong] = kBorder;
    c[ImGuiCol_TableBorderLight] = ImVec4(0.180f, 0.181f, 0.193f, 1.0f);
    c[ImGuiCol_TableRowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_TableRowBgAlt] = ImVec4(1.0f, 1.0f, 1.0f, 0.030f);
    // 模态遮罩（关闭确认等）：中性暗化而非旧黄调
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.04f, 0.04f, 0.05f, 0.60f);
}

} // namespace lemon::editor::theme
