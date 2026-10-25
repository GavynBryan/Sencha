// Projectiles as content uses them: a prefab carrying Projectile is launched,
// flown by the authority against bodies and character capsules, and what it meets
// is struck once -- its effects, its projectile.hit delivery and its cue -- or
// passed through when its tag allows, or splashed. A machine that is not the
// authority never flies or strikes; it only draws a mirrored projectile while
// the tick it is presenting lies between the projectile's launch and its end.

#include <gtest/gtest.h>

#include "../net/StubPrefabSpawner.h"

#include <abilities/AbilityCueLog.h>
#include <app/EngineSchedule.h>
#include <app/GameContexts.h>
#include <attributes/AttributeRegistry.h>
#include <attributes/AttributeSet.h>
#include <authored/AuthoredQueryDispatcher.h>
#include <authored/VerbDispatcher.h>
#include <authored/WorldVocabulary.h>
#include <core/config/EngineConfig.h>
#include <core/console/ConsoleRegistry.h>
#include <core/json/JsonParser.h>
#include <core/logging/LoggingProvider.h>
#include <ecs/StoragePartitionSet.h>
#include <ecs/World.h>
#include <effects/EffectRegistry.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <logic/ResponseSet.h>
#include <logic/ResponseVerbs.h>
#include <logic/Responses.h>
#include <logic/ResponsesSystem.h>
#include <net/NetReplicationComponents.h>
#include <net/NetSpawnPrefab.h>
#include <net/NetTickEstimator.h>
#include <net/ReplicationInterpolation.h>
#include <physics/PhysicsWorld.h>
#include <physics/RigidBodyBinding.h>
#include <physics/components/CharacterController.h>
#include <projectile/Projectile.h>
#include <projectile/ProjectileState.h>
#include <projectile/ProjectileSystem.h>
#include <projectile/ProjectileVerbs.h>
#include <render/StaticMeshComponent.h>
#include <runtime/RuntimeFrameLoop.h>
#include <time/SimClock.h>
#include <world/ComponentRegistrar.h>
#include <world/RuntimeComponentSchema.h>
#include <world/SimulationAuthority.h>
#include <world/transform/TransformComponents.h>
#include <world/transform/TransformHistory.h>

