#include <anim/AnimationClipCache.h>
#include <anim/SkeletonCache.h>
#include <assets/animation/AnimationClipAssetLoader.h>
#include <assets/animation/AnimationClipSerializer.h>
#include <assets/audio_clip/AudioClipAssetLoader.h>
#include <assets/audio_clip/AudioClipSerializer.h>
#include <assets/data/DataAssetCache.h>
#include <assets/data/DataAssetLoader.h>
#include <assets/data/DataAssetTypeRegistry.h>
#include <assets/material/MaterialAssetLoader.h>
#include <assets/skeleton/SkeletonAssetLoader.h>
#include <assets/skeleton/SkeletonSerializer.h>
#include <audio/AudioClipCache.h>
#include <authored/VerbBindingData.h>
#include <core/assets/AssetLease.h>
#include <core/assets/AssetManifest.h>
#include <assets/runtime/AssetPreloader.h>
#include <assets/runtime/AssetSystem.h>
#include <assets/runtime/RegisterAssetKind.h>
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

    // A verb-binding data asset whose one binding names `asset` through the
    // `asset` source: a reference written before any verb says its kind, which
    // the asset's compile therefore reports as a dependency of no type.
    class TempBindingAsset
    {
    public:
        TempBindingAsset(AssetRegistry& registry, std::string_view name, std::string_view asset)
        {
            File = TempAssetPath("sencha_preload_bindings", ".sdata");
            Write(asset);
            Path = "asset://data/test/" + std::string(name) + ".sdata";
            EXPECT_TRUE(registry.Register(AssetRecord{
                .Type = AssetType::Data,
                .SourceKind = AssetSourceKind::File,
                .Path = Path,
                .FilePath = File.generic_string(),
            }));
        }

        ~TempBindingAsset()
        {
            std::error_code ec;
            std::filesystem::remove(File, ec);
        }

        void Write(std::string_view asset, std::string_view source = "asset") const
        {
            std::ofstream out(File, std::ios::trunc);
            out << R"({ "type": "authored.bindings", "version": 1, "data": { "bindings": [ {
                "key": "look", "verb": "test.look",
                "arguments": { "Look": { ")" << source << R"(": ")" << asset << R"(" } } } ] } })";
        }

        std::string Path;
        std::filesystem::path File;
    };

    struct PreloadHarness
    {
        PreloadHarness()
        {
            RegisterAssetKind(Assets, AssetType::Material, MaterialLoader, &Materials);
            RegisterAssetKind(Assets, AssetType::Audio, AudioClipLoader, &AudioClips);
            RegisterAssetKind(Assets, AssetType::Skeleton, SkeletonLoader, &Skeletons);
            RegisterAssetKind(Assets, AssetType::AnimationClip, AnimationClipLoader, &AnimationClips);
            RegisterVerbBindingData(DataTypes, DataSchemas);
            RegisterAssetKind(Assets, AssetType::Data, DataLoader, &DataAssets);
        }

        // Rounds until the preload has nothing left to do.
        void Settle(const AssetPreload& preload)
        {
            for (int round = 0; round < 8 && !preload.IsComplete(); ++round)
            {
                (void)Tasks.PumpWork();
                (void)Tasks.DrainCompletions();
            }
        }

        [[nodiscard]] std::string FailureOf(std::string_view path) const
        {
            for (const AssetSystem::LoadFailure& failure : Assets.LoadFailures())
                if (failure.Path == path)
                    return failure.Reason;
            return {};
        }

        LoggingProvider Logging;
        AsyncTaskQueue Tasks{ 0 };
        AssetRegistry Registry{ Logging };
        MaterialCache Materials;
        AudioClipCache AudioClips{ Logging };
        SkeletonCache Skeletons;
        AnimationClipCache AnimationClips;
        MaterialAssetLoader MaterialLoader{ Logging, &Materials, nullptr };
        AudioClipAssetLoader AudioClipLoader{ Logging, &AudioClips };
        SkeletonAssetLoader SkeletonLoader{ Logging, &Skeletons };
        AnimationClipAssetLoader AnimationClipLoader{ Logging, &AnimationClips, &Skeletons };
        // After the stores its values lease from, so it lets go of them first.
        DataAssetTypeRegistry DataTypes;
        DataSchemaRegistry DataSchemas;
        DataAssetCache DataAssets;
        DataAssetLoader DataLoader{ Logging, &DataTypes, &DataSchemas, &DataAssets };
        AssetSystem Assets{ Logging, Registry };
        AssetPreloader Preloader{ Logging, Registry, Assets, Tasks };
    };
}

