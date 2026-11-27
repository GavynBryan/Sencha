#include <gameplay_tags/GameplayTagVerbs.h>
#include "GeneratedVerbCall.h"
#include <authored/AuthoredValue.h>
#include <core/serialization/JsonArchive.h>
#include <effects/ActiveEffect.h>
#include <effects/EffectRegistry.h>
#include <effects/EffectSystem.h>
#include <ecs/World.h>
#include <gameplay_tags/BaselineGameplayTags.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <gameplay_tags/GameplayTagOverflow.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <gameplay_tags/GameplayTagSerialization.h>
#include <world/SimulationAuthority.h>

#include <gtest/gtest.h>

#include <string>

namespace
{
class GameplayTagVerbTest : public ::testing::Test
{
protected:
    GameplayTagVerbTest()
    {
        Entities.RegisterComponent<GameplayTagContainer>();
        Entities.RegisterComponent<ActiveEffect>();
        Tags = &Entities.AddResource<GameplayTagRegistry>();
        Unlocked = *Tags->RegisterTag("Unlock.DoubleJump");
        Burning = *Tags->RegisterTag("State.Burning");
        Grounded = *Tags->RegisterTag("movement.grounded");
        Tags->SetProjected(Grounded, "movement.support");

        EffectDefinition burn{};
        burn.Duration = EffectDuration::Infinite;
        burn.GrantedTags = { Burning };
        Burn = Entities.AddResource<EffectRegistry>().Define("asset://fx/burn.sdata", burn);

        Target = Entities.CreateEntity();
    }

    VerbInvocation Invocation(EntityId target, GameplayTagId tag, std::int64_t stacks = 1)
    {
        Arguments.Resize(3);
        Arguments.Set(0, AuthoredValue::Entity(target));
        Arguments.Set(1, AuthoredValue::Tag(tag));
        Arguments.Set(2, AuthoredValue::Int(stacks));
        VerbInvocation invocation;
        invocation.Arguments = &Arguments;
        return invocation;
    }

    VerbAdmission Grant(const VerbInvocation& invocation)
    {
        return CallGeneratedVerb(Operations, AuthoredApiDefinition<GameplayTagOperations>::Name_Grant, invocation).Status;
    }

    VerbAdmission Revoke(const VerbInvocation& invocation)
    {
        return CallGeneratedVerb(Operations, AuthoredApiDefinition<GameplayTagOperations>::Name_Revoke, invocation).Status;
    }

    [[nodiscard]] const GameplayTagContainer* TagsOf(EntityId entity) const
    {
        return std::as_const(Entities).TryGet<GameplayTagContainer>(entity);
    }

    [[nodiscard]] std::size_t SavedCount(EntityId entity) const
    {
        JsonWriteArchive saved;
        EXPECT_TRUE(WriteGameplayTags(saved, *TagsOf(entity), *Tags));
        return saved.TakeValue().Find("tags")->AsArray().size();
    }

    World Entities;
    GameplayTagOperations Operations{ Entities };
    AuthoredArguments Arguments;
    GameplayTagRegistry* Tags = nullptr;
    GameplayTagId Unlocked;
    GameplayTagId Burning;
    GameplayTagId Grounded;
    EffectId Burn;
    EntityId Target;
};
}

TEST_F(GameplayTagVerbTest, AGrantIsBaselineAndARevokeTakesItBack)
{
    EXPECT_EQ(Grant(Invocation(Target, Unlocked, 2)), VerbAdmission::Accepted);
    ASSERT_NE(TagsOf(Target), nullptr) << "an entity with no tags gains them";
    EXPECT_EQ(TagsOf(Target)->BaselineCount(Unlocked), 2u);
    EXPECT_EQ(SavedCount(Target), 1u) << "a baseline grant is kept by a save";

    EXPECT_EQ(Revoke(Invocation(Target, Unlocked)), VerbAdmission::Accepted);
    EXPECT_EQ(TagsOf(Target)->StackCount(Unlocked), 1u);
    EXPECT_EQ(Revoke(Invocation(Target, Unlocked, 5)), VerbAdmission::Accepted) << "at most what it keeps";
    EXPECT_FALSE(TagsOf(Target)->HasExact(Unlocked));
    EXPECT_EQ(Revoke(Invocation(Target, Unlocked)), VerbAdmission::Refused) << "nothing left to take";
}

TEST_F(GameplayTagVerbTest, AnEffectsTagIsNeitherRevokedNorSaved)
{
    Entities.AddComponent<GameplayTagContainer>(Target);
    const EntityId burning = ApplyEffect(Entities, Target, Burn, FixedSimTime{});
    ASSERT_TRUE(TagsOf(Target)->HasExact(Burning));
    EXPECT_EQ(Revoke(Invocation(Target, Burning)), VerbAdmission::Refused);
    EXPECT_TRUE(TagsOf(Target)->HasExact(Burning));
    EXPECT_EQ(SavedCount(Target), 0u);

    EXPECT_EQ(Grant(Invocation(Target, Burning)), VerbAdmission::Accepted);
    EXPECT_EQ(TagsOf(Target)->StackCount(Burning), 2u);
    EXPECT_EQ(SavedCount(Target), 1u) << "only the granted stack is kept";
    (void)RemoveEffect(Entities, burning);
    EXPECT_EQ(TagsOf(Target)->StackCount(Burning), 1u) << "the effect ending leaves the granted stack";
}

