#include <logic/Counter.h>

#include <ecs/World.h>
#include <world/held/HeldRecordIndex.h>

HeldRecordKey CounterKey(GameplayTagId name)
{
    return HeldRecordKey{ .Kind = ResolveComponentTypeId<Counter>(), .Name = name.Value };
}

void ComponentTraits<Counter>::OnAdd(Counter& counter, World& world, EntityId record)
{
    if (HeldRecordIndex* index = world.TryGetResource<HeldRecordIndex>())
        index->Key(record, CounterKey(counter.Name));
}

void ComponentTraits<Counter>::OnRemove(const Counter& counter, World& world, EntityId record)
{
    if (HeldRecordIndex* index = world.TryGetResource<HeldRecordIndex>())
        index->Unkey(record, CounterKey(counter.Name));
}
