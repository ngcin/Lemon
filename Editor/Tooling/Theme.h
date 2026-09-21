// Lemon 编辑器 — 主题 token 单点定义（M4.7a §2.4；决议 D2：贴近 Unity 深色）
// 全部 UI 色值/圆角从此处取用，散落 ImVec4 字面量一律收编；overlay 特征色除外
// （Interaction/ViewportRenderer.h overlay::——渲染通道契约，与 UI 主题解耦）。
#pragma once

#include "imgui.h"

namespace lemon::editor::theme {

// ---- 背景三层（中性深灰，Unity 深色系）----
inline constexpr ImVec4 kBgDeep{0.113f, 0.114f, 0.122f, 1.00f};  // 窗口/面板底
inline constexpr ImVec4 kBgMid{0.160f, 0.161f, 0.172f, 1.00f};   // 控件/输入框底
inline constexpr ImVec4 kBgHover{0.208f, 0.210f, 0.224f, 1.00f}; // 控件悬停
inline constexpr ImVec4 kBgActive{0.253f, 0.255f, 0.271f, 1.00f};// 控件按下
inline constexpr ImVec4 kPopupBg{0.108f, 0.109f, 0.118f, 0.96f}; // 弹层底（微透）
inline constexpr ImVec4 kBorder{0.224f, 0.225f, 0.239f, 1.00f};  // 分隔/边框

// ---- 主强调（决议 D3：Unity 蓝惯例）----
inline constexpr ImVec4 kAccent{0.298f, 0.573f, 0.851f, 1.00f};  // 亮蓝：播放态/主选
inline constexpr ImVec4 kAccentDim{0.208f, 0.302f, 0.400f, 1.00f}; // 灰蓝：编辑态 Play
inline constexpr ImVec4 kSelectRow{0.165f, 0.325f, 0.478f, 0.60f}; // 列表选中行
inline constexpr ImVec4 kPlayStop{0.678f, 0.239f, 0.212f, 1.00f};  // Stop 红调

// ---- 文本三级 + 警示三级 ----
inline constexpr ImVec4 kText{0.845f, 0.855f, 0.867f, 1.00f};
inline constexpr ImVec4 kTextDim{0.512f, 0.520f, 0.533f, 1.00f};
inline constexpr ImVec4 kTextBright{0.950f, 0.958f, 0.965f, 1.00f};
inline constexpr ImVec4 kTextError{0.937f, 0.345f, 0.345f, 1.00f};
inline constexpr ImVec4 kTextWarn{0.937f, 0.784f, 0.345f, 1.00f};
inline constexpr ImVec4 kTextOk{0.541f, 0.835f, 0.569f, 1.00f};

// ---- 圆角/内距 ----
inline constexpr float kRounding = 3.0f; // 全控件统一（窗 4 / 控件 3，Unity 近似）

/// 启动（与 DPI 变更重建字体时）调用一次：整体铺色 + 圆角/间距（乘 displayScale）。
/// 替代 ImGui 默认深色与旧"柠檬黄点缀"——本函数是 ImGuiStyle 颜色的唯一写点。
void ApplyTheme(float displayScale);

} // namespace lemon::editor::theme
