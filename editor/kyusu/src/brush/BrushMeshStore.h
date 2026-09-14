#pragma once

#include "BrushEvaluation.h"
#include "BrushId.h"
#include "BrushMesh.h"
#include "BrushModifier.h"
#include "BrushRecord.h"

#include <cstdint>
#include <unordered_map>

//=============================================================================
// BrushMeshStore — owns every brush record (mesh + modifier stack), keyed by
// BrushId, and the evaluated result derived from each. A resource on the
// EditorScene (not an archetype component), so BrushComponent stays trivially
// copyable; serialized as a sidecar alongside the scene.
//
// Every mutation goes through Create/Set/SetModifiers and bumps the record's
// revision, which is what the evaluation cache checks; there is no writable
// access to a stored record. (03-brush-representation.md §2.2)
//=============================================================================
class BrushMeshStore
{
public:
    [[nodiscard]] BrushId Create(BrushMesh mesh, BrushModifierStack modifiers = {});
    [[nodiscard]] BrushId Create(const BrushRecord& record);

    // Insert/replace at a specific id (load and restore preserve ids). Set with
    // a mesh keeps the stack the id already has.
    void Set(BrushId id, BrushMesh mesh);
    void Set(BrushId id, const BrushRecord& record);
    void SetModifiers(BrushId id, BrushModifierStack modifiers);

    [[nodiscard]] const BrushMesh*          Find(BrushId id) const;
    [[nodiscard]] const BrushModifierStack* FindModifiers(BrushId id) const;
    [[nodiscard]] const BrushRecord*        FindRecord(BrushId id) const;

    // The record evaluated under `policy`, or null for an unknown id. Cached
    // beside the record and rebuilt when its revision or the requested policy
    // differs from the cached one, so an idle lookup is a compare, an
    // unmodified brush costs nothing beyond that, and a cook never reuses a
    // preview-limited result. Pointers inside stay valid until the next
    // mutation of that id.
    [[nodiscard]] const BrushEvaluated* Evaluated(BrushId id,
                                                  const BrushEvaluationPolicy& policy) const;

    void Destroy(BrushId id);
    void Clear();

    [[nodiscard]] std::size_t Count() const { return Records.size(); }

    // Iteration for serialization (id -> record).
    [[nodiscard]] const std::unordered_map<std::uint32_t, BrushRecord>& All() const { return Records; }

private:
    struct CachedEvaluation
    {
        std::uint64_t         Revision = 0;
        BrushEvaluationPolicy Policy;
        BrushEvaluated        Value;
    };

    void Place(BrushId id, BrushRecord record);

    // The domain signatures the id's current record was placed with, so the
    // next Place can tell which domains actually changed.
    struct DomainSignatures
    {
        std::uint64_t Topology = 0;
        std::uint64_t Placement = 0;
        std::uint64_t Material = 0;
    };

    std::unordered_map<std::uint32_t, BrushRecord> Records;
    std::unordered_map<std::uint32_t, DomainSignatures> Signatures;
    mutable std::unordered_map<std::uint32_t, CachedEvaluation> Evaluations;
    std::uint32_t NextId = 1; // 0 is the invalid BrushId
    std::uint64_t NextRevision = 1;
};
