#include "ViewportProjection.h"

#include "EditorViewport.h"

#include <cmath>

namespace
{
Vec2d ToVec2(ImVec2 v) { return Vec2d(v.x, v.y); }
ImVec2 ToImVec2(Vec2d v) { return ImVec2{ v.X, v.Y }; }
}

ViewportProjection::ViewportProjection(const EditorViewport& viewport)
{
    Region = ViewportMath::Rect{ ToVec2(viewport.RegionMin), ToVec2(viewport.RegionMax) };
    ViewProjection = viewport.BuildRenderData().ViewProjection;
    InverseViewProjection = ViewProjection.Inverse();

    // Screen +X as a world direction: difference of two near-plane unprojections.
    // Exact for ortho, a good approximation for perspective (used only to measure
    // pixels-per-world for screen-constant sizing).
    const Vec2d center{ (Region.Min.X + Region.Max.X) * 0.5f, (Region.Min.Y + Region.Max.Y) * 0.5f };
    const Ray3d r0 = ViewportMath::RayThroughPixel(InverseViewProjection, Region, center);
    const Ray3d r1 = ViewportMath::RayThroughPixel(InverseViewProjection, Region, Vec2d(center.X + 10.0f, center.Y));
    const Vec3d delta = r1.Origin - r0.Origin;
    ScreenRight = delta.SqrMagnitude() > 0.0f ? delta.Normalized() : Vec3d(1.0f, 0.0f, 0.0f);
}

Ray3d ViewportProjection::RayThroughPixel(ImVec2 pixel) const
{
    return ViewportMath::RayThroughPixel(InverseViewProjection, Region, ToVec2(pixel));
}

std::optional<ProjectedPoint> ViewportProjection::WorldToPixel(Vec3d world) const
{
    const std::optional<ViewportMath::Projected> p =
        ViewportMath::WorldToPixel(ViewProjection, Region, world);
    if (!p.has_value())
        return std::nullopt;
    return ProjectedPoint{ .Pixel = ToImVec2(p->Pixel), .Depth = p->Depth };
}

float ViewportProjection::WorldSizeForPixels(Vec3d at, float pixels) const
{
    const std::optional<ProjectedPoint> a = WorldToPixel(at);
    const std::optional<ProjectedPoint> b = WorldToPixel(at + ScreenRight);
    if (!a.has_value() || !b.has_value())
        return pixels * 0.01f;

    const float dx = b->Pixel.x - a->Pixel.x;
    const float dy = b->Pixel.y - a->Pixel.y;
    const float pixelsPerWorld = std::sqrt(dx * dx + dy * dy);
    if (pixelsPerWorld <= 1.0e-6f)
        return pixels * 0.01f;
    return pixels / pixelsPerWorld;
}

float ViewportProjection::DistancePointToSegment(ImVec2 p, ImVec2 a, ImVec2 b)
{
    return ViewportMath::DistancePointToSegment(ToVec2(p), ToVec2(a), ToVec2(b));
}
