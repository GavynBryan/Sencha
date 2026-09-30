#version 450

// A near-black vertical gradient that replaces the engine's clear color inside each
// editor viewport, giving the dark "analog scope" look. Colors arrive in LINEAR
// space (the swapchain is sRGB and re-encodes on write); the C++ side picks them per
// viewport mode (a faint top->bottom lift for perspective, near-flat for ortho).
layout(location = 0) in  vec2 vUv;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform BackdropPC {
    vec4 topColor; // linear RGB at the top edge
    vec4 botColor; // linear RGB at the bottom edge
} pc;

void main()
{
    float t = clamp(vUv.y, 0.0, 1.0);
    outColor = vec4(mix(pc.topColor.rgb, pc.botColor.rgb, t), 1.0);
}
