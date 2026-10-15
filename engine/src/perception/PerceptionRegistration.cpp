#include <perception/PerceptionRegistration.h>

#include <app/EngineSchedule.h>
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
}