// -- Manifest ----------------------------------------------------------------

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

    const AssetLease resident = h.Assets.LoadLease(skeleton.Path, AssetType::Skeleton);
    ASSERT_TRUE(resident.IsValid());

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

// -- Dependencies of no stated kind -------------------------------------------
//
// A verb binding names an asset before any verb has said what kind it takes, so
// its compile reports the dependency with no type. Both loads take such a
// dependency as whatever is at its path -- the binding's own compile later checks
// that kind against the verb -- and hold it as long as the asset naming it lives.

TEST(AssetDependencies, AnUntypedDependencyLoadsSynchronouslyAndGoesWithItsDependent)
{
    PreloadHarness h;
    TempMaterialAsset look(h.Registry, "look");
    TempBindingAsset bindings(h.Registry, "uses_look", look.Path);
    {
        const AssetLease lease = h.Assets.LoadLease(bindings.Path, AssetType::Data);
        ASSERT_TRUE(lease.IsValid()) << h.FailureOf(bindings.Path);
        EXPECT_TRUE(h.Materials.Find(look.Path).IsValid()) << "held by the asset that names it";
    }
    EXPECT_FALSE(h.DataAssets.Find(bindings.Path).IsValid());
    EXPECT_FALSE(h.Materials.Find(look.Path).IsValid()) << "and let go of with it";
}

TEST(AssetDependencies, AnUntypedDependencyOnNothingFailsTheSynchronousLoadSayingWhich)
{
    PreloadHarness h;
    TempBindingAsset bindings(h.Registry, "dangling", "asset://materials/test/nowhere.smat");
    EXPECT_FALSE(h.Assets.LoadLease(bindings.Path, AssetType::Data).IsValid());
    EXPECT_NE(h.FailureOf("asset://materials/test/nowhere.smat").find("in no mounted content root"),
              std::string::npos);
    EXPECT_NE(h.FailureOf(bindings.Path).find("asset://materials/test/nowhere.smat"), std::string::npos)
        << h.FailureOf(bindings.Path);
}

TEST(AssetDependencies, AnUntypedDependencyIsPreloadedAndHeldRatherThanSkipped)
{
    PreloadHarness h;
    TempMaterialAsset look(h.Registry, "look");
    TempBindingAsset bindings(h.Registry, "uses_look", look.Path);

    auto preload = h.Preloader.Begin(std::vector<std::string>{ bindings.Path });
    h.Settle(*preload);
    ASSERT_TRUE(preload->IsComplete());
    EXPECT_EQ(preload->FailureCount(), 0u);
    EXPECT_TRUE(h.DataAssets.Find(bindings.Path).IsValid());
    EXPECT_TRUE(h.Materials.Find(look.Path).IsValid());

    preload->ReleaseAll();
    EXPECT_FALSE(h.DataAssets.Find(bindings.Path).IsValid());
    EXPECT_FALSE(h.Materials.Find(look.Path).IsValid());
}

TEST(AssetDependencies, AnUntypedDependencyOnNothingFailsThePreloadRatherThanVanishing)
{
    PreloadHarness h;
    TempBindingAsset bindings(h.Registry, "dangling", "asset://materials/test/nowhere.smat");

    auto preload = h.Preloader.Begin(std::vector<std::string>{ bindings.Path });
    h.Settle(*preload);
    ASSERT_TRUE(preload->IsComplete());
    EXPECT_EQ(preload->FailureCount(), 1u);
    EXPECT_FALSE(h.DataAssets.Find(bindings.Path).IsValid());
    EXPECT_NE(h.FailureOf(bindings.Path).find("in no mounted content root"), std::string::npos)
        << h.FailureOf(bindings.Path);
}

