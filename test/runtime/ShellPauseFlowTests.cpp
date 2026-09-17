#include <gtest/gtest.h>

#include <app/BackRouter.h>
#include <app/EngineSchedule.h>
#include <app/PauseInputSystem.h>
#include <app/PauseState.h>
#include <assets/data/DataAssetCache.h>
#include <core/config/EngineConfig.h>
#include <core/logging/LoggingProvider.h>
#include <ecs/StoragePartitionSet.h>
#include <ecs/World.h>
#include <input/InputActionResolveSystem.h>
#include <input/InputActionState.h>
#include <input/InputContextSet.h>
#include <input/InputRegistration.h>
#include <input/ShellInputActions.h>
#include <runtime/RuntimeFrameLoop.h>

#include <SDL3/SDL.h>

#include <chrono>
#include <time/TimeService.h>

// The path from a keypress to a stopped simulation, with no game content of any
// kind: no action set, no profile, no context, no pause code. This is the claim
// the whole design is for -- backing out of gameplay is something a Sencha
// application has, not something each game assembles -- so it is worth a test
// that supplies nothing and still gets it.
//
// The menu itself needs a window and is not here; what is here is everything
// underneath it.

namespace
{
class ShellFlowFixture : public ::testing::Test
{
protected:
    void SetUp() override
    {
        Partitions.Add(StoragePartitionId::Default());
        Runtime.GetWallClock().SetNowSource([this] { return Now; });

        // Exactly what Engine::Run registers, and nothing a game would add.
        RegisterInputSystems(Schedule, Cache, Logging, &Runtime.GetDiscontinuityBus());
        Schedule.Register<PauseInputSystem>(Pause, Router);
        Schedule.After<PauseInputSystem, InputActionResolveSystem>();
        Schedule.Init();

        // Stands in for the menu's fallback consumer: with nothing open, Back
        // pauses.
        Fallback = Router.AddConsumer("test_shell", BackPriority::Fallback, [this] {
            ++Opened;
            Pause.Request(PausePhase::Paused);
            return true;
        });
    }

    // One rendered frame, driven the way the loop drives it: advance the
    // clock, take the tick budget, run PreSimulate, then run whatever budget
    // survived it. Returns the ticks that actually ran.
    int RunFrame(double wallSeconds = 1.0 / 60.0)
    {
        Now += std::chrono::duration_cast<TimeService::Clock::duration>(
            std::chrono::duration<double>(wallSeconds));

        Runtime.BeginFrame();
        Runtime.ResolveLifecycleTransitions();
        Runtime.ScheduleFixedTicks();

        PreSimulateContext preSimulate{
            .Config = Config,
            .Runtime = Runtime,
            .Input = Input,
            .Entities = WorldState,
            .Partitions = Partitions,
        };
        Schedule.RunPreSimulate(preSimulate);

        int ticks = 0;
        while (Runtime.CanRunFixedTickThisFrame())
        {
            const FixedSimTime time = Runtime.BeginFixedTick();
            FixedLogicContext fixed{
                .Config = Config,
                .Runtime = Runtime,
                .Time = time,
                .Entities = WorldState,
                .Partitions = Partitions,
            };
            Schedule.RunFixedLogic(fixed);
            Runtime.EndFixedTick();
            ++ticks;
        }

        Runtime.BuildPresentationFrame();
        Runtime.EndFrame();

        Input.MouseDeltaX = 0.0f;
        Input.MouseDeltaY = 0.0f;
        Input.ClearEdges();
        return ticks;
    }

    void PressKey(std::uint16_t scancode)
    {
        Input.SetKeyHeld(scancode, true);
        Input.KeysPressed.push_back(scancode);
    }
    void ReleaseKey(std::uint16_t scancode)
    {
        Input.SetKeyHeld(scancode, false);
        Input.KeysReleased.push_back(scancode);
    }

