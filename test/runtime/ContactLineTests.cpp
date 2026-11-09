// A contact line as content uses one: thrown by a projectile, anchored where the
// projectile met level geometry, striking characters once, cut by what stands
// across it, and drawn home when let go.

#include <movement/components/HeldMotion.h>
#include <set>

#include <gtest/gtest.h>

#include "../net/StubPrefabSpawner.h"

#include <abilities/AbilityCueLog.h>
#include <app/EngineSchedule.h>
#include <app/GameContexts.h>
#include <attributes/AttributeRegistry.h>
#include <attributes/AttributeSet.h>
#include <core/assets/AssetRef.h>
#include <core/config/EngineConfig.h>
#include <core/console/ConsoleRegistry.h>
#include <core/logging/LoggingProvider.h>
#include <ecs/StoragePartitionSet.h>
#include <ecs/World.h>
#include <effects/EffectRegistry.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <line/ContactLineSystem.h>
#include <physics/CharacterMoverPool.h>
#include <physics/PhysicsWorld.h>
#include <physics/RigidBodyBinding.h>
#include <physics/components/CharacterController.h>
#include <projectile/ProjectileSystem.h>
#include <runtime/RuntimeFrameLoop.h>
#include <time/SimClock.h>
#include <world/ComponentRegistrar.h>
#include <world/RuntimeComponentSchema.h>
#include <world/SimulationAuthority.h>
#include <world/transform/TransformComponents.h>

#include <optional>
#include <vector>

namespace
{
constexpr double kDt = 1.0 / 60.0;
constexpr float kTipDamage = 36.0f;
constexpr float kLineDamage = 9.0f;
const AssetId kLinePrefab{ 0x11E00001ull };
const AssetId kShotPrefab{ 0x11E00002ull };

class ContactLines : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ComponentRegistrar registrar(WorldState);
        RegisterEngineComponents(registrar);
        Tags = &WorldState.AddResource<GameplayTagRegistry>();
        Ally = *Tags->RegisterTag("Test.Ally");
        CutCue = *Tags->RegisterTag("Cue.Test.Cut");
        Attributes = &WorldState.AddResource<AttributeRegistry>();
        Health = Attributes->RegisterAttribute("Health", 0.0f, 1000.0f, 100.0f);
        Effects = &WorldState.AddResource<EffectRegistry>();
        EffectDefinition tip{};
        tip.Modifiers = { { Health, ModifierOp::Add, -kTipDamage } };
        (void)Effects->Define("asset://effects/tip.sdata", tip);
        EffectDefinition line{};
        line.Modifiers = { { Health, ModifierOp::Add, -kLineDamage } };
        (void)Effects->Define("asset://effects/line.sdata", line);

        Logic.Add(StoragePartitionId::Default());
        Shots = &RegisterProjectileSystem(Schedule, WorldState,
                                          ProjectileSystem::Services{ .Physics = &Physics, .Prefabs = &Prefabs,
                                                                      .Console = &Console, .Logging = &Logging });
        Lines = &RegisterContactLineSystem(Schedule, WorldState,
                                           ContactLineSystem::Services{ .Physics = &Physics, .Prefabs = &Prefabs,
                                                                        .Projectiles = Shots });
        Schedule.After<ContactLineSystem, ProjectileSystem>();
        Schedule.Init();

        Shooter = WorldState.CreateEntity();
        WorldState.AddComponent<LocalTransform>(Shooter, LocalTransform{});
        WorldState.AddComponent<WorldTransform>(Shooter, WorldTransform{});
        WorldState.AddComponent<AbilityCueLog>(Shooter, AbilityCueLog{});

