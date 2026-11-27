#include <app/LevelCarry.h>

#include <app/LevelChangeVocabulary.h>
#include <core/logging/Logger.h>
#include <ecs/PartitionFollowers.h>
#include <ecs/World.h>
#include <logic/ResponsesSystem.h>
#include <logic/decision/DecisionAgentSystem.h>
#include <movement/components/CharacterFacts.h>
#include <navigation/NavAgentRequests.h>
#include <participant/ParticipantLifecycle.h>
#include <perception/ObservationMemory.h>
#include <physics/SensorOverlaps.h>
#include <physics/components/RigidBody.h>
#include <world/LevelLandmark.h>
#include <world/RuntimeWorld.h>
#include <world/held/Held.h>
#include <world/held/HeldRecordIndex.h>
#include <world/held/HeldRecords.h>
#include <world/identity/PersistentEntityIndex.h>
#include <world/identity/PersistentIdComponent.h>
#include <world/identity/PersistentIdMint.h>
#include <world/scene/SceneInstance.h>
#include <world/transform/TransformComponents.h>
#include <world/transform/TransformHistory.h>
#include <zone/KeptWithZone.h>
#include <zone/ZonePlacement.h>
#include <zone/ZoneStateMemory.h>
#include <zone/ZoneStateStore.h>

#include <algorithm>
#include <unordered_map>
#include <utility>

namespace
{
    using ChildrenOf = std::unordered_map<EntityId, std::vector<EntityId>, EntityIdHash>;

    ChildrenOf Children(const World& world)
    {
        ChildrenOf children;
        if (world.IsRegistered<Parent>())
            world.ForEachComponent<Parent>(
                [&](EntityId child, const Parent& parent) { children[parent.Entity].push_back(child); });
        return children;
    }

    void Gather(const ChildrenOf& children, EntityId entity, std::vector<EntityId>& group)
    {
        group.push_back(entity);
        if (const auto found = children.find(entity); found != children.end())
            for (const EntityId child : found->second)
                Gather(children, child, group);
    }

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

    void Turn(World& world, EntityId entity, const Quatf& turn)
    {
        if (RigidBody* body = world.IsRegistered<RigidBody>() ? world.TryGet<RigidBody>(entity) : nullptr)
        {
            body->LinearVelocity = turn.RotateVector(body->LinearVelocity);
            body->AngularVelocity = turn.RotateVector(body->AngularVelocity);
        }
        if (KinematicState* motion = world.IsRegistered<KinematicState>() ? world.TryGet<KinematicState>(entity) : nullptr)
            motion->Velocity = turn.RotateVector(motion->Velocity);
    }

    void ForgetTheLevelItLeft(World& world, const std::vector<EntityId>& group)
    {
        const EntityId root = group.front();
        ForgetDecisionPlace(world, root);
        for (const EntityId member : group)
            if (ObservationMemory* memory =
                    world.IsRegistered<ObservationMemory>() ? world.TryGet<ObservationMemory>(member) : nullptr)
            {
                memory->Count = 0;
                std::ranges::fill(memory->Entries, ObservationMemoryEntry{});
            }
        if (SensorOverlaps* overlaps = world.TryGetResource<SensorOverlaps>())
        {
            const auto touches = [&](const SensorPair& pair) {
                return std::ranges::find(group, pair.Sensor) != group.end()
                    || std::ranges::find(group, pair.Activator) != group.end();
            };
            overlaps->ForgetIf(touches);
        }
    }

    void OrderAgain(World& world, EntityId agent, const Transform3f& left, const Transform3f& arrived)
    {
        const NavAgentRequest* order =
            world.IsRegistered<NavAgentRequest>() ? world.TryGet<NavAgentRequest>(agent) : nullptr;
        if (order == nullptr || order->Kind == NavDestinationKind::None)
            return;
        NavDestination destination;
        destination.Kind = order->Kind;
        destination.Point = arrived.TransformPoint(left.InverseTransformPoint(order->Point));
        destination.Target = order->Target;
        destination.Follow = order->Follow;
        destination.ArriveRadius = order->ArriveRadius;
        destination.SpeedScale = order->SpeedScale;
        (void)RequestNavigation(world, agent, destination);
    }
}

CarryRefusal RefuseCarrying(const World& world, std::span<const EntityId> entities, std::string_view landmark)
{
    if (landmark.empty() || !FindLevelLandmark(world, landmark).has_value())
        return CarryRefusal::NoLandmark;
    const ChildrenOf children = Children(world);
    for (const EntityId entity : entities)
    {
        const bool parented = world.IsRegistered<Parent>() && world.HasComponent<Parent>(entity);
        const bool held = world.IsRegistered<Held>() && world.HasComponent<Held>(entity);
        const bool childed = children.contains(entity);
        const bool instanceRoot = world.IsRegistered<SceneInstance>() && world.HasComponent<SceneInstance>(entity);
        if (!world.IsAlive(entity) || parented || held || ParticipantWithBody(world, entity).IsValid()
            || (childed && !instanceRoot))
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
    const ChildrenOf children = Children(world);
    ZoneStateStore* store = world.TryGetResource<ZoneStateStore>();
    const StoragePartitionId holding = runtime.HoldingPartition();
    for (const EntityId entity : entities)
    {
        if (!world.IsAlive(entity))
            continue;
        const Transform3f pose = PoseOf(world, entity);
        Carried carried{ .Root = entity,
                         .Group = {},
                         .Offset = Transform3f{ Left.InverseTransformPoint(pose.Position),
                                                Left.Rotation.Conjugate() * pose.Rotation, pose.Scale } };
        if (const PersistentEntityId id = IdOf(world, entity); id.IsValid() && store != nullptr && scopeOf)
            store->RecordDeparted(scopeOf(entity), id);
        Gather(children, entity, carried.Group);
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
    const Quatf turn = to->Place.Rotation * Left.Rotation.Conjugate();
    std::vector<Carried> arriving = std::move(Held);
    Held.clear();
    for (const Carried& carried : arriving)
    {
        if (!world.IsAlive(carried.Root))
            continue;
        const Transform3f place = to->Place * carried.Offset;
        const std::optional<StoragePartitionId> zone =
            zones != nullptr && zones->PartitionAt ? zones->PartitionAt(place.Position) : std::nullopt;
        if (!zone.has_value())
        {
            Held.push_back(carried);
            Drop(runtime, log, "no zone of the level it entered holds its place");
            continue;
        }
        for (const EntityId member : carried.Group)
            if (world.IsAlive(member))
                (void)world.MoveEntityToPartition(member, *zone);
        (void)DispatchPartitionMoves(world);
        if (LocalTransform* local = world.TryGet<LocalTransform>(carried.Root))
            local->Value = place;
        if (WorldTransform* placed = world.TryGet<WorldTransform>(carried.Root))
            placed->Value = place;
        RequestTransformHistorySnap(world, carried.Root);
        Turn(world, carried.Root, turn);
        TakeAnIdOfItsOwnIfTheLevelHoldsItsOwn(world, carried.Root);
        if (KeepsLevelState(world))
            (void)KeepArrivedWithZone(world, carried.Root);
        ForgetTheLevelItLeft(world, carried.Group);
        OrderAgain(world, carried.Root, Left, to->Place);
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
