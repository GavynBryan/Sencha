#pragma once

#include <ecs/ComponentTypeId.h>
#include <ecs/EntityId.h>
#include <math/Vec.h>

class CharacterMoverPool;
class World;
struct MovementIntent;

//=============================================================================
// CharacterTickStep
//
// One character, one fixed movement tick, run outside the schedule.
//
// The scheduled path steps every character through a system per stage. A caller
// that has to advance one character several times before the next frame -- a
// client re-running the ticks an authority has not answered yet -- cannot use
// it: the systems visit whole worlds, and running them repeatedly would step
// everything else along with the one entity that needs it.
//
// So the stages are available here as a single call over one entity, composed
// in the order the schedule composes them. The point is that this is the same
// movement rather than a second implementation of it: what a replayed tick does
// is what the live tick did, because it is the same kernels in the same order.
//=============================================================================

// Whether a character tick applies to this entity at all, which is to say
// whether it is a character. Something driven that does not walk -- a fixed gun
// with an aim and a trigger and no way to move -- is not one.
//
// Distinct from the mode question below, and the distinction decides what a
// caller does about it. A character under rules this machine cannot re-run has
// diverged and has to be put back where the authority says. A thing that was
// never going to be moved by a character tick has not diverged at all.
[[nodiscard]] bool CharacterTickSubject(const World& world, EntityId entity);

// Whether this entity's current locomotion mode is one StepCharacterTick
// implements. A caller that cannot step an unsupported mode has to be able to
// ask without stepping, because the answer decides what to do even when there
// is no tick to run. An entity with no CharacterMovement, or a world with no
// LocomotionModeRegistry, is not supported.
[[nodiscard]] bool CharacterTickModeSupported(const World& world, EntityId entity);

// Advances one character by one fixed tick under the given intent. Components
// are read and written in place, so consecutive calls compose exactly as
// consecutive scheduled ticks do.
//
// Does nothing for a mode this does not implement, or for a character missing
// the state a step is derived from. Callers that must react to either ask
// CharacterTickModeSupported first.
void StepCharacterTick(World& world,
                       CharacterMoverPool* movers,
                       EntityId entity,
                       const MovementIntent& intent,
                       float fixedDeltaSeconds,
                       Vec3d gravity,
                       Vec3d upAxis);

// Whether re-running a tick puts this component back in step: either the tick
// advances it forward, or it is the state the tick derives those from and
// nothing between ticks moves it on its own.
//
// The question belongs to a caller that re-runs ticks to recover from an
// authority's correction. Such a caller restores a set of components and then
// steps; for a component outside this set the restore happens and the step does
// not, so whatever the caller had advanced since the authority's tick is
// discarded and never made up. That is invisible from the component's own
// behaviour, which is why it is worth being able to ask.
//
// Answered here because the body of StepCharacterTick is the answer -- a stage
// added to the tick is a component added to this list, in the same edit.
[[nodiscard]] bool CharacterTickResumes(ComponentTypeId type);
