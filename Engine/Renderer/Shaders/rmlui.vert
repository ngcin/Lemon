#version 450
// RmlUi 顶点着色器（M6a 批③a，ADR-014）——顶点 = Rml::Vertex 原样（零转换）：
//   location 0 = position vec2（像素）  1 = colour R8G8B8A8（预乘）  2 = tex_coord vec2
// push constant 72B：mvp（像素→NDC，含 SetTransform 折叠）+ translate（RenderGeometry 入参）
layout(push_constant, std430) uniform PC {
    mat4 mvp;
    vec2 translate;
} pc;

layout(location = 0) in vec2 aPos;
layout(location = 1) in vec4 aColor;
layout(location = 2) in vec2 aUV;

layout(location = 0) out vec4 vColor;
layout(location = 1) out vec2 vUV;

void main() {
    vColor = aColor;
    vUV = aUV;
    gl_Position = pc.mvp * vec4(aPos + pc.translate, 0.0, 1.0);
}
