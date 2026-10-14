#pragma once

#include <algorithm>
#include <cmath>
#include <format>
#include <string>

// A length as a document reads it, in whole pixels so edges land on the grid.
[[nodiscard]] inline std::string PixelLength(float pixels)
{
    return std::format("{}px", static_cast<int>(std::lround(pixels)));
}

// A fraction of the parent's length, kept within it.
[[nodiscard]] inline std::string PercentLength(double fraction)
{
    return std::format("{:.3f}%", std::clamp(fraction, 0.0, 1.0) * 100.0);
}
