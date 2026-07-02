#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv0;
// Per-instance world matrix, one vec4 column per attribute (binding 1,
// instance rate): instanced runs draw many placements in one call.
layout(location = 3) in vec4 inWorld0;
layout(location = 4) in vec4 inWorld1;
layout(location = 5) in vec4 inWorld2;
layout(location = 6) in vec4 inWorld3;

layout(set = 0, binding = 0) uniform MeshFrame
{
    mat4 ViewProjection;
    vec4 ViewPositionTime;
} frame;

layout(push_constant) uniform MeshPush
{
    vec4 BaseColor;
    uint BaseColorTextureIndex;
} pushData;

layout(location = 0) out vec3 outWorldNormal;
layout(location = 1) out vec2 outUv0;
layout(location = 2) out vec3 outWorldPos;

void main()
{
    mat4 world = mat4(inWorld0, inWorld1, inWorld2, inWorld3);
    vec4 worldPosition = world * vec4(inPosition, 1.0);
    outWorldNormal = mat3(world) * inNormal;
    outUv0 = inUv0;
    outWorldPos = worldPosition.xyz;
    gl_Position = frame.ViewProjection * worldPosition;
}
