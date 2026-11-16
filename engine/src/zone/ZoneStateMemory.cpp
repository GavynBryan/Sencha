#include <zone/ZoneStateMemory.h>

#include <ecs/Query.h>
#include <ecs/StoragePartitionSet.h>
#include <ecs/World.h>
#include <world/identity/PersistentIdComponent.h>

#include <algorithm>
#include <utility>

void ZoneComponentMemories::Register(const ZoneComponentMemory& memory)
{
    const auto existing = std::ranges::find(Memories, memory.Type, &ZoneComponentMemory::Type);
    if (existing != Memories.end())
        *existing = memory;
    else
        Memories.push_back(memory);
}

std::vector<EntityId> IdentifiedEntitiesIn(World& world, StoragePartitionId partition)
{
    std::vector<EntityId> identified;
    if (!world.IsRegistered<PersistentIdComponent>())
        return identified;
    StoragePartitionSet partitions;
    partitions.Add(partition);
    Query<Read<PersistentIdComponent>> query(world);
    query.ForEachChunkIn(partitions, [&](auto& view) {
        const auto ids = view.template Read<PersistentIdComponent>();
        for (std::uint32_t row = 0; row < view.Count(); ++row)
            if (ids[row].Id.IsValid())
                identified.push_back(view.Entity(row));
    });
    return identified;
}

void CaptureZoneState(World& world, ZoneId zone, std::span<const EntityId> identified, ZoneCapture capture)
{
    ZoneStateStore* store = world.TryGetResource<ZoneStateStore>();
    if (store == nullptr || !world.IsRegistered<PersistentIdComponent>())
        return;

    std::vector<std::pair<EntityId, PersistentEntityId>> alive;
    std::vector<PersistentEntityId> live;
    alive.reserve(identified.size());
    live.reserve(identified.size());
    for (const EntityId entity : identified)
        if (const PersistentIdComponent* id = world.IsAlive(entity) ? world.TryGet<PersistentIdComponent>(entity) : nullptr;
            id != nullptr && id->Id.IsValid())
        {
            alive.emplace_back(entity, id->Id);
            live.push_back(id->Id);
        }

    // Before the authored set goes at detach: a record takes its entity's lifetime from it.
    if (const ZoneComponentMemories* memories = world.TryGetResource<ZoneComponentMemories>())
    {
        std::vector<std::byte> bytes;
        for (const ZoneComponentMemory& memory : memories->All())
        {
            const ComponentId component = world.GetComponentIdByType(memory.Type);
            if (component == InvalidComponentId)
                continue;
            for (const auto& [entity, id] : alive)
            {
                if (!world.HasComponent(entity, component))
                    continue;
                bytes.clear();
                memory.Capture(world, entity, bytes);
                if (bytes.empty())
                    store->ForgetComponentState(zone, id, memory.Type);
                else
                    store->RecordComponentState(zone, id, memory.Type, memory.Version, bytes);
            }
        }
    }
    store->RecordCapture(zone, live, capture);
}

void RestoreZoneState(World& world, ZoneId zone, std::span<const EntityId> created)
{
    const ZoneStateStore* store = world.TryGetResource<ZoneStateStore>();
    const ZoneComponentMemories* memories = world.TryGetResource<ZoneComponentMemories>();
    if (store == nullptr || memories == nullptr || !world.IsRegistered<PersistentIdComponent>())
        return;
    for (const ZoneComponentMemory& memory : memories->All())
    {
        const ComponentId component = world.GetComponentIdByType(memory.Type);
        if (component == InvalidComponentId)
            continue;
        for (const EntityId entity : created)
        {
            if (!world.HasComponent(entity, component))
                continue;
            const PersistentIdComponent* id = world.TryGet<PersistentIdComponent>(entity);
            if (id == nullptr || !id->Id.IsValid())
                continue;
            const RememberedComponentState remembered = store->ComponentState(zone, id->Id, memory.Type);
            if (!remembered.Bytes.empty() && remembered.Version <= memory.Version)
                memory.Restore(world, entity, remembered.Version, remembered.Bytes);
        }
    }
}
