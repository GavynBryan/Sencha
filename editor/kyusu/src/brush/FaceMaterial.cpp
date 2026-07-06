#include "FaceMaterial.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
    constexpr float kPi = 3.14159265358979323846f;

    // Avoid divide-by-zero from a zeroed scale; texel density of 0 is meaningless.
    float SafeScale(float s)
    {
        return std::abs(s) < 1e-6f ? 1.0f : s;
    }

    // A point's coordinate on the (rotated) UV axes, ignoring scale/offset — the
    // basis for the justify presets.
    Vec2d RawUv(const UvProjection& p, Vec3d pos)
    {
        UvProjection raw = p;
        raw.Scale = { 1.0f, 1.0f };
        raw.Offset = { 0.0f, 0.0f };
        return ProjectUv(raw, pos);
    }

    struct RawBounds
    {
        Vec2d Min{ 0.0f, 0.0f };
        Vec2d Max{ 0.0f, 0.0f };
        bool  Valid = false;
    };

    // Bounds of the points' raw (unscaled, unoffset) UV coordinates, generic over
    // the projector so the local and world justify presets share one kernel.
    template <typename ProjectFn>
    RawBounds ComputeRawBoundsWith(ProjectFn&& project, std::span<const Vec3d> positions)
    {
        RawBounds b;
        b.Min = { std::numeric_limits<float>::max(), std::numeric_limits<float>::max() };
        b.Max = { std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest() };
        for (Vec3d pos : positions)
        {
            const Vec2d c = project(pos);
            b.Min.X = std::min(b.Min.X, c.X);
            b.Min.Y = std::min(b.Min.Y, c.Y);
            b.Max.X = std::max(b.Max.X, c.X);
            b.Max.Y = std::max(b.Max.Y, c.Y);
            b.Valid = true;
        }
        return b;
    }

    RawBounds ComputeRawBounds(const UvProjection& p, std::span<const Vec3d> positions)
    {
        return ComputeRawBoundsWith([&](Vec3d pos) { return RawUv(p, pos); }, positions);
    }

    // The justify math shared by the local and world presets: given raw-coordinate
    // bounds, fit rescales so the span is one tile, center re-offsets the midpoint
    // to (0.5, 0.5) at the kept scale.
    void ApplyFitFromBounds(const RawBounds& b, Vec2d& scale, Vec2d& offset)
    {
        const float spanU = b.Max.X - b.Min.X;
        const float spanV = b.Max.Y - b.Min.Y;
        if (spanU > 1e-5f) { scale.X = spanU; offset.X = -b.Min.X / spanU; }
        if (spanV > 1e-5f) { scale.Y = spanV; offset.Y = -b.Min.Y / spanV; }
    }

    void ApplyCenterFromBounds(const RawBounds& b, const Vec2d& scale, Vec2d& offset)
    {
        const float midU = (b.Min.X + b.Max.X) * 0.5f;
        const float midV = (b.Min.Y + b.Max.Y) * 0.5f;
        if (std::abs(scale.X) > 1e-6f) offset.X = 0.5f - midU / scale.X;
        if (std::abs(scale.Y) > 1e-6f) offset.Y = 0.5f - midV / scale.Y;
    }

    // The local projection's axes with its Rotation folded in: the exact basis
    // ProjectUv dots against.
    void RotatedAxes(const UvProjection& p, Vec3d& u, Vec3d& v)
    {
        const float theta = p.Rotation * (3.14159265358979323846f / 180.0f);
        const float c = std::cos(theta);
        const float s = std::sin(theta);
        u = p.AxisU * c + p.AxisV * s;
        v = p.AxisV * c - p.AxisU * s;
    }

    // Componentwise transform-scale application with zero components treated as 1,
    // so the world bridge stays total on degenerate transforms.
    Vec3d MulScale(Vec3d a, Vec3d s)
    {
        return { a.X * SafeScale(s.X), a.Y * SafeScale(s.Y), a.Z * SafeScale(s.Z) };
    }

    Vec3d DivScale(Vec3d a, Vec3d s)
    {
        return { a.X / SafeScale(s.X), a.Y / SafeScale(s.Y), a.Z / SafeScale(s.Z) };
    }
}

const AssetRef& EffectiveMaterial(const FaceMaterial& face, const AssetRef& levelDefault)
{
    return face.Material.Path.empty() ? levelDefault : face.Material;
}

Vec2d ProjectUv(const UvProjection& p, Vec3d localPos)
{
    // Rotate the (U,V) basis in its own plane by p.Rotation (standard texture
    // rotation): U' = cosθ·U + sinθ·V, V' = -sinθ·U + cosθ·V.
    const float theta = p.Rotation * (kPi / 180.0f);
    const float c = std::cos(theta);
    const float s = std::sin(theta);
    const Vec3d u = p.AxisU * c + p.AxisV * s;
    const Vec3d v = p.AxisV * c - p.AxisU * s;

    return Vec2d{
        localPos.Dot(u) / SafeScale(p.Scale.X) + p.Offset.X,
        localPos.Dot(v) / SafeScale(p.Scale.Y) + p.Offset.Y,
    };
}

