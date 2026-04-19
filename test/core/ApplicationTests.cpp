#include <gtest/gtest.h>
#include <app/Application.h>

namespace
{
    class LifecycleGame final : public Game
    {
    public:
        void OnConfigure(GameConfigureContext& ctx) override
        {
            ++ConfigureCalls;
            ctx.Config.App.Name = "Configured Game";
            ctx.Config.Window.GraphicsApi = WindowGraphicsApi::None;
            ctx.Config.Debug.ConsoleLogging = false;
            ctx.Config.Runtime.TargetFps = 144.0;
            ctx.Config.Runtime.FixedTickRate = 120.0;
        }

        void OnStart(GameStartupContext& ctx) override
        {
            ++StartCalls;
            SawConfiguredName = ctx.Config.App.Name == "Configured Game";
            SawEngineConfig = ctx.EngineInstance.Config().Runtime.TargetFps == 144.0;
            SawFixedTickRate =
                ctx.EngineInstance.Runtime().GetSimulationClock().GetFixedDt() == 1.0 / 120.0;
        }

        void OnShutdown(GameShutdownContext& ctx) override
        {
            ++ShutdownCalls;
            SawShutdownConfig = ctx.Config.Runtime.TargetFps == 144.0;
        }

        int ConfigureCalls = 0;
        int StartCalls = 0;
        int ShutdownCalls = 0;
        bool SawConfiguredName = false;
        bool SawEngineConfig = false;
        bool SawShutdownConfig = false;
        bool SawFixedTickRate = false;
    };

    class OwnedLifetimeGame final : public Game
    {
    public:
        ~OwnedLifetimeGame() override
        {
            if (EngineSeen != nullptr)
                DestructorSawLiveEngine = EngineSeen->IsInitialized();
        }

        void OnConfigure(GameConfigureContext& ctx) override
        {
            ctx.Config.Window.GraphicsApi = WindowGraphicsApi::None;
            ctx.Config.Debug.ConsoleLogging = false;
        }

        void OnStart(GameStartupContext& ctx) override
        {
            EngineSeen = &ctx.EngineInstance;
        }

        static inline Engine* EngineSeen = nullptr;
        static inline bool DestructorSawLiveEngine = false;
    };
}

TEST(Application, RunInvokesGameLifecycleWithConfiguredEngineConfig)
{
    Application app(0, nullptr);
    LifecycleGame game;

    const int result = app.Run(game);

    EXPECT_EQ(result, 0);
    EXPECT_EQ(game.ConfigureCalls, 1);
    EXPECT_EQ(game.StartCalls, 1);
    EXPECT_EQ(game.ShutdownCalls, 1);
    EXPECT_TRUE(game.SawConfiguredName);
    EXPECT_TRUE(game.SawEngineConfig);
    EXPECT_TRUE(game.SawShutdownConfig);
    EXPECT_TRUE(game.SawFixedTickRate);
    EXPECT_EQ(app.Config().App.Name, "Configured Game");
}

TEST(Application, ConfigureMutatesEngineConfigBeforeRun)
{
    Application app(0, nullptr);
    app.Configure([](EngineConfig& config) {
        config.App.Name = "Configured Before Run";
        config.Window.Width = 1920;
    });

    EXPECT_EQ(app.Config().App.Name, "Configured Before Run");
    EXPECT_EQ(app.Config().Window.Width, 1920u);
}

TEST(Application, OwnedRunDestroysGameBeforeEngineShutdown)
{
    OwnedLifetimeGame::EngineSeen = nullptr;
    OwnedLifetimeGame::DestructorSawLiveEngine = false;

    Application app(0, nullptr);
    const int result = app.Run<OwnedLifetimeGame>();

    EXPECT_EQ(result, 0);
    EXPECT_TRUE(OwnedLifetimeGame::DestructorSawLiveEngine);
}
