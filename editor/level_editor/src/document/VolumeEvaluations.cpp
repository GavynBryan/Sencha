#include "VolumeEvaluations.h"

#include "EditorScene.h"

#include <core/hash/Fnv1a.h>

namespace
{
    std::uint64_t EntityKey(EntityId entity)
    {
        return (static_cast<std::uint64_t>(entity.Index) << 32) | entity.Generation;
    }
}

const BrushEvaluated* VolumeEvaluations::Evaluate(const EditorScene& scene, EntityId entity,
                                                  const BrushEvaluationPolicy& policy)
{
    const BrushComponent* brush = scene.TryGetBrush(entity);
    const BrushRecord* record = brush != nullptr ? scene.GetBrushMeshStore().FindRecord(brush->Id) : nullptr;
    if (record == nullptr)
        return nullptr;
    std::uint64_t inputs = kFnv1aOffsetBasis;
    HashFnv1aValue(inputs, record->Revision);
    HashFnv1aValue(inputs, policy.MaxPieces);
    Entry& entry = Entries[EntityKey(entity)];
    if (entry.Inputs == inputs && !entry.Value.Meshes.empty())
        return &entry.Value;

    entry.Inputs = inputs;
    entry.Value = EvaluateBrushModifiers(record->Mesh, record->Modifiers, policy);
    for (BrushEvaluatedMesh& mesh : entry.Value.Meshes)
        mesh.Role = BrushRole::Volume;
    return &entry.Value;
}

void VolumeEvaluations::Forget(EntityId entity)
{
    Entries.erase(EntityKey(entity));
}
