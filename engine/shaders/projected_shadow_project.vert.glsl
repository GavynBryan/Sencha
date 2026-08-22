#version 450

// Receiver re-draw: the receiver's own geometry through the view camera,
// carrying world position and normal for the fragment shader. Position math
// ordering matches mesh_forward (ViewProjection * (World * position)) so
// the depths agree exactly and the LESS_OR_EQUAL test lands on the surface
// instead of sparkling. The normal uses the cofactor construction from
// mesh_forward.vert so non-uniform receiver scale cannot bend the facing
// test.
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;

layout(set = 0, binding = 0) uniform ProjectedShadowFrame
{
    mat4 CameraViewProjection;
    mat4 ShadowViewProjection;
    vec4 TileScaleBias;   // silhouette tile rect inside the atlas (uv*xy+zw)
    vec4 Params;          // x occluder index, y fade start, z silhouette index
    vec4 DirectionBias;   // xyz shadow direction, w normalized depth bias
} frame;

layout(push_constant) uniform ProjectPush
{
    mat4 World;
} push;

layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec3 outWorldNormal;

void main()
{
    vec4 world = push.World * vec4(inPosition, 1.0);
    outWorldPos = world.xyz;

    mat3 linear = mat3(push.World);
    vec3 cofactor0 = cross(linear[1], linear[2]);
    vec3 cofactor1 = cross(linear[2], linear[0]);
    vec3 cofactor2 = cross(linear[0], linear[1]);
    float orientation = dot(linear[0], cofactor0) < 0.0 ? -1.0 : 1.0;
    outWorldNormal =
        normalize(mat3(cofactor0, cofactor1, cofactor2) * orientation * inNormal);

    gl_Position = frame.CameraViewProjection * world;
}
