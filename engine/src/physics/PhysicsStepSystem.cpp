#include <physics/PhysicsStepSystem.h>

#include <app/GameContexts.h>
#include <ecs/World.h>
#include <physics/CharacterMoverPool.h>
#include <physics/RigidBodyBinding.h>
#include <physics/components/CharacterController.h>
#include <physics/components/Collider.h>
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
        Registry& registry = *change.Instance;
        World& world = registry.Components;

        // Detaching is an unconditional final visit. Do not infer that an
        // attached-but-never-framed registry has no backend state: finalizers and
        // tools may have materialized bindings before requesting destruction.
        if (change.Kind == RegistryResidencyChangeKind::Detaching)
        {
            if (CharacterMoverPool* pool = registry.Resources.TryGet<CharacterMoverPool>())
                pool->Evict(world);
            if (RigidBodyBinding* binding = registry.Resources.TryGet<RigidBodyBinding>())
                binding->Evict(world);
            continue;
        }

        const bool wasPhysics = change.Previous.Physics;
        const bool isPhysics = change.Current.Physics;

        if (wasPhysics && !isPhysics)
        {
            if (CharacterMoverPool* pool = registry.Resources.TryGet<CharacterMoverPool>())
                pool->Evict(world);
            if (RigidBodyBinding* binding = registry.Resources.TryGet<RigidBodyBinding>())
                binding->Evict(world);
            continue;
        }

        if (!wasPhysics && isPhysics)
        {
            // Restore before the new frame view exposes the registry to physics.
            // The ordinary reconcile remains the one creation path; residency
            // simply runs it at the correct boundary instead of waiting for the
            // first fixed tick.
            if (world.IsRegistered<Collider>())
            {
                RigidBodyBinding& binding =
                    registry.Resources.Ensure<RigidBodyBinding>(Simulation);
                binding.SyncToPhysics(world);
            }
            if (world.IsRegistered<CharacterController>())
            {
                CharacterMoverPool& pool =
                    registry.Resources.Ensure<CharacterMoverPool>(Simulation);
                pool.Reconcile(world);
            }
        }
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
