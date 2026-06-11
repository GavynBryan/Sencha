#include <core/assets/AssetManifest.h>
#include <core/assets/AssetPreloader.h>
#include <core/assets/AssetSystem.h>
#include <core/json/JsonParser.h>
#include <core/logging/LoggingProvider.h>
#include <jobs/AsyncTaskQueue.h>
#include <render/MaterialCache.h>
#include <runtime/RuntimeFrameLoop.h>
#include <world/registry/Registry.h>
#include <zone/AsyncZoneLoader.h>
#include <zone/ZoneRuntime.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace
{
    // One temp .smat on disk plus its registry record: the smallest real
    // asset the headless preload path can take from manifest to residency.
    class TempMaterialAsset
    {
    public:
        TempMaterialAsset(AssetRegistry& registry, std::string_view name,
                          std::string_view contents = R"({"version": 1})")
        {
            static int counter = 0;
            File = std::filesystem::temp_directory_path() /
                   ("sencha_preload_" + std::to_string(++counter) + ".smat");
            std::ofstream out(File, std::ios::trunc);
            out << contents;

            Path = "asset://materials/test/" + std::string(name) + ".smat";
            EXPECT_TRUE(registry.Register(AssetRecord{
                .Type = AssetType::Material,
                .SourceKind = AssetSourceKind::File,
                .Path = Path,
                .FilePath = File.generic_string(),
            }));
        }

        ~TempMaterialAsset()
        {
            std::error_code ec;
            std::filesystem::remove(File, ec);
        }

        std::string Path;
        std::filesystem::path File;
    };

    struct PreloadHarness
    {
        PreloadHarness()
            : Tasks(0)
            , Registry(Logging)
            , Materials()
            , Assets(Logging, Registry, nullptr, &Materials)
            , Preloader(Logging, Registry, Assets, Tasks)
        {
        }

        LoggingProvider Logging;
        AsyncTaskQueue Tasks;
        AssetRegistry Registry;
        MaterialCache Materials;
        AssetSystem Assets;
        AssetPreloader Preloader;
    };
}

// -- Manifest -------------------------------------------------------------------

TEST(AssetManifest, CollectFindsNestedUniquePathsInOrder)
{
    const std::optional<JsonValue> json = JsonParse(R"({
        "a": "asset://meshes/dev/cube.smesh",
        "nested": { "b": ["asset://materials/dev/red.smat", "not a path"],
                    "c": "asset://meshes/dev/cube.smesh" },
        "n": 7
    })");
    ASSERT_TRUE(json.has_value());

    const std::vector<std::string> paths = CollectAssetPaths(*json);
    ASSERT_EQ(paths.size(), 2u);
    EXPECT_EQ(paths[0], "asset://meshes/dev/cube.smesh");
    EXPECT_EQ(paths[1], "asset://materials/dev/red.smat");
}

TEST(AssetManifest, JsonRoundTrip)
{
    AssetManifest manifest;
    manifest.Paths = { "asset://a/b.smesh", "asset://c/d.smat" };

    AssetManifest parsed;
    std::string error;
    ASSERT_TRUE(ParseAssetManifestJson(AssetManifestToJson(manifest), parsed, &error)) << error;
    EXPECT_EQ(parsed.Paths, manifest.Paths);
}

TEST(AssetManifest, ParseRejectsBadVersionAndShape)
{
    AssetManifest parsed;
    const std::optional<JsonValue> wrongVersion =
        JsonParse(R"({"version": 9, "assets": []})");
    ASSERT_TRUE(wrongVersion.has_value());
    EXPECT_FALSE(ParseAssetManifestJson(*wrongVersion, parsed));

    const std::optional<JsonValue> badEntry =
        JsonParse(R"({"version": 1, "assets": ["no_prefix"]})");
    ASSERT_TRUE(badEntry.has_value());
    EXPECT_FALSE(ParseAssetManifestJson(*badEntry, parsed));
}

TEST(AssetManifest, LoadFileReportsMissing)
{
    AssetManifest manifest;
    std::string error;
    EXPECT_FALSE(LoadAssetManifestFile("does/not/exist.manifest.json", manifest, &error));
    EXPECT_FALSE(error.empty());
}

// -- AssetPreloader, zero-thread --------------------------------------------------

