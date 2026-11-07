#version 450
#extension GL_GOOGLE_include_directive : require

#include "mesh_view.glsli"
#include "mesh_position.glsli"

// Only what the mask reads: the position, and the UV an alpha cutout samples.

layout(location = 2) in vec2 inUv0;

layout(location = 1) out vec2 outUv0;

void main()
{
    outUv0 = inUv0;
    gl_Position = MeshClipPosition(InstanceWorld() * vec4(inPosition, 1.0));
}
