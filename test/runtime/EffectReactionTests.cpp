// Reactions to applied effects: the rule a profile picks and what it does, the
// authored form, and the whole fixed tick pushing a struck character and shaking
// the view it owns.

#include <abilities/AbilityKit.h>
#include <app/EngineSchedule.h>
#include <app/GameContexts.h>
#include <assets/data/DataAssetCache.h>
#include <assets/data/DataAssetTypeRegistry.h>
#include <assets/runtime/RuntimeAssets.h>
#include <attributes/AttributeRegistry.h>
#include <attributes/AttributeSet.h>
#include <camera/CameraShake.h>
#include <camera/CameraShakeProfile.h>
#include <core/config/EngineConfig.h>
#include <core/json/JsonParser.h>
#include <core/logging/LoggingProvider.h>
#include <ecs/WorldComponentSchema.h>
#include <effects/EffectApplicationLog.h>
#include <effects/EffectLifetimeSystem.h>
#include <effects/EffectRegistry.h>
#include <effects/EffectSystem.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <gameplay_tags/GameplayTagSet.h>
#include <logic/EffectReactionProfile.h>
#include <logic/EffectReactionSystem.h>
#include <logic/EffectReactions.h>
#include <movement/LocomotionMode.h>
#include <movement/MovementDefs.h>
#include <movement/MovementIntent.h>
#include <movement/MovementRegistration.h>
#include <movement/components/CharacterMovement.h>
#include <physics/PhysicsRegistration.h>
#include <physics/PhysicsStepSystem.h>
#include <physics/components/CharacterController.h>
#include <runtime/RuntimeFrameLoop.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/RuntimeComponentSchema.h>
#include <world/RuntimeWorld.h>
#include <world/transform/TransformComponents.h>
#include <world/transform/TransformPropagation.h>

#include <gtest/gtest.h>

#include <array>
#include <memory>

namespace
{
constexpr std::string_view kShakePath = "asset://shakes/struck.sdata";

struct Rules
{
    GameplayTagRegistry Tags;
    AttributeRegistry Attributes;
    AttributeId Health = Attributes.RegisterAttribute("Health", 0.0f, 100.0f, 100.0f);
    GameplayTagId Claw = *Tags.RegisterTag("damage.claw");
    GameplayTagId Bullet = *Tags.RegisterTag("damage.bullet");
    GameplayTagId Dead = *Tags.RegisterTag("state.dead");
    std::vector<EffectReactionRule> Resolved;

    Rules()
    {
        EffectReactionProfile profile;
        EffectReactionDeclaration claw;
        claw.Kinds.Any = { "damage.claw" };
        claw.Attribute = "Health";
        claw.Threshold = 1.0f;
        claw.ImpulsePerUnit = 0.2f;
        claw.ImpulseMax = 4.0f;
        claw.Lift = 0.1f;
        claw.ShakeProfile = std::string(kShakePath);
        claw.ShakePerUnit = 0.05f;
        claw.ShakeMax = 1.0f;
        EffectReactionDeclaration bullet;
        bullet.Kinds.Any = { "damage.bullet" };
        bullet.Recipient.None = { "state.dead" };
        bullet.Attribute = "Health";
        bullet.ImpulsePerUnit = 0.05f;
        bullet.ImpulseMax = 1.5f;
        profile.Reactions = { claw, bullet };
        std::vector<std::string> errors;
        EXPECT_TRUE(ResolveEffectReactionProfile(profile, Attributes, Tags, Resolved, errors));
    }

    std::optional<EffectReactionOutcome> React(GameplayTagId kind, float before, float after,
                                               const EffectSource& source,
                                               const GameplayTagContainer* recipient = nullptr,
                                               std::optional<Vec3d> instigator = std::nullopt)
    {
        GameplayTagSet kinds;
        if (kind.IsValid())
            (void)kinds.AddExact(kind);
        const std::array<EffectAttributeChange, 1> changes{ EffectAttributeChange{ Health, before, after } };
        return ReactToEffect(Resolved,
                             EffectReactionCase{ .Kinds = &kinds,
                                                 .Recipient = recipient,
                                                 .Changes = changes,
                                                 .Source = source,
                                                 .TargetPosition = Vec3d::Zero(),
                                                 .InstigatorPosition = instigator },
                             Tags);
    }
};

EffectSource Along(Vec3d direction)
{
    return EffectSource{ .Direction = direction, .HasDirection = true };
}
} // namespace

