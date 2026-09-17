#include <gtest/gtest.h>

#include <app/Engine.h>
#include <app/EngineSchedule.h>
#include <app/Game.h>
#include <app/GameContexts.h>
#include <app/PauseInputSystem.h>
#include <core/console/CVarArchive.h>
#include <core/console/ConsoleRegistry.h>
#include <core/console/ConsoleService.h>

#include <SDL3/SDL.h>

#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>

//=============================================================================
// What a host's declared posture buys it, observed through a real Engine.
//
// Two facts are being pinned. A default configuration is an application but
// persists nothing -- so a test fixture, an editor or a headless server that
// never asked for a settings directory cannot read or write a user's real
// one. And a host that says it is not an application gets no shell: no pause
// menu, and no Back reader flipping a pause state nothing presents.
//=============================================================================
namespace
{
    // The environment is process state; a test that changes it puts it back.
    // The user's configuration home is pointed at a scratch directory for the
    // whole test, so that if an engine ever does reach for it again, the
    // evidence lands where the test can see it and not in the developer's home.
    class ScopedEnv
    {
    public:
        ScopedEnv(const char* name, const char* value) : Name(name)
        {
            if (const char* old = std::getenv(name))
                Previous = old;
            setenv(name, value, 1);
        }
        ~ScopedEnv()
        {
            if (Previous.has_value())
                setenv(Name, Previous->c_str(), 1);
            else
                unsetenv(Name);
        }

    private:
        const char* Name;
        std::optional<std::string> Previous;
    };

    struct Scratch
    {
        std::filesystem::path Dir;
        Scratch()
            : Dir(std::filesystem::temp_directory_path()
                  / ("sencha-host-posture-"
                     + std::string(::testing::UnitTest::GetInstance()->current_test_info()->name())))
        {
            std::filesystem::remove_all(Dir);
            std::filesystem::create_directories(Dir / "root");
            std::filesystem::create_directories(Dir / "config");
        }
        ~Scratch()
        {
            std::error_code ec;
            std::filesystem::remove_all(Dir, ec);
        }
        [[nodiscard]] std::filesystem::path Root() const { return Dir / "root"; }
        [[nodiscard]] std::filesystem::path Config() const { return Dir / "config"; }
        [[nodiscard]] bool ConfigIsEmpty() const
        {
            return std::filesystem::is_empty(Config());
        }
    };

    struct StopAfterFrames
    {
        Engine* Host = nullptr;
        int Frames = 0;
        void FrameUpdate(FrameUpdateContext&)
        {
            if (++Frames >= 4 && Host != nullptr)
                Host->RequestExit();
        }
    };

    // Headless, empty content, and whatever posture the test hands it. What it
    // sees of the engine it records at the moment the engine offers it.
    class PostureGame final : public Game
    {
    public:
        std::string Name = "Posture Test Game";
        std::string SettingsRoot;
        bool ApplicationShell = true;
        std::string ContentRoot;
        // Set during the run, when the engine would have to apply it.
        std::optional<double> LookSensitivityToSet;

        bool SawSettingsStore = false;
        std::filesystem::path SettingsFile;
        bool SawBackReader = false;

        void OnConfigure(GameConfigureContext& ctx) override
        {
            ctx.Config.App.Name = Name;
            ctx.Config.Window.GraphicsApi = WindowGraphicsApi::None;
            ctx.Config.Debug.ConsoleLogging = false;
            ctx.Config.Runtime.TargetFps = 1000.0;
            ctx.Config.Runtime.ContentRoots = { ContentRoot };
            ctx.Config.Runtime.ApplicationShell = ApplicationShell;
            ctx.Config.Console.SettingsRoot = SettingsRoot;
        }

        void OnRegisterSystems(SystemRegisterContext& ctx) override
        {
            // The engine registers its own systems before the game's hook, so
            // whether it registered the shell's Back reader is decided by now.
            SawBackReader = ctx.Schedule.Has<PauseInputSystem>();
            ctx.Schedule.Register<StopAfterFrames>().Host = &GetEngine();
        }

        void OnStart(GameStartupContext&) override
        {
            if (const CVarArchive* store = GetEngine().TrySettings())
            {
                SawSettingsStore = true;
                SettingsFile = store->File();
            }
            if (LookSensitivityToSet.has_value())
            {
                (void)GetEngine().Console().Registry().SetCVar(
                    "input.look_sensitivity", CVarValue{ *LookSensitivityToSet },
                    { "test" }, ConsolePhase::EngineReady);
            }
        }
    };

    int RunOnce(PostureGame& game)
    {
        SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
        EngineConfig config;
        GameConfigureContext configure{ .Config = config };
        game.OnConfigure(configure);
        Engine engine(config);
        const int result = engine.Run(game);
        engine.Shutdown();
        return result;
    }
}

TEST(HostPosture, ADefaultConfigurationIsAnApplicationThatPersistsNothing)
{
    const Scratch scratch;
    ScopedEnv xdg("XDG_CONFIG_HOME", scratch.Config().string().c_str());

    PostureGame game;
    game.ContentRoot = scratch.Root().string();
    game.LookSensitivityToSet = 1.75;   // dirties an Archive cvar, so a store would write
    ASSERT_EQ(RunOnce(game), 0);

    EXPECT_TRUE(game.SawBackReader) << "an application reads the shell's Back action";
    EXPECT_FALSE(game.SawSettingsStore)
        << "no settings directory was named, so there is nothing to persist into";
    EXPECT_TRUE(scratch.ConfigIsEmpty())
        << "a host that named no settings directory wrote into the user's configuration home";
}

TEST(HostPosture, ANamedRootPersistsUnderTheGamesOwnName)
{
    const Scratch scratch;
    ScopedEnv xdg("XDG_CONFIG_HOME", scratch.Config().string().c_str());

    PostureGame game;
    game.ContentRoot = scratch.Root().string();
    game.SettingsRoot = (scratch.Dir / "saves").string();
    game.LookSensitivityToSet = 1.75;
    ASSERT_EQ(RunOnce(game), 0);

    ASSERT_TRUE(game.SawSettingsStore);
    EXPECT_EQ(game.SettingsFile, CVarArchive::FileFor(scratch.Dir / "saves", "Posture Test Game"));
    EXPECT_TRUE(std::filesystem::exists(game.SettingsFile))
        << "a changed Archive cvar is written where the host said, at shutdown";
    EXPECT_TRUE(scratch.ConfigIsEmpty()) << "and nowhere else";
}

TEST(HostPosture, ANonApplicationGetsNoBackReader)
{
    const Scratch scratch;
    ScopedEnv xdg("XDG_CONFIG_HOME", scratch.Config().string().c_str());

    PostureGame game;
    game.ContentRoot = scratch.Root().string();
    game.ApplicationShell = false;
    ASSERT_EQ(RunOnce(game), 0);

    EXPECT_FALSE(game.SawBackReader)
        << "a host that is not an application must not have Escape flipping a pause "
           "state nothing presents";
    EXPECT_FALSE(game.SawSettingsStore);
}
