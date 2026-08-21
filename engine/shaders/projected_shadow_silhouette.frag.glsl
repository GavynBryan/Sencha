#version 450

// Coverage, not shading: every fragment the caster touches is silhouette.
layout(location = 0) out float outMask;

void main()
{
    outMask = 1.0;
}