TEST(EffectReactions, APushAndAShakeGrowWithTheChangeUpToTheirCaps)
{
    Rules rules;
    const std::optional<EffectReactionOutcome> scratch = rules.React(rules.Claw, 60.0f, 50.0f, Along(Vec3d(1, 0, 0)));
    ASSERT_TRUE(scratch);
    EXPECT_EQ(scratch->Rule, 0u);
    EXPECT_FLOAT_EQ(scratch->Units, 10.0f);
    EXPECT_NEAR(scratch->Impulse.X, 2.0f, 1e-5f);
    EXPECT_NEAR(scratch->Impulse.Y, 0.2f, 1e-5f) << "lift is a share of the push";
    EXPECT_FLOAT_EQ(scratch->ShakeScale, 0.5f);

    const std::optional<EffectReactionOutcome> maul = rules.React(rules.Claw, 60.0f, 0.0f, Along(Vec3d(1, 0, 0)));
    ASSERT_TRUE(maul);
    EXPECT_NEAR(maul->Impulse.X, 4.0f, 1e-5f);
    EXPECT_FLOAT_EQ(maul->ShakeScale, 1.0f);

    EXPECT_FALSE(rules.React(rules.Claw, 60.0f, 59.5f, Along(Vec3d(1, 0, 0)))) << "under the threshold";
}

TEST(EffectReactions, OnlyAMatchingKindAndTheRightWayOfChangeAreAnswered)
{
    Rules rules;
    EXPECT_FALSE(rules.React(GameplayTagId{}, 60.0f, 40.0f, Along(Vec3d(1, 0, 0)))) << "an effect of no kind";
    EXPECT_FALSE(rules.React(rules.Claw, 40.0f, 60.0f, Along(Vec3d(1, 0, 0)))) << "a gain to a rule for a loss";
    const std::optional<EffectReactionOutcome> shot = rules.React(rules.Bullet, 60.0f, 50.0f, Along(Vec3d(0, 0, 1)));
    ASSERT_TRUE(shot);
    EXPECT_EQ(shot->Rule, 1u);
    EXPECT_NEAR(shot->Impulse.Z, 0.5f, 1e-5f);
    EXPECT_FLOAT_EQ(shot->ShakeScale, 0.0f) << "a rule with no shake profile shakes nothing";

    GameplayTagContainer dead{};
    ASSERT_TRUE(dead.Grant(rules.Dead));
    EXPECT_FALSE(rules.React(rules.Bullet, 60.0f, 50.0f, Along(Vec3d(0, 0, 1)), &dead));
}

TEST(EffectReactions, ThePushRunsAlongTheStrikeElseFromItsPointElseFromTheInstigatorOnTheGround)
{
    Rules rules;
    const std::optional<EffectReactionOutcome> downward =
        rules.React(rules.Bullet, 60.0f, 50.0f, Along(Vec3d(0.0f, -3.0f, 1.0f)));
    ASSERT_TRUE(downward);
    EXPECT_NEAR(downward->Impulse.Z, 0.5f, 1e-5f) << "flattened onto the ground plane";
    EXPECT_NEAR(downward->Impulse.Y, 0.0f, 1e-5f);

    const EffectSource fromPoint{ .Point = Vec3d(-2.0f, 1.0f, 0.0f), .HasPoint = true };
    EXPECT_NEAR(rules.React(rules.Bullet, 60.0f, 50.0f, fromPoint)->Impulse.X, 0.5f, 1e-5f);

    const std::optional<EffectReactionOutcome> fromInstigator =
        rules.React(rules.Bullet, 60.0f, 50.0f, EffectSource{}, nullptr, Vec3d(0.0f, 0.0f, 3.0f));
    EXPECT_NEAR(fromInstigator->Impulse.Z, -0.5f, 1e-5f);

    const std::optional<EffectReactionOutcome> nowhere = rules.React(rules.Claw, 60.0f, 50.0f, EffectSource{});
    ASSERT_TRUE(nowhere) << "with no way to push it still shakes";
    EXPECT_FLOAT_EQ(nowhere->Impulse.Magnitude(), 0.0f);
    EXPECT_FLOAT_EQ(nowhere->ShakeScale, 0.5f);
}

