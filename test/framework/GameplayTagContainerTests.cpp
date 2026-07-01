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
