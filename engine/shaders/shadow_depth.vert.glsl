#version 450

layout(set = 0, binding = 0) uniform ShadowFrame
{
    mat4 ViewProjection;
} frame;

layout(location = 0) in vec3 inPosition;
layout(location = 3) in vec4 inWorld0;
layout(location = 4) in vec4 inWorld1;
layout(location = 5) in vec4 inWorld2;
layout(location = 6) in vec4 inWorld3;

void main()
{
    mat4 world = mat4(inWorld0, inWorld1, inWorld2, inWorld3);
    gl_Position = frame.ViewProjection * world * vec4(inPosition, 1.0);
}
