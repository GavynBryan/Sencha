#include <abilities/AbilityKit.h>

#include <abilities/AbilityPredictionJournal.h>

#include <abilities/AbilityGrantsSerializer.h>
#include <abilities/AbilityPresentationSystem.h>
#include <abilities/CuePresentationLibrary.h>
#include <anim/AnimSelectSystem.h>
#include <app/EngineSchedule.h>
#include <attributes/AttributeSetSerializer.h>
#include <ecs/World.h>
#include <effects/AttributeResolveSystem.h>
#include <effects/EffectLifetimeSystem.h>
#include <gameplay_tags/GameplayTagContainerSerializer.h>
#include <input/InputActionResolveSystem.h>
#include <time/ActorTimeSystem.h>
#include <time/TimeComponentRegistration.h>
#include <world/ComponentRegistrar.h>

void RegisterAbilityKitComponents(ComponentRegistrar& registrar)
{
    registrar.AddAll<AbilityKitComponents>();

    // Tags, attributes and grants hold registration-order ids, so their persisted
    // form is names and cannot come from a schema. The serializers go in beside the
    // storage: one edit here reaches the runtime, the editor's preview registry,
    // and the cook, because all three compose from this registrar.
    registrar.AddSerializer(MakeGameplayTagContainerSerializer());
    registrar.AddSerializer(MakeAttributeSetSerializer());
    registrar.AddSerializer(MakeAbilityGrantsSerializer());
}

void InstallAbilityKitVocabulary(World& world)
{
    if (!world.HasResource<GameplayTagRegistry>())
        world.AddResource<GameplayTagRegistry>();
    if (!world.HasResource<AttributeRegistry>())
        world.AddResource<AttributeRegistry>();
    if (!world.HasResource<EffectRegistry>())
        world.AddResource<EffectRegistry>();
    if (!world.HasResource<AbilityRegistry>())
        world.AddResource<AbilityRegistry>();
    if (!world.HasResource<AbilityGrantEntries>())
        world.AddResource<AbilityGrantEntries>();
    if (!world.HasResource<CuePresentationLibrary>())
        world.AddResource<CuePresentationLibrary>();
}

void RegisterAbilityKit(World& world)
{
    ComponentRegistrar registrar(world);
    // An actor's clock is time's, not the kit's, but every actor the kit runs owns one.
    RegisterTimeComponents(registrar);
    RegisterAbilityKitComponents(registrar);

    InstallAbilityKitVocabulary(world);
    if (!world.HasResource<AbilityRequestQueue>())
        world.AddResource<AbilityRequestQueue>();
    if (!world.HasResource<AbilityTrace>())
        world.AddResource<AbilityTrace>();
    if (!world.HasResource<AbilityPredictionJournal>())
        world.AddResource<AbilityPredictionJournal>();
}

void RegisterAbilityKitSystems(EngineSchedule& schedule, const AbilityHost& host)
{
    schedule.Register<ActorTimeSystem>(host.Console);
    schedule.Register<AbilitySystem>(host);
    schedule.Register<AttributeResolveSystem>();
    schedule.Register<EffectLifetimeSystem>();

    schedule.After<AbilitySystem, ActorTimeSystem>();
    if (host.Presents)
    {
        schedule.Register<AbilityPresentationSystem>(host.Verbs);
        schedule.After<AbilityPresentationSystem, AbilitySystem>();
    }
    schedule.After<AttributeResolveSystem, AbilitySystem>();
    schedule.After<EffectLifetimeSystem, AttributeResolveSystem>();
    // An ability reads the tick's input edges.
    if (schedule.Get<InputActionResolveSystem>() != nullptr)
        schedule.After<AbilitySystem, InputActionResolveSystem>();
    // An ability's animation request is seen by the same tick's selection. Declared
    // by whichever of the two registers second.
    if (schedule.Get<AnimSelectSystem>() != nullptr)
        schedule.After<AnimSelectSystem, AbilitySystem>();
}
