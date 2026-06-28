#include <framework/abilities/AbilityActivation.h>
#include <framework/abilities/AbilityRegistry.h>
#include <framework/abilities/AbilitySet.h>
#include <framework/abilities/AbilitySystem.h>
#include <framework/attributes/AttributeRegistry.h>
#include <framework/attributes/AttributeSet.h>
#include <framework/effects/ActiveEffect.h>
#include <framework/effects/EffectRegistry.h>
#include <framework/effects/EffectSystem.h>
#include <framework/gameplay_tags/GameplayTagContainer.h>
#include <framework/gameplay_tags/GameplayTagRegistry.h>
#include <ecs/World.h>

#include <gtest/gtest.h>

class AbilityFixture : public ::testing::Test
{
protected:
    World world;
    GameplayTagRegistry* tags = nullptr;
    AttributeRegistry* attrs = nullptr;
    EffectRegistry* effects = nullptr;
    AbilityRegistry* abilities = nullptr;

    AttributeId stamina;
    GameplayTagId ready, stunned, cooldownDash, stateA, stateB;
    AbilityId dash, strike, readyAbility, abilityA, abilityB;

    void SetUp() override
    {
        world.RegisterComponent<GameplayTagContainer>();
        world.RegisterComponent<AttributeSet>();
        world.RegisterComponent<ActiveEffect>();
        world.RegisterComponent<AbilitySet>();
        tags = &world.AddResource<GameplayTagRegistry>();
        attrs = &world.AddResource<AttributeRegistry>();
        effects = &world.AddResource<EffectRegistry>();
        abilities = &world.AddResource<AbilityRegistry>();
        world.AddResource<AbilityActivationQueue>();

        stamina = attrs->RegisterAttribute("Stamina", 0.0f, 100.0f, 100.0f);
        ready = *tags->RegisterTag("State.Ready");
        stunned = *tags->RegisterTag("State.Stunned");
        cooldownDash = *tags->RegisterTag("Cooldown.Dash");
        stateA = *tags->RegisterTag("State.A");
        stateB = *tags->RegisterTag("State.B");

        EffectDefinition cost{};
        cost.Duration = EffectDuration::Instant;
        cost.Modifiers = { { stamina, ModifierOp::Add, -20.0f } };
        const EffectId staminaCost = effects->Register("StaminaCost20", cost);

        EffectDefinition cd{};
        cd.Duration = EffectDuration::Duration;
        cd.DurationSeconds = 3.0f;
        cd.GrantedTags = { cooldownDash };
        const EffectId dashCd = effects->Register("DashCooldown", cd);

        EffectDefinition sa{};
        sa.Duration = EffectDuration::Duration;
        sa.DurationSeconds = 5.0f;
        sa.GrantedTags = { stateA };
        const EffectId stateAFx = effects->Register("StateAFx", sa);

        EffectDefinition sb{};
        sb.Duration = EffectDuration::Duration;
        sb.DurationSeconds = 5.0f;
        sb.GrantedTags = { stateB };
        const EffectId stateBFx = effects->Register("StateBFx", sb);

        AbilityDefinition dashDef{};
        dashDef.ActivationRequirements.AddNone(stunned).AddNone(cooldownDash);
        dashDef.Cost = staminaCost;
        dashDef.Cooldown = dashCd;
        dash = abilities->Register("Dash", dashDef);

        AbilityDefinition strikeDef{};
        strikeDef.ActivationRequirements.AddNone(stunned);
        strikeDef.Cost = staminaCost;
        strike = abilities->Register("Strike", strikeDef);

        AbilityDefinition readyDef{};
        readyDef.ActivationRequirements.AddAll(ready);
        readyAbility = abilities->Register("ReadyAbility", readyDef);

        AbilityDefinition aDef{};
        aDef.ActivationRequirements.AddNone(stateB);
        aDef.OnActivate = stateAFx;
        abilityA = abilities->Register("AbilityA", aDef);

        AbilityDefinition bDef{};
        bDef.ActivationRequirements.AddNone(stateA);
        bDef.OnActivate = stateBFx;
        abilityB = abilities->Register("AbilityB", bDef);
    }

