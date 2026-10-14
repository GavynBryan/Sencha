#include <attributes/AttributeRegistry.h>
#include <attributes/AttributeSet.h>
#include <effects/ActiveEffect.h>
#include <effects/EffectCost.h>
#include <effects/EffectRegistry.h>
#include <effects/EffectSystem.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <ecs/StoragePartitionSet.h>
#include <ecs/World.h>

#include <gtest/gtest.h>

#include <array>
#include <string>

namespace
{
// One-second ticks, so authored seconds read as tick counts.
FixedSimTime At(std::uint64_t tick)
{
    return FixedSimTime{ .DeltaSeconds = 1.0, .TickIndex = tick };
}

struct EffectWorld
{
    World world;
    GameplayTagRegistry* tags = nullptr;
    AttributeRegistry* attrs = nullptr;
    EffectRegistry* fx = nullptr;
    AttributeId health;
    AttributeId speed;

    EffectWorld()
    {
        world.RegisterComponent<GameplayTagContainer>();
        world.RegisterComponent<AttributeSet>();
        world.RegisterComponent<ActiveEffect>();
        tags = &world.AddResource<GameplayTagRegistry>();
        attrs = &world.AddResource<AttributeRegistry>();
        fx = &world.AddResource<EffectRegistry>();

        health = attrs->RegisterAttribute("Health", 0.0f, 100.0f, 100.0f);
        speed = attrs->RegisterAttribute("Speed");
    }

    EntityId MakeActor(float hp, float spd, StoragePartitionId partition = StoragePartitionId::Default())
    {
        EntityId e = world.CreateEntity(partition);
        world.AddComponent<AttributeSet>(e);
        world.AddComponent<GameplayTagContainer>(e);
        AttributeSet* set = world.TryGet<AttributeSet>(e);
        set->Add(health, hp);
        set->Add(speed, spd);
        return e;
    }
};
} // namespace

class EffectFixture : public ::testing::Test, protected EffectWorld
{
protected:
    EffectId Define(const char* name, EffectDefinition def) { return fx->Register(name, std::move(def)); }
    AttributeSet& Set(EntityId e) { return *world.TryGet<AttributeSet>(e); }
    GameplayTagContainer& Tags(EntityId e) { return *world.TryGet<GameplayTagContainer>(e); }

    // The lifetime passes of `ticks` consecutive fixed ticks.
    void Pass(int ticks = 1)
    {
        for (int i = 0; i < ticks; ++i)
            TickEffects(world);
    }

    EffectId Marker(const char* name, GameplayTagId tag, float seconds)
    {
        EffectDefinition d{};
        d.Duration = seconds > 0.0f ? EffectDuration::Duration : EffectDuration::Infinite;
        d.DurationSeconds = seconds;
        d.GrantedTags = { tag };
        return Define(name, d);
    }
};

TEST_F(EffectFixture, InstantEffectModifiesBaseAndClamps)
{
    EffectDefinition d{};
    d.Duration = EffectDuration::Instant;
    d.Modifiers = { { health, ModifierOp::Add, -10.0f } };
    const EffectId damage = Define("Damage10", d);

    EntityId hero = MakeActor(100.0f, 5.0f);
    EXPECT_FALSE(ApplyEffect(world, hero, damage, At(0)).IsValid()) << "an instant effect leaves no entity";
    EXPECT_FLOAT_EQ(Set(hero).GetBase(health), 90.0f);

    for (int i = 0; i < 20; ++i)
        ApplyEffect(world, hero, damage, At(0)); // would go negative; clamps at 0
    EXPECT_FLOAT_EQ(Set(hero).GetBase(health), 0.0f);

    ResolveAttributesWithEffects(world);
    EXPECT_FLOAT_EQ(Set(hero).GetCurrent(health), 0.0f);
}

