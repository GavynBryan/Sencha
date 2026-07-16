#version 450
#extension GL_GOOGLE_include_directive : require

#include "mesh_frame.glsli"

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv0;
// Per-instance world matrix, one vec4 column per attribute (binding 1,
// instance rate): instanced runs draw many placements in one call.
layout(location = 3) in vec4 inWorld0;
layout(location = 4) in vec4 inWorld1;
layout(location = 5) in vec4 inWorld2;
layout(location = 6) in vec4 inWorld3;

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

    mat3 linear = mat3(world);
    vec3 cofactor0 = cross(linear[1], linear[2]);
    vec3 cofactor1 = cross(linear[2], linear[0]);
    vec3 cofactor2 = cross(linear[0], linear[1]);
    float orientation = dot(linear[0], cofactor0) < 0.0 ? -1.0 : 1.0;
    mat3 normalMatrix = mat3(cofactor0, cofactor1, cofactor2) * orientation;

    outWorldNormal = normalize(normalMatrix * inNormal);
    outUv0 = inUv0;
    outWorldPos = worldPosition.xyz;
    gl_Position = frame.ViewProjection * worldPosition;
}
