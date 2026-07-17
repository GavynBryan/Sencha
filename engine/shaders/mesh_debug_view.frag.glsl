#version 450
#extension GL_GOOGLE_include_directive : require

#define SENCHA_DEBUG_VIEWS 1
#include "mesh_frame.glsli"
#include "shadow_sampling.glsli"
#include "mesh_material.glsli"
#include "lighting.glsli"

layout(location = 0) out vec4 outColor;

const uint DEBUG_WORLD_NORMALS = 1u;
const uint DEBUG_NORMAL_MAP = 2u;
const uint DEBUG_NORMAL_DELTA = 3u;
const uint DEBUG_DIFFUSE = 4u;
const uint DEBUG_SPECULAR = 5u;
const uint DEBUG_EMISSION = 6u;
const uint DEBUG_ROUGHNESS = 7u;
const uint DEBUG_LIGHT_COMPLEXITY = 8u;
const uint DEBUG_SHADOW_FILTERED = 9u;
const uint DEBUG_SHADOW_RAW = 10u;
const uint DEBUG_OVERDRAW = 11u;

vec3 HeatColor(float value)
{
    float t = clamp(value, 0.0, 1.0);
    return clamp(vec3(1.5 * t, 1.5 - abs(3.0 * t - 1.5), 1.5 * (1.0 - t)),
                 0.0, 1.0);
}

void main()
{
    vec4 baseColor = SampleBaseColor();
    vec3 orm = SampleOrm();
    vec3 geometricNormal = normalize(inWorldNormal);
    vec3 normal = ResolveWorldNormal();

    if (frame.DebugView == DEBUG_WORLD_NORMALS)
    {
        outColor = vec4(normal * 0.5 + 0.5, 1.0);
        return;
    }
    if (frame.DebugView == DEBUG_NORMAL_MAP)
    {
        outColor = vec4(SampleTangentNormal() * 0.5 + 0.5, 1.0);
        return;
    }
    if (frame.DebugView == DEBUG_NORMAL_DELTA)
    {
        float delta = acos(clamp(dot(geometricNormal, normal), -1.0, 1.0))
                    * (2.0 / 3.14159265);
        outColor = vec4(HeatColor(delta), 1.0);
        return;
    }
    if (frame.DebugView == DEBUG_EMISSION)
    {
        outColor = vec4(ResolveOutput(ResolveEmission()), 1.0);
        return;
    }
    if (frame.DebugView == DEBUG_ROUGHNESS)
    {
        float roughness = clamp(pushData.RoughnessFactor * orm.g, 0.0, 1.0);
        float exponent = exp2(mix(11.0, 1.0, roughness));
        outColor = vec4(roughness, log2(exponent) / 11.0, 1.0 - roughness, 1.0);
        return;
    }
    if (frame.DebugView == DEBUG_OVERDRAW)
    {
        outColor = vec4(0.045, 0.012, 0.003, 1.0);
        return;
    }

    vec3 viewDirection = normalize(frame.ViewPositionTime.xyz - inWorldPos);
    float roughness = clamp(pushData.RoughnessFactor * orm.g, 0.0, 1.0);
    float metallic = clamp(pushData.MetallicFactor * orm.b, 0.0, 1.0);
    float specularExponent = exp2(mix(11.0, 1.0, roughness));
    vec3 specularTint = mix(vec3(1.0), baseColor.rgb, metallic);
    float diffuseWrap = max(frame.StyleParams.x, 0.0);

    vec3 diffuseOnly = vec3(0.0);
    vec3 specularOnly = vec3(0.0);
    uint affectingLights = 0u;
    float filteredShadow = 1.0;
    float rawShadow = 1.0;
    bool hasShadow = false;

    uint count = min(frame.LightCount, MAX_LIGHTS);
    for (uint i = 0u; i < count; ++i)
    {
        GpuLight light = frame.Lights[i];
        if (light.Type > 1u)
            continue;

        float filtered = ResolveFilteredShadowVisibility(
            light, inWorldPos, geometricNormal);
        DirectLightTerms terms = EvaluateDirectLight(
            light, inWorldPos, normal, viewDirection, diffuseWrap,
            specularExponent, filtered);
        if (terms.Attenuation > 1e-5)
            ++affectingLights;

        diffuseOnly += baseColor.rgb * terms.Diffuse * terms.Radiance;
        specularOnly += specularTint * terms.Specular * terms.Radiance;

        bool validShadow = light.Type == 0u
            ? light.ShadowIndex < frame.PointShadowCount
            : light.ShadowIndex < frame.SpotShadowCount;
        if (pushData.ReceiveShadows != 0u
            && validShadow && terms.Attenuation > 1e-5)
        {
            hasShadow = true;
            filteredShadow = min(filteredShadow, filtered);
            float raw = light.Type == 0u
                ? SamplePointShadowRaw(light.ShadowIndex, inWorldPos, geometricNormal)
                : SampleSpotShadowRaw(light.ShadowIndex, inWorldPos, geometricNormal);
            rawShadow = min(rawShadow, mix(
                1.0, raw, clamp(frame.ShadowDarkness, 0.0, 1.0)));
        }
    }

    if (frame.DebugView == DEBUG_DIFFUSE)
        outColor = vec4(ResolveOutput(diffuseOnly), 1.0);
    else if (frame.DebugView == DEBUG_SPECULAR)
        outColor = vec4(ResolveOutput(specularOnly), 1.0);
    else if (frame.DebugView == DEBUG_LIGHT_COMPLEXITY)
        outColor = vec4(HeatColor(float(affectingLights) / 8.0), 1.0);
    else if (frame.DebugView == DEBUG_SHADOW_FILTERED)
        outColor = vec4(vec3(hasShadow ? filteredShadow : 1.0), 1.0);
    else if (frame.DebugView == DEBUG_SHADOW_RAW)
        outColor = vec4(vec3(hasShadow ? rawShadow : 1.0), 1.0);
    else
        outColor = vec4(1.0, 0.0, 1.0, 1.0);
}
