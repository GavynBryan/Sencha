// Held movement from abilities: a burst drives the character along its aim for a
// time or while held, a tether pulls it toward an anchor and ends when the anchor
// goes, a mode lease holds a locomotion mode, and replay steps all of it through
// the kernel the live tick uses.

#include <abilities/AbilityDefinitionData.h>
#include <abilities/AbilityDefinitionResolve.h>
#include <abilities/AbilityKit.h>
#include <app/EngineVocabulary.h>
#include <attributes/AttributeRegistry.h>
#include <attributes/AttributeSet.h>
#include <app/EngineVocabularyBindings.h>
#include <authored/AuthoredQueryDispatcher.h>
#include <authored/VerbBindingCompiler.h>
#include <authored/VerbDispatcher.h>
#include <authored/WorldVocabulary.h>
#include <controller/LookOrientation.h>
#include <core/json/JsonParser.h>
#include <effects/EffectRegistry.h>
#include <effects/EffectSystem.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <ecs/StoragePartitionSet.h>
#include <input/InputActionRegistry.h>
#include <input/InputActionSource.h>
#include <input/InputActionState.h>
#include <input/InputActionVocabulary.h>
#include <movement/CharacterTickStep.h>
#include <movement/FreeLocomotionSystem.h>
#include <movement/JumpExecutionSystem.h>
#include <movement/LocomotionMode.h>
#include <movement/MovementModeSystems.h>
#include <movement/MotionComposition.h>
#include <movement/HeldMotionStep.h>
#include <movement/MovementIntent.h>
#include <movement/MovementRegistration.h>
#include <movement/MovementVerbs.h>
#include <movement/components/CharacterMovement.h>
#include <physics/CharacterMoverPool.h>
#include <physics/PhysicsWorld.h>
#include <physics/components/CharacterController.h>
#include <time/ActorTimeSystem.h>
#include <time/SimClock.h>
#include <world/ComponentRegistrar.h>
#include <world/RuntimeComponentSchema.h>
#include <world/transform/TransformComponents.h>

#include <gtest/gtest.h>

#include <memory>

namespace
{
constexpr float kDt = 1.0f / 60.0f;
const Vec3d kGravity(0.0f, -9.81f, 0.0f);
const Vec3d kUp(0.0f, 1.0f, 0.0f);
constexpr std::uint8_t kFire = 0;

const StoragePartitionSet& Partitions()
{
    static const StoragePartitionSet partitions = [] {
        StoragePartitionSet value;
        value.Add(StoragePartitionId::Default());
        return value;
    }();
    return partitions;
}

class AbilityMovement : public ::testing::Test
{
protected:
    AbilityMovement()
    {
        ComponentRegistrar registrar(Entities);
        RegisterEngineComponents(registrar);
        RegisterMovement(Entities);
        RegisterAbilityKit(Entities);
        InstallAuthoredVocabulary(Entities);
        EXPECT_TRUE(DeclareEngineVocabulary(Entities));
        Actions.Rebuild(std::vector<InputActionDefinition>{ { .Name = "Input.PrimaryFire" } });
        Entities.SetResource(InputActionVocabulary{ &Actions, 1 });
        Entities.AddResource<InputActionState>().Configure(1);
        Flight = Entities.GetResource<LocomotionModeRegistry>().Register<FlightSession>("movement.mode.flight");

        Verbs = std::make_unique<VerbDispatcher>(*FindVerbRegistry(Entities));
        Queries = std::make_unique<AuthoredQueryDispatcher>(*FindAuthoredQueryRegistry(Entities));
        Bindings = std::make_unique<EngineVocabularyBindings>(Entities, Clock, *Verbs);
        Host = AbilityHost{ .Verbs = Verbs.get(), .Queries = Queries.get() };

        BodyDesc floor;
        floor.Shape = CollisionShape::MakeBox(Vec3d(50.0f, 0.5f, 50.0f));
        floor.Motion = BodyMotion::Static;
        floor.Layer = CollisionLayer::Static;
        (void)Physics.AddBody(floor);
        Character = Spawn();
    }