    LoggingProvider Logging;
    DataAssetCache Cache;
    EngineConfig Config;
    RuntimeFrameLoop Runtime;
    InputFrame Input;
    World WorldState;
    StoragePartitionSet Partitions;
    EngineSchedule Schedule;
    PauseState Pause;
    BackRouter Router;
    BackConsumerLease Fallback;
    int Opened = 0;
    TimeService::TimePoint Now{};
};
}

TEST_F(ShellFlowFixture, EscapePausesAGameThatShipsNoInputContent)
{
    // The first frame establishes the clock baseline and covers no time.
    ASSERT_EQ(RunFrame(0.0), 0);
    // Then enough wall time for the accumulator to be emitting ticks.
    ASSERT_GT(RunFrame(1.0 / 30.0), 0);
    ASSERT_EQ(Opened, 0);

    PressKey(SDL_SCANCODE_ESCAPE);
    const int ticksOnTheEscapeFrame = RunFrame(1.0 / 30.0);

    EXPECT_EQ(Opened, 1) << "Escape reached nothing: the shell's own action did not resolve";
    EXPECT_TRUE(Pause.IsPaused());

    // The contract, literally: once a frame recognises the request, no further
    // gameplay simulation begins.
    EXPECT_EQ(ticksOnTheEscapeFrame, 0)
        << "the frame that recognised the pause ran its already-budgeted ticks anyway";
    EXPECT_TRUE(Runtime.IsSimulationSuspended());
}

TEST_F(ShellFlowFixture, APausedSimulationRunsNoFurtherTicks)
{
    ASSERT_EQ(RunFrame(0.0), 0);
    PressKey(SDL_SCANCODE_ESCAPE);
    (void)RunFrame();
    ReleaseKey(SDL_SCANCODE_ESCAPE);

    int ticks = 0;
    for (int frame = 0; frame < 30; ++frame)
        ticks += RunFrame();
    EXPECT_EQ(ticks, 0) << "a suspended simulation kept advancing";
}

TEST_F(ShellFlowFixture, ResumingRunsNoCatchUpBurst)
{
    ASSERT_EQ(RunFrame(0.0), 0);
    PressKey(SDL_SCANCODE_ESCAPE);
    (void)RunFrame();
    ReleaseKey(SDL_SCANCODE_ESCAPE);

    // A long stay in the menu. Wall time passes; simulated time must not owe
    // any of it back.
    for (int frame = 0; frame < 60; ++frame)
        (void)RunFrame();

    Pause.Request(PausePhase::Playing);
    const int onResume = RunFrame(1.0 / 30.0);
    EXPECT_EQ(onResume, 0) << "the resuming frame simulated before the state was applied";

    // One frame's worth of wall time is two ticks at 60 Hz. The menu was up for
    // a second: a catch-up burst would be sixty, and the per-frame cap would
    // still let four through.
    const int afterResume = RunFrame(1.0 / 30.0);
    EXPECT_GE(afterResume, 1) << "the simulation did not start again";
    EXPECT_LE(afterResume, 2)
        << "resuming replayed the time the menu was up as a catch-up burst";
}

TEST_F(ShellFlowFixture, BackTakenBySomethingElseDoesNotStopTheFrame)
{
    // Back is not a synonym for pause. An inventory closing on it must leave
    // the frame simulating, which is why cancellation follows the transition
    // rather than the keystroke.
    int inventoryClosed = 0;
    BackConsumerLease inventory = Router.AddConsumer("inventory", BackPriority::Surface,
        [&inventoryClosed] { ++inventoryClosed; return true; });

    ASSERT_EQ(RunFrame(0.0), 0);
    ASSERT_GT(RunFrame(1.0 / 30.0), 0);
    PressKey(SDL_SCANCODE_ESCAPE);
    const int ticks = RunFrame(1.0 / 30.0);

    EXPECT_EQ(inventoryClosed, 1);
    EXPECT_EQ(Opened, 0) << "the shell opened behind a consumer that had taken Back";
    EXPECT_FALSE(Pause.IsPaused());
    EXPECT_GT(ticks, 0) << "closing an inventory turned the frame into a zero-tick frame";
}
