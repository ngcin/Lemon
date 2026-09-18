#version 450
// Lemon 路径 A 片元着色器：bindless 纹理数组 + 采样器数组（02 §3.4）
// 索引来自 push constant（每个 draw 内动态一致 → 无需 nonuniform 限定）
layout(set = 0, binding = 0) uniform texture2D uAtlases[64];
layout(set = 0, binding = 1) uniform sampler uSamplers[8];

layout(push_constant, std430) uniform PC {
    vec4 vpR0;
    vec4 vpR1;
    uint baseInstance;
    uint atlasIndex;
    uint samplerIndex;
    uint flags;
} pcf;

layout(location = 0) in vec4 vColor;
layout(location = 1) in vec2 vUV;
layout(location = 0) out vec4 oColor;

void main() {
    oColor = texture(sampler2D(uAtlases[pcf.atlasIndex], uSamplers[pcf.samplerIndex]), vUV) * vColor;
}
