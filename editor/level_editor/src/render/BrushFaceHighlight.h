#pragma once

#include "brush/BrushEvaluation.h"

#include <math/Vec.h>

#include <cstdint>
#include <vector>

//=============================================================================
// BrushFaceHighlight — the local-space geometry that lights one source face on
// every evaluated mesh that carries it: resolved per source face x distinct
// mesh through the evaluation's element maps, then drawn instanced over that
// mesh's placements. Never per piece. Positions only; the renderer stamps the
// colour when it uploads, so one build serves every viewport and both passes.
//=============================================================================
struct BrushFaceHighlightMesh
{
    std::uint32_t      MeshIndex = 0;
    std::vector<Vec3d> Outline; // local, two per edge, over every evaluated face mapping to the source face
    std::vector<Vec3d> Fill;    // local, three per triangle (BrushTessellateFace)
};

struct BrushFaceHighlight
{
    std::uint64_t                       TopologyRevision = 0; // what it was built for
    std::uint32_t                       SourceFace = 0;
    std::vector<BrushFaceHighlightMesh> Meshes;               // one per distinct mesh that carries the face
};

[[nodiscard]] BrushFaceHighlight BuildBrushFaceHighlight(const BrushEvaluated& evaluated,
                                                         std::uint32_t sourceFace,
                                                         std::uint64_t topologyRevision);
