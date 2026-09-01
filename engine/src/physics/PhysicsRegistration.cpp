#include <physics/PhysicsRegistration.h>
#include <physics/PhysicsComponentSchemas.h>

#include <app/EngineSchedule.h>
#include <ecs/World.h>
#include <physics/CharacterControllerSystem.h>
#include <physics/PhysicsStepSystem.h>
#include <physics/components/CharacterController.h>
#include <physics/components/CharacterMoverLink.h>
#include <physics/components/Collider.h>
#include <physics/components/PhysicsBodyLink.h>
#include <physics/components/RigidBody.h>
#include <world/ComponentRegistrar.h>

void RegisterPhysics(EngineSchedule& schedule)
{
    PhysicsStepSystem& step = schedule.Register<PhysicsStepSystem>();
    schedule.Register<CharacterControllerSystem>(step);
    schedule.After<CharacterControllerSystem, PhysicsStepSystem>();
}

void RegisterPhysicsComponents(ComponentRegistrar& registrar)
{
    registrar.Add<Collider>();
    registrar.Add<RigidBody>();
    registrar.Add<CharacterController>();
    // The runtime links the bridges add at reconcile time. Named here so a
    // caller cannot forget them and the reconcile's AddComponent never asserts.
    registrar.Add<PhysicsBodyLink>();
    registrar.Add<CharacterMoverLink>();
}

void RegisterPhysicsComponents(World& world)
{
    ComponentRegistrar registrar(world);
    RegisterPhysicsComponents(registrar);
}
