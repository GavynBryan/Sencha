#version 450

#include "tonemap.glsli"

// The background, shaded from the same hemisphere the forward pass lights
// surfaces with: mesh_forward.frag.glsl mixes ground to sky by 0.5 + 0.5 * n.y
// for a surface normal, and this does it for the direction the eye is looking.
// So the background is not a decorative ramp behind the scene -- it is what the
// ambient term already claims the surroundings are, drawn where no surface
// covers it.
//
// It goes through the same exposure and tonemap as the geometry in front of it.
// Skipping that would leave the background in a different display space, so
// raising render.exposure would brighten the scene and not the sky it is
// supposedly lit by -- the drift this pass exists to prevent.
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
    // x: exposure, y: tonemap knee, z: tonemap enabled.
    vec4 Output;
} pc;

void main()
{
    vec4 world = pc.InverseViewProjection * vec4(vNdc, 1.0, 1.0);
    vec3 direction = normalize(world.xyz / world.w);

    float hemi = 0.5 + 0.5 * direction.y;
    vec3 color = mix(pc.Bottom.rgb, pc.Top.rgb, hemi);

    outColor = vec4(ApplyOutputTransform(color, pc.Output.x, pc.Output.y,
                                         pc.Output.z != 0.0),
                    1.0);
}
