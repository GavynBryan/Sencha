#include "BrushDrawSet.h"

#include "document/EditorScene.h"
#include "brush/BrushWorkCounters.h"

#include <core/hash/Fnv1a.h>
#include <world/registry/Registry.h>

#include <utility>

namespace
{
    std::uint64_t EntityKey(EntityId entity)
    {
        return (static_cast<std::uint64_t>(entity.Index) << 32) | entity.Generation;
    }

    std::uint64_t FingerprintOf(const BrushBakedRecord& baked)
    {
        std::uint64_t h = kFnv1aOffsetBasis;
        HashFnv1aValue(h, baked.DefaultMaterialHash);
        for (const BrushBakedMesh& mesh : baked.Meshes)
        {
            HashFnv1aValue(h, mesh.Handle.ToToken());
            HashFnv1aValue(h, mesh.ContentHash);
        }
        return h;
    }
}

const BrushDrawEntity* BrushDrawSet::Find(EntityId entity) const
{
    const auto it = IndexOf.find(EntityKey(entity));
    return it == IndexOf.end() ? nullptr : &Records[it->second];
}

bool BrushDrawSet::Refresh(const EditorScene& scene, BrushBakeCache& bakes, const AssetRef& levelDefault)
{
    const RegistryId registry = scene.GetRegistry().Id;
    Scratch.clear();
    bool changed = false;
    std::size_t position = 0;

    for (const EntityId entity : scene.GetAllEntities())
    {
        if (!scene.IsEntityEffectivelyVisible(entity))
            continue;
        const BrushPlacementFacts& facts = scene.PlacementFacts();
        const std::optional<BrushPlacementKey> key = facts.KeyOf(entity);
        if (!key.has_value())
            continue;
        const BrushEvaluated* evaluated = facts.GetEvaluation(entity);
        if (evaluated == nullptr)
            continue;
        const BrushBakedRecord& baked = bakes.Ensure(registry, key->Brush, *evaluated, levelDefault);
        const std::uint64_t fingerprint = FingerprintOf(baked);

        const auto it = IndexOf.find(EntityKey(entity));
        BrushDrawEntity record;
        if (it != IndexOf.end())
        {
            record = std::move(Records[it->second]);
            if (it->second != position)
                changed = true; // an earlier entity left: emission order moved
        }
        else
            changed = true;

        const bool sameKey = it != IndexOf.end() && record.Key == *key;
        if (!sameKey || record.BakedFingerprint != fingerprint)
        {
            record.Key = *key;
            record.BakedFingerprint = fingerprint;
            RebuildPlacements(record, *evaluated, baked, facts.GetPiecePlacements(entity),
                              facts.GetPieceBounds(entity));
            if (!sameKey)
                RebuildEdges(record, *evaluated);
            ++Rebuilds;
            ++BrushWorkCounters::Frame().DrawRecordRebuilds;
            changed = true;
        }
        Scratch.push_back(std::move(record));
        ++position;
    }

    if (Scratch.size() != Records.size())
        changed = true;
    std::swap(Records, Scratch);
    if (!changed)
        return false;

    IndexOf.clear();
    for (std::size_t i = 0; i < Records.size(); ++i)
        IndexOf[EntityKey(Records[i].Key.Entity)] = i;
    RefreshDigest();
    ++Version_;
    return true;
}

