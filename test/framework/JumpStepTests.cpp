#include <gtest/gtest.h>

#include <movement/JumpExecutionSystem.h>
#include <movement/JumpState.h>
#include <movement/MovementComponents.h>

//=============================================================================
// Whether a character leaves the ground, decided from state and input alone.
//
// The reason this is a pure function rather than a queue and a pair of timed
// effects: a client simulating its own movement replays the ticks the authority
// has not answered yet, and a replayed tick has to reach the same verdict the
// live tick reached. State can be restored and re-read. An effect that lives as
// its own entity cannot be re-created inside reconciliation.
//=============================================================================

namespace
{
    constexpr float kDt = 1.0f / 60.0f;

    SupportState Standing()
    {
        SupportState support;
        support.Kind = SupportKind::Stable;
        return support;
    }

    SupportState Airborne()
    {
        SupportState support;
        support.Kind = SupportKind::None;
        return support;
    }
}

TEST(JumpStep, LaunchesFromStableSupportAtTheResolvedSpeed)
{
    ResolvedMovementTuning tuning;
    tuning.JumpSpeed = 6.0f;
    JumpState state;
    float up = 0.0f;

    EXPECT_TRUE(StepJump(Standing(), tuning, true, kDt, state, up));
    EXPECT_FLOAT_EQ(up, 6.0f);
}

TEST(JumpStep, RefusesWithNothingUnderfoot)
{
    ResolvedMovementTuning tuning;
    JumpState state;
    float up = 0.0f;

    EXPECT_FALSE(StepJump(Airborne(), tuning, true, kDt, state, up))
        << "a jump off nothing is a second jump, which this gate exists to stop";
    EXPECT_FLOAT_EQ(state.CooldownRemaining, 0.0f)
        << "a refused jump must not spend the cooldown, or holding the key "
           "while airborne would lock out the landing";
}

// The reason a held key does not stutter-jump: the cooldown outlives the tick
// that spent it.
TEST(JumpStep, HoldingTheKeyDoesNotJumpEveryTick)
{
    ResolvedMovementTuning tuning;
    tuning.JumpCooldownSeconds = 0.15f;
    JumpState state;
    float up = 0.0f;

    ASSERT_TRUE(StepJump(Standing(), tuning, true, kDt, state, up));
    EXPECT_FLOAT_EQ(state.CooldownRemaining, 0.15f);

    int fired = 0;
    for (int tick = 0; tick < 8; ++tick)
    {
        if (StepJump(Standing(), tuning, true, kDt, state, up))
            ++fired;
    }
    EXPECT_EQ(fired, 0) << "the cooldown did not hold across the ticks it covers";
}

TEST(JumpStep, TheCooldownRunsDownOnEveryStep)
{
    ResolvedMovementTuning tuning;
    tuning.JumpCooldownSeconds = 0.15f;
    JumpState state;
    float up = 0.0f;

    ASSERT_TRUE(StepJump(Standing(), tuning, true, kDt, state, up));

    // Nine ticks at a sixtieth is past a tenth of a second and a half, so the
    // next ask lands.
    int steps = 0;
    while (steps < 60 && !StepJump(Standing(), tuning, true, kDt, state, up))
        ++steps;

    EXPECT_LT(steps, 60) << "the cooldown never expired";
    EXPECT_GE(steps, 8) << "the cooldown expired sooner than it was set for";
}

// A step is a step: time passes whether or not the player asked for anything,
// or a cooldown started before a quiet stretch would outlive it.
TEST(JumpStep, TimePassesOnStepsThatAskForNothing)
{
    ResolvedMovementTuning tuning;
    tuning.JumpCooldownSeconds = 0.15f;
    JumpState state;
    float up = 0.0f;

    ASSERT_TRUE(StepJump(Standing(), tuning, true, kDt, state, up));
    const float afterLaunch = state.CooldownRemaining;

    for (int tick = 0; tick < 4; ++tick)
        EXPECT_FALSE(StepJump(Standing(), tuning, false, kDt, state, up));

    EXPECT_LT(state.CooldownRemaining, afterLaunch);
}
