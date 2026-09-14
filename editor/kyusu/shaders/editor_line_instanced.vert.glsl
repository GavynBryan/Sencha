#version 450

// Instanced line list: the vertices are one mesh's edges in its own frame, and
// each instance carries the model matrix of one placement of that mesh, so a
// brush repeated hundreds of times uploads its edges once per frame.

layout(push_constant) uniform PushConstants {
    mat4 ViewProjection;
} Push;

layout(location = 0) in vec3 InPosition;
layout(location = 1) in vec4 InColor;
// Row-major model matrix, one row per attribute (binding 1, per instance).
layout(location = 2) in vec4 InModelRow0;
layout(location = 3) in vec4 InModelRow1;
layout(location = 4) in vec4 InModelRow2;
layout(location = 5) in vec4 InModelRow3;

layout(location = 0) out vec4 OutColor;

void main()
{
    const vec4 local = vec4(InPosition, 1.0);
    const vec4 world = vec4(dot(InModelRow0, local), dot(InModelRow1, local),
                            dot(InModelRow2, local), dot(InModelRow3, local));
    gl_Position = Push.ViewProjection * world;
    OutColor = InColor;
}
