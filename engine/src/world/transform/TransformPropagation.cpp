#include <world/transform/TransformPropagation.h>

#include <world/transform/PropagationOrderCache.h>

#include <cstdint>

namespace
{
// An entity with no parent: its world transform is its local transform.
template <typename View>
void WriteWorldFromLocal(View& view)
{
    const auto locals = view.template Read<LocalTransform>();
    auto worlds = view.template Write<WorldTransform>();
    for (uint32_t row = 0; row < view.Count(); ++row)
        worlds[row].Value = locals[row].Value;
}

// The flat sweep: every transform entity with no parent, in chunk order, with
// the dirty test applied once per chunk instead of once per entity.
//
// Two query shapes rather than one, because a chunk cannot be skipped from
// inside the callback: Query bumps a Write column's version for every chunk it
// visits, so a chunk that visited but wrote nothing would still read as changed
// to everything downstream. Changed<LocalTransform> does the skipping before the
// visit. It tests strictly-after, so the reference frame is one below the last
// sweep, which reproduces exactly the inclusive "written at or after the last
// sweep" test the entity-at-a-time sweep used. The inclusive form matters: two
// writes in one frame are indistinguishable, so a sweep must always redo work
// written during its own frame.
template <typename Unfiltered, typename Filtered>
void SweepUnparented(
    World& world,
    const StoragePartitionSet& partitions,
    const StoragePartitionSet& entering,
    bool fullSweep,
    uint32_t lastSweepFrame)
{
    const auto write = [](auto& view) { WriteWorldFromLocal(view); };

    if (fullSweep)
    {
        Unfiltered all(world);
        all.ForEachChunkIn(partitions, write);
        return;
    }

    // A partition that re-enters this domain may have had local transforms
    // written while it was dormant, so it gets one unconditional sweep.
    if (!entering.Empty())
    {
        Unfiltered resumed(world);
        resumed.ForEachChunkIn(entering, write);
    }

    Filtered moved(world);
    moved.ForEachChunkIn(partitions, write, lastSweepFrame - 1);
}
} // namespace

// Entities carrying Parent, whether or not they carry transforms. Coarser than
// the order's own population on purpose: see PropagationOrderCache.
std::size_t TransformPropagationSystem::ParentCount() const
{
    return Target.CountComponents<Parent>();
}

bool TransformPropagationSystem::HierarchyChangedSince(uint32_t topologyFrame)
{
    if (!Target.IsRegistered<Parent>())
        return false;

    // Chunk-conservative, and one frame behind on purpose: a Parent written in
    // the same frame the order was built must still be seen, because an
    // archetype migration bumps every column in the destination chunk and that
    // is how a Parent gained or lost alongside other components surfaces here.
    //
    // A re-parent performed in frame zero with no intervening AdvanceFrame is
    // not visible to any column-version probe — the version is the frame number,
    // and there is no frame below it to compare against. Advance the frame
    // between a hierarchy edit and the sweep that must observe it;
    // TransformPropagation tests do.
    const uint32_t reference = topologyFrame > 0 ? topologyFrame - 1 : 0;
    Query<Changed<Parent>> changed(Target);
    bool any = false;
    changed.ForEachChunk([&](auto&) { any = true; }, reference);
    return any;
}