#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace
{
constexpr double kDt = 0.1;

// Records what a response ran: the projectile.hit's kind reached a verb.
struct Recorded
{
    EntityId Target;
};

class RecordingOperation
{
public:
    explicit RecordingOperation(std::vector<Recorded>& calls)
        : Calls(calls)
    {
    }
    VerbAdmission Invoke(const VerbInvocation& invocation)
    {
        Recorded call;
        (void)invocation.Arguments->TryGetEntity(0, call.Target);
        Calls.push_back(call);
        return VerbAdmission::Accepted;
    }

private:
    std::vector<Recorded>& Calls;
};

class ProjectileFixture : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ComponentRegistrar registrar(WorldState);
        RegisterEngineComponents(registrar);

        Tags = &WorldState.AddResource<GameplayTagRegistry>();
        Penetrable = *Tags->RegisterTag("Test.Penetrable");
        Charged = *Tags->RegisterTag("Test.Shot.Charged");
        Impact = *Tags->RegisterTag("Cue.Test.Impact");
        Attributes = &WorldState.AddResource<AttributeRegistry>();
        Health = Attributes->RegisterAttribute("Health", 0.0f, 1000.0f, 100.0f);
        Effects = &WorldState.AddResource<EffectRegistry>();
        EffectDefinition damage{};
        damage.Modifiers = { { Health, ModifierOp::Add, -10.0f } };
        (void)Effects->Define("asset://effects/damage.sdata", damage);
        EffectDefinition boost{};
        boost.Modifiers = { { Health, ModifierOp::Add, -5.0f } };
        (void)Effects->Define("asset://effects/boost.sdata", boost);
        EffectDefinition burn{};
        burn.Modifiers = { { Health, ModifierOp::Add, -3.0f } };
        (void)Effects->Define("asset://effects/burn.sdata", burn);

        InstallAuthoredVocabulary(WorldState);
        Verbs = FindVerbRegistry(WorldState);
        {
            VerbRegistrationScope scope(*Verbs, "test");
            AuthoredEventRegistrationScope events(*FindAuthoredEventRegistry(WorldState), "test");
            VerbDefinition record;
            record.Name = "test.record";
            record.Arguments = EmptyVerbArguments();
            record.Arguments.Children.push_back(MakeDataField(DataFieldKind::Entity, "target", "Target"));
            (void)scope.Declare(std::move(record));
            DeclareResponseVocabulary(scope, events);
            DeclareProjectileVocabulary(scope, events);
            ASSERT_TRUE(CommitTogether(scope, events));
        }
        Dispatcher = std::make_unique<VerbDispatcher>(*Verbs);
        Queries = std::make_unique<AuthoredQueryDispatcher>(*FindAuthoredQueryRegistry(WorldState));
        Record = std::make_unique<RecordingOperation>(Calls);
        Tokens.push_back(Dispatcher->Bind(Verbs->Find("test.record"), *Record));

        Logic.Add(StoragePartitionId::Default());
        Responses = &RegisterResponsesSystem(Schedule, WorldState, Loop, *Dispatcher, Queries.get(), nullptr,
                                             nullptr, Console, Logging);
        System = &RegisterProjectileSystem(Schedule, WorldState,
                                           ProjectileSystem::Services{ .Physics = &Physics,
                                                                       .Prefabs = &Prefabs,
                                                                       .Responses = Responses,
                                                                       .Clock = &Clock,
                                                                       .Interpolation = &Interpolation,
                                                                       .Console = &Console,
                                                                       .Logging = &Logging });
        Schedule.After<ProjectileSystem, ResponsesSystem>();
        Tokens.push_back(Dispatcher->Bind<&InvokeProjectileLaunch>(Verbs->Find(kProjectileLaunchVerb), *System));
        Schedule.Init();

        Shooter = WorldState.CreateEntity();
        WorldState.AddComponent<LocalTransform>(Shooter, LocalTransform{});
        WorldState.AddComponent<WorldTransform>(Shooter, WorldTransform{});
        WorldState.AddComponent<AbilityCueLog>(Shooter, AbilityCueLog{});
        Bolt();
    }

    void TearDown() override
    {
        Schedule.Shutdown();
        for (VerbBindingToken& token : Tokens)
            token.Reset();
    }

    // The prefab the test launches, built the way a scene prefab is: a root with
    // a pose, a mesh and the projectile. Each shape is a prefab of its own, as two
    // differently authored projectiles are.
    void Bolt(std::function<void(Projectile&)> shape = {})
    {
        Prefab = AssetId{ 0xB0170000ull + ++Shapes };
        Projectile projectile;
        projectile.Speed = 10.0f;
        projectile.Radius = 0.1f;
        projectile.Lifetime = 2.0f;
        projectile.HitEffect = "asset://effects/damage.sdata";
        projectile.ImpactCue = "Cue.Test.Impact";
        projectile.HitKind = "Test.Shot.Charged";
        if (shape)
            shape(projectile);
        Prefabs.Register(Prefab, [projectile](World& world, StoragePartitionId partition) {
            const EntityId root = world.CreateEntity(partition);
            world.AddComponent<LocalTransform>(root, LocalTransform{});
            world.AddComponent<WorldTransform>(root, WorldTransform{});
            world.AddComponent<StaticMeshComponent>(root, StaticMeshComponent{});
            world.AddComponent<Projectile>(root, projectile);
            return std::vector<EntityId>{ root };
        });
    }

    // A static box with an entity of its own, which can carry attributes and tags.
    EntityId Box(const Vec3d& centre, const Vec3d& half, bool damageable = true)
    {
        const EntityId entity = WorldState.CreateEntity();
        WorldState.AddComponent<LocalTransform>(entity, LocalTransform{ Transform3f{ centre, Quatf::Identity(),
                                                                                     Vec3d::One() } });
        WorldState.AddComponent<WorldTransform>(entity, WorldTransform{ Transform3f{ centre, Quatf::Identity(),
                                                                                     Vec3d::One() } });
        if (damageable)
        {
            WorldState.AddComponent<AttributeSet>(entity, AttributeSet{});
            (void)WorldState.TryGet<AttributeSet>(entity)->Add(Health, 100.0f);
        }
        BodyDesc body;
        body.Shape = CollisionShape::MakeBox(half);
        body.Position = centre;
        body.Motion = BodyMotion::Static;
        body.Layer = CollisionLayer::Static;
        body.UserData = PackEntity(entity);
        (void)Physics.AddBody(body);
        return entity;
    }

    // A walking character: a capsule with no body for any query to find.
    EntityId Character(const Vec3d& centre)
    {
        const EntityId entity = WorldState.CreateEntity();
        WorldState.AddComponent<LocalTransform>(entity, LocalTransform{ Transform3f{ centre, Quatf::Identity(),
                                                                                     Vec3d::One() } });
        WorldState.AddComponent<WorldTransform>(entity, WorldTransform{ Transform3f{ centre, Quatf::Identity(),
                                                                                     Vec3d::One() } });
        WorldState.AddComponent<CharacterController>(entity, CharacterController{ .Radius = 0.4f, .Height = 1.8f });
        WorldState.AddComponent<AttributeSet>(entity, AttributeSet{});
        (void)WorldState.TryGet<AttributeSet>(entity)->Add(Health, 100.0f);
        return entity;
    }

    void Tag(EntityId entity, GameplayTagId tag)
    {
        if (!WorldState.HasComponent<GameplayTagContainer>(entity))
            WorldState.AddComponent<GameplayTagContainer>(entity, GameplayTagContainer{});
        (void)WorldState.TryGet<GameplayTagContainer>(entity)->Grant(tag);
    }

    EntityId Launch(Vec3d direction = Vec3d(1.0f, 0.0f, 0.0f), float magnitude = 0.0f,
                    ProjectileLaunchResult* result = nullptr)
    {
        return System->Launch(ProjectileLaunch{ .Prefab = Prefab,
                                                .Instigator = Shooter,
                                                .Origin = Vec3d::Zero(),
                                                .Direction = direction,
                                                .Tick = TickIndex + 1,
                                                .Magnitude = magnitude },
                              result);
    }

    void Tick()
    {
        ++TickIndex;
        FixedLogicContext fixed{
            .Config = Config,
            .Runtime = Runtime,
            .Time = FixedSimTime{ .DeltaSeconds = kDt, .TickIndex = TickIndex },
            .Entities = WorldState,
            .Partitions = Logic,
        };
        Schedule.RunFixedLogic(fixed);
    }

    void Ticks(int count)
    {
        for (int i = 0; i < count; ++i)
            Tick();
    }

    Vec3d Position(EntityId entity) { return WorldState.TryGet<LocalTransform>(entity)->Value.Position; }
    float HealthOf(EntityId entity) { return WorldState.TryGet<AttributeSet>(entity)->GetBase(Health); }
    const ProjectileFlight& Flight(EntityId entity) { return *WorldState.TryGet<ProjectileFlight>(entity); }
    bool Flying(EntityId entity) { return WorldState.HasComponent<ProjectileMotion>(entity); }
    bool Drawn(EntityId entity) { return WorldState.TryGet<StaticMeshComponent>(entity)->Visible; }

    std::size_t Cues(GameplayTagId cue)
    {
        const AbilityCueLog& log = *WorldState.TryGet<AbilityCueLog>(Shooter);
        std::size_t count = 0;
        for (const AbilityCueEntry& entry : log.Entries)
            count += entry.Seq != 0 && entry.Cue == cue ? 1u : 0u;
        return count;
    }

    EngineConfig Config;
    RuntimeFrameLoop Runtime;
    ConsoleRegistry Console;
    LoggingProvider Logging;
    World WorldState;
    StoragePartitionSet Logic;
    EngineSchedule Schedule;
    FixedSimulationLoop Loop{ kDt };
    PhysicsWorld Physics;
    StubPrefabSpawner Prefabs;
    NetTickEstimator Clock;
    ReplicationInterpolation Interpolation;
    GameplayTagRegistry* Tags = nullptr;
    AttributeRegistry* Attributes = nullptr;
    EffectRegistry* Effects = nullptr;
    AttributeId Health;
    GameplayTagId Penetrable;
    GameplayTagId Charged;
    GameplayTagId Impact;
    VerbRegistry* Verbs = nullptr;
    std::unique_ptr<VerbDispatcher> Dispatcher;
    std::unique_ptr<AuthoredQueryDispatcher> Queries;
    std::vector<Recorded> Calls;
    std::unique_ptr<RecordingOperation> Record;
    std::vector<VerbBindingToken> Tokens;
    ResponsesSystem* Responses = nullptr;
    ProjectileSystem* System = nullptr;
    EntityId Shooter;
    AssetId Prefab;
    std::uint64_t Shapes = 0;
    std::uint64_t TickIndex = 0;
};
} // namespace

