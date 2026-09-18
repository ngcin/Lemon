#version 450
// Lemon 路径 A 顶点着色器：单位四边形 + per-instance 环形 SSBO（02 §3.3/3.4）
// 实例 48B = 3×vec4（与 CPU 侧 SpriteInstance 逐字段对应）：
//   A.xy = 仿射行0(a,b)   A.zw = 行1(c,d)
//   B.xy = 平移(e,f)      B.zw = uv0
//   C.xy = uv1            C.z  = colorBits   C.w = flags(bit0 flipX, bit1 flipY)
layout(push_constant, std430) uniform PC {
    vec4 vpR0;          // 世界→NDC 正交仿射：xy = 行0, zw = 行1
    vec4 vpR1;          // xy = 平移(e,f)
    uint baseInstance;  // 本批在实例环形 SSBO 中的基偏移
    uint atlasIndex;    // bindless 纹理槽
    uint samplerIndex;  // bindless 采样器槽
    uint flags;         // 预留
} pc;

layout(set = 0, binding = 2, std430) readonly buffer InstanceRing {
    vec4 inst[];
} ring;

layout(location = 0) in vec2 aCorner; // 单位四边形角点 [-0.5, 0.5]

layout(location = 0) out vec4 vColor;
layout(location = 1) out vec2 vUV;

void main() {
    uint bi = (pc.baseInstance + gl_InstanceIndex) * 3u;
    vec4 A = ring.inst[bi + 0u];
    vec4 B = ring.inst[bi + 1u];
    vec4 C = ring.inst[bi + 2u];

    vec2 local = vec2(A.x * aCorner.x + A.z * aCorner.y,
                      A.y * aCorner.x + A.w * aCorner.y) + B.xy;
    vec2 ndc = vec2(pc.vpR0.x * local.x + pc.vpR0.z * local.y + pc.vpR1.x,
                    pc.vpR0.y * local.x + pc.vpR0.w * local.y + pc.vpR1.y);
    gl_Position = vec4(ndc, 0.0, 1.0);

    vec2 corner = aCorner;
    uint iflags = floatBitsToUint(C.w);
    if ((iflags & 1u) != 0u) corner.x = -corner.x;
    if ((iflags & 2u) != 0u) corner.y = -corner.y;
    vUV = mix(B.zw, C.xy, corner + 0.5);
    vColor = unpackUnorm4x8(floatBitsToUint(C.z));
}
