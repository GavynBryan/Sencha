#pragma once

#include "EditorWideLinePipeline.h"

#include <math/geometry/3d/Aabb3d.h>

#include <array>
#include <utility>
#include <vector>

// Appends the 12 edges of a world AABB as wide-line segments, the shared
// primitive for overlay renderers that outline a box (zone bounds, probe
// volumes).
inline void AppendBoxEdges(std::vector<EditorLineSegment>& segments, const Aabb3d& box,
                           const Vec4& color, float widthPx)
{
    const Vec3d& lo = box.Min;
    const Vec3d& hi = box.Max;
    const std::array<Vec3d, 8> corners = {
        Vec3d{ lo.X, lo.Y, lo.Z }, Vec3d{ hi.X, lo.Y, lo.Z },
        Vec3d{ hi.X, lo.Y, hi.Z }, Vec3d{ lo.X, lo.Y, hi.Z },
        Vec3d{ lo.X, hi.Y, lo.Z }, Vec3d{ hi.X, hi.Y, lo.Z },
        Vec3d{ hi.X, hi.Y, hi.Z }, Vec3d{ lo.X, hi.Y, hi.Z },
    };
    static constexpr std::array<std::pair<int, int>, 12> kEdges = { {
        { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 },
        { 4, 5 }, { 5, 6 }, { 6, 7 }, { 7, 4 },
        { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 },
    } };
    for (const auto& [a, b] : kEdges)
        segments.push_back(EditorLineSegment{ corners[static_cast<size_t>(a)],
                                              corners[static_cast<size_t>(b)], color,
                                              widthPx });
}
