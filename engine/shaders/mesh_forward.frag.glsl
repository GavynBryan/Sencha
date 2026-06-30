#version 450

// Keep in sync with kMaxForwardLights in engine/include/render/RenderLight.h.
const uint MAX_LIGHTS = 64u;

struct GpuLight
{
    vec4 PositionRange;  // xyz world position, w range
    vec4 DirectionCone;  // xyz direction, w cos(outer) - reserved
    vec4 ColorIntensity; // rgb linear color, w intensity
    uvec4 TypeShadow;    // x type, y shadow index (reserved), zw pad
};

layout(location = 0) in vec3 inWorldNormal;
layout(location = 1) in vec2 inUv0;
layout(location = 2) in vec3 inWorldPos;

layout(set = 0, binding = 0) uniform MeshFrame
{
    mat4 ViewProjection;
    vec4 ViewPositionTime;
    vec4 AmbientSky;
    vec4 AmbientGround;
    uint LightCount;
    uint Pad0;
    uint Pad1;
    uint Pad2;
    GpuLight Lights[MAX_LIGHTS];
} frame;

layout(push_constant) uniform MeshPush
{
    mat4 World;
    vec4 BaseColor;
    uint BaseColorTextureIndex;
} pushData;

layout(set = 1, binding = 0) uniform sampler2D BindlessTextures[1024];

layout(location = 0) out vec4 outColor;

void main()
{
    vec4 baseColor = pushData.BaseColor;
    if (pushData.BaseColorTextureIndex != 0xFFFFFFFFu)
    {
        baseColor *= texture(BindlessTextures[pushData.BaseColorTextureIndex], inUv0);
    }

    vec3 N = normalize(inWorldNormal);

    // Hemispheric ambient: the cheap no-bake indirect fill. Sky tint when the
    // surface faces up, ground tint when it faces down.
    float hemi = 0.5 + 0.5 * N.y;
    vec3 lit = baseColor.rgb * mix(frame.AmbientGround.rgb, frame.AmbientSky.rgb, hemi);

    uint count = min(frame.LightCount, MAX_LIGHTS);
    for (uint i = 0u; i < count; ++i)
    {
        GpuLight L = frame.Lights[i];
        if (L.TypeShadow.x == 0u) // POINT (switch grows for spot/directional)
        {
            vec3 toLight = L.PositionRange.xyz - inWorldPos;
            float dist = length(toLight);
            float range = L.PositionRange.w;
            vec3 lightDir = toLight / max(dist, 1e-4);
            float ndl = max(dot(N, lightDir), 0.0);

            // Windowed inverse-square: physical 1/d^2 falloff multiplied by a
            // smooth window that reaches zero at the range, so a light stays
            // local (no infinite tail) without a hard popping edge.
            float window = clamp(1.0 - pow(dist / max(range, 1e-4), 4.0), 0.0, 1.0);
            float attenuation = (window * window) / (dist * dist + 1e-4);

            lit += baseColor.rgb * L.ColorIntensity.rgb * (L.ColorIntensity.w * ndl * attenuation);
        }
    }

    outColor = vec4(lit, baseColor.a);
}
