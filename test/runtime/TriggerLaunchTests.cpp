#include <gtest/gtest.h>

#include <abilities/AbilityKit.h>
#include <app/EngineSchedule.h>
#include <app/EngineVocabulary.h>
#include <app/EngineVocabularyBindings.h>
#include <app/GameContexts.h>
#include <assets/data/DataAssetCache.h>
#include <attributes/AttributeSet.h>
#include <authored/AuthoredQueryDispatcher.h>
#include <authored/VerbDispatcher.h>
#include <authored/WorldVocabulary.h>
#include <core/config/EngineConfig.h>
#include <core/console/ConsoleService.h>
#include <core/json/JsonParser.h>
#include <core/logging/LoggingProvider.h>
#include <ecs/WorldComponentSchema.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <logic/Trigger.h>
#include <logic/TriggerSequences.h>
#include <logic/TriggerSystem.h>
#include <movement/LocomotionMode.h>
#include <movement/FreeLocomotionSystem.h>
#include <movement/MovementDefs.h>
#include <movement/MovementIntent.h>
#include <movement/MovementRegistration.h>
#include <movement/components/CharacterFacts.h>
#include <movement/components/CharacterMovement.h>
#include <physics/PhysicsRegistration.h>
#include <physics/PhysicsStepSystem.h>
#include <physics/components/CharacterController.h>
#include <physics/components/Collider.h>
#include <runtime/RuntimeFrameLoop.h>
#include <world/RuntimeComponentSchema.h>
#include <world/RuntimeWorld.h>
#include <world/SimulationAuthority.h>
#include <world/transform/TransformComponents.h>
#include <world/transform/TransformPropagation.h>

#include <memory>
#include <numbers>
#include <string_view>

// A bumper as content builds one: a sensor box and a trigger whose sequence
// names movement.launch, run through the real fixed tick -- the trigger pass,
// movement, the character mover, the sensor sample -- with nothing written for
// bumpers anywhere in it.

namespace
{
constexpr double kDt = 1.0 / 60.0;
constexpr float kCapsuleCentre = 0.95f;

// Forward along the trigger, eight up: the sequence a designer writes.
constexpr std::string_view kLaunch = R"({"commands": [{"verb": "movement.launch", "arguments": {
    "target": {"port": "activator"},
    "direction": {"const": [0, 0, 1], "frame": "trigger"},
    "speed": 10, "rise": 8}}]})";

struct LaunchHarness
{
    EngineConfig Config;
    RuntimeFrameLoop Loop;
    DataAssetCache DataAssets;
    ConsoleService Console;
    LoggingProvider Logging;
    WorldComponentSchema Schema = MakeSchema();
    RuntimeWorld Runtime{ Schema };
    std::unique_ptr<VerbDispatcher> Verbs;
    std::unique_ptr<AuthoredQueryDispatcher> Queries;
    std::unique_ptr<EngineVocabularyBindings> Bindings;
    EngineSchedule Schedule;
    TriggerSystem* Triggers = nullptr;
    std::uint64_t Tick = 0;

    LaunchHarness()
    {
        World& world = Runtime.Entities();
        RegisterMovement(world);
        InstallAuthoredVocabulary(world);
        EXPECT_TRUE(InstallEngineVocabulary(world));
        Verbs = std::make_unique<VerbDispatcher>(*FindVerbRegistry(world));
        Queries = std::make_unique<AuthoredQueryDispatcher>(*FindAuthoredQueryRegistry(world));
        Bindings = std::make_unique<EngineVocabularyBindings>(world, Loop.GetSimulationClock(), *Verbs);

        RegisterPhysics(Schedule);
        RegisterAbilityKitSystems(Schedule);
        RegisterMovementSystems(Schedule, DataAssets);
        Triggers = &RegisterTriggerSystem(Schedule, world, *Verbs, Queries.get(), nullptr, &DataAssets,
                                          Console.Registry(), Logging);
        Schedule.After<FreeLocomotionSystem, TriggerSystem>();
        Schedule.Init();

        BodyDesc floor;
        floor.Shape = CollisionShape::MakeBox(Vec3d(50.0f, 0.5f, 50.0f));
        floor.Position = Vec3d(0.0f, -0.5f, 0.0f);
        floor.Layer = CollisionLayer::Static;
        (void)Physics().AddBody(floor);
    }

    ~LaunchHarness()
    {
        Schedule.Shutdown();
        Bindings.reset();
    }

