#version 450
#extension GL_EXT_nonuniform_qualifier : require

// Everything reaching the blend stage here is LINEAR with PREMULTIPLIED alpha,
// which is what the (ONE, ONE_MINUS_SRC_ALPHA) blend the pass sets up expects.
// The display target encodes on write, so nothing is converted back.
//
// Two texture classes arrive differently and are reconciled here:
//
//   - A generated texture (a glyph atlas, a decorator image) was converted once
//     on the CPU at upload and is already linear premultiplied. Sampled as-is.
//   - A content texture is an ordinary asset sampled through an sRGB view, so
//     the hardware decodes it to linear but leaves it straight-alpha. It is
//     premultiplied here, once, rather than at upload -- an asset is shared with
//     the rest of the renderer and does not get a UI-specific copy.
//
// A gradient samples its colour ramp instead: premultiplied sRGB, decoded here,
// at the point along the gradient the fragment's UV (its position in the
// element's box, in pixels) falls. The shape follows the document engine's own
// reference backend, so a gradient draws as RmlUi means it to.

layout(set = 1, binding = 0) uniform sampler2D BindlessTextures[1024];

layout(location = 0) in vec2 vUv;
layout(location = 1) in vec4 vColorLinearPremultiplied;

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform UiPC {
    mat4 Mvp;
    uint TextureIndex;
    uint Flags;
    // Linear: start point and the vector to the end. Radial: centre and one
    // over the radius on each axis. Conic: centre and the start angle's
    // cosine and sine. Stops: where the first and last colour stops sit.
    vec2 GradientOrigin;
    vec2 GradientAxis;
    vec2 GradientStops;
} pc;

const uint kFlagHasTexture = 1u;
const uint kFlagStraightAlphaTexture = 2u;
const uint kFlagGradient = 4u;
const uint kFlagGradientRadial = 8u;
const uint kFlagGradientConic = 16u;
const uint kFlagGradientRepeating = 32u;
// Mirrors kUiGradientRampTexels (render/ui/UiDrawFrame.h).
const float kRampTexels = 512.0;
const float kPi = 3.14159265358979;

vec3 SrgbToLinear(vec3 srgb)
{
    vec3 low = srgb / 12.92;
    vec3 high = pow((srgb + 0.055) / 1.055, vec3(2.4));
    return mix(low, high, step(vec3(0.04045), srgb));
}

vec4 GradientColor()
{
    vec2 offset = vUv - pc.GradientOrigin;
    float t;
    if ((pc.Flags & kFlagGradientRadial) != 0u)
    {
        t = length(pc.GradientAxis * offset);
    }
    else if ((pc.Flags & kFlagGradientConic) != 0u)
    {
        mat2 turn = mat2(pc.GradientAxis.x, -pc.GradientAxis.y, pc.GradientAxis.y, pc.GradientAxis.x);
        vec2 v = turn * offset;
        t = 0.5 + atan(-v.x, v.y) / (2.0 * kPi);
    }
    else
    {
        float length2 = dot(pc.GradientAxis, pc.GradientAxis);
        t = length2 > 0.0 ? dot(pc.GradientAxis, offset) / length2 : 0.0;
    }

    // Where t falls on the ramp, 0 at the first stop and 1 at the last.
    float span = pc.GradientStops.y - pc.GradientStops.x;
    float along;
    if (span <= 0.0)
        along = t < pc.GradientStops.x ? 0.0 : 1.0;
    else if ((pc.Flags & kFlagGradientRepeating) != 0u)
        along = fract((t - pc.GradientStops.x) / span);
    else
        along = clamp((t - pc.GradientStops.x) / span, 0.0, 1.0);

    // Between the first and last texel centres, so filtering never reaches
    // past either end, whatever the sampler's address mode.
    float u = (along * (kRampTexels - 1.0) + 0.5) / kRampTexels;
    vec4 srgb = texture(BindlessTextures[nonuniformEXT(pc.TextureIndex)], vec2(u, 0.5));
    vec3 straight = srgb.a > 0.0 ? srgb.rgb / srgb.a : vec3(0.0);
    return vec4(SrgbToLinear(straight) * srgb.a, srgb.a);
}

void main()
{
    if ((pc.Flags & kFlagGradient) != 0u)
    {
        // The vertex colour carries the element's opacity.
        outColor = GradientColor() * vColorLinearPremultiplied;
        return;
    }

    vec4 texel = vec4(1.0);
    if ((pc.Flags & kFlagHasTexture) != 0u)
    {
        texel = texture(BindlessTextures[nonuniformEXT(pc.TextureIndex)], vUv);
        if ((pc.Flags & kFlagStraightAlphaTexture) != 0u)
            texel.rgb *= texel.a;
    }

    outColor = texel * vColorLinearPremultiplied;
}
