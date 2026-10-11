#version 450
// Lemon 后处理终段（M8 批④，02 §4 FX Pass）：冲击波 UV 扰动 + bloom 加色 +
// 色调曲线，合并单 pass（链式顺序语义保持——bloom 提取基于无扰动图（02 §4：
// Bloom→Shockwave→Tone），扰动在最终采样施加；无活跃波时扰动项为零 = 该
// 效果零额外 pass 成本）。目标 = 显示目标（编辑器 gameRT / lemon-game 交换链），
// 由装配方按目标格式建管线（LightCompositor compositeTargetFormat 同款）。
// 数组大小须与 rhi::kMaxTextureSlots 同源（D10 护栏，sprite.frag 同注）
layout(set = 0, binding = 0) uniform texture2D uAtlases[256];
layout(set = 0, binding = 1) uniform sampler uSamplers[8];

layout(push_constant, std430) uniform PC {
    uint sceneTexIndex; // bindless 槽（fxSceneRT）
    uint bloomTexIndex; // bindless 槽（bloomA）
    uint samplerIndex;
    uint waveCount;     // 活跃冲击波数（≤4；0 = 扰动项整体跳过）
    vec4 uWaves[4];     // xy = 中心（UV）；z = 波前半径（归一化）；w = 幅度（UV 偏移）
    vec4 uParams;       // x=bloomIntensity y=toneContrast z=toneWarmth w=aspect(w/h)
} pcf;

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 oColor;

void main() {
    // 冲击波扰动：aspect 校正距离上的环形高斯偏移（ShockwaveOffset 同式，
    // PostFx.h CPU 镜像——改公式两处同步）
    vec2 uv = vUV;
    for (uint i = 0u; i < pcf.waveCount; ++i) {
        vec2 d = (uv - pcf.uWaves[i].xy) * vec2(pcf.uParams.w, 1.0);
        float dist = length(d);
        float t = dist - pcf.uWaves[i].z;
        const float ringW = 0.06; // 环宽（归一化；PostFx.h kShockwaveRingW 同值）
        float off = pcf.uWaves[i].w * exp(-(t * t) / (ringW * ringW));
        if (dist > 1e-5) uv += (d / dist) / vec2(pcf.uParams.w, 1.0) * off;
    }
    // scene 走扰动 uv；bloom 低频光晕走原 uv（亚像素位移观测不可分，裁决见批文件 §3.6）
    vec3 scene = texture(sampler2D(uAtlases[pcf.sceneTexIndex], uSamplers[pcf.samplerIndex]), uv).rgb;
    vec3 bloom = texture(sampler2D(uAtlases[pcf.bloomTexIndex], uSamplers[pcf.samplerIndex]), vUV).rgb;
    vec3 c = scene + bloom * pcf.uParams.x;
    // 色调曲线（LDR）：0.5 锚 S 曲线对比 + luma 暖偏（ToneApply CPU 镜像）
    c = clamp((c - 0.5) * pcf.uParams.y + 0.5, 0.0, 1.0);
    float luma = dot(c, vec3(0.299, 0.587, 0.114));
    c += pcf.uParams.z * luma * vec3(1.0, 0.3, -0.5);
    oColor = vec4(clamp(c, 0.0, 1.0), 1.0);
}
