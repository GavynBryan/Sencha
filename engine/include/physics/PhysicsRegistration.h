#pragma once

#include <physics/components/CharacterController.h>
#include <physics/components/CharacterMoverLink.h>
#include <physics/components/Collider.h>
#include <physics/components/HitRegion.h>
#include <physics/components/PhysicsBodyLink.h>
#include <physics/components/RigidBody.h>
#include <world/ComponentSet.h>

class CharacterMoverPool;
class ComponentRegistrar;
class EngineSchedule;
class PhysicsWorld;
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
using PhysicsComponents = ComponentSet<
    Collider,
    RigidBody,
    CharacterController,
    HitRegion,
    // The runtime links the bridges add at reconcile time. Named here so a
    // caller cannot forget them and the reconcile's AddComponent never asserts.
    PhysicsBodyLink,
    CharacterMoverLink>;

void RegisterPhysicsComponents(ComponentRegistrar& registrar);
void RegisterPhysicsComponents(World& world);

// A body's velocities turn with a relocation and the sensor pairs of what moved
// are forgotten (docs/gameplay/relocation.md).
void RegisterPhysicsRelocation(World& world);

// The live body and character mover follow the relocated transform. Both must
// outlive every relocation in `world`.
void InstallPhysicsBackendRelocation(World& world, PhysicsWorld& simulation, CharacterMoverPool& movers);