    EntityId Spawn()
    {
        const EntityId character = Entities.CreateEntity();
        Entities.AddComponent<LocalTransform>(
            character, LocalTransform{ Transform3f{ Vec3d(0.0f, 1.5f, 0.0f), Quatf::Identity(), Vec3d::One() } });
        Entities.AddComponent<CharacterController>(character, CharacterController{});
        Entities.AddComponent<CharacterMovement>(
            character, CharacterMovement{ .Mode = Entities.GetResource<LocomotionModeRegistry>().FreeMode() });
        Entities.AddComponent<LookOrientation>(character, LookOrientation{});
        Entities.AddComponent<InputActionSourceRef>(character, InputActionSourceRef{});
        Entities.AddComponent<AbilityGrants>(character, AbilityGrants{});
        Movers.Reconcile(Entities, Partitions());
        return character;
    }

    void Grant(std::string_view json)
    {
        AbilityDeclaration declaration;
        std::vector<AssetRef> dependencies;
        std::vector<AbilityDiagnostic> diagnostics;
        ASSERT_TRUE(CompileAbilityDeclaration(*JsonParse(json), declaration, dependencies, diagnostics))
            << diagnostics.front().Message;
        AbilityDefinition definition;
        ASSERT_TRUE(ResolveAbilityDeclaration(declaration, "asset://abilities/held.sdata",
                                              MakeAbilityResolveEnvironment(Entities), definition, diagnostics))
            << diagnostics.front().Path << " " << diagnostics.front().Message;
        const AbilityId ability =
            Entities.GetResource<AbilityRegistry>().Define("asset://abilities/held.sdata", std::move(definition));
        const std::uint16_t entry = Entities.GetResource<AbilityGrantEntries>().Intern({ "g", ability, {} });
        (void)GrantAbility(*Entities.TryGet<AbilityGrants>(Character), ability, AbilityGrantKind::Authored, entry);
    }

    // One fixed tick: input, the actor clock, abilities, attributes and effects, then movement and the motor.
    void Step(std::vector<InputActionEdge> edges = {})
    {
        InputActionState& state = Entities.GetResource<InputActionState>();
        std::span<InputActionValue> values = state.BeginTick(Now);
        for (const InputActionEdge& edge : edges)
        {
            Held = edge.Kind == InputEdgeKind::Press;
            (void)state.TickEdgeStorage().Append(edge);
        }
        values[0].Flags = Held ? InputActionFlags::Held : InputActionFlags::None;
        AdvanceActorTime(Entities, Now);
        RunAbilityPass(Entities, Host, FixedSimTime{ .DeltaSeconds = kDt, .TickIndex = Now }, nullptr, Settings);
        ResolveAttributesWithEffects(Entities);
        TickEffects(Entities);
        Modes.Step(Entities);
        Transitions.Step(Entities);
        Locomotion.Step(Entities, kDt);
        Jump.Step(Entities, kDt);
        Sessions.Step(Entities, kDt);
        Composition.Step(Entities);
        Movers.Reconcile(Entities, Partitions());
        Movers.Drive(Entities, Partitions(), kDt, kGravity);
        ++Now;
    }

    void Steps(int count)
    {
        for (int i = 0; i < count; ++i)
            Step();
    }

    [[nodiscard]] Vec3d Position(EntityId entity) const
    {
        return static_cast<const World&>(Entities).TryGet<LocalTransform>(entity)->Value.Position;
    }

    [[nodiscard]] const MotionBursts& Bursts() const
    {
        return *static_cast<const World&>(Entities).TryGet<MotionBursts>(Character);
    }

    // A lease taken or let go by hand, as its provider sees the call.
    VerbAdmission Lease(VerbAdmission (MovementOperations::*call)(const VerbInvocation&), const LeaseOwner& owner,
                        std::vector<AuthoredValue> arguments)
    {
        AuthoredArguments record(arguments.size());
        for (std::size_t slot = 0; slot < arguments.size(); ++slot)
            record.Set(slot, std::move(arguments[slot]));
        VerbInvocation invocation;
        invocation.Owner = owner;
        invocation.Step = arguments.empty() ? LeaseStep::Release : LeaseStep::Acquire;
        invocation.Arguments = &record;
        invocation.Tick = Now;
        return (Provider.*call)(invocation);
    }

