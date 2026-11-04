#version 450

#include "tonemap.glsli"

// Takes the scene from scene-referred to display-referred linear light, the only
// place exposure and the shoulder are applied. The target's hardware sRGB write is
// the frame's one encode; only a target with nothing to encode it encodes here.

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uTextures[1024];

layout(constant_id = 0) const bool kEncodeOutput = false;

layout(push_constant) uniform OutputPresentPC {
    uint sceneIndex;
    float exposure;
    float tonemapKnee;
    uint tonemapEnabled;
} pc;

vec3 EncodeSrgb(vec3 linear)
{
    vec3 low = linear * 12.92;
    vec3 high = 1.055 * pow(max(linear, vec3(0.0031308)), vec3(1.0 / 2.4)) - 0.055;
    return mix(high, low, lessThanEqual(linear, vec3(0.0031308)));
}

void main()
{
    vec4 scene = texelFetch(uTextures[pc.sceneIndex], ivec2(gl_FragCoord.xy), 0);
    vec3 display = ApplyOutputTransform(scene.rgb, pc.exposure, pc.tonemapKnee, pc.tonemapEnabled != 0u);
    outColor = vec4(kEncodeOutput ? EncodeSrgb(display) : display, scene.a);
}