    static WorldComponentSchema MakeSchema()
    {
        WorldComponentSchema schema;
        RegisterEngineRuntimeComponents(schema);
        schema.Seal();
        return schema;
    }

    World& Entities() { return Runtime.Entities(); }
    PhysicsWorld& Physics() { return Schedule.Get<PhysicsStepSystem>()->GetSimulation(); }

    EntityId Character(Vec3d feet, Vec3d wish)
    {
        World& world = Entities();
        const EntityId character = world.CreateEntity();
        Transform3f pose = Transform3f::Identity();
        pose.Position = feet + Vec3d(0.0f, kCapsuleCentre, 0.0f);
        world.AddComponent<LocalTransform>(character, LocalTransform{ pose });
        world.AddComponent<WorldTransform>(character, WorldTransform{ pose });
        world.AddComponent<CharacterController>(character, CharacterController{});
        world.AddComponent<CharacterMovement>(
            character, CharacterMovement{ .Mode = world.GetResource<LocomotionModeRegistry>().FreeMode() });
        AttributeSet attributes;
        attributes.Add(world.GetResource<MovementDefs>().MoveSpeed, 6.0f);
        world.AddComponent<AttributeSet>(character, attributes);
        world.AddComponent<GameplayTagContainer>(character, GameplayTagContainer{});
        world.TryGet<MovementIntent>(character)->WishDir = wish;
        return character;
    }

    // A sensor box `halfExtents` whose centre sits `centre` above the entity's
    // origin, turned `yaw` about up, running `sequence` when entered.
    EntityId Bumper(Vec3d origin, float yaw, Vec3d halfExtents, Vec3d centre, std::string_view sequence)
    {
        World& world = Entities();
        const EntityId bumper = world.CreateEntity();
        Transform3f pose = Transform3f::Identity();
        pose.Position = origin;
        pose.Rotation = Quatf::FromAxisAngle(Vec3d(0.0f, 1.0f, 0.0f), yaw);
        world.AddComponent<LocalTransform>(bumper, LocalTransform{ pose });
        world.AddComponent<WorldTransform>(bumper, WorldTransform{ pose });
        Collider collider;
        collider.Shape = CollisionShape::MakeBox(halfExtents);
        collider.Shape.Center = centre;
        collider.IsTrigger = true;
        world.AddComponent<Collider>(bumper, collider);
        world.AddComponent<Trigger>(bumper, Trigger{});
        std::optional<JsonValue> parsed = JsonParse(sequence);
        EXPECT_TRUE(parsed && SetTriggerSequence(world, bumper, *parsed));
        return bumper;
    }

    void Step()
    {
        World& world = Entities();
        const FrameZoneView& zones = Runtime.BuildFrameView();
        const FixedSimTime time{ .DeltaSeconds = kDt, .TickIndex = Tick };
        FixedLogicContext logic{ .Config = Config, .Runtime = Loop, .Time = time, .Entities = world,
                                 .Partitions = zones.Logic };
        Schedule.RunFixedLogic(logic);
        PhysicsContext physics{ Config, Loop, time, world, zones.Physics };
        Schedule.RunPhysics(physics);
        PropagateTransforms(world, zones.Logic, TransformPropagationDomain::Simulation);
        Runtime.EndFrameView();
        ++Tick;
    }

    Vec3d Velocity(EntityId character) { return std::as_const(Entities()).TryGet<KinematicState>(character)->Velocity; }
    Vec3d Feet(EntityId character)
    {
        return std::as_const(Entities()).TryGet<WorldTransform>(character)->Value.Position
            - Vec3d(0.0f, kCapsuleCentre, 0.0f);
    }
};
} // namespace