    World Entities;
    InputActionRegistry Actions;
    LocomotionModeId Flight;
    FixedSimulationLoop Clock{ kDt };
    std::unique_ptr<VerbDispatcher> Verbs;
    std::unique_ptr<AuthoredQueryDispatcher> Queries;
    std::unique_ptr<EngineVocabularyBindings> Bindings;
    AbilityHost Host;
    AbilitySettings Settings;
    PhysicsWorld Physics;
    CharacterMoverPool Movers{ Physics };
    ModeRequestCollectionSystem Modes;
    LocomotionModeTransitionSystem Transitions;
    FreeLocomotionSystem Locomotion{ kGravity, kUp };
    JumpExecutionSystem Jump;
    HeldMotionSystem Sessions;
    MotionCompositionSystem Composition;
    MovementOperations Provider{ Entities };
    EntityId Character;
    std::uint64_t Now = 0;
    bool Held = false;
};

constexpr std::string_view kLunge = R"({
    "input_default": { "action": "Input.PrimaryFire" },
    "phases": [ { "key": "p_lunge", "name": "Lunge",
        "hold": [ { "key": "o_burst", "lease": "movement.burst",
                    "arguments": { "target": { "port": "actor" }, "direction": { "port": "aim.direction" },
                                   "speed": { "const": 12 }, "seconds": { "const": 0.25 } } } ],
        "then": [ { "key": "t_done", "on": "timeout", "ticks": 40, "to": "end" } ] } ] })";

constexpr std::string_view kHeldDash = R"({
    "input_default": { "action": "Input.PrimaryFire" },
    "phases": [ { "key": "p_dash", "name": "Dash",
        "hold": [ { "key": "o_burst", "lease": "movement.burst",
                    "arguments": { "target": { "port": "actor" }, "direction": { "port": "aim.direction" },
                                   "speed": { "const": 12 } } } ],
        "then": [ { "key": "t_done", "on": "timeout", "ticks": 6, "to": "end" } ] } ] })";
}

TEST_F(AbilityMovement, ALungeCarriesTheCharacterAlongItsAimForItsTime)
{
    Grant(kLunge);
    Steps(20);
    const Vec3d start = Position(Character);
    Step({ { kFire, InputEdgeKind::Press }, { kFire, InputEdgeKind::Release } });
    Steps(14);
    const Vec3d travelled = Position(Character) - start;
    EXPECT_NEAR(travelled.Z, -12.0f * 0.25f, 0.1f) << "twelve metres a second for a quarter of one, along the aim";
    EXPECT_NEAR(travelled.X, 0.0f, 1e-3f);
    EXPECT_FALSE(Bursts().Slots[0].Owner.IsSet()) << "its time ran out";
    Steps(30);
    EXPECT_LT((Position(Character) - start).Z, travelled.Z) << "and the character coasts on what it was given";
}

TEST_F(AbilityMovement, AHeldBurstEndsWithItsActivation)
{
    Grant(kHeldDash);
    Steps(20);
    const Vec3d start = Position(Character);
    Step({ { kFire, InputEdgeKind::Press }, { kFire, InputEdgeKind::Release } });
    Steps(5);
    EXPECT_TRUE(Bursts().Slots[0].Owner.IsSet());
    EXPECT_NEAR((Position(Character) - start).Z, -12.0f * 6.0f / 60.0f, 0.05f) << "driven for the six ticks it was held";
    Step();
    EXPECT_FALSE(Bursts().Slots[0].Owner.IsSet()) << "the activation ended and let go of it";
}

