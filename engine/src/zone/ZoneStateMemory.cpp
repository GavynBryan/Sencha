#include <zone/ZoneStateMemory.h>

#include <core/logging/Logger.h>
#include <core/logging/LoggingProvider.h>
#include <ecs/Query.h>
#include <ecs/StoragePartitionSet.h>
#include <ecs/World.h>
#include <world/SimulationAuthority.h>
#include <world/identity/PersistentIdComponent.h>
#include <world/serialization/EntityPatch.h>
#include <world/serialization/SceneSerializationContext.h>

#include <string>
#include <utility>

namespace
{
    struct ZoneStateCapture
    {
    };

    void RecordPatches(World& world, ZoneStateStore& store, const ZoneStateSerialization& serialization,
                       ZoneStateScope scope, std::span<const std::pair<EntityId, PersistentEntityId>> alive)
    {
        Logger& log = serialization.Context->Logging->GetLogger<ZoneStateCapture>();
        std::vector<std::string> failed;
        for (const auto& [entity, id] : alive)
        {
            const std::vector<AuthoredComponent>* baseline = store.BaselineOf(scope, id);
            if (baseline == nullptr)
                continue;
            failed.clear();
            JsonValue patch = CaptureEntityPatch(world, entity, *baseline, ComponentPersistenceSet{ .Saved = true },
                                                 *serialization.Serializers, *serialization.Context,
                                                 store.PatchOf(scope, id), &failed);
            for (const std::string& key : failed)
                log.Error("zone state: {}'s '{}' could not be written; what was kept of it stays",
                          PersistentEntityIdToString(id), key);
            if (!failed.empty())
                if (const JsonValue* kept = store.PatchOf(scope, id))
                    for (const std::string& key : failed)
                        if (const JsonValue* member = kept->Find(key))
                            patch.AsObject().emplace_back(key, *member);
            store.RecordPatch(scope, id, std::move(patch));
        }
    }
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

void CaptureZoneState(World& world, ZoneStateScope scope, std::span<const EntityId> identified, ZoneCapture capture)
{
    ZoneStateStore* store = world.TryGetResource<ZoneStateStore>();
    if (store == nullptr || !scope.IsValid() || !world.IsRegistered<PersistentIdComponent>())
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

    if (const ZoneStateSerialization* serialization = world.TryGetResource<ZoneStateSerialization>();
        serialization != nullptr && serialization->Serializers != nullptr && serialization->Context != nullptr
        && IsSimulationAuthority(world))
        RecordPatches(world, *store, *serialization, scope, alive);
    store->RecordCapture(scope, live, capture);
}
