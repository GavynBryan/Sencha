#include <physics/PhysicsRegistration.h>

#include <app/EngineSchedule.h>
#include <ecs/World.h>
#include <physics/CharacterControllerSystem.h>
#include <physics/PhysicsStepSystem.h>
#include <world/ComponentRegistrar.h>

void RegisterPhysics(EngineSchedule& schedule)
{
    PhysicsStepSystem& step = schedule.Register<PhysicsStepSystem>();
    schedule.Register<CharacterControllerSystem>(step);
    schedule.After<CharacterControllerSystem, PhysicsStepSystem>();
}

void RegisterPhysicsComponents(ComponentRegistrar& registrar)
{
    registrar.AddAll<PhysicsComponents>();
}

void RegisterPhysicsComponents(World& world)
{
    ComponentRegistrar registrar(world);
    RegisterPhysicsComponents(registrar);
}
