#pragma once

#include <render/RenderDebugView.h>

#include <algorithm>

// How one view's scene-referred colour becomes display-referred, applied once by
// the output pass. Filled from render.exposure, render.tonemap.knee and
// render.tonemap by ApplyOutputTransformCVars.
struct OutputTransformParams
{
    float Exposure = 1.0f;
    float TonemapKnee = 0.8f;
    bool TonemapEnabled = true;
};

// The scene value the output pass turns into `display`, for a colour tuned on
// screen that has to draw inside the scene. Undoes tonemap.glsli: the shoulder,
// then exposure. A display value the shoulder never reaches comes back as the
// value that lands closest to it.
[[nodiscard]] inline float SceneValueForDisplay(const OutputTransformParams& params, float display)
{
    float exposed = std::max(display, 0.0f);
    if (params.TonemapEnabled)
    {
        const float knee = std::clamp(params.TonemapKnee, 0.0f, 0.999f);
        if (exposed > knee)
        {
            const float range = 1.0f - knee;
            const float rolled = std::min((exposed - knee) / range, 0.999f);
            exposed = knee + range * rolled / (1.0f - rolled);
        }
    }
    return params.Exposure > 0.0f ? exposed / params.Exposure : 0.0f;
}

// A debug channel shows the value it wrote, so the transform steps aside for it:
// exposure and the shoulder would otherwise remap a normal or a heat colour.
[[nodiscard]] constexpr OutputTransformParams OutputTransformForView(const OutputTransformParams& authored,
                                                                     RenderDebugView debugView)
{
    if (debugView == RenderDebugView::None)
        return authored;
    return OutputTransformParams{ .Exposure = 1.0f, .TonemapKnee = authored.TonemapKnee, .TonemapEnabled = false };
}