TEST_F(EffectFixture, ContinuousBuffFoldsIntoCurrentStacksAndExpires)
{
    const GameplayTagId haste = *tags->RegisterTag("Buff.Haste");
    EffectDefinition d{};
    d.Duration = EffectDuration::Duration;
    d.DurationSeconds = 5.0f;
    d.Modifiers = { { speed, ModifierOp::Add, 2.0f } };
    d.GrantedTags = { haste };
    const EffectId hasteFx = Define("Haste", d);

    EntityId hero = MakeActor(100.0f, 5.0f);

    ApplyEffect(world, hero, hasteFx, At(0));
    ResolveAttributesWithEffects(world);
    EXPECT_FLOAT_EQ(Set(hero).GetCurrent(speed), 7.0f);
    EXPECT_TRUE(Tags(hero).HasExact(haste));

    ApplyEffect(world, hero, hasteFx, At(0)); // stack
    ResolveAttributesWithEffects(world);
    EXPECT_FLOAT_EQ(Set(hero).GetCurrent(speed), 9.0f);
    EXPECT_EQ(Tags(hero).StackCount(haste), 2u);

    Pass(5); // both end for tick 5
    ResolveAttributesWithEffects(world);
    EXPECT_FLOAT_EQ(Set(hero).GetCurrent(speed), 5.0f);
    EXPECT_FALSE(Tags(hero).HasExact(haste));
}

// A duration of D ticks applied on tick N holds through tick N + D - 1 and is
// gone for tick N + D: the first tick a cooldown permits.
TEST_F(EffectFixture, DurationEndsExactlyOnItsEndTick)
{
    const GameplayTagId cooling = *tags->RegisterTag("Cooldown.Test");
    const EffectId cooldown = Marker("Cooldown3", cooling, 3.0f);
    EntityId hero = MakeActor(100.0f, 5.0f);

    ApplyEffect(world, hero, cooldown, At(10));
    Pass(2); // ticks 10 and 11
    EXPECT_TRUE(Tags(hero).HasExact(cooling)) << "held for tick 12";
    Pass(); // tick 12
    EXPECT_FALSE(Tags(hero).HasExact(cooling)) << "gone for tick 13";
}

TEST_F(EffectFixture, DurationsAreCountedInWholeTicksOfTheTickRate)
{
    const GameplayTagId marked = *tags->RegisterTag("State.Marked");
    const EffectId halfSecond = Marker("HalfSecond", marked, 0.5f);
    EntityId hero = MakeActor(100.0f, 5.0f);

    const EntityId applied = ApplyEffect(world, hero, halfSecond, FixedSimTime{ .DeltaSeconds = 1.0 / 60.0, .TickIndex = 100 });
    ASSERT_TRUE(applied.IsValid());
    EXPECT_EQ(world.TryGet<ActiveEffect>(applied)->RemainingTicks, 30u);
}

TEST_F(EffectFixture, PeriodicEffectTicksBaseAndManagesGrantedTag)
{
    const GameplayTagId burning = *tags->RegisterTag("State.Burning");
    EffectDefinition d{};
    d.Duration = EffectDuration::Duration;
    d.DurationSeconds = 2.0f;
    d.Period = 1.0f;
    d.Modifiers = { { health, ModifierOp::Add, -3.0f } };
    d.GrantedTags = { burning };
    const EffectId poison = Define("Poison", d);

    EntityId hero = MakeActor(100.0f, 5.0f);
    ApplyEffect(world, hero, poison, At(0));
    EXPECT_TRUE(Tags(hero).HasExact(burning));

    Pass();
    EXPECT_FLOAT_EQ(Set(hero).GetBase(health), 97.0f) << "first period lands for tick 1";
    EXPECT_TRUE(Tags(hero).HasExact(burning));

    Pass(); // the second period and the end both land for tick 2
    EXPECT_FLOAT_EQ(Set(hero).GetBase(health), 94.0f);
    EXPECT_FALSE(Tags(hero).HasExact(burning));
}

TEST_F(EffectFixture, FoldAppliesAddThenMultiplyThenOverride)
{
    EffectDefinition add{};   add.Duration = EffectDuration::Infinite; add.Modifiers = { { speed, ModifierOp::Add, 10.0f } };
    EffectDefinition mul{};   mul.Duration = EffectDuration::Infinite; mul.Modifiers = { { speed, ModifierOp::Multiply, 2.0f } };
    EffectDefinition ovr{};   ovr.Duration = EffectDuration::Infinite; ovr.Modifiers = { { speed, ModifierOp::Override, 50.0f } };
    const EffectId addFx = Define("AddSpd", add);
    const EffectId mulFx = Define("MulSpd", mul);
    const EffectId ovrFx = Define("OvrSpd", ovr);

    EntityId hero = MakeActor(100.0f, 5.0f);

    ApplyEffect(world, hero, mulFx, At(0));
    ApplyEffect(world, hero, addFx, At(0));
    ResolveAttributesWithEffects(world);
    EXPECT_FLOAT_EQ(Set(hero).GetCurrent(speed), 30.0f); // (5 + 10) * 2

    ApplyEffect(world, hero, ovrFx, At(0));
    ResolveAttributesWithEffects(world);
    EXPECT_FLOAT_EQ(Set(hero).GetCurrent(speed), 50.0f); // override wins last
}

