#include <world/transform/TransformPropagation.h>

#include <world/registry/Registry.h>
#include <world/transform/PropagationOrderCache.h>

#include <cstdint>
#include <unordered_map>
#include <unordered_set>
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

    if (!Target.IsRegistered<LocalTransform>()
        || !Target.IsRegistered<WorldTransform>())
    {
        cache.MarkClean();
        return;
    }

    // Collect all entities that participate in propagation (have LocalTransform
    // and WorldTransform) and map their EntityIndex → EntityId so we can
    // reconstruct full ids from index comparisons below.
    std::unordered_map<EntityIndex, EntityId> indexToId;
    indexToId.reserve(Target.CountComponents<LocalTransform>());

    Target.ForEachComponent<LocalTransform>([&](EntityId id, const LocalTransform&)
    {
        if (Target.HasComponent<WorldTransform>(id))
            indexToId.emplace(id.Index, id);
    });

    if (indexToId.empty())
    {
        cache.MarkClean();
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
                const EntityId childId = EntityId{
                    entities[i],
                    0 // generation placeholder — we look up via indexToId below
                };
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

    cache.MarkClean();
}

// ─── Propagate ────────────────────────────────────────────────────────────────
//
// 1. Ensure the PropagationOrderCache resource exists.
// 2. Detect hierarchy changes via Changed<Parent>: if any chunk with a Parent
//    column was written (parent added/removed) since the last propagation,
//    invalidate the cache so it rebuilds this frame.
// 3. Rebuild if dirty.
// 4. Forward sweep: world[child] = world[parent] * local[child].
//    For root entities, world[child] = local[child].
// 5. The Write<WorldTransform> query bumps per-chunk change-detection counters so
//    downstream consumers (render extraction, physics) can use Changed<WorldTransform>.

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

    // Detect structural hierarchy changes via Changed<Parent>. If the Parent
    // column in any chunk has been written (entity gained or lost a Parent
    // component) since referenceFrame, the sort order may be stale.
    //
    // We use referenceFrame=0 with Changed<Parent> so we detect any write, not
    // just last-frame writes. This is safe because Changed<Parent> only fires
    // when an entity's archetype changed (AddComponent<Parent> / RemoveComponent<
    // Parent>), and those are structural changes that definitely require a rebuild.
    if (!cache.IsDirty() && Target.IsRegistered<Parent>())
    {
        const uint32_t prevFrame = Target.CurrentFrame() > 0
                                       ? Target.CurrentFrame() - 1
                                       : 0;
        Query<Read<Parent>> parentChanged(Target);
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

    const auto& order = cache.GetOrder();
    if (order.empty())
        return;

    // Forward sweep over the ordered list.
    // We write WorldTransform via TryGet (direct pointer) rather than a separate
    // Write<WorldTransform> query loop to keep the sweep a single pass. The
    // Write<WorldTransform> query below is a separate bump-only pass so that
    // Changed<WorldTransform> is correctly set for all written entities.
    for (const PropagationEntry& entry : order)
    {
        const LocalTransform* local = Target.TryGet<LocalTransform>(entry.Child);
        WorldTransform* world = Target.TryGet<WorldTransform>(entry.Child);
        if (!local || !world) continue;

        if (entry.Parent.IsValid())
        {
            const WorldTransform* parentWorld = Target.TryGet<WorldTransform>(entry.Parent);
            if (parentWorld)
                world->Value = parentWorld->Value * local->Value;
            else
                world->Value = local->Value;
        }
        else
        {
            world->Value = local->Value;
        }
    }

    // Bump Changed<WorldTransform> column version counters so downstream systems
    // using Changed<WorldTransform> get chunk-granularity change detection.
    // This is a write-only pass; the actual data was already set above.
    Query<Write<WorldTransform>, With<LocalTransform>> bumpQuery(Target);
    bumpQuery.ForEachChunk([](auto& /*view*/) {});
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
