#version 450

// Receiver re-draw: the receiver's own geometry through the view camera,
// carrying world position for the fragment shader to re-project into the
// caster's shadow space. Position math ordering matches mesh_forward
// (ViewProjection * (World * position)) so the depths agree exactly and the
// LESS_OR_EQUAL test lands on the surface instead of sparkling.
layout(location = 0) in vec3 inPosition;

layout(set = 0, binding = 0) uniform ProjectedShadowFrame
{
    mat4 CameraViewProjection;
    mat4 ShadowViewProjection;
    vec4 TileScaleBias;   // silhouette tile rect inside the atlas (uv*xy+zw)
    vec4 Params;          // x darkness, y fade start, z bindless index, w unused
} frame;

layout(push_constant) uniform ProjectPush
{
    mat4 World;
} push;

layout(location = 0) out vec3 outWorldPos;

void main()
{
    vec4 world = push.World * vec4(inPosition, 1.0);
    outWorldPos = world.xyz;
    gl_Position = frame.CameraViewProjection * world;
}
