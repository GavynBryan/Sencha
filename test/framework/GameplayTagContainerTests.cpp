#include <gameplay_tags/GameplayTagContainer.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <ecs/World.h>

#include <gtest/gtest.h>

namespace
{
    // A registry pre-populated with a small hierarchy used across the tests.
    struct TagFixture
    {
        GameplayTagRegistry Reg;
        GameplayTagId Root    = *Reg.RegisterTag("State.Stunned.Root"); // creates State, State.Stunned
        GameplayTagId Stunned = Reg.FindTag("State.Stunned");
        GameplayTagId State   = Reg.FindTag("State");
        GameplayTagId Dash    = *Reg.RegisterTag("Ability.Dash");
    };
}

TEST(GameplayTagContainer, GrantRevokeStackTransitions)
{
    TagFixture f;
    GameplayTagContainer c{};

    EXPECT_TRUE(c.Empty());
    EXPECT_TRUE(c.Grant(f.Root));        // absent -> present reports true
    EXPECT_FALSE(c.Grant(f.Root));       // additional stack reports false
    EXPECT_EQ(c.StackCount(f.Root), 2u);
    EXPECT_TRUE(c.HasExact(f.Root));

    EXPECT_FALSE(c.Revoke(f.Root));      // one stack remains
    EXPECT_EQ(c.StackCount(f.Root), 1u);
    EXPECT_TRUE(c.Revoke(f.Root));       // last stack -> newly absent reports true
    EXPECT_FALSE(c.HasExact(f.Root));
    EXPECT_TRUE(c.Empty());
}

// D9: a contribution's revoke never takes what persists, and only the baseline's
// own revoke does.
TEST(GameplayTagContainer, AContributionNeverWithdrawsTheBaseline)
{
    TagFixture f;
    GameplayTagContainer c{};
    EXPECT_TRUE(c.GrantBaseline(f.Dash));
    EXPECT_FALSE(c.Grant(f.Dash, 2));
    EXPECT_EQ(c.StackCount(f.Dash), 3u);
    EXPECT_EQ(c.BaselineCount(f.Dash), 1u);

    EXPECT_FALSE(c.Revoke(f.Dash, 5)) << "only the two contributed stacks go";
    EXPECT_EQ(c.StackCount(f.Dash), 1u);
    EXPECT_FALSE(c.Revoke(f.Dash));
    EXPECT_TRUE(c.HasExact(f.Dash));

    c.Grant(f.Dash);
    EXPECT_FALSE(c.RevokeBaseline(f.Dash)) << "the contribution keeps it present";
    EXPECT_EQ(c.BaselineCount(f.Dash), 0u);
    EXPECT_TRUE(c.Revoke(f.Dash));
    EXPECT_TRUE(c.Empty());
}

TEST(GameplayTagContainer, EraseRemovesATagWhateverHoldsIt)
{
    TagFixture f;
    GameplayTagContainer c{};
    c.GrantBaseline(f.Dash);
    c.Grant(f.Dash);
    c.Grant(f.Root);
    EXPECT_TRUE(c.Erase(f.Dash));
    EXPECT_FALSE(c.HasExact(f.Dash));
    EXPECT_TRUE(c.HasExact(f.Root));
    EXPECT_FALSE(c.Erase(f.Dash));
}

// A projection's tags are its alone, so it sets their contribution outright:
// exact whatever was there, idempotent, and never touching the baseline.
TEST(GameplayTagContainer, SettingAContributionMakesItExact)
{
    TagFixture f;
    GameplayTagContainer c{};
    EXPECT_TRUE(c.SetContribution(f.Dash, 1));
    EXPECT_EQ(c.StackCount(f.Dash), 1u);
    EXPECT_FALSE(c.SetContribution(f.Dash, 1)) << "already so";
    c.Grant(f.Dash, 2);
    EXPECT_TRUE(c.SetContribution(f.Dash, 1));
    EXPECT_EQ(c.StackCount(f.Dash), 1u) << "repaired";
    c.GrantBaseline(f.Root);
    EXPECT_TRUE(c.SetContribution(f.Root, 2));
    EXPECT_EQ(c.StackCount(f.Root), 3u);
    EXPECT_EQ(c.BaselineCount(f.Root), 1u);
    EXPECT_TRUE(c.SetContribution(f.Root, 0));
    EXPECT_EQ(c.StackCount(f.Root), 1u) << "the baseline stays";
    EXPECT_TRUE(c.SetContribution(f.Dash, 0));
    EXPECT_FALSE(c.HasExact(f.Dash));
    EXPECT_FALSE(c.SetContribution(f.Dash, 0));
    EXPECT_EQ(c.ContributedCount(f.Root), 0u);
}

TEST(GameplayTagContainer, HierarchicalQueryMatchesAncestors)
{
    TagFixture f;
    GameplayTagContainer c{};
    c.Grant(f.Root);

    EXPECT_FALSE(c.HasExact(f.Stunned));            // we hold Root, not Stunned exactly
    EXPECT_TRUE(c.HasDescendantOf(f.Reg, f.Stunned)); // ...but Root satisfies a Stunned query
    EXPECT_TRUE(c.HasDescendantOf(f.Reg, f.State));
    EXPECT_FALSE(c.HasDescendantOf(f.Reg, f.Dash));
}

TEST(GameplayTagContainer, StaysSortedRegardlessOfInsertOrder)
{
    TagFixture f;
    GameplayTagContainer c{};
    GameplayTagId a = *f.Reg.RegisterTag("A");
    GameplayTagId b = *f.Reg.RegisterTag("B.C");
    GameplayTagId d = *f.Reg.RegisterTag("D");

    c.Grant(d);
    c.Grant(a);
    c.Grant(b);

    EXPECT_EQ(c.Size(), 3u);
    EXPECT_TRUE(c.HasExact(a) && c.HasExact(b) && c.HasExact(d));
    for (int i = 1; i < c.Count; ++i)
        EXPECT_LT(c.Tags[i - 1].Value, c.Tags[i].Value);
}

TEST(GameplayTagContainer, InvalidAndZeroStackAreNoOps)
{
    TagFixture f;
    GameplayTagContainer c{};
    EXPECT_FALSE(c.Grant(GameplayTagId{}));   // invalid id
    EXPECT_FALSE(c.Grant(f.Dash, 0));         // zero stacks
    EXPECT_TRUE(c.Empty());
    EXPECT_FALSE(c.Revoke(f.Dash));           // revoke absent tag
}

TEST(GameplayTagContainer, IntegratesAsEcsComponentAndRegistryAsResource)
{
    World world;
    world.RegisterComponent<GameplayTagContainer>();
    GameplayTagRegistry& reg = world.AddResource<GameplayTagRegistry>();
    const GameplayTagId burning = *reg.RegisterTag("State.Burning");

    EXPECT_EQ(world.TryGetResource<GameplayTagRegistry>(), &reg);

    EntityId e = world.CreateEntity();
    world.AddComponent<GameplayTagContainer>(e);

    ASSERT_NE(world.TryGet<GameplayTagContainer>(e), nullptr);
    EXPECT_TRUE(world.TryGet<GameplayTagContainer>(e)->Grant(burning));

    // mutation persists in chunk storage
    EXPECT_TRUE(world.TryGet<GameplayTagContainer>(e)->HasExact(burning));
    EXPECT_EQ(world.TryGet<GameplayTagContainer>(e)->StackCount(burning), 1u);
}
