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
#include <movement/CharacterMovementSerializer.h>
#include <movement/FreeLocomotionSystem.h>
#include <movement/JumpExecutionSystem.h>
#include <movement/LocomotionMode.h>
#include <movement/MotionComposition.h>
#include <movement/MovementComponentSchemas.h>
#include <movement/components/MovementSessions.h>
#include <movement/MovementDefs.h>
#include <movement/MovementIntent.h>
#include <movement/MovementModeSystems.h>
#include <movement/MovementTags.h>
#include <movement/MovementTuningResolutionSystem.h>
#include <movement/MovementTuningSourceSerializer.h>
#include <world/ComponentRegistrar.h>

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

void RegisterMovementComponents(ComponentRegistrar& registrar)
{
    RegisterAbilityKitComponents(registrar);

    // The physical facts, this tick's request and resolved coefficients, and
    // the contribution channels that compose into one motor request.
    registrar.Add<MovementIntent>();
    registrar.Add<JumpState>();
    registrar.Add<KinematicState>();
    registrar.Add<SupportState>();
    registrar.Add<Immersion>();
    // The mode is a registration and the profile is an asset, so what content
    // states for both is a name; the wire keeps the id the mode is.
    registrar.Add<CharacterMovement>();
    registrar.AddSerializer(MakeCharacterMovementSerializer());
    registrar.Add<MovementTuningSource>();
    registrar.AddSerializer(MakeMovementTuningSourceSerializer());
    registrar.Add<ResolvedMovementTuning>();
    registrar.Add<LocomotionOutput>();
    registrar.Add<MotionAxisOverride>();
    registrar.Add<MotionImpulse>();
    registrar.Add<MotionRequest>();
    registrar.Add<ModeTransitionRequest>();
    registrar.Add<ClingSession>();
    registrar.Add<FlightSession>();
}

void RegisterMovementComponents(World& world)
{
    RegisterAbilityKit(world);

    ComponentRegistrar registrar(world);
    RegisterMovementComponents(registrar);

    // Where MovementTuningSource's hooks hold its profile. Registered empty
    // beside the component rather than by each host, so a world that can carry
    // the component can always answer where its asset lives; a host with a
    // data-asset cache points this at it.
    if (!world.HasResource<MovementComponentAssets>())
        world.AddResource<MovementComponentAssets>(nullptr);

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

void RegisterMovementSystems(EngineSchedule& schedule, DataAssetCache& dataAssets,
                             LoggingProvider* logging)
{
    schedule.Register<SupportTagProjectionSystem>();
    schedule.Register<ModeRequestCollectionSystem>();
    schedule.Register<LocomotionModeTransitionSystem>();
    schedule.Register<MovementTuningResolutionSystem>(dataAssets, logging);
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
