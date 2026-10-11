#version 450
// Lemon 后处理 bloom 上采样（M8 批④，02 §4）：双滤波 up——4-tap tent（对角
// 偏移四点，线性采样各盖 2×2 邻域 = 3×3 tent 等效）。链位：C(1/16)→B(1/8)→A(1/4)，
// 逐级放大回 1/4（写目标覆盖 down 段残留——读写不同 RT 无 hazard）。
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
    vec2 o = vec2(pcf.srcTexelX, pcf.srcTexelY);
    vec3 c = texture(sampler2D(uAtlases[pcf.srcTexIndex], uSamplers[pcf.samplerIndex]), vUV + o).rgb
           + texture(sampler2D(uAtlases[pcf.srcTexIndex], uSamplers[pcf.samplerIndex]), vUV - o).rgb
           + texture(sampler2D(uAtlases[pcf.srcTexIndex], uSamplers[pcf.samplerIndex]), vUV + vec2(o.x, -o.y)).rgb
           + texture(sampler2D(uAtlases[pcf.srcTexIndex], uSamplers[pcf.samplerIndex]), vUV + vec2(-o.x, o.y)).rgb;
    oColor = vec4(c * 0.25, 1.0);
}
