#include <physics/CharacterControllerSystem.h>

#include <app/GameContexts.h>
#include <ecs/EntityStore.h>
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
        EntityStore& world = reg->Entities;
        if (!world.IsRegistered<CharacterController>())
            continue;

        CharacterMoverPool& pool = reg->Resources.Ensure<CharacterMoverPool>(physics);

        pool.Reconcile(world);
        pool.Drive(world, dt, Gravity);
    }
}