TEST_F(ProjectileFixture, AProjectileFliesAtItsSpeedFromTheTickItIsLaunchedAndEndsWhenItsTimeIsUp)
{
    Bolt([](Projectile& p) { p.Lifetime = 0.35f; });
    const EntityId bolt = Launch();
    ASSERT_TRUE(bolt.IsValid());
    EXPECT_TRUE(WorldState.HasComponent<NetReplicated>(bolt)) << "every machine is to draw it";
    EXPECT_EQ(WorldState.TryGet<NetSpawnPrefab>(bolt)->Scene, Prefab);
    EXPECT_TRUE(WorldState.HasComponent<WorldTransformHistory>(bolt)) << "drawn between ticks";
    EXPECT_EQ(ProjectilesInFlight(WorldState), 1u);

    Tick();
    EXPECT_NEAR(Position(bolt).X, 1.0f, 1e-4f) << "it flew on the tick it was launched";
    EXPECT_EQ(Flight(bolt).LaunchTick, 1u);
    Ticks(2);
    EXPECT_NEAR(Position(bolt).X, 3.0f, 1e-4f);
    EXPECT_TRUE(Flying(bolt));
    Tick();
    EXPECT_NEAR(Position(bolt).X, 3.5f, 1e-4f) << "the last tick flies only the time it had left";
    EXPECT_FALSE(Flying(bolt));
    EXPECT_EQ(Flight(bolt).EndTick, 4u);
    EXPECT_FALSE(Drawn(bolt)) << "an ended projectile is hidden";
    EXPECT_EQ(ProjectilesInFlight(WorldState), 0u) << "an ended projectile gives its place back";
    EXPECT_EQ(Cues(Impact), 1u) << "where it ended is cued, struck or not";

    System->SetLingerTicks(2);
    Ticks(1);
    EXPECT_TRUE(WorldState.IsAlive(bolt)) << "kept long enough for its end to reach every peer";
    Ticks(1);
    EXPECT_FALSE(WorldState.IsAlive(bolt));
}

