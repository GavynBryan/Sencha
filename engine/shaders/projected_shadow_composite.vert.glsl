#version 450

// Single full-screen triangle over the view; the composite's reach is
// bounded by its scissor, not by geometry.
layout(location = 0) out vec2 vUv;

void main()
{
    vUv = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    gl_Position = vec4(vUv * 2.0 - 1.0, 0.0, 1.0);
}
