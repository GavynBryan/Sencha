#pragma once

class World;

//=============================================================================
// Locomotion operations
//
// An ECS-native, extensible locomotion state machine plus the jump bridge, as
// free functions on framework data (the same shape as EffectSystem /
// AbilitySystem). Mode is archetype membership (OnGround / InAir markers), so a
// new mode is a new operation, not a new branch. The framework reads the physics
// CharacterController POD directly (contact in, desired velocity + jump impulse
// out); it never touches the physics simulation.
//
// A composition-layer runner calls these each fixed tick, in order:
//   ProcessAbilityActivations  (AbilityKit: turn queued jumps into effects/tags)
//   TickJumpExecution          (consume the request tag -> PendingJumpSpeed)
//   TickLocomotionTransitions  (contact -> mode marker + tag projection)
//   ResolveAttributesWithEffects (AbilityKit: MoveSpeed.Current)
//   TickGroundLocomotion / TickAirLocomotion (per-mode planar velocity)
//   TickEffects                (AbilityKit: advance cooldowns)
//=============================================================================

// Single writer of locomotion mode. Reads CharacterController.Grounded, advances
// coyote grace, and on a mode edge swaps the OnGround/InAir marker (through a
// CommandBuffer) and updates the movement.grounded{.idle,.walking}/airborne tag
// projection other systems (abilities, animation) query. Settled characters cost
// nothing: no marker churn, tags touched only on a change.
void TickLocomotionTransitions(World& world, float dt);

// Ground locomotion: for each OnGround character, accelerate PlanarVelocity toward
// WishDir * MoveSpeed (the attribute Current) with ground acceleration, decaying
// to rest with friction when there is no input, and write the result to
// CharacterController.DesiredVelocity.
void TickGroundLocomotion(World& world, float dt);

// Air locomotion: for each InAir character, the same steer at reduced authority
// (AirControl) and no friction; writes CharacterController.DesiredVelocity. The
// mover owns the vertical component.
void TickAirLocomotion(World& world, float dt);

// Turn an activated jump into a physics impulse: for each character holding the
// movement.jump.requested tag, set CharacterController.PendingJumpSpeed from the
// profile and revoke the tag (single fire). Gating (grounded, cooldown, cost)
// already ran in the ability activation that granted the tag.
void TickJumpExecution(World& world);

// Register the movement components and markers, the movement.* tag hierarchy, the
// MoveSpeed attribute, and the Jump ability, plus the AbilityKit resources they
// depend on (tag/attribute/effect/ability registries and the activation queue).
// Idempotent. Call before entities are created in that World.
void InitializeMovementRegistry(World& world);
