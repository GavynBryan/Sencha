#pragma once

#include "BrushBakeCache.h"
#include "brush/BrushEvaluation.h"
#include "brush/BrushId.h"
#include "document/BrushPlacementFacts.h"

#include <core/assets/AssetRef.h>
#include <ecs/EntityId.h>
#include <math/Mat.h>
#include <math/geometry/3d/Aabb3d.h>
#include <math/geometry/3d/Transform3d.h>
#include <render/Material.h>
#include <render/static_mesh/StaticMeshHandle.h>

#include <cstdint>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

class EditorScene;

//=============================================================================
// BrushDrawSet — the retained render representation of a document's brushes.
//
// One record per visible brush entity: its distinct baked meshes, every piece's
// world matrix and bounds, the edge geometry the wireframe instances over those
// placements, and a content digest. Placements, bounds and edge topology come
// from the scene's placement facts and the evaluation; this layer adds only
// what rendering needs (GPU handles, materials, instance rows). A record is
// rebuilt only when the entity's BrushPlacementKey or its baked fingerprint
// changes; an unchanged frame costs one key compare per entity and recomputes
// no geometry.
//=============================================================================

struct BrushEdgeVertex
{
    Vec3d Position; // brush-local
    bool  Soft = false;
};

struct BrushDrawMesh // one distinct mesh of the entity, owned by value
{
    StaticMeshHandle             Handle;
    std::vector<MaterialHandle>  SlotMaterials; // copied from the bake cache at rebuild
    Aabb3d                       LocalBounds = Aabb3d::Empty();
    std::uint64_t                ContentHash = 0;
    std::vector<BrushEdgeVertex> Edges;         // two per edge
};

struct BrushPlacement
{
    Mat4   World = Mat4::Identity();
    Aabb3d WorldBounds = Aabb3d::Empty(); // vertex-tight
};

struct BrushMeshRun // placements of one mesh, contiguous
{
    std::uint32_t MeshIndex = 0;
    std::uint32_t FirstPlacement = 0;
    std::uint32_t PlacementCount = 0;
};

struct BrushDrawEntity
{
    BrushPlacementKey           Key;
    std::uint64_t               BakedFingerprint = 0; // handles + default material; placements redo, edges do not
    std::vector<BrushDrawMesh>  Meshes;               // aligned with BrushEvaluated::Meshes
    std::vector<BrushMeshRun>   Runs;
    std::vector<BrushPlacement> Placements;           // grouped by run
    std::vector<Mat4>           InstanceRows;         // World by rows (Data[r] = row r), aligned with Placements
    std::uint32_t               SourceSlot = 0;       // Placements index of the Origin == Source piece
    std::uint64_t               Digest = 0;           // content + placements, folded once
};

class BrushDrawSet
{
public:
    // One pass over the scene's visible brush entities. An entity whose key or
    // fingerprint differs from its retained record is rebuilt; new entities are
    // added; entities no longer visible or present are dropped. Returns true
    // when anything changed, so flat per-document emissions can be redone.
    bool Refresh(const EditorScene& scene, BrushBakeCache& bakes, const AssetRef& levelDefault);

    [[nodiscard]] std::uint64_t Version() const { return Version_; }       // bumps when Refresh returned true
    [[nodiscard]] std::uint64_t ContentDigest() const { return Digest_; }  // fold of entity digests
    [[nodiscard]] std::span<const BrushDrawEntity> Entities() const { return Records; } // scene entity order
    [[nodiscard]] const BrushDrawEntity* Find(EntityId entity) const;
    [[nodiscard]] std::size_t EntityRebuildCount() const { return Rebuilds; }

private:
    void RebuildPlacements(BrushDrawEntity& record, const BrushEvaluated& evaluated,
                           const BrushBakedRecord& baked,
                           std::span<const Transform3f> pieceWorld,
                           std::span<const Aabb3d> pieceBounds);
    static void RebuildEdges(BrushDrawEntity& record, const BrushEvaluated& evaluated);
    void RefreshDigest();

    std::vector<BrushDrawEntity> Records;
    std::vector<BrushDrawEntity> Scratch; // reused between refreshes
    std::unordered_map<std::uint64_t, std::size_t> IndexOf; // entity key -> Records index
    std::uint64_t Version_ = 0;
    std::uint64_t Digest_ = 0;
    std::size_t Rebuilds = 0;
};
