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

    RawBounds ComputeRawBounds(const UvProjection& p, std::span<const Vec3d> positions)
    {
        RawBounds b;
        b.Min = { std::numeric_limits<float>::max(), std::numeric_limits<float>::max() };
        b.Max = { std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest() };
        for (Vec3d pos : positions)
        {
            const Vec2d c = RawUv(p, pos);
            b.Min.X = std::min(b.Min.X, c.X);
            b.Min.Y = std::min(b.Min.Y, c.Y);
            b.Max.X = std::max(b.Max.X, c.X);
            b.Max.Y = std::max(b.Max.Y, c.Y);
            b.Valid = true;
        }
        return b;
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
        // Dominant world axis of the normal picks the box-mapping plane (Hammer's
        // table). U/V are the other two world axes, chosen so the basis reads
        // upright in the perspective view.
        const float ax = std::abs(normal.X);
        const float ay = std::abs(normal.Y);
        const float az = std::abs(normal.Z);
        if (az >= ax && az >= ay) // facing ±Z: floor/ceiling
        {
            p.AxisU = { 1.0f, 0.0f, 0.0f };
            p.AxisV = { 0.0f, 1.0f, 0.0f };
        }
        else if (ax >= ay) // facing ±X: wall
        {
            p.AxisU = { 0.0f, 1.0f, 0.0f };
            p.AxisV = { 0.0f, 0.0f, 1.0f };
        }
        else // facing ±Y: wall
        {
            p.AxisU = { 1.0f, 0.0f, 0.0f };
            p.AxisV = { 0.0f, 0.0f, 1.0f };
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
    if (!b.Valid)
        return out;

    const float spanU = b.Max.X - b.Min.X;
    const float spanV = b.Max.Y - b.Min.Y;
    if (spanU > 1e-5f) { out.Scale.X = spanU; out.Offset.X = -b.Min.X / spanU; }
    if (spanV > 1e-5f) { out.Scale.Y = spanV; out.Offset.Y = -b.Min.Y / spanV; }
    return out;
}

UvProjection UvProjectionCenter(const UvProjection& p, std::span<const Vec3d> localPositions)
{
    UvProjection out = p;
    const RawBounds b = ComputeRawBounds(p, localPositions);
    if (!b.Valid)
        return out;

    const float midU = (b.Min.X + b.Max.X) * 0.5f;
    const float midV = (b.Min.Y + b.Max.Y) * 0.5f;
    if (std::abs(out.Scale.X) > 1e-6f) out.Offset.X = 0.5f - midU / out.Scale.X;
    if (std::abs(out.Scale.Y) > 1e-6f) out.Offset.Y = 0.5f - midV / out.Scale.Y;
    return out;
}