UvProjection UvProjectionForNormal(Vec3d normal, bool worldAligned)
{
    UvProjection p;
    p.WorldAligned = worldAligned;

    if (worldAligned)
    {
        // Dominant world axis of the normal picks the box-mapping plane. The
        // world is Y-up, so every wall maps V to world up and floors/ceilings
        // box-map the ground plane; if wall axes disagreed on which way is up,
        // a texture pulled around a corner would rotate 90 degrees between the
        // X- and Z-facing walls.
        const float ax = std::abs(normal.X);
        const float ay = std::abs(normal.Y);
        const float az = std::abs(normal.Z);
        if (ay >= ax && ay >= az) // facing ±Y: floor/ceiling
        {
            p.AxisU = { 1.0f, 0.0f, 0.0f };
            p.AxisV = { 0.0f, 0.0f, 1.0f };
        }
        else if (ax >= az) // facing ±X: wall
        {
            p.AxisU = { 0.0f, 0.0f, 1.0f };
            p.AxisV = { 0.0f, 1.0f, 0.0f };
        }
        else // facing ±Z: wall
        {
            p.AxisU = { 1.0f, 0.0f, 0.0f };
            p.AxisV = { 0.0f, 1.0f, 0.0f };
        }
        return p;
    }

    // Face-aligned: build an in-plane orthonormal basis from the normal so the
    // texture is glued to the face and rotates with it.
    Vec3d n = normal;
    if (n.SqrMagnitude() <= 0.0f)
        n = { 0.0f, 0.0f, 1.0f };
    else
        n = n.Normalized();

    // Pick a reference axis least parallel to n, then Gram-Schmidt.
    const Vec3d reference =
        std::abs(n.Z) < 0.99f ? Vec3d{ 0.0f, 0.0f, 1.0f } : Vec3d{ 1.0f, 0.0f, 0.0f };
    Vec3d u = reference - n * reference.Dot(n);
    u = u.SqrMagnitude() > 0.0f ? u.Normalized() : Vec3d{ 1.0f, 0.0f, 0.0f };
    const Vec3d v = n.Cross(u);

    p.AxisU = u;
    p.AxisV = v;
    return p;
}

UvProjection UvProjectionFit(const UvProjection& p, std::span<const Vec3d> localPositions)
{
    UvProjection out = p;
    const RawBounds b = ComputeRawBounds(p, localPositions);
    if (b.Valid)
        ApplyFitFromBounds(b, out.Scale, out.Offset);
    return out;
}

UvProjection UvProjectionCenter(const UvProjection& p, std::span<const Vec3d> localPositions)
{
    UvProjection out = p;
    const RawBounds b = ComputeRawBounds(p, localPositions);
    if (b.Valid)
        ApplyCenterFromBounds(b, out.Scale, out.Offset);
    return out;
}

Vec2d ProjectWorldUv(const WorldUvProjection& p, Vec3d worldPos)
{
    return Vec2d{
        worldPos.Dot(p.AxisU) / SafeScale(p.Scale.X) + p.Offset.X,
        worldPos.Dot(p.AxisV) / SafeScale(p.Scale.Y) + p.Offset.Y,
    };
}

WorldUvProjection UvProjectionToWorld(const UvProjection& local, const Transform3f& localToWorld)
{
    Vec3d uRot;
    Vec3d vRot;
    RotatedAxes(local, uRot, vRot);

    // With linear part M = R * S (TRS), dot(M x + t, Aw) = dot(x, M^T Aw) + dot(t, Aw).
    // Choosing Aw = M^-T u = R(u / S) makes the x term match the local projection
    // exactly; the constant dot(t, Aw) folds into the offset.
    WorldUvProjection out;
    out.AxisU = localToWorld.Rotation.RotateVector(DivScale(uRot, localToWorld.Scale));
    out.AxisV = localToWorld.Rotation.RotateVector(DivScale(vRot, localToWorld.Scale));
    out.Scale = local.Scale;
    out.Offset = Vec2d{
        local.Offset.X - localToWorld.Position.Dot(out.AxisU) / SafeScale(local.Scale.X),
        local.Offset.Y - localToWorld.Position.Dot(out.AxisV) / SafeScale(local.Scale.Y),
    };
    return out;
}

UvProjection UvProjectionToLocal(const WorldUvProjection& world, const Transform3f& localToWorld)
{
    // The local axis equivalent to a world axis is M^T Aw = S * R^-1(Aw): the
    // exact adjoint of the ToWorld mapping, so the round trip is identity.
    const Quatf inverseRotation = localToWorld.Rotation.Inverse();
    UvProjection out;
    out.AxisU = MulScale(inverseRotation.RotateVector(world.AxisU), localToWorld.Scale);
    out.AxisV = MulScale(inverseRotation.RotateVector(world.AxisV), localToWorld.Scale);
    out.Scale = world.Scale;
    out.Rotation = 0.0f;
    out.WorldAligned = true;
    out.Offset = Vec2d{
        world.Offset.X + localToWorld.Position.Dot(world.AxisU) / SafeScale(world.Scale.X),
        world.Offset.Y + localToWorld.Position.Dot(world.AxisV) / SafeScale(world.Scale.Y),
    };
    return out;
}

WorldUvProjection WorldUvProjectionFit(const WorldUvProjection& p, std::span<const Vec3d> worldPositions)
{
    WorldUvProjection out = p;
    WorldUvProjection raw = p;
    raw.Scale = { 1.0f, 1.0f };
    raw.Offset = { 0.0f, 0.0f };
    const RawBounds b = ComputeRawBoundsWith(
        [&](Vec3d pos) { return ProjectWorldUv(raw, pos); }, worldPositions);
    if (b.Valid)
        ApplyFitFromBounds(b, out.Scale, out.Offset);
    return out;
}

WorldUvProjection WorldUvProjectionCenter(const WorldUvProjection& p, std::span<const Vec3d> worldPositions)
{
    WorldUvProjection out = p;
    WorldUvProjection raw = p;
    raw.Scale = { 1.0f, 1.0f };
    raw.Offset = { 0.0f, 0.0f };
    const RawBounds b = ComputeRawBoundsWith(
        [&](Vec3d pos) { return ProjectWorldUv(raw, pos); }, worldPositions);
    if (b.Valid)
        ApplyCenterFromBounds(b, out.Scale, out.Offset);
    return out;
}
