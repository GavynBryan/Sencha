#include <app/LevelCarry.h>

#include <app/LevelChangeVocabulary.h>
#include <core/logging/Logger.h>
#include <ecs/PartitionFollowers.h>
#include <ecs/World.h>
#include <logic/ResponsesSystem.h>
#include <math/geometry/3d/RigidMapping.h>
#include <participant/ParticipantLifecycle.h>
#include <world/LevelLandmark.h>
#include <world/RuntimeWorld.h>
#include <world/held/Held.h>
#include <world/held/HeldRecordIndex.h>
#include <world/held/HeldRecords.h>
#include <world/identity/PersistentEntityIndex.h>
#include <world/identity/PersistentIdComponent.h>
#include <world/identity/PersistentIdMint.h>
#include <world/relocation/Relocation.h>
#include <world/scene/SceneInstance.h>
#include <world/transform/TransformComponents.h>
#include <world/transform/TransformHierarchy.h>
#include <world/transform/TransformHistory.h>
#include <zone/KeptWithZone.h>
#include <zone/ZonePlacement.h>
#include <zone/ZoneStateMemory.h>
#include <zone/ZoneStateStore.h>

#include <utility>

namespace
{
    Transform3f PoseOf(const World& world, EntityId entity)
    {
        if (const WorldTransform* placed = world.IsRegistered<WorldTransform>() ? world.TryGet<WorldTransform>(entity) : nullptr)
            return placed->Value;
        const LocalTransform* local = world.IsRegistered<LocalTransform>() ? world.TryGet<LocalTransform>(entity) : nullptr;
        return local != nullptr ? local->Value : Transform3f::Identity();
    }

    PersistentEntityId IdOf(const World& world, EntityId entity)
    {
        const PersistentIdComponent* identity =
            world.IsRegistered<PersistentIdComponent>() ? world.TryGet<PersistentIdComponent>(entity) : nullptr;
        return identity != nullptr ? identity->Id : PersistentEntityId{};
    }

    void TakeAnIdOfItsOwnIfTheLevelHoldsItsOwn(World& world, EntityId entity)
    {
        const PersistentEntityId id = IdOf(world, entity);
        if (!id.IsValid())
            return;
        EntityId twin;
        std::as_const(world).ForEachComponent<PersistentIdComponent>([&](EntityId other, const PersistentIdComponent& held) {
            if (other != entity && held.Id == id)
                twin = other;
        });
        if (!twin.IsValid())
            return;
        const PersistentEntityId fresh = MintRuntimePersistentId(world);
        if (!fresh.IsValid())
            return;
        (void)world.ReplaceComponent<PersistentIdComponent>(entity, PersistentIdComponent{ fresh });
        if (PersistentEntityIndex* index = world.TryGetResource<PersistentEntityIndex>())
            (void)index->Register(id, twin);
        if (const HeldRecordIndex* records = world.TryGetResource<HeldRecordIndex>())
        {
            const std::span<const EntityId> listed = records->RecordsOf(entity);
            const std::vector<EntityId> held(listed.begin(), listed.end());
            for (const EntityId record : held)
                NameHeldHolder(world, record);
        }
    }
}

CarryRefusal RefuseCarrying(const World& world, std::span<const EntityId> entities, std::string_view landmark)
{
    if (landmark.empty() || !FindLevelLandmark(world, landmark).has_value())
        return CarryRefusal::NoLandmark;
    for (const EntityId entity : entities)
    {
        if (!world.IsAlive(entity))
            return CarryRefusal::NotCarriable;
        const bool parented = world.IsRegistered<Parent>() && world.HasComponent<Parent>(entity);
        const bool held = world.IsRegistered<Held>() && world.HasComponent<Held>(entity);
        const bool childed = RootAndDescendants(world, entity).size() > 1;
        const bool instanceRoot = world.IsRegistered<SceneInstance>() && world.HasComponent<SceneInstance>(entity);
        if (parented || held || ParticipantWithBody(world, entity).IsValid() || (childed && !instanceRoot))
            return CarryRefusal::NotCarriable;
    }
    return CarryRefusal::None;
}