TEST_F(EffectFixture, GrantedTagsAreRefCountedAcrossStackedEffects)
{
    const GameplayTagId marked = *tags->RegisterTag("State.Marked");
    const EffectId shortFx = Marker("ShortMark", marked, 1.0f);
    const EffectId longFx = Marker("LongMark", marked, 5.0f);

    EntityId hero = MakeActor(100.0f, 5.0f);
    ApplyEffect(world, hero, shortFx, At(0));
    ApplyEffect(world, hero, longFx, At(0));
    EXPECT_EQ(Tags(hero).StackCount(marked), 2u);

    Pass(); // short ends for tick 1, long remains
    EXPECT_TRUE(Tags(hero).HasExact(marked));
    EXPECT_EQ(Tags(hero).StackCount(marked), 1u);

    Pass(4); // long ends for tick 5
    EXPECT_FALSE(Tags(hero).HasExact(marked));
}

TEST_F(EffectFixture, RemovingAnEffectRevokesItsTags)
{
    const GameplayTagId marked = *tags->RegisterTag("State.Marked");
    const EffectId mark = Marker("Mark", marked, 0.0f);
    EntityId hero = MakeActor(100.0f, 5.0f);

    const EntityId first = ApplyEffect(world, hero, mark, At(0));
    ApplyEffect(world, hero, mark, At(0));
    EXPECT_TRUE(RemoveEffect(world, first));
    EXPECT_FALSE(world.IsAlive(first));
    EXPECT_EQ(Tags(hero).StackCount(marked), 1u) << "the other instance's stack stays";
    EXPECT_FALSE(RemoveEffect(world, first)) << "ending twice is a no-op";
}

TEST_F(EffectFixture, RemovingByOwnerEndsOnlyThatOwnersEffects)
{
    const GameplayTagId marked = *tags->RegisterTag("State.Marked");
    const EffectId mark = Marker("Mark", marked, 0.0f);
    EntityId hero = MakeActor(100.0f, 5.0f);
    const EntityId holder = world.CreateEntity();

    const LeaseOwner phase{ holder, 1 };
    const LeaseOwner other{ holder, 2 };
    ApplyEffect(world, hero, mark, At(0), phase);
    ApplyEffect(world, hero, mark, At(0), phase);
    const EntityId kept = ApplyEffect(world, hero, mark, At(0), other);
    const EntityId unowned = ApplyEffect(world, hero, mark, At(0));

    EXPECT_EQ(RemoveEffectsOwnedBy(world, phase), 2u);
    EXPECT_TRUE(world.IsAlive(kept));
    EXPECT_TRUE(world.IsAlive(unowned));
    EXPECT_EQ(Tags(hero).StackCount(marked), 2u);
}

TEST_F(EffectFixture, AnEffectWhoseOwnerIsGoneEndsOnTheNextPass)
{
    const GameplayTagId marked = *tags->RegisterTag("State.Marked");
    const EffectId mark = Marker("Mark", marked, 0.0f);
    EntityId hero = MakeActor(100.0f, 5.0f);
    const EntityId holder = world.CreateEntity();

    const EntityId held = ApplyEffect(world, hero, mark, At(0), LeaseOwner{ holder, 7 });
    world.DestroyEntity(holder);
    Pass();
    EXPECT_FALSE(world.IsAlive(held));
    EXPECT_FALSE(Tags(hero).HasExact(marked));
}

TEST_F(EffectFixture, AnEffectWhoseDefinitionIsGoneStillRevokesItsTags)
{
    const GameplayTagId marked = *tags->RegisterTag("State.Marked");
    const EffectId mark = Marker("Mark", marked, 0.0f);
    EntityId hero = MakeActor(100.0f, 5.0f);
    ApplyEffect(world, hero, mark, At(0));

    world.SetResource(EffectRegistry{}); // the definition disappears, as a failed reload would leave it
    Pass();
    EXPECT_FALSE(Tags(hero).HasExact(marked));
}

