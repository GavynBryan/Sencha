#include <attributes/AttributeResolve.h>

#include <attributes/AttributeRegistry.h>
#include <attributes/AttributeSet.h>

#include <ecs/Query.h>
#include <ecs/StoragePartitionSet.h>
#include <ecs/World.h>

#include <utility>

namespace
{
template <typename Visit>
void VisitAttributeSets(
    World& world,
    const StoragePartitionSet* partitions,
    Visit&& visit)
{
    if (!world.IsRegistered<AttributeSet>())
        return;

    Query<Write<AttributeSet>> query(world);
    const auto process = [&](auto& view)
    {
        auto sets = view.template Write<AttributeSet>();
        for (std::uint32_t i = 0; i < view.Count(); ++i)
            visit(sets[i]);
    };

    if (partitions != nullptr)
        query.ForEachChunkIn(*partitions, process);
    else
        query.ForEachChunk(process);
}
} // namespace

void ResetAttributesToBase(World& world)
{
    VisitAttributeSets(world, nullptr, [](AttributeSet& set)
    {
        for (int i = 0; i < set.Count; ++i)
            set.Current[i] = set.Base[i];
    });
}

void ResetAttributesToBase(
    World& world,
    const StoragePartitionSet& partitions)
{
    VisitAttributeSets(world, &partitions, [](AttributeSet& set)
    {
        for (int i = 0; i < set.Count; ++i)
            set.Current[i] = set.Base[i];
    });
}

void ClampAttributes(World& world)
{
    const AttributeRegistry* registry =
        std::as_const(world).TryGetResource<AttributeRegistry>();
    if (registry == nullptr)
        return;

    VisitAttributeSets(world, nullptr, [registry](AttributeSet& set)
    {
        for (int i = 0; i < set.Count; ++i)
            set.Current[i] = registry->Clamp(set.Ids[i], set.Current[i]);
    });
}

void ClampAttributes(
    World& world,
    const StoragePartitionSet& partitions)
{
    const AttributeRegistry* registry =
        std::as_const(world).TryGetResource<AttributeRegistry>();
    if (registry == nullptr)
        return;

    VisitAttributeSets(world, &partitions, [registry](AttributeSet& set)
    {
        for (int i = 0; i < set.Count; ++i)
            set.Current[i] = registry->Clamp(set.Ids[i], set.Current[i]);
    });
}

void ResolveAttributes(World& world)
{
    ResetAttributesToBase(world);
    ClampAttributes(world);
}

void ResolveAttributes(
    World& world,
    const StoragePartitionSet& partitions)
{
    ResetAttributesToBase(world, partitions);
    ClampAttributes(world, partitions);
}
