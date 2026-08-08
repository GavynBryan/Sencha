#include <net/PawnStateReplay.h>

#include <ecs/World.h>
#include <ecs/WorldComponentSchema.h>
#include <movement/FreeLocomotionSystem.h>
#include <movement/JumpExecutionSystem.h>
#include <movement/JumpState.h>
#include <movement/LocomotionMode.h>
#include <movement/MotionComposition.h>
#include <movement/MovementComponents.h>
#include <net/ClientPrediction.h>
#include <physics/CharacterMoverPool.h>
#include <world/transform/TransformComponents.h>
#include <world/transform/TransformHistory.h>

namespace
{
    // One replayed tick, composed from the same kernels the live tick runs. The
    // components are read and written in place, so each step starts from what
    // the one before it produced -- exactly as the scheduled systems do.
    void StepOnce(World& world, CharacterMoverPool* movers, EntityId pawn,
                  const PawnCommandTick& command, const PawnReplayRequest& request)
    {
        KinematicState* motion = world.TryGet<KinematicState>(pawn);
        SupportState* support = world.TryGet<SupportState>(pawn);
        const ResolvedMovementTuning* tuning =
            world.TryGet<ResolvedMovementTuning>(pawn);
        if (motion == nullptr || support == nullptr || tuning == nullptr)
            return;

        const LocomotionOutput locomotion = StepFreeLocomotion(
            *motion, *support, command.Intent, *tuning, request.Gravity,
            request.UpAxis, request.FixedDeltaSeconds);

        // The mailboxes are rebuilt per step rather than read off the entity:
        // the live tick clears them during composition, so a replayed tick that
        // inherited them would apply a jump twice.
        MotionAxisOverride overrides{};
        if (JumpState* jump = world.TryGet<JumpState>(pawn))
        {
            float up = 0.0f;
            if (StepJump(*support, *tuning, command.Intent.Jump,
                         request.FixedDeltaSeconds, *jump, up))
            {
                overrides.UpVelocity = up;
                overrides.HasUp = true;
            }
        }

        const MotionRequest motionRequest =
            ComposeMotion(locomotion, support, overrides, MotionImpulse{});

        if (movers != nullptr)
        {
            (void)movers->Step(world, pawn, motionRequest,
                               request.FixedDeltaSeconds, request.Gravity);
            return;
        }

        // No physics: integrate the request the way a sweep against nothing
        // would, so a headless configuration still advances rather than
        // standing still.
        motion->Velocity = motionRequest.Velocity;
        if (LocalTransform* pose = world.TryGet<LocalTransform>(pawn))
        {
            pose->Value.Position =
                pose->Value.Position + motionRequest.Velocity * request.FixedDeltaSeconds;
        }
    }
}

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
        const CharacterMovement* movement = world.TryGet<CharacterMovement>(pawn);
        const LocomotionModeRegistry* modes =
            world.HasResource<LocomotionModeRegistry>()
                ? &world.GetResource<LocomotionModeRegistry>()
                : nullptr;
        const bool freeMode = movement != nullptr && modes != nullptr
                           && movement->Mode == modes->FreeMode();

        if (!freeMode)
        {
            const LocalTransform* pose = world.TryGet<LocalTransform>(pawn);
            snapTo(pose == nullptr ? start : pose->Value.Position);
        }
        else
        {
            std::uint32_t replayed = 0;
            const bool complete = prediction.Commands().ForEachAfter(
                request.AckTick, [&](const PawnCommandTick& command) {
                    StepOnce(world, request.Movers, pawn, command, request);
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
