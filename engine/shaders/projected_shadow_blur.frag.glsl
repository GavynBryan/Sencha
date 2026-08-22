#version 450

// One axis of the separable silhouette blur. Binary coverage plus a single
// bilinear tap magnifies into stair-steps on close receivers; blurring the
// tile itself turns the mask into a soft gradient whose penumbra magnifies
// smoothly at any projection scale, and costs atlas texels once per frame
// instead of shadowed screen pixels. 7-tap binomial: the tails are 1/64, so
// reach beyond the tile's fitted padding is invisible before it can read a
// neighbouring tile.
layout(location = 0) in vec2 vUv;

layout(set = 1, binding = 0) uniform sampler2D BindlessTextures[1024];

layout(push_constant) uniform BlurPush
{
    vec2 StepUv;       // one tap's offset, softness/3 texels along one axis
    uint SourceIndex;  // bindless slot of the pass being blurred
    float Pad;
} push;

layout(location = 0) out float outMask;

void main()
{
    const float weights[4] =
        float[](20.0 / 64.0, 15.0 / 64.0, 6.0 / 64.0, 1.0 / 64.0);
    float sum = texture(BindlessTextures[push.SourceIndex], vUv).r * weights[0];
    for (int tap = 1; tap <= 3; ++tap)
    {
        vec2 offset = push.StepUv * float(tap);
        sum += texture(BindlessTextures[push.SourceIndex], vUv + offset).r * weights[tap];
        sum += texture(BindlessTextures[push.SourceIndex], vUv - offset).r * weights[tap];
    }
    outMask = sum;
}