TEST_F(AbilityMovement, ReplayStepsABurstAsTheLiveTickDid)
{
    // The same character in a World of its own, stepped only through replay's kernel.
    World kernel;
    ComponentRegistrar registrar(kernel);
    RegisterEngineComponents(registrar);
    RegisterMovement(kernel);
    PhysicsWorld physics;
    BodyDesc floor;
    floor.Shape = CollisionShape::MakeBox(Vec3d(50.0f, 0.5f, 50.0f));
    floor.Motion = BodyMotion::Static;
    floor.Layer = CollisionLayer::Static;
    (void)physics.AddBody(floor);
    CharacterMoverPool movers(physics);
    const EntityId replayed = kernel.CreateEntity();
    kernel.AddComponent<LocalTransform>(
        replayed, LocalTransform{ Transform3f{ Vec3d(0.0f, 1.5f, 0.0f), Quatf::Identity(), Vec3d::One() } });
    kernel.AddComponent<CharacterController>(replayed, CharacterController{});
    kernel.AddComponent<CharacterMovement>(
        replayed, CharacterMovement{ .Mode = kernel.GetResource<LocomotionModeRegistry>().FreeMode() });
    movers.Reconcile(kernel, Partitions());

    const auto settle = [&](std::uint64_t ticks) {
        for (std::uint64_t i = 0; i < ticks; ++i)
            StepCharacterTick(kernel, &movers, replayed, MovementIntent{}, i, kDt, kGravity, kUp);
    };
    Steps(20);
    settle(20);
    const Vec3d liveStart = Position(Character);
    const Vec3d kernelStart = kernel.TryGet<LocalTransform>(replayed)->Value.Position;
    const MotionBurst burst{ .Owner = {}, .Velocity = Vec3d(0.0f, 0.0f, -10.0f), .RemainingSeconds = 0.2f,
                             .Expires = true };
    Entities.TryGet<MotionBursts>(Character)->Slots[0] = burst;
    Entities.TryGet<MotionBursts>(Character)->Slots[0].Owner = { .Entity = Character };
    kernel.TryGet<MotionBursts>(replayed)->Slots[0] = burst;
    kernel.TryGet<MotionBursts>(replayed)->Slots[0].Owner = { .Entity = replayed };
    for (std::uint64_t tick = 20; tick < 40; ++tick)
    {
        Step();
        StepCharacterTick(kernel, &movers, replayed, MovementIntent{}, tick, kDt, kGravity, kUp);
    }
    const Vec3d live = Position(Character) - liveStart;
    const Vec3d replay = kernel.TryGet<LocalTransform>(replayed)->Value.Position - kernelStart;
    EXPECT_NEAR(replay.Z, live.Z, 1e-3f);
    EXPECT_FALSE(kernel.TryGet<MotionBursts>(replayed)->Slots[0].Owner.IsSet());
    movers.EvictAll(kernel);
}

TEST_F(AbilityMovement, ATetherPullsTowardItsAnchorAndEndsWhenTheAnchorGoes)
{
    const EntityId anchor = Entities.CreateEntity();
    Entities.AddComponent<LocalTransform>(
        anchor, LocalTransform{ Transform3f{ Vec3d(10.0f, 1.5f, 0.0f), Quatf::Identity(), Vec3d::One() } });
    Steps(10);
    const LeaseOwner holder{ .Entity = Character, .Key = 1 };
    ASSERT_EQ(Lease(&MovementOperations::Tether, holder,
                    { AuthoredValue::Entity(Character), AuthoredValue::Entity(anchor), AuthoredValue{},
                      AuthoredValue::Float(2.0), AuthoredValue::Float(12.0) }),
              VerbAdmission::Accepted);
    Steps(90);
    const float distance = (Position(anchor) - Position(Character)).Magnitude();
    EXPECT_LT(distance, 2.2f) << "pulled in to about the rope's length";
    EXPECT_GT(distance, 1.5f) << "and no further";
    Entities.DestroyEntity(anchor);
    Step();
    EXPECT_FALSE(static_cast<const World&>(Entities).TryGet<TetherSession>(Character)->Owner.IsSet())
        << "its anchor is gone, so it is";
}

