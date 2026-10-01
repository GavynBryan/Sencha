#version 450

// 1px analytic anti-aliasing: alpha falls from 1 to 0 across the outermost pixel of
// the stroke, so the edge is smooth without MSAA. Requires alpha blending.

layout(location = 0) in vec4 InColor;
layout(location = 1) in float InAcrossPx;     // signed pixel distance from center
layout(location = 2) in float InHalfWidthPx;

layout(location = 0) out vec4 OutFragColor;

void main()
{
    float alpha = clamp(InHalfWidthPx - abs(InAcrossPx) + 0.5, 0.0, 1.0);
    OutFragColor = vec4(InColor.rgb, InColor.a * alpha);
}
