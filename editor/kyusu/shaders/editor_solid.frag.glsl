#version 450

// One texture tile == one UV unit. The checker makes the projection legible:
// when a brush is resized the squares stay the same world size (texture lock),
// when UV scale changes they grow/shrink uniformly. Colors are LINEAR — the
// swapchain is sRGB and encodes on write (matches editor_line). See memory
// editor-srgb-gamma.

layout(location = 0) in vec3 InNormal;
layout(location = 1) in vec2 InUv;
layout(location = 2) in vec4 InTint;

layout(location = 0) out vec4 OutFragColor;

void main()
{
    vec2 cell = floor(InUv);
    float checker = mod(cell.x + cell.y, 2.0);
    vec3 base = mix(vec3(0.26), vec3(0.52), checker) * InTint.rgb;

    vec3 n = normalize(InNormal);
    vec3 lightDir = normalize(vec3(0.4, 0.9, 0.6));
    float diffuse = max(dot(n, lightDir), 0.0);
    float light = 0.4 + 0.6 * diffuse;

    OutFragColor = vec4(base * light, 1.0);
}
