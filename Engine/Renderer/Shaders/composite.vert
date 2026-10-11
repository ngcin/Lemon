#version 450
// Lemon 光贴图合成顶点着色器（M8 批②，ADR-019 M1）：gameRT 全屏四边形。
// 角点 [-0.5,0.5] 直推 NDC [-1,1]（无需 vp——pass 视口即整个 RT）；
// UV = corner + 0.5，与 ImGui 采样 gameRT 同向（引擎 Y 向下）。
layout(push_constant, std430) uniform PC {
    uint sceneTexIndex; // bindless 槽（sceneRT）
    uint lightTexIndex; // bindless 槽（lightRT）
    uint samplerIndex;  // bindless 采样器槽
    uint _pad;
} pc; // 本阶段不消费索引（frag 用），块字段与 composite.frag 逐字段一致

layout(location = 0) in vec2 aCorner;

layout(location = 0) out vec2 vUV;

void main() {
    gl_Position = vec4(aCorner * 2.0, 0.0, 1.0);
    vUV = aCorner + 0.5;
}
