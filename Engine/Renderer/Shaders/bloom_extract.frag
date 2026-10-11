#version 450
// Lemon 后处理 bloom 亮部提取（M8 批④，02 §4 FX Pass）：
//   out = max(scene.rgb − threshold, 0) —— LDR 阈值语义（02 §8 拍板：v1 双 RT 均
//   RGBA8Unorm，bloom 只做"近白像素"提取；HDR 16F RT 列 v1.1 触发项）。
// 写 bloomA（1/4 分辨率）——线性采样即 4:1 降采样（srcT 全分辨率）。
// 数组大小须与 rhi::kMaxTextureSlots 同源（D10 护栏，sprite.frag 同注）
layout(set = 0, binding = 0) uniform texture2D uAtlases[256];
layout(set = 0, binding = 1) uniform sampler uSamplers[8];

layout(push_constant, std430) uniform PC {
    uint sceneTexIndex; // bindless 槽（fxSceneRT）
    uint samplerIndex;
    float threshold;    // LDR 阈值（PostFxParams.bloomThreshold）
    float _pad;
} pcf;

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 oColor;

void main() {
    vec3 c = texture(sampler2D(uAtlases[pcf.sceneTexIndex], uSamplers[pcf.samplerIndex]), vUV).rgb;
    oColor = vec4(max(c - pcf.threshold, vec3(0.0)), 1.0);
}
