#include <gtest/gtest.h>

#include <framework/gameplay_tags/GameplayTagContainer.h>
#include <framework/gameplay_tags/GameplayTagQuery.h>
#include <framework/gameplay_tags/GameplayTagRegistry.h>
#include <framework/movement/MovementIntent.h>
#include <framework/movement/MovementProfile.h>
#include <framework/movement/MovementState.h>
#include <framework/movement/MovementSystems.h>
#include <framework/movement/MovementTags.h>

namespace
{
    constexpr float kTick = 1.0f / 60.0f;

    MovementIntent Walking()
    {
        MovementIntent intent;
        intent.WishDir = Vec3d(1.0f, 0.0f, 0.0f);
        return intent;
    }
}

TEST(MovementStep, AcceleratesTowardWishOnGround)
{
    MovementState state;
    state.Grounded = true;
    MovementIntent intent = Walking();
    MovementProfile profile;

    StepMovement(state, intent, profile, kTick);

    EXPECT_GT(state.PlanarVelocity.X, 0.0f);
    EXPECT_FLOAT_EQ(state.PlanarVelocity.Y, 0.0f);
    EXPECT_LE(state.PlanarVelocity.X, profile.MaxSpeed);
    EXPECT_EQ(state.JumpRequest, 0.0f);
}

TEST(MovementStep, ConvergesToMaxSpeed)
{
    MovementState state;
    state.Grounded = true;
    MovementIntent intent = Walking();
    MovementProfile profile;

    for (int i = 0; i < 600; ++i)
        StepMovement(state, intent, profile, kTick);

    EXPECT_NEAR(state.PlanarVelocity.X, profile.MaxSpeed, 1e-2f);
}

TEST(MovementStep, SprintRaisesTopSpeed)
{
    MovementProfile profile;
    MovementState walkState;
    walkState.Grounded = true;
    MovementState runState;
    runState.Grounded = true;
    MovementIntent walk = Walking();
    MovementIntent run = Walking();
    run.Sprint = true;

    for (int i = 0; i < 600; ++i)
    {
        StepMovement(walkState, walk, profile, kTick);
        StepMovement(runState, run, profile, kTick);
    }

    EXPECT_GT(runState.PlanarVelocity.X, walkState.PlanarVelocity.X);
    EXPECT_NEAR(runState.PlanarVelocity.X, profile.MaxSpeed * profile.SprintMultiplier, 1e-2f);
}

TEST(MovementStep, GroundFrictionStopsIdleCharacter)
{
    MovementProfile profile;
    MovementState state;
    state.Grounded = true;
    state.PlanarVelocity = Vec3d(profile.MaxSpeed, 0.0f, 0.0f);
    MovementIntent idle;

    for (int i = 0; i < 600; ++i)
        StepMovement(state, idle, profile, kTick);

    EXPECT_NEAR(state.PlanarVelocity.X, 0.0f, 1e-2f);
}

TEST(MovementStep, JumpsFromGroundAndConsumesRequest)
{
    MovementProfile profile;
    MovementState state;
    state.Grounded = true;
    MovementIntent intent;
    intent.JumpQueued = true;

    StepMovement(state, intent, profile, kTick);

    EXPECT_FLOAT_EQ(state.JumpRequest, profile.JumpUpSpeed);
    EXPECT_FALSE(intent.JumpQueued);
}

TEST(MovementStep, NoJumpWhileAirborneWithoutCoyote)
{
    MovementProfile profile;
    MovementState state;
    state.Grounded = false;
    state.CoyoteTimer = 0.0f;
    MovementIntent intent;
    intent.JumpQueued = true;

    StepMovement(state, intent, profile, kTick);

    EXPECT_EQ(state.JumpRequest, 0.0f);
}

TEST(MovementStep, CoyoteGraceAllowsJumpJustAfterLeavingGround)
{
    MovementProfile profile;
    MovementState state;
    state.Grounded = true;
    MovementIntent grounding;
    StepMovement(state, grounding, profile, kTick); // arms coyote while grounded

    state.Grounded = false;
    MovementIntent jump;
    jump.JumpQueued = true;
    StepMovement(state, jump, profile, kTick);

    EXPECT_FLOAT_EQ(state.JumpRequest, profile.JumpUpSpeed);
}

TEST(MovementTagsTest, GroundedHierarchyMatchesWalkingSubstate)
{
    GameplayTagRegistry registry;
    const MovementTags ids = RegisterMovementTags(registry);

    GameplayTagContainer tags{};
    ResolveMovementTags(tags, ids, /*grounded*/ true, Walking());

    EXPECT_TRUE(tags.HasExact(ids.Grounded));
    EXPECT_TRUE(tags.HasExact(ids.GroundedWalking));
    EXPECT_FALSE(tags.HasExact(ids.GroundedIdle));
    EXPECT_FALSE(tags.HasExact(ids.Airborne));

    EXPECT_TRUE(tags.HasDescendantOf(registry, ids.Grounded));

    GameplayTagQuery query;
    query.AddAll(ids.Grounded, GameplayTagMatchMode::Hierarchical);
    EXPECT_TRUE(query.Matches(tags, registry));
}

TEST(MovementTagsTest, SprintingIsAGroundedDescendant)
{
    GameplayTagRegistry registry;
    const MovementTags ids = RegisterMovementTags(registry);

    GameplayTagContainer tags{};
    MovementIntent run = Walking();
    run.Sprint = true;
    ResolveMovementTags(tags, ids, true, run);

    EXPECT_TRUE(tags.HasExact(ids.GroundedSprinting));
    EXPECT_TRUE(tags.HasDescendantOf(registry, ids.Grounded));
}

TEST(MovementTagsTest, LeavingGroundRevokesGroundedSubstates)
{
    GameplayTagRegistry registry;
    const MovementTags ids = RegisterMovementTags(registry);

    GameplayTagContainer tags{};
    ResolveMovementTags(tags, ids, true, Walking());
    ResolveMovementTags(tags, ids, false, Walking());

    EXPECT_TRUE(tags.HasExact(ids.Airborne));
    EXPECT_FALSE(tags.HasExact(ids.Grounded));
    EXPECT_FALSE(tags.HasExact(ids.GroundedWalking));
}

TEST(MovementTagsTest, RepeatedTicksDoNotStackTags)
{
    GameplayTagRegistry registry;
    const MovementTags ids = RegisterMovementTags(registry);

    GameplayTagContainer tags{};
    for (int i = 0; i < 16; ++i)
        ResolveMovementTags(tags, ids, true, Walking());

    EXPECT_EQ(tags.StackCount(ids.Grounded), 1);
    EXPECT_EQ(tags.StackCount(ids.GroundedWalking), 1);
}

TEST(MovementTagsTest, IdleWhenGroundedWithoutInput)
{
    GameplayTagRegistry registry;
    const MovementTags ids = RegisterMovementTags(registry);

    GameplayTagContainer tags{};
    ResolveMovementTags(tags, ids, true, MovementIntent{});

    EXPECT_TRUE(tags.HasExact(ids.GroundedIdle));
    EXPECT_FALSE(tags.HasExact(ids.GroundedWalking));
}
