#include <abilities/AbilityKit.h>

#include <abilities/AbilityActivation.h>
#include <abilities/AbilityActivationSystem.h>
#include <abilities/AbilityRegistry.h>
#include <abilities/AbilitySet.h>
#include <app/EngineSchedule.h>
#include <attributes/AttributeRegistry.h>
#include <attributes/AttributeSet.h>
#include <ecs/World.h>
#include <effects/ActiveEffect.h>
#include <effects/AttributeResolveSystem.h>
#include <effects/EffectLifetimeSystem.h>
#include <effects/EffectRegistry.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <gameplay_tags/GameplayTagRegistry.h>

void RegisterAbilityKit(World& world)
{
    if (!world.IsRegistered<GameplayTagContainer>())
        world.RegisterComponent<GameplayTagContainer>();
    if (!world.IsRegistered<AttributeSet>())
        world.RegisterComponent<AttributeSet>();
    if (!world.IsRegistered<AbilitySet>())
        world.RegisterComponent<AbilitySet>();
    if (!world.IsRegistered<ActiveEffect>())
        world.RegisterComponent<ActiveEffect>();

    if (!world.HasResource<GameplayTagRegistry>())
        world.AddResource<GameplayTagRegistry>();
    if (!world.HasResource<AttributeRegistry>())
        world.AddResource<AttributeRegistry>();
    if (!world.HasResource<EffectRegistry>())
        world.AddResource<EffectRegistry>();
    if (!world.HasResource<AbilityRegistry>())
        world.AddResource<AbilityRegistry>();
    if (!world.HasResource<AbilityActivationQueue>())
        world.AddResource<AbilityActivationQueue>();
}

void RegisterAbilityKitSystems(EngineSchedule& schedule)
{
    schedule.Register<AbilityActivationSystem>();
    schedule.Register<AttributeResolveSystem>();
    schedule.Register<EffectLifetimeSystem>();

    schedule.After<AttributeResolveSystem, AbilityActivationSystem>();
    schedule.After<EffectLifetimeSystem, AttributeResolveSystem>();
}
