#version 450
// Lemon 光贴图片元着色器（M8 批②，ADR-019 M1）：按 type 计算预乘光照贡献
// （rgb = color × intensity × 衰减，a = 1），混合由管线承担：
//   Additive = c + d / Screen = c + d·(1−c) / Subtract = d − c（RHI BlendMode）
// 衰减定形（02 §8，2026-10-11 拍板）：径向 t = clamp(1 − dn)，att = t²(3−2t)
// （smoothstep 端点零导数，range 边界无亮环/硬切）；Spot 角向内外角同款 soft。
// CPU 侧纯函数 LightRadialAttenuation/LightSpotAngular 同式（LightTypes.h）——
// 改公式两处同步。
// SpriteMask（批③）：蒙版因子 = texel.rgb × texel.a（白 RGB+alpha 渐变 glow 与
// 不透明白灰度蒙版两类素材通吃）；无径向衰减——贴图即造型（Unity Sprite 灯同
// 语义）；蒙版页槽按实例自 typeBlend 低 24 位（nonuniform 索引，RHI 特性位担保）
#extension GL_EXT_nonuniform_qualifier : require
layout(set = 0, binding = 0) uniform texture2D uAtlases[256];
layout(set = 0, binding = 1) uniform sampler uSamplers[8];

layout(push_constant, std430) uniform PC {
    vec4 vpR0;
    vec4 vpR1;
    uint baseInstance;
    uint ringIndex;
    uint _pad0;
    uint _pad1;
} pcf; // 与 light.vert 逐字段一致（本着色器不消费实例数据——参数走 flat 插值）

layout(location = 0) in vec2 vLocal;
layout(location = 1) flat in vec4 vParams;
layout(location = 2) flat in uint vTypeBlend;
layout(location = 3) flat in vec4 vColor;
layout(location = 4) in vec2 vMaskUV;

layout(location = 0) out vec4 oColor;

float smooth01(float t) { return t * t * (3.0 - 2.0 * t); }

void main() {
    const uint type = vTypeBlend >> 28;
    // SpriteMask（type 3，批③）：蒙版贴图即造型——无径向衰减，quad = 2×range 方形
    //（vMaskUV 由 light.vert 按 D 行 UV 矩形插值）；页槽 = typeBlend 低 24 位
    //（per-instance → nonuniform 索引，draw 内不动态一致）
    if (type == 3u) {
        const uint page = vTypeBlend & 0x00FFFFFFu;
        vec4 m = texture(sampler2D(uAtlases[nonuniformEXT(page)], uSamplers[0]), vMaskUV);
        oColor = vec4(vColor.rgb * vParams.x * m.rgb * m.a, 1.0);
        return;
    }
    // 径向：vLocal ∈ [-0.5,0.5]²，四边形边长 = 2×range → 归一化距离 dn = 2|vLocal|
    //（四边形是 range 圆的外接方：角点 dn = √2 > 1 → att = 0，角区零贡献）
    float dn = length(vLocal) * 2.0;
    float att = smooth01(clamp(1.0 - dn, 0.0, 1.0));
    if (type == 2u) {
        // Spot 角向软边：方向 = 实例旋转（local +X）；外半角 angle/2、内半角 ×innerRatio
        float halfOuter = clamp(vParams.z * 0.5, 0.0, 3.14159265);
        // min（非 max）：innerRatio < 1 取真内半角（与 CPU LightSpotAngular 同式）；
        // = 1 时收窄到 0.999·外半角——防 smoothstep edge0==edge1 除零
        float halfInner = min(halfOuter * clamp(vParams.w, 0.0, 1.0), halfOuter * 0.999);
        float len = max(length(vLocal), 1e-6);
        float cosAng = vLocal.x / len;
        // angle=0（halfOuter=0）无锥角 = 零贡献（CPU 同判：cosAng ≤ outerCos=1 恒真 → 0）
        float ang = halfOuter > 0.0 ? smoothstep(cos(halfOuter), cos(halfInner), cosAng) : 0.0;
        att *= ang;
    }
    oColor = vec4(vColor.rgb * vParams.x * att, 1.0);
}