void LevelCarry::TakeOut(RuntimeWorld& runtime, std::span<const EntityId> entities, std::string landmark,
                         const ScopeOf& scopeOf)
{
    World& world = runtime.Entities();
    const std::optional<LevelLandmarkPlace> from = FindLevelLandmark(world, landmark);
    if (!from.has_value() || entities.empty())
        return;
    Landmark = std::move(landmark);
    Left = from->Place;
    ZoneStateStore* store = world.TryGetResource<ZoneStateStore>();
    const StoragePartitionId holding = runtime.HoldingPartition();
    for (const EntityId entity : entities)
    {
        if (!world.IsAlive(entity))
            continue;
        Carried carried{ .Root = entity, .Group = RootAndDescendants(world, entity) };
        if (const PersistentEntityId id = IdOf(world, entity); id.IsValid() && store != nullptr && scopeOf)
            store->RecordDeparted(scopeOf(entity), id);
        for (const EntityId member : carried.Group)
            if (world.IsAlive(member) && world.GetEntityPartition(member) != holding)
                (void)world.MoveEntityToPartition(member, holding);
        Held.push_back(std::move(carried));
    }
    (void)DispatchPartitionMoves(world);
}

void LevelCarry::Arrive(RuntimeWorld& runtime, ResponsesSystem* responses, Logger& log)
{
    if (Held.empty())
        return;
    World& world = runtime.Entities();
    const std::optional<LevelLandmarkPlace> to = FindLevelLandmark(world, Landmark);
    if (!to.has_value())
    {
        Drop(runtime, log, "the level it entered has no loaded landmark named '" + Landmark + "'");
        return;
    }
    const ZonePlacement* zones = world.TryGetResource<ZonePlacement>();
    const Transform3f mapping = to->Place * InverseRigidMapping(Left);
    std::vector<Carried> arriving = std::move(Held);
    Held.clear();
    for (const Carried& carried : arriving)
    {
        if (!world.IsAlive(carried.Root))
            continue;
        const Vec3d place = mapping.TransformPoint(PoseOf(world, carried.Root).Position);
        const std::optional<StoragePartitionId> zone =
            zones != nullptr && zones->PartitionAt ? zones->PartitionAt(place) : std::nullopt;
        if (!zone.has_value())
        {
            Held.push_back(carried);
            Drop(runtime, log, "no zone of the level it entered holds its place");
            continue;
        }
        const RelocationResult moved =
            RelocateEntity(world, carried.Root, RigidRelocation{ .Mapping = mapping, .Partition = zone });
        if (moved.Outcome != RelocationOutcome::Relocated)
        {
            Held.push_back(carried);
            Drop(runtime, log, moved.Component.empty() ? std::string("it could not be relocated")
                                                       : "it holds " + std::string(moved.Component)
                                                             + ", which refuses relocation");
            continue;
        }
        // Out of every simulation for the load: no motion to carry across.
        for (const EntityId member : carried.Group)
            if (world.IsAlive(member))
                RequestTransformHistorySnap(world, member);
        TakeAnIdOfItsOwnIfTheLevelHoldsItsOwn(world, carried.Root);
        if (KeepsLevelState(world))
            (void)KeepArrivedWithZone(world, carried.Root);
        if (responses != nullptr)
            (void)responses->Deliver(to->Entity, LevelArrived{ .Instigator = carried.Root });
    }
}

void LevelCarry::Drop(RuntimeWorld& runtime, Logger& log, std::string_view why)
{
    if (Held.empty())
        return;
    World& world = runtime.Entities();
    std::size_t dropped = 0;
    for (const Carried& carried : Held)
    {
        dropped += world.IsAlive(carried.Root) ? 1 : 0;
        for (auto member = carried.Group.rbegin(); member != carried.Group.rend(); ++member)
            if (world.IsAlive(*member))
                world.DestroyEntity(*member);
    }
    Held.clear();
    log.Error("level change: {} carried entities are lost: {}", dropped, why);
}
