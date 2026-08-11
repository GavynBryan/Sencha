#pragma once

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
