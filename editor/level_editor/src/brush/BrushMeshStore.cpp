#include "BrushMeshStore.h"

#include <core/hash/Fnv1a.h>

#include <algorithm>
#include <utility>

BrushId BrushMeshStore::Create(BrushMesh mesh, BrushModifierStack modifiers)
{
    const BrushId id{ NextId++ };
    Place(id, BrushRecord{ std::move(mesh), std::move(modifiers) });
    return id;
}

BrushId BrushMeshStore::Create(const BrushRecord& record)
{
    const BrushId id{ NextId++ };
    Place(id, record);
    return id;
}

void BrushMeshStore::Set(BrushId id, BrushMesh mesh)
{
    if (!id.IsValid())
        return;
    BrushRecord record;
    if (const BrushRecord* existing = FindRecord(id))
        record.Modifiers = existing->Modifiers;
    record.Mesh = std::move(mesh);
    Place(id, std::move(record));
}

void BrushMeshStore::Set(BrushId id, const BrushRecord& record)
{
    if (!id.IsValid())
        return;
    Place(id, record);
}

void BrushMeshStore::SetModifiers(BrushId id, BrushModifierStack modifiers)
{
    const BrushRecord* existing = FindRecord(id);
    if (existing == nullptr)
        return;
    BrushRecord record;
    record.Mesh = existing->Mesh;
    record.Modifiers = std::move(modifiers);
    Place(id, std::move(record));
}

void BrushMeshStore::Place(BrushId id, BrushRecord record)
{
    // Revisions are store-wide monotonic, so a record re-seated at a freed id
    // can never repeat a revision an evaluation was cached under. The domain
    // revisions advance only when their signature differs from the record
    // being replaced; a fresh id starts every domain at the new revision.
    const std::uint64_t revision = NextRevision++;
    record.Revision = revision;
    const std::uint64_t geometry = BrushGeometrySignature(record.Mesh);
    const std::uint64_t material = BrushMaterialSignature(record.Mesh);
    const BrushModifierSignatures stack = BrushStackSignatures(record.Modifiers);
    std::uint64_t topology = geometry;
    HashFnv1aValue(topology, stack.Topology);
    std::uint64_t placement = geometry;
    HashFnv1aValue(placement, stack.Placement);

    const auto previous = Records.find(id.Value);
    if (previous == Records.end())
    {
        record.TopologyRevision = revision;
        record.PlacementRevision = revision;
        record.MaterialRevision = revision;
    }
    else
    {
        const DomainSignatures& before = Signatures[id.Value];
        record.TopologyRevision = before.Topology == topology ? previous->second.TopologyRevision : revision;
        record.PlacementRevision = before.Placement == placement ? previous->second.PlacementRevision : revision;
        record.MaterialRevision = before.Material == material ? previous->second.MaterialRevision : revision;
    }
    Signatures[id.Value] = DomainSignatures{ topology, placement, material };
    Records[id.Value] = std::move(record);
    Evaluations.erase(id.Value);
    NextId = std::max(NextId, id.Value + 1); // keep Create() ids unique after a load
}

const BrushMesh* BrushMeshStore::Find(BrushId id) const
{
    const BrushRecord* record = FindRecord(id);
    return record != nullptr ? &record->Mesh : nullptr;
}

const BrushModifierStack* BrushMeshStore::FindModifiers(BrushId id) const
{
    const BrushRecord* record = FindRecord(id);
    return record != nullptr ? &record->Modifiers : nullptr;
}

const BrushRecord* BrushMeshStore::FindRecord(BrushId id) const
{
    auto it = Records.find(id.Value);
    return it == Records.end() ? nullptr : &it->second;
}

const BrushEvaluated* BrushMeshStore::Evaluated(BrushId id,
                                                const BrushEvaluationPolicy& policy) const
{
    const BrushRecord* record = FindRecord(id);
    if (record == nullptr)
        return nullptr;
    CachedEvaluation& cached = Evaluations[id.Value];
    if (cached.Revision != record->Revision || !(cached.Policy == policy))
    {
        cached.Value = EvaluateBrushModifiers(record->Mesh, record->Modifiers, policy);
        cached.Revision = record->Revision;
        cached.Policy = policy;
    }
    return &cached.Value;
}

void BrushMeshStore::Destroy(BrushId id)
{
    Records.erase(id.Value);
    Evaluations.erase(id.Value);
    Signatures.erase(id.Value);
}

void BrushMeshStore::Clear()
{
    Records.clear();
    Evaluations.clear();
    Signatures.clear();
    NextId = 1;
}