        Shape();
    }

    void TearDown() override { Schedule.Shutdown(); }

    void Shape(float radius = 0.12f, float retract = 40.0f, std::string_view ignores = {})
    {
        const float maxLength = MaxLength;
        const float leash = LeashSlack;
        const float rehit = RehitSeconds;
        const std::uint32_t slots = Slots;
        const AssetId linePrefab{ kLinePrefab.Value + ++Shapes * 2 };
        const AssetId shotPrefab{ kShotPrefab.Value + Shapes * 2 };
        Prefabs.Register(linePrefab, [=](World& world, StoragePartitionId partition) {
            ContactLine authored;
            authored.Radius = radius;
            authored.Effect = "asset://effects/line.sdata";
            authored.RetractSpeed = retract;
            authored.CutCue = "Cue.Test.Cut";
            authored.MaxLength = maxLength;
            authored.LeashSlack = leash;
            authored.RehitSeconds = rehit;
            authored.Slots = slots;
            authored.SlotSpacing = 0.5f;
            if (!ignores.empty())
                authored.Ignores = ignores;
            const EntityId root = world.CreateEntity(partition);
            world.AddComponent<LocalTransform>(root, LocalTransform{});
            world.AddComponent<WorldTransform>(root, WorldTransform{});
            world.AddComponent<ContactLine>(root, authored);
            return std::vector<EntityId>{ root };
        });
        Prefabs.Register(shotPrefab, [=](World& world, StoragePartitionId partition) {
            Projectile authored;
            authored.Speed = 40.0f;
            authored.Radius = 0.1f;
            authored.Lifetime = 0.6f;
            authored.HitEffect = "asset://effects/tip.sdata";
            if (!ignores.empty())
                authored.Ignores = ignores;
            const EntityId root = world.CreateEntity(partition);
            world.AddComponent<LocalTransform>(root, LocalTransform{});
            world.AddComponent<WorldTransform>(root, WorldTransform{});
            world.AddComponent<Projectile>(root, authored);
            return std::vector<EntityId>{ root };
        });
        LinePrefab = linePrefab;
        ShotPrefab = shotPrefab;
    }

    VerbAdmission Lease(LeaseStep step, const Vec3d& at = Vec3d::Zero())
    {
        AuthoredArguments arguments(5);
        arguments.Set(0, AuthoredValue::Entity(Shooter));
        arguments.Set(1, AuthoredValue::Asset(AssetRef{ AssetType::Scene, "asset://line.smap", LinePrefab }));
        arguments.Set(2, AuthoredValue::Asset(AssetRef{ AssetType::Scene, "asset://shot.smap", ShotPrefab }));
        arguments.Set(3, AuthoredValue::Vector({ { at.X, at.Y, at.Z }, 3 }));
        arguments.Set(4, AuthoredValue::Bool(Keep));
        VerbInvocation invocation;
        invocation.Owner = Holder;
        invocation.Step = step;
        invocation.Arguments = step == LeaseStep::Acquire ? &arguments : nullptr;
        invocation.Tick = TickIndex + 1;
        return Lines->Extend(invocation);
    }

    void Throw(const Vec3d& at) { ASSERT_EQ(Lease(LeaseStep::Acquire, at), VerbAdmission::Accepted); }
    void LetGo() { (void)Lease(LeaseStep::Release); }
    [[nodiscard]] bool Held() { return Lease(LeaseStep::Probe) == VerbAdmission::Accepted; }

    // Level geometry: a static body no entity owns.
    void Wall(const Vec3d& centre, const Vec3d& half)
    {
        BodyDesc body;
        body.Shape = CollisionShape::MakeBox(half);
        body.Position = centre;
        body.Motion = BodyMotion::Static;
        body.Layer = CollisionLayer::Static;
        (void)Physics.AddBody(body);
    }

    EntityId Character(const Vec3d& centre)
    {
        const EntityId entity = WorldState.CreateEntity();
        const Transform3f pose{ centre, Quatf::Identity(), Vec3d::One() };
        WorldState.AddComponent<LocalTransform>(entity, LocalTransform{ pose });
        WorldState.AddComponent<WorldTransform>(entity, WorldTransform{ pose });
        WorldState.AddComponent<CharacterController>(entity, CharacterController{ .Radius = 0.4f, .Height = 1.8f });
        WorldState.AddComponent<AttributeSet>(entity, AttributeSet{});
        (void)WorldState.TryGet<AttributeSet>(entity)->Add(Health, 100.0f);
        Movers.Reconcile(WorldState, Logic);
        return entity;
    }

    void Tick(int count = 1)
    {
        for (int i = 0; i < count; ++i)
        {
            ++TickIndex;
            FixedLogicContext fixed{ .Config = Config, .Runtime = Runtime,
                                     .Time = FixedSimTime{ .DeltaSeconds = kDt, .TickIndex = TickIndex },
                                     .Entities = WorldState, .Partitions = Logic };
            Schedule.RunFixedLogic(fixed);
        }
    }

    float HealthOf(EntityId entity) { return WorldState.TryGet<AttributeSet>(entity)->GetBase(Health); }

    std::size_t LinesOut()
    {
        std::size_t count = 0;
        std::as_const(WorldState).ForEachComponent<ContactLineState>(
            [&](EntityId, const ContactLineState&) { ++count; });
        return count;
    }

    [[nodiscard]] EntityId LineEntity()
    {
        EntityId found;
        std::as_const(WorldState).ForEachComponent<ContactLineState>([&](EntityId line, const ContactLineState&) { found = line; });
        return found;
    }

    std::size_t CutCues()
    {
        std::size_t count = 0;
        for (const AbilityCueEntry& entry : WorldState.TryGet<AbilityCueLog>(Shooter)->Entries)
            count += entry.Seq != 0 && entry.Cue == CutCue ? 1u : 0u;
        return count;
    }

    EngineConfig Config;
    RuntimeFrameLoop Runtime;
    ConsoleRegistry Console;
    LoggingProvider Logging;
    World WorldState;
    StoragePartitionSet Logic;
    EngineSchedule Schedule;
    PhysicsWorld Physics;
    CharacterMoverPool Movers{ Physics };
    StubPrefabSpawner Prefabs;
    GameplayTagRegistry* Tags = nullptr;
    AttributeRegistry* Attributes = nullptr;
    EffectRegistry* Effects = nullptr;
    AttributeId Health;
    GameplayTagId Ally;
    GameplayTagId CutCue;
    ProjectileSystem* Shots = nullptr;
    ContactLineSystem* Lines = nullptr;
    EntityId Shooter;
    LeaseOwner Holder{};
    AssetId LinePrefab;
    AssetId ShotPrefab;
    std::uint64_t Shapes = 0;
    std::uint64_t TickIndex = 0;
    float MaxLength = 0.0f;
    float LeashSlack = 0.0f;
    float RehitSeconds = 0.0f;
    std::uint32_t Slots = 0;
    bool Keep = false;
};
} // namespace