TEST(TriggerLaunch, AWalkerEnteringABumperIsLaunchedAlongTheBumpersForward)
{
    LaunchHarness harness;
    const EntityId walker = harness.Character(Vec3d(0.0f, 0.0f, 0.0f), Vec3d(0.0f, 0.0f, 1.0f));
    // A pad on the floor three metres ahead, turned so its forward is world +X;
    // its box sits above the entity's origin, which is on the floor.
    (void)harness.Bumper(Vec3d(0.0f, 0.0f, 3.0f), std::numbers::pi_v<float> * 0.5f, Vec3d(1.0f, 0.25f, 0.5f),
                         Vec3d(0.0f, 0.25f, 0.0f), kLaunch);

    bool launched = false;
    for (int i = 0; i < 240 && !launched; ++i)
    {
        harness.Step();
        launched = harness.Velocity(walker).Y > 6.0f;
    }
    ASSERT_TRUE(launched) << "the walker crossed the bumper and was never launched";
    EXPECT_EQ(harness.Triggers->Stats().Activations, 1u);
    EXPECT_EQ(harness.Triggers->Stats().Failed, 0u);
    // The launch replaced the walk: it goes the bumper's way, not its own.
    const Vec3d velocity = harness.Velocity(walker);
    EXPECT_GT(velocity.X, 8.0f);
    EXPECT_NEAR(velocity.Z, 0.0f, 0.5f);

    // Launched, not held: let go of the stick and it rises and carries on.
    harness.Entities().TryGet<MovementIntent>(walker)->WishDir = Vec3d::Zero();
    const Vec3d before = harness.Feet(walker);
    for (int i = 0; i < 20; ++i)
        harness.Step();
    EXPECT_GT(harness.Feet(walker).Y - before.Y, 1.0f);
    EXPECT_GT(harness.Feet(walker).X - before.X, 0.5f);
}

TEST(TriggerLaunch, AFastCrossingOfAThinBumperStillLaunches)
{
    LaunchHarness harness;
    // Fifty metres a second across a bumper twenty centimetres thick: no fixed
    // tick finds the character inside it.
    const EntityId runner = harness.Character(Vec3d(0.0f, 0.0f, -2.0f), Vec3d::Zero());
    harness.Entities().TryGet<KinematicState>(runner)->Velocity = Vec3d(0.0f, 0.0f, 50.0f);
    (void)harness.Bumper(Vec3d(0.0f, 0.0f, 1.0f), 0.0f, Vec3d(2.0f, 2.0f, 0.1f), Vec3d(0.0f, 2.0f, 0.0f), kLaunch);

    float highest = 0.0f;
    for (int i = 0; i < 10; ++i)
    {
        harness.Step();
        highest = std::max(highest, harness.Velocity(runner).Y);
    }
    EXPECT_EQ(harness.Triggers->Stats().Activations, 1u);
    EXPECT_GT(highest, 6.0f);
}

TEST(TriggerLaunch, AClientDoesNotLaunchWhatItSeesEnterABumper)
{
    LaunchHarness harness;
    harness.Entities().AddResource<SimulationAuthority>().Authoritative = false;
    const EntityId walker = harness.Character(Vec3d(0.0f, 0.0f, 0.0f), Vec3d(0.0f, 0.0f, 1.0f));
    (void)harness.Bumper(Vec3d(0.0f, 0.0f, 2.0f), 0.0f, Vec3d(1.0f, 0.25f, 0.5f), Vec3d(0.0f, 0.25f, 0.0f), kLaunch);

    float highest = 0.0f;
    for (int i = 0; i < 120; ++i)
    {
        harness.Step();
        highest = std::max(highest, harness.Velocity(walker).Y);
    }
    EXPECT_LT(highest, 1.0f);
    EXPECT_EQ(harness.Triggers->Stats().Activations, 0u);
}

TEST(TriggerLaunch, TheLaunchCanBeReplacedByAnotherVerbWithoutTouchingTheTrigger)
{
    LaunchHarness harness;
    const EntityId walker = harness.Character(Vec3d(0.0f, 0.0f, 0.0f), Vec3d(0.0f, 0.0f, 1.0f));
    const EntityId bumper = harness.Bumper(Vec3d(0.0f, 0.0f, 2.0f), 0.0f, Vec3d(1.0f, 0.25f, 0.5f),
                                           Vec3d(0.0f, 0.25f, 0.0f), kLaunch);
    // The same trigger, its sequence now another engine verb: it disarms itself.
    std::optional<JsonValue> disarm = JsonParse(R"({"commands": [
        {"verb": "trigger.disable", "arguments": {"trigger": {"port": "trigger"}}}]})");
    ASSERT_TRUE(disarm && SetTriggerSequence(harness.Entities(), bumper, *disarm));

    float highest = 0.0f;
    for (int i = 0; i < 120; ++i)
    {
        harness.Step();
        highest = std::max(highest, harness.Velocity(walker).Y);
    }
    EXPECT_EQ(harness.Triggers->Stats().Activations, 1u);
    EXPECT_FALSE(harness.Entities().TryGet<Trigger>(bumper)->Enabled);
    EXPECT_LT(highest, 1.0f);
}
