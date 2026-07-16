#version 450
#extension GL_GOOGLE_include_directive : require

#include "mesh_frame.glsli"

layout(location = 0) in vec3 inWorldNormal;
layout(location = 1) in vec2 inUv0;
layout(location = 2) in vec3 inWorldPos;
layout(location = 3) in vec3 inWorldTangent;
layout(location = 4) in float inTangentSign;

layout(push_constant) uniform MeshPush
{
    vec4 BaseColor;
    uint BaseColorTextureIndex;
    uint NormalTextureIndex;
    float NormalScale;
    uint Pad0;
} pushData;

layout(set = 1, binding = 0) uniform sampler2D BindlessTextures[1024];

layout(location = 0) out vec4 outColor;

vec3 ResolveWorldNormal()
{
    vec3 normal = normalize(inWorldNormal);
    if (pushData.NormalTextureIndex == 0xFFFFFFFFu)
        return normal;

    vec2 mappedXy = texture(BindlessTextures[pushData.NormalTextureIndex], inUv0).xy * 2.0 - 1.0;
    mappedXy *= pushData.NormalScale;
    float mappedZ = sqrt(max(1.0 - dot(mappedXy, mappedXy), 0.0));

    vec3 tangent = normalize(inWorldTangent);
    vec3 bitangent = normalize(cross(normal, tangent)) * inTangentSign;
    return normalize(mat3(tangent, bitangent, normal) * vec3(mappedXy, mappedZ));
}

void main()
{
    vec4 baseColor = pushData.BaseColor;
    if (pushData.BaseColorTextureIndex != 0xFFFFFFFFu)
    {
        baseColor *= texture(BindlessTextures[pushData.BaseColorTextureIndex], inUv0);
    }

    vec3 N = ResolveWorldNormal();

    // Hemispheric ambient is the no-bake indirect fill. Sky tint applies to
    // upward-facing surfaces and ground tint applies to downward-facing ones.
    float hemi = 0.5 + 0.5 * N.y;
    vec3 lit = baseColor.rgb * mix(frame.AmbientGround.rgb, frame.AmbientSky.rgb, hemi);

    uint count = min(frame.LightCount, MAX_LIGHTS);
    for (uint i = 0u; i < count; ++i)
    {
        GpuLight L = frame.Lights[i];
        if (L.TypeShadow.x == 0u)
        {
            vec3 toLight = L.PositionRange.xyz - inWorldPos;
            float dist = length(toLight);
            float range = L.PositionRange.w;
            vec3 lightDir = toLight / max(dist, 1e-4);
            float ndl = max(dot(N, lightDir), 0.0);

            // Windowed inverse-square attenuation reaches zero at the authored
            // range without adding an infinite tail or a hard edge.
            float window = clamp(1.0 - pow(dist / max(range, 1e-4), 4.0), 0.0, 1.0);
            float attenuation = (window * window) / (dist * dist + 1e-4);

            lit += baseColor.rgb * L.ColorIntensity.rgb * (L.ColorIntensity.w * ndl * attenuation);
        }
    }

    outColor = vec4(lit, baseColor.a);
}
