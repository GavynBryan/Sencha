#version 450

layout(push_constant) uniform GridPC {
    mat4 viewProj;
    vec3 gridCenter;
    float halfExtent;
    vec3 axisU;
    float spacing;       // base grid unit (the snap unit): the finest level drawn
    vec3 axisV;
    float subdivSpacing; // unused by the adaptive grid; kept for push-constant layout
    vec3 gridForward;
    float fadeEnd;
    vec3 gridOrigin;     // grid frame origin: lattice phase + axis-line anchor
    float pad0;
    vec4 style;          // x = cell px (density), y = opacity, z = brightness, w = fade start fraction
} pc;

layout(location = 0) in  vec3 fragWorldPos;
layout(location = 0) out vec4 outColor;

// Adaptive grid by LOD cross-fade (continuous across level steps, no hard seam). The
// look knobs (density, opacity, brightness, fade start) come from pc.style, driven by
// editor.grid.* cvars so they can be dialed live in the dev console.
const float kBase       = 10.0;  // decade step
const float kLineHalfPx = 1.1;   // grid line half-width (px); solid at 1 sample (no MSAA)
const float kAxisHalfPx = 1.7;   // axis line half-width (px)

float lineCov(float coord, float period, float fw, float halfPx)
{
    float halfP = period * 0.5;
    float d     = abs(mod(coord + halfP, period) - halfP);
    return 1.0 - smoothstep(0.0, halfPx, d / fw);
}

float axisCov(float coord, float fw)
{
    return 1.0 - smoothstep(0.0, kAxisHalfPx, abs(coord) / fw);
}

vec3 axisColor(vec3 axis)
{
    vec3 a = abs(axis);
    if (a.x >= a.y && a.x >= a.z) return vec3(0.85, 0.20, 0.20); // X  red
    if (a.y >= a.x && a.y >= a.z) return vec3(0.20, 0.85, 0.20); // Y  green
    return                               vec3(0.20, 0.20, 0.85); // Z  blue
}

void main()
{
    // Coordinates relative to the grid frame origin: the lattice phase and the
    // colored axis lines follow the frame, so moving/rotating the working grid
    // is visible (the axis cross marks the origin).
    float u = dot(fragWorldPos - pc.gridOrigin, pc.axisU);
    float v = dot(fragWorldPos - pc.gridOrigin, pc.axisV);
    float fwu = max(fwidth(u), 1e-8);
    float fwv = max(fwidth(v), 1e-8);
    float fw  = max(fwu, fwv);

    // style.w (fade_start) is a signed fraction of the reach: negative biases the fade
    // to begin near/before the camera for a gentle global falloff (the useful range at
    // grazing angles). Clamp the near edge below the far edge so the smoothstep can
    // never invert (which would blank the whole grid).
    float metric   = dot(fragWorldPos - pc.gridCenter, pc.gridForward);
    float fadeNear = min(pc.fadeEnd * pc.style.w, pc.fadeEnd - 1.0);
    float fade     = 1.0 - smoothstep(fadeNear, pc.fadeEnd, metric);
    if (fade < 0.001)
        discard;

    // Continuous decade level (clamped so the grid is never finer than the snap unit),
    // sized to the cvar-driven target cell pixel size.
    float cellPx = max(pc.style.x, 1.0);
    float lod = max(log2(cellPx * fw / pc.spacing) / log2(kBase), 0.0);
    float f   = fract(lod);
    float lv  = floor(lod);

    float p0 = pc.spacing * pow(kBase, lv);  // finest decade
    float p1 = p0 * kBase;                    // 10x
    float p2 = p1 * kBase;                     // 100x

    float c0 = max(lineCov(u, p0, fwu, kLineHalfPx), lineCov(v, p0, fwv, kLineHalfPx));
    float c1 = max(lineCov(u, p1, fwu, kLineHalfPx), lineCov(v, p1, fwv, kLineHalfPx));
    float c2 = max(lineCov(u, p2, fwu, kLineHalfPx), lineCov(v, p2, fwv, kLineHalfPx));

    // Cross-fade: finest out (1-f), middle full (1), coarsest in (f). Continuous across
    // each level step, so no line changes role at a boundary (no hard seam).
    float g = max(max(c0 * (1.0 - f), c1), c2 * f);

    vec3 color = vec3(pc.style.z);
    float alpha = g * pc.style.y;

    float axisU0 = axisCov(u, fwu); // u==0 line, runs along axisV
    float axisV0 = axisCov(v, fwv); // v==0 line, runs along axisU
    if (axisV0 > 0.0) { color = mix(color, axisColor(pc.axisU), axisV0); alpha = max(alpha, axisV0 * 0.95); }
    if (axisU0 > 0.0) { color = mix(color, axisColor(pc.axisV), axisU0); alpha = max(alpha, axisU0 * 0.95); }

    alpha *= fade;
    if (alpha < 0.005)
        discard;
    outColor = vec4(color, alpha);
}
