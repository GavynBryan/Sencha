#include <physics/PhysicsStepSystem.h>

#include <app/GameContexts.h>
#include <ecs/EntityStore.h>
#include <physics/PhysicsScene.h>
#include <world/registry/Registry.h>

PhysicsStepSystem::PhysicsStepSystem()
{
    Simulation.SetShapeCache(&Shapes);
}

PhysicsStepSystem::~PhysicsStepSystem() = default;

void PhysicsStepSystem::Physics(PhysicsContext& ctx)
{
    const float dt = static_cast<float>(ctx.Time.DeltaSeconds);

    for (Registry* reg : ctx.ActiveRegistries)
    {
        EntityStore& world = reg->Entities;
        reg->Resources.Ensure<PhysicsScene>(Simulation).SyncToPhysics(world);
    }

    Simulation.Step(dt, CollisionSteps);

    for (Registry* reg : ctx.ActiveRegistries)
    {
        EntityStore& world = reg->Entities;
        if (PhysicsScene* scene = reg->Resources.TryGet<PhysicsScene>())
            scene->SyncFromPhysics(world);
    }
}
