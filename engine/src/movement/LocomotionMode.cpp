#include <movement/LocomotionMode.h>

#include <app/GameContexts.h>
#include <ecs/ComponentId.h>
#include <ecs/World.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <world/registry/Registry.h>

#include <utility>
#include <vector>

void LocomotionModeRegistry::Register(ComponentTypeId marker, GameplayTagId activeTag)
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

GameplayTagId LocomotionModeRegistry::TagFor(ComponentTypeId marker) const
{
    for (const LocomotionModeEntry& entry : Modes)
        if (entry.Marker == marker)
            return entry.ActiveTag;
    return GameplayTagId{};
}

void RequestLocomotionMode(World& world, EntityId entity, ComponentTypeId marker, int priority)
{
    LocomotionModeRequest* request = world.TryGet<LocomotionModeRequest>(entity);
    if (request == nullptr)
        return;
    if (priority > request->Priority)
    {
        request->Priority = priority;
        request->Marker = marker;
    }
}

void ApplyLocomotionModes(World& world)
{
    if (!world.IsRegistered<LocomotionModeRequest>())
        return;
    const LocomotionModeRegistry* registry = std::as_const(world).TryGetResource<LocomotionModeRegistry>();
    if (registry == nullptr)
        return;

    // Marker swaps are structural, so collect during iteration and apply after
    // (World type-erased ops assert no active query). We record every requesting
    // entity (not just the ones that change marker) so the projected tag is kept
    // consistent each tick, including the spawn-bootstrap case.
    struct Resolved
    {
        EntityId Entity;
        ComponentId Current; // marker id the entity holds now, or invalid
        ComponentId Desired; // marker id the winning request wants
        ComponentTypeId DesiredType;
    };
    std::vector<Resolved> resolved;

    world.ForEachComponent<LocomotionModeRequest>([&](EntityId entity, LocomotionModeRequest& request)
    {
        if (request.Priority <= 0)
            return;
        const ComponentTypeId desiredType = request.Marker;
        request.Priority = 0;              // consume the request this tick
        request.Marker = ComponentTypeId{};

        const ComponentId desired = world.GetComponentIdByType(desiredType);
        if (desired == InvalidComponentId)
            return; // marker not registered in this World

        ComponentId current = InvalidComponentId;
        for (const LocomotionModeEntry& entry : registry->Entries())
        {
            const ComponentId id = world.GetComponentIdByType(entry.Marker);
            if (id != InvalidComponentId && world.HasComponent(entity, id))
            {
                current = id;
                break;
            }
        }
        resolved.push_back({ entity, current, desired, desiredType });
    });

    for (const Resolved& r : resolved)
    {
        if (r.Current != r.Desired)
        {
            if (r.Current != InvalidComponentId)
                world.RemoveComponentRaw(r.Entity, r.Current, nullptr);
            if (!world.HasComponent(r.Entity, r.Desired))
                world.AddComponentRaw(r.Entity, r.Desired, nullptr, 0, 1, nullptr);
        }

        // Project exactly the current mode's gameplay tag (mutual exclusion),
        // idempotently -- so gating tags are correct even without a marker change.
        if (GameplayTagContainer* tags = world.TryGet<GameplayTagContainer>(r.Entity))
        {
            const GameplayTagId desiredTag = registry->TagFor(r.DesiredType);
            for (const LocomotionModeEntry& entry : registry->Entries())
                if (entry.Marker != r.DesiredType && entry.ActiveTag.IsValid())
                    tags->Revoke(entry.ActiveTag);
            if (desiredTag.IsValid() && !tags->HasExact(desiredTag))
                tags->Grant(desiredTag);
        }
    }
}

void LocomotionModeArbiter::FixedLogic(FixedLogicContext& ctx)
{
    for (Registry* reg : ctx.ActiveRegistries)
        ApplyLocomotionModes(reg->Components);
}