void TransformPropagationSystem::RebuildTopology(
    PropagationOrderCache& cache,
    std::size_t parentCount)
{
    std::vector<PropagationEntry>& order = cache.GetOrder();
    order.clear();

    if (!Target.IsRegistered<Parent>())
    {
        cache.MarkTopologyClean(parentCount, Target.CurrentFrame());
        return;
    }

    PropagationOrderCache::RebuildWorkspace& work = cache.Workspace();
    work.Child.clear();
    work.Parent.clear();

    Query<Read<Parent>, With<LocalTransform>, With<WorldTransform>> nodes(Target);
    nodes.ForEachChunk([&](auto& view)
    {
        const auto parents = view.template Read<Parent>();
        for (uint32_t row = 0; row < view.Count(); ++row)
        {
            work.Child.push_back(view.Entity(row));
            work.Parent.push_back(parents[row].Entity);
        }
    });

    const std::size_t count = work.Child.size();
    if (count == 0)
    {
        cache.MarkTopologyClean(parentCount, Target.CurrentFrame());
        return;
    }

    work.SlotOfIndex.clear();
    work.SlotOfIndex.reserve(count);
    for (std::size_t slot = 0; slot < count; ++slot)
        work.SlotOfIndex.emplace(work.Child[slot].Index, static_cast<uint32_t>(slot));

    // A parent that is itself parented is ordered ahead of its child here. Any
    // other parent — an unparented root, a non-transform entity, or a stale id —
    // is reached by address instead.
    work.ParentSlot.assign(count, UINT32_MAX);
    for (std::size_t slot = 0; slot < count; ++slot)
    {
        const EntityId parent = work.Parent[slot];
        if (!parent.IsValid())
            continue;

        const auto found = work.SlotOfIndex.find(parent.Index);
        // Full identity, not just the index: entity slots are reused, and
        // inheriting the transform of whatever now occupies the slot would be
        // silently wrong rather than merely stale.
        if (found != work.SlotOfIndex.end()
            && work.Child[found->second] == parent)
        {
            work.ParentSlot[slot] = found->second;
        }
    }

    // Children per parent in compressed adjacency form: counts, prefix sum, fill.
    work.ChildOffset.assign(count + 1, 0);
    for (std::size_t slot = 0; slot < count; ++slot)
    {
        if (work.ParentSlot[slot] != UINT32_MAX)
            ++work.ChildOffset[work.ParentSlot[slot] + 1];
    }
    for (std::size_t slot = 0; slot < count; ++slot)
        work.ChildOffset[slot + 1] += work.ChildOffset[slot];

    work.ChildCursor = work.ChildOffset;
    work.ChildSlots.assign(work.ChildOffset[count], 0);
    for (std::size_t slot = 0; slot < count; ++slot)
    {
        const uint32_t parentSlot = work.ParentSlot[slot];
        if (parentSlot != UINT32_MAX)
            work.ChildSlots[work.ChildCursor[parentSlot]++] = static_cast<uint32_t>(slot);
    }

    // Breadth-first from the entities whose parent is not itself in the order.
    // Anything unreachable from one of those seeds is in a parent cycle and stays
    // out of the order, so a cycle costs no propagation rather than an infinite
    // walk.
    order.reserve(count);
    work.OrderSlot.clear();
    work.OrderSlot.reserve(count);
    for (std::size_t slot = 0; slot < count; ++slot)
    {
        if (work.ParentSlot[slot] != UINT32_MAX)
            continue;

        order.push_back(PropagationEntry{
            .Child = work.Child[slot],
            .Parent = work.Parent[slot],
        });
        work.OrderSlot.push_back(static_cast<uint32_t>(slot));
    }

    for (std::size_t position = 0; position < order.size(); ++position)
    {
        const uint32_t slot = work.OrderSlot[position];
        for (uint32_t edge = work.ChildOffset[slot];
             edge < work.ChildOffset[slot + 1];
             ++edge)
        {
            const uint32_t child = work.ChildSlots[edge];
            order.push_back(PropagationEntry{
                .Child = work.Child[child],
                .Parent = work.Parent[child],
                .ParentOrderIndex = static_cast<uint32_t>(position),
            });
            work.OrderSlot.push_back(child);
        }
    }

    cache.MarkTopologyClean(parentCount, Target.CurrentFrame());
}

void TransformPropagationSystem::ResolveAddresses(PropagationOrderCache& cache)
{
    std::vector<PropagationEntry>& order = cache.GetOrder();
    const ComponentId localId = Target.GetComponentId<LocalTransform>();
    const ComponentId worldId = Target.GetComponentId<WorldTransform>();

    for (PropagationEntry& entry : order)
    {
        entry.LocalPtr = nullptr;
        entry.WorldPtr = nullptr;
        entry.ParentWorldPtr = nullptr;
        entry.ChunkPtr = nullptr;
        entry.ParentChunkPtr = nullptr;
        entry.LocalCol = UINT32_MAX;
        entry.WorldCol = UINT32_MAX;
        entry.ParentWorldCol = UINT32_MAX;

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

        // Parents precede their children in the order, so an in-order parent's
        // address is already resolved.
        if (entry.ParentOrderIndex != UINT32_MAX)
        {
            entry.ParentWorldPtr = order[entry.ParentOrderIndex].WorldPtr;
            continue;
        }

        if (!entry.Parent.IsValid())
            continue;

        const World::EntityChunkLocation parentLoc =
            Target.LocateEntity(entry.Parent);
        if (parentLoc.ChunkPtr == nullptr)
            continue;

        const uint32_t parentWorldCol = parentLoc.ChunkPtr->FindColumn(worldId);
        if (parentWorldCol == UINT32_MAX)
            continue;

        entry.ParentChunkPtr = parentLoc.ChunkPtr;
        entry.ParentWorldCol = parentWorldCol;
        entry.ParentWorldPtr = reinterpret_cast<const WorldTransform*>(
            parentLoc.ChunkPtr->ColumnData(parentWorldCol)) + parentLoc.Row;
    }

    cache.MarkAddressesResolved(Target.StructuralVersion());
}

