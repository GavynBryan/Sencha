#pragma once

#include "ViewportMath.h"

#include <math/Mat.h>
#include <math/Vec.h>
#include <math/geometry/3d/Ray3d.h>

#include <imgui.h>

#include <optional>

struct EditorViewport;

// A world point projected into a viewport: pixel position plus clip-space depth
// (W) for front-to-back tie-breaking.
struct ProjectedPoint
{
    ImVec2 Pixel = {};
    float Depth = 0.0f;
};

// GUI-side adapter over ViewportMath: pulls the view-projection and pixel rect
// from an EditorViewport once, then delegates the math (which is unit-tested in
// isolation). Built once per viewport per use; caches the matrices so repeated
// projections are cheap. (docs/architecture/hardening-and-consolidation.md W2)
class ViewportProjection
{
public:
    explicit ViewportProjection(const EditorViewport& viewport);

    [[nodiscard]] Ray3d RayThroughPixel(ImVec2 pixel) const;
    [[nodiscard]] std::optional<ProjectedPoint> WorldToPixel(Vec3d world) const;
    [[nodiscard]] float WorldSizeForPixels(Vec3d at, float pixels) const;

    [[nodiscard]] static float DistancePointToSegment(ImVec2 p, ImVec2 a, ImVec2 b);

private:
    ViewportMath::Rect Region = {};
    Mat4 ViewProjection = {};
    Mat4 InverseViewProjection = {};
    Vec3d ScreenRight = {}; // unit world direction parallel to screen +X
};
