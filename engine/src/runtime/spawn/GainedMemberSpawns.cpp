#include <runtime/spawn/GainedMemberSpawns.h>

#include <core/logging/Logger.h>
#include <core/logging/LoggingProvider.h>
#include <ecs/World.h>
#include <world/RuntimeWorld.h>
#include <world/held/HeldRecords.h>
#include <world/identity/PersistentEntityIndex.h>
#include <world/transform/TransformComponents.h>
#include <world/transform/TransformHistory.h>
#include <zone/GainedSpawnRequests.h>
#include <zone/KeptWithZone.h>
#include <zone/ZoneCaptureImage.h>
#include <zone/ZoneStateStore.h>

#include <algorithm>
#include <utility>

GainedMemberSpawns::GainedMemberSpawns(RuntimeWorld& world, SceneSpawnService& spawns, LoggingProvider& logging)
    : WorldState(world)
    , Spawns(spawns)
    , Logging(logging)
{
}

void GainedMemberSpawns::Request()
{
    World& world = WorldState.Entities();
    GainedSpawnRequests* requests = world.TryGetResource<GainedSpawnRequests>();
    const auto asked = [&](ZoneStateScope scope, PersistentEntityId id, StoragePartitionId partition) {
        return requests != nullptr && std::ranges::any_of(requests->Entries(), [&](const GainedSpawnRequest& request) {
                   return request.Scope == scope && request.Id == id && request.Partition == partition;
               });
    };
    std::erase_if(InFlight, [&](const Spawning& spawning) {
        if (asked(spawning.Scope, spawning.Id, spawning.Partition))
            return false;
        (void)Spawns.RequestDespawn(spawning.Spawn);
        return true;
    });
    if (requests == nullptr || requests->Entries().empty())
        return;

    const PersistentEntityIndex* identities = world.TryGetResource<PersistentEntityIndex>();
    const std::vector<GainedSpawnRequest> waiting(requests->Entries().begin(), requests->Entries().end());
    for (const GainedSpawnRequest& request : waiting)
    {
        if (std::ranges::any_of(InFlight, [&](const Spawning& spawning) {
                return spawning.Scope == request.Scope && spawning.Id == request.Id;
            }))
            continue;
        if (request.Partition != PersistentStoragePartition)
        {
            const RuntimeZoneRecord* zone = WorldState.FindZone(request.Scope.Zone);
            if (zone == nullptr || zone->Partition != request.Partition || zone->StateScope != request.Scope)
            {
                requests->Remove(request.Scope, request.Id);
                continue;
            }
            if (zone->State != RuntimeZoneLoadState::Resident)
                continue;
        }
        if (identities != nullptr && world.IsAlive(identities->TryResolve(request.Id)))
        {
            Logging.GetLogger<GainedMemberSpawns>().Warn(
                "zone state: what the zone gained, {}, is not made; a live entity already has its id",
                PersistentEntityIdToString(request.Id));
            requests->Remove(request.Scope, request.Id);
            continue;
        }
        const SceneSpawnId spawn = Spawns.RequestSpawn(request.Source, request.Root, request.Partition,
                                                       SceneSpawnLifetime::Independent,
                                                       SceneSpawnOverlay{ request.Patch, request.Id });
        InFlight.push_back(Spawning{ request.Scope, request.Partition, request.Id, request.Root, spawn });
    }
}

void GainedMemberSpawns::Settle()
{
    World& world = WorldState.Entities();
    GainedSpawnRequests* requests = world.TryGetResource<GainedSpawnRequests>();
    const std::vector<Spawning> settling = InFlight;
    for (const Spawning& spawning : settling)
    {
        const SceneSpawnStatus status = Spawns.Status(spawning.Spawn);
        if (status == SceneSpawnStatus::Pending)
            continue;
        std::erase_if(InFlight, [&](const Spawning& entry) { return entry.Spawn == spawning.Spawn; });
        if (requests != nullptr)
            requests->Remove(spawning.Scope, spawning.Id);
        const EntityId root = status == SceneSpawnStatus::Live ? Spawns.Root(spawning.Spawn) : EntityId{};
        if (!root.IsValid() || !KeepWithZone(world, root))
        {
            if (status == SceneSpawnStatus::Live)
                (void)Spawns.RequestDespawn(spawning.Spawn);
            Forget(spawning, status == SceneSpawnStatus::Live ? "was published without a single root it could keep"
                                                              : "did not spawn from its source");
            continue;
        }
        if (LocalTransform* placed = world.TryGet<LocalTransform>(root))
        {
            placed->Value = spawning.Root;
            RequestTransformHistorySnap(world, root);
        }
        const ZoneStateSerialization* serialization = world.TryGetResource<ZoneStateSerialization>();
        if (ZoneStateStore* store = world.TryGetResource<ZoneStateStore>();
            store != nullptr && serialization != nullptr && serialization->Serializers != nullptr)
            store->RecordGainedImage(spawning.Scope, spawning.Id,
                                     ZoneCaptureImager(world, *serialization->Serializers).Gained(root));
    }
}

void GainedMemberSpawns::Forget(const Spawning& spawning, const char* why)
{
    World& world = WorldState.Entities();
    Logger& log = Logging.GetLogger<GainedMemberSpawns>();
    const std::string named = PersistentEntityIdToString(spawning.Id);
    log.Error("zone state: what the zone gained, {}, {}; it is dropped, and the spawn service logged why", named, why);
    if (ZoneStateStore* store = world.TryGetResource<ZoneStateStore>())
        store->ForgetGained(spawning.Scope, spawning.Id);
    if (const std::size_t dropped = DropRecordsAwaiting(world, spawning.Id); dropped > 0)
        log.Error("zone state: {} held records waiting for {}, which did not arrive, are dropped", dropped, named);
}
