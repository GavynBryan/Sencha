#include <controller/ControllerRegistration.h>

#include <app/EngineSchedule.h>
#include <controller/AimFacingSystem.h>
#include <controller/LookIntegrationSystem.h>
#include <controller/LookOrientation.h>
#include <ecs/World.h>

void RegisterControllerComponents(World& world)
{
    if (!world.IsRegistered<LookOrientation>())
        world.RegisterComponent<LookOrientation>();
    if (!world.IsRegistered<LocalLookControl>())
        world.RegisterComponent<LocalLookControl>();
    if (!world.IsRegistered<AimFacing>())
        world.RegisterComponent<AimFacing>();
}

void RegisterControllerSystems(EngineSchedule& schedule)
{
    schedule.Register<LookIntegrationSystem>();
    schedule.Register<AimFacingSystem>();

    // A body faces the aim this tick integrated, not the one before it.
    schedule.After<AimFacingSystem, LookIntegrationSystem>();
}
