#include "render/LineShapes.h"

#include <array>
#include <cmath>
#include <numbers>
#include <utility>

namespace
{
constexpr int kRingSegments = 24;

void AppendRing(std::vector<EditorLineSegment>& segments, const Vec3d& centre, float radius, const Vec4& color,
                float widthPx)
{
    const auto at = [&](int step) {
        const float angle = 2.0f * std::numbers::pi_v<float> * static_cast<float>(step) / kRingSegments;
        return centre + Vec3d(std::cos(angle) * radius, 0.0f, std::sin(angle) * radius);
    };
    for (int step = 0; step < kRingSegments; ++step)
        segments.push_back({ at(step), at(step + 1), color, widthPx });
}
}

void AppendBoxEdges(std::vector<EditorLineSegment>& segments, const Aabb3d& box, const Vec4& color, float widthPx)
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
        segments.push_back({ corners[static_cast<std::size_t>(a)], corners[static_cast<std::size_t>(b)], color,
                             widthPx });
}

void AppendCapsuleEdges(std::vector<EditorLineSegment>& segments, const Vec3d& centre, float radius, float height,
                        const Vec4& color, float widthPx)
{
    const float side = std::max(0.0f, height * 0.5f - radius);
    const Vec3d up(0.0f, side, 0.0f);
    const Vec3d top(0.0f, side + radius, 0.0f);
    AppendRing(segments, centre, radius, color, widthPx);
    AppendRing(segments, centre + up, radius, color, widthPx);
    AppendRing(segments, centre - up, radius, color, widthPx);
    for (const Vec3d& out : { Vec3d(radius, 0.0f, 0.0f), Vec3d(-radius, 0.0f, 0.0f), Vec3d(0.0f, 0.0f, radius),
                              Vec3d(0.0f, 0.0f, -radius) })
    {
        segments.push_back({ centre - up + out, centre + up + out, color, widthPx });
        segments.push_back({ centre + up + out, centre + top, color, widthPx });
        segments.push_back({ centre - up + out, centre - top, color, widthPx });
    }
}

void AppendFloorGrid(std::vector<EditorLineSegment>& segments, const Vec3d& centre, float halfSize, float spacing,
                     const Vec4& color, float widthPx)
{
    if (spacing <= 0.0f)
        return;
    const int lines = static_cast<int>(std::floor(halfSize / spacing));
    for (int i = -lines; i <= lines; ++i)
    {
        const float offset = static_cast<float>(i) * spacing;
        segments.push_back({ centre + Vec3d(offset, 0.0f, -halfSize), centre + Vec3d(offset, 0.0f, halfSize), color,
                             widthPx });
        segments.push_back({ centre + Vec3d(-halfSize, 0.0f, offset), centre + Vec3d(halfSize, 0.0f, offset), color,
                             widthPx });
    }
}
