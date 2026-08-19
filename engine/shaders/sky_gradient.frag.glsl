#version 450

// The background, shaded from the same hemisphere the forward pass lights
// surfaces with: mesh_forward.frag.glsl mixes ground to sky by 0.5 + 0.5 * n.y
// for a surface normal, and this does it for the direction the eye is looking.
// So the background is not a decorative ramp behind the scene -- it is what the
// ambient term already claims the surroundings are, drawn where no surface
// covers it.
//
// Colours arrive LINEAR. The swapchain is sRGB and encodes on write, so
// brightening these to compensate would double-encode.

layout(location = 0) in  vec2 vNdc;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform SkyPC {
    // Clip space back to a world direction. The view translation is already
    // stripped out of this on the CPU side, so the result is a direction and
    // the gradient does not slide with the camera.
    mat4 InverseViewProjection;
    vec4 Top;     // linear RGB toward +Y
    vec4 Bottom;  // linear RGB toward -Y
} pc;

void main()
{
    vec4 world = pc.InverseViewProjection * vec4(vNdc, 1.0, 1.0);
    vec3 direction = normalize(world.xyz / world.w);

    float hemi = 0.5 + 0.5 * direction.y;
    outColor = vec4(mix(pc.Bottom.rgb, pc.Top.rgb, hemi), 1.0);
}
