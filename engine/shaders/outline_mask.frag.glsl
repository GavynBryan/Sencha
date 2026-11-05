#version 450
#extension GL_GOOGLE_include_directive : require

#include "mesh_view.glsli"
#include "mesh_material.glsli"

// Writes the style slot where a highlighted surface is the front-most thing drawn.
// No blending anywhere: a mask value is a slot, never a colour. An alpha-masked
// material is outlined along its cutout.

layout(constant_id = 1) const bool MATERIAL_ALPHA_MASK = false;

layout(location = 0) out vec4 outMask;

void main()
{
    if (MATERIAL_ALPHA_MASK && SampleBaseColor().a < pushData.AlphaCutoff)
        discard;
    outMask = vec4(float(frame.OutlineStyleSlot) / 255.0, 0.0, 0.0, 1.0);
}
