#version 450

#include "display_encode.glsli"
#include "tonemap.glsli"

// Takes the scene from scene-referred to display-referred linear light, the only
// place exposure and the shoulder are applied. The target's hardware sRGB write is the
// frame's one encode; only a target with nothing to encode it encodes here.

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uTextures[1024];

layout(constant_id = 0) const bool kEncodeOutput = false;

layout(push_constant) uniform OutputPresentPC {
    uint sceneIndex;
    float exposure;
    float tonemapKnee;
    uint tonemapEnabled;
} pc;

void main()
{
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    vec4 scene = texelFetch(uTextures[pc.sceneIndex], pixel, 0);
    vec3 display = ApplyOutputTransform(scene.rgb, pc.exposure, pc.tonemapKnee, pc.tonemapEnabled != 0u);
    outColor = vec4(kEncodeOutput ? EncodeSrgb(display) : display, scene.a);
}