TEST_F(GameplayTagVerbTest, AProjectedTagIsRefused)
{
    EXPECT_EQ(Grant(Invocation(Target, Grounded)), VerbAdmission::Refused);
    EXPECT_EQ(Revoke(Invocation(Target, Grounded)), VerbAdmission::Refused);
    EXPECT_TRUE(TagsOf(Target) == nullptr || !TagsOf(Target)->HasExact(Grounded));
}

TEST_F(GameplayTagVerbTest, AFullContainerRefusesANewTagAndCountsIt)
{
    Entities.AddComponent<GameplayTagContainer>(Target);
    for (std::uint8_t i = 0; i + 1 < GameplayTagContainer::Capacity; ++i)
        Entities.TryGet<GameplayTagContainer>(Target)->GrantBaseline(*Tags->RegisterTag("Fill.T" + std::to_string(i)));
    EXPECT_EQ(Grant(Invocation(Target, Unlocked)), VerbAdmission::Accepted);
    EXPECT_FALSE(Entities.HasResource<GameplayTagOverflow>());

    EXPECT_EQ(Grant(Invocation(Target, Burning)), VerbAdmission::Refused);
    EXPECT_FALSE(TagsOf(Target)->HasExact(Burning));
    ASSERT_TRUE(Entities.HasResource<GameplayTagOverflow>());
    EXPECT_EQ(Entities.GetResource<GameplayTagOverflow>().Refused, 1u);
    EXPECT_EQ(Entities.GetResource<GameplayTagOverflow>().FirstTag, Burning);

    EXPECT_EQ(Grant(Invocation(Target, Unlocked)), VerbAdmission::Accepted);
    EXPECT_EQ(TagsOf(Target)->BaselineCount(Unlocked), 2u);
}

TEST_F(GameplayTagVerbTest, OnlyTheSimulationAuthorityWrites)
{
    Entities.SetResource(SimulationAuthority{ .Authoritative = false, .TickOffset = 0 });
    EXPECT_EQ(Grant(Invocation(Target, Unlocked)), VerbAdmission::Refused);
    EXPECT_EQ(TagsOf(Target), nullptr);
}

TEST_F(GameplayTagVerbTest, StacksOutsideTheirRangeAreInvalid)
{
    EXPECT_EQ(Grant(Invocation(Target, Unlocked, 0)), VerbAdmission::InvalidArguments);
    EXPECT_EQ(Grant(Invocation(Target, Unlocked, 0x10000)), VerbAdmission::InvalidArguments);
    EXPECT_EQ(Grant(Invocation(Target, GameplayTagId{})), VerbAdmission::InvalidArguments);
}

TEST_F(GameplayTagVerbTest, ABaselineGrantAtSaturationLeavesTheEffectsStacksAndTheirTag)
{
    Entities.AddComponent<GameplayTagContainer>(Target);
    for (int i = 0; i < 10; ++i)
        ASSERT_TRUE(ApplyEffect(Entities, Target, Burn, FixedSimTime{}).IsValid());
    EXPECT_EQ(Grant(Invocation(Target, Burning, 0xFFFF)), VerbAdmission::Accepted);
    EXPECT_EQ(TagsOf(Target)->ContributedCount(Burning), 10u);

    EXPECT_EQ(Revoke(Invocation(Target, Burning, 0xFFFF)), VerbAdmission::Accepted);
    EXPECT_EQ(TagsOf(Target)->StackCount(Burning), 10u) << "ten effects still hold it";
}

TEST_F(GameplayTagVerbTest, GrantBaselineTagGivesAContainerAndRefusesAndCountsWhatWillNotFit)
{
    EXPECT_EQ(GrantBaselineTag(Entities, Target, Unlocked), GameplayTagWrite::Added);
    EXPECT_EQ(TagsOf(Target)->BaselineCount(Unlocked), 1u);
    EXPECT_EQ(GrantBaselineTag(Entities, Target, Unlocked, 2), GameplayTagWrite::Restacked);
    for (std::uint8_t i = 1; i < GameplayTagContainer::Capacity; ++i)
        ASSERT_EQ(GrantBaselineTag(Entities, Target, *Tags->RegisterTag("Fill.T" + std::to_string(i))),
                  GameplayTagWrite::Added);

    EXPECT_EQ(GrantBaselineTag(Entities, Target, Burning), GameplayTagWrite::Full);
    EXPECT_FALSE(TagsOf(Target)->HasExact(Burning));
    ASSERT_TRUE(Entities.HasResource<GameplayTagOverflow>());
    EXPECT_EQ(Entities.GetResource<GameplayTagOverflow>().FirstTag, Burning);
}

TEST_F(GameplayTagVerbTest, RevokeBaselineTagTakesAtMostWhatIsKeptAndNothingAnEffectHolds)
{
    EXPECT_FALSE(RevokeBaselineTag(Entities, Target, Unlocked, 1)) << "no container, nothing kept";
    Entities.AddComponent<GameplayTagContainer>(Target);
    (void)ApplyEffect(Entities, Target, Burn, FixedSimTime{});
    EXPECT_FALSE(RevokeBaselineTag(Entities, Target, Burning, 1));
    EXPECT_TRUE(TagsOf(Target)->HasExact(Burning));

    (void)GrantBaselineTag(Entities, Target, Unlocked, 3);
    EXPECT_TRUE(RevokeBaselineTag(Entities, Target, Unlocked, 0xFFFF));
    EXPECT_FALSE(TagsOf(Target)->HasExact(Unlocked));
}
