#include <physics/PhysicsStepSystem.h>

#include <app/GameContexts.h>
#include <ecs/World.h>
#include <physics/RigidBodyBinding.h>
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
        RigidBodyBinding& binding = reg->Resources.Ensure<RigidBodyBinding>(Simulation);
        binding.SyncToPhysics(reg->Components);
    }

    Simulation.Step(dt, CollisionSteps);

    for (Registry* reg : ctx.ActiveRegistries)
    {
        if (RigidBodyBinding* binding = reg->Resources.TryGet<RigidBodyBinding>())
            binding->SyncFromPhysics(reg->Components);
    }
}
