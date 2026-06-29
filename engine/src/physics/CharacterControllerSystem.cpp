#include <physics/CharacterControllerSystem.h>

#include <app/GameContexts.h>
#include <ecs/World.h>
#include <physics/CharacterMoverPool.h>
#include <physics/PhysicsStepSystem.h>
#include <physics/PhysicsWorld.h>
#include <physics/components/CharacterController.h>
#include <world/registry/Registry.h>

CharacterControllerSystem::CharacterControllerSystem(PhysicsStepSystem& step)
    : Step(&step)
{
}

void CharacterControllerSystem::Physics(PhysicsContext& ctx)
{
    PhysicsWorld& physics = Step->GetSimulation();
    const float dt = static_cast<float>(ctx.Time.DeltaSeconds);

    for (Registry* reg : ctx.ActiveRegistries)
    {
        World& world = reg->Components;
        if (!world.IsRegistered<CharacterController>())
            continue;

        CharacterMoverPool& pool = world.HasResource<CharacterMoverPool>()
            ? world.GetResource<CharacterMoverPool>()
            : world.AddResource<CharacterMoverPool>(physics);

        pool.Reconcile(world);
        pool.Drive(world, dt, Gravity);
    }
}
