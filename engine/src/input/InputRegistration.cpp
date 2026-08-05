#include <input/InputRegistration.h>

#include <app/EngineSchedule.h>
#include <ecs/World.h>
#include <input/InputActionResolveSystem.h>
#include <input/InputActionState.h>
#include <input/InputContextSet.h>

void RegisterInputMapping(World& world, DataAssetCache& dataAssets, InputProfileHandle profile)
{
    InputProfileBinding& binding = world.HasResource<InputProfileBinding>()
        ? world.GetResource<InputProfileBinding>()
        : world.AddResource<InputProfileBinding>();
    binding.Profile = profile;

    if (!world.HasResource<InputBindingCache>())
        (void)world.AddResource<InputBindingCache>(dataAssets);
    if (!world.HasResource<InputActionState>())
        (void)world.AddResource<InputActionState>();
    if (!world.HasResource<InputContextSet>())
        (void)world.AddResource<InputContextSet>();
}

void RegisterInputSystems(EngineSchedule& schedule,
                          DataAssetCache& dataAssets,
                          LoggingProvider& logging)
{
    schedule.Register<InputActionResolveSystem>(dataAssets, logging);
}
