#version 450

// Preview backdrop: black with a screen-space grid whose lines glow. Each
// line is a crisp ~1px core plus a gaussian halo; the vignette fades the
// lattice toward the corners so the previewed mesh stays the focus. Color
// arrives LINEAR (the swapchain is sRGB and encodes on write); intensity may
// exceed 1 into the HDR target for a hotter core.
layout(location = 0) in  vec2 vUv;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform BackdropPC {
    vec2  extent;    // target size in pixels
    float cellPx;    // grid cell size in pixels
    float glowPx;    // halo sigma in pixels
    vec4  lineColor; // linear RGB; .a = intensity multiplier
} pc;

void main()
{
    // Center-anchored so the lattice stays symmetric in the box.
    vec2 px = (vUv - 0.5) * pc.extent;
    vec2 toLine = abs(fract(px / pc.cellPx + 0.5) - 0.5) * pc.cellPx;
    float d = min(toLine.x, toLine.y);

    float core = 1.0 - smoothstep(0.0, 1.2, d);
    float halo = exp(-(d * d) / (2.0 * pc.glowPx * pc.glowPx));
    float line = core + 0.4 * halo;

    vec2 ndc = (vUv - 0.5) * 2.0;
    float vignette = max(1.0 - 0.6 * dot(ndc, ndc), 0.0);

    outColor = vec4(pc.lineColor.rgb * (line * pc.lineColor.a * vignette), 1.0);
}
