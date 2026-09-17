#include <gtest/gtest.h>

#include <app/Application.h>
#include <app/Game.h>
#include <core/config/EngineConfig.h>

#include <SDL3/SDL.h>

#include <filesystem>
#include <random>
#include <string>
#include <vector>

// Ending a run. The claim under test is the strong one -- a game can intercept
// application exit, not merely change what its own menu button does -- so what
// matters is that every graceful source reaches one gate, and that deferring
// one is a protocol rather than a boolean a caller has to break out of itself.

namespace
{
class EmptyRoot
{
public:
    EmptyRoot()
    {
        std::random_device rd;
        Root = std::filesystem::temp_directory_path()
             / ("sencha_exit_test_" + std::to_string(rd()));
        std::filesystem::create_directories(Root);
    }
    ~EmptyRoot()
    {
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
    }
    EmptyRoot(const EmptyRoot&) = delete;
    EmptyRoot& operator=(const EmptyRoot&) = delete;
    [[nodiscard]] std::string String() const { return Root.generic_string(); }

private:
    std::filesystem::path Root;
};

// A headless game that asks to exit from a chosen source once it is running,
// and records what its handler saw.
class ExitGame : public Game
{
public:
    ExitGame(std::string root, Engine::ExitSource source)
        : Root(std::move(root))
        , Source(source)
    {
    }

    void OnConfigure(GameConfigureContext& ctx) override
    {
        ctx.Config.App.Name = "ExitTest";
        ctx.Config.Window.GraphicsApi = WindowGraphicsApi::None;
        ctx.Config.Runtime.ContentRoots = { Root };
        ctx.Config.Runtime.HasLocalPlayer = false;
        ctx.Config.Console.UiEnabled = false;
        ctx.Config.Runtime.TargetFps = 240.0;
    }

    void OnStart(GameStartupContext&) override
    {
        Engine& engine = GetEngine();
        engine.OnExitRequested = [this](Engine::ExitSource source) {
            Seen.push_back(source);
            if (!Defer)
                return Engine::ExitDecision::Allow;
            // One confirmation, then grant it. A handler that re-entered here
            // would be the recursion this protocol exists to remove.
            Deferred = true;
            return Engine::ExitDecision::Defer;
        };
    }

    void OnRegisterSystems(SystemRegisterContext& ctx) override
    {
        ctx.Schedule.Register<Driver>(*this);
    }

    struct Driver
    {
        explicit Driver(ExitGame& owner) : Owner(&owner) {}

        void FrameUpdate(FrameUpdateContext&)
        {
            Engine& engine = Owner->GetEngineForTest();
            ++Owner->Frames;
            if (Owner->Frames == 3)
            {
                engine.RequestExit(Owner->Source);
                // Repeats while a confirmation is up: a window manager sending
                // close again, or somebody holding the shortcut.
                engine.RequestExit(Owner->Source);
                engine.RequestExit(Owner->Source);
            }
            if (Owner->Deferred && Owner->Frames == 6)
            {
                Owner->ConfirmedAt = Owner->Frames;
                if (Owner->CancelInstead)
                    engine.CancelExit();
                else
                    engine.ConfirmExit();
            }
            // A cancelled run has to stop somehow.
            if (Owner->CancelInstead && Owner->Frames >= 20)
                engine.StopImmediately();
        }

        ExitGame* Owner = nullptr;
    };

    [[nodiscard]] Engine& GetEngineForTest() { return GetEngine(); }

    std::string Root;
    Engine::ExitSource Source = Engine::ExitSource::Game;
    bool Defer = false;
    bool CancelInstead = false;
    bool Deferred = false;
    int Frames = 0;
    int ConfirmedAt = 0;
    std::vector<Engine::ExitSource> Seen;
};
}

TEST(ExitRequest, AGamesHandlerSeesTheRequestAndItsSource)
{
    const EmptyRoot root;
    Application app(0, nullptr);
    ExitGame game(root.String(), Engine::ExitSource::Menu);

    EXPECT_EQ(app.Run(game), 0);
    ASSERT_FALSE(game.Seen.empty());
    EXPECT_EQ(game.Seen.front(), Engine::ExitSource::Menu);
}

TEST(ExitRequest, RepeatedRequestsWhileOneIsPendingDoNotReenterTheHandler)
{
    // The reason Defer is a protocol and not a bool: holding Alt+F4 with a
    // confirmation up would otherwise stack one dialog per event.
    const EmptyRoot root;
    Application app(0, nullptr);
    ExitGame game(root.String(), Engine::ExitSource::WindowClose);
    game.Defer = true;

    EXPECT_EQ(app.Run(game), 0);
    EXPECT_EQ(game.Seen.size(), 1u)
        << "the handler was asked again while it already had a request pending";
    EXPECT_TRUE(game.Deferred);
}

TEST(ExitRequest, DeferringHoldsTheRunUntilItIsConfirmed)
{
    const EmptyRoot root;
    Application app(0, nullptr);
    ExitGame game(root.String(), Engine::ExitSource::WindowClose);
    game.Defer = true;

    EXPECT_EQ(app.Run(game), 0);
    // Asked at frame 3, granted at frame 6: the run kept going in between,
    // which is what a save prompt needs.
    EXPECT_EQ(game.ConfirmedAt, 6);
    EXPECT_GE(game.Frames, 6);
}

TEST(ExitRequest, CancellingADeferredRequestLetsTheRunContinue)
{
    const EmptyRoot root;
    Application app(0, nullptr);
    ExitGame game(root.String(), Engine::ExitSource::WindowClose);
    game.Defer = true;
    game.CancelInstead = true;

    EXPECT_EQ(app.Run(game), 0);
    EXPECT_GE(game.Frames, 20) << "cancelling did not put the run back on its feet";
    EXPECT_EQ(game.Seen.size(), 1u);
}

TEST(ExitRequest, AGameWithNoHandlerExitsOnTheFirstRequest)
{
    const EmptyRoot root;
    Application app(0, nullptr);
    ExitGame game(root.String(), Engine::ExitSource::Game);
    // Handler installed but always allowing, which is the no-policy shape.
    EXPECT_EQ(app.Run(game), 0);
    EXPECT_LT(game.Frames, 10) << "the run outlived a request nothing deferred";
}