// A kind the reference does state is kept, so naming an asset of another kind
// fails, and says both kinds.
TEST(AssetDependencies, ADependencyOfTheWrongStatedKindFailsSayingBothKinds)
{
    PreloadHarness h;
    TempMaterialAsset look(h.Registry, "look");
    TempBindingAsset bindings(h.Registry, "misnamed", look.Path);
    bindings.Write(look.Path, "data");

    auto preload = h.Preloader.Begin(std::vector<std::string>{ bindings.Path });
    h.Settle(*preload);
    ASSERT_TRUE(preload->IsComplete());
    EXPECT_EQ(preload->FailureCount(), 1u);
    EXPECT_FALSE(h.Materials.Find(look.Path).IsValid());
    EXPECT_NE(h.FailureOf(bindings.Path).find("as a Data asset, but it is a Material"), std::string::npos)
        << h.FailureOf(bindings.Path);

    EXPECT_FALSE(h.Assets.LoadLease(bindings.Path, AssetType::Data).IsValid()) << "the synchronous load agrees";
}

TEST(AssetDependencies, ACancelledPreloadLeavesNeitherAnAssetNorItsDependencyResident)
{
    PreloadHarness h;
    TempMaterialAsset look(h.Registry, "look");
    TempBindingAsset bindings(h.Registry, "uses_look", look.Path);

    auto preload = h.Preloader.Begin(std::vector<std::string>{ bindings.Path });
    // The bindings stage and wait on the material their compile named.
    (void)h.Tasks.PumpWork();
    (void)h.Tasks.DrainCompletions();
    preload->Cancel();
    for (int round = 0; round < 8; ++round)
    {
        (void)h.Tasks.PumpWork();
        (void)h.Tasks.DrainCompletions();
    }
    EXPECT_EQ(preload->HeldHandleCount(), 0u);
    EXPECT_FALSE(h.DataAssets.Find(bindings.Path).IsValid());
    EXPECT_FALSE(h.Materials.Find(look.Path).IsValid());
}

TEST(AssetDependencies, AReloadHoldsWhatItsContentNowNamesAndLetsGoOfTheRest)
{
    PreloadHarness h;
    TempMaterialAsset before(h.Registry, "before");
    TempMaterialAsset after(h.Registry, "after");
    TempBindingAsset bindings(h.Registry, "look", before.Path);
    const AssetLease lease = h.Assets.LoadLease(bindings.Path, AssetType::Data);
    ASSERT_TRUE(lease.IsValid());
    ASSERT_TRUE(h.Materials.Find(before.Path).IsValid());

    bindings.Write(after.Path);
    h.Assets.StageReload(*h.Registry.FindByPath(bindings.Path), h.Tasks);
    (void)h.Tasks.PumpWork();
    (void)h.Tasks.DrainCompletions();
    EXPECT_TRUE(lease.IsValid()) << "reloaded in place, under the same lease";
    EXPECT_TRUE(h.Materials.Find(after.Path).IsValid());
    EXPECT_FALSE(h.Materials.Find(before.Path).IsValid());

    // A reload naming nothing does not take: what loaded before stays, and why is said.
    bindings.Write("asset://materials/test/nowhere.smat");
    h.Assets.StageReload(*h.Registry.FindByPath(bindings.Path), h.Tasks);
    (void)h.Tasks.PumpWork();
    (void)h.Tasks.DrainCompletions();
    EXPECT_TRUE(h.Materials.Find(after.Path).IsValid());
    EXPECT_NE(h.FailureOf("asset://materials/test/nowhere.smat").find("in no mounted content root"),
              std::string::npos);
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

    const AssetLease existing = h.Assets.LoadLease(red.Path, AssetType::Material);
    ASSERT_TRUE(existing.IsValid());

    const std::vector<std::string> paths{ red.Path };
    auto preload = h.Preloader.Begin(paths);
    EXPECT_TRUE(preload->IsComplete());
    EXPECT_EQ(preload->HeldHandleCount(), 1u);

    bool fired = false;
    preload->SetOnComplete([&] { fired = true; });
    EXPECT_TRUE(fired);

    preload->ReleaseAll();
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
    MaterialAssetLoader materialLoader(logging, &materials, nullptr);
    AssetSystem assets(logging, registry);
    RegisterAssetKind(assets, AssetType::Material, materialLoader, &materials);
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
