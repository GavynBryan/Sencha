#include <gtest/gtest.h>

#include <app/PauseState.h>
#include <input/InputBindings.h>
#include <input/InputContextSet.h>
#include <runtime/RuntimeFrameLoop.h>

#include <vector>

// The transition, on its own. No UI, no documents, no page stack -- which is
// the point of keeping this separate from the menu: what pause does to input,
// the pointer and the clock is answerable without any of that.

namespace
{
// A profile with one gameplay context and the shell's, which is the shape every
// compiled profile has.
BoundInputProfile TwoContextProfile()
{
    BoundInputProfile profile;

    InputContextDefinition shell;
    shell.Name = "ui";
    shell.Priority = 1'000'000;
    shell.IsShell = true;
    profile.Contexts.push_back(std::move(shell));

    InputContextDefinition gameplay;
    gameplay.Name = "gameplay";
    gameplay.Priority = 100;
    profile.Contexts.push_back(std::move(gameplay));

    return profile;
}

[[nodiscard]] bool ShellActive(const InputContextSet& contexts, const BoundInputProfile& profile)
{
    std::vector<std::uint8_t> mask;
    contexts.BuildActiveMask(profile, mask);
    return mask[0] != 0;
}

[[nodiscard]] bool GameplayActive(const InputContextSet& contexts, const BoundInputProfile& profile)
{
    std::vector<std::uint8_t> mask;
    contexts.BuildActiveMask(profile, mask);
    return mask[1] != 0;
}
}

TEST(PauseStateTest, EnteringAndLeavingReportTheEdgeAndNothingElseDoes)
{
    RuntimeFrameLoop runtime;
    InputContextSet contexts;
    PauseState pause;

    EXPECT_EQ(pause.Apply(runtime, contexts), PauseTransition::None);

    pause.Request(PausePhase::Paused);
    EXPECT_TRUE(pause.HasPendingRequest());
    EXPECT_EQ(pause.Apply(runtime, contexts), PauseTransition::Entered);
    EXPECT_TRUE(pause.IsPaused());

    // Resolved. A second Apply with nothing asked for is not a second entry.
    EXPECT_EQ(pause.Apply(runtime, contexts), PauseTransition::None);

    pause.Request(PausePhase::Playing);
    EXPECT_EQ(pause.Apply(runtime, contexts), PauseTransition::Left);
    EXPECT_FALSE(pause.IsPaused());
    EXPECT_EQ(pause.Apply(runtime, contexts), PauseTransition::None);
}

TEST(PauseStateTest, RepeatedRequestsCoalesceIntoOneTransition)
{
    // Holding the key, or a focus loss landing beside a Back press, must not
    // queue two entries -- the second would be a transition out of a state
    // nothing was ever in.
    RuntimeFrameLoop runtime;
    InputContextSet contexts;
    PauseState pause;

    pause.Request(PausePhase::Paused);
    pause.Request(PausePhase::Paused);
    pause.Request(PausePhase::Paused);
    EXPECT_EQ(pause.Apply(runtime, contexts), PauseTransition::Entered);
    EXPECT_EQ(pause.Apply(runtime, contexts), PauseTransition::None);
}

TEST(PauseStateTest, AskingForTheStateAlreadyInForceChangesNothing)
{
    RuntimeFrameLoop runtime;
    InputContextSet contexts;
    PauseState pause;

    pause.Request(PausePhase::Playing);
    EXPECT_FALSE(pause.HasPendingRequest());
    EXPECT_EQ(pause.Apply(runtime, contexts), PauseTransition::None);
}

TEST(PauseStateTest, PausingSuspendsGameplayControlsAndLeavesTheShellsAlone)
{
    const BoundInputProfile profile = TwoContextProfile();
    RuntimeFrameLoop runtime;
    InputContextSet contexts;
    InputContextLease gameplay = contexts.Activate("gameplay");
    contexts.ApplyPending();

    PauseState pause;
    ASSERT_TRUE(GameplayActive(contexts, profile));

    pause.Request(PausePhase::Paused);
    ASSERT_EQ(pause.Apply(runtime, contexts), PauseTransition::Entered);
    EXPECT_FALSE(GameplayActive(contexts, profile));
    EXPECT_TRUE(ShellActive(contexts, profile)) << "the action that resumes went quiet";

    pause.Request(PausePhase::Playing);
    ASSERT_EQ(pause.Apply(runtime, contexts), PauseTransition::Left);
    EXPECT_TRUE(GameplayActive(contexts, profile));
    EXPECT_TRUE(gameplay.IsValid()) << "the holder's lease was taken from it";
}

