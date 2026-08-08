#include <movement/MovementRegistration.h>

#include <abilities/AbilityDefinition.h>
#include <abilities/AbilityKit.h>
#include <abilities/AbilityRegistry.h>
#include <app/EngineSchedule.h>
#include <attributes/AttributeRegistry.h>
#include <ecs/World.h>
#include <effects/AttributeResolveSystem.h>
#include <effects/EffectDefinition.h>
#include <effects/EffectLifetimeSystem.h>
#include <effects/EffectRegistry.h>
#include <gameplay_tags/GameplayTagQuery.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <movement/FreeLocomotionSystem.h>
#include <movement/JumpExecutionSystem.h>
#include <movement/LocomotionMode.h>
#include <movement/MotionComposition.h>
#include <movement/MovementComponents.h>
#include <movement/MovementDefs.h>
#include <movement/MovementIntent.h>
#include <movement/MovementModeSystems.h>
#include <movement/MovementTags.h>
#include <movement/MovementTuningResolutionSystem.h>

namespace
{
    constexpr float kDefaultMoveSpeed = 6.0f;

    MovementTags EnsureMovementTags(World& world)
    {
        GameplayTagRegistry& tagReg = world.GetResource<GameplayTagRegistry>();
        const MovementTags tags = RegisterMovementTags(tagReg);
        if (!world.HasResource<MovementTags>())
            world.AddResource<MovementTags>(tags);
        return world.GetResource<MovementTags>();
    }
}

void RegisterMovementComponents(World& world)
{
    RegisterAbilityKit(world);

    const auto ensure = [&world]<typename T>()
    {
        if (!world.IsRegistered<T>())
            world.RegisterComponent<T>();
    };

    ensure.template operator()<MovementIntent>();
    ensure.template operator()<JumpState>();
    ensure.template operator()<KinematicState>();
    ensure.template operator()<SupportState>();
    ensure.template operator()<Immersion>();
    ensure.template operator()<CharacterMovement>();
    ensure.template operator()<ResolvedMovementTuning>();
    ensure.template operator()<LocomotionOutput>();
    ensure.template operator()<MotionAxisOverride>();
    ensure.template operator()<MotionImpulse>();
    ensure.template operator()<MotionRequest>();
    ensure.template operator()<ModeTransitionRequest>();
    ensure.template operator()<ClingSession>();
    ensure.template operator()<FlightSession>();

    (void)EnsureMovementTags(world);

    // Free is the one built-in mode: one planar algorithm whose ground and air
    // behavior is a difference in resolved coefficients, not a difference in
    // archetype. A game adds its own modes through the same registry.
    GameplayTagRegistry& tagRegistry = world.GetResource<GameplayTagRegistry>();
    LocomotionModeRegistry& modes = world.HasResource<LocomotionModeRegistry>()
        ? world.GetResource<LocomotionModeRegistry>()
        : world.AddResource<LocomotionModeRegistry>(tagRegistry);
    if (!modes.FreeMode().IsValid())
        (void)modes.RegisterFree();
}

void RegisterDefaultMovementAbilities(World& world)
{
    RegisterMovementComponents(world);

    AttributeRegistry& attrReg = world.GetResource<AttributeRegistry>();
    (void)EnsureMovementTags(world);

    MovementDefs defs;
    defs.MoveSpeed = attrReg.RegisterAttribute("MoveSpeed", 0.0f, 100.0f, kDefaultMoveSpeed);

    if (!world.HasResource<MovementDefs>())
        world.AddResource<MovementDefs>(defs);
}

void RegisterMovement(World& world)
{
    RegisterDefaultMovementAbilities(world);
}

void RegisterMovementSystems(EngineSchedule& schedule, DataAssetCache& dataAssets)
{
    schedule.Register<SupportTagProjectionSystem>();
    schedule.Register<ModeRequestCollectionSystem>();
    schedule.Register<LocomotionModeTransitionSystem>();
    schedule.Register<MovementTuningResolutionSystem>(dataAssets);
    schedule.Register<FreeLocomotionSystem>();
    schedule.Register<JumpExecutionSystem>();
    schedule.Register<MotionCompositionSystem>();

    // The tick reads as one sentence: project last step's physical facts onto
    // tags, collect and apply mode transitions, resolve this tick's
    // coefficients from the profile, run locomotion, let actions contribute,
    // then compose the single motor request.
    schedule.After<ModeRequestCollectionSystem, SupportTagProjectionSystem>();
    schedule.After<AbilityActivationSystem, SupportTagProjectionSystem>();
    schedule.After<ModeRequestCollectionSystem, AbilityActivationSystem>();
    schedule.After<LocomotionModeTransitionSystem, ModeRequestCollectionSystem>();

    // Tuning resolution reads the mode chosen this tick and the attributes
    // effects have already resolved, so it follows both.
    schedule.After<AttributeResolveSystem, AbilityActivationSystem>();
    schedule.After<MovementTuningResolutionSystem, LocomotionModeTransitionSystem>();
    schedule.After<MovementTuningResolutionSystem, AttributeResolveSystem>();

    schedule.After<FreeLocomotionSystem, MovementTuningResolutionSystem>();

    // Jump contributes over locomotion's base output, before composition folds
    // the channels together.
    schedule.After<JumpExecutionSystem, FreeLocomotionSystem>();
    schedule.After<MotionCompositionSystem, JumpExecutionSystem>();

    schedule.After<EffectLifetimeSystem, MotionCompositionSystem>();
}
