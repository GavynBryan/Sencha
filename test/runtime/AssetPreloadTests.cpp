#include <anim/AnimationClipCache.h>
#include <anim/SkeletonCache.h>
#include <assets/animation/AnimationClipSerializer.h>
#include <assets/audio_clip/AudioClipSerializer.h>
#include <assets/skeleton/SkeletonSerializer.h>
#include <audio/AudioClipCache.h>
#include <core/assets/AssetManifest.h>
#include <assets/runtime/AssetPreloader.h>
#include <assets/runtime/AssetSystem.h>
#include <core/json/JsonParser.h>
#include <core/logging/LoggingProvider.h>
#include <jobs/AsyncTaskQueue.h>
#include <render/MaterialCache.h>
#include <ecs/WorldComponentSchema.h>
#include <runtime/RuntimeFrameLoop.h>
#include <world/RuntimeWorld.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/serialization/SceneSerializationContext.h>
#include <zone/AsyncZoneLoader.h>
#include <world/build/EntityBuildPackage.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    // ctest runs each case in its own process, so a per-process counter alone
    // repeats the same names in every case and two cases running in parallel
    // would write and delete each other's file. The case name disambiguates.
    std::filesystem::path TempAssetPath(std::string_view prefix, std::string_view extension)
    {
        static int counter = 0;
        std::string caseName = "unknown";
        if (const auto* info = testing::UnitTest::GetInstance()->current_test_info())
            caseName = std::string(info->test_suite_name()) + "_" + info->name();

        const std::string name = std::string(prefix) + "_" + caseName + "_"
            + std::to_string(++counter) + std::string(extension);
        return std::filesystem::temp_directory_path() / name;
    }

    // One temp .smat on disk plus its registry record: the smallest real
    // asset the headless preload path can take from manifest to residency.
    class TempMaterialAsset
    {
    public:
        TempMaterialAsset(AssetRegistry& registry, std::string_view name,
                          std::string_view contents = R"({"version": 2})")
        {
            File = TempAssetPath("sencha_preload_material", ".smat");
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

    // One temp .sclip on disk plus its registry record. This is the audio
    // analogue of TempMaterialAsset, and the whole round trip is CPU-side.
    class TempAudioClipAsset
    {
    public:
        TempAudioClipAsset(AssetRegistry& registry, std::string_view name)
        {
            AudioClip clip;
            clip.SampleRate = 22050;
            clip.ChannelCount = 1;
            clip.Samples = { 100, 200, 300, 400 };
            std::vector<std::byte> bytes;
            EXPECT_TRUE(WriteSclipToBytes(clip, bytes));

            File = TempAssetPath("sencha_preload_audio", ".sclip");
            std::ofstream out(File, std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char*>(bytes.data()),
                      static_cast<std::streamsize>(bytes.size()));

            Path = "asset://audio/test/" + std::string(name) + ".sclip";
            EXPECT_TRUE(registry.Register(AssetRecord{
                .Type = AssetType::Audio,
                .SourceKind = AssetSourceKind::File,
                .Path = Path,
                .FilePath = File.generic_string(),
            }));
        }

        ~TempAudioClipAsset()
        {
            std::error_code ec;
            std::filesystem::remove(File, ec);
        }

        std::string Path;
        std::filesystem::path File;
    };

    // One temp .sskel on disk plus its registry record. Clips and skinned
    // meshes name one, which is the dependency edge the preloader orders on.
    class TempSkeletonAsset
    {
    public:
        TempSkeletonAsset(AssetRegistry& registry, std::string_view name)
        {
            SkeletonData skeleton;
            skeleton.Joints.push_back(SkeletonJoint{});

            std::vector<std::byte> bytes;
            EXPECT_TRUE(WriteSskelToBytes(skeleton, bytes));

            File = TempAssetPath("sencha_preload_skeleton", ".sskel");
            std::ofstream out(File, std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char*>(bytes.data()),
                      static_cast<std::streamsize>(bytes.size()));

            Path = "asset://skeletons/test/" + std::string(name) + ".sskel";
            EXPECT_TRUE(registry.Register(AssetRecord{
                .Type = AssetType::Skeleton,
                .SourceKind = AssetSourceKind::File,
                .Path = Path,
                .FilePath = File.generic_string(),
            }));
        }

        ~TempSkeletonAsset()
        {
            std::error_code ec;
            std::filesystem::remove(File, ec);
        }

        std::string Path;
        std::filesystem::path File;
    };

    // A valid .sanim whose skeleton path the fixture supplies, so a test can
    // point it at a skeleton that resolves or one that does not.
    class TempAnimationClipAsset
    {
    public:
        TempAnimationClipAsset(AssetRegistry& registry,
                               std::string_view name,
                               std::string_view skeletonPath)
        {
            AnimationClipData clip;
            clip.SkeletonPath = std::string(skeletonPath);
            clip.DurationSeconds = 1.0f;

            AnimationJointTrack track;
            track.JointIndex = 0;
            track.Path = AnimationChannelPath::Translation;
            track.Interpolation = AnimationInterpolation::Linear;
            track.TimesSeconds = { 0.0f, 1.0f };
            track.Values = { 0.0f, 0.0f, 0.0f,
                             0.0f, 0.0f, 0.0f };
            clip.Tracks.push_back(std::move(track));

            std::vector<std::byte> bytes;
            EXPECT_TRUE(WriteSanimToBytes(clip, bytes));

            File = TempAssetPath("sencha_preload_animation", ".sanim");
            std::ofstream out(File, std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char*>(bytes.data()),
                      static_cast<std::streamsize>(bytes.size()));

            Path = "asset://animation/test/" + std::string(name) + ".sanim";
            EXPECT_TRUE(registry.Register(AssetRecord{
                .Type = AssetType::AnimationClip,
                .SourceKind = AssetSourceKind::File,
                .Path = Path,
                .FilePath = File.generic_string(),
            }));
        }

        ~TempAnimationClipAsset()
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
            , AudioClips(Logging)
            , Skeletons()
            , AnimationClips()
            , Assets(Logging, Registry, nullptr, &Materials, nullptr, &AudioClips,
                     &Skeletons, &AnimationClips)
            , Preloader(Logging, Registry, Assets, Tasks)
        {
        }

        LoggingProvider Logging;
        AsyncTaskQueue Tasks;
        AssetRegistry Registry;
        MaterialCache Materials;
        AudioClipCache AudioClips;
        SkeletonCache Skeletons;
        AnimationClipCache AnimationClips;
        AssetSystem Assets;
        AssetPreloader Preloader;
    };
}

// -- Manifest ----------------------------------------------------------------

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

TEST(AssetManifest, ResolvePathsPrefersIdOverStalePath)
{
    LoggingProvider logging;
    AssetRegistry registry(logging);

    AssetRecord record;
    record.Type = AssetType::StaticMesh;
    record.SourceKind = AssetSourceKind::File;
    record.Path = "asset://meshes/renamed.smesh";
    record.FilePath = "renamed.smesh";
    ASSERT_TRUE(registry.Register(record));

    const AssetId id{ 0x1111222233334444ull };
    ASSERT_TRUE(registry.AssignId(record.Path, id));

    AssetManifest manifest;
    manifest.Entries = { { id, "asset://meshes/old_name.smesh" },
                         { AssetId{ 0x9999888877776666ull }, "asset://meshes/unknown_id.smesh" },
                         { AssetId{}, "asset://meshes/no_id.smesh" } };

    const std::vector<std::string> paths = ResolveManifestPaths(manifest, registry);
    ASSERT_EQ(paths.size(), 3u);
    EXPECT_EQ(paths[0], "asset://meshes/renamed.smesh");
    EXPECT_EQ(paths[1], "asset://meshes/unknown_id.smesh");
    EXPECT_EQ(paths[2], "asset://meshes/no_id.smesh");
}

// -- AssetPreloader, zero-thread ---------------------------------------------

TEST(AssetPreload, MaterialsStreamToResidencyHeadless)
{
    PreloadHarness h;
    TempMaterialAsset red(h.Registry, "red", R"({"version": 2, "base_color_factor": [1, 0, 0, 1]})");
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

    // The preload's handles are the only references. Releasing them frees
    // the entries, proving the refcount accounting is exact.
    preload->ReleaseAll();
    EXPECT_FALSE(h.Materials.Find(red.Path).IsValid());
    EXPECT_FALSE(h.Materials.Find(blue.Path).IsValid());
}

TEST(AssetPreload, IndependentRootsStageTogether)
{
    PreloadHarness h;
    TempAudioClipAsset blip(h.Registry, "blip");
    TempMaterialAsset red(h.Registry, "red");

    const std::vector<std::string> paths{ blip.Path, red.Path };
    auto preload = h.Preloader.Begin(paths);
    EXPECT_EQ(preload->PendingCount(), 2u);

    // Neither declares a dependency, so both stage in the same round. Only a
    // declared edge delays a commit; kind alone never does.
    EXPECT_EQ(h.Tasks.PumpWork(), 2u);
    EXPECT_EQ(h.Tasks.DrainCompletions(), 2u);
    EXPECT_TRUE(h.AudioClips.Find(blip.Path).IsValid());
    EXPECT_TRUE(h.Materials.Find(red.Path).IsValid());
    EXPECT_TRUE(preload->IsComplete());
    EXPECT_EQ(preload->FailureCount(), 0u);
    EXPECT_EQ(preload->HeldHandleCount(), 2u);

    preload->ReleaseAll();
    EXPECT_FALSE(h.AudioClips.Find(blip.Path).IsValid());
    EXPECT_FALSE(h.Materials.Find(red.Path).IsValid());
}

TEST(AssetPreload, DeclaredDependencyCommitsBeforeItsDependent)
{
    PreloadHarness h;
    TempSkeletonAsset skeleton(h.Registry, "rig");
    TempAnimationClipAsset clip(h.Registry, "walk", skeleton.Path);

    // Only the clip is in the manifest: the skeleton is reached through the
    // dependency the clip's staging declares.
    const std::vector<std::string> paths{ clip.Path };
    auto preload = h.Preloader.Begin(paths);
    EXPECT_EQ(preload->PendingCount(), 1u);

    // Round one stages the clip and discovers the skeleton it needs. The
    // clip cannot commit yet, so neither is resident.
    EXPECT_EQ(h.Tasks.PumpWork(), 1u);
    EXPECT_EQ(h.Tasks.DrainCompletions(), 1u);
    EXPECT_FALSE(h.AnimationClips.Find(clip.Path).IsValid());
    EXPECT_FALSE(preload->IsComplete());

    // Round two commits the skeleton, which releases the clip's commit in the
    // same drain. Both land, and the clip holds its own skeleton reference.
    EXPECT_EQ(h.Tasks.PumpWork(), 1u);
    EXPECT_EQ(h.Tasks.DrainCompletions(), 1u);
    EXPECT_TRUE(h.Skeletons.Find(skeleton.Path).IsValid());
    EXPECT_TRUE(h.AnimationClips.Find(clip.Path).IsValid());
    EXPECT_TRUE(preload->IsComplete());
    EXPECT_EQ(preload->FailureCount(), 0u);

    // The preload holds only the clip: the skeleton stays resident on the
    // clip's reference alone, and both free once that goes.
    EXPECT_EQ(preload->HeldHandleCount(), 1u);
    preload->ReleaseAll();
    EXPECT_FALSE(h.AnimationClips.Find(clip.Path).IsValid());
    EXPECT_FALSE(h.Skeletons.Find(skeleton.Path).IsValid());
}

TEST(AssetPreload, AlreadyResidentDependencyCommitsImmediately)
{
    PreloadHarness h;
    TempSkeletonAsset skeleton(h.Registry, "rig");
    TempAnimationClipAsset clip(h.Registry, "walk", skeleton.Path);

    ASSERT_TRUE(h.Assets.LoadSkeleton(skeleton.Path).IsValid());

    const std::vector<std::string> paths{ clip.Path };
    auto preload = h.Preloader.Begin(paths);

    // The dependency resolves against the cache, so no second round is needed.
    EXPECT_EQ(h.Tasks.PumpWork(), 1u);
    EXPECT_EQ(h.Tasks.DrainCompletions(), 1u);
    EXPECT_TRUE(h.AnimationClips.Find(clip.Path).IsValid());
    EXPECT_TRUE(preload->IsComplete());
    EXPECT_EQ(preload->FailureCount(), 0u);
}

TEST(AssetPreload, FailedDependencyFailsItsDependentWithoutDeadlock)
{
    PreloadHarness h;
    ASSERT_TRUE(h.Registry.Register(AssetRecord{
        .Type = AssetType::Skeleton,
        .SourceKind = AssetSourceKind::File,
        .Path = "asset://skeletons/test/missing.sskel",
        .FilePath = "does/not/exist.sskel",
    }));
    TempAnimationClipAsset clip(h.Registry, "orphan", "asset://skeletons/test/missing.sskel");

    const std::vector<std::string> paths{ clip.Path };
    auto preload = h.Preloader.Begin(paths);

    EXPECT_EQ(h.Tasks.PumpWork(), 1u);
    EXPECT_EQ(h.Tasks.DrainCompletions(), 1u);
    EXPECT_FALSE(preload->IsComplete());

    // The skeleton fails to stage; the failure propagates to the clip rather
    // than leaving it waiting forever.
    EXPECT_EQ(h.Tasks.PumpWork(), 1u);
    EXPECT_EQ(h.Tasks.DrainCompletions(), 1u);
    EXPECT_TRUE(preload->IsComplete());
    EXPECT_EQ(preload->FailureCount(), 1u);
    EXPECT_FALSE(h.AnimationClips.Find(clip.Path).IsValid());
}

TEST(AssetPreload, DependencyOnAnUnregisteredPathFailsTheDependent)
{
    PreloadHarness h;
    TempAnimationClipAsset clip(h.Registry, "dangling", "asset://skeletons/test/nowhere.sskel");

    const std::vector<std::string> paths{ clip.Path };
    auto preload = h.Preloader.Begin(paths);

    EXPECT_EQ(h.Tasks.PumpWork(), 1u);
    EXPECT_EQ(h.Tasks.DrainCompletions(), 1u);
    EXPECT_TRUE(preload->IsComplete());
    EXPECT_EQ(preload->FailureCount(), 1u);
}

TEST(AssetPreload, TwoPreloadsCoalesceOnOneLoad)
{
    PreloadHarness h;
    TempMaterialAsset shared(h.Registry, "shared");

    const std::vector<std::string> paths{ shared.Path };
    auto first = h.Preloader.Begin(paths);
    auto second = h.Preloader.Begin(paths);

    EXPECT_EQ(h.Tasks.PumpWork(), 1u);
    EXPECT_EQ(h.Tasks.DrainCompletions(), 1u);

    EXPECT_TRUE(first->IsComplete());
    EXPECT_TRUE(second->IsComplete());
    EXPECT_EQ(first->HeldHandleCount(), 1u);
    EXPECT_EQ(second->HeldHandleCount(), 1u);

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

    TempMaterialAsset doomed(h.Registry, "doomed");
    std::filesystem::remove(doomed.File);

    const std::vector<std::string> paths{ "asset://materials/test/unregistered.smat",
                                          good.Path, doomed.Path };
    auto preload = h.Preloader.Begin(paths);
    EXPECT_EQ(preload->FailureCount(), 1u);

    (void)h.Tasks.PumpWork();
    (void)h.Tasks.DrainCompletions();

    EXPECT_TRUE(preload->IsComplete());
    EXPECT_EQ(preload->FailureCount(), 2u);
    EXPECT_EQ(preload->HeldHandleCount(), 1u);
    preload->ReleaseAll();
}

// -- Zone attach gating, zero-thread -----------------------------------------

namespace
{
    struct ZoneHarness : PreloadHarness
    {
        static WorldComponentSchema MakeSchema()
        {
            WorldComponentSchema schema;
            schema.Seal();
            return schema;
        }

        ZoneHarness()
            : Schema(MakeSchema())
            , World(Schema)
            , SceneContext(Logging, &Assets)
            , Loader(Tasks, World, Schema, Serializers, SceneContext, Runtime)
        {
        }

        WorldComponentSchema Schema;
        RuntimeWorld World;
        ComponentSerializerRegistry Serializers;
        SceneSerializationContext SceneContext;
        RuntimeFrameLoop Runtime;
        AsyncZoneLoader Loader;
    };
}

TEST(ZoneAssetGating, AttachDefersUntilDependencyChainCompletes)
{
    ZoneHarness h;

    // A clip whose skeleton commits a round later, so the preload spans two
    // drain rounds and the deferred attach has something real to wait on.
    TempSkeletonAsset skeleton(h.Registry, "gated");
    TempAnimationClipAsset clip(h.Registry, "gated", skeleton.Path);

    const std::vector<std::string> paths{ clip.Path };
    auto preload = h.Preloader.Begin(paths);

    const ZoneId zone{ 5 };
    bool finalized = false;
    (void)h.Loader.BeginLoad(
        zone,
        [](EntityBuildPackage&) {},
        [&](RuntimeWorld&, RuntimeZoneRecord&) { finalized = true; return true; },
        ZoneParticipation{ .Logic = true },
        preload);

    // The clip staging and the zone build, then both commits: the clip's
    // staging discovers its skeleton and submits it, and the zone commit must
    // defer the attach because the preload is still short one asset.
    EXPECT_EQ(h.Tasks.PumpWork(), 2u);

    EXPECT_EQ(h.Tasks.DrainCompletions(), 2u);
    EXPECT_FALSE(h.World.IsZoneResident(zone));
    EXPECT_FALSE(finalized);
    EXPECT_TRUE(h.Loader.IsLoading(zone));
    EXPECT_FALSE(preload->IsComplete());

    // The skeleton lands, releasing the clip's commit in the same drain. That
    // completes the preload, which fires the deferred attach.
    EXPECT_EQ(h.Tasks.PumpWork(), 1u);
    EXPECT_EQ(h.Tasks.DrainCompletions(), 1u);
    EXPECT_TRUE(h.World.IsZoneResident(zone));
    EXPECT_TRUE(finalized);
    EXPECT_FALSE(h.Loader.IsLoading(zone));
    EXPECT_TRUE(preload->IsComplete());
    EXPECT_EQ(preload->FailureCount(), 0u);
    EXPECT_EQ(preload->HeldHandleCount(), 0u);
    EXPECT_FALSE(h.AnimationClips.Find(clip.Path).IsValid());

    EXPECT_EQ(h.Runtime.GetCurrentFrame().DiscontinuityReason,
              TemporalDiscontinuityReason::ZoneLoad);
}

TEST(ZoneAssetGating, CompletePreloadAttachesInline)
{
    ZoneHarness h;
    TempMaterialAsset material(h.Registry, "warm");

    const std::vector<std::string> paths{ material.Path };
    auto preload = h.Preloader.Begin(paths);

    (void)h.Tasks.PumpWork();
    (void)h.Tasks.DrainCompletions();
    ASSERT_TRUE(preload->IsComplete());

    const ZoneId zone{ 6 };
    (void)h.Loader.BeginLoad(zone, [](EntityBuildPackage&) {}, AsyncZoneLoader::FinalizeFn{},
                             ZoneParticipation{ .Logic = true }, preload);

    (void)h.Tasks.PumpWork();
    (void)h.Tasks.DrainCompletions();
    EXPECT_TRUE(h.World.IsZoneResident(zone));
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
    (void)h.Loader.BeginLoad(zone, [](EntityBuildPackage&) {}, AsyncZoneLoader::FinalizeFn{},
                             ZoneParticipation{ .Logic = true }, preload);

    (void)h.Tasks.PumpWork();
    (void)h.Tasks.DrainCompletions();

    EXPECT_TRUE(h.World.IsZoneResident(zone));
    EXPECT_EQ(preload->HeldHandleCount(), 0u);
    EXPECT_FALSE(h.Materials.Find(material.Path).IsValid());
}

// -- Threaded smoke -----------------------------------------------------------

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
