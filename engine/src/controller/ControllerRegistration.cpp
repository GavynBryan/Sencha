#include <controller/ControllerRegistration.h>

#include <app/EngineSchedule.h>
#include <controller/LookIntegrationSystem.h>
#include <controller/LookOrientation.h>
#include <ecs/World.h>

void RegisterControllerComponents(World& world)
{
    if (!world.IsRegistered<LookOrientation>())
        world.RegisterComponent<LookOrientation>();
    if (!world.IsRegistered<LocalLookControl>())
        world.RegisterComponent<LocalLookControl>();
}

void RegisterControllerSystems(EngineSchedule& schedule)
{
    schedule.Register<LookIntegrationSystem>();
}
