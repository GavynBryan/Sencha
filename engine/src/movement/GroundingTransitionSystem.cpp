#include <movement/GroundingTransitionSystem.h>

#include <app/GameContexts.h>
#include <ecs/EntityStore.h>
#include <movement/LocomotionMode.h>
#include <movement/MovementModes.h>
#include <physics/components/CharacterController.h>
#include <world/registry/Registry.h>

namespace
{
    // Base priority for the built-in ground/air request; game modes outrank it.
    constexpr int kGroundingPriority = 1;
}

void RequestGroundingLocomotionModes(EntityStore& world)
{
    if (!world.IsRegistered<CharacterController>() || !world.IsRegistered<LocomotionModeRequest>())
        return;

    const ComponentTypeId onGround = ResolveComponentTypeId<OnGround>();
    const ComponentTypeId inAir = ResolveComponentTypeId<InAir>();

    std::as_const(world).ForEachComponent<CharacterController>(
        [&](EntityId entity, const CharacterController& controller)
    {
        if (!world.HasComponent<LocomotionModeRequest>(entity))
            return;
        RequestLocomotionMode(world, entity, controller.Grounded ? onGround : inAir,
                              kGroundingPriority);
    });
}

void GroundingTransitionSystem::FixedLogic(FixedLogicContext& ctx)
{
    for (Registry* reg : ctx.ActiveRegistries)
        Step(reg->Entities);
}

void GroundingTransitionSystem::Step(EntityStore& world)
{
    RequestGroundingLocomotionModes(world);
}
