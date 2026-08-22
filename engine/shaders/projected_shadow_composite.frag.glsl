#version 450

// Applies the frame's projected-shadow mask to the scene in one multiply:
// scene *= 1 - darkness * mask. Overlap already resolved to the union when
// the mask was written (blend op MAX), so darkness lands exactly once no
// matter how many casters cover a pixel. UvScale maps view UVs into the
// mask, which may be larger than the view (the editor's shared mask target
// is sized to its largest viewport and every view renders at the origin --
// a scale, never an offset).
layout(location = 0) in vec2 vUv;

layout(set = 1, binding = 0) uniform sampler2D BindlessTextures[1024];

layout(push_constant) uniform CompositePush
{
    vec2 UvScale;
    float Darkness;
    uint MaskIndex;
} push;

layout(location = 0) out vec4 outColor;

void main()
{
    float amount = texture(BindlessTextures[push.MaskIndex], vUv * push.UvScale).r;
    outColor = vec4(vec3(1.0 - push.Darkness * amount), 1.0);
}
