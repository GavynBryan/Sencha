#include <world/transform/TransformPropagation.h>

#include <world/transform/PropagationOrderCache.h>

#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

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
                auto childIt = indexToId.find(entities[i]);
                if (childIt == indexToId.end())
                    continue;

                const EntityId& parentEntityId = parentComps[i].Entity;
                if (!parentEntityId.IsValid())
                    continue;
                if (indexToId.count(parentEntityId.Index) == 0)
                    continue;

                children[parentEntityId.Index].push_back(childIt->second);
                hasParent.insert(entities[i]);
            }
        });
    }

    struct BfsEntry
    {
        EntityId Child;
        EntityId Parent;
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
        if (childrenIt == children.end())
            continue;
        for (const EntityId& childId : childrenIt->second)
            queue.push_back({ childId, current.Child });
    }

    std::unordered_map<EntityIndex, size_t> indexToOrder;
    indexToOrder.reserve(order.size());
    for (size_t i = 0; i < order.size(); ++i)
        indexToOrder.emplace(order[i].Child.Index, i);

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

void TransformPropagationSystem::Propagate(
    const StoragePartitionSet& partitions,
    TransformPropagationDomain domain)
{
    if (!Target.IsRegistered<LocalTransform>()
        || !Target.IsRegistered<WorldTransform>())
    {
        return;
    }

    if (!Target.HasResource<PropagationOrderCache>())
        Target.AddResource<PropagationOrderCache>();

    PropagationOrderCache& cache = Target.GetResource<PropagationOrderCache>();

    if (!cache.IsDirty()
        && !cache.StructuralVersionMatches(Target.StructuralVersion()))
    {
        cache.Invalidate();
    }

    if (!cache.IsDirty() && Target.IsRegistered<Parent>())
    {
        const uint32_t prevFrame = Target.CurrentFrame() > 0
                                       ? Target.CurrentFrame() - 1
                                       : 0;
        Query<Changed<Parent>> parentChanged(Target);
        bool anyChanged = false;
        parentChanged.ForEachChunk([&](auto&)
        {
            anyChanged = true;
        }, prevFrame);

        if (anyChanged)
            cache.Invalidate();
    }

    if (cache.IsDirty())
        RebuildCache(cache);

    auto& order = cache.GetOrder();
    if (order.empty())
        return;

    const bool rebuilt = cache.ConsumeFullSweepPending();
    PropagationSweepState& sweep = cache.SweepState(domain);
    const uint32_t frame = Target.CurrentFrame();
    const uint32_t lastSweep = sweep.LastSweepFrame;

    std::vector<uint8_t>& dirty = cache.DirtyScratch();
    dirty.assign(order.size(), 0);

    for (size_t i = 0; i < order.size(); ++i)
    {
        const PropagationEntry& entry = order[i];
        if (entry.LocalPtr == nullptr || entry.WorldPtr == nullptr
            || entry.ChunkPtr == nullptr)
        {
            continue;
        }

        const StoragePartitionId partition = entry.ChunkPtr->Partition;
        if (!partitions.Contains(partition))
            continue;

        const bool newlyActive =
            !sweep.PreviousPartitions.Contains(partition);
        const bool parentDirty =
            entry.ParentOrderIndex != UINT32_MAX
            && dirty[entry.ParentOrderIndex] != 0;
        const bool localDirty =
            entry.ChunkPtr->ColumnLastWrittenFrame(entry.LocalCol) >= lastSweep;

        if (!rebuilt && !newlyActive && !parentDirty && !localDirty)
            continue;

        dirty[i] = 1;

        if (entry.ParentWorldPtr != nullptr)
            entry.WorldPtr->Value =
                entry.ParentWorldPtr->Value * entry.LocalPtr->Value;
        else
            entry.WorldPtr->Value = entry.LocalPtr->Value;

        entry.ChunkPtr->BumpColumnVersion(entry.WorldCol, frame);
    }

    sweep.PreviousPartitions.Clear();
    for (StoragePartitionId partition : partitions.Members())
        sweep.PreviousPartitions.Add(partition);
    sweep.LastSweepFrame = frame;
}
