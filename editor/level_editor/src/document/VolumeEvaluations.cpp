#include "VolumeEvaluations.h"

#include "EditorScene.h"
#include "SpaceComponents.h"

#include "brush/BrushValidation.h"

#include <core/hash/Fnv1a.h>
#include <world/registry/Registry.h>

#include <memory>

namespace
{
    std::uint64_t EntityKey(EntityId entity)
    {
        return (static_cast<std::uint64_t>(entity.Index) << 32) | entity.Generation;
    }

    const BrushRecord* RecordOf(const EditorScene& scene, EntityId entity)
    {
        const BrushComponent* brush = scene.TryGetBrush(entity);
        return brush != nullptr ? scene.GetBrushMeshStore().FindRecord(brush->Id) : nullptr;
    }

    BrushEvaluatedMesh SlabMesh(const BrushMesh& mesh, const ShellSlab& slab)
    {
        BrushEvaluatedMesh evaluated;
        evaluated.Mesh = &mesh;
        evaluated.LocalBounds = BrushComputeBounds(mesh);
        evaluated.Signature = BrushMeshSignature(mesh);
        evaluated.Role = BrushRole::Solid;
        evaluated.ToSource.Faces = BrushElementMapKind::Table;
        evaluated.ToSource.FaceTable.assign(mesh.Faces.size(), kBrushNoElement);
        evaluated.ToSource.FaceTable[slab.InnerFace] = slab.SourceFace;
        evaluated.ToSource.Vertices = BrushElementMapKind::None;
        evaluated.EdgePairs = BrushEdgePairs(mesh);
        evaluated.EdgeSoft.assign(evaluated.EdgePairs.size(), false);
        return evaluated;
    }
}

std::uint64_t VolumeEvaluationInputs(const EditorScene& scene, EntityId entity, const BrushEvaluationPolicy& policy)
{
    const BrushRecord* record = RecordOf(scene, entity);
    if (record == nullptr || scene.RoleOf(entity) != BrushRole::Volume)
        return 0;
    std::uint64_t hash = kFnv1aOffsetBasis;
    HashFnv1aValue(hash, record->Revision);
    HashFnv1aValue(hash, policy.MaxPieces);
    const Shell* shell = scene.GetRegistry().Components.TryGet<Shell>(entity);
    const Transform3f* world = scene.TryGetWorldTransform(entity);
    if (shell != nullptr && world != nullptr)
    {
        HashFnv1aValue(hash, shell->Thickness);
        HashFnv1aValue(hash, world->Position);
        HashFnv1aValue(hash, world->Rotation);
        HashFnv1aValue(hash, world->Scale);
        HashFnv1aValue(hash, scene.OpenFaceMaterial().Id.Value);
        HashFnv1aBytes(hash, scene.OpenFaceMaterial().Path.data(), scene.OpenFaceMaterial().Path.size());
    }
    return hash;
}

const BrushEvaluated* VolumeEvaluations::Evaluate(const EditorScene& scene, EntityId entity,
                                                  const BrushEvaluationPolicy& policy)
{
    const BrushRecord* record = RecordOf(scene, entity);
    if (record == nullptr)
        return nullptr;
    const std::uint64_t inputs = VolumeEvaluationInputs(scene, entity, policy);
    Entry& entry = Entries[EntityKey(entity)];
    if (entry.Inputs == inputs && !entry.Value.Meshes.empty())
        return &entry.Value;

    entry.Inputs = inputs;
    entry.Problems.clear();
    entry.Value = EvaluateBrushModifiers(record->Mesh, record->Modifiers, policy);
    BrushEvaluated& value = entry.Value;
    for (BrushEvaluatedMesh& mesh : value.Meshes)
        mesh.Role = BrushRole::Volume;

    const Shell* shell = scene.GetRegistry().Components.TryGet<Shell>(entity);
    const Transform3f* world = scene.TryGetWorldTransform(entity);
    if (shell == nullptr || world == nullptr)
        return &value;

    // Walls are measured in the world, where up and the thickness mean what
    // they say, and stored in the volume's frame like every other piece.
    BrushMesh inWorld = record->Mesh;
    for (BrushVertex& vertex : inWorld.Vertices)
        vertex.Position = world->TransformPoint(vertex.Position);
    ShellRules rules;
    rules.Thickness = shell->Thickness;
    rules.OpenFaceMaterial = scene.OpenFaceMaterial();
    ShellResult walls = GenerateShell(inWorld, rules);
    entry.Problems = std::move(walls.Problems);

    for (ShellSlab& slab : walls.Slabs)
    {
        for (BrushVertex& vertex : slab.Mesh.Vertices)
            vertex.Position = world->InverseTransformPoint(vertex.Position);
        (void)BrushValidateAndRepair(slab.Mesh, 0.0f);
        const BrushMesh& mesh = *value.Generated.emplace_back(std::make_unique<BrushMesh>(std::move(slab.Mesh)));
        value.Meshes.push_back(SlabMesh(mesh, slab));

        BrushPiece piece;
        piece.Mesh = &mesh;
        piece.MeshIndex = static_cast<std::uint32_t>(value.Meshes.size() - 1);
        piece.Ordinal = static_cast<std::uint32_t>(value.Pieces.size());
        piece.Origin = BrushPieceOrigin::Generated;
        value.Pieces.push_back(piece);
        value.LocalBounds.ExpandToInclude(value.Meshes.back().LocalBounds);
    }
    return &value;
}

std::span<const ShellProblem> VolumeEvaluations::ShellProblems(EntityId entity) const
{
    const auto found = Entries.find(EntityKey(entity));
    return found != Entries.end() ? std::span<const ShellProblem>(found->second.Problems)
                                  : std::span<const ShellProblem>{};
}

void VolumeEvaluations::Forget(EntityId entity)
{
    Entries.erase(EntityKey(entity));
}
