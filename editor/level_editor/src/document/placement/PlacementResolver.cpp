#include "PlacementResolver.h"

#include "viewport/GridSettings.h"
#include "viewport/ViewportOrientation.h"

std::optional<PlacementSite> ResolvePlacementSite(const std::optional<SurfaceHit>& surface,
                                                  const std::optional<Vec3d>& gridPoint, const GridSettings& grid)
{
    // On a surface, snapped across it at its own height, on the working lattice.
    if (surface.has_value())
        return UprightSite(GridThrough(GridForAxis(surface->Normal), surface->Point, grid).Snap(surface->Point));
    if (gridPoint.has_value())
        return UprightSite(*gridPoint);
    return std::nullopt;
}

std::optional<PlacementSite> PlacementSiteAt(const PickingService& picking, const EditorViewport& viewport,
                                             Vec2d pixel, const EditorScene& scene, const GridSettings& grid)
{
    if (const std::optional<SurfaceHit> surface = picking.PickSurface(viewport, pixel, scene))
        return ResolvePlacementSite(surface, std::nullopt, grid);
    return ResolvePlacementSite(std::nullopt, picking.ProjectPointToGrid(viewport, pixel, grid), grid);
}
