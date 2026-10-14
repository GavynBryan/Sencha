#include "VolumeEvaluations.h"

#include "EditorScene.h"
#include "SpaceComponents.h"

#include "brush/BrushValidation.h"

#include <core/hash/Fnv1a.h>
#include <world/registry/Registry.h>

#include <memory>
#include <optional>
#include <vector>

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

    BrushMesh InWorld(const BrushMesh& mesh, const Transform3f& world)
    {
        BrushMesh placed = mesh;
        for (BrushVertex& vertex : placed.Vertices)
            vertex.Position = world.TransformPoint(vertex.Position);
        return placed;
    }

    Aabb3d WorldBounds(const BrushMesh& mesh, const Transform3f& world)
    {
        Aabb3d bounds = Aabb3d::Empty();
        for (const BrushVertex& vertex : mesh.Vertices)
            bounds.ExpandToInclude(world.TransformPoint(vertex.Position));
        return bounds;
    }

    struct ShelledVolume
    {
        EntityId           Entity;
        const BrushRecord* Record = nullptr;
        const Transform3f* World = nullptr;
        float              Thickness = 0.0f;
        PersistentEntityId Id;
    };

    std::optional<ShelledVolume> Shelled(const EditorScene& scene, EntityId entity)
    {
        const Shell* shell = scene.GetRegistry().Components.TryGet<Shell>(entity);
        const BrushRecord* record = RecordOf(scene, entity);
        const Transform3f* world = scene.TryGetWorldTransform(entity);
        if (shell == nullptr || record == nullptr || world == nullptr || scene.RoleOf(entity) != BrushRole::Volume)
            return std::nullopt;
        return ShelledVolume{ entity, record, world, shell->Thickness, scene.PersistentIdOf(entity) };
    }

    // The shelled volumes near enough to share a wall with `self`: within its
    // thickness of its bounds. Every volume is visited, so this is
    // O(volumes x vertices) per call (docs/deferred.md).
    std::vector<ShelledVolume> NeighboursOf(const EditorScene& scene, const ShelledVolume& self)
    {
        constexpr float kReach = 1e-2f;
        Aabb3d reach = WorldBounds(self.Record->Mesh, *self.World);
        reach.Min = reach.Min - Vec3d{ self.Thickness + kReach, self.Thickness + kReach, self.Thickness + kReach };
        reach.Max = reach.Max + Vec3d{ self.Thickness + kReach, self.Thickness + kReach, self.Thickness + kReach };
        std::vector<ShelledVolume> neighbours;
        for (const EntityId entity : scene.GetAllEntities())
        {
            if (entity == self.Entity)
                continue;
            const std::optional<ShelledVolume> other = Shelled(scene, entity);
            if (other && reach.Intersects(WorldBounds(other->Record->Mesh, *other->World)))
                neighbours.push_back(*other);
        }
        return neighbours;
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
        for (const std::uint32_t face : slab.InnerFaces)
            evaluated.ToSource.FaceTable[face] = slab.SourceFace;
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
    const std::optional<ShelledVolume> self = Shelled(scene, entity);
    if (!self)
        return hash;
    const auto fold = [&hash](const ShelledVolume& volume) {
        HashFnv1aValue(hash, volume.Id.Value);
        HashFnv1aValue(hash, volume.Record->Revision);
        HashFnv1aValue(hash, volume.Thickness);
        HashFnv1aValue(hash, volume.World->Position);
        HashFnv1aValue(hash, volume.World->Rotation);
        HashFnv1aValue(hash, volume.World->Scale);
    };
    fold(*self);
    for (const ShelledVolume& neighbour : NeighboursOf(scene, *self))
        fold(neighbour);
    HashFnv1aValue(hash, scene.OpenFaceMaterial().Id.Value);
    HashFnv1aBytes(hash, scene.OpenFaceMaterial().Path.data(), scene.OpenFaceMaterial().Path.size());
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

    const std::optional<ShelledVolume> self = Shelled(scene, entity);
    if (!self)
        return &value;
    const Transform3f* world = self->World;

    // Walls are measured in the world, where up and the thickness mean what
    // they say, and stored in the volume's frame like every other piece.
    const std::vector<ShelledVolume> near = NeighboursOf(scene, *self);
    std::vector<BrushMesh> nearInWorld;
    nearInWorld.reserve(near.size());
    std::vector<ShellNeighbour> neighbours;
    for (const ShelledVolume& neighbour : near)
    {
        nearInWorld.push_back(InWorld(neighbour.Record->Mesh, *neighbour.World));
        neighbours.push_back({ &nearInWorld.back(), neighbour.Thickness, self->Id.Value < neighbour.Id.Value });
    }
    ShellRules rules;
    rules.Thickness = self->Thickness;
    rules.OpenFaceMaterial = scene.OpenFaceMaterial();
    ShellResult walls = GenerateShell(InWorld(record->Mesh, *world), rules, neighbours);
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
