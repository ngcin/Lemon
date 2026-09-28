#version 450
// RmlUi 纹理变体（M6a 批③a）：RGBA 预乘 × 顶点色（6.3 字体图集亦 RGBA 预乘——
// GenerateTexture 契约，无需 ALPHAMAP 变体；set0 = 每纹理懒建的 combined sampler）
layout(set = 0, binding = 0) uniform sampler2D uTex;

layout(location = 0) in vec4 vColor;
layout(location = 1) in vec2 vUV;

layout(location = 0) out vec4 oColor;

void main() { oColor = texture(uTex, vUV) * vColor; }
