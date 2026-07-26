#include <physics/PhysicsStepSystem.h>

#include <app/GameContexts.h>
#include <world/RuntimeWorld.h>

PhysicsStepSystem::PhysicsStepSystem()
    : Bodies(Simulation)
    , Characters(Simulation)
{
    Simulation.SetShapeCache(&Shapes);
}

PhysicsStepSystem::~PhysicsStepSystem() = default;

void PhysicsStepSystem::ZoneResidency(ZoneResidencyContext& ctx)
{
    for (const ZoneResidencyChange& change : ctx.Changes)
    {
        const bool detaching =
            change.Kind == ZoneResidencyChangeKind::Detaching;
        const bool leftPhysics =
            change.Previous.Physics && !change.Current.Physics;
        if (!detaching && !leftPhysics)
            continue;

        // Final visit happens while the partition and its entities are still
        // alive. Capture dynamic state and strip backend links before RuntimeWorld
        // destroys the partition.
        Characters.EvictPartition(ctx.Entities, change.Partition);
        Bodies.EvictPartition(ctx.Entities, change.Partition);
    }
}

void PhysicsStepSystem::Physics(PhysicsContext& ctx)
{
    const float dt = static_cast<float>(ctx.Time.DeltaSeconds);

    Bodies.SyncToPhysics(ctx.Entities, ctx.Partitions);
    Simulation.Step(dt, CollisionSteps);
    Bodies.SyncFromPhysics(ctx.Entities, ctx.Partitions);
}