TEST(PauseStateTest, PauseNeverWritesTheTimescale)
{
    // Suspension and rate are separate facts. Recording the rate and restoring
    // it would be a last-writer-wins rule: a game that legitimately changed
    // speed while paused would find the change reverted on resume.
    RuntimeFrameLoop runtime;
    InputContextSet contexts;
    PauseState pause;

    runtime.SetSimulationTimescale(0.5f);
    pause.Request(PausePhase::Paused);
    ASSERT_EQ(pause.Apply(runtime, contexts), PauseTransition::Entered);
    EXPECT_FLOAT_EQ(runtime.GetSimulationTimescale(), 0.5f);
    EXPECT_TRUE(runtime.IsSimulationSuspended());

    // Somebody changes the rate while the world is stopped.
    runtime.SetSimulationTimescale(0.75f);

    pause.Request(PausePhase::Playing);
    ASSERT_EQ(pause.Apply(runtime, contexts), PauseTransition::Left);
    EXPECT_FALSE(runtime.IsSimulationSuspended());
    EXPECT_FLOAT_EQ(runtime.GetSimulationTimescale(), 0.75f)
        << "resuming reverted a rate it never owned";
}

TEST(PauseStateTest, InputOnlyLeavesTheSimulationRunning)
{
    RuntimeFrameLoop runtime;
    InputContextSet contexts;
    PauseState pause;
    pause.SetPolicy(PausePolicy::InputOnly);

    pause.Request(PausePhase::Paused);
    ASSERT_EQ(pause.Apply(runtime, contexts), PauseTransition::Entered);
    EXPECT_FALSE(runtime.IsSimulationSuspended());
    EXPECT_TRUE(contexts.IsSuspended()) << "local controls kept driving the world";
}

TEST(PauseStateTest, ALiveSessionDegradesSuspensionToInputOnlyAtEitherEnd)
{
    // Opening a stock menu must not freeze a match, for a client or for a host.
    // Session shape decides this, not authority: a host does own the simulation,
    // so an authority rule would let it stop everyone's game while the menu
    // claimed to be local -- and would make correctness a per-game chore.
    RuntimeFrameLoop runtime;
    InputContextSet contexts;
    PauseState pause;
    ASSERT_EQ(pause.GetPolicy(), PausePolicy::SuspendSimulation);

    pause.SetSessionLive(true);
    EXPECT_EQ(pause.Effective(), PausePolicy::InputOnly);

    pause.Request(PausePhase::Paused);
    ASSERT_EQ(pause.Apply(runtime, contexts), PauseTransition::Entered);
    EXPECT_FALSE(runtime.IsSimulationSuspended());
    EXPECT_TRUE(contexts.IsSuspended());

    pause.SetSessionLive(false);
    EXPECT_EQ(pause.Effective(), PausePolicy::SuspendSimulation);
}

TEST(PauseStateTest, LeavingDoesNotClearASuspensionItNeverApplied)
{
    // Under InputOnly the shell never suspended the clock, so resuming must not
    // lift a suspension somebody else owns.
    RuntimeFrameLoop runtime;
    InputContextSet contexts;
    PauseState pause;
    pause.SetPolicy(PausePolicy::InputOnly);

    runtime.SetSimulationSuspended(true);   // somebody else's
    pause.Request(PausePhase::Paused);
    ASSERT_EQ(pause.Apply(runtime, contexts), PauseTransition::Entered);
    pause.Request(PausePhase::Playing);
    ASSERT_EQ(pause.Apply(runtime, contexts), PauseTransition::Left);

    EXPECT_TRUE(runtime.IsSimulationSuspended())
        << "resuming lifted a suspension the shell did not apply";
}

TEST(PauseStateTest, PointerCaptureIsSuppressedExactlyWhilePaused)
{
    RuntimeFrameLoop runtime;
    InputContextSet contexts;
    PauseState pause;

    EXPECT_FALSE(pause.SuppressesPointerCapture());
    pause.Request(PausePhase::Paused);
    (void)pause.Apply(runtime, contexts);
    EXPECT_TRUE(pause.SuppressesPointerCapture());
    pause.Request(PausePhase::Playing);
    (void)pause.Apply(runtime, contexts);
    EXPECT_FALSE(pause.SuppressesPointerCapture());
}
