#version 450
#extension GL_EXT_nonuniform_qualifier : require

// Everything reaching the blend stage here is LINEAR with PREMULTIPLIED alpha,
// which is what the (ONE, ONE_MINUS_SRC_ALPHA) blend the pass sets up expects.
// The swapchain is sRGB and encodes on write, so nothing is converted back.
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

layout(push_constant) uniform UiPC {
    mat4 Mvp;
    uint TextureIndex;
    uint Flags;
} pc;

const uint kFlagHasTexture = 1u;
const uint kFlagStraightAlphaTexture = 2u;

void main()
{
    vec4 texel = vec4(1.0);
    if ((pc.Flags & kFlagHasTexture) != 0u)
    {
        texel = texture(BindlessTextures[nonuniformEXT(pc.TextureIndex)], vUv);
        if ((pc.Flags & kFlagStraightAlphaTexture) != 0u)
            texel.rgb *= texel.a;
    }

    outColor = texel * vColorLinearPremultiplied;
}
