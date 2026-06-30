#pragma once

class World;
struct GameplayTagContainer;
struct MovementIntent;
struct MovementProfile;
struct MovementState;
struct MovementTags;

//=============================================================================
// Movement operations
//
// The reusable, backend-free core of the character controller: free functions on
// framework data, the same shape as EffectSystem/AbilitySystem. Gameplay stays
// decoupled from the render/physics backends (the framework isolation rule), so
// these never touch a CharacterController or a camera. A consumer feeds contact
// state into MovementState.Grounded, calls TickMovement, and reads back the
// resolved planar velocity and jump request (the thin in/out seam, D-G).
//=============================================================================

// Advance one character. Integrates PlanarVelocity toward the intent (reduced
// authority in air), applies ground friction when idle, updates coyote grace, and
// turns a queued jump into JumpRequest when grounded or within grace. Reads
// state.Grounded; writes PlanarVelocity, CoyoteTimer, JumpRequest; clears
// intent.JumpQueued.
void StepMovement(MovementState& state,
                  MovementIntent& intent,
                  const MovementProfile& profile,
                  float dt);

// Grant/revoke the grounded/airborne branch and the grounded substate
// (idle/walking/sprinting) so locomotion state is queryable as hierarchical tags.
// Sole owner of these tags, so it holds each at a stack count of zero or one.
void ResolveMovementTags(GameplayTagContainer& tags,
                         const MovementTags& ids,
                         bool grounded,
                         const MovementIntent& intent);

// Advance every character in the world: StepMovement plus ResolveMovementTags per
// entity carrying a MovementState (with MovementIntent and MovementProfile).
// Resolves the MovementTags resource once. Grounded must already be fed in.
void TickMovement(World& world, float dt);

// Register the movement components and the movement.* tag hierarchy (storing the
// resolved MovementTags resource) on a zone World. Call before entities are
// created in that World.
void InitializeMovementRegistry(World& world);
