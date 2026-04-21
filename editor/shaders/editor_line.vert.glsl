#version 450

layout(push_constant) uniform PushConstants {
    mat4 ViewProjection;
} Push;

layout(location = 0) in vec3 InPosition;
layout(location = 1) in vec4 InColor;

layout(location = 0) out vec4 OutColor;

void main()
{
    gl_Position = Push.ViewProjection * vec4(InPosition, 1.0);
    OutColor = InColor;
}
