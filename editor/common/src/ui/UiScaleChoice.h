#pragma once

#include <algorithm>

// The scale a window's UI draws at: what the person asked for, or the window's
// display scale when they asked for nothing (0), clamped to what reads.
[[nodiscard]] constexpr float ChooseUiScale(double requested, float displayScale)
{
    float scale = static_cast<float>(requested);
    if (scale <= 0.0f)
        scale = displayScale > 0.0f ? displayScale : 1.0f;
    return std::clamp(scale, 0.5f, 4.0f);
}
