#include "BrushPlacementFacts.h"

#include "EditorScene.h"
#include "brush/BrushBounds.h"
#include "brush/BrushWorkCounters.h"

#include <utility>

namespace
{
    std::uint64_t EntityKey(EntityId entity)
    {
        return (static_cast<std::uint64_t>(entity.Index) << 32) | entity.Generation;
    }
}

std::optional<BrushPlacementKey> MakeBrushPlacementKey(const EditorScene& scene, EntityId entity)
{
    const BrushComponent* brush = scene.TryGetBrush(entity);
    const Transform3f* world = scene.TryGetWorldTransform(entity);
    if (brush == nullptr || world == nullptr)
        return std::nullopt;
    const BrushRecord* record = scene.GetBrushMeshStore().FindRecord(brush->Id);
    if (record == nullptr)
        return std::nullopt;
    return BrushPlacementKey{
        .Entity = entity,
        .Brush = brush->Id,
        .TopologyRevision = record->TopologyRevision,
        .PlacementRevision = record->PlacementRevision,
        .MaxPieces = scene.InteractiveEvaluationPolicy().MaxPieces,
        .World = *world,
    };
}

BrushPlacementFacts::BrushPlacementFacts(const EditorScene& scene)
    : Scene(scene)
{
}

const BrushPlacementFacts::Record* BrushPlacementFacts::Ensure(EntityId entity) const
{
    const std::optional<BrushPlacementKey> key = MakeBrushPlacementKey(Scene, entity);
    if (!key.has_value())
        return nullptr;
    // Asking the scene for the pieces also restores the interactive
    // evaluation if a one-shot consumer (merge, bake) evaluated the record
    // under the cook policy in between; evaluation is deterministic, so a
    // record built from the same key still describes it exactly.
    const BrushEvaluated* evaluated = Scene.TryGetBrushPieces(entity);
    Record& record = Records[EntityKey(entity)];
    if (record.Evaluated != evaluated || !(record.Key == *key))
        Rebuild(record, *key);
    return &record;
}

void BrushPlacementFacts::Rebuild(Record& record, const BrushPlacementKey& key) const
{
    const BrushEvaluated* evaluated = Scene.TryGetBrushPieces(key.Entity);
    if (evaluated == nullptr)
    {
        record = Record{};
        return;
    }
    ++Rebuilds;
    ++BrushWorkCounters::Frame().PlacementRebuilds;

    // Elements depend on topology and transform alone: a placement-only change
    // keeps them.
    std::optional<SourceWorldElements> keptElements;
    if (record.Elements.has_value() && record.ElementsTopology == key.TopologyRevision
        && record.ElementsWorld == key.World)
        keptElements = std::move(record.Elements);

    record.Key = key;
    record.Evaluated = evaluated;
    record.PieceWorld.clear();
    record.PieceWorldBounds.clear();
    record.PieceWorld.reserve(evaluated->Pieces.size());
    record.PieceWorldBounds.resize(evaluated->Pieces.size(), Aabb3d::Empty());
    record.UnionWorldBounds = Aabb3d::Empty();
    for (const BrushPiece& piece : evaluated->Pieces)
        record.PieceWorld.push_back(PieceWorldTransform(key.World, piece));
    ForEachPieceWorldBounds(*evaluated, key.World, [&](const BrushPiece& piece, const Aabb3d& bounds)
    {
        record.PieceWorldBounds[piece.Ordinal] = bounds;
        record.UnionWorldBounds.ExpandToInclude(bounds);
    });
    record.SourceWorldBounds = evaluated->SourcePiece < record.PieceWorldBounds.size()
        ? record.PieceWorldBounds[evaluated->SourcePiece]
        : Aabb3d::Empty();

    record.Elements = std::move(keptElements);
    record.ElementsTopology = key.TopologyRevision;
    record.ElementsWorld = key.World;
}

std::optional<Aabb3d> BrushPlacementFacts::GetEntityBounds(EntityId entity) const
{
    const Record* record = Ensure(entity);
    if (record == nullptr || !record->UnionWorldBounds.IsValid())
        return std::nullopt;
    return record->UnionWorldBounds;
}

std::optional<Aabb3d> BrushPlacementFacts::GetSourceBounds(EntityId entity) const
{
    const Record* record = Ensure(entity);
    if (record == nullptr || !record->SourceWorldBounds.IsValid())
        return std::nullopt;
    return record->SourceWorldBounds;
}

std::span<const Transform3f> BrushPlacementFacts::GetPiecePlacements(EntityId entity) const
{
    const Record* record = Ensure(entity);
    return record == nullptr ? std::span<const Transform3f>{} : std::span<const Transform3f>(record->PieceWorld);
}

std::span<const Aabb3d> BrushPlacementFacts::GetPieceBounds(EntityId entity) const
{
    const Record* record = Ensure(entity);
    return record == nullptr ? std::span<const Aabb3d>{} : std::span<const Aabb3d>(record->PieceWorldBounds);
}

const BrushEvaluated* BrushPlacementFacts::GetEvaluation(EntityId entity) const
{
    const Record* record = Ensure(entity);
    return record == nullptr ? nullptr : record->Evaluated;
}

const SourceWorldElements* BrushPlacementFacts::GetSourceWorldElements(EntityId entity) const
{
    const Record* record = Ensure(entity);
    if (record == nullptr || record->Evaluated == nullptr)
        return nullptr;
    if (!record->Elements.has_value())
    {
        const BrushEvaluated& evaluated = *record->Evaluated;
        const BrushPiece& source = evaluated.Pieces[evaluated.SourcePiece];
        const Transform3f& world = record->PieceWorld[evaluated.SourcePiece];
        Record& mutableRecord = Records[EntityKey(entity)];
        SourceWorldElements elements;
        elements.Edges = MeshElements::Edges(*source.Mesh, world);
        elements.Vertices = MeshElements::Vertices(*source.Mesh, world);
        elements.Faces = MeshElements::Faces(*source.Mesh, world);
        mutableRecord.Elements = std::move(elements);
        return &*mutableRecord.Elements;
    }
    return &*record->Elements;
}

std::optional<std::uint32_t> BrushPlacementFacts::SourceEdgeIndexOf(EntityId entity, std::uint32_t a,
                                                                    std::uint32_t b) const
{
    const Record* record = Ensure(entity);
    if (record == nullptr || record->Evaluated == nullptr)
        return std::nullopt;
    return ::SourceEdgeIndexOf(*record->Evaluated, a, b);
}

std::optional<BrushPlacementKey> BrushPlacementFacts::KeyOf(EntityId entity) const
{
    return MakeBrushPlacementKey(Scene, entity);
}

void BrushPlacementFacts::Refresh()
{
    for (auto it = Records.begin(); it != Records.end();)
    {
        if (MakeBrushPlacementKey(Scene, it->second.Key.Entity).has_value())
            ++it;
        else
            it = Records.erase(it);
    }
}

void BrushPlacementFacts::Clear()
{
    Records.clear();
}