TEST_F(ContactLines, ItAnchorsWhereItsProjectileMeetsLevelGeometry)
{
    Holder = LeaseOwner{ .Entity = Shooter, .Key = 7 };
    Wall(Vec3d(10.5f, 0.0f, 0.0f), Vec3d(0.5f, 3.0f, 3.0f));
    Throw(Vec3d(10.0f, 0.0f, 0.0f));
    EXPECT_FALSE(Lines->AnchorOf(Shooter).has_value()) << "it is still in the air";
    EXPECT_TRUE(Held());
    Tick(20);
    const std::optional<Vec3d> anchor = Lines->AnchorOf(Shooter);
    ASSERT_TRUE(anchor.has_value());
    EXPECT_NEAR(anchor->X, 10.0f, 0.2f);
    EXPECT_TRUE(Held()) << "an anchored line is still out";
}

TEST_F(ContactLines, ATipStrikeLeavesTheLineNothingToStrike)
{
    Holder = LeaseOwner{ .Entity = Shooter, .Key = 7 };
    const EntityId target = Character(Vec3d(6.0f, 0.0f, 0.0f));
    Throw(Vec3d(10.0f, 0.0f, 0.0f));
    Tick(30);
    EXPECT_FLOAT_EQ(HealthOf(target), 100.0f - kTipDamage) << "the tip struck; the line must not as well";
    EXPECT_FALSE(Held()) << "it stopped short, so it comes home";
    EXPECT_FALSE(Lines->AnchorOf(Shooter).has_value());
}

