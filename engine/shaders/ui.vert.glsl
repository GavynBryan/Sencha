#version 450
#extension GL_GOOGLE_include_directive : require

// Authored UI geometry, in surface pixels.
//
// The document engine hands over vertex colour as sRGB with PREMULTIPLIED
// alpha. Converting here rather than in the fragment stage is not an
// optimisation -- it is the only correct place. Interpolating sRGB values and
// converting afterwards interpolates in the wrong space, and the error shows up
// exactly where UI gradients and antialiased edges live.
//
// Because the colour is premultiplied, the conversion has to happen on the
// straight colour: unpremultiply, convert, premultiply again. Alpha is a
// coverage fraction and is never converted.

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inUv;
layout(location = 2) in vec4 inColorSrgbPremultiplied;

layout(location = 0) out vec2 vUv;
layout(location = 1) out vec4 vColorLinearPremultiplied;

#include "ui.glsli"

void main()
{
    vUv = inUv;
    vColorLinearPremultiplied = PremultipliedSrgbToLinear(inColorSrgbPremultiplied);

    gl_Position = pc.Mvp * vec4(inPosition, 0.0, 1.0);
}
