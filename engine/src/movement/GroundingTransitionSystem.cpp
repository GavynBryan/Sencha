#include <movement/GroundingTransitionSystem.h>

#include <app/GameContexts.h>
#include <ecs/World.h>
#include <movement/LocomotionMode.h>
#include <movement/MovementModes.h>
#include <physics/components/CharacterController.h>
#include <world/registry/Registry.h>

namespace
{
    // Base priority for the built-in ground/air request; game modes outrank it.
    constexpr int kGroundingPriority = 1;
}

void RequestGroundingLocomotionModes(World& world)
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
        Step(reg->Components);
}

void GroundingTransitionSystem::Step(World& world)
{
    RequestGroundingLocomotionModes(world);
}