TEST_F(EffectFixture, ATagTheContainerCannotHoldIsNeitherGrantedNorRevoked)
{
    EntityId hero = MakeActor(100.0f, 5.0f);
    for (std::uint8_t i = 0; i < GameplayTagContainer::Capacity; ++i)
        Tags(hero).Grant(*tags->RegisterTag("Fill.T" + std::to_string(i)));

    const GameplayTagId extra = *tags->RegisterTag("State.Extra");
    const EffectId mark = Marker("Mark", extra, 0.0f);
    const EntityId applied = ApplyEffect(world, hero, mark, At(0));
    EXPECT_FALSE(Tags(hero).HasExact(extra));
    EXPECT_EQ(world.TryGet<ActiveEffect>(applied)->GrantedTagCount, 0u);

    RemoveEffect(world, applied);
    EXPECT_EQ(Tags(hero).Size(), GameplayTagContainer::Capacity);
}

TEST_F(EffectFixture, TheRegistryRefusesMoreGrantedTagsThanAnEffectRecords)
{
    EffectDefinition d{};
    d.Duration = EffectDuration::Infinite;
    for (std::uint8_t i = 0; i <= kEffectGrantedTagCapacity; ++i)
        d.GrantedTags.push_back(*tags->RegisterTag("Many.T" + std::to_string(i)));
    EXPECT_FALSE(Define("TooMany", d).IsValid());
}

// Effects live in the persistent partition, so a target moving between zones, or
// its old zone detaching, leaves its effects and tags consistent; a target whose
// zone detaches with it takes its effects with it on the next pass.
TEST_F(EffectFixture, EffectsSurviveTheirTargetsZoneMovesAndEndWithTheTarget)
{
    const StoragePartitionId zoneA{ 2 };
    const StoragePartitionId zoneB{ 3 };
    const GameplayTagId marked = *tags->RegisterTag("State.Marked");
    const EffectId mark = Marker("Mark", marked, 0.0f);
    EntityId hero = MakeActor(100.0f, 5.0f, zoneA);

    const EntityId applied = ApplyEffect(world, hero, mark, At(0));
    EXPECT_EQ(world.GetEntityPartition(applied), StoragePartitionId::Default());

    ASSERT_TRUE(world.MoveEntityToPartition(hero, zoneB));
    world.DestroyPartition(zoneA);
    Pass();
    EXPECT_TRUE(world.IsAlive(applied));
    EXPECT_TRUE(Tags(hero).HasExact(marked));

    world.DestroyPartition(zoneB);
    Pass();
    EXPECT_FALSE(world.IsAlive(applied));
}

TEST_F(EffectFixture, ADormantTargetsEffectsDoNotAdvance)
{
    const StoragePartitionId zone{ 2 };
    const GameplayTagId marked = *tags->RegisterTag("State.Marked");
    const EffectId brief = Marker("Brief", marked, 1.0f);
    EntityId hero = MakeActor(100.0f, 5.0f, zone);
    ApplyEffect(world, hero, brief, At(0));

    StoragePartitionSet active;
    active.Add(StoragePartitionId::Default());
    for (int i = 0; i < 5; ++i)
        TickEffects(world, active);
    EXPECT_TRUE(Tags(hero).HasExact(marked)) << "asleep: nothing counts down";

    active.Add(zone);
    TickEffects(world, active);
    EXPECT_FALSE(Tags(hero).HasExact(marked));
}

namespace
{
// Applies three Multiply effects to one target, optionally reshuffling the effect
// storage first, and returns the folded value.
float FoldThreeMultipliers(bool reshuffleStorage)
{
    EffectWorld w;
    EffectId factors[3];
    const float magnitudes[3] = { 1.1f, 1.3f, 0.7f };
    for (int i = 0; i < 3; ++i)
    {
        EffectDefinition d{};
        d.Duration = EffectDuration::Infinite;
        d.Modifiers = { { w.speed, ModifierOp::Multiply, magnitudes[i] } };
        factors[i] = w.fx->Register("Factor" + std::to_string(i), d);
    }
    EffectDefinition filler{};
    filler.Duration = EffectDuration::Infinite;
    const EffectId fillerFx = w.fx->Register("Filler", filler);

    const EntityId hero = w.MakeActor(100.0f, 3.0f);
    const EntityId bystander = w.MakeActor(100.0f, 1.0f);
    EntityId spacer;
    if (reshuffleStorage)
        spacer = ApplyEffect(w.world, bystander, fillerFx, At(0));
    for (EffectId factor : factors)
        ApplyEffect(w.world, hero, factor, At(0));
    // Swap-and-pop moves the last effect into the spacer's row, so storage now
    // holds the factors in a different order.
    if (reshuffleStorage)
        RemoveEffect(w.world, spacer);

    ResolveAttributesWithEffects(w.world);
    return w.world.TryGet<AttributeSet>(hero)->GetCurrent(w.speed);
}
} // namespace

