#include <physics/PhysicsStepSystem.h>

#include <app/GameContexts.h>
#include <ecs/World.h>
#include <physics/CharacterMoverPool.h>
#include <physics/RigidBodyBinding.h>
#include <world/registry/Registry.h>

PhysicsStepSystem::PhysicsStepSystem()
{
    Simulation.SetShapeCache(&Shapes);
}

PhysicsStepSystem::~PhysicsStepSystem() = default;

void PhysicsStepSystem::RegistryResidency(RegistryResidencyContext& ctx)
{
    for (const RegistryResidencyChange& change : ctx.Changes)
    {
        // Only exits from the physics domain need work: Attached starts with
        // no backend state, and entering restores through the ordinary
        // reconcile. Detaching arrives here as Previous.Physics -> false.
        if (!change.Previous.Physics || change.Current.Physics)
            continue;

        World& world = change.Instance->Components;
        if (RigidBodyBinding* binding = change.Instance->Resources.TryGet<RigidBodyBinding>())
            binding->Evict(world);
        if (CharacterMoverPool* pool = change.Instance->Resources.TryGet<CharacterMoverPool>())
            pool->Evict(world);
    }
}

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
