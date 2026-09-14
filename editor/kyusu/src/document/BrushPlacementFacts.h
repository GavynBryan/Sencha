#pragma once

#include "brush/BrushEvaluation.h"
#include "brush/BrushId.h"
#include "meshedit/MeshElements.h"

#include <ecs/EntityId.h>
#include <math/geometry/3d/Aabb3d.h>
#include <math/geometry/3d/Transform3d.h>

#include <cstdint>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

class EditorScene;

//=============================================================================
// BrushPlacementFacts — every world-space fact about a brush entity that
// follows from its evaluation and its transform, retained per entity and
// rebuilt only when the entity's placement key changes.
//
// Consumers query; they do not recombine pieces and transforms themselves.
// Each query validates the entity's key on access, so a command that moves a
// brush and reads its bounds in the same frame sees the truth; Refresh() only
// drops entities that left. Hidden entities keep their record (visibility is
// a consumer filter), so hide and show rebuild nothing.
//
// Source world elements (the authored mesh's edges, vertices and faces in
// world space, the targets of element picking and editing) are built on first
// request and keyed on topology and transform alone, so a placement-only
// change such as an Array count keeps them.
//=============================================================================

// Identity of an entity's placements. Equal keys mean every retained piece
// transform, bound and element list is still exact. Built by one function so
// every retained layer agrees on what "changed" is.
struct BrushPlacementKey
{
    EntityId      Entity;
    BrushId       Brush;
    std::uint64_t TopologyRevision = 0;  // BrushRecord::TopologyRevision
    std::uint64_t PlacementRevision = 0; // BrushRecord::PlacementRevision
    std::uint32_t MaxPieces = 0;         // the interactive policy the evaluation ran under
    Transform3f   World;                 // WorldTransform after RefreshDerivedTransforms; exact compare

    bool operator==(const BrushPlacementKey&) const = default;
};

// nullopt when the entity has no live brush record or no world transform.
[[nodiscard]] std::optional<BrushPlacementKey> MakeBrushPlacementKey(const EditorScene& scene,
                                                                     EntityId entity);

class BrushPlacementFacts
{
public:
    explicit BrushPlacementFacts(const EditorScene& scene);

    [[nodiscard]] std::optional<Aabb3d>        GetEntityBounds(EntityId entity) const; // union of every piece
    [[nodiscard]] std::optional<Aabb3d>        GetSourceBounds(EntityId entity) const; // the authored piece alone
    [[nodiscard]] std::span<const Transform3f> GetPiecePlacements(EntityId entity) const; // entity * placement, per piece
    [[nodiscard]] std::span<const Aabb3d>      GetPieceBounds(EntityId entity) const;     // vertex-tight, per piece
    [[nodiscard]] const BrushEvaluated*        GetEvaluation(EntityId entity) const;      // provenance, meshes, maps
    [[nodiscard]] const SourceWorldElements*   GetSourceWorldElements(EntityId entity) const;
    [[nodiscard]] std::optional<std::uint32_t> SourceEdgeIndexOf(EntityId entity, std::uint32_t a,
                                                                 std::uint32_t b) const;
    [[nodiscard]] std::optional<BrushPlacementKey> KeyOf(EntityId entity) const;

    // Drops records for entities that no longer have a live brush. Called once
    // per frame by the scene; never needed for correctness.
    void Refresh();
    void Clear();

    [[nodiscard]] std::size_t RebuildCount() const { return Rebuilds; }

private:
    struct Record
    {
        BrushPlacementKey        Key;
        const BrushEvaluated*    Evaluated = nullptr;
        std::vector<Transform3f> PieceWorld;
        std::vector<Aabb3d>      PieceWorldBounds;
        Aabb3d                   UnionWorldBounds = Aabb3d::Empty();
        Aabb3d                   SourceWorldBounds = Aabb3d::Empty();
        // Built on demand; kept across placement-only rebuilds.
        std::optional<SourceWorldElements> Elements;
        std::uint64_t            ElementsTopology = 0;
        Transform3f              ElementsWorld;
    };

    [[nodiscard]] const Record* Ensure(EntityId entity) const;
    void Rebuild(Record& record, const BrushPlacementKey& key) const;

    const EditorScene& Scene;
    mutable std::unordered_map<std::uint64_t, Record> Records;
    mutable std::size_t Rebuilds = 0;
};
