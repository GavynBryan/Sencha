#include <app/Application.h>
#include <app/Engine.h>
#include <assets/runtime/RuntimeAssets.h>
#include <assets/scene/SceneCache.h>
#include <jobs/AsyncTaskQueue.h>
#include <runtime/spawn/SceneSpawnService.h>

#include <gtest/gtest.h>
#include <SDL3/SDL.h>

#include <semaphore>

namespace
{
    class SpawningExitGame final : public Game
    {
    public:
        void OnConfigure(GameConfigureContext& ctx) override
        {
            ctx.Config.Window.GraphicsApi = WindowGraphicsApi::None;
            ctx.Config.Debug.ConsoleLogging = false;
            ctx.Config.Runtime.AsyncTaskThreadCount = 1;
            ctx.Config.Runtime.JobWorkerCount = 0;
            ctx.Config.Runtime.ContentRoots.clear();
        }

        void OnStart(GameStartupContext&) override
        {
            Engine& engine = GetEngine();
            RuntimeAssets& assets = engine.Content().Assets();
            EXPECT_TRUE(assets.Registry.Register(AssetRecord{
                .Type = AssetType::Scene,
                .SourceKind = AssetSourceKind::File,
                .Path = Path,
            }));
            SmapContents contents;
            contents.Entities.emplace_back();
            const SceneHandle scene = assets.Scenes.Register(Path, std::move(contents));
            Spawn = engine.Spawns().RequestSpawn(Path, Transform3f::Identity());
            assets.Scenes.Release(scene);
            // One worker: this fence follows the scene build, while no frame
            // runs to commit it. Its residency is now held only by task work.
            (void)engine.Tasks().Submit<int>(
                [this] { Built.release(); return 0; }, [](int) {});
            engine.RequestExit();
        }

        void OnShutdown(GameShutdownContext&) override
        {
            Built.acquire();
            EXPECT_TRUE(GetEngine().Spawns().RequestDespawn(Spawn));
        }

        void OnUnregisterDataAssetTypes(DataAssetTypeRegistry&,
                                       DataSchemaRegistry&) override
        {
            Engine& engine = GetEngine();
            const bool resident = engine.Content().Assets().Scenes.Find(Path).IsValid();
            EXPECT_FALSE(resident)
                << "unfinished scene work retained a cache lease into content teardown";
            Checked = true;
            // Keep a failed regression from releasing the lease after its
            // cache has gone. This observes the failure before cleaning up.
            if (resident)
            {
                (void)engine.Tasks().DrainCompletions();
                engine.Spawns().Pump();
            }
        }

        bool Checked = false;

    private:
        static constexpr const char* Path = "asset://shutdown/pending.smap";
        std::binary_semaphore Built{ 0 };
        SceneSpawnId Spawn;
    };
}

TEST(SceneSpawnShutdown, ReleasesUncommittedBuildsBeforeContentTeardown)
{
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    Application app(0, nullptr);
    SpawningExitGame game;
    EXPECT_EQ(app.Run(game), 0);
    EXPECT_TRUE(game.Checked);
}
