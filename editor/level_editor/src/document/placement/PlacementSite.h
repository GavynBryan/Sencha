#pragma once

#include <math/Quat.h>
#include <math/Vec.h>
#include <math/geometry/3d/Transform3d.h>

#include <cmath>
#include <numbers>

// Where a placement lands and the frame it turns in. Up and Facing (its
// direction at yaw zero) are unit and perpendicular; a yaw turns it about Up,
// from Facing toward Side.
struct PlacementSite
{
    Vec3d Point = {};
    Vec3d Up = Vec3d::Up();
    Vec3d Facing = Vec3d::Forward();

    [[nodiscard]] Vec3d Side() const { return Up.Cross(Facing); }
    bool operator==(const PlacementSite&) const = default;
};

// A site standing on the plane through `point` with `normal`. Yaw zero faces
// world forward laid onto that plane, or world right where forward runs along
// the normal, so the frame depends on the normal alone.
[[nodiscard]] inline PlacementSite SiteOnPlane(Vec3d point, Vec3d normal)
{
    constexpr float kParallel = 1e-3f;
    const Vec3d up = normal.Normalized();
    PlacementSite site{ .Point = point, .Up = up };
    for (const Vec3d reference : { Vec3d::Forward(), Vec3d::Right() })
    {
        const Vec3d laid = reference - up * reference.Dot(up);
        if (laid.Magnitude() > kParallel)
        {
            site.Facing = laid.Normalized();
            return site;
        }
    }
    return site;
}

[[nodiscard]] inline PlacementSite UprightSite(Vec3d point)
{
    return { .Point = point, .Up = Vec3d::Up(), .Facing = Vec3d::Forward() };
}

// Into [0, 2pi), so a yaw reads the same however it was reached.
[[nodiscard]] inline float NormalizedYaw(float radians)
{
    constexpr float kTurn = 2.0f * std::numbers::pi_v<float>;
    float yaw = std::fmod(radians, kTurn);
    if (yaw < 0.0f)
        yaw += kTurn;
    return yaw >= kTurn ? 0.0f : yaw;
}

// The transform a placement is written with: its root at the site, local up
// along the site's Up, local forward along Facing turned by `yaw`, unscaled.
[[nodiscard]] inline Transform3f PlacementTransform(const PlacementSite& site, float yaw)
{
    const Quatf frame = Quatf::FromBasis(site.Facing.Cross(site.Up), site.Up, -site.Facing);
    Transform3f placement = Transform3f::Identity();
    placement.Position = site.Point;
    placement.Rotation = Quatf::FromAxisAngle(site.Up, yaw) * frame;
    return placement;
}
