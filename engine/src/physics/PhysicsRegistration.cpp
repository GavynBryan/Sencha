#include <physics/PhysicsRegistration.h>

#include <app/EngineSchedule.h>
#include <ecs/World.h>
#include <physics/CharacterControllerSystem.h>
#include <physics/PhysicsStepSystem.h>
#include <physics/components/CharacterController.h>
#include <physics/components/CharacterMoverLink.h>
#include <physics/components/Collider.h>
#include <physics/components/PhysicsBodyLink.h>
#include <physics/components/RigidBody.h>

void RegisterPhysics(EngineSchedule& schedule)
{
    PhysicsStepSystem& step = schedule.Register<PhysicsStepSystem>();
    schedule.Register<CharacterControllerSystem>(step);
    schedule.After<CharacterControllerSystem, PhysicsStepSystem>();
}

void RegisterPhysicsComponents(World& world)
{
    world.RegisterComponent<Collider>();
    world.RegisterComponent<RigidBody>();
    world.RegisterComponent<CharacterController>();
    world.RegisterComponent<PhysicsBodyLink>();
    world.RegisterComponent<CharacterMoverLink>();
}