TEST(EffectReactions, AFlinchPlaysForTheSideTheBlowCameFromAndTheFrontWithoutOne)
{
    GameplayTagRegistry tags;
    AttributeRegistry attributes;
    const AttributeId health = attributes.RegisterAttribute("Health", 0.0f, 100.0f, 100.0f);
    const GameplayTagId front = *tags.RegisterTag("Anim.Flinch.Front");
    const GameplayTagId back = *tags.RegisterTag("Anim.Flinch.Back");
    const GameplayTagId left = *tags.RegisterTag("Anim.Flinch.Left");
    const GameplayTagId right = *tags.RegisterTag("Anim.Flinch.Right");

    EffectReactionDeclaration flinch;
    flinch.Attribute = "Health";
    flinch.AnimationFront = "Anim.Flinch.Front";
    flinch.AnimationBack = "Anim.Flinch.Back";
    flinch.AnimationLeft = "Anim.Flinch.Left";
    flinch.AnimationRight = "Anim.Flinch.Right";
    flinch.AnimationTicks = 24;
    std::vector<EffectReactionRule> rules;
    std::vector<std::string> errors;
    ASSERT_TRUE(ResolveEffectReactionProfile(EffectReactionProfile{ { flinch } }, attributes, tags, rules, errors));

    const std::array<EffectAttributeChange, 1> changes{ EffectAttributeChange{ health, 60.0f, 50.0f } };
    GameplayTagSet noKinds;
    // The recipient faces -Z; a blow is named for where it came from, not where it goes.
    const auto intentOf = [&](const EffectSource& source, std::optional<Vec3d> forward = Vec3d::Forward()) {
        const std::optional<EffectReactionOutcome> outcome = ReactToEffect(
            rules, EffectReactionCase{ .Kinds = &noKinds, .Changes = changes, .Source = source,
                                       .InstigatorPosition = std::nullopt, .Forward = forward }, tags);
        EXPECT_TRUE(outcome);
        EXPECT_EQ(outcome->AnimationTicks, 24u);
        return outcome->AnimationIntent;
    };
    EXPECT_EQ(intentOf(Along(Vec3d(0, 0, 1))), front) << "travelling at its face";
    EXPECT_EQ(intentOf(Along(Vec3d(0, 0, -1))), back) << "travelling the way it faces";
    EXPECT_EQ(intentOf(Along(Vec3d(1, 0, 0))), left) << "travelling to its right, so from its left";
    EXPECT_EQ(intentOf(Along(Vec3d(-1, 0, 0))), right);
    EXPECT_EQ(intentOf(EffectSource{}), front) << "no direction";
    EXPECT_EQ(intentOf(Along(Vec3d(1, 0, 0)), std::nullopt), front) << "no facing";
}

TEST(EffectReactions, AProfileCompilesItsReactionsAndNamesItsShake)
{
    LoggingProvider logging;
    ComponentSerializerRegistry serializers;
    RuntimeAssets assets{ logging, serializers };
    const auto compile = [&](std::string_view data) {
        return assets.DataTypes.Find(kEffectReactionProfileType)->Compile(*JsonParse(data));
    };
    const DataAssetCompileResult compiled = compile(R"({ "reactions": [ {
        "kinds": { "any": ["damage.claw"] }, "attribute": "Health", "threshold": 1,
        "impulse": { "per_unit": 0.18, "max": 4 },
        "shake": { "profile": "asset://shakes/struck.sdata", "per_unit": 0.03, "max": 1 } },
        { "attribute": "Health", "change": "increase" } ] })");
    ASSERT_TRUE(compiled.IsValid()) << compiled.Error;
    const auto& profile = *static_cast<const EffectReactionProfile*>(compiled.Value.get());
    ASSERT_EQ(profile.Reactions.size(), 2u);
    EXPECT_FLOAT_EQ(profile.Reactions[0].ImpulsePerUnit, 0.18f);
    EXPECT_TRUE(profile.Reactions[0].Decrease);
    EXPECT_FALSE(profile.Reactions[1].Decrease);
    ASSERT_EQ(compiled.Dependencies.size(), 1u);
    EXPECT_EQ(compiled.Dependencies[0].Path, kShakePath);

    EXPECT_FALSE(compile(R"({})").IsValid());
    EXPECT_FALSE(compile(R"({ "reactions": [ { "threshold": 1 } ] })").IsValid()) << "no attribute";
    EXPECT_FALSE(compile(R"({ "reactions": [ { "attribute": "Health", "change": "sideways" } ] })").IsValid());
    EXPECT_FALSE(compile(R"({ "reactions": [ { "attribute": "Health", "impulse": { "max": -1 } } ] })").IsValid());
    EXPECT_FALSE(compile(R"({ "reactions": [ { "attribute": "Health", "animation": { "back": "A.B", "ticks": 4 } } ] })")
                     .IsValid()) << "an animation names its front";
    EXPECT_FALSE(compile(R"({ "reactions": [ { "attribute": "Health", "animation": { "front": "A.B" } } ] })").IsValid())
        << "an animation names its ticks";
    EXPECT_TRUE(compile(R"({ "reactions": [ { "attribute": "Health", "animation": { "front": "A.B", "ticks": 4 } } ] })")
                    .IsValid());
}