TEST_F(ContactLines, TheLineBehindTheTipStrikesWhatTheTipMissed)
{
    Holder = LeaseOwner{ .Entity = Shooter, .Key = 7 };
    Shape(0.3f);
    Wall(Vec3d(14.5f, 0.0f, 0.0f), Vec3d(0.5f, 3.0f, 3.0f));
    // Beside the path: past the tip's reach, within the line's.
    const EntityId target = Character(Vec3d(6.0f, 0.0f, 0.6f));
    Throw(Vec3d(14.0f, 0.0f, 0.0f));
    Tick(40);
    EXPECT_FLOAT_EQ(HealthOf(target), 100.0f - kLineDamage);
}

TEST_F(ContactLines, ACharacterIsStruckOnceHoweverLongItStaysOnTheLine)
{
    Holder = LeaseOwner{ .Entity = Shooter, .Key = 7 };
    Wall(Vec3d(10.5f, 0.0f, 0.0f), Vec3d(0.5f, 3.0f, 3.0f));
    Throw(Vec3d(10.0f, 0.0f, 0.0f));
    Tick(20);
    const EntityId target = Character(Vec3d(5.0f, 0.0f, 0.0f));
    Tick(1);
    EXPECT_FLOAT_EQ(HealthOf(target), 100.0f - kLineDamage) << "walking into a taut line strikes at once";
    Tick(60);
    EXPECT_FLOAT_EQ(HealthOf(target), 100.0f - kLineDamage);
}

TEST_F(ContactLines, ALineLetGoStrikesNothingAndEndsAtHome)
{
    Holder = LeaseOwner{ .Entity = Shooter, .Key = 7 };
    Wall(Vec3d(10.5f, 0.0f, 0.0f), Vec3d(0.5f, 3.0f, 3.0f));
    Throw(Vec3d(10.0f, 0.0f, 0.0f));
    Tick(20);
    LetGo();
    EXPECT_FALSE(Lines->AnchorOf(Shooter).has_value());
    const EntityId target = Character(Vec3d(5.0f, 0.0f, 0.0f));
    Tick(10);
    EXPECT_FLOAT_EQ(HealthOf(target), 100.0f) << "a retracting line is slack";
    EXPECT_EQ(LinesOut(), 1u);
    Tick(30);
    EXPECT_EQ(LinesOut(), 0u) << "it reached home";
}

TEST_F(ContactLines, SomethingSolidAcrossItCutsIt)
{
    Holder = LeaseOwner{ .Entity = Shooter, .Key = 7 };
    Wall(Vec3d(10.5f, 0.0f, 0.0f), Vec3d(0.5f, 3.0f, 3.0f));
    Throw(Vec3d(10.0f, 0.0f, 0.0f));
    Tick(20);
    const EntityId behind = Character(Vec3d(7.0f, 0.0f, 0.0f));
    Tick(1);
    const float struck = HealthOf(behind);
    Wall(Vec3d(5.0f, 0.0f, 0.0f), Vec3d(0.2f, 3.0f, 3.0f));
    Tick(1);
    EXPECT_EQ(LinesOut(), 1u) << "a moment's graze does not cut it";
    Tick(1);
    EXPECT_EQ(LinesOut(), 0u);
    EXPECT_EQ(CutCues(), 1u);
    EXPECT_FALSE(Held());
    EXPECT_FLOAT_EQ(HealthOf(behind), struck);
}

