#version 450
// RmlUi 无纹理变体（M6a 批③a）：预乘 alpha 直出（管线混合 = ONE/ONE_MINUS_SRC_ALPHA）
layout(location = 0) in vec4 vColor;

layout(location = 0) out vec4 oColor;

void main() { oColor = vColor; }