namespace
{
constexpr float kCapsuleCentre = 0.95f;
constexpr std::string_view kProfilePath = "asset://reactions/walker.sdata";

// The fixed tick a struck character goes through: effects, reactions,
// movement, the character mover.
struct StrikeHarness
{
    EngineConfig Config;
    RuntimeFrameLoop Loop;
    DataAssetCache DataAssets;
    LoggingProvider Logging;
    WorldComponentSchema Schema = MakeSchema();
    RuntimeWorld Runtime{ Schema };
    EngineSchedule Schedule;
    AttributeId Health;
    EffectId Claw;
    EffectId Mend;
    DataAssetHandle Profile;

    StrikeHarness()
    {
        World& world = Runtime.Entities();
        RegisterMovement(world);
        RegisterPhysics(Schedule);
        RegisterAbilityKitSystems(Schedule);
        RegisterMovementSystems(Schedule, DataAssets);
        Schedule.Register<EffectReactionSystem>(EffectReactionSystem::Services{ .DataAssets = &DataAssets });
        Schedule.After<EffectReactionSystem, EffectLifetimeSystem>();
        Schedule.Init();

        BodyDesc floor;
        floor.Shape = CollisionShape::MakeBox(Vec3d(50.0f, 0.5f, 50.0f));
        floor.Position = Vec3d(0.0f, -0.5f, 0.0f);
        floor.Layer = CollisionLayer::Static;
        (void)Schedule.Get<PhysicsStepSystem>()->GetSimulation().AddBody(floor);

        GameplayTagRegistry& tags = world.GetResource<GameplayTagRegistry>();
        const GameplayTagId claw = *tags.RegisterTag("damage.claw");
        Health = world.GetResource<AttributeRegistry>().RegisterAttribute("Health", 0.0f, 100.0f, 100.0f);
        EffectDefinition hurt{};
        hurt.Modifiers = { { Health, ModifierOp::Add, -18.0f } };
        (void)hurt.Kinds.AddExact(claw);
        EffectDefinition heal{};
        heal.Modifiers = { { Health, ModifierOp::Add, 18.0f } };
        Claw = world.GetResource<EffectRegistry>().Register("claw", hurt);
        Mend = world.GetResource<EffectRegistry>().Register("mend", heal);

        CameraShakeProfile shake;
        shake.Kick.Translation = Vec3d(0.0f, 0.05f, 0.0f);
        shake.Envelope = { .Attack = 0.0f, .Hold = 0.0f, .Release = 0.2f };
        (void)DataAssets.Register(kShakePath, std::string(kCameraShakeType), std::make_shared<CameraShakeProfile>(shake));
        EffectReactionProfile profile;
        EffectReactionDeclaration answer;
        answer.Kinds.Any = { "damage.claw" };
        answer.Attribute = "Health";
        answer.ImpulsePerUnit = 0.18f;
        answer.ImpulseMax = 4.0f;
        answer.ShakeProfile = std::string(kShakePath);
        answer.ShakePerUnit = 0.03f;
        answer.ShakeMax = 1.0f;
        profile.Reactions = { answer };
        Profile = DataAssets.Register(kProfilePath, std::string(kEffectReactionProfileType),
                                      std::make_shared<EffectReactionProfile>(profile));
    }

    ~StrikeHarness() { Schedule.Shutdown(); }

    static WorldComponentSchema MakeSchema()
    {
        WorldComponentSchema schema;
        RegisterEngineRuntimeComponents(schema);
        schema.Seal();
        return schema;
    }

    World& Entities() { return Runtime.Entities(); }

    EntityId Character()
    {
        World& world = Entities();
        const EntityId character = world.CreateEntity();
        Transform3f pose = Transform3f::Identity();
        pose.Position = Vec3d(0.0f, kCapsuleCentre, 0.0f);
        world.AddComponent<LocalTransform>(character, LocalTransform{ pose });
        world.AddComponent<WorldTransform>(character, WorldTransform{ pose });
        world.AddComponent<CharacterController>(character, CharacterController{});
        world.AddComponent<CharacterMovement>(
            character, CharacterMovement{ .Mode = world.GetResource<LocomotionModeRegistry>().FreeMode() });
        AttributeSet attributes;
        attributes.Add(world.GetResource<MovementDefs>().MoveSpeed, 6.0f);
        attributes.Add(Health, 100.0f);
        world.AddComponent<AttributeSet>(character, attributes);
        world.AddComponent<GameplayTagContainer>(character, GameplayTagContainer{});
        world.AddComponent<EffectReactions>(character, EffectReactions{ .Profile = Profile });
        return character;
    }

