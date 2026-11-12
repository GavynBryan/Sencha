#version 450
#extension GL_GOOGLE_include_directive : require

#include "mesh_view.glsli"
#include "mesh_material.glsli"

// Writes the style slot where a highlighted surface is the front-most thing drawn,
// with the surface's depth as the fraction, so the composite can test its ring
// against what stands in front. No blending anywhere: a mask value is a slot and a
// depth, never a colour. An alpha-masked material is outlined along its cutout.

// Keeps the depth a fraction below 1, so the slot is the integer part.
const float kOutlineDepthScale = 0.9375;

layout(location = 0) out vec4 outMask;

void main()
{
    if (MATERIAL_ALPHA_MASK && SampleBaseColor().a < pushData.AlphaCutoff)
        discard;
    outMask = vec4(float(frame.OutlineStyleSlot) + gl_FragCoord.z * kOutlineDepthScale, 0.0, 0.0, 1.0);
}
