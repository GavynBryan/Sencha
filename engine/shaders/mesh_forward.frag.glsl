#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require

#include "mesh_frame.glsli"
#include "shadow_sampling.glsli"
#include "probe_sampling.glsli"
#include "tonemap.glsli"
#include "mesh_material.glsli"
#include "lighting.glsli"

layout(constant_id = 0) const bool MATERIAL_UNLIT = false;
// Alpha masking is a pipeline variant rather than a branch every material pays
// for: a fragment shader that can discard loses early depth testing, and an
// opaque scene should not give that up to serve the masked materials in it.
layout(constant_id = 1) const bool MATERIAL_ALPHA_MASK = false;

layout(location = 0) out vec4 outColor;

void main()
{
    vec4 baseColor = SampleBaseColor();
    if (MATERIAL_ALPHA_MASK && baseColor.a < pushData.AlphaCutoff)
        discard;
    vec3 emission = ResolveEmission();

    if (MATERIAL_UNLIT)
    {
        outColor = vec4(ResolveOutput(baseColor.rgb + emission), baseColor.a);
        return;
    }

    vec3 orm = SampleOrm();
    vec3 geometricNormal = normalize(inWorldNormal);
    vec3 normal = ResolveWorldNormal(geometricNormal);
    vec3 viewDirection = normalize(frame.ViewPositionTime.xyz - inWorldPos);

    float hemi = 0.5 + 0.5 * normal.y;
    vec3 ambientColor = mix(frame.AmbientGround.rgb, frame.AmbientSky.rgb, hemi);
    ambientColor = SampleProbeAmbient(inWorldPos, normal, ambientColor);
    ambientColor = max(ambientColor, vec3(max(frame.StyleParams.y, 0.0)));
    // Baked AO joins the material's own occlusion channel on the ambient
    // term only; direct light, baked direct, and emission stay untouched.
    vec3 lit = baseColor.rgb * ambientColor * clamp(orm.r, 0.0, 1.0)
        * SampleBakedAo();

    float roughness = clamp(pushData.RoughnessFactor * orm.g, 0.0, 1.0);
    float metallic = clamp(pushData.MetallicFactor * orm.b, 0.0, 1.0);
    float specularExponent = exp2(mix(11.0, 1.0, roughness));
    vec3 specularTint = mix(vec3(1.0), baseColor.rgb, metallic);
    float diffuseWrap = max(frame.StyleParams.x, 0.0);

    bool chartedReceiver = pushData.LightmapTextureIndex != 0xFFFFFFFFu;
    uint count = min(frame.LightCount, MAX_LIGHTS);
    for (uint i = 0u; i < count; ++i)
    {
        GpuLight light = frame.Lights[i];
        // A charted receiver's copy of a baked light is already in its
        // lightmap; skipping the live light here is what keeps the term
        // single-counted. Everything else receives baked lights live.
        if ((light.Type & LIGHT_BAKED_BIT) != 0u && chartedReceiver)
            continue;
        light.Type &= LIGHT_TYPE_MASK;
        if (light.Type > 1u)
            continue;

        // Past its range a light contributes exactly zero: the r^4 window
        // clamps to 0, which zeroes Radiance and so both the diffuse and the
        // specular term. Cull here so an unreachable light never pays for the
        // shadow filter's texture taps. The epsilon matches the window's own
        // denominator so a sub-epsilon range is not culled early.
        vec3 toLight = light.PositionRange.xyz - inWorldPos;
        float lightRange = max(light.PositionRange.w, 1e-4);
        if (dot(toLight, toLight) >= lightRange * lightRange)
            continue;

        float shadowVisibility = ResolveFilteredShadowVisibility(
            light, inWorldPos, geometricNormal);
        DirectLightTerms terms = EvaluateDirectLight(
            light, inWorldPos, normal, viewDirection, diffuseWrap,
            specularExponent, shadowVisibility);

        lit += baseColor.rgb * terms.Diffuse * terms.Radiance;
        lit += specularTint * terms.Specular * terms.Radiance;
    }

    // Baked static direct diffuse (diffuse only, no specular). The lights
    // that fed this term are skipped by the loop above on charted receivers,
    // so it does not double-count. Zero on unbaked meshes.
    if (frame.BakedDirectEnabled != 0u)
        lit += baseColor.rgb * SampleBakedDirect();

    outColor = vec4(ResolveOutput(lit + emission), baseColor.a);
}
