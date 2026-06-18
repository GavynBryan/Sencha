#version 450

layout(push_constant) uniform GridPC {
    mat4 viewProj;
    vec3 gridCenter;
    float halfExtent;
    vec3 axisU;
    float spacing;
    vec3 axisV;
    float subdivSpacing;
    vec3 cameraPos;
    float fadeEnd;
} pc;

layout(location = 0) in  vec3 fragWorldPos;
layout(location = 0) out vec4 outColor;

// Coverage of the nearest grid line: a smooth ~1px-wide band (smoothstep AA),
// faded out as a cell shrinks toward pixel size so distant lines dissolve cleanly
// instead of aliasing into sparkly noise / moire.
float gridLine(float coord, float spacing)
{
    float fw       = max(fwidth(coord), 1e-6);
    float halfCell = spacing * 0.5;
    float dist     = abs(mod(coord + halfCell, spacing) - halfCell);
    float cov      = 1.0 - smoothstep(0.0, fw, dist);
    // Level-of-detail: fade the line once its cell is only a few pixels wide.
    float cellPx   = spacing / fw;
    cov           *= clamp(cellPx * 0.5 - 0.5, 0.0, 1.0);
    return cov;
}

// Coverage of the world axis at coord == 0 — a smooth ~1px band (no LOD fade; the
// axis line is always meaningful).
float axisLine(float coord)
{
    float fw = max(fwidth(coord), 1e-6);
    return 1.0 - smoothstep(0.0, fw, abs(coord));
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

    // Fade with distance from camera's projection onto the grid plane.
    float dist = length(fragWorldPos - pc.gridCenter);
    float fade = 1.0 - smoothstep(pc.fadeEnd * 0.5, pc.fadeEnd, dist);
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
