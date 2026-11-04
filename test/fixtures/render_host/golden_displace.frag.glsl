#version 450

#include "post_effect.glsli"

// A test-only scene-domain displacement. It reads its phase from params[0].x rather
// than the wall clock, so a capture of it is the same image on every run.
void main()
{
    vec2 offset = vec2(sin(vUv.y * 24.0 + post.params[0].x), cos(vUv.x * 24.0 + post.params[0].x)) * 0.02 * post.weight;
    outColor = SampleScene(vUv + offset);
}
