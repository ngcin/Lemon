#version 450
layout(push_constant) uniform PC {
    vec2 resolution;
} pc;

layout(location = 0) in vec2 aCorner;   // 单位四边形角点 [-0.5, 0.5]
// ---- per-instance（VK_VERTEX_INPUT_RATE_INSTANCE）----
layout(location = 1) in vec2 iPos;      // 像素坐标
layout(location = 2) in float iRot;
layout(location = 3) in float iScale;   // 像素直径
layout(location = 4) in uint iColor;    // rgba8

layout(location = 0) out vec4 vColor;
layout(location = 1) out vec2 vUV;

void main() {
    float c = cos(iRot), s = sin(iRot);
    vec2 local = vec2(aCorner.x * c - aCorner.y * s,
                      aCorner.x * s + aCorner.y * c) * iScale;
    vec2 p = (iPos + local) / pc.resolution * 2.0 - 1.0;
    gl_Position = vec4(p.x, -p.y, 0.0, 1.0);
    vColor = unpackUnorm4x8(iColor);
    vUV = aCorner + 0.5;
}
