#version 450
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_GOOGLE_include_directive : require

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

layout(set = 1, binding = 0) uniform sampler2D BindlessTextures[1024];

layout(location = 0) in vec2 vUv;
layout(location = 1) in vec4 vColorLinearPremultiplied;

layout(location = 0) out vec4 outColor;

#include "ui.glsli"

const float kPi = 3.14159265358979;

float GradientPosition()
{
    vec2 offset = vUv - pc.GradientOrigin;
    switch ((pc.Flags >> kGradientShapeShift) & kGradientShapeMask)
    {
    case kGradientShapeRadial:
        return length(pc.GradientAxis * offset);
    case kGradientShapeConic:
    {
        mat2 turn = mat2(pc.GradientAxis.x, -pc.GradientAxis.y, pc.GradientAxis.y, pc.GradientAxis.x);
        vec2 turned = turn * offset;
        return 0.5 + atan(-turned.x, turned.y) / (2.0 * kPi);
    }
    default:
    {
        float length2 = dot(pc.GradientAxis, pc.GradientAxis);
        return length2 > 0.0 ? dot(pc.GradientAxis, offset) / length2 : 0.0;
    }
    }
}

float RampPosition(float gradientPosition)
{
    float first = pc.GradientStops.x;
    float span = pc.GradientStops.y - first;
    if (span <= 0.0)
        return gradientPosition < first ? 0.0 : 1.0;
    float along = (gradientPosition - first) / span;
    return (pc.Flags & kFlagGradientRepeating) != 0u ? fract(along) : clamp(along, 0.0, 1.0);
}

vec4 SampleRampBetweenEndTexelCentres(float rampPosition)
{
    float texels = float(textureSize(BindlessTextures[nonuniformEXT(pc.TextureIndex)], 0).x);
    float u = (rampPosition * (texels - 1.0) + 0.5) / texels;
    return texture(BindlessTextures[nonuniformEXT(pc.TextureIndex)], vec2(u, 0.5));
}

void main()
{
    if ((pc.Flags & kFlagGradient) != 0u)
    {
        vec4 ramp = SampleRampBetweenEndTexelCentres(RampPosition(GradientPosition()));
        outColor = PremultipliedSrgbToLinear(ramp) * vColorLinearPremultiplied;
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