TEST_F(ProjectileFixture, AProjectileWithGravityFallsAlongAnArc)
{
    Bolt([](Projectile& p) { p.GravityScale = 1.0f; });
    const EntityId bolt = Launch();
    Ticks(10);
    EXPECT_NEAR(Position(bolt).X, 10.0f, 1e-3f);
    // One second of fall from rest: half of g, to a tick's integration.
    EXPECT_NEAR(Position(bolt).Y, -0.5f * 9.81f, 0.01f);
    const Quatf facing = WorldState.TryGet<LocalTransform>(bolt)->Value.Rotation;
    EXPECT_LT(facing.RotateVector(Vec3d::Forward()).Y, -0.5f) << "it faces the way it is falling";
}

TEST_F(ProjectileFixture, ItStrikesWhatItMeetsOnceAppliesItsEffectsAndStops)
{
    const EntityId wall = Box(Vec3d(3.0f, 0.0f, 0.0f), Vec3d(0.5f, 2.0f, 2.0f));
    const EntityId behind = Box(Vec3d(6.0f, 0.0f, 0.0f), Vec3d(0.5f, 2.0f, 2.0f));
    Bolt([](Projectile& p) { p.ScaledEffect = "asset://effects/boost.sdata"; });
    const EntityId bolt = Launch(Vec3d(1.0f, 0.0f, 0.0f), 2.0f);
    Ticks(5);

    EXPECT_FALSE(Flying(bolt));
    EXPECT_NEAR(Position(bolt).X, 2.4f, 0.02f) << "it stopped where its sphere met the wall";
    EXPECT_FLOAT_EQ(HealthOf(wall), 100.0f - 10.0f - 5.0f * 2.0f) << "its hit effect and its scaled effect";
    EXPECT_FLOAT_EQ(HealthOf(behind), 100.0f);
    EXPECT_EQ(Cues(Impact), 1u);
    EXPECT_EQ(System->Strikes(), 1u);
}

TEST_F(ProjectileFixture, ItNeverStrikesItsInstigator)
{
    BodyDesc body;
    body.Shape = CollisionShape::MakeBox(Vec3d(0.5f, 0.5f, 0.5f));
    body.Position = Vec3d(1.0f, 0.0f, 0.0f);
    body.Motion = BodyMotion::Static;
    body.Layer = CollisionLayer::Static;
    body.UserData = PackEntity(Shooter);
    (void)Physics.AddBody(body);
    const EntityId bolt = Launch();
    Ticks(3);
    EXPECT_TRUE(Flying(bolt));
    EXPECT_GT(Position(bolt).X, 2.5f);
}

