#version 450
// Lemon 光贴图顶点着色器（M8 批②，ADR-019 M1）：光源四边形 per-instance 环形 SSBO。
// 实例 64B = 4×vec4（与 CPU 侧 LightInstance 逐字段对应，LightTypes.h）：
//   A.xy = 仿射行0(a,b)   A.zw = 行1(c,d)     —— scale = 四边形世界边长（2×range）
//   B.xy = 平移(e,f)      B.z = colorBits     B.w = type<<16|blend
//   C    = intensity, range, angle, innerRatio
//   D    = 蒙版 UV（SpriteMask 用，批③ 消费；Point/Spot 弃）
layout(push_constant, std430) uniform PC {
    vec4 vpR0;          // 世界→NDC 正交仿射（sprite.vert 同款）
    vec4 vpR1;
    uint baseInstance;  // 本批在实例环中的基偏移
    uint ringIndex;     // 实例环 SSBO 数组槽（LightBatcher 固定槽）
    uint _pad0;
    uint _pad1;
} pc;

// 环形 SSBO 数组（与 sprite.vert 同 binding；LightBatcher 绑固定槽，M4.7-P0 语义）
layout(set = 0, binding = 2, std430) readonly buffer InstanceRing {
    vec4 inst[];
} rings[4];

layout(location = 0) in vec2 aCorner; // 单位四边形角点 [-0.5, 0.5]

layout(location = 0) out vec2 vLocal;           // 角点原值（径向/角向计算坐标系）
layout(location = 1) flat out vec4 vParams;     // C（intensity/range/angle/innerRatio）
layout(location = 2) flat out uint vTypeBlend;  // type<<16|blend
layout(location = 3) flat out vec4 vColor;      // unpack 后颜色
layout(location = 4) out vec2 vMaskUV;          // 蒙版插值 UV（批③ SpriteMask 消费）

void main() {
    uint bi = (pc.baseInstance + gl_InstanceIndex) * 4u;
    vec4 A = rings[pc.ringIndex].inst[bi + 0u];
    vec4 B = rings[pc.ringIndex].inst[bi + 1u];
    vec4 C = rings[pc.ringIndex].inst[bi + 2u];
    vec4 D = rings[pc.ringIndex].inst[bi + 3u];

    vec2 local = vec2(A.x * aCorner.x + A.z * aCorner.y,
                      A.y * aCorner.x + A.w * aCorner.y) + B.xy;
    vec2 ndc = vec2(pc.vpR0.x * local.x + pc.vpR0.z * local.y + pc.vpR1.x,
                    pc.vpR0.y * local.x + pc.vpR0.w * local.y + pc.vpR1.y);
    gl_Position = vec4(ndc, 0.0, 1.0);

    vLocal = aCorner;
    vParams = C;
    vTypeBlend = floatBitsToUint(B.w);
    vColor = unpackUnorm4x8(floatBitsToUint(B.z));
    vMaskUV = mix(D.xy, D.zw, aCorner + 0.5);
}
