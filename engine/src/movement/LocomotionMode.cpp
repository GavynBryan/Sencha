#include <movement/LocomotionMode.h>

#include <app/GameContexts.h>
#include <ecs/ComponentId.h>
#include <ecs/Query.h>
#include <ecs/StoragePartitionSet.h>
#include <ecs/World.h>
#include <gameplay_tags/GameplayTagContainer.h>

#include <utility>
#include <vector>

void LocomotionModeRegistry::Register(
    ComponentTypeId marker,
    GameplayTagId activeTag)
{
    for (LocomotionModeEntry& entry : Modes)
    {
        if (entry.Marker == marker)
        {
            entry.ActiveTag = activeTag;
            return;
        }
    }
    Modes.push_back({ marker, activeTag });
}

GameplayTagId LocomotionModeRegistry::TagFor(
    ComponentTypeId marker) const
{
    for (const LocomotionModeEntry& entry : Modes)
        if (entry.Marker == marker)
            return entry.ActiveTag;
    return GameplayTagId{};
}

void RequestLocomotionMode(
    World& world,
    EntityId entity,
    ComponentTypeId marker,
    int priority)
{
    LocomotionModeRequest* request =
        world.TryGet<LocomotionModeRequest>(entity);
    if (request != nullptr && priority > request->Priority)
    {
        request->Priority = priority;
        request->Marker = marker;
    }
}

namespace
{
struct ResolvedMode
{
    EntityId Entity;
    ComponentId Current;
    ComponentId Desired;
    ComponentTypeId DesiredType;
};

void ApplyLocomotionModesImpl(
    World& world,
    const StoragePartitionSet* partitions)
{
    if (!world.IsRegistered<LocomotionModeRequest>())
        return;
    const LocomotionModeRegistry* registry =
        std::as_const(world).TryGetResource<LocomotionModeRegistry>();
    if (registry == nullptr)
        return;

    std::vector<ResolvedMode> resolved;
    Query<Write<LocomotionModeRequest>> query(world);
    const auto visit = [&](auto& view)
    {
        auto requests = view.template Write<LocomotionModeRequest>();
        for (std::uint32_t i = 0; i < view.Count(); ++i)
        {
            LocomotionModeRequest& request = requests[i];
            if (request.Priority <= 0)
                continue;

            const EntityId entity = view.Entity(i);
            const ComponentTypeId desiredType = request.Marker;
            request.Priority = 0;
            request.Marker = ComponentTypeId{};

            const ComponentId desired =
                world.GetComponentIdByType(desiredType);
            if (desired == InvalidComponentId)
                continue;

            ComponentId current = InvalidComponentId;
            for (const LocomotionModeEntry& entry : registry->Entries())
            {
                const ComponentId id =
                    world.GetComponentIdByType(entry.Marker);
                if (id != InvalidComponentId
                    && world.HasComponent(entity, id))
                {
                    current = id;
                    break;
                }
            }
            resolved.push_back(
                { entity, current, desired, desiredType });
        }
    };

    if (partitions != nullptr)
        query.ForEachChunkIn(*partitions, visit);
    else
        query.ForEachChunk(visit);

    for (const ResolvedMode& mode : resolved)
    {
        if (mode.Current != mode.Desired)
        {
            if (mode.Current != InvalidComponentId)
                world.RemoveComponentRaw(mode.Entity, mode.Current, nullptr);
            if (!world.HasComponent(mode.Entity, mode.Desired))
            {
                world.AddComponentRaw(
                    mode.Entity,
                    mode.Desired,
                    nullptr,
                    0,
                    1,
                    nullptr);
            }
        }

        if (GameplayTagContainer* tags =
                world.TryGet<GameplayTagContainer>(mode.Entity))
        {
            const GameplayTagId desiredTag =
                registry->TagFor(mode.DesiredType);
            for (const LocomotionModeEntry& entry : registry->Entries())
            {
                if (entry.Marker != mode.DesiredType
                    && entry.ActiveTag.IsValid())
                {
                    tags->Revoke(entry.ActiveTag);
                }
            }
            if (desiredTag.IsValid() && !tags->HasExact(desiredTag))
                tags->Grant(desiredTag);
        }
    }
}
} // namespace

void ApplyLocomotionModes(World& world)
{
    ApplyLocomotionModesImpl(world, nullptr);
}

void ApplyLocomotionModes(
    World& world,
    const StoragePartitionSet& partitions)
{
    ApplyLocomotionModesImpl(world, &partitions);
}

void LocomotionModeArbiter::FixedLogic(FixedLogicContext& ctx)
{
    ApplyLocomotionModes(ctx.Entities, ctx.Partitions);
}
