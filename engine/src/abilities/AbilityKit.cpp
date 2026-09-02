#include <abilities/AbilityKit.h>

#include <abilities/AbilityActivation.h>
#include <abilities/AbilityActivationSystem.h>
#include <abilities/AbilityRegistry.h>
#include <abilities/AbilitySet.h>
#include <abilities/AbilitySetSerializer.h>
#include <app/EngineSchedule.h>
#include <attributes/AttributeRegistry.h>
#include <attributes/AttributeSet.h>
#include <attributes/AttributeSetSerializer.h>
#include <ecs/World.h>
#include <effects/ActiveEffect.h>
#include <effects/AttributeResolveSystem.h>
#include <effects/EffectLifetimeSystem.h>
#include <effects/EffectRegistry.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <gameplay_tags/GameplayTagContainerSerializer.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <world/ComponentRegistrar.h>

void RegisterAbilityKitComponents(ComponentRegistrar& registrar)
{
    registrar.AddAll<AbilityKitComponents>();

    // Tags and attributes hold registration-order ids, so their persisted form
    // is names and cannot come from a schema. The serializers go in beside the
    // storage: one edit here reaches the runtime, the editor's preview
    // registry, and the cook, because all three compose from this registrar.
    registrar.AddSerializer(MakeGameplayTagContainerSerializer());
    registrar.AddSerializer(MakeAttributeSetSerializer());
    registrar.AddSerializer(MakeAbilitySetSerializer());
}

void RegisterAbilityKit(World& world)
{
    ComponentRegistrar registrar(world);
    RegisterAbilityKitComponents(registrar);

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
