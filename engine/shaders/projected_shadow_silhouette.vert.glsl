#version 450

// Silhouette tile render: the caster's geometry through its light-space
// ortho, nothing else. One MVP per caster; sections share it.
layout(location = 0) in vec3 inPosition;

layout(push_constant) uniform SilhouettePush
{
    mat4 Mvp;
} push;

void main()
{
    gl_Position = push.Mvp * vec4(inPosition, 1.0);
}
