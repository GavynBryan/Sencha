#include <physics/PhysicsStepSystem.h>

#include <app/GameContexts.h>
#include <ecs/World.h>
#include <physics/PhysicsScene.h>
#include <world/registry/Registry.h>

namespace
{
PhysicsScene& EnsureScene(World& world, PhysicsWorld& physics)
{
    if (world.HasResource<PhysicsScene>())
        return world.GetResource<PhysicsScene>();
    return world.AddResource<PhysicsScene>(physics);
}
} // namespace

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
        World& world = reg->Components;
        EnsureScene(world, Simulation).SyncToPhysics(world);
    }

    Simulation.Step(dt, CollisionSteps);

    for (Registry* reg : ctx.ActiveRegistries)
    {
        World& world = reg->Components;
        if (PhysicsScene* scene = world.TryGetResource<PhysicsScene>())
            scene->SyncFromPhysics(world);
    }
}
