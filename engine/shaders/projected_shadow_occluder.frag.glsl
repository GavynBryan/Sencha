#version 450

// Nearest receiver depth per texel: the pipeline blends with op MIN over a
// tile cleared to 1.0, so no depth attachment is needed -- the blend IS the
// depth test. Projection then keeps a shadow only on the first receiver
// surface along the ray.
layout(location = 0) out float outDepth;

void main()
{
    outDepth = gl_FragCoord.z;
}
