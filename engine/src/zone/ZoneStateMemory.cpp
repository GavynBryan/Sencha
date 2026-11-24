#include <zone/ZoneStateMemory.h>

#include <core/logging/Logger.h>
#include <core/logging/LoggingProvider.h>
#include <ecs/Query.h>
#include <ecs/StoragePartitionSet.h>
#include <ecs/World.h>
#include <physics/SensorOverlaps.h>
#include <world/SimulationAuthority.h>
#include <world/identity/PersistentEntityIndex.h>
#include <world/identity/PersistentIdComponent.h>
#include <world/serialization/EntityPatch.h>
#include <world/serialization/SceneSerializationContext.h>
#include <zone/KeptWithZone.h>

#include <algorithm>
#include <string>
#include <utility>

namespace
{
    struct ZoneStateCapture
    {
    };

    void RecordPatches(World& world, ZoneStateStore& store, const ZoneStateSerialization& serialization,
                       ZoneCaptureImager& imager, ZoneStateScope scope,
                       std::span<const std::pair<EntityId, PersistentEntityId>> alive)
    {
        Logger& log = serialization.Context->Logging->GetLogger<ZoneStateCapture>();
        std::vector<std::string> failed;
        std::vector<ComponentTypeId> unchanged;
        for (const auto& [entity, id] : alive)
        {
            const ResidentEntityState resident = store.ResidentOf(scope, id);
            if (resident.Baseline == nullptr || imager.Unchanged(entity, *resident.Image))
                continue;
            unchanged.clear();
            imager.UnchangedRows(entity, *resident.Image, unchanged);
            failed.clear();
            const JsonValue* kept = store.PatchOf(scope, id);
            JsonValue patch = CaptureEntityPatch(world, entity, *resident.Baseline, ComponentPersistenceSet{ .Saved = true },
                                                 *serialization.Serializers, *serialization.Context, kept, &failed,
                                                 unchanged);
            for (const std::string& key : failed)
                log.Error("zone state: {}'s '{}' could not be written; what was kept of it stays",
                          PersistentEntityIdToString(id), key);
            if (!failed.empty() && kept != nullptr)
                for (const std::string& key : failed)
                    if (const JsonValue* member = kept->Find(key))
                        patch.AsObject().emplace_back(key, *member);
            store.RecordPatch(scope, id, std::move(patch));
            *resident.Image = failed.empty() ? imager.Authored(entity, *resident.Baseline) : ZoneCaptureImage{};
        }
    }

    void RecordOccupancy(World& world, ZoneStateStore& store, ZoneStateScope scope,
                         std::span<const std::pair<EntityId, PersistentEntityId>> alive)
    {
        SensorOverlaps* overlaps = world.TryGetResource<SensorOverlaps>();
        std::vector<SensorSeed> pairs;
        if (overlaps != nullptr && (!overlaps->Pairs().empty() || !overlaps->Seeds().empty() || !overlaps->Awaited().empty()))
        {
            std::vector<std::pair<EntityId, PersistentEntityId>> sensors(alive.begin(), alive.end());
            std::ranges::sort(sensors, {}, &std::pair<EntityId, PersistentEntityId>::first);
            const auto sensorId = [&](EntityId sensor) {
                const auto found = std::ranges::lower_bound(sensors, sensor, {}, &std::pair<EntityId, PersistentEntityId>::first);
                return found != sensors.end() && found->first == sensor ? found->second : PersistentEntityId{};
            };
            const auto occupantId = [&](EntityId occupant) {
                const PersistentIdComponent* id = world.IsAlive(occupant) ? world.TryGet<PersistentIdComponent>(occupant) : nullptr;
                return id != nullptr ? id->Id : PersistentEntityId{};
            };
            const auto take = [&](SensorPair pair) {
                const SensorSeed seed{ sensorId(pair.Sensor), occupantId(pair.Activator) };
                if (seed.Sensor.IsValid() && seed.Occupant.IsValid())
                    pairs.push_back(seed);
            };
            for (const SensorPair& pair : overlaps->Pairs())
                take(pair);
            for (const SensorPair& pair : overlaps->Seeds())
                take(pair);
            const PersistentEntityIndex* identities = world.TryGetResource<PersistentEntityIndex>();
            overlaps->ForgetAwaitedIf([&](const SensorSeed& seed) {
                if (!std::ranges::any_of(sensors, [&](const auto& sensor) { return sensor.second == seed.Sensor; }))
                    return false;
                if (identities == nullptr || !world.IsAlive(identities->TryResolve(seed.Occupant)))
                    return true;
                pairs.push_back(seed);
                return false;
            });
        }
        store.RecordOccupied(scope, std::move(pairs));
    }

    void RecordGained(const World& world, ZoneStateStore& store, const ZoneStateSerialization& serialization,
                      ZoneCaptureImager& imager, ZoneStateScope scope,
                      std::span<const std::pair<EntityId, PersistentEntityId>> alive)
    {
        std::vector<GainedEntityState> gained;
        std::vector<std::pair<PersistentEntityId, ZoneCaptureImage>> images;
        if (world.IsRegistered<KeptWithZone>())
        {
            Logger& log = serialization.Context->Logging->GetLogger<ZoneStateCapture>();
            const ComponentId kept = world.GetComponentId<KeptWithZone>();
            std::vector<std::string> failed;
            for (const auto& [entity, id] : alive)
            {
                if (!world.HasComponent(entity, kept))
                    continue;
                const ZoneStateForgetting forgotten = ForgettingOfKept(world, entity);
                if (forgotten == ZoneStateForgetting::AtDetach)
                    continue;
                if (ZoneCaptureImage* image = store.GainedImageOf(scope, id);
                    image != nullptr && imager.Unchanged(entity, *image))
                    if (const GainedEntityState* previous = store.GainedMemberOf(scope, id);
                        previous != nullptr && previous->Forgotten == forgotten)
                    {
                        gained.push_back(*previous);
                        continue;
                    }
                failed.clear();
                JsonValue record = WriteEntityRecord(world, entity, *serialization.Serializers, *serialization.Context,
                                                     { .Declared = { .Authored = true, .Saved = true } },
                                                     SceneRecordPurpose::Save, &failed);
                for (const std::string& key : failed)
                    log.Error("zone state: what the zone gained, {}, left out its '{}', which could not be written",
                              PersistentEntityIdToString(id), key);
                gained.push_back(GainedEntityState{
                    id, forgotten,
                    GainedZoneMember{ std::string(kWholeGainedForm),
                                      EncodeWholeGainedMember(ZoneListEntity{ id, std::move(record) }) } });
                images.emplace_back(id, failed.empty() ? imager.Gained(entity) : ZoneCaptureImage{});
            }
        }
        store.RecordGained(scope, std::move(gained));
        for (auto& [id, image] : images)
            store.RecordGainedImage(scope, id, std::move(image));
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
    {
        ZoneCaptureImager imager(world, *serialization->Serializers);
        RecordPatches(world, *store, *serialization, imager, scope, alive);
        RecordGained(world, *store, *serialization, imager, scope, alive);
        RecordOccupancy(world, *store, scope, alive);
    }
    store->RecordCapture(scope, live, capture);
}