void BrushDrawSet::RebuildPlacements(BrushDrawEntity& record, const BrushEvaluated& evaluated,
                                     const BrushBakedRecord& baked,
                                     std::span<const Transform3f> pieceWorld,
                                     std::span<const Aabb3d> pieceBounds)
{
    record.Meshes.resize(evaluated.Meshes.size());
    for (std::size_t i = 0; i < evaluated.Meshes.size(); ++i)
    {
        BrushDrawMesh& mesh = record.Meshes[i];
        if (i < baked.Meshes.size())
        {
            mesh.Handle = baked.Meshes[i].Handle;
            mesh.SlotMaterials = baked.Meshes[i].SlotMaterials;
            mesh.LocalBounds = baked.Meshes[i].LocalBounds;
            mesh.ContentHash = baked.Meshes[i].ContentHash;
        }
        else
        {
            mesh.Handle = {};
            mesh.SlotMaterials.clear();
            mesh.LocalBounds = Aabb3d::Empty();
            mesh.ContentHash = 0;
        }
    }

    // Placements and bounds are facts; grouping by mesh makes each run one
    // contiguous instance stream.
    struct Placed { std::uint32_t MeshIndex; bool Source; BrushPlacement Placement; };
    std::vector<Placed> placed;
    placed.reserve(evaluated.Pieces.size());
    for (const BrushPiece& piece : evaluated.Pieces)
    {
        if (piece.Ordinal >= pieceWorld.size() || piece.Ordinal >= pieceBounds.size())
            continue;
        Placed p;
        p.MeshIndex = piece.MeshIndex;
        p.Source = piece.Origin == BrushPieceOrigin::Source;
        p.Placement.World = pieceWorld[piece.Ordinal].ToMat4();
        p.Placement.WorldBounds = pieceBounds[piece.Ordinal];
        placed.push_back(p);
    }

    std::vector<std::uint32_t> counts(record.Meshes.size(), 0);
    for (const Placed& p : placed)
        if (p.MeshIndex < counts.size())
            ++counts[p.MeshIndex];
    record.Runs.clear();
    std::vector<std::uint32_t> cursor(record.Meshes.size(), 0);
    std::uint32_t first = 0;
    for (std::uint32_t mesh = 0; mesh < counts.size(); ++mesh)
    {
        if (counts[mesh] == 0)
            continue;
        record.Runs.push_back(BrushMeshRun{ mesh, first, counts[mesh] });
        cursor[mesh] = first;
        first += counts[mesh];
    }
    record.Placements.resize(first);
    record.InstanceRows.resize(first);
    for (const Placed& p : placed)
    {
        if (p.MeshIndex >= counts.size())
            continue;
        const std::uint32_t slot = cursor[p.MeshIndex]++;
        if (p.Source)
            record.SourceSlot = slot;
        record.Placements[slot] = p.Placement;
        record.InstanceRows[slot] = p.Placement.World; // Data[r] is row r: what the instanced shader reads
    }

    const Transform3f& world = record.Key.World;
    std::uint64_t digest = kFnv1aOffsetBasis;
    for (const BrushDrawMesh& mesh : record.Meshes)
        HashFnv1aValue(digest, mesh.ContentHash);
    HashFnv1aValue(digest, world.Position);
    HashFnv1aValue(digest, world.Rotation);
    HashFnv1aValue(digest, world.Scale);
    for (const BrushPiece& piece : evaluated.Pieces)
    {
        HashFnv1aValue(digest, piece.MeshIndex);
        HashFnv1aValue(digest, piece.Placement.Position);
        HashFnv1aValue(digest, piece.Placement.Rotation);
        HashFnv1aValue(digest, piece.Placement.Scale);
    }
    record.Digest = digest;
}

void BrushDrawSet::RebuildEdges(BrushDrawEntity& record, const BrushEvaluated& evaluated)
{
    // Edge topology is an evaluation fact; this only pairs it with positions.
    for (std::size_t i = 0; i < evaluated.Meshes.size() && i < record.Meshes.size(); ++i)
    {
        std::vector<BrushEdgeVertex>& edges = record.Meshes[i].Edges;
        edges.clear();
        const BrushEvaluatedMesh& mesh = evaluated.Meshes[i];
        if (mesh.Mesh == nullptr)
            continue;
        edges.reserve(mesh.EdgePairs.size() * 2);
        for (std::size_t e = 0; e < mesh.EdgePairs.size(); ++e)
        {
            const bool soft = e < mesh.EdgeSoft.size() && mesh.EdgeSoft[e];
            edges.push_back(BrushEdgeVertex{ mesh.Mesh->Vertices[mesh.EdgePairs[e][0]].Position, soft });
            edges.push_back(BrushEdgeVertex{ mesh.Mesh->Vertices[mesh.EdgePairs[e][1]].Position, soft });
        }
    }
}

void BrushDrawSet::RefreshDigest()
{
    std::uint64_t digest = kFnv1aOffsetBasis;
    for (const BrushDrawEntity& record : Records)
    {
        HashFnv1aValue(digest, record.Digest);
        HashFnv1aByte(digest, '|');
    }
    Digest_ = digest;
}
