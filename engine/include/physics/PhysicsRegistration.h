#pragma once

class EngineSchedule;
class World;

//=============================================================================
// RegisterPhysics
//
// Opt-in physics for a game: registers PhysicsStepSystem and
// CharacterControllerSystem in the Physics step, ordered so characters move
// against the stepped world. A game calls this once from OnRegisterSystems.
// Physics still only runs on zones that opt into Physics participation.
//=============================================================================
void RegisterPhysics(EngineSchedule& schedule);

//=============================================================================
// RegisterPhysicsComponents
//
// Register the physics component set on a zone's World before any entity is
// created in it. This includes the runtime link components (PhysicsBodyLink,
// CharacterMoverLink) the bridges add at reconcile time, so callers cannot
// forget them and the reconcile's AddComponent never asserts. Registering a
// component a zone never uses is harmless (no archetype exists until an entity
// has it). Call once per World.
//=============================================================================
void RegisterPhysicsComponents(World& world);
