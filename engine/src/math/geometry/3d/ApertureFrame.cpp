#include <math/geometry/3d/ApertureFrame.h>

#include <algorithm>
#include <cmath>

namespace
{
    constexpr float kSmallestRotationLength = 1.0e-6f;

    bool Finite(const Vec3d& value)
    {
        return std::isfinite(value.X) && std::isfinite(value.Y) && std::isfinite(value.Z);
    }

    bool Finite(const Quatf& value)
    {
        return std::isfinite(value.X) && std::isfinite(value.Y) && std::isfinite(value.Z) && std::isfinite(value.W);
    }

    Quatf BasisRotation(const ApertureFrame& frame)
    {
        return Quatf::FromBasis(frame.Right, frame.Up, -frame.Normal);
    }
}

const char* ApertureFrameFaultName(ApertureFrameFault fault)
{
    switch (fault)
    {
    case ApertureFrameFault::None: return "valid";
    case ApertureFrameFault::NonFiniteTransform: return "non-finite transform";
    case ApertureFrameFault::DegenerateRotation: return "degenerate rotation";
    case ApertureFrameFault::ScaleNotUnit: return "scale is not one";
    case ApertureFrameFault::InvalidExtents: return "half extents are not finite and positive";
    }
    return "unknown";
}

ApertureFrameFault CheckApertureFrame(const Transform3f& world, Vec2d halfExtents)
{
    if (!Finite(world.Position) || !Finite(world.Rotation) || !Finite(world.Scale))
        return ApertureFrameFault::NonFiniteTransform;
    if (!(world.Rotation.Length() > kSmallestRotationLength))
        return ApertureFrameFault::DegenerateRotation;
    for (int axis = 0; axis < 3; ++axis)
        if (!(std::abs(world.Scale[axis] - 1.0f) <= ApertureTolerance::UnitScale))
            return ApertureFrameFault::ScaleNotUnit;
    if (!std::isfinite(halfExtents.X) || !std::isfinite(halfExtents.Y) || !(halfExtents.X > 0.0f)
        || !(halfExtents.Y > 0.0f))
        return ApertureFrameFault::InvalidExtents;
    return ApertureFrameFault::None;
}

ApertureFrame MakeApertureFrame(const Transform3f& world, Vec2d halfExtents)
{
    const Quatf rotation = world.Rotation.Normalized();
    return ApertureFrame{
        .Origin = world.Position,
        .Normal = rotation.RotateVector(Vec3d::Forward()).Normalized(),
        .Right = rotation.RotateVector(Vec3d::Right()).Normalized(),
        .Up = rotation.RotateVector(Vec3d::Up()).Normalized(),
        .HalfExtents = halfExtents,
    };
}

float ApertureSignedDistance(const ApertureFrame& frame, const Vec3d& point)
{
    return (point - frame.Origin).Dot(frame.Normal);
}

ApertureSide ClassifyApertureSide(const ApertureFrame& frame, const Vec3d& point, float planeTolerance)
{
    const float distance = ApertureSignedDistance(frame, point);
    if (distance > planeTolerance)
        return ApertureSide::Front;
    if (distance < -planeTolerance)
        return ApertureSide::Back;
    return ApertureSide::On;
}

Vec2d ApertureCoordinates(const ApertureFrame& frame, const Vec3d& point)
{
    const Vec3d offset = point - frame.Origin;
    return Vec2d{ offset.Dot(frame.Right), offset.Dot(frame.Up) };
}

bool InsideApertureRectangle(const ApertureFrame& frame, const Vec2d& coordinates, float edgeTolerance)
{
    return std::abs(coordinates.X) <= frame.HalfExtents.X + edgeTolerance
        && std::abs(coordinates.Y) <= frame.HalfExtents.Y + edgeTolerance;
}

ApertureInterval SphereSupportInterval(const ApertureFrame& frame, const Vec3d& centre, float radius)
{
    const float distance = ApertureSignedDistance(frame, centre);
    return ApertureInterval{ distance - radius, distance + radius };
}

ApertureInterval CapsuleSupportInterval(const ApertureFrame& frame, const Vec3d& a, const Vec3d& b, float radius)
{
    const float first = ApertureSignedDistance(frame, a);
    const float second = ApertureSignedDistance(frame, b);
    return ApertureInterval{ std::min(first, second) - radius, std::max(first, second) + radius };
}

std::optional<ApertureCrossing> CrossAperture(const ApertureFrame& frame, const Vec3d& from, const Vec3d& to,
                                              float edgeTolerance)
{
    const float start = ApertureSignedDistance(frame, from);
    const float end = ApertureSignedDistance(frame, to);
    const bool startsInFront = start >= 0.0f;
    const bool endsInFront = end >= 0.0f;
    if (startsInFront == endsInFront)
        return std::nullopt;
    // Opposite sides, so start - end is never zero.
    const float fraction = std::clamp(start / (start - end), 0.0f, 1.0f);
    const Vec3d point = from + (to - from) * fraction;
    const Vec2d coordinates = ApertureCoordinates(frame, point);
    if (!InsideApertureRectangle(frame, coordinates, edgeTolerance))
        return std::nullopt;
    return ApertureCrossing{
        .Fraction = fraction,
        .Point = point,
        .Coordinates = coordinates,
        .Direction = startsInFront ? ApertureCrossingDirection::FrontToBack : ApertureCrossingDirection::BackToFront,
    };
}

Transform3f ApertureMapping(const ApertureFrame& entry, const ApertureFrame& exit)
{
    const Quatf halfTurnAboutUp{ 0.0f, 1.0f, 0.0f, 0.0f };
    const Quatf rotation = (BasisRotation(exit) * halfTurnAboutUp * BasisRotation(entry).Conjugate()).Normalized();
    return Transform3f{ exit.Origin - rotation.RotateVector(entry.Origin), rotation, Vec3d::One() };
}

bool IsUprightMapping(const Transform3f& mapping, float tolerance)
{
    return 1.0f - mapping.Rotation.RotateVector(Vec3d::Up()).Dot(Vec3d::Up()) <= tolerance;
}
