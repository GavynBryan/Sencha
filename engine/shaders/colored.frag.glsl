#version 450

#include "display_encode.glsli"

layout(location = 0) in vec4 InColor;
layout(location = 0) out vec4 OutFragColor;

// Set only by a pipeline drawing straight to a display with no hardware sRGB write.
layout(constant_id = 0) const bool kEncodeOutput = false;

void main()
{
    OutFragColor = vec4(kEncodeOutput ? EncodeSrgb(InColor.rgb) : InColor.rgb, InColor.a);
}
