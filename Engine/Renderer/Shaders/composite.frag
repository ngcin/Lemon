#version 450
// Lemon 光贴图合成片元着色器（M8 批②，ADR-019 M1）：
//   out = sceneRT.rgb × lightRT.rgb —— **gamma 空间直乘**（02 §8 2026-10-11 评审
//   拍板：v1 = LDR（双 RT 均 RGBA8Unorm）+ gamma 空间乘法合成，像素风惯例，
//   有意决策防后人当 bug 修；HDR bloom（16F RT）列 v1.1 触发项）。
// 数组大小须与 rhi::kMaxTextureSlots 同源（D10 护栏，sprite.frag 同注）
layout(set = 0, binding = 0) uniform texture2D uAtlases[256];
layout(set = 0, binding = 1) uniform sampler uSamplers[8];

layout(push_constant, std430) uniform PC {
    uint sceneTexIndex;
    uint lightTexIndex;
    uint samplerIndex;
    uint _pad;
} pcf;

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 oColor;

void main() {
    vec3 scene = texture(sampler2D(uAtlases[pcf.sceneTexIndex], uSamplers[pcf.samplerIndex]), vUV).rgb;
    vec3 light = texture(sampler2D(uAtlases[pcf.lightTexIndex], uSamplers[pcf.samplerIndex]), vUV).rgb;
    oColor = vec4(scene * light, 1.0);
}
