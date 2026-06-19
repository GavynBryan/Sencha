#version 450

layout(push_constant) uniform GridPC {
    mat4 viewProj;
    vec3 gridCenter;
    float halfExtent;
    vec3 axisU;
    float spacing;
    vec3 axisV;
    float subdivSpacing;
    vec3 gridForward;
    float fadeEnd;
} pc;

layout(location = 0) in  vec3 fragWorldPos;
layout(location = 0) out vec4 outColor;

// Half-line widths in pixels. A bit wider than 1px so the lines read as solid at
// 1 sample (no MSAA) instead of crawling; the axis line is widest since it's the
// most visible. Distances are measured in pixels (world dist / derivative).
const float kGridHalfPx = 1.15;
const float kAxisHalfPx = 1.75;

// Coverage of the nearest grid line: a smooth band whose width is fixed in pixels
// (so it doesn't thin into sparkle at grazing angles). When a cell shrinks past a
// few pixels the line can no longer be resolved, so coverage converges to its duty
// cycle — a flat gray haze — instead of dissolving to black.
float gridLine(float coord, float spacing)
{
    float fw       = max(fwidth(coord), 1e-6);
    float halfCell = spacing * 0.5;
    float dist     = abs(mod(coord + halfCell, spacing) - halfCell);
    float distPx   = dist / fw;
    float cov      = 1.0 - smoothstep(0.0, kGridHalfPx, distPx);

    // Minify toward the line's average coverage (its duty cycle), not toward zero:
    // zeroing collapses fastest where cells compress most — the vanishing point —
    // which pinched the grid into a concave notch. The distance fade then blacks out
    // the resulting haze along a straight, horizon-parallel front.
    float lineWidthPx = 2.0 * kGridHalfPx;                     // full drawn line width
    float cellPx      = spacing / fw;                          // this fragment's cell size
    float duty        = clamp(lineWidthPx / cellPx, 0.0, 1.0); // average coverage when dense
    float minify      = clamp(2.0 / cellPx - 0.5, 0.0, 1.0);   // 0 at 4 px/cell, 1 at 4/3 px
    return mix(cov, duty, minify);
}

// Coverage of the world axis at coord == 0 — a fixed pixel-width band so the long
// thin diagonal reads as a solid line rather than a sparkly thread.
float axisLine(float coord)
{
    float fw     = max(fwidth(coord), 1e-6);
    float distPx = abs(coord) / fw;
    return 1.0 - smoothstep(0.0, kAxisHalfPx, distPx);
}

// Derive a canonical editor axis color from a direction vector.
vec3 axisColor(vec3 axis)
{
    vec3 a = abs(axis);
    if (a.x >= a.y && a.x >= a.z) return vec3(0.85, 0.20, 0.20); // X  red
    if (a.y >= a.x && a.y >= a.z) return vec3(0.20, 0.85, 0.20); // Y  green
    return                                vec3(0.20, 0.20, 0.85); // Z  blue
}

void main()
{
    float u = dot(fragWorldPos, pc.axisU);
    float v = dot(fragWorldPos, pc.axisV);

    // Fade toward the horizon by distance measured ALONG the camera's heading
    // (pc.gridForward = forward projected onto the plane), so the fade front runs
    // parallel to the horizon and the grid reaches a flat edge; radial distance would
    // fade on a circle and read as a concave bowl. In ortho / looking straight down the
    // renderer sends a zero heading, leaving the metric at zero so the grid stays
    // uniform and unfaded — what a top-down view wants.
    float metric = dot(fragWorldPos - pc.gridCenter, pc.gridForward);
    float fade   = 1.0 - smoothstep(pc.fadeEnd * 0.5, pc.fadeEnd, metric);
    if (fade < 0.001) discard;

    // --- line contributions --------------------------------------------------

    float major = max(gridLine(u, pc.spacing),      gridLine(v, pc.spacing));
    float minor = max(gridLine(u, pc.subdivSpacing), gridLine(v, pc.subdivSpacing));

    // Axis lines: the line where u == 0 runs along pc.axisV; where v == 0 along pc.axisU.
    float axisUStrength = axisLine(u); // u=0 line
    float axisVStrength = axisLine(v); // v=0 line

    // --- color and alpha -----------------------------------------------------

    const vec3 minorColor = vec3(0.30);
    const vec3 majorColor = vec3(0.52);
    // axisU direction's color = color of axis that runs along axisU (the v=0 line)
    vec3 axisULineColor = axisColor(pc.axisU);
    // axisV direction's color = color of axis that runs along axisV (the u=0 line)
    vec3 axisVLineColor = axisColor(pc.axisV);

    vec3 color = vec3(0.0);
    float alpha = 0.0;

    // Layer from weakest to strongest so stronger contributions override.
    if (minor > 0.0) {
        float a = minor * 0.14 * fade;
        color = mix(color, minorColor, minor);
        alpha = max(alpha, a);
    }
    if (major > 0.0) {
        float a = major * 0.32 * fade;
        color = mix(color, majorColor, major);
        alpha = max(alpha, a);
    }
    if (axisVStrength > 0.0) {          // v=0 line — colored by axisU direction
        float a = axisVStrength * 0.85 * fade;
        color = mix(color, axisULineColor, axisVStrength);
        alpha = max(alpha, a);
    }
    if (axisUStrength > 0.0) {          // u=0 line — colored by axisV direction
        float a = axisUStrength * 0.85 * fade;
        color = mix(color, axisVLineColor, axisUStrength);
        alpha = max(alpha, a);
    }

    if (alpha < 0.005) discard;
    outColor = vec4(color, alpha);
}
