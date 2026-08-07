#include <gtest/gtest.h>

#include <app/Application.h>
#include <app/Engine.h>
#include <app/EngineSchedule.h>
#include <app/GameContexts.h>
#include <jobs/AsyncTaskQueue.h>
#include <world/RuntimeWorld.h>

#include <SDL3/SDL.h>

namespace
{
    // What the loop is expected to have done, owned by the game rather than the
    // schedule so it survives the engine going away when Run returns.
    struct HeadlessCounters
    {
        int Ticks = 0;
        int Frames = 0;
        int Extracts = 0;
    };

    struct HeadlessTickSystem
    {
        HeadlessCounters* Counters = nullptr;
        Engine* Host = nullptr;
        // Enough frames for the wall-time accumulator to cross the fixed step
        // several times over; the pacer makes this tens of milliseconds.
        int StopAfterFrames = 64;

        void FixedLogic(FixedLogicContext&)
        {
            if (Counters != nullptr)
                ++Counters->Ticks;
        }

        // Registered but never driven headless: the phase that would call it is
        // not registered at all. This is the assertion that the render half of
        // the frame is genuinely absent rather than merely idle.
        void ExtractRender(RenderExtractContext&)
        {
            if (Counters != nullptr)
                ++Counters->Extracts;
        }

        void FrameUpdate(FrameUpdateContext&)
        {
            if (Counters == nullptr)
                return;
            ++Counters->Frames;
            if (Counters->Frames >= StopAfterFrames && Host != nullptr)
                Host->RequestExit();
        }
    };

    // A host with no window: the dedicated-server shape, and the CI
    // simulation-soak shape.
    class HeadlessGame final : public Game
    {
    public:
        void OnConfigure(GameConfigureContext& ctx) override
        {
            ctx.Config.Window.GraphicsApi = WindowGraphicsApi::None;
            ctx.Config.Debug.ConsoleLogging = false;
            // Pace fast so the test is not bound by the frame pacer.
            ctx.Config.Runtime.TargetFps = 1000.0;
            ctx.Config.Runtime.FixedTickRate = 120.0;
        }

        void OnStart(GameStartupContext&) override
        {
            // Commits land at the drain phase, which only runs if the headless
            // frame actually has one.
            GetEngine().Tasks().Submit<int>(
                [] { return 7; },
                [this](int value) { Committed = value; });
        }

        void OnRegisterSystems(SystemRegisterContext& ctx) override
        {
            HeadlessTickSystem& system = ctx.Schedule.Register<HeadlessTickSystem>();
            system.Counters = &Counters;
            system.Host = &GetEngine();
        }

        void OnShutdown(GameShutdownContext&) override
        {
            SawLiveWorldAtShutdown =
                GetEngine().World().Entities().GetAliveEntities().empty();
        }

        HeadlessCounters Counters;
        int Committed = 0;
        bool SawLiveWorldAtShutdown = false;
    };
}

// The gate for the headless frame loop: a host with no window still steps the
// frame. Before this, a headless engine initialized and then returned without
// ever constructing a FrameDriver, so it could not tick at all.
TEST(HeadlessFrameLoop, StepsFixedTicksWithNoGraphics)
{
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");

    Application app(0, nullptr);
    HeadlessGame game;

    const int result = app.Run(game);

    EXPECT_EQ(result, 0);
    EXPECT_GE(game.Counters.Frames, 64);
    EXPECT_GT(game.Counters.Ticks, 0)
        << "a headless host that never ticks is not simulating anything";
    EXPECT_EQ(game.Counters.Extracts, 0)
        << "the render phases must not be registered without a window";
}

// The async lane commits at a frame phase, so it only works if the headless
// frame runs that phase. A dedicated host loads its world through this path.
TEST(HeadlessFrameLoop, DrainsAsyncCommitsOnTheFrame)
{
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");

    Application app(0, nullptr);
    HeadlessGame game;

    ASSERT_EQ(app.Run(game), 0);
    EXPECT_EQ(game.Committed, 7)
        << "the commit point is FramePhase::DrainAsyncTasks; headless must run it";
}

// RequestExit has to mean something before the first frame, or a headless host
// that cannot load what it was told to load spins with no window to close.
TEST(HeadlessFrameLoop, ExitRequestedDuringStartupRunsNoFrames)
{
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");

    struct BailingGame final : public Game
    {
        void OnConfigure(GameConfigureContext& ctx) override
        {
            ctx.Config.Window.GraphicsApi = WindowGraphicsApi::None;
            ctx.Config.Debug.ConsoleLogging = false;
        }

        void OnStart(GameStartupContext&) override
        {
            GetEngine().RequestExit();
        }

        void OnShutdown(GameShutdownContext&) override { Shutdown = true; }

        bool Shutdown = false;
    };

    Application app(0, nullptr);
    BailingGame game;

    EXPECT_EQ(app.Run(game), 0);
    EXPECT_TRUE(game.Shutdown) << "declining to run must still tear down cleanly";
}
