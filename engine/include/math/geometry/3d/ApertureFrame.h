#pragma once

#include <math/Quat.h>
#include <math/Vec.h>
#include <math/geometry/3d/Transform3d.h>

#include <cstdint>
#include <optional>

// A rectangle in a plane with a front side, as a link joins two of them. The
// conventions are pinned in docs/zone/links.md, "Frame convention".
struct ApertureFrame
{
    Vec3d Origin = Vec3d::Zero();
    Vec3d Normal = Vec3d::Forward();
    Vec3d Right = Vec3d::Right();
    Vec3d Up = Vec3d::Up();
    Vec2d HalfExtents{ 1.0f, 1.0f };

    friend bool operator==(const ApertureFrame&, const ApertureFrame&) = default;
};

namespace ApertureTolerance
{
    // Metres either side of the plane that count as on it.
    inline constexpr float PlaneSide = 1.0e-4f;
    // Metres past the rectangle's edge that still count as inside it.
    inline constexpr float RectangleEdge = 1.0e-4f;
    // How far each axis of an aperture's scale may stray from one.
    inline constexpr float UnitScale = 1.0e-4f;
    // Length of the difference between world up and its image under a mapping.
    inline constexpr float Upright = 1.0e-3f;
    // Half extents of two linked apertures may differ by this many metres.
    inline constexpr float MatchingExtents = 1.0e-3f;
}

enum class ApertureFrameFault : std::uint8_t
{
    None,
    NonFiniteTransform,
    DegenerateRotation,
    ScaleNotUnit,
    InvalidExtents,
};

[[nodiscard]] const char* ApertureFrameFaultName(ApertureFrameFault fault);

// Finite translation and rotation, a rotation that normalizes, unit scale, and
// half extents that are finite and positive.
[[nodiscard]] ApertureFrameFault CheckApertureFrame(const Transform3f& world, Vec2d halfExtents);

// The frame of an aperture whose world transform passes CheckApertureFrame.
[[nodiscard]] ApertureFrame MakeApertureFrame(const Transform3f& world, Vec2d halfExtents);

enum class ApertureSide : std::uint8_t
{
    Front,
    On,
    Back,
};

[[nodiscard]] float ApertureSignedDistance(const ApertureFrame& frame, const Vec3d& point);
[[nodiscard]] ApertureSide ClassifyApertureSide(const ApertureFrame& frame, const Vec3d& point,
                                                float planeTolerance = ApertureTolerance::PlaneSide);
// The point's right and up coordinates in the aperture's plane.
[[nodiscard]] Vec2d ApertureCoordinates(const ApertureFrame& frame, const Vec3d& point);
[[nodiscard]] bool InsideApertureRectangle(const ApertureFrame& frame, const Vec2d& coordinates,
                                           float edgeTolerance = ApertureTolerance::RectangleEdge);

// The nearest and farthest signed distances a shape reaches along the normal.
struct ApertureInterval
{
    float Near = 0.0f;
    float Far = 0.0f;
};

[[nodiscard]] ApertureInterval SphereSupportInterval(const ApertureFrame& frame, const Vec3d& centre, float radius);
// A capsule is the points within `radius` of the segment from `a` to `b`.
[[nodiscard]] ApertureInterval CapsuleSupportInterval(const ApertureFrame& frame, const Vec3d& a, const Vec3d& b,
                                                      float radius);

enum class ApertureCrossingDirection : std::uint8_t
{
    FrontToBack,
    BackToFront,
};

struct ApertureCrossing
{
    float Fraction = 0.0f;
    Vec3d Point = Vec3d::Zero();
    Vec2d Coordinates{};
    ApertureCrossingDirection Direction = ApertureCrossingDirection::FrontToBack;
};

// Where a segment passes through the rectangle. The plane itself belongs to the
// front, so a segment that ends on the plane has not crossed and one that
// starts on it and goes behind crosses at fraction zero.
[[nodiscard]] std::optional<ApertureCrossing> CrossAperture(const ApertureFrame& frame, const Vec3d& from,
                                                            const Vec3d& to,
                                                            float edgeTolerance = ApertureTolerance::RectangleEdge);

// The rigid transform carrying what goes behind `entry` to the same place in
// front of `exit`. Unit scale: a point maps with TransformPoint, a direction or
// a linear or angular velocity with Rotation.RotateVector, an orientation by
// composing Rotation on its left.
[[nodiscard]] Transform3f ApertureMapping(const ApertureFrame& entry, const ApertureFrame& exit);

[[nodiscard]] bool IsUprightMapping(const Transform3f& mapping, float tolerance = ApertureTolerance::Upright);