TEST_F(ProjectileFixture, ItStrikesAWalkingCharacterThoughNoQueryCanSeeOne)
{
    const EntityId walker = Character(Vec3d(4.0f, 0.0f, 0.0f));
    const EntityId bolt = Launch();
    Ticks(6);
    EXPECT_FALSE(Flying(bolt));
    EXPECT_NEAR(Position(bolt).X, 4.0f - 0.4f - 0.1f, 0.02f) << "met at the capsule's surface";
    EXPECT_FLOAT_EQ(HealthOf(walker), 90.0f);
}

TEST_F(ProjectileFixture, ItPassesThroughWhatItsTagLetsItWhileItsHitsLast)
{
    Bolt([](Projectile& p) {
        p.PassesThrough = "Test.Penetrable";
        p.MaxHits = 2;
    });
    const EntityId first = Box(Vec3d(2.0f, 0.0f, 0.0f), Vec3d(0.2f, 1.0f, 1.0f));
    const EntityId second = Box(Vec3d(4.0f, 0.0f, 0.0f), Vec3d(0.2f, 1.0f, 1.0f));
    const EntityId third = Box(Vec3d(6.0f, 0.0f, 0.0f), Vec3d(0.2f, 1.0f, 1.0f));
    for (const EntityId target : { first, second, third })
        Tag(target, Penetrable);
    const EntityId bolt = Launch();
    Ticks(8);
    EXPECT_FLOAT_EQ(HealthOf(first), 90.0f);
    EXPECT_FLOAT_EQ(HealthOf(second), 90.0f);
    EXPECT_FLOAT_EQ(HealthOf(third), 100.0f) << "its second hit was its last";
    EXPECT_FALSE(Flying(bolt));
    EXPECT_NEAR(Position(bolt).X, 3.7f, 0.02f);
}

TEST_F(ProjectileFixture, AnythingWithoutThePassThroughTagStopsIt)
{
    Bolt([](Projectile& p) {
        p.PassesThrough = "Test.Penetrable";
        p.MaxHits = 4;
    });
    const EntityId wall = Box(Vec3d(2.0f, 0.0f, 0.0f), Vec3d(0.2f, 1.0f, 1.0f));
    const EntityId beyond = Box(Vec3d(4.0f, 0.0f, 0.0f), Vec3d(0.2f, 1.0f, 1.0f));
    Tag(beyond, Penetrable);
    const EntityId bolt = Launch();
    Ticks(6);
    EXPECT_FLOAT_EQ(HealthOf(wall), 90.0f);
    EXPECT_FLOAT_EQ(HealthOf(beyond), 100.0f);
    EXPECT_FALSE(Flying(bolt));
}

TEST_F(ProjectileFixture, ASplashReachesWhatItCanSeeButNeitherItsInstigatorNorWhatItStruck)
{
    Bolt([](Projectile& p) {
        p.SplashRadius = 3.0f;
        p.SplashEffect = "asset://effects/burn.sdata";
    });
    const EntityId wall = Box(Vec3d(3.0f, 0.0f, 0.0f), Vec3d(0.5f, 2.0f, 2.0f));
    const EntityId near = Character(Vec3d(2.0f, 0.0f, 1.5f));
    const EntityId hidden = Box(Vec3d(4.5f, 0.0f, 0.0f), Vec3d(0.3f, 0.3f, 0.3f));
    WorldState.TryGet<LocalTransform>(Shooter)->Value.Position = Vec3d(1.0f, 0.0f, 0.0f);
    WorldState.TryGet<WorldTransform>(Shooter)->Value.Position = Vec3d(1.0f, 0.0f, 0.0f);
    // A capsule well inside the splash, launching from inside itself.
    WorldState.AddComponent<CharacterController>(Shooter, CharacterController{ .Radius = 0.4f, .Height = 1.8f });
    WorldState.AddComponent<AttributeSet>(Shooter, AttributeSet{});
    (void)WorldState.TryGet<AttributeSet>(Shooter)->Add(Health, 100.0f);
    (void)Launch();
    Ticks(5);
    EXPECT_FLOAT_EQ(HealthOf(wall), 90.0f) << "struck once, and not splashed as well";
    EXPECT_FLOAT_EQ(HealthOf(near), 97.0f) << "within the splash and in sight";
    EXPECT_FLOAT_EQ(HealthOf(hidden), 100.0f) << "behind the wall";
    EXPECT_FLOAT_EQ(HealthOf(Shooter), 100.0f);
}

