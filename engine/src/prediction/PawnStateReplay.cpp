#include <prediction/PawnStateReplay.h>

#include <ecs/World.h>
#include <ecs/WorldComponentSchema.h>
#include <movement/CharacterTickStep.h>
#include <net/ClientPrediction.h>
#include <physics/CharacterMoverPool.h>
#include <world/transform/TransformComponents.h>
#include <world/transform/TransformHistory.h>

PawnReplayResult ReplayPawnState(const PawnReplayRequest& request)
{
    PawnReplayResult result;
    if (request.Entities == nullptr || request.Schema == nullptr
        || request.Prediction == nullptr)
    {
        return result;
    }

    World& world = *request.Entities;
    ClientPrediction& prediction = *request.Prediction;
    const EntityId pawn = prediction.Predicted();
    if (!pawn.IsValid() || !world.IsAlive(pawn))
        return result;

    const LocalTransform* before = world.TryGet<LocalTransform>(pawn);
    const Vec3d start = before == nullptr ? Vec3d::Zero() : before->Value.Position;

    // Everything the authority has said about this pawn, back onto the pawn.
    // Not the position alone: the velocity it was carrying and what it was
    // standing on are what the ticks after this one are derived from.
    if (!prediction.RestoreTo(world, *request.Schema, pawn))
        return result;
    result.Ran = true;

    if (request.Movers != nullptr)
    {
        // Through the mover, or the restore is a copy the next sweep discards.
        const LocalTransform* restored = world.TryGet<LocalTransform>(pawn);
        if (restored != nullptr)
        {
            (void)request.Movers->RestorePosition(world, pawn,
                                                  restored->Value.Position);
        }
    }

    // Ticks at or below the acknowledgement are accounted for by the state just
    // restored, so re-running them would apply the same input twice.
    prediction.Commands().DropThrough(request.AckTick);

    const auto snapTo = [&](const Vec3d& position) {
        result.Snapped = true;
        prediction.Commands().Clear();
        if (request.Movers != nullptr)
            (void)request.Movers->SetPosition(world, pawn, position);
        else
            RequestTransformHistorySnap(world, pawn);
    };

    if (request.Replay)
    {
        // Rules this machine does not implement. Replaying under the wrong ones
        // would be a quiet disagreement; moving the pawn is a visible one.
        // Asked before the loop rather than inside it, because a pawn under
        // rules this cannot re-run has to be snapped even when there is no
        // unanswered tick to run.
        if (!CharacterTickModeSupported(world, pawn))
        {
            const LocalTransform* pose = world.TryGet<LocalTransform>(pawn);
            snapTo(pose == nullptr ? start : pose->Value.Position);
        }
        else
        {
            std::uint32_t replayed = 0;
            const bool complete = prediction.Commands().ForEachAfter(
                request.AckTick, [&](const PawnCommandTick& command) {
                    StepCharacterTick(world, request.Movers, pawn, command.Intent,
                                      request.FixedDeltaSeconds, request.Gravity,
                                      request.UpAxis);
                    ++replayed;
                });

            if (!complete)
            {
                // A stall longer than the window this machine keeps. Some tick
                // the authority has not answered has already been forgotten, so
                // replaying what is left would skip input it is about to apply.
                const LocalTransform* pose = world.TryGet<LocalTransform>(pawn);
                snapTo(pose == nullptr ? start : pose->Value.Position);
            }
            else
            {
                result.TicksReplayed = replayed;
            }
        }
    }

    if (const LocalTransform* after = world.TryGet<LocalTransform>(pawn))
        result.ResetMeters = (after->Value.Position - start).Magnitude();
    prediction.NoteReconcile(result.TicksReplayed, result.ResetMeters,
                             result.Snapped);
    return result;
}