TEST_F(ContactLines, CuttingEndsItAtOnceAndItsInstigatorGoingEndsIt)
{
    Holder = LeaseOwner{ .Entity = Shooter, .Key = 7 };
    Wall(Vec3d(10.5f, 0.0f, 0.0f), Vec3d(0.5f, 3.0f, 3.0f));
    Throw(Vec3d(10.0f, 0.0f, 0.0f));
    Tick(20);
    AuthoredArguments arguments(1);
    arguments.Set(0, AuthoredValue::Entity(Shooter));
    VerbInvocation cut;
    cut.Arguments = &arguments;
    cut.Tick = TickIndex + 1;
    EXPECT_EQ(Lines->Cut(cut), VerbAdmission::Accepted);
    EXPECT_EQ(LinesOut(), 0u);
    EXPECT_EQ(CutCues(), 1u);

    Throw(Vec3d(10.0f, 0.0f, 0.0f));
    Tick(20);
    EXPECT_EQ(LinesOut(), 1u);
    WorldState.DestroyEntity(Shooter);
    Tick(1);
    EXPECT_EQ(LinesOut(), 0u);
}

TEST_F(ContactLines, AnIgnoredCharacterIsNeitherStruckNorInTheWay)
{
    Holder = LeaseOwner{ .Entity = Shooter, .Key = 7 };
    Shape(0.12f, 40.0f, "Test.Ally");
    Wall(Vec3d(10.5f, 0.0f, 0.0f), Vec3d(0.5f, 3.0f, 3.0f));
    const EntityId friendly = Character(Vec3d(5.0f, 0.0f, 0.0f));
    WorldState.AddComponent<GameplayTagContainer>(friendly, GameplayTagContainer{});
    (void)WorldState.TryGet<GameplayTagContainer>(friendly)->Grant(Ally);
    Throw(Vec3d(10.0f, 0.0f, 0.0f));
    Tick(20);
    EXPECT_FLOAT_EQ(HealthOf(friendly), 100.0f);
    EXPECT_TRUE(Lines->AnchorOf(Shooter).has_value()) << "the shot flew through it";
    Tick(20);
    EXPECT_FLOAT_EQ(HealthOf(friendly), 100.0f);
}

TEST_F(ContactLines, AThrowEndsAtItsMaxLength)
{
    Holder = LeaseOwner{ .Entity = Shooter, .Key = 7 };
    MaxLength = 8.0f;
    Shape();
    Wall(Vec3d(14.5f, 0.0f, 0.0f), Vec3d(0.5f, 3.0f, 3.0f));
    Throw(Vec3d(14.0f, 0.0f, 0.0f));
    float farthest = 0.0f;
    for (int tick = 0; tick < 30; ++tick)
    {
        Tick();
        std::as_const(WorldState).ForEachComponent<ContactLineState>(
            [&](EntityId, const ContactLineState& line) { farthest = std::max(farthest, line.Tip.X); });
    }
    EXPECT_NEAR(farthest, 8.0f, 0.7f);
    EXPECT_FALSE(Lines->AnchorOf(Shooter).has_value()) << "short of the wall, it comes home";
    EXPECT_FALSE(Held());
}

TEST_F(ContactLines, AKeptLineStaysWhenLetGoAndCanBeDrawnHome)
{
    Holder = LeaseOwner{ .Entity = Shooter, .Key = 7 };
    Keep = true;
    Wall(Vec3d(10.5f, 0.0f, 0.0f), Vec3d(0.5f, 3.0f, 3.0f));
    Throw(Vec3d(10.0f, 0.0f, 0.0f));
    Tick(20);
    LetGo();
    Tick(5);
    EXPECT_EQ(LinesOut(), 1u) << "it stays where it stuck";
    EXPECT_FALSE(Lines->AnchorOf(Shooter).has_value()) << "but nobody holds it";
    EXPECT_EQ(WorldState.TryGet<ContactLineState>(LineEntity())->Stage, ContactLineStage::Anchored);
    RetractKeptContactLines(WorldState, Shooter);
    EXPECT_EQ(WorldState.TryGet<ContactLineState>(LineEntity())->Stage, ContactLineStage::Retracting);
    Tick(60);
    EXPECT_EQ(LinesOut(), 0u);
}

