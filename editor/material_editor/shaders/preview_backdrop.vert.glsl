#version 450

// Fullscreen triangle behind the preview mesh. vUv covers [0,1] across the
// visible target.
layout(location = 0) out vec2 vUv;

void main()
{
    vec2 p = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    vUv = p; // (0,0), (2,0), (0,2) -> covers [0,1] across the visible region
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