TEST_F(AbilityMovement, AModeLeaseHoldsTheModeUntilLetGoOrItsHolderGoes)
{
    Steps(2);
    const LeaseOwner holder{ .Entity = Character, .Key = 1 };
    ASSERT_EQ(Lease(&MovementOperations::Mode, holder,
                    { AuthoredValue::Entity(Character), AuthoredValue::String("movement.mode.flight") }),
              VerbAdmission::Accepted);
    Step();
    EXPECT_EQ(Entities.TryGet<CharacterMovement>(Character)->Mode, Flight);
    ASSERT_EQ(Lease(&MovementOperations::Mode, holder, {}), VerbAdmission::Accepted);
    Step();
    EXPECT_EQ(Entities.TryGet<CharacterMovement>(Character)->Mode,
              Entities.GetResource<LocomotionModeRegistry>().FreeMode());

    const EntityId holderEntity = Entities.CreateEntity();
    ASSERT_EQ(Lease(&MovementOperations::Mode, { .Entity = holderEntity },
                    { AuthoredValue::Entity(Character), AuthoredValue::String("movement.mode.flight") }),
              VerbAdmission::Accepted);
    Step();
    ASSERT_EQ(Entities.TryGet<CharacterMovement>(Character)->Mode, Flight);
    Entities.DestroyEntity(holderEntity);
    Steps(2);
    EXPECT_EQ(Entities.TryGet<CharacterMovement>(Character)->Mode,
              Entities.GetResource<LocomotionModeRegistry>().FreeMode())
        << "the holder is gone, so its mode is let go";
}

// Overclock as content and nothing else: a toggled phase holding a time scale and an
// energy drain, continued while energy lasts, cooling down when it ends.
TEST_F(AbilityMovement, OverclockDilatesUntilItsEnergyRunsOutThenCoolsDown)
{
    const GameplayTagId cooling = *Entities.GetResource<GameplayTagRegistry>().RegisterTag("Cooldown.Overclock");
    const AttributeId energy = Entities.GetResource<AttributeRegistry>().RegisterAttribute("Energy", 0.0f, 100.0f, 100.0f);
    EffectDefinition drain{};
    drain.Duration = EffectDuration::Infinite;
    drain.Period = 0.1f;
    drain.Modifiers = { { energy, ModifierOp::Add, -25.0f } };
    EffectDefinition cooldown{};
    cooldown.Duration = EffectDuration::Duration;
    cooldown.DurationSeconds = 1.0f;
    cooldown.GrantedTags = { cooling };
    EffectRegistry& effects = Entities.GetResource<EffectRegistry>();
    (void)effects.Define("asset://effects/drain.sdata", drain);
    (void)effects.Define("asset://effects/overclock_cooldown.sdata", cooldown);
    Entities.AddComponent<AttributeSet>(Character);
    (void)Entities.TryGet<AttributeSet>(Character)->Add(energy, 100.0f);
    Grant(R"({
        "input_default": { "action": "Input.PrimaryFire", "policy": "toggle" },
        "activation": [ { "key": "a_ready", "tags": { "none": ["Cooldown.Overclock"] } } ],
        "continuation": [ { "key": "c_energy", "attribute": { "name": "Energy", "above": 0 } } ],
        "cooldown": { "effect": { "data": "asset://effects/overclock_cooldown.sdata" }, "start": "on_end" },
        "phases": [ { "key": "p_on", "name": "Overclocked",
            "hold": [ { "key": "o_fast", "lease": "time.scale",
                        "arguments": { "target": { "port": "actor" }, "scale": { "const": 2 } } },
                      { "key": "o_drain", "lease": "effect.hold",
                        "arguments": { "target": { "port": "actor" },
                                       "effect": { "data": "asset://effects/drain.sdata" } } } ],
            "then": [ { "key": "t_off", "on": "release", "to": "end" } ] } ] })");
    const auto scale = [&] { return static_cast<const World&>(Entities).TryGet<ActorTime>(Character)->ScaleQ16; };
    Steps(2);
    Step({ { kFire, InputEdgeKind::Press } });
    Step();
    EXPECT_EQ(scale(), 2 * kActorTimeOne) << "dilated from the tick after it began";
    Steps(40);
    EXPECT_FLOAT_EQ(Entities.TryGet<AttributeSet>(Character)->GetCurrent(energy), 0.0f) << "drained in its own time";
    EXPECT_EQ(scale(), kActorTimeOne) << "out of energy, it ended and let its time go";
    EXPECT_GT(Entities.TryGet<GameplayTagContainer>(Character)->StackCount(cooling), 0u) << "and cools down";
    Step({ { kFire, InputEdgeKind::Press } });
    Step();
    EXPECT_EQ(scale(), kActorTimeOne) << "not while cooling down";
}