TEST_F(ContactLines, AThrowLetGoInFlightStillLandsWithItsChain)
{
    Holder = LeaseOwner{ .Entity = Shooter, .Key = 7 };
    const EntityId target = Character(Vec3d(10.0f, 0.0f, 0.0f));
    Throw(Vec3d(10.0f, 0.0f, 0.0f));
    Tick(3);
    LetGo();
    Tick(1);
    EXPECT_EQ(WorldState.TryGet<ContactLineState>(LineEntity())->Stage, ContactLineStage::Extending)
        << "a throw is committed";
    Tick(30);
    EXPECT_FLOAT_EQ(HealthOf(target), 100.0f - kTipDamage);
    Tick(90);
    EXPECT_EQ(LinesOut(), 0u);
}

TEST_F(ContactLines, ThrowLetGoInFlightStaysIfKeptAndComesHomeIfNot)
{
    Holder = LeaseOwner{ .Entity = Shooter, .Key = 7 };
    Keep = true;
    Wall(Vec3d(10.5f, 0.0f, 0.0f), Vec3d(0.5f, 3.0f, 3.0f));
    Throw(Vec3d(10.0f, 0.0f, 0.0f));
    Tick(3);
    LetGo();
    Tick(30);
    ASSERT_EQ(LinesOut(), 1u);
    EXPECT_EQ(WorldState.TryGet<ContactLineState>(LineEntity())->Stage, ContactLineStage::Anchored);
    RetractKeptContactLines(WorldState, Shooter);
    Tick(60);
    Keep = false;
    Throw(Vec3d(10.0f, 0.0f, 0.0f));
    Tick(3);
    LetGo();
    Tick(90);
    EXPECT_EQ(LinesOut(), 0u) << "nothing holds it and it was not meant to stay";
}

TEST_F(ContactLines, CuttingAThrowEndsItsTipToo)
{
    Holder = LeaseOwner{ .Entity = Shooter, .Key = 7 };
    const EntityId target = Character(Vec3d(10.0f, 0.0f, 0.0f));
    Throw(Vec3d(10.0f, 0.0f, 0.0f));
    Tick(3);
    AuthoredArguments arguments(1);
    arguments.Set(0, AuthoredValue::Entity(Shooter));
    VerbInvocation cut;
    cut.Arguments = &arguments;
    cut.Tick = TickIndex + 1;
    EXPECT_EQ(Lines->Cut(cut), VerbAdmission::Accepted);
    Tick(30);
    EXPECT_FLOAT_EQ(HealthOf(target), 100.0f) << "no tip goes on without its line";
}

TEST_F(ContactLines, AKeptLineLeashesItsInstigator)
{
    Holder = LeaseOwner{ .Entity = Shooter, .Key = 7 };
    Keep = true;
    LeashSlack = 2.0f;
    Shape();
    WorldState.AddComponent<TetherSession>(Shooter, TetherSession{});
    Wall(Vec3d(10.5f, 0.0f, 0.0f), Vec3d(0.5f, 3.0f, 3.0f));
    Throw(Vec3d(10.0f, 0.0f, 0.0f));
    Tick(20);
    LetGo();
    Tick(2);
    const TetherSession* leash = WorldState.TryGet<TetherSession>(Shooter);
    ASSERT_TRUE(leash->Owner.IsSet());
    EXPECT_NEAR(leash->Length, 12.0f, 0.3f) << "where it stood when the line anchored, plus the slack";
    EXPECT_TRUE(leash->Yields) << "a reel the character starts takes the slot from its leash";
    RetractKeptContactLines(WorldState, Shooter);
    Tick(1);
    EXPECT_FALSE(WorldState.TryGet<TetherSession>(Shooter)->Owner.IsSet()) << "a line drawn home holds nobody";
}

