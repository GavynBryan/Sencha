#version 450

// Single full-screen triangle (3 vertices, no vertex buffer). UV spans [0,1] across
// the screen. Shared by the bloom passes.
layout(location = 0) out vec2 vUv;

void main()
{
    vUv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(vUv * 2.0 - 1.0, 0.0, 1.0);
}
