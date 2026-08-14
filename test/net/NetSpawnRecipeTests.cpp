#include <gtest/gtest.h>

#include <ecs/EntityId.h>
#include <ecs/World.h>
#include <net/NetSpawnRecipe.h>

//=============================================================================
// NetSpawnRecipes
//
// A recipe is how a replicated entity becomes something this machine can draw.
// Every failure here is invisible in the same way: the entity arrives, its
// state is correct, and nothing presents it -- so the registry refuses what it
// cannot honour rather than accepting it and going quiet.
//=============================================================================

namespace
{
    NetSpawnRecipes::Builder NoOp()
    {
        return [](World&, EntityId) {};
    }
}

TEST(NetSpawnRecipes, RegisteringAnIdOnceTakesIt)
{
    NetSpawnRecipes recipes;
    EXPECT_TRUE(recipes.Register(1, NoOp()));
    EXPECT_TRUE(recipes.Contains(1));
    EXPECT_EQ(recipes.Size(), 1u);
}

// Two features that both chose the same number is the case this exists to
// catch. Whichever one lost would otherwise wear the other's presentation, in a
// build where both look correct on their own.
TEST(NetSpawnRecipes, ASecondClaimOnAnIdIsRefusedRatherThanReplacing)
{
    NetSpawnRecipes recipes;
    bool firstRan = false;
    bool secondRan = false;

    ASSERT_TRUE(recipes.Register(4, [&firstRan](World&, EntityId) { firstRan = true; }));
    EXPECT_FALSE(recipes.Register(4, [&secondRan](World&, EntityId) { secondRan = true; }));

    World world;
    const EntityId entity = world.CreateEntity();
    EXPECT_TRUE(recipes.Build(4, world, entity));
    EXPECT_TRUE(firstRan) << "the registration that won was not the one that ran";
    EXPECT_FALSE(secondRan);
    EXPECT_EQ(recipes.Size(), 1u);
}

// Zero is the absence of a recipe, so it cannot name one. Refused in every
// build rather than asserted, because a release build that accepted it would
// store a recipe Build() can never reach.
TEST(NetSpawnRecipes, ZeroCannotNameARecipe)
{
    NetSpawnRecipes recipes;
    EXPECT_FALSE(recipes.Register(kNetNoSpawnRecipe, NoOp()));
    EXPECT_FALSE(recipes.Contains(kNetNoSpawnRecipe));
    EXPECT_EQ(recipes.Size(), 0u);
}

TEST(NetSpawnRecipes, ABuilderThatIsNotThereIsRefused)
{
    NetSpawnRecipes recipes;
    EXPECT_FALSE(recipes.Register(2, nullptr));
    EXPECT_FALSE(recipes.Contains(2));
}

TEST(NetSpawnRecipes, AnIdNobodyRegisteredBuildsNothingAndSaysSo)
{
    NetSpawnRecipes recipes;
    World world;
    const EntityId entity = world.CreateEntity();
    EXPECT_FALSE(recipes.Build(9, world, entity));
    EXPECT_TRUE(world.IsAlive(entity)) << "a missing recipe is not fatal to the entity";
}

// A reload re-registers from nothing, which is why replacement never had to be
// how an id changes hands.
TEST(NetSpawnRecipes, ClearingReleasesTheIdsForReregistration)
{
    NetSpawnRecipes recipes;
    ASSERT_TRUE(recipes.Register(3, NoOp()));
    EXPECT_FALSE(recipes.Register(3, NoOp()));

    recipes.Clear();
    EXPECT_FALSE(recipes.Contains(3));
    EXPECT_TRUE(recipes.Register(3, NoOp()));
}
