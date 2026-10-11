#version 450
// Lemon 后处理 bloom 降采样（M8 批④，02 §4）：双滤波（dual filter）down——
// 半像素偏移 4-tap box（线性采样点 = 各 2×2 像素均值，实际 16 像素等效盒）。
// 链位：bloomA(1/4)→B(1/8)→C(1/16)；srcTexel = 源 RT 归一化纹素。
// 数组大小须与 rhi::kMaxTextureSlots 同源（D10 护栏，sprite.frag 同注）
layout(set = 0, binding = 0) uniform texture2D uAtlases[256];
layout(set = 0, binding = 1) uniform sampler uSamplers[8];

layout(push_constant, std430) uniform PC {
    uint srcTexIndex;   // bindless 槽（源 bloom RT）
    uint samplerIndex;
    float srcTexelX;    // 源 RT 归一化纹素宽（1/srcW）
    float srcTexelY;    // 源 RT 归一化纹素高（1/srcH）
} pcf;

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 oColor;

void main() {
    vec2 o = vec2(pcf.srcTexelX, pcf.srcTexelY) * 0.5;
    vec3 c = texture(sampler2D(uAtlases[pcf.srcTexIndex], uSamplers[pcf.samplerIndex]), vUV + o).rgb
           + texture(sampler2D(uAtlases[pcf.srcTexIndex], uSamplers[pcf.samplerIndex]), vUV - o).rgb
           + texture(sampler2D(uAtlases[pcf.srcTexIndex], uSamplers[pcf.samplerIndex]), vUV + vec2(o.x, -o.y)).rgb
           + texture(sampler2D(uAtlases[pcf.srcTexIndex], uSamplers[pcf.samplerIndex]), vUV + vec2(-o.x, o.y)).rgb;
    oColor = vec4(c * 0.25, 1.0);
}
