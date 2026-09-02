#include <movement/CharacterTickStep.h>

#include <ecs/World.h>
#include <movement/FreeLocomotionSystem.h>
#include <movement/JumpExecutionSystem.h>
#include <movement/JumpState.h>
#include <movement/LocomotionMode.h>
#include <movement/MotionComposition.h>
#include <movement/components/CharacterFacts.h>
#include <movement/components/CharacterMovement.h>
#include <movement/components/MotionChannels.h>
#include <movement/components/MovementTuning.h>
#include <movement/MovementIntent.h>
#include <physics/CharacterMoverPool.h>
#include <world/ComponentSet.h>
#include <world/transform/TransformComponents.h>

bool CharacterTickSubject(const World& world, EntityId entity)
{
    // Read-only for the same reason as below: asking what something is must not
    // mark a chunk's column changed.
    return world.TryGet<CharacterMovement>(entity) != nullptr;
}

bool CharacterTickModeSupported(const World& world, EntityId entity)
{
    // Read-only throughout: asking which rules a character moves under is not a
    // write, and mutable access would mark the whole chunk's column changed.
    const CharacterMovement* movement = world.TryGet<CharacterMovement>(entity);
    const LocomotionModeRegistry* modes =
        world.TryGetResource<LocomotionModeRegistry>();
    return movement != nullptr && modes != nullptr
        && movement->Mode == modes->FreeMode();
}

// The first four are advanced by the step below: locomotion integrates
// velocity, the sweep writes the pose and what the character came to rest
// against, and the jump gate carries its own state between ticks.
//
// The last two are read rather than advanced, and still resumed: the step
// derives everything above from them, and neither is a function of its own
// previous value. The mode is chosen by an arbiter from other state and the
// tuning is re-resolved from the profile every tick, so a restored copy is
// corrected by the next live tick rather than left behind by the replayed ones.
using CharacterTickResumedComponents = ComponentSet<
    KinematicState, SupportState, LocalTransform, JumpState,
    CharacterMovement, ResolvedMovementTuning>;

bool CharacterTickResumes(ComponentTypeId type)
{
    return CharacterTickResumedComponents::Contains(type);
}

void StepCharacterTick(World& world,
                       CharacterMoverPool* movers,
                       EntityId entity,
                       const MovementIntent& intent,
                       float fixedDeltaSeconds,
                       Vec3d gravity,
                       Vec3d upAxis)
{
    if (!CharacterTickModeSupported(world, entity))
        return;

    KinematicState* motion = world.TryGet<KinematicState>(entity);
    SupportState* support = world.TryGet<SupportState>(entity);
    const ResolvedMovementTuning* tuning =
        world.TryGet<ResolvedMovementTuning>(entity);
    if (motion == nullptr || support == nullptr || tuning == nullptr)
        return;

    const LocomotionOutput locomotion = StepFreeLocomotion(
        *motion, *support, intent, *tuning, gravity, upAxis, fixedDeltaSeconds);

    // The mailboxes are rebuilt per step rather than read off the entity: the
    // live tick clears them during composition, so a step that inherited them
    // would apply a jump twice.
    MotionAxisOverride overrides{};
    if (JumpState* jump = world.TryGet<JumpState>(entity))
    {
        float up = 0.0f;
        if (StepJump(*support, *tuning, intent.Jump, fixedDeltaSeconds, *jump, up))
        {
            overrides.UpVelocity = up;
            overrides.HasUp = true;
        }
    }

    const MotionRequest motionRequest =
        ComposeMotion(locomotion, support, overrides, MotionImpulse{});

    if (movers != nullptr)
    {
        (void)movers->Step(world, entity, motionRequest, fixedDeltaSeconds, gravity);
        return;
    }

    // No physics: integrate the request the way a sweep against nothing would,
    // so a headless configuration still advances rather than standing still.
    motion->Velocity = motionRequest.Velocity;
    if (LocalTransform* pose = world.TryGet<LocalTransform>(entity))
    {
        pose->Value.Position =
            pose->Value.Position + motionRequest.Velocity * fixedDeltaSeconds;
    }
}
