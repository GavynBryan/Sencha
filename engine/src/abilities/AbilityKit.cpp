#include <abilities/AbilityKit.h>

#include <abilities/AbilityActivation.h>
#include <abilities/AbilityActivationSystem.h>
#include <abilities/AbilityRegistry.h>
#include <abilities/AbilitySet.h>
#include <app/EngineSchedule.h>
#include <attributes/AttributeRegistry.h>
#include <attributes/AttributeSet.h>
#include <core/ResourceStore.h>
#include <ecs/EntityStore.h>
#include <effects/ActiveEffect.h>
#include <effects/AttributeResolveSystem.h>
#include <effects/EffectLifetimeSystem.h>
#include <effects/EffectRegistry.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <gameplay_tags/GameplayTagRegistry.h>

void RegisterAbilityComponents(EntityStore& world)
{
    if (!world.IsRegistered<GameplayTagContainer>())
        world.RegisterComponent<GameplayTagContainer>();
    if (!world.IsRegistered<AttributeSet>())
        world.RegisterComponent<AttributeSet>();
    if (!world.IsRegistered<AbilitySet>())
        world.RegisterComponent<AbilitySet>();
    if (!world.IsRegistered<ActiveEffect>())
        world.RegisterComponent<ActiveEffect>();
}

void RegisterAbilityDefinitions(ResourceStore& sessionResources)
{
    sessionResources.Ensure<GameplayTagRegistry>();
    sessionResources.Ensure<AttributeRegistry>();
    sessionResources.Ensure<EffectRegistry>();
    sessionResources.Ensure<AbilityRegistry>();
}

void RegisterAbilityRuntime(ResourceStore& registryResources)
{
    registryResources.Ensure<AbilityActivationQueue>();
}

void RegisterAbilityKitSystems(EngineSchedule& schedule)
{
    schedule.Register<AbilityActivationSystem>();
    schedule.Register<AttributeResolveSystem>();
    schedule.Register<EffectLifetimeSystem>();

    schedule.After<AttributeResolveSystem, AbilityActivationSystem>();
    schedule.After<EffectLifetimeSystem, AttributeResolveSystem>();
}
