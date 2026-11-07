#version 450

#include "post_effect.glsli"

// Fades the frame to params[0].rgb, a linear display-referred colour, by the weight.
void main()
{
    vec4 scene = SampleScene(vUv);
    outColor = vec4(mix(scene.rgb, post.params[0].rgb, clamp(post.weight, 0.0, 1.0)), scene.a);
}
