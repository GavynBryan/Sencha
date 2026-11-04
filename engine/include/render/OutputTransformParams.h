#pragma once

#include <render/RenderDebugView.h>

// How one view's scene-referred colour becomes display-referred, applied once by
// the output pass. Filled from render.exposure, render.tonemap.knee and
// render.tonemap by ApplyOutputTransformCVars.
struct OutputTransformParams
{
    float Exposure = 1.0f;
    float TonemapKnee = 0.8f;
    bool TonemapEnabled = true;
};

// A debug channel shows the value it wrote, so the transform steps aside for it:
// exposure and the shoulder would otherwise remap a normal or a heat colour.
[[nodiscard]] constexpr OutputTransformParams OutputTransformForView(const OutputTransformParams& authored,
                                                                     RenderDebugView debugView)
{
    if (debugView == RenderDebugView::None)
        return authored;
    return OutputTransformParams{ .Exposure = 1.0f, .TonemapKnee = authored.TonemapKnee, .TonemapEnabled = false };
}
