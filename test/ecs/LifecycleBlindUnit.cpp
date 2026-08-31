// The trait-blind unit. It includes the component's declaration and nothing
// else: no ComponentTraits specialization is visible here, which is the whole
// point of the unit. Adding an include that drags the traits in would make the
// test pass for the wrong reason.

#include "LifecycleBlindUnit.h"

#include <ecs/CommandBuffer.h>

void AddBlindHookedFromBlindUnit(World& world, EntityId entity, int value)
{
    world.AddComponent<BlindHooked>(entity, BlindHooked{ value });
}

void RemoveBlindHookedFromBlindUnit(World& world, EntityId entity)
{
    world.RemoveComponent<BlindHooked>(entity);
}

void AddBlindHookedViaCommandBufferFromBlindUnit(World& world, EntityId entity, int value)
{
    CommandBuffer commands(world);
    commands.AddComponent<BlindHooked>(entity, BlindHooked{ value });
    commands.Flush();
}

void RemoveBlindHookedViaCommandBufferFromBlindUnit(World& world, EntityId entity)
{
    CommandBuffer commands(world);
    commands.RemoveComponent<BlindHooked>(entity);
    commands.Flush();
}

void AddBlindHookedRunViaCommandBufferFromBlindUnit(
    World& world, const std::vector<EntityId>& entities)
{
    CommandBuffer commands(world);
    for (std::size_t i = 0; i < entities.size(); ++i)
        commands.AddComponent<BlindHooked>(entities[i], BlindHooked{ static_cast<int>(i) });
    commands.Flush();
}

void RemoveBlindHookedRunViaCommandBufferFromBlindUnit(
    World& world, const std::vector<EntityId>& entities)
{
    CommandBuffer commands(world);
    for (const EntityId entity : entities)
        commands.RemoveComponent<BlindHooked>(entity);
    commands.Flush();
}

void InitializeBlindHookedFromBlindUnit(World& world, EntityId entity, int value)
{
    world.InitializeComponent<BlindHooked>(entity, BlindHooked{ value });
}