TEST_F(ProjectileFixture, AFusedProjectileSplashesWhereItsTimeRunsOut)
{
    Bolt([](Projectile& p) {
        p.Lifetime = 0.3f;
        p.SplashRadius = 1.0f;
        p.SplashOnExpire = true;
    });
    const EntityId walker = Character(Vec3d(3.0f, 0.0f, 1.2f));
    (void)Launch();
    Ticks(3);
    EXPECT_FLOAT_EQ(HealthOf(walker), 90.0f) << "the hit effect, as the splash names none";
}

TEST_F(ProjectileFixture, WhatItStrikesHearsProjectileHitWithItsKind)
{
    const EntityId wall = Box(Vec3d(3.0f, 0.0f, 0.0f), Vec3d(0.5f, 2.0f, 2.0f), false);
    WorldState.AddComponent<::Responses>(wall, ::Responses{});
    const std::optional<JsonValue> bindings = JsonParse(R"({"struck": {"on": "projectile.hit", "sequence": {
        "conditions": [{"tag": "kind", "tags": {"any": ["Test.Shot.Charged"]}}],
        "commands": [{"verb": "test.record", "arguments": {"target": {"port": "instigator"}}}]}}})");
    ASSERT_TRUE(bindings.has_value());
    ASSERT_TRUE(SetResponseBindings(WorldState, wall, *bindings));
    (void)Launch();
    Ticks(6);
    ASSERT_EQ(Calls.size(), 1u);
    EXPECT_EQ(Calls.front().Target, Shooter);
}

TEST_F(ProjectileFixture, ALaunchPastCapacityIsRefusedUntilOneEnds)
{
    Bolt([](Projectile& p) { p.Lifetime = 0.1f; });
    System->SetCapacity(2);
    ProjectileLaunchResult result{};
    (void)Launch(Vec3d(1.0f, 0.0f, 0.0f), 0.0f, &result);
    (void)Launch(Vec3d(1.0f, 0.0f, 0.0f), 0.0f, &result);
    EXPECT_EQ(result, ProjectileLaunchResult::Launched);
    EXPECT_FALSE(Launch(Vec3d(1.0f, 0.0f, 0.0f), 0.0f, &result).IsValid());
    EXPECT_EQ(result, ProjectileLaunchResult::Full);
    Tick();
    EXPECT_TRUE(Launch(Vec3d(1.0f, 0.0f, 0.0f), 0.0f, &result).IsValid()) << "an ended one is no longer in flight";
}

TEST_F(ProjectileFixture, APrefabWithoutAProjectileOrWithAnUnknownEffectDoesNotLaunch)
{
    ProjectileLaunchResult result{};
    EXPECT_FALSE(System->Launch(ProjectileLaunch{ .Prefab = AssetId{ 0x404 }, .Direction = Vec3d::Forward() },
                                &result)
                     .IsValid());
    EXPECT_EQ(result, ProjectileLaunchResult::Unresolved);

    Bolt([](Projectile& p) { p.HitEffect = "asset://effects/nothing.sdata"; });
    EXPECT_FALSE(Launch(Vec3d(1.0f, 0.0f, 0.0f), 0.0f, &result).IsValid());
    EXPECT_EQ(result, ProjectileLaunchResult::Unresolved);
    EXPECT_EQ(ProjectilesInFlight(WorldState), 0u);
}

TEST_F(ProjectileFixture, ScatterIsChosenByTheSeedAloneAndStaysInsideItsCone)
{
    auto direction = [&](std::uint64_t seed) {
        const EntityId bolt = System->Launch(ProjectileLaunch{ .Prefab = Prefab,
                                                               .Instigator = Shooter,
                                                               .Direction = Vec3d(1.0f, 0.0f, 0.0f),
                                                               .SpreadDegrees = 5.0f,
                                                               .Seed = seed });
        return WorldState.TryGet<ProjectileMotion>(bolt)->Velocity.Normalized();
    };
    const Vec3d a = direction(7);
    const Vec3d b = direction(7);
    const Vec3d c = direction(8);
    EXPECT_NEAR((a - b).Magnitude(), 0.0f, 1e-6f);
    EXPECT_GT((a - c).Magnitude(), 1e-4f);
    for (const Vec3d& d : { a, c })
        EXPECT_GE(d.X, std::cos(5.0f * 3.14159265f / 180.0f) - 1e-5f);
}