TEST(AssetPreload, MaterialsStreamToResidencyHeadless)
{
    PreloadHarness h;
    TempMaterialAsset red(h.Registry, "red", R"({"version": 1, "base_color_factor": [1, 0, 0, 1]})");
    TempMaterialAsset blue(h.Registry, "blue");

    const std::vector<std::string> paths{ red.Path, blue.Path };
    auto preload = h.Preloader.Begin(paths);

    EXPECT_FALSE(preload->IsComplete());
    EXPECT_EQ(preload->PendingCount(), 2u);
    EXPECT_FALSE(h.Materials.Find(red.Path).IsValid());

    EXPECT_EQ(h.Tasks.PumpWork(), 2u);
    EXPECT_EQ(h.Tasks.DrainCompletions(), 2u);

    EXPECT_TRUE(preload->IsComplete());
    EXPECT_EQ(preload->FailureCount(), 0u);
    EXPECT_EQ(preload->HeldHandleCount(), 2u);
    EXPECT_TRUE(h.Materials.Find(red.Path).IsValid());

    // The preload's handles are the only references: releasing them frees
    // the entries — proof the refcount accounting is exact.
    preload->ReleaseAll();
    EXPECT_FALSE(h.Materials.Find(red.Path).IsValid());
    EXPECT_FALSE(h.Materials.Find(blue.Path).IsValid());
}

TEST(AssetPreload, TwoPreloadsCoalesceOnOneLoad)
{
    PreloadHarness h;
    TempMaterialAsset shared(h.Registry, "shared");

    const std::vector<std::string> paths{ shared.Path };
    auto first = h.Preloader.Begin(paths);
    auto second = h.Preloader.Begin(paths);

    // One task serves both: the second preload joined the in-flight load.
    EXPECT_EQ(h.Tasks.PumpWork(), 1u);
    EXPECT_EQ(h.Tasks.DrainCompletions(), 1u);

    EXPECT_TRUE(first->IsComplete());
    EXPECT_TRUE(second->IsComplete());
    EXPECT_EQ(first->HeldHandleCount(), 1u);
    EXPECT_EQ(second->HeldHandleCount(), 1u);

    // Exactly two references exist: releasing both frees the entry.
    first->ReleaseAll();
    EXPECT_TRUE(h.Materials.Find(shared.Path).IsValid());
    second->ReleaseAll();
    EXPECT_FALSE(h.Materials.Find(shared.Path).IsValid());
}

TEST(AssetPreload, AlreadyResidentAssetsCompleteImmediately)
{
    PreloadHarness h;
    TempMaterialAsset red(h.Registry, "resident");

    MaterialHandle existing = h.Assets.LoadMaterial(red.Path);
    ASSERT_TRUE(existing.IsValid());

    const std::vector<std::string> paths{ red.Path };
    auto preload = h.Preloader.Begin(paths);
    EXPECT_TRUE(preload->IsComplete());
    EXPECT_EQ(preload->HeldHandleCount(), 1u);

    bool fired = false;
    preload->SetOnComplete([&] { fired = true; });
    EXPECT_TRUE(fired);

    preload->ReleaseAll();
    h.Assets.ReleaseMaterial(existing);
}

TEST(AssetPreload, MissingRecordsAndFilesCountAsFailuresNotDeadlocks)
{
    PreloadHarness h;
    TempMaterialAsset good(h.Registry, "good");

    // A path with no registry record, and a record whose file is gone.
    TempMaterialAsset doomed(h.Registry, "doomed");
    std::filesystem::remove(doomed.File);

    const std::vector<std::string> paths{ "asset://materials/test/unregistered.smat",
                                          good.Path, doomed.Path };
    auto preload = h.Preloader.Begin(paths);
    EXPECT_EQ(preload->FailureCount(), 1u); // unregistered, known at Begin

    (void)h.Tasks.PumpWork();
    (void)h.Tasks.DrainCompletions();

    EXPECT_TRUE(preload->IsComplete());
    EXPECT_EQ(preload->FailureCount(), 2u); // + the missing file
    EXPECT_EQ(preload->HeldHandleCount(), 1u);
    preload->ReleaseAll();
}

// -- Zone attach gating, zero-thread ----------------------------------------------

namespace
{
    struct ZoneHarness : PreloadHarness
    {
        ZoneHarness()
            : Loader(Tasks, Zones, Runtime)
        {
        }

        ZoneRuntime Zones;
        RuntimeFrameLoop Runtime;
        AsyncZoneLoader Loader;
    };
}

