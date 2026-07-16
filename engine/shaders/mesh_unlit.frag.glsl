#version 450
#extension GL_GOOGLE_include_directive : require

#include "mesh_frame.glsli"

layout(location = 1) in vec2 inUv0;

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
} pushData;

layout(set = 1, binding = 0) uniform sampler2D BindlessTextures[1024];

layout(location = 0) out vec4 outColor;

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

void main()
{
    vec4 baseColor = pushData.BaseColor;
    if (pushData.BaseColorTextureIndex != 0xFFFFFFFFu)
        baseColor *= texture(BindlessTextures[pushData.BaseColorTextureIndex], inUv0);

    vec3 emissiveTexture = vec3(1.0);
    if (pushData.EmissiveTextureIndex != 0xFFFFFFFFu)
        emissiveTexture = texture(BindlessTextures[pushData.EmissiveTextureIndex], inUv0).rgb;

    vec3 emission = pushData.EmissiveFactor.rgb
                  * max(pushData.EmissiveFactor.a, 0.0)
                  * emissiveTexture;
    vec3 color = (baseColor.rgb + emission) * max(frame.StyleParams.z, 0.0);
    if (frame.TonemapEnabled != 0u)
        color = ApplyShoulder(color, frame.StyleParams.w);
    else
        color = clamp(color, 0.0, 1.0);

    outColor = vec4(color, baseColor.a);
}