TEST_F(ProjectileFixture, AnInstigatorsSeedIsItsOwnWhateverElseLaunchesThatTick)
{
    const EntityId other = WorldState.CreateEntity();
    (void)System->NextSeed(other, 6);
    (void)System->NextSeed(other, 6);
    const std::uint64_t first = System->NextSeed(Shooter, 6);
    const std::uint64_t second = System->NextSeed(Shooter, 6);
    EXPECT_NE(first, second) << "two launches in one tick scatter apart";

    // The same tick on a machine where nobody else launched.
    ProjectileSystem alone(WorldState, ProjectileSystem::Services{});
    EXPECT_EQ(alone.NextSeed(Shooter, 6), first);
    EXPECT_EQ(alone.NextSeed(Shooter, 6), second);
    EXPECT_NE(alone.NextSeed(Shooter, 7), first) << "and the next tick's is its own";
}

TEST_F(ProjectileFixture, TheLaunchVerbRefusesWhatItCannotResolve)
{
    AuthoredArguments arguments(8);
    arguments.Set(0, AuthoredValue::Entity(Shooter));
    arguments.Set(1, AuthoredValue::String("asset://prefabs/missing.sscene"));
    arguments.Set(4, AuthoredValue::Float(0.0));
    arguments.Set(5, AuthoredValue::Float(1.0));
    arguments.Set(6, AuthoredValue::Float(0.0));
    arguments.Set(7, AuthoredValue::Float(1.0));
    VerbInvocation call;
    call.Arguments = &arguments;
    EXPECT_EQ(InvokeProjectileLaunch(*System, call), VerbAdmission::UnresolvedReference)
        << "a host with no asset system resolves no path";
    AuthoredArguments missing(1);
    missing.Set(0, AuthoredValue::Entity(Shooter));
    call.Arguments = &missing;
    EXPECT_EQ(InvokeProjectileLaunch(*System, call), VerbAdmission::InvalidArguments);
}

TEST_F(ProjectileFixture, AMachineThatIsNotTheAuthorityNeverLaunchesFliesOrStrikes)
{
    const EntityId wall = Box(Vec3d(1.0f, 0.0f, 0.0f), Vec3d(0.5f, 2.0f, 2.0f));
    const EntityId bolt = Launch();
    WorldState.AddResource<SimulationAuthority>(SimulationAuthority{ .Authoritative = false });
    ProjectileLaunchResult result{};
    EXPECT_FALSE(Launch(Vec3d(1.0f, 0.0f, 0.0f), 0.0f, &result).IsValid());
    EXPECT_EQ(result, ProjectileLaunchResult::Refused);
    Ticks(4);
    EXPECT_NEAR(Position(bolt).X, 0.0f, 1e-6f) << "a mirrored projectile moves only as the authority says";
    EXPECT_FLOAT_EQ(HealthOf(wall), 100.0f);
}

TEST_F(ProjectileFixture, AMirroredProjectileIsDrawnOnlyWhileThePresentedTickIsInItsFlight)
{
    const EntityId bolt = Launch();
    WorldState.AddResource<SimulationAuthority>(SimulationAuthority{ .Authoritative = false });
    // The authority's ticks are this machine's, with no flight: presented ticks
    // run the interpolation lag behind.
    Clock.Observe(0, 0, 0, kDt);
    WorldState.TryGet<ProjectileFlight>(bolt)->LaunchTick = 10;
    const std::uint64_t lag = Interpolation.PresentationLagTicks();

    while (TickIndex + 1 < 10 + lag)
    {
        Tick();
        EXPECT_FALSE(Drawn(bolt)) << "not before it was launched, at tick " << TickIndex;
    }
    Tick();
    EXPECT_TRUE(Drawn(bolt)) << "from its launch";
    WorldState.TryGet<ProjectileFlight>(bolt)->EndTick = 14;
    while (TickIndex + 1 <= 14 + lag)
    {
        Tick();
        EXPECT_TRUE(Drawn(bolt)) << "through its end, at tick " << TickIndex;
    }
    Tick();
    EXPECT_FALSE(Drawn(bolt)) << "and not after";
}
