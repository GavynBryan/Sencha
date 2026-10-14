// effect.apply and effect.hold: what they refuse, and how a hold ends with its owner.

#include <attributes/AttributeRegistry.h>
#include <attributes/AttributeSet.h>
#include <effects/ActiveEffect.h>
#include <effects/EffectRegistry.h>
#include <effects/EffectSystem.h>
#include <effects/EffectVerbs.h>
#include <ecs/World.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <time/SimClock.h>

#include <gtest/gtest.h>

#include <string>

namespace
{
class EffectVerbTest : public ::testing::Test
{
protected:
    EffectVerbTest()
    {
        Entities.RegisterComponent<GameplayTagContainer>();
        Entities.RegisterComponent<AttributeSet>();
        Entities.RegisterComponent<ActiveEffect>();
        Tags = &Entities.AddResource<GameplayTagRegistry>();
        AttributeRegistry& attributes = Entities.AddResource<AttributeRegistry>();
        Effects = &Entities.AddResource<EffectRegistry>();
        Health = attributes.RegisterAttribute("Health", 0.0f, 100.0f, 100.0f);
        Speed = attributes.RegisterAttribute("Speed");
        Burning = Tags->RegisterTag("State.Burning").value_or(GameplayTagId{});

        Target = Entities.CreateEntity();
        Entities.AddComponent<AttributeSet>(Target);
        Entities.AddComponent<GameplayTagContainer>(Target);
        Entities.TryGet<AttributeSet>(Target)->Add(Health, 100.0f);

        EffectDefinition hit{};
        hit.Modifiers = { { Health, ModifierOp::Add, -10.0f } };
        Effects->Define("asset://fx/hit.sdata", hit);
        EffectDefinition burn{};
        burn.Duration = EffectDuration::Infinite;
        burn.Modifiers = { { Health, ModifierOp::Add, -5.0f } };
        burn.GrantedTags = { Burning };
        Effects->Define("asset://fx/burn.sdata", burn);
        EffectDefinition slow{};
        slow.Modifiers = { { Speed, ModifierOp::Multiply, 0.5f } };
        Effects->Define("asset://fx/slow.sdata", slow);
    }

    VerbInvocation Invocation(EntityId target, std::string effect, double scale = 1.0)
    {
        Arguments.Resize(3);
        Arguments.Set(0, AuthoredValue::Entity(target));
        Arguments.Set(1, AuthoredValue::DataAsset(AssetRef{ .Type = AssetType::Data, .Path = std::move(effect) }));
        Arguments.Set(2, AuthoredValue::Float(scale));
        VerbInvocation invocation;
        invocation.Arguments = &Arguments;
        return invocation;
    }

    float HealthOf(EntityId entity) { return Entities.TryGet<AttributeSet>(entity)->GetBase(Health); }

    World Entities;
    FixedSimulationLoop Clock;
    EffectOperations Operations{ Entities, Clock };
    AuthoredArguments Arguments;
    GameplayTagRegistry* Tags = nullptr;
    EffectRegistry* Effects = nullptr;
    AttributeId Health;
    AttributeId Speed;
    GameplayTagId Burning;
    EntityId Target;
};
}

TEST_F(EffectVerbTest, ApplyScalesEveryModifier)
{
    EXPECT_EQ(Operations.Apply(Invocation(Target, "asset://fx/hit.sdata", 2.5)), VerbAdmission::Accepted);
    EXPECT_FLOAT_EQ(HealthOf(Target), 75.0f);
}

TEST_F(EffectVerbTest, ApplyRefusesARecipientItCannotModify)
{
    const EntityId bare = Entities.CreateEntity();
    EXPECT_EQ(Operations.Apply(Invocation(bare, "asset://fx/hit.sdata")), VerbAdmission::Refused);
    EXPECT_EQ(Operations.Apply(Invocation(Target, "asset://fx/slow.sdata")), VerbAdmission::Refused)
        << "the target has no Speed for the effect to modify";
    EXPECT_EQ(Operations.Apply(Invocation(Target, "asset://fx/missing.sdata")), VerbAdmission::InvalidArguments);
    EXPECT_FLOAT_EQ(HealthOf(Target), 100.0f);
}

TEST_F(EffectVerbTest, AHoldLastsUntilItsOwnerReleasesIt)
{
    const LeaseOwner holder{ .Entity = Target, .Key = 3 };
    VerbInvocation acquire = Invocation(Target, "asset://fx/burn.sdata", 2.0);
    acquire.Step = LeaseStep::Acquire;
    acquire.Owner = holder;
    ASSERT_EQ(Operations.Hold(acquire), VerbAdmission::Accepted);
    ResolveAttributesWithEffects(Entities);
    EXPECT_FLOAT_EQ(Entities.TryGet<AttributeSet>(Target)->GetCurrent(Health), 90.0f) << "folded at its scale";
    EXPECT_GT(Entities.TryGet<GameplayTagContainer>(Target)->StackCount(Burning), 0u);

    VerbInvocation release;
    release.Step = LeaseStep::Release;
    release.Owner = holder;
    EXPECT_EQ(Operations.Hold(release), VerbAdmission::Accepted);
    ResolveAttributesWithEffects(Entities);
    EXPECT_FLOAT_EQ(Entities.TryGet<AttributeSet>(Target)->GetCurrent(Health), 100.0f);
    EXPECT_EQ(Entities.TryGet<GameplayTagContainer>(Target)->StackCount(Burning), 0u);
}

TEST_F(EffectVerbTest, AnInstantEffectCannotBeHeld)
{
    VerbInvocation acquire = Invocation(Target, "asset://fx/hit.sdata");
    acquire.Step = LeaseStep::Acquire;
    acquire.Owner = LeaseOwner{ .Entity = Target, .Key = 1 };
    EXPECT_EQ(Operations.Hold(acquire), VerbAdmission::InvalidArguments);
    EXPECT_FLOAT_EQ(HealthOf(Target), 100.0f);
}