TEST_F(ContactLines, ALineLeftStuckCanBeFoundAndMeasuredBack)
{
    Holder = LeaseOwner{ .Entity = Shooter, .Key = 7 };
    Keep = true;
    Shape();
    Wall(Vec3d(10.5f, 0.0f, 0.0f), Vec3d(0.5f, 3.0f, 3.0f));
    Throw(Vec3d(10.0f, 0.0f, 0.0f));
    Tick(20);
    EXPECT_FALSE(Lines->StuckPoint(Shooter, 0.0f).has_value()) << "something is still holding it";
    LetGo();
    Tick(1);
    const std::optional<Vec3d> tip = Lines->StuckPoint(Shooter, 0.0f);
    const std::optional<Vec3d> shortOf = Lines->StuckPoint(Shooter, 2.0f);
    ASSERT_TRUE(tip.has_value());
    ASSERT_TRUE(shortOf.has_value());
    EXPECT_NEAR((*tip - *shortOf).Magnitude(), 2.0f, 0.01f);
    EXPECT_LT(shortOf->X, tip->X) << "back toward the one who threw it";
    RetractKeptContactLines(WorldState, Shooter);
    EXPECT_FALSE(Lines->StuckPoint(Shooter, 0.0f).has_value());
}

TEST_F(ContactLines, ACharacterOffTheLineLongEnoughIsStruckAgain)
{
    Holder = LeaseOwner{ .Entity = Shooter, .Key = 7 };
    RehitSeconds = 1.0f;
    Shape();
    Wall(Vec3d(10.5f, 0.0f, 0.0f), Vec3d(0.5f, 3.0f, 3.0f));
    Throw(Vec3d(10.0f, 0.0f, 0.0f));
    Tick(20);
    const EntityId target = Character(Vec3d(5.0f, 0.0f, 0.0f));
    Tick(90);
    EXPECT_FLOAT_EQ(HealthOf(target), 100.0f - kLineDamage) << "standing on it is one strike";
    (void)Movers.SetPosition(WorldState, target, Vec3d(5.0f, 0.0f, 3.0f));
    Tick(70);
    (void)Movers.SetPosition(WorldState, target, Vec3d(5.0f, 0.0f, 0.0f));
    Tick(2);
    EXPECT_FLOAT_EQ(HealthOf(target), 100.0f - 2.0f * kLineDamage);
}

TEST_F(ContactLines, TwoSlotsThrowTwoAndAThirdTakesTheOldestKeptLine)
{
    Slots = 2;
    Keep = true;
    Shape();
    Wall(Vec3d(10.5f, 0.0f, 0.0f), Vec3d(0.5f, 8.0f, 8.0f));
    Holder = LeaseOwner{ .Entity = Shooter, .Key = 1 };
    Throw(Vec3d(10.0f, 0.0f, 0.0f));
    Tick(20);
    LetGo();
    Tick(20);
    Holder = LeaseOwner{ .Entity = Shooter, .Key = 2 };
    Throw(Vec3d(10.0f, 0.0f, 3.0f));
    Tick(20);
    LetGo();
    std::set<int> slots;
    std::as_const(WorldState).ForEachComponent<ContactLineState>(
        [&](EntityId, const ContactLineState& line) { slots.insert(line.Slot); });
    EXPECT_EQ(slots.size(), 2u) << "a hand each";
    Holder = LeaseOwner{ .Entity = Shooter, .Key = 3 };
    Throw(Vec3d(10.0f, 0.0f, -3.0f));
    std::size_t home = 0;
    std::as_const(WorldState).ForEachComponent<ContactLineState>(
        [&](EntityId, const ContactLineState& line) { home += line.Stage == ContactLineStage::Retracting ? 1u : 0u; });
    EXPECT_EQ(home, 1u) << "the oldest comes home to make room";
}
