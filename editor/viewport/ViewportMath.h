#pragma once

#include <math/Mat.h>
#include <math/Vec.h>
#include <math/geometry/3d/Ray3d.h>

#include <algorithm>
#include <cmath>
#include <optional>

// Pure viewport projection math — no ImGui, no EditorViewport, no Vulkan. The GUI
// wrapper (ViewportProjection) feeds it a view-projection matrix and a pixel rect;
// this is the part worth unit-testing (the bugs lived here: ray/pixel mapping,
// screen-space distance). Vec2d is the engine's float-backed 2D vector.
// (docs/architecture/hardening-and-consolidation.md W2)
namespace ViewportMath
{
inline constexpr double kParallelEpsilon = 1.0e-8;

// A viewport's pixel rectangle (top-left Min, bottom-right Max).
struct Rect
{
    Vec2d Min = {};
    Vec2d Max = {};

    [[nodiscard]] float Width() const { return Max.X - Min.X; }
    [[nodiscard]] float Height() const { return Max.Y - Min.Y; }
};

// A world point projected to pixels, with clip-space W as depth for tie-breaking.
struct Projected
{
    Vec2d Pixel = {};
    float Depth = 0.0f;
};

// Camera ray through a pixel. `invViewProj` is the inverse of the view-projection.
// Vulkan NDC: near z = 0, far z = 1; no Y flip (matches the render path).
[[nodiscard]] inline Ray3d RayThroughPixel(const Mat4& invViewProj, const Rect& region, Vec2d pixel)
{
    if (region.Width() <= 0.0f || region.Height() <= 0.0f)
        return {};

    const float localX = (pixel.X - region.Min.X) / region.Width();
    const float localY = (pixel.Y - region.Min.Y) / region.Height();
    const float clipX = localX * 2.0f - 1.0f;
    const float clipY = localY * 2.0f - 1.0f;

    const Vec4 nearClip(clipX, clipY, 0.0f, 1.0f);
    const Vec4 farClip(clipX, clipY, 1.0f, 1.0f);

    Vec4 nearWorld = invViewProj * nearClip;
    Vec4 farWorld = invViewProj * farClip;
    nearWorld /= nearWorld.W;
    farWorld /= farWorld.W;

    const Vec3d origin(nearWorld.X, nearWorld.Y, nearWorld.Z);
    const Vec3d target(farWorld.X, farWorld.Y, farWorld.Z);
    return Ray3d(origin, (target - origin).Normalized());
}

// World point -> pixel + depth; nullopt when behind the camera.
[[nodiscard]] inline std::optional<Projected> WorldToPixel(const Mat4& viewProj, const Rect& region, Vec3d world)
{
    const Vec4 clip = viewProj * Vec4(static_cast<float>(world.X),
                                      static_cast<float>(world.Y),
                                      static_cast<float>(world.Z),
                                      1.0f);
    if (clip.W <= kParallelEpsilon)
        return std::nullopt;

    const float ndcX = clip.X / clip.W;
    const float ndcY = clip.Y / clip.W;
    return Projected{
        .Pixel = Vec2d(region.Min.X + (ndcX * 0.5f + 0.5f) * region.Width(),
                       region.Min.Y + (ndcY * 0.5f + 0.5f) * region.Height()),
        .Depth = clip.W,
    };
}

[[nodiscard]] inline float DistancePointToSegment(Vec2d p, Vec2d a, Vec2d b)
{
    const float abx = b.X - a.X;
    const float aby = b.Y - a.Y;
    const float lengthSq = abx * abx + aby * aby;
    float t = 0.0f;
    if (lengthSq > 0.0f)
        t = std::clamp(((p.X - a.X) * abx + (p.Y - a.Y) * aby) / lengthSq, 0.0f, 1.0f);
    const float dx = p.X - (a.X + t * abx);
    const float dy = p.Y - (a.Y + t * aby);
    return std::sqrt(dx * dx + dy * dy);
}
} // namespace ViewportMath
