#pragma once

#include "DocumentCook.h"

#include <assets/cook/BrushClustering.h>
#include <core/json/JsonValue.h>
#include <math/Vec.h>
#include <assets/static_mesh/MeshGeometry.h>

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

struct DocumentCookContext;

// A cell mesh staged but not yet written: the lighting bake needs every cell's
// geometry (for the shared occlusion BVH) before any cell mesh is finalized on
// disk, so the writer holds the geometry until the bake completes. Origin is the
// cell's world translation.
struct PendingCellMesh
{
    std::filesystem::path Physical;
    MeshGeometry          Geometry;
    Vec3d                 Origin;
};

// One cell's cooked collision: the blob path (relative to the cooked root) and
// the cell origin the runtime places the static collider at.
struct CellCollisionEntry
{
    std::string BlobRelPath;
    Vec3d       Origin;
};

// Bakes each clustered cell into a render mesh (staged, held in `meshes`) and its
// scene StaticMesh entity (`entities`), and, when `emitCollision`, a pre-baked
// collision blob (`collision`). Records mesh, material, and collision membership
// in the context's catalog and mesh asset paths on its result. Owns the
// RenderMeshes (and, when emitting, Collision) progress steps and the per-cell
// cancellation check. Returns false with the context result carrying the error
// or cancellation.
[[nodiscard]] bool EmitCellArtifacts(
    const DocumentCookContext& ctx,
    const std::vector<BrushCell>& cells,
    bool emitCollision,
    std::vector<PendingCellMesh>& meshes,
    JsonValue::Array& entities,
    std::vector<CellCollisionEntry>& collision);
