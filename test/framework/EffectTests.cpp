#include <attributes/AttributeRegistry.h>
#include <attributes/AttributeSet.h>
#include <effects/ActiveEffect.h>
#include <effects/EffectRegistry.h>
#include <effects/EffectSystem.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <ecs/World.h>

#include <gtest/gtest.h>

class EffectFixture : public ::testing::Test
{
protected:
    World world;
    GameplayTagRegistry* tags = nullptr;
    AttributeRegistry* attrs = nullptr;
    EffectRegistry* fx = nullptr;

    AttributeId health;
    AttributeId speed;

    void SetUp() override
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

    EntityId MakeActor(float hp, float spd)
    {
        EntityId e = world.CreateEntity();
        world.AddComponent<AttributeSet>(e);
        world.AddComponent<GameplayTagContainer>(e);
        AttributeSet* set = world.TryGet<AttributeSet>(e);
        set->Add(health, hp);
        set->Add(speed, spd);
        return e;
    }

    EffectId Define(const char* name, EffectDefinition def) { return fx->Register(name, std::move(def)); }
    AttributeSet& Set(EntityId e) { return *world.TryGet<AttributeSet>(e); }
    GameplayTagContainer& Tags(EntityId e) { return *world.TryGet<GameplayTagContainer>(e); }
};

TEST_F(EffectFixture, InstantEffectModifiesBaseAndClamps)
{
    EffectDefinition d{};
    d.Duration = EffectDuration::Instant;
    d.Modifiers = { { health, ModifierOp::Add, -10.0f } };
    const EffectId damage = Define("Damage10", d);

    EntityId hero = MakeActor(100.0f, 5.0f);
    ApplyEffect(world, hero, damage);
    EXPECT_FLOAT_EQ(Set(hero).GetBase(health), 90.0f);

    for (int i = 0; i < 20; ++i)
        ApplyEffect(world, hero, damage); // would go negative; clamps at 0
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

    ApplyEffect(world, hero, hasteFx);
    ResolveAttributesWithEffects(world);
    EXPECT_FLOAT_EQ(Set(hero).GetCurrent(speed), 7.0f);
    EXPECT_TRUE(Tags(hero).HasExact(haste));

    ApplyEffect(world, hero, hasteFx); // stack
    ResolveAttributesWithEffects(world);
    EXPECT_FLOAT_EQ(Set(hero).GetCurrent(speed), 9.0f);
    EXPECT_EQ(Tags(hero).StackCount(haste), 2u);

    TickEffects(world, 5.0f); // both expire
    ResolveAttributesWithEffects(world);
    EXPECT_FLOAT_EQ(Set(hero).GetCurrent(speed), 5.0f);
    EXPECT_FALSE(Tags(hero).HasExact(haste));
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
    ApplyEffect(world, hero, poison);
    EXPECT_TRUE(Tags(hero).HasExact(burning));

    TickEffects(world, 1.0f);
    EXPECT_FLOAT_EQ(Set(hero).GetBase(health), 97.0f);
    EXPECT_TRUE(Tags(hero).HasExact(burning));

    TickEffects(world, 1.0f); // second tick applies -3 and expires
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

    ApplyEffect(world, hero, addFx);
    ApplyEffect(world, hero, mulFx);
    ResolveAttributesWithEffects(world);
    EXPECT_FLOAT_EQ(Set(hero).GetCurrent(speed), 30.0f); // (5 + 10) * 2

    ApplyEffect(world, hero, ovrFx);
    ResolveAttributesWithEffects(world);
    EXPECT_FLOAT_EQ(Set(hero).GetCurrent(speed), 50.0f); // override wins last
}

TEST_F(EffectFixture, GrantedTagsAreRefCountedAcrossStackedEffects)
{
    const GameplayTagId marked = *tags->RegisterTag("State.Marked");
    EffectDefinition shortMark{}; shortMark.Duration = EffectDuration::Duration; shortMark.DurationSeconds = 1.0f; shortMark.GrantedTags = { marked };
    EffectDefinition longMark{};  longMark.Duration = EffectDuration::Duration;  longMark.DurationSeconds = 5.0f;  longMark.GrantedTags = { marked };
    const EffectId shortFx = Define("ShortMark", shortMark);
    const EffectId longFx = Define("LongMark", longMark);

    EntityId hero = MakeActor(100.0f, 5.0f);
    ApplyEffect(world, hero, shortFx);
    ApplyEffect(world, hero, longFx);
    EXPECT_EQ(Tags(hero).StackCount(marked), 2u);

    TickEffects(world, 1.0f); // short expires, long remains
    EXPECT_TRUE(Tags(hero).HasExact(marked));
    EXPECT_EQ(Tags(hero).StackCount(marked), 1u);

    TickEffects(world, 5.0f); // long expires
    EXPECT_FALSE(Tags(hero).HasExact(marked));
}
