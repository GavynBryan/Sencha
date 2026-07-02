#version 450

// Solid brush preview: world-space lit checker driven by the per-face UV
// projection, so resizing a brush visibly keeps texel density constant.

layout(push_constant) uniform PushConstants {
    mat4 ViewProjection;
} Push;

layout(location = 0) in vec3 InPosition; // world space
layout(location = 1) in vec3 InNormal;   // world space
layout(location = 2) in vec2 InUv;       // computed from the UV projection
layout(location = 3) in vec4 InTint;     // per-material tint

layout(location = 0) out vec3 OutNormal;
layout(location = 1) out vec2 OutUv;
layout(location = 2) out vec4 OutTint;

void main()
{
    gl_Position = Push.ViewProjection * vec4(InPosition, 1.0);
    OutNormal = InNormal;
    OutUv = InUv;
    OutTint = InTint;
}
