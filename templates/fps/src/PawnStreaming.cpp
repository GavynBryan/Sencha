#include "PawnStreaming.h"

#include <app/Engine.h>
#include <app/GameContexts.h>
#include <ecs/World.h>
#include <participant/LocalControl.h>
#include <physics/CharacterMoverPool.h>
#include <physics/components/CharacterController.h>
#include <world/transform/TransformComponents.h>
#include <world/transform/TransformHistory.h>
#include <zone/DockCrossing.h>
#include <zone/WorldPartitionRuntime.h>

void FocusStreamingOnLocalPawn(const World& world, WorldPartitionRuntime& partition)
{
    if (!partition.HasManifest())
        return;
    const EntityId pawn = LocalControlSubjectOf(world);
    if (!pawn.IsValid())
        return;

    // SetFocus, not RelocateFocus: successive positions are swept through the
    // docks between them, which is how a crossing is noticed. Relocation is
    // the teleport path and would step over one.
    if (const WorldTransform* transform = world.TryGet<WorldTransform>(pawn))
        partition.SetFocus(transform->Value.Position);
    if (world.IsRegistered<CharacterController>())
    {
        if (const CharacterController* shape = world.TryGet<CharacterController>(pawn))
            partition.SetFocusCapsule(shape->Radius, shape->Height);
    }
}

void PawnStreaming::ZoneResidency(ZoneResidencyContext& ctx)
{
    if (WorldPartitionRuntime* partition =
            Owner != nullptr ? Owner->WorldStreaming() : nullptr)
    {
        FocusStreamingOnLocalPawn(ctx.Entities, *partition);
    }
}

void PawnStreaming::FrameUpdate(FrameUpdateContext& ctx)
{
    const WorldPartitionRuntime* partition =
        Owner != nullptr ? Owner->WorldStreaming() : nullptr;
    if (partition == nullptr || !partition->HasManifest())
        return;

    // A crossing the destination is not ready for leaves the pawn where the
    // sweep last had it fully inside the room it is leaving; streaming decides
    // that on the wall clock, but moving a pawn is simulation, so the position
    // is recorded and applied on the next fixed tick.
    if (LocalControlSubjectOf(ctx.Entities).IsValid()
        && partition->LastTraversal().Status
            == DockTraversalStatus::BlockedDestinationNotReady)
    {
        PendingSafePosition = partition->LastTraversal().SafeSourcePosition;
    }
}

// Applied at the head of the tick, before movement runs, so the pawn never
// enters physics at the position that reached into the unloaded zone.
void PawnStreaming::FixedLogic(FixedLogicContext& ctx)
{
    if (!PendingSafePosition.has_value())
        return;

    World& world = ctx.Entities;
    const EntityId pawn = LocalControlSubjectOf(world);
    if (!pawn.IsValid())
    {
        PendingSafePosition.reset();
        return;
    }

    const Vec3d safe = *PendingSafePosition;
    PendingSafePosition.reset();

    // Through the mover, never onto the transform alone: a character's
    // position lives inside its mover and the transform is where the last
    // sweep left a copy, so writing the copy is undone by the next tick.
    bool moved = false;
    if (Movers != nullptr)
        moved = Movers->SetPosition(world, pawn, safe);
    if (!moved)
    {
        if (LocalTransform* transform = world.TryGet<LocalTransform>(pawn))
            transform->Value.Position = safe;
        RequestTransformHistorySnap(world, pawn);
    }
    if (WorldTransform* transform = world.TryGet<WorldTransform>(pawn))
        transform->Value.Position = safe;
}
