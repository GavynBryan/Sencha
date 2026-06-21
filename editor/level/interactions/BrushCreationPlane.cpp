#include "BrushCreationPlane.h"

#include "../BrushGeometry.h"
#include "../LevelScene.h"
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

    // 2. Depth extent: match a selected brush (ortho QoL) > rest on the hit
    //    surface (perspective) > rest on the grid toward the camera.
    std::optional<Aabb3d> selectedBounds;
    if (!perspective)
    {
        const SelectableRef sel = ctx.Selection.GetPrimarySelection();
        if (sel.IsEntity() && ctx.Scene.TryGetBrush(sel.Entity) != nullptr)
            selectedBounds = BrushGeometry::ComputeWorldBounds(ctx.Scene, sel.Entity);
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
    else
    {
        const float toward = perspective ? (viewport.Camera.Position[depthAxis] - plane.Origin[depthAxis])
                                         : traits.OrthoAxis[depthAxis];
        depth = RestOnGridDepth(plane.Origin[depthAxis], ctx.Grid.Spacing, toward);
    }

    return BrushCreationPlane{
        .Plane = plane,
        .Anchor = *anchor,
        .DepthAxis = depthAxis,
        .DepthCenter = depth.Center,
        .DepthHalf = depth.Half,
    };
}
