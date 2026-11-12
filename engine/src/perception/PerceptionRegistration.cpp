#include <perception/PerceptionRegistration.h>

#include <app/EngineSchedule.h>
#include <perception/ObservationMemorySystem.h>
#include <perception/PerceptionSystem.h>
#include <physics/PhysicsStepSystem.h>

#include <cassert>

void RegisterPerception(EngineSchedule& schedule, ConsoleRegistry* console)
{
    const PhysicsStepSystem* physics = schedule.Get<PhysicsStepSystem>();
    assert(physics != nullptr && "RegisterPerception needs RegisterPhysics first");
    if (physics == nullptr)
        return;
    schedule.Register<PerceptionSystem>(physics->GetSimulation(), console);
    schedule.Register<ObservationMemorySystem>();
    schedule.After<ObservationMemorySystem, PerceptionSystem>();
}
