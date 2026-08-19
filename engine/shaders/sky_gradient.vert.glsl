#version 450

// Single full-screen triangle, no vertex buffer. The clip position is passed
// through so the fragment shader can turn it back into a view direction; the
// perspective divide on a w of 1 leaves xy as NDC, and z = 1 is the far plane,
// which is the depth a background belongs at whether or not the pipeline
// happens to test against it.
layout(location = 0) out vec2 vNdc;

void main()
{
    vec2 corner = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    vNdc = corner * 2.0 - 1.0;
    gl_Position = vec4(vNdc, 1.0, 1.0);
}
