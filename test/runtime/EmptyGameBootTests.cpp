#include <gtest/gtest.h>

#include <app/Application.h>
#include <app/Engine.h>
#include <app/EngineSchedule.h>
#include <app/GameContexts.h>
#include <app/RuntimeContent.h>
#include <core/assets/AssetStoreTable.h>
#include <ecs/World.h>
#include <world/RuntimeWorld.h>

#include <SDL3/SDL.h>

#include <filesystem>
#include <string>

//=============================================================================
// The engine without a game.
//
// Every hook empty, no components registered, no systems, no participants, no
// map: the process still composes its content stack, mounts what it was
// configured to mount, runs frames, and shuts down clean. This is the floor a
// starter template stands on, and the thing that regresses first when engine
// code quietly starts depending on what a game happens to do.
//=============================================================================
namespace
{
    // Stops the run without the game registering a system of its own, so the
    // empty game stays literally empty.
    struct FrameCounterSystem
    {
        Engine* Host = nullptr;
        int* Frames = nullptr;
        // Observed during the run rather than after: the resource is withdrawn
        // at teardown, so a check afterwards cannot tell "published then
        // withdrawn" from "never published".
        bool* SawAssetStores = nullptr;
        int StopAfterFrames = 8;

        void FrameUpdate(FrameUpdateContext& ctx)
        {
            if (SawAssetStores != nullptr
                && ctx.Entities.TryGetResource<AssetStoreTable>() != nullptr)
            {
                *SawAssetStores = true;
            }
            if (Frames == nullptr)
                return;
            ++*Frames;
            if (*Frames >= StopAfterFrames && Host != nullptr)
                Host->RequestExit();
        }
    };

    // Empty in every hook a game can implement. The one system it registers is
    // the test's own stop condition, which the engine does not know about.
    class EmptyGame final : public Game
    {
    public:
        explicit EmptyGame(std::vector<std::string> roots)
            : Roots(std::move(roots))
        {
        }

        void OnConfigure(GameConfigureContext& ctx) override
        {
            ctx.Config.Window.GraphicsApi = WindowGraphicsApi::None;
            ctx.Config.Debug.ConsoleLogging = false;
            ctx.Config.Runtime.TargetFps = 1000.0;
            ctx.Config.Runtime.FixedTickRate = 120.0;
            ctx.Config.Runtime.ContentRoots = Roots;
        }

        void OnRegisterSystems(SystemRegisterContext& ctx) override
        {
            FrameCounterSystem& system = ctx.Schedule.Register<FrameCounterSystem>();
            system.Host = &GetEngine();
            system.Frames = &Frames;
            system.SawAssetStores = &SawAssetStores;
        }

        void OnShutdown(GameShutdownContext&) override
        {
            // The stack is still up here: this is the span in which a game is
            // allowed to hold references into it, and the last moment it may.
            MountedRootCount = GetEngine().Content().Roots().size();
        }

        std::vector<std::string> Roots;
        int Frames = 0;
        bool SawAssetStores = false;
        std::size_t MountedRootCount = 0;
    };

    // A content root with nothing in it: the shape a new project has before
    // anything is authored, and the one an engine test can rely on existing.
    class EmptyRoot
    {
    public:
        EmptyRoot()
            : Path(std::filesystem::temp_directory_path() / "sencha-empty-game-root")
        {
            std::filesystem::remove_all(Path);
            std::filesystem::create_directories(Path);
        }

        ~EmptyRoot()
        {
            std::error_code ec;
            std::filesystem::remove_all(Path, ec);
        }

        [[nodiscard]] std::string String() const { return Path.string(); }

    private:
        std::filesystem::path Path;
    };
}

TEST(EmptyGameBoot, AGameThatDoesNothingStillBootsMountsAndFrames)
{
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");

    const EmptyRoot root;
    Application app(0, nullptr);
    EmptyGame game({ root.String() });

    EXPECT_EQ(app.Run(game), 0);
    EXPECT_GE(game.Frames, 8);
    EXPECT_EQ(game.MountedRootCount, 1u)
        << "the engine mounts what it was configured to mount, with no game to ask";
    EXPECT_TRUE(game.SawAssetStores)
        << "the asset stores are published before the first frame, not by the game";
}

// The stack is the engine's for a bounded span, and the world stops naming it
// when that span ends. The engine is driven directly here rather than through
// Application, which destroys it inside Run and leaves nothing to inspect.
//
// Under ASan this is also the check that catches a consumer left connected to a
// destroyed cache.
TEST(EmptyGameBoot, TheContentStackIsWithdrawnFromTheWorldAtShutdown)
{
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");

    const EmptyRoot root;
    EmptyGame game({ root.String() });

    EngineConfig config;
    GameConfigureContext configure{ .Config = config };
    game.OnConfigure(configure);

    Engine engine(config);
    ASSERT_EQ(engine.Run(game), 0);
    ASSERT_TRUE(game.SawAssetStores);

    // Run has returned, so the caches are gone; a table still naming them is a
    // dangling read waiting to happen.
    const AssetStoreTable& stores =
        engine.World().Entities().GetResource<AssetStoreTable>();
    EXPECT_EQ(stores.Find(AssetType::StaticMesh, AssetArity::Single), nullptr);
    EXPECT_EQ(stores.Find(AssetType::Material, AssetArity::Single), nullptr);

    engine.Shutdown();
}

// A content root full of a game's assets, mounted by an engine with no game
// module at all. Nothing about scanning, classifying, or resolving content
// needs the game that authored it.
TEST(EmptyGameBoot, TheEngineMountsAGamesContentWithoutTheGame)
{
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");

    const std::filesystem::path templateAssets =
        std::filesystem::path(SENCHA_REPO_ROOT) / "templates" / "fps" / "assets";
    ASSERT_TRUE(std::filesystem::is_directory(templateAssets));

    Application app(0, nullptr);
    EmptyGame game({ templateAssets.string() });

    EXPECT_EQ(app.Run(game), 0);
    EXPECT_GE(game.Frames, 8);
}
