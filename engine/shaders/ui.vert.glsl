#version 450

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

layout(push_constant) uniform UiPC {
    // Surface pixels straight to clip space. Built on the CPU as
    // orthographic * documentTransform * translate(commandTranslation), so the
    // shader never has to know the order those compose in.
    mat4 Mvp;
    uint TextureIndex;
    uint Flags;
} pc;

vec3 SrgbToLinear(vec3 srgb)
{
    vec3 low = srgb / 12.92;
    vec3 high = pow((srgb + 0.055) / 1.055, vec3(2.4));
    return mix(low, high, step(vec3(0.04045), srgb));
}

void main()
{
    vUv = inUv;

    float alpha = inColorSrgbPremultiplied.a;
    vec3 straightSrgb = alpha > 0.0 ? inColorSrgbPremultiplied.rgb / alpha : vec3(0.0);
    vColorLinearPremultiplied = vec4(SrgbToLinear(straightSrgb) * alpha, alpha);

    gl_Position = pc.Mvp * vec4(inPosition, 0.0, 1.0);
}
