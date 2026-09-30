#version 450
// Lemon 路径 A 片元着色器：bindless 纹理数组 + 采样器数组（02 §3.4）
// 索引来自 push constant（每个 draw 内动态一致 → 无需 nonuniform 限定）
// 数组大小须与 rhi::kMaxTextureSlots（RHI.h，=256）同源——Engine/CMakeLists.txt 配置期
// 有一致性断言；slot ≥ 数组长度的采样是未定义行为（曾 64 vs 256 静默越界，评审 D10）
layout(set = 0, binding = 0) uniform texture2D uAtlases[256];
layout(set = 0, binding = 1) uniform sampler uSamplers[8];

layout(push_constant, std430) uniform PC {
    vec4 vpR0;
    vec4 vpR1;
    uint baseInstance;
    uint atlasIndex;
    uint samplerIndex;
    uint ringIndex; // 与 sprite.vert 逐字段一致（本着色器不消费）
} pcf;

layout(location = 0) in vec4 vColor;
layout(location = 1) in vec2 vUV;
layout(location = 0) out vec4 oColor;

void main() {
    oColor = texture(sampler2D(uAtlases[pcf.atlasIndex], uSamplers[pcf.samplerIndex]), vUV) * vColor;
}