    void Strike(EntityId target, EffectId effect)
    {
        (void)ApplyEffect(Entities(), target, effect, FixedSimTime{ .DeltaSeconds = 1.0 / 60.0, .TickIndex = 0 }, {}, 1.0f,
                          EffectSource{ .Direction = Vec3d(0.0f, 0.0f, 1.0f), .HasDirection = true });
    }

    void Step(int ticks = 1)
    {
        for (int i = 0; i < ticks; ++i)
        {
            World& world = Entities();
            const FrameZoneView& zones = Runtime.BuildFrameView();
            const FixedSimTime time = Loop.GetSimulationClock().BeginFixedTick();
            FixedLogicContext logic{ .Config = Config, .Runtime = Loop, .Time = time, .Entities = world,
                                     .Partitions = zones.Logic };
            Schedule.RunFixedLogic(logic);
            PhysicsContext physics{ Config, Loop, time, world, zones.Physics };
            Schedule.RunPhysics(physics);
            PropagateTransforms(world, zones.Logic, TransformPropagationDomain::Simulation);
            Runtime.EndFrameView();
            Loop.GetSimulationClock().EndFixedTick();
        }
    }

    Vec3d Position(EntityId character) { return std::as_const(Entities()).TryGet<WorldTransform>(character)->Value.Position; }
};
} // namespace

TEST(EffectReactions, AStruckCharacterIsShovedAlongTheStrikeAndItsViewShakesOnce)
{
    StrikeHarness harness;
    const EntityId walker = harness.Character();
    harness.Step(30);
    const Vec3d before = harness.Position(walker);
    CameraShakeState& shakes = harness.Entities().GetResource<CameraShakeState>();
    const std::size_t shaking = shakes.Count();

    harness.Strike(walker, harness.Claw);
    harness.Step();
    EXPECT_EQ(shakes.Count(), shaking + 1);
    EXPECT_TRUE(harness.Entities().GetResource<EffectApplicationLog>().Applications().empty())
        << "the reactions read the log and cleared it";
    harness.Step(60);
    const Vec3d after = harness.Position(walker);
    EXPECT_GT(after.Z - before.Z, 0.2f) << "a claw for 18 shoves it back along the swing";
    EXPECT_LT(after.Z - before.Z, 1.5f);
    EXPECT_NEAR(after.X, before.X, 0.05f);

    harness.Strike(walker, harness.Mend);
    harness.Step(60);
    EXPECT_NEAR(harness.Position(walker).Z, after.Z, 0.02f) << "a gain of the same size moves nothing";
}

TEST(EffectReactions, AKindRegisteredAfterTheProfileBoundIsStillAnswered)
{
    StrikeHarness harness;
    EffectReactionProfile late;
    EffectReactionDeclaration answer;
    answer.Kinds.Any = { "damage.late" };
    answer.Attribute = "Health";
    answer.ShakeProfile = std::string(kShakePath);
    answer.ShakePerUnit = 0.03f;
    answer.ShakeMax = 1.0f;
    late.Reactions = { answer };
    const DataAssetHandle profile = harness.DataAssets.Register(
        "asset://reactions/late.sdata", std::string(kEffectReactionProfileType), std::make_shared<EffectReactionProfile>(late));
    const EntityId walker = harness.Character();
    harness.Entities().TryGet<EffectReactions>(walker)->Profile = profile;
    harness.Step(5);
    World& world = harness.Entities();
    CameraShakeState& shakes = world.GetResource<CameraShakeState>();
    const std::size_t calm = shakes.Count();
    harness.Strike(walker, harness.Claw);
    harness.Step();
    EXPECT_EQ(shakes.Count(), calm) << "a claw is not the kind this profile answers";

    const GameplayTagId kind = *world.GetResource<GameplayTagRegistry>().RegisterTag("damage.late");
    EffectDefinition hurt{};
    hurt.Modifiers = { { harness.Health, ModifierOp::Add, -18.0f } };
    (void)hurt.Kinds.AddExact(kind);
    const EffectId strike = world.GetResource<EffectRegistry>().Register("late", hurt);
    const std::size_t shaking = shakes.Count();
    harness.Strike(walker, strike);
    harness.Step();
    EXPECT_EQ(shakes.Count(), shaking + 1) << "the kind it names exists now, so the profile answers it";
}

