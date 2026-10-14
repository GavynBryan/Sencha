#include <movement/CharacterTickStep.h>

#include <ecs/World.h>
#include <movement/FreeLocomotionSystem.h>
#include <movement/JumpExecutionSystem.h>
#include <movement/JumpState.h>
#include <movement/LocomotionMode.h>
#include <movement/RootMotionSource.h>
#include <movement/MotionComposition.h>
#include <movement/components/CharacterFacts.h>
#include <movement/components/CharacterMovement.h>
#include <movement/components/MotionChannels.h>
#include <movement/components/MovementTuning.h>
#include <movement/MovementIntent.h>
#include <physics/CharacterMoverPool.h>
#include <world/ComponentSet.h>
#include <time/ActorTime.h>
#include <world/transform/TransformComponents.h>

#include <cmath>

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

namespace
{
    // One complete step -- locomotion, jump, root motion, composition, sweep -- of
    // `dt` actor seconds. `carry` scales what the ground carries the character by,
    // so a dilated character on a platform rides it at the platform's speed.
    void StepOnce(World& world, CharacterMoverPool* movers, EntityId entity, const MovementIntent& intent,
                  std::uint64_t tick, float dt, float carry, Vec3d gravity, Vec3d upAxis)
    {
        KinematicState* motion = world.TryGet<KinematicState>(entity);
        SupportState* support = world.TryGet<SupportState>(entity);
        const ResolvedMovementTuning* tuning = world.TryGet<ResolvedMovementTuning>(entity);
        if (motion == nullptr || support == nullptr || tuning == nullptr)
            return;
        SupportState carried = *support;
        carried.SurfaceVelocity = carried.SurfaceVelocity * carry;

        const LocomotionOutput locomotion =
            StepFreeLocomotion(*motion, carried, intent, *tuning, gravity, upAxis, dt);

        // The mailboxes are rebuilt per step rather than read off the entity: the
        // live tick clears them during composition, so a step that inherited them
        // would apply a jump twice.
        MotionAxisOverride overrides{};
        if (JumpState* jump = world.TryGet<JumpState>(entity))
        {
            float up = 0.0f;
            if (StepJump(carried, *tuning, intent.Jump, dt, *jump, up))
            {
                overrides.UpVelocity = up;
                overrides.HasUp = true;
            }
        }
        // Asked again rather than remembered, so a replayed tick is carried by
        // what the source says now -- after a correction, not before it.
        RootMotionSample rootMotion;
        if (SampleRootMotion(world, entity, tick, dt, rootMotion))
            ApplyRootMotion(overrides, rootMotion);

        const MotionRequest motionRequest = ComposeMotion(locomotion, &carried, overrides, MotionImpulse{});

        if (movers != nullptr)
        {
            (void)movers->Step(world, entity, motionRequest, dt, gravity);
            return;
        }

        // No physics: integrate the request the way a sweep against nothing would,
        // so a headless configuration still advances rather than standing still.
        motion->Velocity = motionRequest.Velocity;
        if (LocalTransform* pose = world.TryGet<LocalTransform>(entity))
        {
            pose->Value.Position = pose->Value.Position + motionRequest.Velocity * dt;
            if (motionRequest.TurnRadians != 0.0f)
                pose->Value.Rotation = (Quat<float>::FromAxisAngle(motionRequest.UpAxis, motionRequest.TurnRadians)
                                        * pose->Value.Rotation)
                                           .Normalized();
        }
    }
}

void StepScaledCharacterTick(World& world,
                             CharacterMoverPool* movers,
                             EntityId entity,
                             const MovementIntent& intent,
                             std::uint64_t tick,
                             float fixedDeltaSeconds,
                             std::int64_t scaleQ16,
                             Vec3d gravity,
                             Vec3d upAxis)
{
    if (!CharacterTickModeSupported(world, entity) || scaleQ16 <= 0)
        return;
    const double scale = static_cast<double>(scaleQ16) / static_cast<double>(kActorTimeOne);
    const int substeps = scale > 1.0 ? static_cast<int>(std::ceil(scale)) : 1;
    const float step = static_cast<float>(fixedDeltaSeconds * scale / substeps);
    const float carry = static_cast<float>(1.0 / scale);
    for (int i = 0; i < substeps; ++i)
        StepOnce(world, movers, entity, intent, tick, step, carry, gravity, upAxis);
}

void StepCharacterTick(World& world,
                       CharacterMoverPool* movers,
                       EntityId entity,
                       const MovementIntent& intent,
                       std::uint64_t tick,
                       float fixedDeltaSeconds,
                       Vec3d gravity,
                       Vec3d upAxis)
{
    StepScaledCharacterTick(world, movers, entity, intent, tick, fixedDeltaSeconds, kActorTimeOne, gravity, upAxis);
}
