#pragma once

#include "ViewportMath.h"

#include <imgui.h>
#include <math/Vec.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>
#include <span>

// Where a rotation dial pinned to a face plane sits, and whether the cursor is
// on it. Pure geometry with no ImGui runtime and no projection of its own, so
// the panel that draws the dial and the tool that hit-tests it go through one
// rule rather than two that drift apart.
//
// Sizes are in design pixels and scaled at the call site, and the two functions
// that care about the screen take points somebody else has already projected.
// That keeps this header free of viewport and style state.
namespace ViewportDial
{
inline constexpr int kRimSegments = 48;
inline constexpr float kFraction = 0.5f;    // of the box's smaller half-extent
inline constexpr float kMaxPixels = 56.0f;  // a cap on the radius, never a floor
inline constexpr float kMinPixels = 16.0f;  // below this the dial hides instead
inline constexpr float kRimHitPixels = 7.0f;
inline constexpr float kKnobPixels = 8.0f;
// A face turned further than this from the viewer shows an angle too foreshortened
// to judge, so the dial hides rather than inviting a guess.
inline constexpr float kEdgeOnCosine = 0.2f;

struct Placement
{
    Vec3d Center;
    Vec3d AxisU; // the angle is measured from +U toward +V
    Vec3d AxisV;
    float Radius = 0.0f;
    bool Visible = false;

    [[nodiscard]] Vec3d PointAt(float angle) const
    {
        return Center + AxisU * (Radius * std::cos(angle)) + AxisV * (Radius * std::sin(angle));
    }
};

// The dial for a box whose smaller half-extent is `boxSemiMinor`, on the plane
// spanned by (axisU, axisV).
//
// The radius is geometric first: half the box's smaller half-extent, so the rim
// sits well inside the shape it controls. The nearest box corner is at least
// that half-extent away, so the rim provably cannot reach a corner handle at any
// aspect ratio and the two never contend for a press.
//
// A pixel cap keeps the dial from swelling when the box is close; there is no
// pixel floor. When the box is too small on screen to give the rim a usable
// target the dial **hides**, because a widget that grew past the thing it
// controls would be worse than one that is briefly unavailable -- and the
// properties panel still has the numeric control either way.
//
// `worldPerPixel` is the world size of one pixel at the centre, which the caller
// takes from ViewportProjection::WorldSizeForPixels(centre, 1).
[[nodiscard]] inline Placement Place(Vec3d center, Vec3d axisU, Vec3d axisV, Vec3d viewDirection,
                                     float boxSemiMinor, float worldPerPixel, float scale)
{
    Placement placement;
    placement.Center = center;
    placement.AxisU = axisU;
    placement.AxisV = axisV;
    if (!(boxSemiMinor > 0.0f) || !(worldPerPixel > 0.0f))
        return placement;

    const Vec3d normal = axisU.Cross(axisV);
    if (std::abs(normal.Dot(viewDirection)) < kEdgeOnCosine)
        return placement;

    placement.Radius = std::min(kFraction * boxSemiMinor, worldPerPixel * kMaxPixels * scale);
    placement.Visible = placement.Radius / worldPerPixel >= kMinPixels * scale;
    return placement;
}

// The rim, tessellated in the plane. Writes min(out.size(), kRimSegments + 1)
// points, closing the ring, and returns how many.
[[nodiscard]] inline int RimPoints(const Placement& placement, std::span<Vec3d> out)
{
    const int count = std::min(static_cast<int>(out.size()), kRimSegments + 1);
    for (int i = 0; i < count; ++i)
    {
        const float angle = 2.0f * std::numbers::pi_v<float> * static_cast<float>(i)
                            / static_cast<float>(kRimSegments);
        out[static_cast<std::size_t>(i)] = placement.PointAt(angle);
    }
    return count;
}

// The stops a snapped turn lands on, every `increment` radians round the rim.
// An increment of zero or less is free rotation and has no stops.
[[nodiscard]] inline int TickPoints(const Placement& placement, float increment, std::span<Vec3d> out)
{
    if (increment <= 0.0f)
        return 0;
    const int count = std::min(static_cast<int>(out.size()),
                               static_cast<int>(2.0f * std::numbers::pi_v<float> / increment));
    for (int i = 0; i < count; ++i)
        out[static_cast<std::size_t>(i)] = placement.PointAt(static_cast<float>(i) * increment);
    return count;
}

// Distance in pixels from `cursor` to the projected rim, or nothing when too
// little of the rim reached the screen to measure against.
[[nodiscard]] inline std::optional<float> DistanceToRim(std::span<const std::optional<ImVec2>> rim,
                                                        ImVec2 cursor)
{
    std::optional<float> best;
    for (std::size_t i = 0; i + 1 < rim.size(); ++i)
    {
        if (!rim[i].has_value() || !rim[i + 1].has_value())
            continue;
        const float distance = ViewportMath::DistancePointToSegment(
            Vec2d{ cursor.x, cursor.y }, Vec2d{ rim[i]->x, rim[i]->y },
            Vec2d{ rim[i + 1]->x, rim[i + 1]->y });
        if (!best.has_value() || distance < *best)
            best = distance;
    }
    return best;
}
} // namespace ViewportDial
