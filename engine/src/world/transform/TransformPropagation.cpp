#include <world/transform/TransformPropagation.h>

#include <world/registry/Registry.h>
#include <world/registry/RegistryParallel.h>
#include <world/transform/PropagationOrderCache.h>

#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

// ─── RebuildCache ─────────────────────────────────────────────────────────────
//
// Builds a parent-before-child ordered list of PropagationEntries.
//
// Algorithm: BFS from roots (entities with LocalTransform+WorldTransform but no
// Parent) outward through the parent→children adjacency built from Read<Parent>.
//
// The children map is keyed by parent EntityIndex so BFS fan-out is O(1) per
// child. BFS guarantees every parent entry is emitted before any of its children,
// satisfying the forward-sweep invariant in Propagate().

void TransformPropagationSystem::RebuildCache(PropagationOrderCache& cache)
{
    auto& order = cache.GetOrder();
    order.clear();
    const uint64_t structuralVersion = Target.StructuralVersion();

    if (!Target.IsRegistered<LocalTransform>()
        || !Target.IsRegistered<WorldTransform>())
    {
        cache.MarkClean(structuralVersion);
        return;
    }

    // Collect all entities that participate in propagation (have LocalTransform
    // and WorldTransform) and map their EntityIndex → EntityId so we can
    // reconstruct full ids from index comparisons below.
    std::unordered_map<EntityIndex, EntityId> indexToId;
    indexToId.reserve(Target.CountComponents<LocalTransform>());

    std::as_const(Target).ForEachComponent<LocalTransform>(
        [&](EntityId id, const LocalTransform&)
    {
        if (Target.HasComponent<WorldTransform>(id))
            indexToId.emplace(id.Index, id);
    });

    if (indexToId.empty())
    {
        cache.MarkClean(structuralVersion);
        return;
    }

    // Build children adjacency: parent EntityIndex → list of child EntityIds.
    // Also track which entities have a parent so we can identify roots.
    std::unordered_map<EntityIndex, std::vector<EntityId>> children;
    std::unordered_set<EntityIndex> hasParent;

    if (Target.IsRegistered<Parent>())
    {
        Query<Read<Parent>, With<LocalTransform>, With<WorldTransform>> parentQuery(Target);
        parentQuery.ForEachChunk([&](auto& view)
        {
            const auto parentComps = view.template Read<Parent>();
            const EntityIndex* entities = view.Entities();
            for (uint32_t i = 0; i < view.Count(); ++i)
            {
                // Recover the full EntityId for the child from our map.
                auto childIt = indexToId.find(entities[i]);
                if (childIt == indexToId.end()) continue;

                const EntityId& parentEntityId = parentComps[i].Entity;
                if (!parentEntityId.IsValid()) continue;

                // Only add the parent→child link if the parent is also a
                // propagation participant (has LocalTransform + WorldTransform).
                if (indexToId.count(parentEntityId.Index) == 0) continue;

                children[parentEntityId.Index].push_back(childIt->second);
                hasParent.insert(entities[i]);
            }
        });
    }

    // BFS from roots (propagation participants without a Parent entry).
    // Each BFS entry carries the child EntityId and the parent EntityId (invalid
    // for roots).
    struct BfsEntry
    {
        EntityId Child;
        EntityId Parent; // invalid for roots
    };

    std::vector<BfsEntry> queue;
    queue.reserve(indexToId.size());

    for (const auto& [idx, id] : indexToId)
    {
        if (hasParent.count(idx) == 0)
            queue.push_back({ id, EntityId{} });
    }

    order.reserve(indexToId.size());

    for (size_t head = 0; head < queue.size(); ++head)
    {
        const BfsEntry current = queue[head];
        order.push_back({ current.Child, current.Parent });

        auto childrenIt = children.find(current.Child.Index);
        if (childrenIt != children.end())
        {
            for (const EntityId& childId : childrenIt->second)
                queue.push_back({ childId, current.Child });
        }
    }

    std::unordered_map<EntityIndex, size_t> indexToOrder;
    indexToOrder.reserve(order.size());
    for (size_t i = 0; i < order.size(); ++i)
        indexToOrder.emplace(order[i].Child.Index, i);

    // Resolve chunk locations and row pointers for the steady-state sweep.
    // LocateEntity is pure address resolution — unlike non-const TryGet it
    // bumps no column versions, so a rebuild does not register as a change.
    // Parents precede children in BFS order, so order[parentIdx].WorldPtr is
    // already resolved when a child entry reads it.
    const ComponentId localId = Target.GetComponentId<LocalTransform>();
    const ComponentId worldId = Target.GetComponentId<WorldTransform>();

    for (PropagationEntry& entry : order)
    {
        const World::EntityChunkLocation loc = Target.LocateEntity(entry.Child);
        if (loc.ChunkPtr != nullptr)
        {
            const uint32_t localCol = loc.ChunkPtr->FindColumn(localId);
            const uint32_t worldCol = loc.ChunkPtr->FindColumn(worldId);
            if (localCol != UINT32_MAX && worldCol != UINT32_MAX)
            {
                entry.ChunkPtr = loc.ChunkPtr;
                entry.LocalCol = localCol;
                entry.WorldCol = worldCol;
                entry.LocalPtr = reinterpret_cast<LocalTransform*>(
                    loc.ChunkPtr->ColumnData(localCol)) + loc.Row;
                entry.WorldPtr = reinterpret_cast<WorldTransform*>(
                    loc.ChunkPtr->ColumnData(worldCol)) + loc.Row;
            }
        }

        if (!entry.Parent.IsValid())
            continue;

        auto parentIt = indexToOrder.find(entry.Parent.Index);
        if (parentIt != indexToOrder.end())
        {
            entry.ParentOrderIndex = static_cast<uint32_t>(parentIt->second);
            entry.ParentWorldPtr = order[parentIt->second].WorldPtr;
        }
    }

    cache.MarkClean(structuralVersion);
}

