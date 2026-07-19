#version 450
#extension GL_GOOGLE_include_directive : require

#include "mesh_frame.glsli"

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv0;
layout(location = 3) in vec4 inWorld0;
layout(location = 4) in vec4 inWorld1;
layout(location = 5) in vec4 inWorld2;
layout(location = 6) in vec4 inWorld3;
layout(location = 7) in vec4 inTangent;
layout(location = 8) in vec2 inLightmapUv;        // unorm16 atlas UV
layout(location = 9) in vec4 inLightmapScaleBias; // per-instance rect remap

layout(push_constant) uniform MeshPush
{
    vec4 BaseColor;
    vec4 EmissiveFactor;
    float NormalScale;
    float RoughnessFactor;
    float MetallicFactor;
    float SpecularIntensity;
    uint BaseColorTextureIndex;
    uint NormalTextureIndex;
    uint OrmTextureIndex;
    uint EmissiveTextureIndex;
    uint ReceiveShadows;
    uint LightmapTextureIndex;
    uint Pad1;
    uint Pad2;
} pushData;

layout(location = 0) out vec3 outWorldNormal;
layout(location = 1) out vec2 outUv0;
layout(location = 2) out vec3 outWorldPos;
layout(location = 3) out vec3 outWorldTangent;
layout(location = 4) out float outTangentSign;
layout(location = 5) out vec2 outLightmapUv;

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

    vec3 worldNormal = normalize(normalMatrix * inNormal);
    vec3 worldTangent = linear * inTangent.xyz;
    worldTangent = normalize(worldTangent - worldNormal * dot(worldNormal, worldTangent));

    outWorldNormal = worldNormal;
    outUv0 = inUv0;
    outWorldPos = worldPosition.xyz;
    outWorldTangent = worldTangent;
    outTangentSign = inTangent.w * orientation;
    outLightmapUv = inLightmapUv * inLightmapScaleBias.xy + inLightmapScaleBias.zw;
    gl_Position = frame.ViewProjection * worldPosition;
}
