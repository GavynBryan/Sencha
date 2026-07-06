#include "SelectionLabels.h"

#include <cmath>
#include <cstdio>

std::string FormatUnits(double value)
{
    const double rounded = std::round(value);
    char buffer[32];
    if (std::abs(value - rounded) < 0.05)
        std::snprintf(buffer, sizeof(buffer), "%.0f", rounded);
    else
        std::snprintf(buffer, sizeof(buffer), "%.2f", value);
    return buffer;
}

std::vector<LabelRequest> SelectionDimensionLabels(const Aabb3d& bounds, const Vec4& color)
{
    std::vector<LabelRequest> labels;
    if (!bounds.IsValid())
        return labels;

    const Vec3d mn = bounds.Min;
    const Vec3d mx = bounds.Max;
    const Vec3d size = bounds.Size(); // full width/length/height (Extent() is half)

    for (int axis = 0; axis < 3; ++axis)
    {
        const float length = size[axis];
        if (length <= 1e-4f)
            continue;

        // Midpoint of the box edge running along this axis from the min corner.
        Vec3d anchor = mn;
        anchor[axis] = (mn[axis] + mx[axis]) * 0.5f;
        labels.push_back(LabelRequest{ .World = anchor, .Color = color, .Text = FormatUnits(length), .Axis = axis });
    }
    return labels;
}
