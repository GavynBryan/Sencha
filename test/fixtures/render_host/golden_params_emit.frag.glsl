#version 450
#extension GL_GOOGLE_include_directive : require

#include "mesh_view.glsli"
#include "mesh_material.glsli"

layout(location = 0) out vec4 outColor;

// Ignores the material's own emission, so a capture tells it from the built-in shading.
void main()
{
    vec4 base = SampleBaseColor();
    outColor = MeshOutput(base.rgb, pushData.Params[0].rgb * pushData.Params[0].a, base.a);
}
