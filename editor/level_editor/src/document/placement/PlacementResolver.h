#pragma once

#include "PlacementSite.h"

#include "viewport/Picking.h"

#include <math/Vec.h>

#include <optional>

class EditorScene;
struct EditorViewport;
struct GridSettings;

// Where a placement belongs, given what is under the pointer: on the nearest
// surface, snapped across it on `grid`'s lattice at the surface's height, else
// at the (already snapped) grid point; upright. Nullopt when there is neither.
[[nodiscard]] std::optional<PlacementSite> ResolvePlacementSite(const std::optional<SurfaceHit>& surface,
                                                                const std::optional<Vec3d>& gridPoint,
                                                                const GridSettings& grid);

[[nodiscard]] std::optional<PlacementSite> PlacementSiteAt(const PickingService& picking,
                                                           const EditorViewport& viewport, Vec2d pixel,
                                                           const EditorScene& scene, const GridSettings& grid);
