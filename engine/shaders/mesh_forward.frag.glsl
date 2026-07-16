#version 450
#extension GL_GOOGLE_include_directive : require

#include "mesh_frame.glsli"
#include "shadow_sampling.glsli"

layout(constant_id = 0) const bool MATERIAL_UNLIT = false;

layout(location = 0) in vec3 inWorldNormal;
layout(location = 1) in vec2 inUv0;
layout(location = 2) in vec3 inWorldPos;
layout(location = 3) in vec3 inWorldTangent;
layout(location = 4) in float inTangentSign;

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
    uint Pad0;
    uint Pad1;
    uint Pad2;
} pushData;

layout(set = 1, binding = 0) uniform sampler2D BindlessTextures[1024];

layout(location = 0) out vec4 outColor;

vec4 SampleBaseColor()
{
    vec4 color = pushData.BaseColor;
    if (pushData.BaseColorTextureIndex != 0xFFFFFFFFu)
        color *= texture(BindlessTextures[pushData.BaseColorTextureIndex], inUv0);
    return color;
}

vec3 SampleOrm()
{
    if (pushData.OrmTextureIndex == 0xFFFFFFFFu)
        return vec3(1.0, 1.0, 0.0);
    return texture(BindlessTextures[pushData.OrmTextureIndex], inUv0).rgb;
}

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

vec3 ResolveEmission()
{
    vec3 emissiveTexture = vec3(1.0);
    if (pushData.EmissiveTextureIndex != 0xFFFFFFFFu)
        emissiveTexture = texture(BindlessTextures[pushData.EmissiveTextureIndex], inUv0).rgb;
    return pushData.EmissiveFactor.rgb
         * max(pushData.EmissiveFactor.a, 0.0)
         * emissiveTexture;
}

vec3 ApplyShoulder(vec3 color, float knee)
{
    float clampedKnee = clamp(knee, 0.0, 0.999);
    float shoulderRange = 1.0 - clampedKnee;
    vec3 result = color;
    for (int channel = 0; channel < 3; ++channel)
    {
        if (result[channel] <= clampedKnee)
            continue;
        float s = (result[channel] - clampedKnee) / shoulderRange;
        result[channel] = clampedKnee + shoulderRange * s / (1.0 + s);
    }
    return result;
}

vec3 ResolveOutput(vec3 color)
{
    color *= max(frame.StyleParams.z, 0.0);
    if (frame.TonemapEnabled != 0u)
        return ApplyShoulder(color, frame.StyleParams.w);
    return clamp(color, 0.0, 1.0);
}

void main()
{
    vec4 baseColor = SampleBaseColor();
    vec3 emission = ResolveEmission();

    if (MATERIAL_UNLIT)
    {
        outColor = vec4(ResolveOutput(baseColor.rgb + emission), baseColor.a);
        return;
    }

    vec3 orm = SampleOrm();
    vec3 geometricNormal = normalize(inWorldNormal);
    vec3 normal = ResolveWorldNormal();
    vec3 viewDirection = normalize(frame.ViewPositionTime.xyz - inWorldPos);

    float hemi = 0.5 + 0.5 * normal.y;
    vec3 ambientColor = mix(frame.AmbientGround.rgb, frame.AmbientSky.rgb, hemi);
    ambientColor = max(ambientColor, vec3(max(frame.StyleParams.y, 0.0)));
    vec3 lit = baseColor.rgb * ambientColor * clamp(orm.r, 0.0, 1.0);

    float roughness = clamp(pushData.RoughnessFactor * orm.g, 0.0, 1.0);
    float metallic = clamp(pushData.MetallicFactor * orm.b, 0.0, 1.0);
    float specularExponent = exp2(mix(11.0, 1.0, roughness));
    vec3 specularTint = mix(vec3(1.0), baseColor.rgb, metallic);
    float diffuseWrap = max(frame.StyleParams.x, 0.0);

    uint count = min(frame.LightCount, MAX_LIGHTS);
    for (uint i = 0u; i < count; ++i)
    {
        GpuLight light = frame.Lights[i];
        if (light.Type > 1u)
            continue;

        vec3 toLight = light.PositionRange.xyz - inWorldPos;
        float distanceToLight = length(toLight);
        float range = light.PositionRange.w;
        vec3 lightDirection = toLight / max(distanceToLight, 1e-4);

        float coneAttenuation = 1.0;
        if (light.Type == 1u)
        {
            float coneCosine = dot(-lightDirection, light.DirectionCone.xyz);
            coneAttenuation = clamp(
                coneCosine * light.ConeScale + light.ConeOffset, 0.0, 1.0);
            coneAttenuation *= coneAttenuation;
        }

        float window = clamp(1.0 - pow(distanceToLight / max(range, 1e-4), 4.0), 0.0, 1.0);
        float attenuation = coneAttenuation
                          * (window * window)
                          / (distanceToLight * distanceToLight + 1e-4);
        float shadowVisibility = 1.0;
        if (pushData.ReceiveShadows != 0u && light.Type == 1u)
        {
            float filteredVisibility = SampleSpotShadow(
                light.ShadowIndex, inWorldPos, geometricNormal);
            shadowVisibility = mix(
                1.0, filteredVisibility, clamp(frame.ShadowDarkness, 0.0, 1.0));
        }
        vec3 radiance = light.ColorIntensity.rgb * (light.ColorIntensity.w * attenuation);
        radiance *= shadowVisibility;

        float diffuse = clamp((dot(normal, lightDirection) + diffuseWrap)
                              / (1.0 + diffuseWrap), 0.0, 1.0);

        vec3 halfVectorSum = lightDirection + viewDirection;
        vec3 halfVector = dot(halfVectorSum, halfVectorSum) > 1e-8
            ? normalize(halfVectorSum)
            : normal;
        float ndh = max(dot(normal, halfVector), 0.0);
        float specular = ((specularExponent + 8.0) / 8.0)
                       * pow(ndh, specularExponent)
                       * max(pushData.SpecularIntensity, 0.0);

        lit += baseColor.rgb * diffuse * radiance;
        lit += specularTint * specular * radiance;
    }

    outColor = vec4(ResolveOutput(lit + emission), baseColor.a);
}