// ─── Propagate ────────────────────────────────────────────────────────────────
//
// 1. Ensure the PropagationOrderCache resource exists.
// 2. Invalidate the cache when World::StructuralVersion() has moved — any
//    entity create/destroy or component add/remove can relocate rows and stale
//    the cached pointers (swap-removes and moves into existing archetypes do
//    NOT change the archetype count, so the count is not a sufficient key).
// 3. Also invalidate on Changed<Parent>: re-parenting via Write<Parent> or
//    non-const TryGet<Parent> mutates hierarchy order without any structural
//    change.
// 4. Rebuild if dirty. A rebuild forces a full sweep (structural moves copy
//    component data without bumping column versions).
// 5. Forward sweep: world[child] = world[parent] * local[child]; for roots,
//    world[child] = local[child]. An entry is recomputed only when its
//    LocalTransform column changed since the last sweep, or its parent entry
//    was recomputed this sweep (hierarchy dirtiness propagates down). The
//    sweep bumps the WorldTransform column version for exactly the chunks it
//    writes, so downstream Changed<WorldTransform> filters skip clean chunks.
//
// Skip granularity is chunk-conservative for the local-change test (a chunk
// write marks all its rows locally dirty) — consistent with the documented
// Changed<T> semantics.

void TransformPropagationSystem::Propagate()
{
    if (!Target.IsRegistered<LocalTransform>()
        || !Target.IsRegistered<WorldTransform>())
    {
        return;
    }

    // Ensure the cache resource exists.
    if (!Target.HasResource<PropagationOrderCache>())
        Target.AddResource<PropagationOrderCache>();

    PropagationOrderCache& cache = Target.GetResource<PropagationOrderCache>();

    if (!cache.IsDirty()
        && !cache.StructuralVersionMatches(Target.StructuralVersion()))
    {
        cache.Invalidate();
    }

    // Detect hierarchy edits via Changed<Parent>. Parent add/remove is already
    // covered by the structural version above; this catches re-parenting that
    // rewrites an existing Parent value (Write<Parent> query access or
    // non-const TryGet<Parent>), which moves no rows.
    //
    // The Changed<Parent> accessor is what arms the filter — a plain
    // Read<Parent> query ignores referenceFrame entirely and would report
    // every chunk as changed, forcing a rebuild every frame.
    if (!cache.IsDirty() && Target.IsRegistered<Parent>())
    {
        const uint32_t prevFrame = Target.CurrentFrame() > 0
                                       ? Target.CurrentFrame() - 1
                                       : 0;
        Query<Changed<Parent>> parentChanged(Target);
        bool anyChanged = false;
        parentChanged.ForEachChunk([&](auto& /*view*/)
        {
            anyChanged = true;
        }, prevFrame);

        if (anyChanged)
            cache.Invalidate();
    }

    if (cache.IsDirty())
        RebuildCache(cache);

    auto& order = cache.GetOrder();
    const bool fullSweep = cache.ConsumeFullSweepPending();
    if (order.empty())
        return;

    const uint32_t frame     = Target.CurrentFrame();
    const uint32_t lastSweep = cache.LastSweepFrame();

    // Frame-local dirty flags indexed like `order`. Parents precede children,
    // so dirty[ParentOrderIndex] is final by the time a child reads it.
    std::vector<uint8_t>& dirty = cache.DirtyScratch();
    dirty.assign(order.size(), 0);

    for (size_t i = 0; i < order.size(); ++i)
    {
        const PropagationEntry& entry = order[i];
        if (entry.LocalPtr == nullptr || entry.WorldPtr == nullptr)
            continue;

        const bool parentDirty =
            entry.ParentOrderIndex != UINT32_MAX
            && dirty[entry.ParentOrderIndex] != 0;
        const bool localDirty =
            entry.ChunkPtr->ColumnLastWrittenFrame(entry.LocalCol) >= lastSweep;

        if (!fullSweep && !parentDirty && !localDirty)
            continue;

        dirty[i] = 1;

        if (entry.ParentWorldPtr != nullptr)
            entry.WorldPtr->Value = entry.ParentWorldPtr->Value * entry.LocalPtr->Value;
        else
            entry.WorldPtr->Value = entry.LocalPtr->Value;

        // Precise change signal: only chunks actually written this sweep match
        // Changed<WorldTransform> downstream. Redundant bumps within a chunk
        // are a single store each.
        entry.ChunkPtr->BumpColumnVersion(entry.WorldCol, frame);
    }

    cache.SetLastSweepFrame(frame);
}

void PropagateTransforms(std::span<Registry*> registries)
{
    std::unordered_set<Registry*> seen;
    for (Registry* registry : registries)
    {
        if (registry == nullptr || !seen.insert(registry).second)
            continue;

        PropagateTransforms(registry->Components);
    }
}

void PropagateTransforms(JobSystem& jobs, std::span<Registry*> registries)
{
    // Deduplicate before the fork: ForEachRegistryParallel requires distinct
    // entries, and duplicates here would mean two jobs propagating one World.
    std::vector<Registry*> unique;
    unique.reserve(registries.size());
    std::unordered_set<Registry*> seen;
    for (Registry* registry : registries)
    {
        if (registry != nullptr && seen.insert(registry).second)
            unique.push_back(registry);
    }

    ForEachRegistryParallel(jobs, std::span<Registry* const>(unique),
                            [](Registry& registry) {
                                PropagateTransforms(registry.Components);
                            });
}