TEST(ZoneAssetGating, AttachDefersUntilPreloadCompletes)
{
    ZoneHarness h;

    // Wave 1: a mesh record whose file is missing (fails, still counts) so
    // the material is deferred to wave 2 — two drain rounds total.
    ASSERT_TRUE(h.Registry.Register(AssetRecord{
        .Type = AssetType::StaticMesh,
        .SourceKind = AssetSourceKind::File,
        .Path = "asset://meshes/test/missing.smesh",
        .FilePath = "does/not/exist.smesh",
    }));
    TempMaterialAsset material(h.Registry, "gated");

    const std::vector<std::string> paths{ "asset://meshes/test/missing.smesh", material.Path };
    auto preload = h.Preloader.Begin(paths);

    const ZoneId zone{ 5 };
    bool finalized = false;
    (void)h.Loader.BeginLoad(
        zone,
        [](Registry&) {},
        [&](Registry&) { finalized = true; },
        ZoneParticipation{ .Logic = true },
        preload);

    // Run the mesh work and the zone build.
    EXPECT_EQ(h.Tasks.PumpWork(), 2u);

    // Drain both commits: the mesh commit fails (wave 1 done, material
    // submitted), then the zone commit runs — and must defer the attach.
    EXPECT_EQ(h.Tasks.DrainCompletions(), 2u);
    EXPECT_FALSE(h.Zones.IsZoneLoaded(zone));
    EXPECT_FALSE(finalized);
    EXPECT_TRUE(h.Loader.IsLoading(zone));
    EXPECT_FALSE(preload->IsComplete());

    // The material (wave 2) lands: its commit completes the preload, which
    // fires the deferred attach — zone visible, finalize ran, scaffolding
    // handles released.
    EXPECT_EQ(h.Tasks.PumpWork(), 1u);
    EXPECT_EQ(h.Tasks.DrainCompletions(), 1u);
    EXPECT_TRUE(h.Zones.IsZoneLoaded(zone));
    EXPECT_TRUE(finalized);
    EXPECT_FALSE(h.Loader.IsLoading(zone));
    EXPECT_TRUE(preload->IsComplete());
    EXPECT_EQ(preload->HeldHandleCount(), 0u);
    EXPECT_FALSE(h.Materials.Find(material.Path).IsValid());

    EXPECT_EQ(h.Runtime.GetCurrentFrame().DiscontinuityReason,
              TemporalDiscontinuityReason::ZoneLoad);
}

TEST(ZoneAssetGating, CompletePreloadAttachesInline)
{
    ZoneHarness h;
    TempMaterialAsset material(h.Registry, "warm");

    const std::vector<std::string> paths{ material.Path };
    auto preload = h.Preloader.Begin(paths);

    // Let the asset land before the zone build is even submitted.
    (void)h.Tasks.PumpWork();
    (void)h.Tasks.DrainCompletions();
    ASSERT_TRUE(preload->IsComplete());

    const ZoneId zone{ 6 };
    (void)h.Loader.BeginLoad(zone, [](Registry&) {}, AsyncZoneLoader::FinalizeFn{},
                             ZoneParticipation{ .Logic = true }, preload);

    (void)h.Tasks.PumpWork();
    (void)h.Tasks.DrainCompletions();
    EXPECT_TRUE(h.Zones.IsZoneLoaded(zone));
    EXPECT_EQ(preload->HeldHandleCount(), 0u);
}

TEST(ZoneAssetGating, CancelledPreloadNeverBlocksAttach)
{
    ZoneHarness h;
    TempMaterialAsset material(h.Registry, "cancelled");

    const std::vector<std::string> paths{ material.Path };
    auto preload = h.Preloader.Begin(paths);
    preload->Cancel();

    const ZoneId zone{ 9 };
    (void)h.Loader.BeginLoad(zone, [](Registry&) {}, AsyncZoneLoader::FinalizeFn{},
                             ZoneParticipation{ .Logic = true }, preload);

    (void)h.Tasks.PumpWork();
    (void)h.Tasks.DrainCompletions();

    EXPECT_TRUE(h.Zones.IsZoneLoaded(zone));
    EXPECT_EQ(preload->HeldHandleCount(), 0u);
    // The cancelled preload's late commit released its reference instead of
    // storing it: nothing keeps the material alive.
    EXPECT_FALSE(h.Materials.Find(material.Path).IsValid());
}

// -- Threaded smoke (the one non-deterministic test, per the house pattern) ---------

#include <chrono>
#include <thread>

TEST(AssetPreload, ThreadedSmoke)
{
    using namespace std::chrono;
    using namespace std::chrono_literals;

    LoggingProvider logging;
    AsyncTaskQueue tasks(1);
    AssetRegistry registry(logging);
    MaterialCache materials;
    AssetSystem assets(logging, registry, nullptr, &materials);
    AssetPreloader preloader(logging, registry, assets, tasks);
    TempMaterialAsset material(registry, "threaded");

    const std::vector<std::string> paths{ material.Path };
    auto preload = preloader.Begin(paths);

    const auto deadline = steady_clock::now() + 5s;
    while (!preload->IsComplete() && steady_clock::now() < deadline)
    {
        (void)tasks.DrainCompletions();
        std::this_thread::sleep_for(1ms);
    }

    EXPECT_TRUE(preload->IsComplete());
    EXPECT_EQ(preload->FailureCount(), 0u);
    EXPECT_EQ(preload->HeldHandleCount(), 1u);
    preload->ReleaseAll();
}
