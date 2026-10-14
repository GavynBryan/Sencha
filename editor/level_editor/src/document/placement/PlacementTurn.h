#pragma once

#include "PlacementSite.h"

#include <math/geometry/3d/Ray3d.h>

#include <cmath>
#include <optional>

// Turning a placement to face where the pointer's ray meets its site's plane.
// Why the ill-posed cases hold rather than guess: docs/editor/placement.md.
namespace PlacementTurn
{
// A ray this close to lying in the plane (about 3 degrees) meets it too far
// away, or too unsteadily, to aim by.
inline constexpr float kMinGrazingSine = 0.052f;

struct Aim
{
    float Yaw = 0.0f;
    Vec3d Toward = {}; // where on the site's plane the placement faces
};

// The yaw that faces the site toward where `ray` meets its plane; nullopt where
// the ray grazes the plane, meets it behind its origin, or meets it within
// `minRadius` of the site's point.
[[nodiscard]] inline std::optional<Aim> FacingToward(const PlacementSite& site, const Ray3d& ray, float minRadius)
{
    const Vec3d direction = ray.Direction.Normalized();
    const float along = direction.Dot(site.Up);
    if (std::abs(along) < kMinGrazingSine)
        return std::nullopt;
    const float distance = (site.Point - ray.Origin).Dot(site.Up) / along;
    if (!(distance > 0.0f))
        return std::nullopt;
    const Vec3d toward = ray.Origin + direction * distance;
    Vec3d offset = toward - site.Point;
    offset = offset - site.Up * offset.Dot(site.Up);
    const float radius = offset.Magnitude();
    if (!(radius > 0.0f) || radius < minRadius)
        return std::nullopt;
    return Aim{ .Yaw = NormalizedYaw(std::atan2(offset.Dot(site.Side()), offset.Dot(site.Facing))),
                .Toward = toward };
}

} // namespace PlacementTurn
