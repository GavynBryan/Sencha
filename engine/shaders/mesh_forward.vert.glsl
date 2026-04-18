#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv0;

layout(set = 0, binding = 0) uniform MeshFrame
{
    mat4 ViewProjection;
    vec4 ViewPositionTime;
} frame;

layout(push_constant) uniform MeshPush
{
    mat4 World;
    vec4 BaseColor;
} pushData;

layout(location = 0) out vec3 outWorldNormal;
layout(location = 1) out vec2 outUv0;

void main()
{
    vec4 worldPosition = pushData.World * vec4(inPosition, 1.0);
    outWorldNormal = mat3(pushData.World) * inNormal;
    outUv0 = inUv0;
    gl_Position = frame.ViewProjection * worldPosition;
}
