#include <physics/PhysicsRegistration.h>

#include <app/EngineSchedule.h>
#include <physics/CharacterControllerSystem.h>
#include <physics/PhysicsStepSystem.h>

void RegisterPhysics(EngineSchedule& schedule)
{
    PhysicsStepSystem& step = schedule.Register<PhysicsStepSystem>();
    schedule.Register<CharacterControllerSystem>(step);
    schedule.After<CharacterControllerSystem, PhysicsStepSystem>();
}
