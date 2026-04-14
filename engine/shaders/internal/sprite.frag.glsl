#version 450
#extension GL_GOOGLE_include_directive  : require
#extension GL_EXT_nonuniform_qualifier  : require

#include "sencha/bindless.glsli"

layout(location = 0) in  vec2  vUv;
layout(location = 1) in  vec4  vColor;
layout(location = 2) flat in uint vTextureIndex;

layout(location = 0) out vec4 oColor;

void main()
{
    vec4 texel = texture(uImages[nonuniformEXT(vTextureIndex)], vUv);
    oColor     = texel * vColor;
}
