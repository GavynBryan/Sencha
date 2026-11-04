#version 450

// Writes the finished scene into the swapchain. The target's hardware sRGB write
// is the frame's one encode; only a target without one encodes here, so an _SRGB
// swapchain never sees this shader's curve.

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uTextures[1024];

layout(constant_id = 0) const bool kEncodeOutput = false;

layout(push_constant) uniform OutputPresentPC {
    uint sceneIndex;
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
    outColor = vec4(kEncodeOutput ? EncodeSrgb(scene.rgb) : scene.rgb, scene.a);
}
