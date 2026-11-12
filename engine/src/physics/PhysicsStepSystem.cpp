#include <physics/PhysicsStepSystem.h>

#include <app/GameContexts.h>
#include <world/RuntimeWorld.h>
#include <world/transform/TransformPropagation.h>

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

    if (ctx.Entities.IsRegistered<WorldTransform>())
        PropagateTransforms(ctx.Entities, ctx.Partitions, TransformPropagationDomain::Simulation,
                            ctx.Config.Runtime.TransformForceFullPropagation);
    Bodies.SyncToPhysics(ctx.Entities, ctx.Partitions);
    Simulation.Step(dt, CollisionSteps);
    ReportCapacityFailures();
    Bodies.SyncFromPhysics(ctx.Entities, ctx.Partitions);
}

void PhysicsStepSystem::ReportCapacityFailures()
{
    const PhysicsCapacityFailures& now = Simulation.GetCapacityFailures();
    const bool failed = now.BodiesRefused != ReportedFailures.BodiesRefused
        || now.OverflowedSteps != ReportedFailures.OverflowedSteps;
    ReportedFailures = now;
    if (failed && CapacityLog.Record() && Log != nullptr)
    {
        Log->Error("Physics capacity exceeded: {} bodies refused and {} steps dropped contacts so far",
                   now.BodiesRefused, now.OverflowedSteps);
    }
    if (const auto ended = CapacityLog.EndPeriod(); ended.has_value() && Log != nullptr)
        Log->Warn("Physics is within capacity again after {} ticks over it", ended->Periods);
}
