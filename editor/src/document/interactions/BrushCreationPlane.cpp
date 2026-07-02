#include "BrushCreationPlane.h"

#include "../EditorScene.h"
#include "../../selection/SelectionService.h"
#include "../../tools/ToolContext.h"
#include "../../viewport/EditorViewport.h"
#include "../../viewport/Picking.h"
#include "../../viewport/ViewportOrientation.h"

#include <cmath>

namespace
{
int DominantAxis(const Vec3d& v)
{
    const float ax = std::abs(v.X);
    const float ay = std::abs(v.Y);
    const float az = std::abs(v.Z);
    if (ax >= ay && ax >= az)
        return 0;
    if (ay >= az)
        return 1;
    return 2;
}

Vec3d UnitAxis(int axis)
{
    return Vec3d(axis == 0 ? 1.0f : 0.0f, axis == 1 ? 1.0f : 0.0f, axis == 2 ? 1.0f : 0.0f);
}

bool IsSignedWorldAxis(const Vec3d& v)
{
    constexpr float kEps = 1e-5f;
    int units = 0;
    int zeros = 0;
    for (int i = 0; i < 3; ++i)
    {
        const float a = std::abs(v[i]);
        if (std::abs(a - 1.0f) <= kEps)
            ++units;
        else if (a <= kEps)
            ++zeros;
    }
    return units == 1 && zeros == 2;
}
}

std::optional<BrushCreationPlane>
ResolveBrushCreationPlane(const ToolContext& ctx, const EditorViewport& viewport, ImVec2 pressPos)
{
    const OrientationTraits& traits = viewport.GetOrientationTraits();
    const bool perspective = traits.Mode == EditorCamera::Mode::Perspective;

    // 1. Plane + depth axis. In perspective, drawing onto geometry rests on that
    //    surface; otherwise the cursor draws on the orientation grid.
    GridPlane plane;
    int depthAxis = 1;
    std::optional<SurfaceHit> surface;
    if (perspective)
    {
        surface = ctx.Picking.PickSurface(viewport, pressPos, ctx.Scene);
        if (surface.has_value())
        {
            depthAxis = DominantAxis(surface->Normal);
            plane = GridForAxis(UnitAxis(depthAxis));
            plane.Origin = surface->Point;
            plane.Spacing = ctx.Grid.Spacing;
            plane.SnapEnabled = ctx.Grid.SnapEnabled;
        }
    }
    if (!surface.has_value())
    {
        plane = viewport.GetGrid(ctx.Grid);
        depthAxis = DominantAxis(plane.AxisU.Cross(plane.AxisV));
    }

    const std::optional<Vec3d> anchor = ctx.Picking.ProjectPointToPlane(viewport, pressPos, plane);
    if (!anchor.has_value())
        return std::nullopt;

    // A plane whose axes are all signed world axes takes the identity-transform
    // path (world-component extents, exactly the pre-frame behavior); a custom
    // grid frame takes the rotated path with depth along the plane normal.
    const bool frameAligned = IsSignedWorldAxis(plane.AxisU) && IsSignedWorldAxis(plane.AxisV);
    Vec3d depthDir = UnitAxis(depthAxis);
    if (!frameAligned)
    {
        const Vec3d normal = plane.AxisU.Cross(plane.AxisV);
        depthDir = normal.SqrMagnitude() > 0.0f ? normal.Normalized() : UnitAxis(depthAxis);
    }

    // 2. Depth extent: match a selected brush (ortho QoL, world-aligned only:
    //    an AABB has no extent along a rotated normal) > rest on the hit surface
    //    (perspective) > rest on the grid toward the camera.
    std::optional<Aabb3d> selectedBounds;
    if (!perspective && frameAligned)
    {
        const SelectableRef sel = ctx.Selection.GetPrimarySelection();
        if (sel.IsEntity() && ctx.Scene.TryGetBrush(sel.Entity) != nullptr)
            selectedBounds = ctx.Scene.TryGetWorldBounds(sel.Entity);
    }

    BrushDepthPlacement depth;
    if (selectedBounds.has_value())
    {
        depth = CopyDepthFromBounds(*selectedBounds, depthAxis);
    }
    else if (surface.has_value())
    {
        depth = RestOnSurfaceDepth(surface->Point[depthAxis], ctx.Grid.Spacing, surface->Normal[depthAxis]);
    }
    else if (frameAligned)
    {
        const float toward = perspective ? (viewport.Camera.Position[depthAxis] - plane.Origin[depthAxis])
                                         : traits.OrthoAxis[depthAxis];
        depth = RestOnGridDepth(plane.Origin[depthAxis], ctx.Grid.Spacing, toward);
    }
    else
    {
        // Same rest-on-grid rule, measured along the frame normal instead of a
        // world axis: coordinates are dot products with depthDir.
        const float toward = perspective ? (viewport.Camera.Position - plane.Origin).Dot(depthDir)
                                         : traits.OrthoAxis.Dot(depthDir);
        depth = RestOnGridDepth(plane.Origin.Dot(depthDir), ctx.Grid.Spacing, toward);
    }

    return BrushCreationPlane{
        .Plane = plane,
        .Anchor = *anchor,
        .DepthAxis = depthAxis,
        .DepthDir = depthDir,
        .DepthCenter = depth.Center,
        .DepthHalf = depth.Half,
        .FrameAligned = frameAligned,
    };
}
