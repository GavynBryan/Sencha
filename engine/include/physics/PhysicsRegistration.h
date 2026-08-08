#pragma once

class ComponentRegistrar;
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
// The physics component vocabulary, including the runtime link components the
// bridges add at reconcile time. Registering a component a zone never uses is
// harmless: no archetype exists until an entity has it.
//
// The World overload registers straight into one World for a caller that built
// it by hand rather than sealing a schema for it. Call once per World, before
// any entity is created in it.
//=============================================================================
void RegisterPhysicsComponents(ComponentRegistrar& registrar);
void RegisterPhysicsComponents(World& world);
