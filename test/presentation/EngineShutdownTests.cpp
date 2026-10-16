// A windowed engine torn down in the states a player leaves it in, against a
// real window and device. Everything Run composes -- the application shell
// above all -- has to be given back by Run, while what it borrows still lives.

#include <app/Engine.h>
#include <app/EngineSchedule.h>
#include <app/Game.h>
#include <app/GameContexts.h>
#include <app/PauseMenu.h>
#include <core/logging/LoggingProvider.h>
#include <ui/UiService.h>

#include <DeviceTestSupport.h>
#include <SDL3/SDL.h>
#include <gtest/gtest.h>

#include <atomic>
#include <cstdlib>
#include <string>

namespace
{
    enum class ShellAtExit
    {
        Closed,
        RootPage,
        PageOverRoot,
    };

    class ShutdownGame;

    struct ExitAfterFrames
    {
        ShutdownGame* Owner = nullptr;
        int Frames = 0;
        void FrameUpdate(FrameUpdateContext&);
    };

    class ShutdownGame final : public Game
    {
    public:
        explicit ShutdownGame(ShellAtExit shell) : Shell(shell) {}

        void OnConfigure(GameConfigureContext& ctx) override
        {
            ctx.Config.App.Name = "Engine Shutdown Test";
            ctx.Config.Window.GraphicsApi = WindowGraphicsApi::Vulkan;
            ctx.Config.Window.Width = 320;
            ctx.Config.Window.Height = 240;
            ctx.Config.Graphics.EnableValidation = true;
            ctx.Config.Debug.ConsoleLogging = false;
            ctx.Config.Runtime.ApplicationShell = true;
            ctx.Config.Runtime.ContentRoots = {};
        }

        void OnRegisterSystems(SystemRegisterContext& ctx) override
        {
            ctx.Schedule.Register<ExitAfterFrames>().Owner = this;
        }

        void OnStart(GameStartupContext&) override
        {
            GetEngine().Logging().AddSink<ValidationErrorCount>(ValidationErrors);
        }

        void OpenShell()
        {
            PauseMenu* menu = GetEngine().TryPauseMenu();
            ASSERT_NE(menu, nullptr) << "a windowed application composes the shell";
            if (Shell == ShellAtExit::Closed)
                return;
            menu->Open();
            ASSERT_TRUE(menu->IsOpen());
            if (Shell != ShellAtExit::PageOverRoot)
                return;

            PauseMenu::Page page;
            page.Desc.PackagePath = "asset://ui/options.rml";
            page.Desc.ModelName = "shutdown_test_page";
            page.Desc.Modal = true;
            page.Desc.Properties = {
                UiModelProperty{ "title", UiValue(std::string("Options")) },
                UiModelProperty{ "hint", UiValue(std::string{}) },
            };
            page.Desc.RowLists = { "rows" };
            page.Desc.Actions = { "options_activate" };
            page.Closed = [this] {
                ++PageClosedCount;
                PageClosedBeforeRunReturned = !RunReturned;
            };
            menu->Push(std::move(page));
            ASSERT_EQ(menu->Depth(), 2u);
        }

        void Exit() { GetEngine().RequestExit(Engine::ExitSource::Menu); }

        ShellAtExit Shell;
        std::atomic<int> ValidationErrors{ 0 };
        bool RunReturned = false;
        int PageClosedCount = 0;
        bool PageClosedBeforeRunReturned = false;
    };

    void ExitAfterFrames::FrameUpdate(FrameUpdateContext&)
    {
        ++Frames;
        if (Frames == 3)
            Owner->OpenShell();
        if (Frames == 10)
            Owner->Exit();
    }

    class EngineShutdownTest : public ::testing::TestWithParam<ShellAtExit>
    {
    protected:
        void SetUp() override
        {
            if (!DisplayAvailable())
                GTEST_SKIP() << "no display; the application shell needs a window";
            setenv("SENCHA_PRESENT_MODE", "IMMEDIATE", 1);
            SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
        }

        // Run and Shutdown on one engine, the way a host that runs twice does.
        static void RunOnce(Engine& engine, ShutdownGame& game)
        {
            ASSERT_EQ(engine.Run(game), 0);
            game.RunReturned = true;
            EXPECT_EQ(engine.TryPauseMenu(), nullptr)
                << "the shell Run composed outlived the run, and the UI service it draws through";
            engine.Shutdown();
            EXPECT_EQ(game.ValidationErrors.load(), 0);
            if (game.Shell == ShellAtExit::PageOverRoot)
            {
                EXPECT_EQ(game.PageClosedCount, 1);
                EXPECT_TRUE(game.PageClosedBeforeRunReturned)
                    << "a page left open is closed by the run that opened it";
            }
        }
    };

    EngineConfig ConfigFor(ShutdownGame& game)
    {
        EngineConfig config;
        GameConfigureContext configure{ .Config = config };
        game.OnConfigure(configure);
        return config;
    }
}

TEST_P(EngineShutdownTest, ExitingTearsTheShellDownBeforeWhatItBorrows)
{
    ShutdownGame game(GetParam());
    Engine engine(ConfigFor(game));
    RunOnce(engine, game);
}

TEST_P(EngineShutdownTest, ASecondRunInOneProcessComposesAndReleasesItsOwnShell)
{
    ShutdownGame first(GetParam());
    ShutdownGame second(GetParam());
    Engine engine(ConfigFor(first));
    RunOnce(engine, first);
    RunOnce(engine, second);
}

INSTANTIATE_TEST_SUITE_P(ShellStates, EngineShutdownTest,
                         ::testing::Values(ShellAtExit::Closed, ShellAtExit::RootPage,
                                           ShellAtExit::PageOverRoot),
                         [](const ::testing::TestParamInfo<ShellAtExit>& param) {
                             switch (param.param)
                             {
                             case ShellAtExit::Closed: return std::string("ShellClosed");
                             case ShellAtExit::RootPage: return std::string("RootPageOpen");
                             case ShellAtExit::PageOverRoot: return std::string("PageOverRootOpen");
                             }
                             return std::string("Unknown");
                         });
