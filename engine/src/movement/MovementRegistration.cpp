#include <movement/MovementRegistration.h>

#include <abilities/AbilityDefinition.h>
#include <abilities/AbilityKit.h>
#include <abilities/AbilityRegistry.h>
#include <app/EngineSchedule.h>
#include <attributes/AttributeRegistry.h>
#include <core/ResourceStore.h>
#include <ecs/EntityStore.h>
#include <effects/AttributeResolveSystem.h>
#include <effects/EffectDefinition.h>
#include <effects/EffectLifetimeSystem.h>
#include <effects/EffectRegistry.h>
#include <gameplay_tags/GameplayTagQuery.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <movement/GroundingTransitionSystem.h>
#include <movement/JumpExecutionSystem.h>
#include <movement/LocomotionMode.h>
#include <movement/MovementDefs.h>
#include <movement/MovementIntent.h>
#include <movement/MovementModes.h>
#include <movement/MovementProfile.h>
#include <movement/MovementState.h>
#include <movement/MovementTags.h>

namespace
{
    constexpr float kDefaultMoveSpeed = 6.0f;
}

void RegisterMovementComponents(EntityStore& world)
{
    RegisterAbilityComponents(world);

    if (!world.IsRegistered<MovementIntent>())
        world.RegisterComponent<MovementIntent>();
    if (!world.IsRegistered<MovementState>())
        world.RegisterComponent<MovementState>();
    if (!world.IsRegistered<MovementProfile>())
        world.RegisterComponent<MovementProfile>();
    if (!world.IsRegistered<OnGround>())
        world.RegisterComponent<OnGround>();
    if (!world.IsRegistered<InAir>())
        world.RegisterComponent<InAir>();
    if (!world.IsRegistered<LocomotionModeRequest>())
        world.RegisterComponent<LocomotionModeRequest>();
}

void RegisterMovementDefinitions(ResourceStore& sessionResources)
{
    RegisterAbilityDefinitions(sessionResources);
    if (sessionResources.Has<MovementDefs>())
        return; // movement definitions are session-wide: assign their ids once

    GameplayTagRegistry& tagReg = sessionResources.Get<GameplayTagRegistry>();
    const MovementTags tags = RegisterMovementTags(tagReg);
    sessionResources.Register<MovementTags>(tags);

    // The built-in locomotion modes, each mapping its marker to the gameplay tag
    // it projects while active. A game registers its own modes the same way; the
    // arbiter stays mode-agnostic.
    LocomotionModeRegistry& modes = sessionResources.Ensure<LocomotionModeRegistry>();
    RegisterLocomotionMode<OnGround>(modes, tags.Grounded);
    RegisterLocomotionMode<InAir>(modes, tags.Airborne);

    AttributeRegistry& attrReg = sessionResources.Get<AttributeRegistry>();
    EffectRegistry& effReg = sessionResources.Get<EffectRegistry>();
    AbilityRegistry& abilityReg = sessionResources.Get<AbilityRegistry>();

    MovementDefs defs;
    defs.MoveSpeed = attrReg.RegisterAttribute("MoveSpeed", 0.0f, 100.0f, kDefaultMoveSpeed);

    // Jump is authored data, not a code path: gated by grounded, blocked by its own
    // cooldown tag, and its behavior is a short effect granting the one-tick request
    // tag jump execution consumes. Cooldown outlives the request so grants
    // never overlap.
    EffectDefinition request;
    request.Duration = EffectDuration::Duration;
    request.DurationSeconds = 0.05f;
    request.GrantedTags = { tags.JumpRequested };
    const EffectId requestFx = effReg.Register("movement.jump.request", request);

    EffectDefinition cooldown;
    cooldown.Duration = EffectDuration::Duration;
    cooldown.DurationSeconds = 0.3f;
    cooldown.GrantedTags = { tags.JumpCooldown };
    const EffectId cooldownFx = effReg.Register("movement.jump.cooldown", cooldown);

    AbilityDefinition jump;
    jump.ActivationRequirements
        .AddAll(tags.Grounded, GameplayTagMatchMode::Hierarchical)
        .AddNone(tags.JumpCooldown);
    jump.Cooldown = cooldownFx;
    jump.OnActivate = requestFx;
    defs.Jump = abilityReg.Register("movement.jump", jump);

    sessionResources.Register<MovementDefs>(defs);
}

void RegisterMovementSystems(EngineSchedule& schedule)
{
    schedule.Register<GroundingTransitionSystem>();
    schedule.Register<LocomotionModeArbiter>();
    schedule.Register<JumpExecutionSystem>();
    schedule.Register<GroundLocomotionSystem>();
    schedule.Register<AirLocomotionSystem>();

    // Interleave with the ability-kit systems (registered by RegisterAbilityKitSystems):
    // grounding -> arbiter -> ability activation -> jump -> attribute resolve ->
    // ground/air locomotion -> effect lifetime.
    schedule.After<LocomotionModeArbiter, GroundingTransitionSystem>();
    schedule.After<AbilityActivationSystem, LocomotionModeArbiter>();
    schedule.After<JumpExecutionSystem, AbilityActivationSystem>();
    schedule.After<AttributeResolveSystem, JumpExecutionSystem>();

    schedule.After<GroundLocomotionSystem, LocomotionModeArbiter>();
    schedule.After<GroundLocomotionSystem, AttributeResolveSystem>();
    schedule.After<AirLocomotionSystem, LocomotionModeArbiter>();
    schedule.After<AirLocomotionSystem, AttributeResolveSystem>();

    schedule.After<EffectLifetimeSystem, GroundLocomotionSystem>();
    schedule.After<EffectLifetimeSystem, AirLocomotionSystem>();
}