    EntityId MakeActor(float sta, std::initializer_list<AbilityId> granted)
    {
        EntityId e = world.CreateEntity();
        world.AddComponent<AttributeSet>(e);
        world.AddComponent<GameplayTagContainer>(e);
        world.AddComponent<AbilitySet>(e);
        world.TryGet<AttributeSet>(e)->Add(stamina, sta);
        for (AbilityId a : granted)
            world.TryGet<AbilitySet>(e)->Grant(a);
        return e;
    }

    GameplayTagContainer& Tags(EntityId e) { return *world.TryGet<GameplayTagContainer>(e); }
    AttributeSet& Attrs(EntityId e) { return *world.TryGet<AttributeSet>(e); }
};

TEST_F(AbilityFixture, RequireTagGatesActivation)
{
    EntityId hero = MakeActor(100.0f, { readyAbility });
    EXPECT_FALSE(TryActivateAbility(world, hero, readyAbility)); // missing State.Ready
    Tags(hero).Grant(ready);
    EXPECT_TRUE(TryActivateAbility(world, hero, readyAbility));
}

TEST_F(AbilityFixture, BlockTagPreventsActivation)
{
    EntityId hero = MakeActor(100.0f, { strike });
    Tags(hero).Grant(stunned);
    EXPECT_FALSE(TryActivateAbility(world, hero, strike));
}

TEST_F(AbilityFixture, CostIsPaidAndUnaffordableActivationFails)
{
    EntityId hero = MakeActor(50.0f, { strike });
    EXPECT_TRUE(TryActivateAbility(world, hero, strike));
    EXPECT_FLOAT_EQ(Attrs(hero).GetBase(stamina), 30.0f);
    EXPECT_TRUE(TryActivateAbility(world, hero, strike));
    EXPECT_FLOAT_EQ(Attrs(hero).GetBase(stamina), 10.0f);
    EXPECT_FALSE(TryActivateAbility(world, hero, strike)); // 10 < 20
    EXPECT_FLOAT_EQ(Attrs(hero).GetBase(stamina), 10.0f);  // unchanged
}

TEST_F(AbilityFixture, CooldownTagBlocksReactivationUntilExpiry)
{
    EntityId hero = MakeActor(100.0f, { dash });
    EXPECT_TRUE(TryActivateAbility(world, hero, dash));
    EXPECT_TRUE(Tags(hero).HasExact(cooldownDash));
    EXPECT_FALSE(TryActivateAbility(world, hero, dash)); // on cooldown
    EXPECT_FLOAT_EQ(Attrs(hero).GetBase(stamina), 80.0f); // no second charge

    TickEffects(world, 3.0f); // cooldown expires
    EXPECT_FALSE(Tags(hero).HasExact(cooldownDash));
    EXPECT_TRUE(TryActivateAbility(world, hero, dash));
    EXPECT_FLOAT_EQ(Attrs(hero).GetBase(stamina), 60.0f);
}

TEST_F(AbilityFixture, MutuallyBlockingAbilitiesCannotCoexist)
{
    EntityId hero = MakeActor(100.0f, { abilityA, abilityB });
    EXPECT_TRUE(TryActivateAbility(world, hero, abilityA));
    EXPECT_TRUE(Tags(hero).HasExact(stateA));
    EXPECT_FALSE(TryActivateAbility(world, hero, abilityB)); // blocked by State.A

    EntityId other = MakeActor(100.0f, { abilityA, abilityB });
    EXPECT_TRUE(TryActivateAbility(world, other, abilityB));
    EXPECT_FALSE(TryActivateAbility(world, other, abilityA)); // blocked by State.B
}

TEST_F(AbilityFixture, UngrantedAbilityCannotActivate)
{
    EntityId hero = MakeActor(100.0f, { strike }); // not granted Dash
    EXPECT_FALSE(TryActivateAbility(world, hero, dash));
}

TEST_F(AbilityFixture, IntentQueueDrains)
{
    EntityId hero = MakeActor(100.0f, { abilityA });
    world.GetResource<AbilityActivationQueue>().Pending.push_back({ hero, abilityA });
    ProcessAbilityActivations(world);
    EXPECT_TRUE(Tags(hero).HasExact(stateA));
    EXPECT_TRUE(world.GetResource<AbilityActivationQueue>().Pending.empty());
}
