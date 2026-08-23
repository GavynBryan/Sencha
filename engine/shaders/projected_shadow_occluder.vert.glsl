#version 450

// Occluder tile render: a receiver's geometry through the caster's
// light-space ortho, position only. One MVP per draw, precomputed by the
// assembler.
layout(location = 0) in vec3 inPosition;

layout(push_constant) uniform OccluderPush
{
    mat4 Mvp;
} push;

void main()
{
    gl_Position = push.Mvp * vec4(inPosition, 1.0);
}
