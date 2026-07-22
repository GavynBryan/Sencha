#pragma once

#include "DocumentCookSnapshot.h"

#include <assets/cook/DirectLightBake.h>
#include <math/geometry/3d/Aabb3d.h>

#include <span>
#include <vector>

// The bake's world extent and the neighbor-zone occluders within probe-ray reach
// of it. Only zones a bake ray could reach participate, so a neighbor edit outside
// reach does not restale this zone. Both the document fingerprint (which folds the
// reachable set) and the occlusion BVH consume this same set.
struct DocumentBakeExtent
{
    std::vector<const ProbeHaloZone*> ReachableHalo;
    Aabb3d                            Bounds = Aabb3d::Empty();
};

// Empty bounds and no reachable halo when nothing bakes or no halo was offered:
// the occlusion assembly then adds no halo geometry, matching a bake with no
// neighbors.
[[nodiscard]] inline DocumentBakeExtent SelectDocumentBakeExtent(
    const DocumentCookSnapshot& snapshot)
{
    DocumentBakeExtent extent;
    const std::span<const ProbeHaloZone> halo = snapshot.Halo;
    if ((snapshot.BakeLights.empty() && snapshot.ProbeVolumes.empty()) || halo.empty())
        return extent;

    for (const CookBrushGeometry& brush : snapshot.Brushes)
        extent.Bounds.ExpandToInclude(brush.WorldBounds);
    for (const LightmapPlacement& placement : snapshot.Placements)
        if (placement.CastsIntoBake)
            for (const StaticMeshVertex& vertex : placement.Geometry.Vertices)
                extent.Bounds.ExpandToInclude(
                    placement.ToWorld.TransformPoint(vertex.Position));
    for (const ProbeVolumeInput& volume : snapshot.ProbeVolumes)
        extent.Bounds.ExpandToInclude(volume.Grid.Bounds());
    extent.ReachableHalo = SelectProbeHaloZones(extent.Bounds, halo,
                                                snapshot.Lighting.Probe.MaxRayDistance);
    return extent;
}
