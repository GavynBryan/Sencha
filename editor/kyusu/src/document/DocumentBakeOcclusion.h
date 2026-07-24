#pragma once

#include "BakeTriangleGather.h"
#include "CellArtifactCook.h"
#include "DocumentCookSnapshot.h"

#include <assets/cook/BakeBvh.h>
#include <assets/cook/DirectLightBake.h>
#include <math/Mat.h>
#include <math/geometry/3d/Aabb3d.h>

#include <span>
#include <utility>
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

// One occlusion BVH over every cell's world triangles plus placed meshes that
// cast into the bake and any neighbor-zone halo within reach. It serves both the
// direct-light and probe bakes: a light in one cell shadows onto its neighbors,
// and probe rays see the whole document plus reachable halo.
[[nodiscard]] inline BakeBvh BuildDocumentOcclusionBvh(
    const std::vector<PendingCellMesh>& meshes, const DocumentCookSnapshot& snapshot,
    const DocumentBakeExtent& extent)
{
    std::vector<BakeTriangle> occluders;
    for (const PendingCellMesh& pending : meshes)
        GatherWorldTriangles(pending.Geometry, Mat4::MakeTranslation(pending.Origin),
                             occluders);
    // Placed meshes occlude too (and shadow each other) unless authored out via
    // AffectsBakedLighting.
    for (const LightmapPlacement& placement : snapshot.Placements)
        if (placement.CastsIntoBake)
            GatherWorldTriangles(placement.Geometry, placement.ToWorld, occluders);

    BakeBvh bvh;
    bvh.Build(AssembleProbeBakeTriangles(std::move(occluders), extent.Bounds,
                                         snapshot.Halo,
                                         snapshot.Lighting.Probe.MaxRayDistance));
    return bvh;
}