void TransformPropagationSystem::SweepOrder(
    PropagationOrderCache& cache,
    const StoragePartitionSet& partitions,
    const PropagationSweepState& sweep,
    bool fullSweep,
    uint32_t frame)
{
    std::vector<PropagationEntry>& order = cache.GetOrder();
    if (order.empty())
        return;

    const uint32_t lastSweep = sweep.LastSweepFrame;
    std::vector<uint8_t>& dirty = cache.DirtyScratch();
    dirty.assign(order.size(), 0);

    for (std::size_t index = 0; index < order.size(); ++index)
    {
        const PropagationEntry& entry = order[index];
        if (entry.LocalPtr == nullptr || entry.WorldPtr == nullptr
            || entry.ChunkPtr == nullptr)
        {
            continue;
        }

        const StoragePartitionId partition = entry.ChunkPtr->Partition;
        if (!partitions.Contains(partition))
            continue;

        const bool resumed = !sweep.PreviousPartitions.Contains(partition);
        // A parent inside the order reports through the dirty flags. A parent
        // outside it was handled by the flat pass, which bumps its world
        // column's version when it recomputes.
        const bool parentDirty =
            entry.ParentOrderIndex != UINT32_MAX
                ? dirty[entry.ParentOrderIndex] != 0
                : entry.ParentChunkPtr != nullptr
                      && entry.ParentChunkPtr->ColumnLastWrittenFrame(
                             entry.ParentWorldCol) >= lastSweep;
        const bool localDirty =
            entry.ChunkPtr->ColumnLastWrittenFrame(entry.LocalCol) >= lastSweep;

        if (!fullSweep && !resumed && !parentDirty && !localDirty)
            continue;

        dirty[index] = 1;

        if (entry.ParentWorldPtr != nullptr)
        {
            entry.WorldPtr->Value =
                entry.ParentWorldPtr->Value * entry.LocalPtr->Value;
        }
        else
        {
            entry.WorldPtr->Value = entry.LocalPtr->Value;
        }

        entry.ChunkPtr->BumpColumnVersion(entry.WorldCol, frame);
    }
}

void TransformPropagationSystem::Propagate(
    const StoragePartitionSet& partitions,
    TransformPropagationDomain domain,
    bool forceFullInvalidation)
{
    if (!Target.IsRegistered<LocalTransform>()
        || !Target.IsRegistered<WorldTransform>())
    {
        return;
    }

    if (!Target.HasResource<PropagationOrderCache>())
        Target.AddResource<PropagationOrderCache>();

    PropagationOrderCache& cache = Target.GetResource<PropagationOrderCache>();

    if (forceFullInvalidation)
        cache.Invalidate();

    if (!cache.IsDirty() && HierarchyChangedSince(cache.TopologyFrame()))
        cache.Invalidate();

    const std::size_t parentCount = ParentCount();
    if (!cache.TopologyMatches(parentCount))
        RebuildTopology(cache, parentCount);

    if (!cache.AddressesMatch(Target.StructuralVersion()))
        ResolveAddresses(cache);

    PropagationSweepState& sweep = cache.SweepState(domain);
    const uint32_t frame = Target.CurrentFrame();

    // The frame counter is the only change stamp available, so a domain that has
    // never swept, or whose last sweep was frame zero, has nothing it can
    // compare against and sweeps everything. A rebuilt order additionally
    // re-derives every parent link, so its entries all recompute once — but the
    // unparented entities are untouched by a hierarchy change and stay
    // incremental, which is what keeps a hierarchy edit off the critical path of
    // a large flat world.
    const bool rebuilt = cache.ConsumeFullSweepPending();
    const bool flatFullSweep = !sweep.Swept || sweep.LastSweepFrame == 0;
    const bool orderFullSweep = flatFullSweep || rebuilt;

    StoragePartitionSet& entering = cache.EnteringScratch();
    entering.Clear();
    if (!flatFullSweep)
    {
        for (const StoragePartitionId partition : partitions.Members())
        {
            if (!sweep.PreviousPartitions.Contains(partition))
                entering.Add(partition);
        }
    }

    if (Target.IsRegistered<Parent>())
    {
        SweepUnparented<
            Query<Read<LocalTransform>, Write<WorldTransform>, Without<Parent>>,
            Query<
                Read<LocalTransform>,
                Write<WorldTransform>,
                Without<Parent>,
                Changed<LocalTransform>>>(
            Target,
            partitions,
            entering,
            flatFullSweep,
            sweep.LastSweepFrame);
    }
    else
    {
        SweepUnparented<
            Query<Read<LocalTransform>, Write<WorldTransform>>,
            Query<
                Read<LocalTransform>,
                Write<WorldTransform>,
                Changed<LocalTransform>>>(
            Target,
            partitions,
            entering,
            flatFullSweep,
            sweep.LastSweepFrame);
    }

    SweepOrder(cache, partitions, sweep, orderFullSweep, frame);

    sweep.PreviousPartitions = partitions;
    sweep.LastSweepFrame = frame;
    sweep.Swept = true;
}