TEST(EffectFold, TheFoldIsIdenticalWhateverOrderStorageHolds)
{
    EXPECT_EQ(FoldThreeMultipliers(false), FoldThreeMultipliers(true));
}

TEST(EffectCost, CostsOnOneAttributeAreSummedBeforeTheyAreCompared)
{
    EffectWorld w;
    EffectDefinition half{};
    half.Duration = EffectDuration::Instant;
    half.Modifiers = { { w.health, ModifierOp::Add, -60.0f } };
    const EffectId first = w.fx->Register("SixtyA", half);
    const EffectId second = w.fx->Register("SixtyB", half);
    const EntityId hero = w.MakeActor(100.0f, 5.0f);

    const std::array one{ first };
    EXPECT_TRUE(CheckEffectCosts(w.world, hero, one).Affordable());

    const std::array both{ first, second };
    const EffectCostCheck check = CheckEffectCosts(w.world, hero, both);
    EXPECT_EQ(check.Status, EffectCostStatus::Insufficient);
    EXPECT_EQ(check.Attribute, w.health);
    EXPECT_FLOAT_EQ(check.Needed, 120.0f);
    EXPECT_FLOAT_EQ(check.Available, 100.0f);
}

TEST(EffectCost, ACostFailsClosedOnAnythingMissingOrInexact)
{
    EffectWorld w;
    EffectDefinition lasting{};
    lasting.Duration = EffectDuration::Duration;
    lasting.DurationSeconds = 1.0f;
    lasting.Modifiers = { { w.health, ModifierOp::Add, -1.0f } };
    EffectDefinition scaling{};
    scaling.Duration = EffectDuration::Instant;
    scaling.Modifiers = { { w.health, ModifierOp::Multiply, 0.5f } };
    EffectDefinition onSpeed{};
    onSpeed.Duration = EffectDuration::Instant;
    onSpeed.Modifiers = { { w.speed, ModifierOp::Add, -1.0f } };
    const EffectId lastingFx = w.fx->Register("Lasting", lasting);
    const EffectId scalingFx = w.fx->Register("Scaling", scaling);
    const EffectId speedFx = w.fx->Register("OnSpeed", onSpeed);

    const EntityId hero = w.MakeActor(100.0f, 5.0f);
    const EntityId bare = w.world.CreateEntity();
    const EntityId empty = w.world.CreateEntity();
    w.world.AddComponent<AttributeSet>(empty);

    const auto check = [&](EntityId actor, EffectId cost) {
        const std::array costs{ cost };
        return CheckEffectCosts(w.world, actor, costs).Status;
    };
    EXPECT_EQ(check(hero, EffectId{ 99 }), EffectCostStatus::DefinitionMissing);
    EXPECT_EQ(check(hero, lastingFx), EffectCostStatus::NotAnInstantCost);
    EXPECT_EQ(check(hero, scalingFx), EffectCostStatus::NotAnInstantCost);
    EXPECT_EQ(check(bare, speedFx), EffectCostStatus::AttributeSetMissing);
    EXPECT_EQ(check(empty, speedFx), EffectCostStatus::AttributeMissing);
    EXPECT_EQ(check(hero, speedFx), EffectCostStatus::Affordable);
}

TEST(EffectCost, ACostNeverTakesAnUnboundedAttributeBelowZero)
{
    EffectWorld w;
    EffectDefinition costly{};
    costly.Duration = EffectDuration::Instant;
    costly.Modifiers = { { w.speed, ModifierOp::Add, -6.0f } };
    const EffectId cost = w.fx->Register("Costly", costly);
    const EntityId hero = w.MakeActor(100.0f, 5.0f);

    const std::array costs{ cost };
    EXPECT_EQ(CheckEffectCosts(w.world, hero, costs).Status, EffectCostStatus::Insufficient);
}
